#!/usr/bin/env python3
import copy,importlib.util,json,subprocess,sys,tempfile,hashlib,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parent/'fixtures/space/port-game'
sys.path.insert(0,str(ROOT))
from build import build
from ledger import make,symbols
sha=lambda b:hashlib.sha256(b).hexdigest()
def call(*args,ok=True):
    p=subprocess.run([sys.argv[1],*map(str,args)],capture_output=True,text=True,timeout=300)
    assert (p.returncode==0)==ok,(p.stdout,p.stderr)
    text=p.stdout if ok else p.stderr
    return json.loads(text) if text.strip() else {}
def main():
    if any(not shutil.which(t) for t in ['wla-gb','wla-z80','wlalink']):print('WLA unavailable: not run');return 77
    with tempfile.TemporaryDirectory(prefix='space-port-') as temp:
        out=Path(temp);build(out/'a',['gb','gg']);build(out/'b',['gb','gg']);a=out/'a'
        for ext in ['gb','gg']:assert (a/f'collect-and-exit.{ext}').read_bytes()==(out/'b'/f'collect-and-exit.{ext}').read_bytes()
        # Single-system builds must never resolve the other assembler.
        from unittest.mock import patch
        real_which=shutil.which
        for system,unused in [('gb','wla-z80'),('gg','wla-gb')]:
            with patch('build.shutil.which',side_effect=lambda name,unused=unused: None if name==unused else real_which(name)):
                single=out/system;build(single,[system])
            manifest=json.loads((single/'build.json').read_text())
            assert unused not in manifest['tools'] and len(manifest['tools'])==2
            assert (single/f'collect-and-exit.{system}').read_bytes()==(a/f'collect-and-exit.{system}').read_bytes()
        source=(a/'collect-and-exit.gb').read_bytes();target=(a/'collect-and-exit.gg').read_bytes()
        assert source[0x147:0x14a]==bytes([1,1,0]) and source[0x14d]==(-sum(source[0x134:0x14d])-25)&255
        assert target[0x7ff0:0x7ff8]==b'TMR SEGA' and target[0x7fff]==0x6e
        assert int.from_bytes(target[0x7ffa:0x7ffc],'little')==(sum(target[:0x7ff0])+sum(target[0x8000:]))&65535
        ledger=make(a);inp=a/'ledger.json';project=a/'project.json';gb=a/'collect-and-exit.gb';gg=a/'collect-and-exit.gg'
        def write(v):inp.write_text(json.dumps(v))
        def op(kind,ok=True):return call('port',kind,'--project',project,'--rom',gb,'--target',gg,'--input',inp,ok=ok)
        write(ledger);initial=op('init')['result'];assert initial['complete'] and not initial['stage5Gate']
        original=project.read_bytes();op('import');assert project.read_bytes()==original
        # Malformed merge input returns a JSON error and never publishes state.
        for field in ['verification','revision']:
            malformed=copy.deepcopy(ledger);del malformed[field];write(malformed)
            op('import',False);assert project.read_bytes()==original
        for value in [None,{},[{'id':'duplicate'},{'id':'duplicate'}]]:
            malformed=copy.deepcopy(ledger);malformed['verification']=value;write(malformed)
            op('import',False);assert project.read_bytes()==original
        for revision in [None,7,'bad']:
            malformed=copy.deepcopy(ledger);malformed['revision']=revision;write(malformed)
            op('import',False);assert project.read_bytes()==original
        write(ledger)
        binding=initial['binding'];contract=ledger['contracts'][0]['id'];tool=dict(name='test',version='1',sha256='0'*64)
        verification=dict(id='proof',binding=binding,type='behavioral',status='passed',contracts=[contract],scenarios=['idle'],artifacts=[dict(path='proof',sha256=sha(b'proof'))],tools=[tool],evidence=['test evidence'])
        (a/'proof').write_bytes(b'proof');write(verification);op('record-verification');saved=project.read_bytes();op('record-verification');assert project.read_bytes()==saved
        conflict=copy.deepcopy(verification);conflict['status']='failed';write(conflict);op('record-verification',False);assert project.read_bytes()==saved
        def rejected(change):
            v=copy.deepcopy(ledger);change(v);write(v);op('init',False);assert project.read_bytes()==saved
        rejected(lambda p:p['inventory'][1].update(offset=p['inventory'][0]['offset']))
        rejected(lambda p:p['inventory'][1].update(bank=9))
        rejected(lambda p:p['inventory'][1].update(bank=0.5))
        rejected(lambda p:p['model']['entryPoints'].append('0:0103'))
        rejected(lambda p:p['mappings'][0]['targets'][0].update(bank=7))
        rejected(lambda p:p.update(version=2))
        rejected(lambda p:p['source'].update(sha256='0'*64))
        rejected(lambda p:p['inventory'].pop())
        rejected(lambda p:p['mappings'][0]['sourceRegions'].append('missing'))
        rejected(lambda p:p['mappings'][0]['targets'][0].update(length='999999'))
        rejected(lambda p:p['mappings'][0].update(contracts=[]))
        rejected(lambda p:p['supplementalEvidence'][0].update(sha256='0'*64))
        rejected(lambda p:p['inventory'][1]['instructions'][0].update(id='0'*64))
        def incomplete(change):
            v=copy.deepcopy(ledger);change(v);write(v);assert not op('init')['result']['complete']
        incomplete(lambda p:p['inventory'][0].update(kind='unknown'))
        incomplete(lambda p:p['inventory'][1].update(reviewed=False))
        incomplete(lambda p:p['mappings'][0].update(implemented=False))
        incomplete(lambda p:p['mappings'].pop())
        incomplete(lambda p:p.update(mappings=[m for m in p['mappings'] if m['id']!='convert-Art']))
        incomplete(lambda p:p['hardware'][0].update(implemented=False))
        incomplete(lambda p:p['controlFlow'][0].update(implemented=False))
        incomplete(lambda p:p['model'].update(ramExecution=True))
        stale=copy.deepcopy(ledger);stale['verification']=[verification];stale['assumptions'].append('changed evidence');write(stale);assert op('init')['result']['verification'][0]['status']=='stale'
        write(ledger);op('import',False) # conflicting ledger must not silently replace
        op('init');q=out/'query.jsonl';q.write_text('\n'.join(json.dumps(dict(type=t,limit=1)) for t in ['port','inventory','conversions','obligations','verification']))
        p=subprocess.run([sys.argv[1],'query','--project',str(project),'--commands',str(q)],capture_output=True,text=True,timeout=300);assert p.returncode==0,p.stderr
        rows=[json.loads(r)['result'] for r in p.stdout.splitlines()];assert rows[1]['truncated'] and len(rows[1]['items'])==1
        q.write_text('{"type":"inventory","limit":1025}\n');call('query','--project',project,'--commands',q,ok=False)
        # Legacy project with no porting retains loadability.
        old=json.loads(project.read_text());del old['porting'];project.write_text(json.dumps(old));call('export','--project',project,'--html',out/'old.html')
        # Same logical address in distinct source banks has distinct stable identities.
        banked=[r for r in ledger['inventory'] if r['id'] in ['Tick','Attempt']];assert banked[0]['address']==banked[1]['address']==0x4000 and banked[0]['instructions'][0]['id']!=banked[1]['instructions'][0]['id']
        print('port inventory, stale/conflicting evidence, pagination, compatibility and reproducible ROM builds passed')
    return 0
if __name__=='__main__':sys.exit(main())
