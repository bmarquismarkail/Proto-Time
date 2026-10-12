#!/usr/bin/env python3
"""Collect/evaluate native x86-64 acceleration evidence without opening admission."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import platform
import re
import statistics
import subprocess
import sys

import measure_acceleration as measure

ROOT = Path(__file__).resolve().parents[1]
REPETITIONS = 9
EXPERIMENTS = ('core-baselines', 'whole-block-corpus', 'whole-block-roms')
REQUIRED_TESTS = {'perf_concurrency_success_gates', 'perf_headless_scheduling_video',
                  'perf-simd-pixel-ops', 'perf-gameboy-block-cache', 'perf-gamegear-ir-dispatch'}
REQUIRED_GATES = {'headless_frame_residence_p99', 'headless_presentation_p99',
                  'headless_scheduling_to_present_p99', 'headless_input_to_frame_response_p99'}
BINARY_NAMES = {'core-baselines': 'time-measure-acceleration-corpus',
                'whole-block-corpus': 'time-measure-whole-block-corpus',
                'whole-block-roms': 'time-measure-whole-block-roms'}


def require_x86_binary(path):
    # Reject binfmt-mediated ARM execution even when no explicit launcher exists.
    with path.open('rb') as file:
        header = file.read(20)
    if header[:6] != b'\x7fELF\x02\x01' or header[18:20] != b'\x3e\x00':
        raise ValueError('not a native ELF64 x86-64 executable: ' + str(path))


def thresholds(comparisons):
    """Repeatable means a median or majority of paired regressions above 5%."""
    failures, cores = [], {}
    for row in comparisons:
        paired = row['pairedSamples']
        improvement = row['improvementPercent']
        if (len(paired) != REPETITIONS or
                not math.isfinite(improvement) or
                any(not math.isfinite(p['improvementPercent']) for p in paired)):
            raise ValueError('missing/invalid nine-repetition comparison')
        regressed = sum(p['improvementPercent'] < -5 for p in paired)
        if improvement < -5 or regressed > REPETITIONS // 2:
            failures.append(f"{row['core']}/{row['workload']}: repeatable regression above 5%")
        cores.setdefault(row['core'], []).append(improvement)
    if set(cores) != {'gameboy', 'gamegear'}:
        raise ValueError('missing core comparisons')
    return failures, {core: statistics.median(values) for core, values in cores.items()}


def check_baselines(report):
    rows = {}
    for sample in report['samples']:
        key = (sample['core'], sample['workload'], sample['repeat'], sample['backend'])
        if key in rows or sample['nanoseconds'] <= 0:
            raise ValueError('duplicate/invalid baseline sample')
        rows[key] = sample
    expected = {(c, w, r, b) for c in ('gameboy', 'gamegear') for w in ('compute', 'ram', 'devices')
                for r in range(REPETITIONS) for b in ('baseline', 'cached-block', 'portable-ir')}
    if set(rows) != expected or report['measuredInstructions'] != 100000 or report['warmupInstructions'] != 4096:
        raise ValueError('incomplete baseline corpus')
    for core in ('gameboy', 'gamegear'):
        for workload in ('compute', 'ram', 'devices'):
            reference = rows[(core, workload, 0, 'baseline')]
            for key, sample in rows.items():
                if key[:2] == (core, workload) and any(sample[k] != reference[k]
                        for k in ('fingerprint', 'cycles', 'romSha256')):
                    raise ValueError('baseline correctness mismatch')


def check_test_log(text, expected):
    """CTest can exit successfully with skipped tests; require every result."""
    results = re.findall(r'^\s*\d+/\d+\s+Test\s+#\d+:\s+(\S+)\s+\.+\s*(\S.*)$',
                         text, flags=re.MULTILINE)
    if len(results) != len(expected) or {name for name, _ in results} != expected:
        raise ValueError('missing/duplicate discovered CTest results')
    if any(re.search(r'Skipped|Not Run|Disabled', result, flags=re.IGNORECASE)
           for _, result in results):
        raise ValueError('skipped/disabled CTest evidence cannot establish acceptance')
    return all(result.startswith('Passed ') for _, result in results)


def evaluate(directory, root=ROOT):
    bundle = json.loads((directory / 'evidence.json').read_text())
    if bundle['schema'] != 'proto-time-x86-acceptance-v1' or bundle['sourceSha256'] != measure.source_digest(root):
        raise ValueError('stale/incompatible acceptance evidence')
    build = Path(bundle['build'])
    cache_hash = measure.digest(build / 'CMakeCache.txt')
    if bundle['cmakeCacheSha256'] != cache_hash or bundle['host']['system'] != 'Linux' or bundle['host']['machine'] != 'x86_64':
        raise ValueError('wrong/stale native x86-64 build')
    if 'BMMQ_ENABLE_TSAN:BOOL=ON' in (build / 'CMakeCache.txt').read_text():
        raise ValueError('sanitizer throughput cannot establish native acceptance')
    discovery = json.loads(subprocess.check_output(
        ['ctest', '--test-dir', str(build), '-N', '-L', 'performance', '--show-only=json-v1'], text=True))
    tests = {t['name'] for t in discovery['tests']}
    if set(bundle['performanceTests']) != tests or not REQUIRED_TESTS <= tests:
        raise ValueError('missing/changed required performance tests')
    binaries = {t['command'][0] for t in discovery['tests']}
    if set(bundle['performanceBinaries']) != binaries or any(
            measure.digest(Path(path)) != sha for path, sha in bundle['performanceBinaries'].items()):
        raise ValueError('stale performance executables')
    for binary in binaries: require_x86_binary(Path(binary))
    required_artifacts = {e + '.json' for e in EXPERIMENTS} | {'full-ctest.log'} | {
        f'latency-{i}.log' for i in range(REPETITIONS)}
    if set(bundle['artifacts']) != required_artifacts:
        raise ValueError('missing required acceptance artifact bindings')
    for name, sha in bundle['artifacts'].items():
        if measure.digest(directory / name) != sha:
            raise ValueError('changed acceptance artifact: ' + name)
    reports = {}
    for experiment in EXPERIMENTS:
        report = json.loads((directory / (experiment + '.json')).read_text())
        binding = report['binding']
        if (report['repetitions'] != REPETITIONS or binding['smoke'] or binding['launcher'] or
                binding['measurementKind'] != 'native-throughput' or binding['host']['system'] != 'Linux' or
                binding['host'] != bundle['host'] or binding['experiment'] != experiment or
                binding['sourceSha256'] != bundle['sourceSha256'] or binding['cmakeCacheSha256'] != cache_hash or
                binding['command'] != [str(build / BINARY_NAMES[experiment])] or
                measure.digest(build / BINARY_NAMES[experiment]) != binding['binarySha256']):
            raise ValueError('missing/stale native full measurement: ' + experiment)
        require_x86_binary(build / BINARY_NAMES[experiment])
        if experiment != 'core-baselines':
            generated = 'research-rom-blocks.cpp' if experiment == 'whole-block-roms' else 'research-whole-blocks.cpp'
            if measure.digest(build / generated) != binding['generatedSourceSha256']:
                raise ValueError('stale generated backend')
        if experiment == 'whole-block-roms':
            manifest_path = build / 'acceleration-roms/rom-corpus.json'
            manifest = json.loads(manifest_path.read_text())
            manifest_sha = hashlib.sha256(json.dumps(manifest, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
            if measure.digest(manifest_path) != binding['romManifestSha256'] or manifest_sha != report['manifestSha256']:
                raise ValueError('stale compiled ROM manifest')
        measure.summarize(report, experiment, root=root)
        if experiment == 'core-baselines': check_baselines(report)
        reports[experiment] = report
    failures, _ = thresholds(reports['whole-block-corpus']['comparisons'])
    rom_failures, gains = thresholds(reports['whole-block-roms']['comparisons'])
    failures.extend(rom_failures)
    for core, gain in gains.items():
        if gain < 10: failures.append(f'{core}: representative median improvement {gain:.2f}% below 10%')
    latency = bundle['latency']
    if len(latency) != REPETITIONS or {r['repeat'] for r in latency} != set(range(REPETITIONS)):
        raise ValueError('missing nine latency repetitions')
    for run in latency:
        if run['log'] != f"latency-{run['repeat']}.log":
            raise ValueError('invalid latency log binding')
        text = (directory / run['log']).read_text()
        tests_passed = check_test_log(text, tests)
        # The CTest exit code remains authoritative for every discovered gate.
        gates = re.findall(r'gate (headless_\w+) observed_ns=(\d+) limit_ns=(\d+) enforced=(\w+) result=(\w+)', text)
        if len(gates) != 4 or {g[0] for g in gates} != REQUIRED_GATES:
            raise ValueError('missing/duplicate scheduling latency evidence')
        if any(int(g[2]) != 16000000 or g[3] != 'true' for g in gates):
            raise ValueError('latency thresholds disabled or changed')
        if run['exitCode'] or not tests_passed or any(int(g[1]) >= 16000000 or g[4] != 'pass' for g in gates):
            failures.append(f"latency repetition {run['repeat']}: existing gate failed")
    if bundle['fullCtest']['log'] != 'full-ctest.log':
        raise ValueError('missing full suite log binding')
    full_discovery = json.loads(subprocess.check_output(
        ['ctest', '--test-dir', str(build), '-N', '--show-only=json-v1'], text=True))
    full_tests = {t['name'] for t in full_discovery['tests']}
    full_text = (directory / 'full-ctest.log').read_text()
    full_passed = check_test_log(full_text, full_tests)
    if bundle['fullCtest']['exitCode'] or not full_passed:
        failures.append('full discovered CTest suite failed')
    elif not re.search(r'100% tests passed[^\n]*(?:0 tests failed out of \d+|out of \d+)',
                       full_text):
        raise ValueError('missing successful full CTest evidence')
    return dict(schema='proto-time-x86-acceptance-result-v1', sourceSha256=bundle['sourceSha256'],
                checkedSourceRoot=str(root.resolve()),
                status='failed' if failures else 'thresholds-met-awaiting-review', failures=failures,
                representativeMedianImprovementPercent=gains,
                nativeArm64='user-deferred; not evaluated', admissionChanged=False)


def collect(build, directory):
    if platform.system() != 'Linux' or platform.machine() != 'x86_64':
        raise ValueError('requires a native Linux x86-64 runner')
    if 'BMMQ_ENABLE_TSAN:BOOL=ON' in (build / 'CMakeCache.txt').read_text():
        raise ValueError('requires an unsanitized build')
    source = measure.source_digest(ROOT)
    directory.mkdir(parents=True, exist_ok=True)
    discovery = json.loads(subprocess.check_output(
        ['ctest', '--test-dir', str(build), '-N', '-L', 'performance', '--show-only=json-v1'], text=True))
    tests = [t['name'] for t in discovery['tests']]
    if not REQUIRED_TESTS <= set(tests): raise ValueError('missing required performance tests')
    binaries = {Path(t['command'][0]): measure.digest(Path(t['command'][0])) for t in discovery['tests']}
    for binary in {*binaries, *(build / name for name in BINARY_NAMES.values())}:
        require_x86_binary(binary)
    cache_sha = measure.digest(build / 'CMakeCache.txt')
    bundle = dict(schema='proto-time-x86-acceptance-v1', sourceSha256=source,
                  build=str(build), cmakeCacheSha256=cache_sha, host=platform.uname()._asdict(),
                  performanceTests=tests, performanceBinaries={str(k): v for k, v in binaries.items()}, latency=[])
    for experiment in EXPERIMENTS:
        print('Measuring ' + experiment, flush=True)
        subprocess.run([sys.executable, str(ROOT / 'tools/measure_acceleration.py'), '--build', str(build),
                        '--experiment', experiment, '--output', str(directory / (experiment + '.json'))], check=True)
    for repeat in range(REPETITIONS):
        name = f'latency-{repeat}.log'
        print(f'Existing performance gates {repeat + 1}/{REPETITIONS}', flush=True)
        with (directory / name).open('w') as out:
            run = subprocess.run(['ctest', '--test-dir', str(build), '-V', '-L', 'performance', '--output-on-failure'], stdout=out, stderr=subprocess.STDOUT)
        bundle['latency'].append(dict(repeat=repeat, log=name, exitCode=run.returncode))
    print('Full discovered CTest suite', flush=True)
    with (directory / 'full-ctest.log').open('w') as out:
        run = subprocess.run(['ctest', '--test-dir', str(build), '--output-on-failure'], stdout=out, stderr=subprocess.STDOUT)
    bundle['fullCtest'] = dict(log='full-ctest.log', exitCode=run.returncode)
    if (source != measure.source_digest(ROOT) or cache_sha != measure.digest(build / 'CMakeCache.txt') or
            any(measure.digest(path) != sha for path, sha in binaries.items())):
        raise ValueError('source/build changed during acceptance collection')
    names = [e + '.json' for e in EXPERIMENTS] + [r['log'] for r in bundle['latency']] + ['full-ctest.log']
    bundle['artifacts'] = {name: measure.digest(directory / name) for name in names}
    (directory / 'evidence.json').write_text(json.dumps(bundle, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path)
    parser.add_argument('--output', type=Path, required=True, help='evidence directory')
    parser.add_argument('--check', action='store_true', help='evaluate retained evidence without running tests')
    parser.add_argument('--source-root', type=Path, default=ROOT,
                        help='exact tested checkout for retained evidence; defaults to current source')
    args = parser.parse_args()
    try:
        if not args.check:
            if args.build is None: parser.error('--build is required for collection')
            if args.source_root.resolve() != ROOT:
                raise ValueError('--source-root is supported only for retained-evidence checks')
            collect(args.build.resolve(), args.output.resolve())
        result = evaluate(args.output.resolve(), root=args.source_root.resolve())
    except (ValueError, KeyError, OSError, subprocess.SubprocessError) as error:
        result = dict(schema='proto-time-x86-acceptance-result-v1', status='not-run', reason=str(error), admissionChanged=False)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'acceptance.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result['status'] == 'thresholds-met-awaiting-review' else 1


if __name__ == '__main__':
    sys.exit(main())
