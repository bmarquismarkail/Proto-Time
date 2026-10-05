"""Stage-4 public analysis/query/exploration workflow, in both execution modes."""
import json,subprocess,sys,tempfile,re,shutil
from pathlib import Path
space=sys.argv[1]
def invoke(args,commands=None,check=True):
    r=subprocess.run([space,*map(str,args)],input=None if commands is None else '\n'.join(map(json.dumps,commands)),capture_output=True,text=True,check=check)
    return [json.loads(x) for x in r.stdout.splitlines()] if check else r
with tempfile.TemporaryDirectory(prefix='space-analysis-cli-') as tmp:
    root=Path(tmp);rom=root/'fixture.gb'
    subprocess.run([sys.executable,str(Path(__file__).parent/'fixtures/space/create_analysis_rom.py'),str(rom)],check=True)
    fingerprints=[]
    for mode in ['baseline','snapshot']:
        p=root/mode;p.mkdir();live=p/'live.json';frozen=p/'analyzed.json';html=p/'graph.html'
        replies=invoke(['explore','--rom',rom,'--project',live,'--execution',mode],[
            {'op':'input','mask':1},{'op':'run','count':500},{'op':'status'},
            {'op':'analyze','path':str(frozen)},{'op':'query','type':'summary'},
            {'op':'run','count':4},{'op':'query','type':'summary'},
            {'op':'analyze','path':str(frozen)},{'op':'status'},
            {'op':'export','analyzed':True,'path':str(frozen),'html':str(html)},
            {'op':'run','count':2},{'op':'quit'}])
        assert all(r['ok'] for r in replies),replies
        fingerprints.append(replies[2]['fingerprint'])
        assert replies[4]['result']['analysisId']==replies[6]['result']['analysisId']
        assert replies[7]['analysis']['id']!=replies[3]['analysis']['id']
        assert replies[5]['fingerprint']==replies[8]['fingerprint']
        analyzed=json.loads(frozen.read_text());a=analyzed['analysis']
        assert not a['incomplete'] and a['routines'] and a['loops']
        assert {'audio.configuration','graphics.tile-data-output','input.polling'}<={f['role'] for f in a['findings']}
        embedded=json.loads(re.search(r'<script id="capture" type="application/json">(.*?)</script>',html.read_text(),re.S)[1])
        assert embedded['analysis']['id']==a['id']
        assert int(embedded['revision'])<int(json.loads(live.read_text())['revision'])
        assert all(name in html.read_text() for name in ['Routine candidates','Effects through callees','Access evidence','Highlight cycles'])
        if shutil.which('node'):
            subprocess.run(['node',str(Path(__file__).with_name('smoke_space_viewer.mjs')),str(html)],check=True)
        queries=p/'queries.jsonl';queries.write_text('\n'.join(map(json.dumps,[
            {'type':'summary'},{'type':'routines','limit':1},{'type':'loops'},
            {'type':'hardware','category':'audio.channel'},{'type':'dependencies','limit':1},
            {'type':'dependency_path','id':a['dataTransfers'][0]['target'],'depth':16},
            {'type':'callees','id':a['routines'][0]['id']},{'type':'predecessors','id':analyzed['blocks'][0]['id']} ])))
        results=invoke(['query','--project',frozen,'--commands',queries])
        assert all(r['ok'] and r['result']['analysisId']==a['id'] for r in results)
        assert results[1]['result']['truncated'] and results[4]['result']['truncated']
        original=frozen.read_text();invoke(['merge','--project',frozen,'--input',frozen]);assert json.loads(frozen.read_text())==json.loads(original)
        notes=json.loads(original);entry=notes['analysis']['routines'][0]['entry']
        notes['annotations'].setdefault(entry,[]).append({'kind':'inference','text':'Annotation-only merge'})
        notes_file=p/'notes-only.json';notes_file.write_text(json.dumps(notes))
        invoke(['merge','--project',frozen,'--input',notes_file]);assert json.loads(frozen.read_text())['analysis']==a
        invoke(['merge','--project',frozen,'--input',live]);assert 'analysis' not in json.loads(frozen.read_text())
        f=a['findings'][0];annotated=p/'annotated.json'
        annotation_replies=invoke(['explore','--rom',rom,'--project',p/'notes-live.json','--execution',mode],[
            {'op':'run','count':500},{'op':'analyze'},
            {'op':'annotate','instruction':f['instruction'],'annotation':{'kind':'correction','text':'Fixture interpretation','finding':f['id']}},
            {'op':'export','analyzed':True,'path':str(annotated)},{'op':'quit'}])
        assert all(r['ok'] for r in annotation_replies),annotation_replies
        note=json.loads(annotated.read_text())['annotations'][f['instruction']][0]
        assert note['finding']==f['id'] and note['analysisId']
        invoke(['merge','--project',live,'--input',annotated]);assert json.loads(live.read_text())['annotations'][f['instruction']]
        event=invoke(['explore','--rom',rom,'--project',p/'event.json','--execution',mode],[
            {'op':'run_until','event':{'category':'audio.channel','access':'write'},'count':500},
            {'op':'quit'}])[0]['result']
        assert event['reason']=='event_observed' and event['event']['address']==0xff12
        assert int(event['count'])<500 and event['event']['origin']=='cpu'
        offline=p/'offline.json';invoke(['analyze','--project',live,'--output',offline]);assert json.loads(offline.read_text())['analysis']
    assert fingerprints[0]==fingerprints[1]
print('analysis/query CLI, frozen revisions, merges, annotations and run-until passed')
