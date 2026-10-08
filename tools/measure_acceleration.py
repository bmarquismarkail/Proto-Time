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


def summarize(samples, experiment):
    grouped = {}
    for sample in samples['samples']:
        if experiment != 'whole-block-model':
            key = (sample['core'], sample['workload'], sample['backend'])
            grouped.setdefault(key, []).append(sample['nanoseconds'])
        else:
            for backend in ('portable', 'emitted'):
                key = ('IR-host-model', str(sample['fixture']), backend)
                grouped.setdefault(key, []).append(sample[backend]['nanoseconds'])
    summaries = [dict(core=k[0], workload=k[1], backend=k[2],
                      medianNanoseconds=statistics.median(v),
                      minNanoseconds=min(v), maxNanoseconds=max(v),
                      standardDeviationNanoseconds=statistics.pstdev(v))
                 for k, v in grouped.items()]
    if experiment == 'whole-block-corpus':
        # The binary publishes only after per-retirement and timed endpoint
        # checks pass. Bind comparisons to its explicit correctness records.
        correctness = {(c['core'], c['workload']): c for c in samples['correctness']}
        expected_cases = {(core, workload) for core in ('gameboy', 'gamegear')
                          for workload in ('compute-heavy', 'ram-heavy', 'mixed-devices')}
        if set(correctness) != expected_cases or {k[:2] for k in grouped} != expected_cases:
            raise ValueError('incomplete corpus cases')
        comparisons = []
        for core, workload in sorted({k[:2] for k in grouped}):
            check = correctness[(core, workload)]
            if check['status'] != 'passed' or set(check['backends']) != {'cached-block', 'portable-ir', 'emitted'}:
                raise ValueError('missing whole-block corpus correctness evidence')
            candidates = ('baseline', 'cached-block', 'portable-ir')
            comparator = min(candidates, key=lambda b: statistics.median(grouped[(core, workload, b)]))
            base = statistics.median(grouped[(core, workload, comparator)])
            emitted = statistics.median(grouped[(core, workload, 'emitted')])
            by_repeat = {}
            for sample in samples['samples']:
                if (sample['core'], sample['workload']) == (core, workload):
                    row = by_repeat.setdefault(sample['repeat'], {})
                    if sample['backend'] in row:
                        raise ValueError('duplicate backend sample')
                    row[sample['backend']] = sample
            paired = []
            for repeat, row in sorted(by_repeat.items()):
                if set(row) != {*candidates, 'emitted'}:
                    raise ValueError('incomplete backend repetition')
                state_keys = ('romSha256', 'fingerprint', 'registers', 'cycles',
                              'warmupFingerprint', 'warmupCycles', 'instructions')
                reference = {k: row['baseline'][k] for k in state_keys}
                for sample in row.values():
                    if {k: sample[k] for k in state_keys} != reference:
                        raise ValueError('whole-block corpus sample correctness mismatch')
                    if sample['instructions'] != samples['measuredInstructions'] or sample['nanoseconds'] <= 0:
                        raise ValueError('invalid measurement accounting')
                    if sample['emittedInstructions'] + sample['nonEmittedInstructions'] != sample['instructions']:
                        raise ValueError('invalid execution coverage')
                native = row['emitted']
                paired.append(dict(repeat=repeat,
                    improvementPercent=100 * (1 - native['nanoseconds'] / row[comparator]['nanoseconds']),
                    emittedFraction=native['emittedInstructions'] / native['instructions']))
            if len(paired) != samples['repetitions']:
                raise ValueError('missing repetitions')
            comparisons.append(dict(core=core, workload=workload, comparator=comparator,
                comparatorMedianNanoseconds=base, emittedMedianNanoseconds=emitted,
                improvementPercent=100 * (1 - emitted / base), pairedSamples=paired,
                baselineMedianNanoseconds=statistics.median(grouped[(core, workload, 'baseline')]),
                baselineImprovementPercent=100 * (1 - emitted / statistics.median(
                    grouped[(core, workload, 'baseline')]))))
        samples['comparisons'] = comparisons
        samples['comparisonScope'] = ('same segmented scheduler on all four paths; fastest validated '
                                      'existing backend per case; synthetic patterns, no latency/admission decision')
    return summaries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=pathlib.Path, required=True)
    parser.add_argument('--output', type=pathlib.Path, required=True)
    parser.add_argument('--experiment', choices=('core-baselines', 'whole-block-model', 'whole-block-corpus'), default='core-baselines')
    parser.add_argument('--smoke', action='store_true', help='short correctness/measurement plumbing run')
    parser.add_argument('--launcher', nargs=argparse.REMAINDER, default=[])
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    binaries = {'core-baselines': 'time-measure-acceleration-corpus',
                'whole-block-model': 'time-smoke-research-whole-block',
                'whole-block-corpus': 'time-measure-whole-block-corpus'}
    if args.smoke and args.experiment == 'whole-block-model':
        parser.error('--smoke is not supported for whole-block-model measurements')
    binary = args.build.resolve() / binaries[args.experiment]
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
        if args.experiment != 'core-baselines':
            result['generatedSourceSha256'] = digest(args.build.resolve() / 'research-whole-blocks.cpp')
        return result
    bindings = build_bindings()
    command = args.launcher + [str(binary)]
    if args.experiment == 'whole-block-model':
        command.append('--measure')
    elif args.smoke:
        command.append('--smoke')
    samples = json.loads(subprocess.check_output(command, cwd=root))
    if bindings != build_bindings():
        raise RuntimeError('build changed during measurement')
    if source_hash != source_digest():
        raise RuntimeError('source changed during measurement')
    samples['summaries'] = summarize(samples, args.experiment)
    governors = {str(p): p.read_text().strip() for p in
                 pathlib.Path('/sys/devices/system/cpu').glob('cpu*/cpufreq/scaling_governor')}
    compiler_file = next((args.build.resolve() / 'CMakeFiles').glob('*/CMakeCXXCompiler.cmake'))
    compiler = re.search(r'set\(CMAKE_CXX_COMPILER "([^"]+)"\)', compiler_file.read_text()).group(1)
    compiler_version = subprocess.check_output([compiler, '--version'], text=True)
    samples['binding'] = dict(sourceSha256=source_hash, **bindings,
                             host=platform.uname()._asdict(), governors=governors,
                             compilerVersion=compiler_version, experiment=args.experiment,
                             command=command, smoke=args.smoke,
                             cpuInfo=pathlib.Path('/proc/cpuinfo').read_text(),
                             cmakeCache=cache.read_text(), launcher=args.launcher,
                             measurementKind='emulated-throughput' if args.launcher else 'native-throughput')
    samples['decision'] = 'pending: latency gates, representative game coverage, native ARM64 performance (user-deferred), and review required'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(samples, indent=2) + '\n')


if __name__ == '__main__':
    main()
