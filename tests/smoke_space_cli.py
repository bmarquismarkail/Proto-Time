"""End-to-end CLI fixture, paused input/checkpoints, export and config rejection."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path
space, emulator = sys.argv[1:3]
with tempfile.TemporaryDirectory(prefix='time-space-cli-') as work:
    root = Path(work)
    fixture = Path(__file__).parent / 'fixtures' / 'space' / 'create_rom.py'
    rom, project, checkpoint, html = [root / name for name in ['fixture.gb','project.json','checkpoint','graph.html']]
    subprocess.run([sys.executable, str(fixture), str(rom)], check=True)
    commands = [
        {'op':'run','count':24}, {'op':'input','mask':1},
        {'op':'checkpoint','path':str(checkpoint)}, {'op':'status'},
        {'op':'run','count':4}, {'op':'restore','path':str(checkpoint)}, {'op':'status'},
        {'op':'export','html':str(html)}, {'op':'quit'},
    ]
    run = subprocess.run([space,'explore','--rom',str(rom),'--project',str(project)],
                         input='\n'.join(map(json.dumps,commands)), text=True, capture_output=True, check=True)
    replies = [json.loads(line) for line in run.stdout.splitlines()]
    assert all(reply['ok'] for reply in replies), replies
    assert replies[3]['fingerprint'] == replies[6]['fingerprint']
    assert replies[3]['history']['writers'] == replies[6]['history']['writers']
    assert replies[3]['history']['branch'] != replies[6]['history']['branch']
    saved = json.loads(project.read_text())
    assert saved['blocks'] and not saved['gaps']
    assert saved['inputs'][0]['mask'] == 1
    assert 'http://' not in html.read_text().replace('http://www.w3.org/2000/svg','')
    subprocess.run([space,'merge','--project',str(project),'--input',str(project)], check=True)
    merged = json.loads(project.read_text())
    assert merged['dependencies'] == saved['dependencies']
    manual = root / 'manual.json'
    subprocess.run([emulator,'--core','gameboy','--rom',str(rom),'--headless','--no-audio',
                    '--steps','24','--space-project',str(manual)], check=True, capture_output=True)
    assert json.loads(manual.read_text())['blocks']
    rejected = subprocess.run([emulator,'--core','gameboy','--rom',str(rom),'--headless',
                               '--cpu-mode','block','--space-project',str(root/'bad.json')],capture_output=True)
    assert rejected.returncode != 0 and not (root/'bad.json').exists()
    # Exercise real cartridge mapping changes, in addition to synthetic CFG identities.
    banked = bytearray(0x10000)
    banked[:0x8000] = rom.read_bytes()
    banked[0x147:0x149] = bytes([1,1])  # MBC1, four ROM banks
    program = bytes.fromhex('3e02ea0020c30040')
    banked[0x150:0x150+len(program)] = program
    banked[0x8000:0x8005] = bytes.fromhex('3e55c30040')
    bank_rom, bank_project = root/'banked.gb', root/'banked.json'
    bank_rom.write_bytes(banked)
    subprocess.run([space,'explore','--rom',str(bank_rom),'--project',str(bank_project)],
                   input='{"op":"run","count":8}\n{"op":"quit"}\n',text=True,capture_output=True,check=True)
    evidence = json.loads(bank_project.read_text())
    assert any(n['address']==0x4000 and int(n['location'])==(1<<32 | 2<<16)
               for n in evidence['instructions'].values())
    assert any(d['kind']=='mapping' and not d['accepted'] for d in evidence['dependencies'])
    # Write executable RAM, visit it, replace its first opcode, and revisit it.
    ram_code = bytearray(rom.read_bytes())
    program = bytes.fromhex('3e00ea00c03ec3ea01c03e67ea02c03e01ea03c0c300c03e3eea00c0c300c0')
    ram_code[0x150:0x150+len(program)] = program
    ram_rom, ram_project = root/'ram.gb', root/'ram.json'
    ram_rom.write_bytes(ram_code)
    subprocess.run([space,'explore','--rom',str(ram_rom),'--project',str(ram_project)],
                   input='{"op":"run","count":18}\n{"op":"quit"}\n',text=True,capture_output=True,check=True)
    revisions = {n['bytes'] for n in json.loads(ram_project.read_text())['instructions'].values()
                 if n['address']==0xc000}
    assert revisions == {'00','3ec3'}, revisions
print('S.P.A.C.E. CLI checkpoint replay and manual capture passed')
