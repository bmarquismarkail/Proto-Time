"""Real module-owned core CLI/config selection and incompatible capabilities."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
exe, module = map(lambda p: str(Path(p).absolute()), sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='time-runtime-cli-') as directory:
    root=Path(directory)
    rom=root/'fixture.rom'
    rom.write_bytes(bytes(32768))
    for provider in ['runtime-gb','runtime-gg']:
        def run(extra=(), ok=True):
            report=root/(provider+'.json')
            result=subprocess.run([exe,'--core',provider,'--machine-provider',module,
                '--rom',str(rom),'--headless','--no-audio','--steps','16',
                '--diagnostics-report',str(report),*extra],capture_output=True,text=True,timeout=60)
            assert (result.returncode==0)==ok, result.stdout+result.stderr
            if ok: assert json.loads(report.read_text())['retired_instructions']==16
            return result
        run()
        config=root/'runtime.ini'
        config.write_text(f'[emulator]\ncore={provider}\nmachine_provider={module}\nrom={rom}\nheadless=true\nsteps=16\n[audio]\nenabled=false\n')
        result=subprocess.run([exe,'--config',str(config)],capture_output=True,text=True,timeout=60)
        assert result.returncode==0,result.stderr
        run(['--cpu-mode','ir'],ok=False)
        result=run(['--space-project',str(root/'capture')],ok=False)
        assert 'capture extension' in result.stderr
        run(['--visual-pack',str(root/'missing-pack')],ok=False)
print('Both-core external runtime CLI, INI and rejected capabilities passed')
