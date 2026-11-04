#!/usr/bin/env python3
"""Device-free CLI regression: real transport drains must reach diagnostics."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('emulator', type=Path)
    p.add_argument('--output-dir', type=Path)
    args = p.parse_args()
    out = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='proto-time-occupancy-'))
    if args.output_dir: out.mkdir(parents=True, exist_ok=False)
    try:
        rom = bytearray(32768); rom[:3] = bytes([0xC3, 0, 0])
        (out / 'loop.gg').write_bytes(rom)
        command = [str(args.emulator.resolve()), '--core', 'gamegear', '--rom', str(out / 'loop.gg'),
                   '--headless', '--audio-backend', 'dummy', '--steps', '1000000',
                   '--diagnostics-report', str(out / 'diagnostics.jsonl'),
                   '--diagnostics-interval-ms', '2147483647']
        (out / 'command.json').write_text(json.dumps(command, indent=2))
        with (out / 'process.log').open('w') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=25)
        (out / 'exit-code.txt').write_text(str(result.returncode))
        if result.returncode: raise AssertionError('CLI failed')
        rows = [json.loads(line) for line in (out / 'diagnostics.jsonl').read_text().splitlines()]
        audio = rows[-1]['audio']; occupancy = audio['ready_queue']['drain_occupancy']
        callbacks = audio['drain']['callback_count']
        if not callbacks: raise AssertionError('No output callbacks exercised')
        if not occupancy['sample_count']:
            raise AssertionError(f'Occupancy sample count {occupancy["sample_count"]} despite {callbacks} real drains')
        buckets = sum(value for key, value in occupancy.items() if key.startswith('bucket_'))
        # Independent observational atomics do not promise a coherent snapshot.
        # Check real bucket export without assuming cross-counter atomicity.
        if not buckets:
            raise AssertionError('No drain histogram buckets reached diagnostics')
        if any(not 0 <= occupancy[key] <= audio['ready_queue']['capacity_chunks']
               for key in ('depth_last', 'depth_low_water', 'depth_high_water')):
            raise AssertionError('Invalid exported FIFO depth bounds')
        print('Audio drain occupancy reached CLI diagnostics')
        if not args.output_dir: shutil.rmtree(out)
        return 0
    except Exception as error:
        print(f'{error}; preserved evidence: {out}')
        return 1


if __name__ == '__main__': raise SystemExit(main())
