#!/usr/bin/env python3
"""Package the optional observer for an explicitly supplied ROM/core."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--rom', type=Path, required=True)
parser.add_argument('--target', choices=['gameboy', 'gamegear'], required=True)
parser.add_argument('--module', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
identity = hashlib.sha256(args.rom.read_bytes()).hexdigest()
if not args.module.is_file():
    parser.error('module does not exist')
args.output.mkdir(parents=True, exist_ok=False)
shutil.copyfile(args.module, args.output / args.module.name)
(args.output / 'manifest.json').write_text(json.dumps(dict(
    schemaVersion=1, id='river-xmb-telemetry', version='1.0.0',
    target=args.target, romSha256=identity, nativeModule=args.module.name,
), indent=2) + '\n')
