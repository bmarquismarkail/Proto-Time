#!/usr/bin/env python3
"""Independent-core state, canonical artwork and measured cue-contract checks."""
import argparse,hashlib,json,statistics
from pathlib import Path
from PIL import Image
from assets import TILES,level
sha=lambda b:hashlib.sha256(b).hexdigest()
KEYS=['x','y','picked','score','won','pose','phase','input','previous','remaining','note','bank','blocked','collected','victory','lastCue','sound']
def normalize(image):
    image=Image.open(image).convert('RGB');pixels=list(image.get_flattened_data());colors=sorted(set(pixels),key=sum,reverse=True)
    assert len(colors)==4,'expected canonical four-shade output'
    rank={c:i for i,c in enumerate(colors)}
    return image.size,[rank[c] for c in pixels]
def canonical(state):
    pixels=[0]*(160*144);tiles=list(level());p=state['picked']
    for i,(x,y) in enumerate([(4,1),(4,5),(8,5)]):
        if p&(1<<i):tiles[y*20+x]=2
    tiles[5*20+10]=6 if state['score']==3 else 5
    for y in range(18):
        for x in range(20):
            t=tiles[(y-2)*20+x] if y>=2 else 8 if y==0 and 1<=x<=3 and x<=state['score'] else 7 if y==0 and 1<=x<=3 else 9 if y==0 and x==6 and state['won'] else 2
            for dy in range(8):
                for dx in range(8):pixels[(y*8+dy)*160+x*8+dx]=int(TILES[t][dy][dx])
    for dy in range(8):
        for dx in range(8):
            v=int(TILES[state['pose']][dy][dx])
            if v:pixels[((state['y']+2)*8+dy)*160+state['x']*8+dx]=v
    return pixels
def verify(root):
    a=json.loads((root/'reference-gb/result.json').read_text());b=json.loads((root/'reference-gg/result.json').read_text())
    assert a['commands']==b['commands'] and len(a['states'])==len(b['states'])
    assert a['romSha256']==sha((root/'collect-and-exit.gb').read_bytes()) and b['romSha256']==sha((root/'collect-and-exit.gg').read_bytes())
    for n,(x,y) in enumerate(zip(a['states'],b['states'])):
        assert all(x[k]==y[k] for k in KEYS),f'logical tick {n} mismatch'
        assert x['bank']==y['bank']==1,'nested bank restoration'
        if n:assert x['commit']==a['states'][n-1]['commit']+1 and y['commit']==b['states'][n-1]['commit']+1
    expected={'idle':(1,1,0,0),'first':(4,1,1,0),'leave-item':(3,1,1,0),'revisit-item':(4,1,1,0),'wall':(5,1,1,0),'back-from-wall':(4,1,1,0),'down':(4,5,2,0),'up-gap':(4,4,2,0),'cross-gap':(10,4,2,0),'locked-exit':(10,4,2,0),'to-item':(8,4,2,0),'third':(8,5,3,0),'win':(10,5,3,1),'victory':(10,5,3,1),'reset':(1,1,0,0),'priority':(2,1,0,0),'start-edge-move':(2,1,0,0),'start-held':(3,1,0,0)}
    frames=0;art=[]
    for command in a['commands']:
        frames+=command['frames'];s=a['states'][frames-1];assert tuple(s[k] for k in ['x','y','score','won'])==expected[command['name']]
        images=[normalize(root/f'reference-{system}'/(command['name']+'.png')) for system in ['gb','gg']]
        assert images[0][0]==images[1][0]==(160,144)
        assert images[0][1]==images[1][1],f'art source/target mismatch {command["name"]}'
        # Video is scanned before the latest VBlank update: accept only its explicitly mapped preceding tick.
        assert images[0][1]==canonical(s) or images[0][1]==canonical(a['states'][frames-2]),f'canonical artwork mismatch {command["name"]}'
        art.append(dict(scenario=command['name'],normalizedSha256=sha(bytes(images[0][1]))))
    for result in [a,b]:
        previous=None;last_move=None
        for i,s in enumerate(result['states']):
            if previous:
                moved=(s['x'],s['y'])!=(previous['x'],previous['y'])
                reset=s['input']&128 and not previous['previous']&128
                if moved and not reset:
                    assert s['phase']==0 and s['pose']==previous['pose']^1
                    if last_move is not None and s['input']==previous['input']:assert i-last_move>=8
                    last_move=i
                if s['input']==0 and not s['won']:assert s['pose']==0
            previous=s
    frequencies=[]
    for result in [a,b]:
        for sound,note,wanted in [(2,0,440),(1,0,880),(3,0,880),(3,1,1046),(3,2,1319)]:
            measures=[]
            for i,(s,samples) in enumerate(zip(result['states'],result['audioFrames'])):
                if s['sound']!=sound or s['note']!=note or s['remaining']<2 or i<1:continue
                if result['states'][i-1]['remaining']==0:continue # onset/filter transients
                values=samples[::2];values=values[len(values)//5:];mean=sum(values)/len(values)
                positions=[j+(mean-x)/(y-x) for j,(x,y) in enumerate(zip(values,values[1:])) if x<mean<=y]
                if len(positions)>=3:measures.append((len(positions)-1)*result['sampleRate']/(positions[-1]-positions[0]))
            measured=statistics.median(measures);assert abs(measured/wanted-1)<=.05,f'{result["core"]["name"]} tone {wanted}: {measured}'
            frequencies.append(dict(core=result['core']['name'],sound=sound,note=note,wantedHz=wanted,measuredHz=measured))
        # Audio-active duration from recorded samples, independently of the RAM countdown.
        for i,s in enumerate(result['states']):
            if not s['remaining'] or i and result['states'][i-1]['remaining']>=s['remaining']:continue
            wanted=4 if s['sound']==2 else 8 if s['sound']==1 else 24
            n=0
            for audio in result['audioFrames'][i:i+wanted+2]:
                # high-pass/filter tails can remain, so count frames with sustained square-wave transitions.
                v=audio[::2];mean=sum(v)/len(v);crosses=sum(x<mean<=y for x,y in zip(v,v[1:]))
                if crosses<3:break
                n+=1
            assert abs(n-wanted)<=1,f'audio cue timing {result["core"]["name"]}: {n} vs {wanted}'
    report=dict(status='passed',logicalTicks=len(a['states']),scenarios=list(expected),artwork=art,frequencies=frequencies,cores=[a['core'],b['core']],roms=[a['romSha256'],b['romSha256']],limitations=['Logical-tick state mapping, not equal CPU cycles.','Palette values normalized by luminance rank; waveforms may differ.','Headless reference-core verification does not establish live RetroArch acceptance.'])
    (root/'reference-verification.json').write_text(json.dumps(report,indent=2));return report
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('build',type=Path);a=p.parse_args();print(json.dumps(verify(a.build)))
