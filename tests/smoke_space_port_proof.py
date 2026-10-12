#!/usr/bin/env python3
import json,shutil,sys,tempfile,subprocess
from pathlib import Path
reverse=len(sys.argv)>4 and sys.argv[4]=='gamegear'
ROOT=Path(__file__).resolve().parent/('fixtures/space/port-reverse-game' if reverse else 'fixtures/space/port-game');sys.path.insert(0,str(ROOT))
from proof import proof
from build import build

def main():
    if any(not shutil.which(t) for t in ['wla-gb','wla-z80','wlalink']) or not all(Path('/usr/lib/libretro/'+c).exists() for c in ['gambatte_libretro.so','genesis_plus_gx_libretro.so']):print('external tool/core unavailable: not run');return 77
    with tempfile.TemporaryDirectory(prefix='space-port-proof-') as temp:
        out=Path(temp);assert proof(out,*[Path(a).resolve() for a in sys.argv[1:4]])
        check=json.loads((out/'check.json').read_text())['result'];assert check['complete'] and not check['stage5Gate'] and check['liveAcceptance']=='not run'
        assert json.loads((out/'captured/capture-report.json').read_text())['accountingFingerprintParity']=='passed'
        p=json.loads((out/'project.json').read_text());assert p['porting']['verification'] and p['analysis'] and not p['gaps'];assert len(p['captureWindows']['windows'])==16
        assert p['captureWindows']['intentionalOmissions'] and len(p['instructions'])<512
        # Active histories start anew. Captured suppliers must stay on the read's window branch.
        for d in p['dependencies']:
            if d['supplier']['instruction']:assert d['supplier']['branch']==d['branch']
        for label,windows in [('oversize',[dict(name='bad',scenario='initialization',pc=0x100,count=129)]),('exhausted',[dict(name='bad',scenario='initialization',pc=0x100,count=32,budget=1024)]),('too-many',[{}]*17),('missed',[dict(name='bad',scenario='initialization',pc=0x104 if reverse else 0x103,count=32)])]:
            path=out/(label+'.json');path.write_text(json.dumps(windows));target=out/label
            source_rom=out/('collect-and-exit.gg' if reverse else 'collect-and-exit.gb')
            run=subprocess.run([sys.argv[2],str(source_rom),str(ROOT/'scenarios.json'),str(path),str(target)]+(['gamegear'] if reverse else []),capture_output=True,text=True)
            assert run.returncode!=0 and json.loads((target/'capture-report.json').read_text())['status']=='incomplete'
        print('banked game, logical ticks, independent cores, art/audio, capture histories/omissions/bounds passed; live acceptance not run')
    return 0
if __name__=='__main__':sys.exit(main())
