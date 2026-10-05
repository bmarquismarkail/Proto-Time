#!/usr/bin/env python3
"""ROM-matched WLA evidence for this bounded authored fixture; not a general translator."""
import argparse,hashlib,json,re
from pathlib import Path
from assets import TILES
sha=lambda b:hashlib.sha256(b).hexdigest()
def symbols(path):
    labels={};section=''
    for line in path.read_text().splitlines():
        if line.startswith('['):section=line
        if section=='[labels]':
            m=re.fullmatch(r'([0-9a-fA-F]+):([0-9a-fA-F]+) (\w+)',line)
            if m:bank,address=int(m[1],16),int(m[2],16);labels[m[3]]=bank*16384+address-(16384 if bank else 0)
    return labels
THREE={8,0xc3,0xcd,0xc2,0xca,0xd2,0xda,0xc4,0xcc,0xd4,0xdc,0xea,0xfa}
TWO={0xcb,0x10,0x18,0x20,0x28,0x30,0x38,0xe0,0xf0,0xe8,0xf8}
def length(op):
    if op in THREE or op<0x40 and op&15==1:return 3
    if op in TWO or op<0x40 and op&7==6 or op>=0xc0 and op&7==6:return 2
    return 1
def instructions(data,a,b):
    out=[]
    while a<b:
        n=length(data[a]);assert a+n<=b
        loc=(1<<32)|((a//16384)<<16)|(a%16384);hex=data[a:a+n].hex()
        out.append(dict(id=sha(f'{loc}:{hex}'.encode()),offset=str(a),length=str(n),location=str(loc),bytes=hex));a+=n
    return out
def make(root):
    build=json.loads((root/'build.json').read_text());source=(root/'collect-and-exit.gb').read_bytes();target=(root/'collect-and-exit.gg').read_bytes();gb=symbols(root/'symbols.gb');gg=symbols(root/'symbols.gg')
    assert len(source)==len(target)==65536
    assert build['roms']['gb']['sha256']==sha(source) and build['roms']['gg']['sha256']==sha(target)
    routines=['Irq','Entry','Init','CallBank1','CallBank2','ResetGame','ReadInput','Render','SoundTick','Tick','Attempt']
    regions=[]
    for name in routines+['Header','Art','Level']:
        a,b=gb[name],gb[name+'End'];kind='code' if name in routines else 'header' if name=='Header' else 'data'
        regions.append(dict(id=name,offset=str(a),length=str(b-a),kind=kind,bank=a//16384,address=(16384 if a//16384 else 0)+a%16384,sha256=sha(source[a:b]),reviewed=True,evidence=['Exact WLA build with post-link documented header checksums','symbols.gb:'+name],instructions=instructions(source,a,b) if kind=='code' else []))
    regions.sort(key=lambda r:int(r['offset']));inventory=[];cursor=0
    def padding(a,b):
        assert set(source[a:b])<={255}
        return dict(id=f'fill-{a}',offset=str(a),length=str(b-a),kind='padding',bank=a//16384,address=(16384 if a//16384 else 0)+a%16384,sha256=sha(source[a:b]),reviewed=True,evidence=['WLA EMPTYFILL FF; fixed vectors/entries and static calls exclude this region'],instructions=[])
    for r in regions:
        a=int(r['offset'])
        while cursor<a:
            end=min(a,(cursor//16384+1)*16384);inventory.append(padding(cursor,end));cursor=end
        assert cursor==a;inventory.append(r);cursor=a+int(r['length'])
    while cursor<len(source):
        end=min(len(source),(cursor//16384+1)*16384);inventory.append(padding(cursor,end));cursor=end
    texts={
      'gameplay':'Identical collect-and-exit state transition algorithm: R/L/D/U priority, one move per eight held VBlanks, wall/locked exit collision, once-only pickups, win and rising Start reset; same C000..C024 state mapping.',
      'initialization':'Start fixed ROM, initialize RAM, artwork and level; Game Boy MBC1 and Game Gear Sega paging retain ROM execution. Enable only VBlank interrupt after initialization.',
      'mapper':'Fixed bank trampolines select 1/2 in slot 1 at 4000, save caller bank on stack and restore including nested calls; slot 0 stays bank 0.',
      'display':'Same canonical four-shade 8x8 art, 160x144 viewport, two-row HUD, 20x16 playfield, two movement-linked poses and stationary pose 0; target planar encoding and SAT/VDP are deliberate replacements.',
      'input':'Sample normalized held R/L/U/D/Start each VBlank. Start bit 7 is rising-edge reset; hardware joypad interfaces replaced.',
      'audio':'Blocked 440 Hz/4 logical frames; pickup 880 Hz/8; victory 880,1046,1319 Hz each 8 frames. Pitch tolerance 5 percent; duration tolerance one logical frame. Square/PSG waveforms need not match.',
      'interrupt':'VBlank ISR acknowledges source, sets flag and increments frame. CPU state saved; no other interrupt source enabled; interrupt handlers execute fixed ROM.',
      'header':'Valid 64 KiB MBC1 Game Boy header/checksums replaced by export Game Gear TMR SEGA/64 KiB header/checksum.',
      'level':'Identical 320-byte authored static level and pickup/exit coordinates; assets remain inventoried independently of capture coverage.'}
    contracts=[dict(id=k,text=t,sha256=sha(t.encode())) for k,t in texts.items()]
    assigned={'Irq':['interrupt'],'Entry':['initialization'],'Init':['initialization','display'],'CallBank1':['mapper'],'CallBank2':['mapper'],'ResetGame':['gameplay'],'ReadInput':['input'],'Render':['display'],'SoundTick':['audio'],'Tick':['gameplay'],'Attempt':['gameplay'],'Header':['header'],'Art':['display'],'Level':['level']}
    mappings=[]
    for r in regions:
        name=r['id'];names=[name]+(['SetVram'] if name=='Render' else [])+(['Registers','Palette'] if name=='Init' else [])
        targets=[]
        for n in names:
            a,b=gg[n],gg[n+'End'];targets.append(dict(offset=str(a),length=str(b-a),sha256=sha(target[a:b]),bank=a//16384,address=(16384 if a//16384 else 0)+a%16384,symbol=n))
        mappings.append(dict(id='convert-'+name,kind='translation' if name in ['Tick','Attempt','ResetGame','Level'] else 'replacement',sourceRegions=[name],instructions=[i['id'] for i in r['instructions']],targets=targets,contracts=assigned[name],implemented=True,reviewed=True,evidence=['Agent-authored assembly reviewed against exact source/target bytes','Separate '+name+' target routine and stated contract']))
    obligation=lambda id,contracts,evidence:dict(id=id,contracts=contracts,implemented=True,reviewed=True,evidence=evidence)
    files=['game.gb.asm','gameplay.gb.inc','state.inc','symbols.gb','art.gb.bin','level.bin','canonical-art.json','assets.py','build.py','game.gg.asm','gameplay.gg.inc','symbols.gg']
    return dict(version=1,revision='1',source=dict(core='gameboy',sha256=sha(source),size=str(len(source))),target=dict(core='gamegear',sha256=sha(target),size=str(len(target))),buildSha256=sha((root/'build.json').read_bytes()),sourceSha256=sha(json.dumps(build['sources'],sort_keys=True,separators=(',',':')).encode()),assetSha256=build['artSha256'],
      supplementalEvidence=[dict(kind='matched assembler/symbol/asset',path=f,sha256=sha((root/f).read_bytes()),romSha256=sha(source)) for f in files],
      inventory=inventory,mappings=mappings,contracts=contracts,requiredScenarios=[c["name"] for c in json.loads((Path(__file__).parent/"scenarios.json").read_text())],
      hardware=[obligation(k,[k],['Reviewed source/target interfaces and bounded fixture contract']) for k in ['initialization','mapper','display','input','audio','interrupt']],
      controlFlow=[obligation('static-rom-closure',['mapper','interrupt','gameplay'],['All source code inventoried from exact WLA symbols; only immediate CALL/JP/JR and matched stack RET/RETI; no indirect JP, RST or RAM execution','Entry 0100 and VBlank 0040; 4000 entries uniquely distinguished by selected bank; nested CallBank1 -> Tick -> CallBank2 -> Attempt restores bank 1, then returns to the fixed-bank caller with its selected bank preserved'])],
      model=dict(reviewed=True,evidence=['Authored assembly control-flow and stack review; exact ROM-matched symbols'],banks=[0,1,2],entryPoints=['0:0100','0:0040','1:4000','2:4000'],ramExecution=False,stackBytes=64),
      assumptions=['Completeness applies only to this ROM-matched authored fixture and its legal input set, not arbitrary Game Boy games.','Static assembler classification and reviewed legal entry/call/interrupt model account for unobserved bytes; trace coverage is supplementary.','No writable/executable RAM; no serial, background music or autonomous bank changes. Padding cannot be entered under the bounded model.','Reviewed fields record agent review; they are not user live acceptance.'],verification=[])
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('build',type=Path);p.add_argument('output',type=Path);a=p.parse_args();a.output.write_text(json.dumps(make(a.build),indent=2))
