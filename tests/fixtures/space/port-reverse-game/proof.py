#!/usr/bin/env python3
"""Reproduce the Game Gear-authored reverse standalone proof. Each reference core runs in its own process."""
import argparse,hashlib,json,shutil,subprocess,sys
from pathlib import Path
from build import build,ROOT
from ledger import make,symbols
from verify import verify,KEYS
sha=lambda b:hashlib.sha256(b).hexdigest()
def run(args):subprocess.run(list(map(str,args)),check=True,stdout=subprocess.DEVNULL)
def proof(out,space,capture,state):
    out.mkdir(parents=True,exist_ok=True);(out/'proof-run.json').write_text(json.dumps(dict(status='incomplete',liveAcceptance='not run')))
    build(out,['gb','gg']);build(out/'rebuild',['gb','gg'])
    for s in ['gb','gg']:assert (out/f'collect-and-exit.{s}').read_bytes()==(out/'rebuild'/f'collect-and-exit.{s}').read_bytes()
    # Discover the exact source ROM through SPACE before admitting supplemental files.
    (out/'rom-first.json').unlink(missing_ok=True)
    commands=out/'discovery.jsonl';commands.write_text('{"op":"step","count":32}\n{"op":"quit"}\n')
    run([space,'explore','--core','gamegear','--rom',out/'collect-and-exit.gg','--project',out/'rom-first.json','--commands',commands])
    (out/'ledger.json').write_text(json.dumps(make(out),indent=2))
    gb=symbols(out/'symbols.gg')
    triggers=[('initialization','LoadArt'),('idle','Render'),('first','Attempt'),('leave-item','CallBank2'),('revisit-item','Tick'),('wall','Attempt'),('back-from-wall','ReadInput'),('down','SoundTick'),('up-gap','CallBank1'),('cross-gap','Render'),('locked-exit','Attempt'),('to-item','Tick'),('third','SoundTick'),('win','Attempt'),('victory','SoundTick'),('reset','Tick')]
    windows=[dict(name=f'{scene}:{label}',scenario=scene,pc=(16384 if gb[label]//16384 else 0)+gb[label]%16384,bank=gb[label]//16384,count=32,atFrame=1 if scene=="reset" else None) for scene,label in triggers]
    (out/'windows.json').write_text(json.dumps(windows,indent=2))
    run([capture,out/'collect-and-exit.gg',ROOT/'scenarios.json',out/'windows.json',out/'captured','gamegear'])
    target_labels=symbols(out/'symbols.gb');target_windows=[dict(name='initialization:LoadArt',scenario='initialization',pc=target_labels['LoadArt'],bank=0,count=32)]
    (out/'target-windows.json').write_text(json.dumps(target_windows))
    run([capture,out/'collect-and-exit.gb',ROOT/'scenarios.json',out/'target-windows.json',out/'target-captured'])
    run([state,out/'collect-and-exit.gg',ROOT/'scenarios.json',out/'internal-gg.json'])
    scenarios=json.loads((ROOT/'scenarios.json').read_text())
    cores={'gb':Path('/usr/lib/libretro/gambatte_libretro.so'),'gg':Path('/usr/lib/libretro/genesis_plus_gx_libretro.so')}
    if not all(c.exists() for c in cores.values()):
        (out/'reference-verification.json').write_text(json.dumps(dict(status='not run',reason='missing reference cores')));return False
    for s,c in cores.items():run([sys.executable,ROOT/'reference.py','--core',c,'--rom',out/f'collect-and-exit.{s}','--output',out/f'reference-{s}','--commands',ROOT/'scenarios.json'])
    report=verify(out)
    internal=json.loads((out/'internal-gg.json').read_text());capture_report=json.loads((out/'captured/capture-report.json').read_text())
    for s,states in [('gg',internal['states']),('gb',json.loads((out/'target-captured/capture-report.json').read_text())['ticks']),('gg',capture_report['ticks'])]:
        reference=json.loads((out/f'reference-{s}/result.json').read_text())['states'];assert len(states)==len(reference)
        for i,(x,y) in enumerate(zip(states,reference)):assert all(x[k]==y[k] for k in KEYS),f'TIME/{s} tick {i} mismatch'
    report['internalStateComparisons']='passed';(out/'reference-verification.json').write_text(json.dumps(report,indent=2))
    ledger=make(out);ledger['supplementalEvidence'].append(dict(kind='ROM-first discovery',path='rom-first.json',sha256=sha((out/'rom-first.json').read_bytes()),romSha256=ledger['source']['sha256']))
    (out/'ledger.json').write_text(json.dumps(ledger,indent=2));project=out/'project.json';shutil.copyfile(out/'captured/analyzed.json',project)
    result=subprocess.check_output(list(map(str,[space,'port','init','--project',project,'--rom',out/'collect-and-exit.gg','--target',out/'collect-and-exit.gb','--input',out/'ledger.json'])));binding=json.loads(result)['result']['binding']
    contracts=[c['id'] for c in ledger['contracts']]
    artifacts=[dict(path=f,sha256=sha((out/f).read_bytes())) for f in ['collect-and-exit.gb','collect-and-exit.gg','reference-verification.json','internal-gg.json','captured/capture-report.json','target-captured/capture-report.json','scenarios.json']]
    for s in cores:
        artifacts.extend(dict(path=f'reference-{s}/{f}',sha256=sha((out/f'reference-{s}/{f}').read_bytes())) for f in ['audio.wav','result.json']+[c['name']+'.png' for c in scenarios])
    capture_tool=dict(name='TIME fixture capture driver',version='two-core-reverse-1',sha256=sha(capture.read_bytes()))
    state_tool=dict(name='TIME Game Gear state driver',version='two-core-reverse-1',sha256=sha(state.read_bytes()))
    reference_tool=dict(name='Independent libretro Python harness',version='two-core-reverse-1',sha256=sha((ROOT/'reference.py').read_bytes()))
    verifier_tool=dict(name='State/artwork/audio verifier',version='two-core-reverse-1',sha256=sha((ROOT/'verify.py').read_bytes()))
    for kind,tools in [('behavioral',[capture_tool,state_tool,verifier_tool]),('independent',report['cores']+[reference_tool,verifier_tool])]:
        record=dict(id='reverse-fixture-'+kind,binding=binding,type=kind,status='passed',contracts=contracts,scenarios=report['scenarios'],artifacts=artifacts,tools=tools,evidence=['reference-verification.json', 'Exact mapped state at every recorded logical tick, normalized artwork and measured cue contracts','Two byte-identical builds; no live acceptance claimed'])
        recordpath=out/f'verification-{kind}.json';recordpath.write_text(json.dumps(record,indent=2));run([space,'port','record-verification','--project',project,'--rom',out/'collect-and-exit.gg','--target',out/'collect-and-exit.gb','--input',recordpath])
    run([space,'export','--project',project,'--html',out/'graph.html'])
    result=subprocess.check_output(list(map(str,[space,'port','check','--project',project,'--rom',out/'collect-and-exit.gg','--target',out/'collect-and-exit.gb'])));(out/'check.json').write_bytes(result)
    (out/'proof-run.json').write_text(json.dumps(dict(status='passed',liveAcceptance='not run',viewerAcceptance='not run',stage5Gate=False)))
    return True
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--space',type=Path,required=True);p.add_argument('--capture',type=Path,required=True);p.add_argument('--state',type=Path,required=True);a=p.parse_args();proof(a.output.resolve(),a.space.resolve(),a.capture.resolve(),a.state.resolve())
