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


def source_digest(root):
    files = subprocess.check_output(['git', 'ls-files', '-co', '--exclude-standard', '-z'], cwd=root).split(b'\0')
    source = hashlib.sha256()
    for name in sorted(set(files)):
        if name and (root / name.decode()).is_file():
            source.update(name + b'\0' + bytes.fromhex(digest(root / name.decode())))
    return source.hexdigest()


def validate_rom_corpus(report, root=None):
    root = root or pathlib.Path(__file__).resolve().parents[1]
    contract_path = root / 'tests/fixtures/acceleration/rom-corpus.json'
    contract = json.loads(contract_path.read_text())
    if (report.get('schema') != 'proto-time-whole-block-roms-v1' or
            report.get('corpus') != contract or report.get('contractSha256') != digest(contract_path)):
        raise ValueError('missing or stale ROM corpus contract')
    expected = {(c['core'], c['workload']): c for c in contract['cases']}
    def unique(rows):
        result = {}
        for row in rows:
            key = (row['core'], row['workload'])
            if key in result:
                raise ValueError('duplicate ROM corpus evidence')
            result[key] = row
        if set(result) != set(expected):
            raise ValueError('incomplete ROM corpus evidence')
        return result
    checks = unique(report['correctness'])
    bindings = unique(report['romBindings'])
    comparisons = []
    for key, required in expected.items():
        check, binding = checks[key], bindings[key]
        scenarios = json.loads((root / 'tests/fixtures/space' / required['fixture'] / 'scenarios.json').read_text())
        scenario_hash = hashlib.sha256(json.dumps(scenarios, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        for evidence in (check, binding):
            if evidence['romSha256'] != required['romSha256'] or evidence['scenarioSha256'] != scenario_hash:
                raise ValueError('ROM/scenario evidence binding mismatch')
        if binding['scenarios'] != scenarios:
            raise ValueError('ROM replay inputs changed')
        system = 'gb' if required['core'] == 'gameboy' else 'gg'
        if (binding['build']['roms'][system]['sha256'] != required['romSha256'] or
                binding['build']['roms'][system]['size'] != 65536):
            raise ValueError('ROM build identity mismatch')
        source_names = {'game.gb.asm', 'game.gg.asm', 'gameplay.gb.inc', 'gameplay.gg.inc',
                        'state.inc', 'assets.py', 'build.py', 'scenarios.json'}
        if set(binding['build']['sources']) != source_names or any(
                digest(root / 'tests/fixtures/space' / required['fixture'] / name) != value
                for name, value in binding['build']['sources'].items()):
            raise ValueError('stale ROM source/asset build evidence')
        modes = {'cached-block', 'portable-ir', 'emitted'}
        if check['status'] != 'passed' or set(check['backends']) != modes:
            raise ValueError('missing ROM differential evidence')
        windows = {(w['scenario'], w['frame']): w for w in check['windows']}
        expected_windows = {(s['name'], f) for s in scenarios for f in (0, s['frames'] - 1)}
        if (len(windows) != len(check['windows']) or set(windows) != expected_windows or
                any(w['retirementsPerBackend'] != 128 or not re.fullmatch('[0-9a-f]{64}', w['rowsSha256'])
                    for w in windows.values())):
            raise ValueError('missing ROM retirement windows')
        timeline = check['timeline']
        if len(timeline) != sum(s['frames'] for s in scenarios):
            raise ValueError('missing committed-tick evidence')
        timeline_hash = hashlib.sha256(json.dumps(timeline, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        cursor = 0
        for scenario in scenarios:
            cursor += scenario['frames']
            gameplay = timeline[cursor - 1]['gameplay']
            if [gameplay[i] for i in (0, 1, 3, 4)] != contract['scenarioEndpoints'][scenario['name']] or gameplay[12] != 1:
                raise ValueError('failed gameplay or nested-bank obligation')
        for index, tick in enumerate(timeline):
            # Preserve the existing core fingerprint contracts: GB uses a
            # 64-bit state digest, GG uses SHA-256. ROM identities are SHA-256.
            fingerprint_size = 16 if key[0] == 'gameboy' else 64
            if (tick['tick'] != index + 3 or len(tick['registers']) != 20 or
                    not re.fullmatch('[0-9a-f]{' + str(fingerprint_size) + '}', tick['fingerprint']) or
                    tick['instructions'] <= (timeline[index - 1]['instructions'] if index else 0) or
                    tick['cycles'] <= (timeline[index - 1]['cycles'] if index else 0)):
                raise ValueError('invalid ROM tick/CPU evidence')
        def coverage(row):
            if (row['instructions'] != timeline[-1]['instructions'] or row['cycles'] != timeline[-1]['cycles'] or
                    row['timelineSha256'] != timeline_hash or row['finalState'] != timeline[-1] or
                    row['emittedInstructions'] < 0 or row['nonEmittedInstructions'] < 0 or
                    row['emittedInstructions'] + row['nonEmittedInstructions'] != row['instructions'] or
                    row['nanoseconds'] <= 0):
                raise ValueError('ROM replay state/accounting mismatch')
            required_regions = set(contract['requiredRegions']) - {'Init'}
            if (not required_regions <= set(row['regions']) or
                    any(row['regions'][r] <= 0 for r in required_regions) or
                    row['warmup']['regions'].get('Init', 0) <= 0 or
                    row['mappingChanges'] <= 0 or row['haltPolls'] <= 0 or
                    not {1, 2} <= {bank for bank, count in row['banks'] if count > 0}):
                raise ValueError('missing ROM hardware/control coverage')
            if key == ('gamegear', 'collect-reverse') and (row['prefixedInstructions'] <= 0 or row['shadowInstructions'] <= 0):
                raise ValueError('missing indexed/shadow ROM coverage')
            if row['backend'] == 'emitted':
                if row['emittedInstructions'] <= 0 or row['nonEmittedInstructions'] <= 0 or row['bindings'] <= 0:
                    raise ValueError('missing ROM emitted coverage')
            elif row['emittedInstructions'] != 0:
                raise ValueError('invalid ROM backend coverage')
        if {r['backend'] for r in check['coverage']} != modes or len(check['coverage']) != 3:
            raise ValueError('missing ROM backend differential coverage')
        for row in check['coverage']:
            coverage(row)
        by_repeat = {}
        for row in report['samples']:
            if (row['core'], row['workload']) != key:
                continue
            if row['romSha256'] != required['romSha256'] or row['scenarioSha256'] != scenario_hash:
                raise ValueError('ROM sample binding mismatch')
            coverage(row)
            repeat = by_repeat.setdefault(row['repeat'], {})
            if row['backend'] in repeat:
                raise ValueError('duplicate ROM backend sample')
            repeat[row['backend']] = row
        if set(by_repeat) != set(range(report['repetitions'])):
            raise ValueError('missing ROM repetitions')
        for row in by_repeat.values():
            if set(row) != modes | {'baseline'} or {r['order'] for r in row.values()} != set(range(4)):
                raise ValueError('incomplete ROM backend repetition')
            if any(r['warmupState'] != row['baseline']['warmupState'] or
                   r['warmup']['instructions'] != row['baseline']['warmup']['instructions'] or
                   r['warmup']['cycles'] != row['baseline']['warmup']['cycles'] for r in row.values()):
                raise ValueError('ROM startup state mismatch')
        medians = {mode: statistics.median(row[mode]['nanoseconds'] for row in by_repeat.values())
                   for mode in modes | {'baseline'}}
        comparator = min(('baseline', 'cached-block', 'portable-ir'), key=medians.get)
        comparisons.append(dict(core=key[0], workload=key[1], comparator=comparator,
            comparatorMedianNanoseconds=medians[comparator], emittedMedianNanoseconds=medians['emitted'],
            improvementPercent=100 * (1 - medians['emitted'] / medians[comparator]),
            pairedSamples=[dict(repeat=n,
                improvementPercent=100 * (1 - row['emitted']['nanoseconds'] / row[comparator]['nanoseconds']),
                emittedFraction=row['emitted']['emittedInstructions'] / row['emitted']['instructions'])
                for n, row in sorted(by_repeat.items())]))
    if {(s['core'], s['workload']) for s in report['samples']} != set(expected):
        raise ValueError('unknown/missing ROM sample cases')
    report['comparisons'] = comparisons
    report['comparisonScope'] = ('same bank-aware scheduler; full 273-tick authored gameplay replay; '
        'includes runtime binding/invalidation, input commits and tick fingerprints; startup excluded; '
        'fastest validated existing backend per case; no latency/native ARM64 admission decision')


def summarize(samples, experiment, root=None):
    if experiment == 'whole-block-roms':
        validate_rom_corpus(samples, root=root)
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
    parser.add_argument('--experiment', choices=('core-baselines', 'whole-block-model', 'whole-block-corpus', 'whole-block-roms'), default='core-baselines')
    parser.add_argument('--smoke', action='store_true', help='short correctness/measurement plumbing run')
    parser.add_argument('--launcher', nargs=argparse.REMAINDER, default=[])
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    binaries = {'core-baselines': 'time-measure-acceleration-corpus',
                'whole-block-model': 'time-smoke-research-whole-block',
                'whole-block-corpus': 'time-measure-whole-block-corpus',
                'whole-block-roms': 'time-measure-whole-block-roms'}
    if args.smoke and args.experiment == 'whole-block-model':
        parser.error('--smoke is not supported for whole-block-model measurements')
    binary = args.build.resolve() / binaries[args.experiment]
    cache = args.build.resolve() / 'CMakeCache.txt'
    # Snapshot bindings before execution and reject concurrent edits afterwards.
    source_hash = source_digest(root)
    def build_bindings():
        result = {'binarySha256': digest(binary), 'cmakeCacheSha256': digest(cache)}
        if args.experiment != 'core-baselines':
            generated = 'research-rom-blocks.cpp' if args.experiment == 'whole-block-roms' else 'research-whole-blocks.cpp'
            result['generatedSourceSha256'] = digest(args.build.resolve() / generated)
        if args.experiment == 'whole-block-roms':
            result['romManifestSha256'] = digest(args.build.resolve() / 'acceleration-roms/rom-corpus.json')
        return result
    bindings = build_bindings()
    command = args.launcher + [str(binary)]
    if args.experiment == 'whole-block-model':
        command.append('--measure')
    elif args.smoke:
        command.append('--smoke')
    samples = json.loads(subprocess.check_output(command, cwd=root))
    if args.experiment == 'whole-block-roms':
        manifest = json.loads((args.build.resolve() / 'acceleration-roms/rom-corpus.json').read_text())
        manifest_hash = hashlib.sha256(json.dumps(manifest, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        if samples['manifestSha256'] != manifest_hash:
            raise RuntimeError('compiled ROM corpus differs from build manifest')
    if bindings != build_bindings():
        raise RuntimeError('build changed during measurement')
    if source_hash != source_digest(root):
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
    samples['decision'] = ('design go approved by user; performance acceptance pending: latency gates, '
                           'representative corpus performance, native ARM64 performance (user-deferred), and review required')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(samples, indent=2) + '\n')


if __name__ == '__main__':
    main()
