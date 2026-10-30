#!/usr/bin/env python3
"""Reproducible standalone ROM builds; no execution or runtime-generated code."""
import argparse,hashlib,json,shutil,subprocess,re
from pathlib import Path
from assets import encode,level,TILES
ROOT=Path(__file__).resolve().parent
sha=lambda b:hashlib.sha256(b).hexdigest()
def tool_identity(name):
    path=Path(shutil.which(name));r=subprocess.run([str(path)],capture_output=True,text=True,timeout=5)
    text=r.stdout+r.stderr;m=re.search(r'v(\d[^\s]*)',text)
    return dict(sha256=sha(path.read_bytes()),version=m[1] if m else text[:256].strip())
def build(out,systems):
    out.mkdir(parents=True,exist_ok=True)
    for name in ['state.inc','gameplay.gb.inc','gameplay.gg.inc','game.gb.asm','game.gg.asm','assets.py','build.py','scenarios.json']:
        if (ROOT/name).exists():shutil.copyfile(ROOT/name,out/name)
    (out/'canonical-art.json').write_text(json.dumps(TILES));(out/'art.gb.bin').write_bytes(encode(2));(out/'art.gg.bin').write_bytes(encode(4));(out/'level.bin').write_bytes(level())
    results={}
    for system in systems:
        assembler='wla-gb' if system=='gb' else 'wla-z80'
        if not shutil.which(assembler) or not shutil.which('wlalink'):raise RuntimeError('WLA toolchain unavailable: not run')
        subprocess.run([assembler,'-o',f'game.{system}.o',f'game.{system}.asm'],cwd=out,check=True)
        (out/f'link.{system}').write_text(f'[objects]\ngame.{system}.o\n')
        rom=out/f'collect-and-exit.{system}'
        subprocess.run(['wlalink','-S',f'link.{system}',rom.name],cwd=out,check=True)
        shutil.copyfile(rom.with_suffix('.sym'),out/f'symbols.{system}')
        data=bytearray(rom.read_bytes())
        if system=='gb':
            data[0x147]=1;data[0x148]=1;data[0x149]=0
            data[0x14d]=(-sum(data[0x134:0x14d])-25)&255
            total=(sum(data)-data[0x14e]-data[0x14f])&65535;data[0x14e:0x150]=total.to_bytes(2,'big')
        else:
            # Header checksum excludes its own final 16 bytes; size code E = 64 KiB.
            total=(sum(data[:0x7ff0])+sum(data[0x8000:]))&65535
            data[0x7ffa:0x7ffc]=total.to_bytes(2,'little')
        rom.write_bytes(data)
        results[system]={'rom':rom.name,'sha256':sha(data),'size':len(data)}
    sources={p.name:sha(p.read_bytes()) for p in sorted(ROOT.iterdir()) if p.is_file() and p.name in ['game.gb.asm','game.gg.asm','gameplay.gb.inc','gameplay.gg.inc','state.inc','assets.py','build.py','scenarios.json']}
    used=[{'gb':'wla-gb','gg':'wla-z80'}[system] for system in systems]+['wlalink']
    (out/'build.json').write_text(json.dumps({'schemaVersion':1,'roms':results,'tools':{name:tool_identity(name) for name in used},'sources':sources,'artSha256':sha(json.dumps(TILES).encode()),'conversion':'not assessed'},indent=2))
    return results
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--system',choices=['gb','gg','both'],default='both');a=p.parse_args();build(a.output, ['gb','gg'] if a.system=='both' else [a.system])
