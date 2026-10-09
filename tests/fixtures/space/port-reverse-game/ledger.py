#!/usr/bin/env python3
"""Reviewed fixture-specific GG-to-GB physical inventory and replacements."""
import argparse,hashlib,importlib.util,json,re
from pathlib import Path
sha=lambda b:hashlib.sha256(b).hexdigest()
ROOT=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('forward_ledger',ROOT.parent/'port-game/ledger.py')
forward=importlib.util.module_from_spec(spec);spec.loader.exec_module(forward)
symbols=forward.symbols

def length(data,a):
    i=a
    while data[i] in (0xdd,0xfd):i+=1
    indexed=i!=a;op=data[i];suffix=1
    if op==0xcb:suffix=3 if indexed else 2
    elif op==0xed:suffix=4 if data[i+1]&0xc7==0x43 else 2
    elif op<0x40:
        if op&7==0 and op>=0x10:suffix=2
        elif op&15==1:suffix=3
        elif op&15 in (2,10) and op>=0x20:suffix=3
        elif op&7==6:suffix=2+int(indexed and (op>>3)&7==6)
        elif indexed and (op>>3)&7==6 and op&7 in (4,5):suffix=2
    elif op<0xc0:
        if indexed and op!=0x76 and (op&7==6 or op<0x80 and (op>>3)&7==6):suffix=2
    elif op&0xc7 in (0xc2,0xc4) or op in (0xc3,0xcd):suffix=3
    elif op&0xc7==0xc6 or op in (0xd3,0xdb):suffix=2
    return i-a+suffix

def instructions(data,a,b):
    result=[]
    while a<b:
        n=length(data,a)
        if a+n>b:raise ValueError('source code region cuts an instruction')
        loc=(1<<32)|((a//16384)<<16)|(a%16384);encoded=data[a:a+n].hex()
        runs=json.dumps([dict(offset=0,location=str(loc),length=n)],sort_keys=True,separators=(',',':'))
        result.append(dict(id=sha(f'{loc}:{encoded}:{runs}'.encode()),offset=str(a),length=str(n),location=str(loc),bytes=encoded));a+=n
    return result

def make(root):
    inherited=forward.make(root);build=json.loads((root/'build.json').read_text())
    source=(root/'collect-and-exit.gg').read_bytes();target=(root/'collect-and-exit.gb').read_bytes()
    gg=symbols(root/'symbols.gg');gb=symbols(root/'symbols.gb')
    routines=['Entry','Irq','Init','SetVram','CallBank1','CallBank2','ResetGame','ReadInput','Render','SoundTick','Tick','Attempt']
    regions=[]
    for name in routines+['Header','Registers','Palette','Art','Level']:
        a,b=gg[name],gg[name+'End'];kind='code' if name in routines else 'header' if name=='Header' else 'data'
        regions.append(dict(id=name,offset=str(a),length=str(b-a),kind=kind,bank=a//16384,address=(16384 if a//16384 else 0)+a%16384,
            sha256=sha(source[a:b]),reviewed=True,evidence=['Exact Game Gear-authored indexed/shadow source and WLA symbols','symbols.gg:'+name],instructions=instructions(source,a,b) if kind=='code' else []))
    regions.sort(key=lambda r:int(r['offset']));inventory=[];cursor=0
    def fill(a,b):
        if set(source[a:b])-{255}:raise ValueError('unclassified non-padding source bytes')
        return dict(id=f'fill-{a}',offset=str(a),length=str(b-a),kind='padding',bank=a//16384,address=(16384 if a//16384 else 0)+a%16384,sha256=sha(source[a:b]),reviewed=True,
            evidence=['EMPTYFILL FF; fixed legal entries/calls/interrupt exclude padding'],instructions=[])
    for r in regions:
        a=int(r['offset'])
        while cursor<a:
            end=min(a,(cursor//16384+1)*16384);inventory.append(fill(cursor,end));cursor=end
        if cursor!=a:raise ValueError('overlapping source inventory')
        inventory.append(r);cursor=a+int(r['length'])
    while cursor<len(source):
        end=min(len(source),(cursor//16384+1)*16384);inventory.append(fill(cursor,end));cursor=end
    register_contract='GG IX/IY remain C000 and indexed byte accesses map to the same GB absolute RAM bytes. Tick uses alternate AF/BC/DE/HL as private work registers; GB saves/restores AF/BC/DE/HL on its stack. Neither affects mapped gameplay state or legal bank returns; reviewed stack allowance is 64 bytes.'
    contracts=inherited['contracts']+[dict(id='registers',text=register_contract,sha256=sha(register_contract.encode()))]
    assigned={m['sourceRegions'][0]:m['contracts'] for m in inherited['mappings']}
    assigned.update(SetVram=['display'],Registers=['display'],Palette=['display'])
    assigned['Tick']=assigned['Tick']+['registers'];assigned['Init']=assigned['Init']+['registers']
    mappings=[]
    for r in regions:
        name=r['id'];replacement=name not in ['Tick','Attempt','ResetGame','Level']
        destination='Render' if name=='SetVram' else 'Init' if name in ['Registers','Palette'] else name
        a,b=gb[destination],gb[destination+'End']
        mappings.append(dict(id='reverse-'+name,kind='replacement' if replacement else 'translation',sourceRegions=[name],instructions=[i['id'] for i in r['instructions']],
            targets=[dict(offset=str(a),length=str(b-a),sha256=sha(target[a:b]),bank=a//16384,address=(16384 if a//16384 else 0)+a%16384,symbol=destination)],
            contracts=assigned[name],implemented=True,reviewed=True,evidence=['Authored Game Gear source semantics and explicit Game Boy hardware/register replacements','Exact target WLA region '+destination]))
    hardware=inherited['hardware']+[dict(id='registers',contracts=['registers'],implemented=True,reviewed=True,evidence=['Indexed state and alternate-register save/restore reviewed in Tick/Init'])]
    files=['game.gg.asm','gameplay.gg.inc','game.gb.asm','gameplay.gb.inc','state.inc','symbols.gg','symbols.gb','art.gg.bin','art.gb.bin','level.bin','canonical-art.json','assets.py','build.py']
    return dict(version=1,revision='1',source=dict(core='gamegear',sha256=sha(source),size=str(len(source))),target=dict(core='gameboy',sha256=sha(target),size=str(len(target))),
        buildSha256=sha((root/'build.json').read_bytes()),sourceSha256=sha(json.dumps(build['sources'],sort_keys=True,separators=(',',':')).encode()),assetSha256=build['artSha256'],
        supplementalEvidence=[dict(kind='ROM-matched source/symbol/asset',path=f,sha256=sha((root/f).read_bytes()),romSha256=sha(source)) for f in files],
        inventory=inventory,mappings=mappings,contracts=contracts,requiredScenarios=inherited['requiredScenarios'],hardware=hardware,
        controlFlow=[dict(id='reverse-static-rom-closure',contracts=['mapper','interrupt','gameplay','registers'],implemented=True,reviewed=True,
            evidence=['Exact source inventory, immediate transfers, matched stack returns, fixed IM1 vector 0038 and banked entries 1/2:4000; no RAM or indirect execution','IRQ preserves active AF/HL; IX/IY remain fixed and Tick exchanges shadow banks only at entry/exit'])],
        model=dict(reviewed=True,evidence=['Exact WLA physical inventory and authored legal control-flow review'],banks=[0,1,2],entryPoints=['0:0000','0:0038','1:4000','2:4000'],ramExecution=False,stackBytes=64),
        assumptions=['Applies only to the exact authored reverse fixture and declared legal inputs.','Source is Game Gear; indexed/shadow accesses and port hardware are reviewed replacements on Game Boy.','Static inventory covers unobserved bytes; dynamic windows alone never establish completeness.','Agent review is distinct from user live acceptance.'],verification=[])
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('build',type=Path);parser.add_argument('output',type=Path);args=parser.parse_args();args.output.write_text(json.dumps(make(args.build),indent=2))
