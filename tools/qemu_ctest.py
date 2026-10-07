#!/usr/bin/env python3
"""Run cross-built CTest binaries, including binaries invoked by Python tests.

Uses an isolated shadow test directory and host wrappers; never changes binfmt,
the generated binaries, or the original CTest file. Does not certify performance.
"""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path


def arm64_elf(path):
    if re.search(r'\.so(?:\.\d+)*$', path.name):
        return False
    try:
        with path.open('rb') as f:
            header = f.read(64)
            if len(header)!=64 or header[:4]!=b'\x7fELF' or header[4:6]!=b'\x02\x01' or header[18:20]!=b'\xb7\x00':
                return False
            if header[16:18]==b'\x02\x00':
                return True
            if header[16:18]!=b'\x03\x00':
                return False
            # A PIE executable has PT_INTERP; a shared module must stay an ELF
            # argument so the guest process can load it through dlopen.
            offset=int.from_bytes(header[32:40], 'little')
            size=int.from_bytes(header[54:56], 'little')
            count=int.from_bytes(header[56:58], 'little')
            if size<4 or count>1024:
                return False
            for i in range(count):
                f.seek(offset+i*size)
                if f.read(4)==b'\x03\x00\x00\x00':
                    return True
            return False
    except OSError:
        return False


def prepare(build, output, qemu, sysroot, timeout_scale=1.0):
    original = (build / 'CTestTestfile.cmake').read_text()
    if re.search(r'^subdirs\(', original, re.MULTILINE):
        raise ValueError('nested test directories require a separate shadow file')
    output.mkdir(parents=True, exist_ok=True)
    wrappers = output / 'bin'
    wrappers.mkdir(exist_ok=True)
    bindings = []
    for binary in sorted(build.iterdir()):
        if not binary.is_file() or not arm64_elf(binary):
            continue
        wrapper = wrappers / binary.name
        wrapper.write_text('#!' + sys.executable + '\nimport os,sys\nos.execv(' + repr(str(qemu)) + ', ' +
                           repr([str(qemu), '-L', str(sysroot), str(binary)]) + ' + sys.argv[1:])\n')
        wrapper.chmod(0o755)
        bindings.append({'binary': str(binary), 'sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                         'wrapper': str(wrapper), 'wrapperSha256': hashlib.sha256(wrapper.read_bytes()).hexdigest()})
    lines = []
    for line in original.splitlines():
        # Keep the direct guest executable following CMake's QEMU/-L/sysroot
        # prefix. Other executable arguments need host launchers, including
        # children started by guest C++ CLI tests through a host shell.
        if line.startswith('add_test('):
            arguments=list(re.finditer(r'"([^"\n]+)"',line))
            launcher = arguments[0].group(1) if arguments else ''
            uses_qemu = bool(launcher) and Path(launcher).is_absolute() and Path(launcher).resolve() == qemu
            protected=4 if uses_qemu else 1
            replacements={b['binary']:b['wrapper'] for b in bindings}
            for argument in reversed(arguments[protected:]):
                value=argument.group(1)
                # CMake preserves the configured path while a moved build may
                # be reached through a symlink. Bind aliases to the same ELF.
                resolved=str(Path(value).resolve()) if Path(value).is_absolute() else value
                if resolved in replacements:
                    line=line[:argument.start(1)]+replacements[resolved]+line[argument.end(1):]
        if line.startswith('set_tests_properties(') and 'WORKING_DIRECTORY' not in line:
            line = line.replace(' PROPERTIES ', ' PROPERTIES WORKING_DIRECTORY "' + str(build) + '" ', 1)
        if line.startswith('set_tests_properties('):
            line=re.sub(r' TIMEOUT "([0-9.]+)"',lambda m:' TIMEOUT "'+str(float(m.group(1))*timeout_scale)+'"',line)
        lines.append(line)
    (output / 'CTestTestfile.cmake').write_text('\n'.join(lines) + '\n')
    (output / 'qemu-bindings.json').write_text(json.dumps({'execution': 'QEMU ARM64 user mode',
        'nativePerformanceAcceptance': 'not run', 'build': str(build), 'sysroot': str(sysroot),
        'qemu': str(qemu), 'qemuSha256': hashlib.sha256(qemu.read_bytes()).hexdigest(), 'outerTimeoutScale':timeout_scale,
        'bindings': bindings}, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--qemu', type=Path, required=True)
    parser.add_argument('--sysroot', type=Path, required=True)
    parser.add_argument('--timeout-scale',type=float,default=1.0,help='Scale outer CTest timeouts only; latency thresholds remain unchanged')
    args, ctest_args = parser.parse_known_args()
    build, output, qemu, sysroot = [p.resolve() for p in (args.build, args.output, args.qemu, args.sysroot)]
    if output == build or not qemu.is_file() or not sysroot.is_dir() or not 1<=args.timeout_scale<=100:
        parser.error('use a separate output directory, existing QEMU binary and target sysroot')
    prepare(build, output, qemu, sysroot,args.timeout_scale)
    return subprocess.run(['ctest', '--test-dir', str(output), '--output-on-failure', *ctest_args]).returncode


if __name__ == '__main__':
    sys.exit(main())
