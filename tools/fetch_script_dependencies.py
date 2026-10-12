#!/usr/bin/env python3
"""Fetch pinned, hash-verified runtime sources into explicitly chosen build storage."""
import argparse
import hashlib
import json
import tarfile
import tempfile
import urllib.request
from pathlib import Path

SOURCES = {
    'lua': {'version': '5.4.9', 'source': 'https://www.lua.org/ftp/lua-5.4.9.tar.gz',
            'sha256': '2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6'},
    'quickjs': {'version': '2026-06-04', 'source': 'https://bellard.org/quickjs/quickjs-2026-06-04.tar.xz',
                'sha256': 'b376e839b322978313d929fd20663b11ba58b75df5a46c126dd19ea2fa70ad2a'},
}


def fetch(destination):
    destination.mkdir(parents=True, exist_ok=True)
    for name, source in SOURCES.items():
        archive = destination / source['source'].rsplit('/', 1)[-1]
        if not archive.is_file():
            with urllib.request.urlopen(source['source'], timeout=60) as response:
                data = response.read(16 * 1024 * 1024 + 1)
            if len(data) > 16 * 1024 * 1024:
                raise ValueError('runtime archive exceeds download budget')
            if hashlib.sha256(data).hexdigest() != source['sha256']:
                raise ValueError('runtime archive hash mismatch: ' + name)
            archive.write_bytes(data)
        elif hashlib.sha256(archive.read_bytes()).hexdigest() != source['sha256']:
            raise ValueError('existing runtime archive hash mismatch: ' + name)
        directory = destination / (name + '-' + source['version'])
        with tempfile.TemporaryDirectory(prefix='extract-', dir=destination) as temporary:
            staging = Path(temporary)
            with tarfile.open(archive) as package:
                members = package.getmembers()
                if any(m.issym() or m.islnk() or m.name.startswith('/') or '..' in Path(m.name).parts or
                       not (m.isfile() or m.isdir()) for m in members):
                    raise ValueError('unsafe runtime archive member')
                if sum(m.size for m in members) > 64 * 1024 * 1024:
                    raise ValueError('runtime extraction budget exhausted')
                package.extractall(staging, members=members, filter='data')
            extracted = staging / directory.name
            if not extracted.is_dir():
                raise ValueError('runtime archive root mismatch')
            if directory.exists():
                expected = {p.relative_to(extracted): hashlib.sha256(p.read_bytes()).hexdigest()
                            for p in extracted.rglob('*') if p.is_file()}
                actual = {p.relative_to(directory): hashlib.sha256(p.read_bytes()).hexdigest()
                          for p in directory.rglob('*') if p.is_file()}
                if expected != actual:
                    raise ValueError('existing runtime source differs from pinned archive: ' + name)
            else:
                extracted.rename(directory)
    (destination / 'manifest.json').write_text(json.dumps(SOURCES, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    fetch(parser.parse_args().output.resolve())
