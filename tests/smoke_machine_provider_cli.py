"""Exercise actual CLI/config provider selection and rejected module isolation."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe, module = map(lambda p: str(Path(p).absolute()), sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="time-provider-cli-") as directory:
    root = Path(directory)
    rom = root / "fixture.rom"
    rom.write_bytes(bytes(32768))
    environment = dict(os.environ)
    environment.pop("TIME_TEST_PROVIDER_MODE", None)
    def run(arguments, ok=True, mode=None):
        env = dict(environment)
        if mode:
            env["TIME_TEST_PROVIDER_MODE"] = mode
        result = subprocess.run([exe, *arguments], env=env, capture_output=True, text=True, timeout=60)
        assert (result.returncode == 0) == ok, result.stderr + result.stdout
        return result
    for provider, family in [("external-gb", "gameboy"), ("external-gg", "gamegear")]:
        report = root / (family + ".json")
        args = ["--core", provider, "--machine-provider", module, "--rom", str(rom),
                "--headless", "--no-audio", "--steps", "16", "--diagnostics-report", str(report)]
        run(args)
        assert report.is_file() and json.loads(report.read_text())
        run(args, ok=False, mode="unknown")
        config = root / "provider.ini"
        config.write_text(f"[emulator]\ncore={provider}\nmachine_provider={module}\nrom={rom}\nheadless=true\nsteps=16\n[audio]\nenabled=false\n")
        run(["--config", str(config)])
    run(["--machine-provider"], ok=False)
    run(["--core", "not-present", "--machine-provider", module, "--rom", str(rom), "--headless", "--no-audio", "--steps", "1"], ok=False)
print("Both-core CLI/config provider selection and closed-family rejection passed")
