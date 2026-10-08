#!/usr/bin/env python3
"""Bind research corpus samples to the tested source and build, without admission."""
import argparse
import hashlib
import json
import pathlib
import platform
import re
import statistics
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--experiment', choices=('core-baselines', 'whole-block-model'), default='core-baselines')
    parser.add_argument('--launcher', nargs=argparse.REMAINDER, default=[])
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    binary = args.build.resolve() / ('time-measure-acceleration-corpus' if args.experiment == 'core-baselines'
                                    else 'time-smoke-research-whole-block')
    cache = args.build.resolve() / 'CMakeCache.txt'
    def source_digest():
        files = subprocess.check_output(['git', 'ls-files', '-co', '--exclude-standard', '-z'], cwd=root).split(b'\0')
        source = hashlib.sha256()
        for name in sorted(set(files)):
            if not name:
                continue
            path = root / name.decode()
            if path.is_file():
                source.update(name + b'\0' + bytes.fromhex(digest(path)))
        return source.hexdigest()
    # Snapshot bindings before execution and reject concurrent edits afterwards.
    source_hash = source_digest()
    def build_bindings():
        result = {'binarySha256': digest(binary), 'cmakeCacheSha256': digest(cache)}
        if args.experiment == 'whole-block-model':
            result['generatedSourceSha256'] = digest(args.build.resolve() / 'research-whole-blocks.cpp')
        return result
    bindings = build_bindings()
    command = args.launcher + [str(binary)] + ([] if args.experiment == 'core-baselines' else ['--measure'])
    samples = json.loads(subprocess.check_output(command, cwd=root))
    if bindings != build_bindings():
        raise RuntimeError('build changed during measurement')
    if source_hash != source_digest():
        raise RuntimeError('source changed during measurement')
    grouped = {}
    for sample in samples['samples']:
        if args.experiment == 'core-baselines':
            key = (sample['core'], sample['workload'], sample['backend'])
            grouped.setdefault(key, []).append(sample['nanoseconds'])
        else:
            for backend in ('portable', 'emitted'):
                key = ('IR-host-model', str(sample['fixture']), backend)
                grouped.setdefault(key, []).append(sample[backend]['nanoseconds'])
    samples['summaries'] = [dict(core=k[0], workload=k[1], backend=k[2],
                                medianNanoseconds=statistics.median(v),
                                minNanoseconds=min(v), maxNanoseconds=max(v),
                                standardDeviationNanoseconds=statistics.pstdev(v))
                            for k, v in grouped.items()]
    governors = {str(p): p.read_text().strip() for p in
                 pathlib.Path('/sys/devices/system/cpu').glob('cpu*/cpufreq/scaling_governor')}
    compiler_file = next((args.build.resolve() / 'CMakeFiles').glob('*/CMakeCXXCompiler.cmake'))
    compiler = re.search(r'set\(CMAKE_CXX_COMPILER "([^"]+)"\)', compiler_file.read_text()).group(1)
    compiler_version = subprocess.check_output([compiler, '--version'], text=True)
    samples['binding'] = dict(sourceSha256=source_hash, **bindings,
                             host=platform.uname()._asdict(), governors=governors,
                             compilerVersion=compiler_version, experiment=args.experiment,
                             cpuInfo=pathlib.Path('/proc/cpuinfo').read_text(),
                             cmakeCache=cache.read_text(), launcher=args.launcher,
                             measurementKind='emulated-throughput' if args.launcher else 'native-throughput')
    samples['decision'] = 'pending: redesigned whole-block backend and review required'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(samples, indent=2) + '\n')


if __name__ == '__main__':
    main()
