#!/usr/bin/env python3
"""Run standalone ARM64 C++ tests in an ephemeral QEMU kernel/initramfs guest.

Useful for ThreadSanitizer, which needs guest /proc memory maps. Inputs must be
prepared trusted kernel/QEMU/userspace binaries. No host mount, network, disk,
binfmt or service configuration is changed. This is not physical acceptance.
"""
import argparse
import hashlib
import json
import re
import shutil
import shlex
import subprocess
import tempfile
from pathlib import Path
from qemu_ctest import arm64_elf


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['build','sysroot','qemu','kernel','busybox','output']:
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--tests',nargs='+',default=['time-smoke-space','time-smoke-space-execution',
        'time-smoke-space-gamegear','time-smoke-space-exploration','time-smoke-space-meaning'])
    parser.add_argument('--test-module', action='append', default=[], metavar='TEST=MODULE',
        help='copy a build-root module and pass its guest path as the test argument')
    parser.add_argument('--timeout',type=int,default=3600)
    parser.add_argument('--guest-resource',action='append',default=[],metavar='GUEST=HOST',help='copy an explicit file/tree into the isolated guest; no host mount')
    parser.add_argument('--loopback',action='store_true',help='enable only the isolated guest loopback interface; no host or external network')
    parser.add_argument('--test-argument',action='append',default=[],metavar='TEST=VALUE',help='append a shell-quoted literal argument to one selected test')
    parser.add_argument('--env',action='append',default=[],metavar='NAME=VALUE',help='explicit guest test environment')
    args=parser.parse_args()
    for name in ['build','sysroot','qemu','kernel','busybox','output']:
        setattr(args,name,getattr(args,name).resolve())
    if args.output in [args.build,args.sysroot] or args.timeout<1:
        parser.error('use a separate output directory and a positive timeout')
    if not all(p.is_file() for p in [args.qemu,args.kernel,args.busybox]):
        parser.error('QEMU, kernel and BusyBox files are required')
    for test in args.tests:
        if not re.fullmatch(r'[A-Za-z0-9_.-]+',test) or not arm64_elf(args.build/test):
            parser.error('tests must name existing ARM64 executables in the build root')
    modules = {}
    for item in args.test_module:
        test, separator, module = item.partition('=')
        if (not separator or test not in args.tests or test in modules or
                not re.fullmatch(r'[A-Za-z0-9_.-]+', module)):
            parser.error('test-module must bind one selected test to a build-root filename')
        path = args.build / module
        if not path.is_file():
            parser.error('test module does not exist')
        header = path.read_bytes()[:20]
        if len(header) < 20 or header[:6] != b'\x7fELF\x02\x01' or header[18:20] != b'\xb7\x00':
            parser.error('test modules must be ARM64 ELF files')
        modules[test] = module
    arguments = {}
    for item in args.test_argument:
        test, separator, value = item.partition('=')
        if not separator or test not in args.tests or not value or len(value) > 4096 or '\x00' in value:
            parser.error('test-argument requires a selected TEST and a bounded literal VALUE')
        arguments.setdefault(test, []).append(value)
        if len(arguments[test]) > 128 or sum(len(v) for v in arguments[test]) > 65536:
            parser.error('test argument byte budget exhausted')
    args.output.mkdir(parents=True,exist_ok=True)
    resources = []
    resource_bytes = 0
    destinations = set()
    for item in args.guest_resource:
        guest, separator, host = item.partition('=')
        target = Path(guest)
        source = Path(host).resolve()
        if not separator or not target.is_absolute() or '..' in target.parts or guest in destinations or guest in ['/init','/bin/busybox'] or not source.exists():
            parser.error('guest-resource requires a unique absolute guest path and an existing host file/tree')
        destinations.add(guest)
        files = [source] if source.is_file() else sorted(p for p in source.rglob('*') if p.is_file())
        if len(files) > 30000:
            parser.error('guest resource file-count budget exhausted')
        entries = []
        for path in files:
            if not path.resolve().is_relative_to(source if source.is_dir() else source.parent):
                parser.error('guest resource symlink escapes its selected source')
            resource_bytes += path.stat().st_size
            if resource_bytes > 256*1024*1024:
                parser.error('guest resource byte budget exhausted')
            destination = target if source.is_file() else target/path.relative_to(source)
            entries.append({'source':str(path),'guest':str(destination),'sha256':sha(path)})
        resources.extend(entries)
    environment = {}
    for item in args.env:
        name, separator, value = item.partition('=')
        if not separator or not re.fullmatch(r'[A-Z][A-Z0-9_]*',name) or name in environment or '\x00' in value:
            parser.error('guest env requires unique uppercase NAME=VALUE entries')
        environment[name] = value
    report={'execution':'QEMU ARM64 full-system guest','status':'not run',
        'physicalAcceptance':'not run','runnerSha256':sha(Path(__file__)), 'qemuSha256':sha(args.qemu),'kernelSha256':sha(args.kernel),
        'busyboxSha256':sha(args.busybox),'tests':[{'name':t,'binarySha256':sha(args.build/t),
            **({'module': modules[t], 'moduleSha256': sha(args.build/modules[t])} if t in modules else {})} for t in args.tests]}
    if (args.build/'CMakeCache.txt').is_file():
        report['cmakeCacheSha256']=sha(args.build/'CMakeCache.txt')
    report['resources']=resources;report['environment']=environment
    report['loopback']=args.loopback;report['arguments']=arguments
    with tempfile.TemporaryDirectory(prefix='guest-',dir=args.output) as temporary:
        root=Path(temporary)
        for directory in ['bin','lib','dev','proc','sys','tmp','tests']:
            (root/directory).mkdir()
        shutil.copy2(args.busybox,root/'bin/busybox')
        libraries=args.sysroot/'lib'
        report['libraries']=[]
        for library in sorted(libraries.glob('*.so*')):
            # Copy the resolved bytes so no guest link depends on a host path.
            shutil.copy2(library,root/'lib'/library.name)
            report['libraries'].append({'name':library.name,'sha256':sha(library)})
        (root/'lib64').symlink_to('lib')
        for entry in resources:
            destination=root/entry['guest'].lstrip('/')
            if destination.exists():
                raise ValueError('guest resource collides with prepared runtime: '+entry['guest'])
            destination.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(entry['source'],destination)
        commands=['#!/bin/busybox sh','/bin/busybox mount -t devtmpfs devtmpfs /dev',
            'exec </dev/console >/dev/console 2>&1','/bin/busybox mount -t proc proc /proc',
            '/bin/busybox mount -t sysfs sysfs /sys','export LD_LIBRARY_PATH=/lib',
            'export TSAN_OPTIONS=halt_on_error=1:exitcode=66','echo TIME_QEMU_GUEST_START',
            '/bin/busybox uname -a']
        if args.loopback:
            commands.append('/bin/busybox ip link set lo up || { echo TIME_QEMU_LOOPBACK_FAILED; /bin/busybox poweroff -f; exit 1; }')
        commands.extend('export '+name+'='+shlex.quote(value) for name,value in environment.items())
        for test in args.tests:
            shutil.copy2(args.build/test,root/'tests'/test)
            invocation = '/tests/' + test
            if test in modules:
                shutil.copy2(args.build/modules[test], root/'tests'/modules[test])
                invocation += ' ' + shlex.quote('/tests/' + modules[test])
            invocation += ''.join(' '+shlex.quote(value) for value in arguments.get(test, []))
            commands.extend(['echo TIME_QEMU_TEST_START='+test,invocation,
                'result=$?','echo TIME_QEMU_TEST_RESULT='+test+':$result'])
        commands.extend(['echo TIME_QEMU_GUEST_DONE','/bin/busybox poweroff -f'])
        (root/'init').write_text('\n'.join(commands)+'\n');(root/'init').chmod(0o755)
        image=args.output/'guest.cpio.gz'
        with image.open('wb') as out:
            archive=subprocess.Popen(['bsdtar','--format=newc','-cf','-','-C',str(root),'.'],stdout=subprocess.PIPE)
            try:
                subprocess.run(['gzip','-c'],stdin=archive.stdout,stdout=out,check=True)
            finally:
                archive.stdout.close()
                archive_status=archive.wait()
            if archive_status:
                raise RuntimeError('initramfs archive failed: '+str(archive_status))
        report['initramfsSha256']=sha(image)
    command=[str(args.qemu),'-machine','virt','-cpu','max','-m','2048','-smp','2',
        '-kernel',str(args.kernel),'-initrd',str(image),'-append','console=ttyAMA0 rdinit=/init panic=1',
        '-display','none','-monitor','none','-serial','stdio','-nic','none','-no-reboot']
    report['command']=command;log=args.output/'guest.log'
    print('ARM64 kernel test log: '+str(log),flush=True)
    with log.open('w') as out:
        process=subprocess.Popen(command,stdout=out,stderr=subprocess.STDOUT)
        try:
            report['qemuReturncode']=process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            process.kill();process.wait();report['qemuReturncode']=124
    text=log.read_text(errors='replace')
    results=dict(re.findall(r'TIME_QEMU_TEST_RESULT=([A-Za-z0-9_.-]+):(\d+)',text))
    for test in report['tests']:
        test['status']='passed' if results.get(test['name'])=='0' else 'failed' if test['name'] in results else 'not run'
    complete='TIME_QEMU_GUEST_DONE' in text and report['qemuReturncode']==0
    clean='WARNING: ThreadSanitizer' not in text and 'FATAL: ThreadSanitizer' not in text
    report['status']='passed' if complete and clean and all(t['status']=='passed' for t in report['tests']) else 'failed'
    report['logSha256']=sha(log)
    (args.output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'status':report['status'],'tests':report['tests']}))
    return int(report['status']!='passed')


if __name__=='__main__':
    raise SystemExit(main())
