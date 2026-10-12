#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
from pathlib import Path
with tempfile.TemporaryDirectory(prefix='time-script-cli-') as temporary:
    root=Path(temporary);rom=root/'empty.rom';rom.write_bytes(bytes(32768))
    for core in ['gameboy','gamegear']:
        source=root/'script.json';source.write_text(json.dumps({'schemaVersion':1,'core':core,'actions':[{'operation':'write','address':49152,'value':85},{'operation':'register','name':'BC','value':1234},{'operation':'report','text':'done'}]}))
        command=[sys.argv[1],'--core',core,'--rom',str(rom),'--source',str(source)]
        denied=subprocess.run(command,capture_output=True,text=True,timeout=20)
        assert denied.returncode==1 and not json.loads(denied.stdout)['ok']
        accepted=subprocess.run(command+['--allow-ram','--allow-registers'],capture_output=True,text=True,timeout=20)
        assert accepted.returncode==0,accepted.stdout+accepted.stderr
        result=json.loads(accepted.stdout);assert result['ok'] and result['memory'][0]==85 and result['registers'][1]==1234 and result['report']=='done'
        source.write_text(json.dumps({'schemaVersion':1,'core':core,'actions':[{'operation':'write','address':49152,'value':85},{'operation':'write','address':32768,'value':1}]}))
        rejected=subprocess.run(command+['--allow-ram'],capture_output=True,text=True,timeout=20)
        assert rejected.returncode==1 and not json.loads(rejected.stdout)['ok']
print('both-core scripting file automation and explicit permissions passed')
