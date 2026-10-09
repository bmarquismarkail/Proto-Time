#!/usr/bin/env python3
"""Build ROM-bound static research inventories from the reviewed port fixtures."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import shutil

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = ROOT / 'tests/fixtures/acceleration/rom-corpus.json'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'))


def supported(encoded):
    # Exact current built-in IR envelope. This does not add lowerings, prefixes,
    # conditional branches, stack operations, or device/absolute-memory opcodes.
    op = encoded[0]
    return (op == 0 or 0x40 <= op <= 0xbf and op != 0x76 or
            op & 0xc7 in (4, 5, 6) or op == 0x18)


def inventory(data, labels, gb, forward, reverse):
    routines = ['Entry', 'Irq', 'Init', 'CallBank1', 'CallBank2',
                'ResetGame', 'ReadInput', 'Render', 'SoundTick', 'Tick', 'Attempt']
    if not gb:
        routines.append('SetVram')
    entries = set(labels.values())
    segments = []
    used = set()
    for name in routines:
        start, end = labels[name], labels[name + 'End']
        pending = None
        for item in (forward if gb else reverse).instructions(data, start, end):
            offset = int(item['offset'])
            encoded = bytes.fromhex(item['bytes'])
            if offset in used or offset + len(encoded) > end:
                raise ValueError('overlapping or truncated ROM inventory')
            used.add(offset)
            emit = supported(encoded)
            if (pending is None or not emit or not pending['emitted'] or
                    offset in entries or len(pending['instructions']) == 8):
                pending = dict(region=name, bank=offset // 16384,
                               emitted=emit, instructions=[])
                segments.append(pending)
            pending['instructions'].append(dict(offset=offset,
                address=(0 if offset < 16384 else 16384) + offset % 16384,
                bytes=list(encoded)))
            if encoded[0] == 0x18:
                pending = None
    return segments


def build(output):
    contract = json.loads(CONTRACT.read_text())
    # Use the existing reviewed instruction inventories, never a disassembly of
    # padding/assets. Imports are isolated from the caller's module search path.
    fixture_root = ROOT / 'tests/fixtures/space'
    sys.path.insert(0, str(fixture_root / 'port-game'))
    modules = []
    for name, fixture in [('forward', 'port-game'), ('reverse', 'port-reverse-game')]:
        spec = importlib.util.spec_from_file_location(name, fixture_root / fixture / 'ledger.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        modules.append(module)
    forward, reverse = modules
    cases = []
    for fixture in ('port-game', 'port-reverse-game'):
        source = fixture_root / fixture
        directory = output / fixture
        subprocess.run([sys.executable, str(source / 'build.py'), str(directory)], check=True)
        subprocess.run([sys.executable, str(source / 'build.py'), str(directory / 'rebuild')], check=True)
        for required in [c for c in contract['cases'] if c['fixture'] == fixture]:
            gb = required['core'] == 'gameboy'
            system = 'gb' if gb else 'gg'
            data = (directory / f'collect-and-exit.{system}').read_bytes()
            if (sha(data) != required['romSha256'] or
                    data != (directory / 'rebuild' / f'collect-and-exit.{system}').read_bytes()):
                raise ValueError('ROM build differs from the reviewed corpus contract')
            symbol_file = directory / f'symbols.{system}'
            labels = forward.symbols(symbol_file)
            scenarios = json.loads((source / 'scenarios.json').read_text())
            if {s['name'] for s in scenarios} != set(contract['scenarioEndpoints']):
                raise ValueError('scenario obligations changed without a corpus revision')
            cases.append(dict(**required, romHex=data.hex(), scenarios=scenarios,
                scenarioSha256=sha(canonical(scenarios).encode()),
                symbolsSha256=sha(symbol_file.read_bytes()),
                build=json.loads((directory / 'build.json').read_text()),
                readInput=labels['ReadInput'],
                segments=inventory(data, labels, gb, forward, reverse)))
    manifest = dict(schema='proto-time-rom-corpus-build-v1',
                    contract=contract, contractSha256=sha(CONTRACT.read_bytes()), cases=cases)
    (output / 'rom-corpus.json').write_text(canonical(manifest) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, nargs='?')
    parser.add_argument('--check-tools', action='store_true')
    args = parser.parse_args()
    missing = [name for name in ('wla-gb', 'wla-z80', 'wlalink') if not shutil.which(name)]
    if missing:
        parser.exit(2, 'Standalone ROM corpus not run: missing ' + ', '.join(missing) + '\n')
    if args.check_tools:
        parser.exit(2, 'Standalone ROM corpus not run: reconfigure and build the ROM corpus target\n')
    if args.output is None:
        parser.error('output is required')
    build(args.output.resolve())
