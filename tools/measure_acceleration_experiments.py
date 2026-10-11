#!/usr/bin/env python3
"""Collect supplemental acceleration experiments; never grant admission."""
import argparse
import json
import math
from pathlib import Path
import platform
import os
import re
import time
import statistics
import subprocess
import measure_acceleration as binding

ROOT = Path(__file__).resolve().parents[1]
EXISTING = {'baseline', 'cached-block', 'portable-ir'}


def summarize(report):
    synthetic = report['schema'] in ('proto-time-acceleration-synthetic-experiments-v1', 'proto-time-acceleration-synthetic-experiments-v2')
    if not synthetic and report['schema'] not in ('proto-time-acceleration-experiments-v1', 'proto-time-acceleration-experiments-v2'):
        raise ValueError('unknown experiment schema')
    groups = {}
    for row in report['samples']:
        if type(row['nanoseconds']) is not int or row['nanoseconds'] <= 0:
            raise ValueError('invalid timing')
        key = (row.get('variant', 'synthetic'), row['core'], row['workload'])
        repeats = groups.setdefault(key, {})
        pair = repeats.setdefault(row['repeat'], {})
        if row['backend'] in pair:
            raise ValueError('duplicate backend sample')
        pair[row['backend']] = row
    correctness = {}
    for row in report.get('correctness', []):
        key = (row.get('variant', 'synthetic'), row['core'], row['workload'])
        if key in correctness or row.get('status') != 'passed':
            raise ValueError('invalid correctness evidence')
        correctness[key] = row
    if set(correctness) != set(groups):
        raise ValueError('missing correctness evidence')
    comparisons = []
    for (variant, core, workload), repeats in sorted(groups.items()):
        expected = EXISTING | {'emitted'} | ({'direct'} if synthetic else set())
        if set(repeats) != set(range(report['repetitions'])):
            raise ValueError('incomplete repetitions')
        for pair in repeats.values():
            if set(pair) != expected or {r['order'] for r in pair.values()} != set(range(len(expected))):
                raise ValueError('incomplete backend rotation')
            if synthetic and report['schema'].endswith('-v2'):
                if len({json.dumps(r['finalState'], sort_keys=True) for r in pair.values()}) != 1:
                    raise ValueError('different synthetic final state')
            if not synthetic:
                for key in ('lean', 'haltBatch', 'reuse', 'direct', 'expanded',
                            'timelineSha256', 'warmupState', 'finalState', 'cycles', 'instructions'):
                    if len({json.dumps(r[key], sort_keys=True) for r in pair.values()}) != 1:
                        raise ValueError('different comparator configuration or state: ' + key)
        if report['schema'].endswith('-v2') and not synthetic:
            for pair in repeats.values():
                if len({r['copyBindings'] for r in pair.values()}) != 1: raise ValueError('different binding control')
        if report['schema'].endswith('-v2'):
            fields=('finalState','instructions','cycles') if synthetic else ('finalState','instructions','cycles','timelineSha256','warmupState','lean','haltBatch','reuse','direct','expanded','copyBindings')
            for field in fields:
                if len({json.dumps(r[field],sort_keys=True) for pair in repeats.values() for r in pair.values()})!=1:
                    raise ValueError('state/configuration changed between repetitions: '+field)
            if not synthetic:
                checked=correctness[(variant,core,workload)]
                for field in ('finalState','instructions','cycles','timelineSha256','warmupState'):
                    if checked[field]!=next(iter(repeats[0].values()))[field]: raise ValueError('correctness/timing mismatch: '+field)
        medians = {mode: statistics.median(pair[mode]['nanoseconds'] for pair in repeats.values()) for mode in expected}
        comparator = min(EXISTING, key=lambda mode: (medians[mode], mode))
        for candidate in (('emitted', 'direct') if synthetic else ('emitted',)):
            paired = [100 * (1 - pair[candidate]['nanoseconds'] / pair[comparator]['nanoseconds'])
                      for _, pair in sorted(repeats.items())]
            improvement = 100 * (1 - medians[candidate] / medians[comparator])
            row = dict(variant=variant, core=core, workload=workload, candidate=candidate,
                       comparator=comparator, mediansNanoseconds=medians,
                       improvementPercent=improvement, pairedImprovementPercent=paired,
                       repeatableRegression=improvement < -5 or sum(x < -5 for x in paired) > len(paired) // 2)
            if not synthetic:
                sample = repeats[0][candidate]
                row.update(emittedFraction=sample['emittedInstructions'] / sample['instructions'],
                           bindings=sample['bindings'], rebindings=sample['rebindings'],
                           haltBatches=sample['haltBatches'], blockLengths=sample['blockLengths'])
            comparisons.append(row)
    if not comparisons or any(not math.isfinite(r['improvementPercent']) for r in comparisons):
        raise ValueError('missing comparison evidence')
    return comparisons


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=Path('build-working'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--synthetic', action='store_true')
    parser.add_argument('--guard-reference', action='store_true')
    parser.add_argument('--code-view', action='store_true')
    parser.add_argument('--screen', action='store_true')
    parser.add_argument('--smoke', action='store_true')
    parser.add_argument('--variant')
    parser.add_argument('--case', type=int)
    parser.add_argument('--no-windows', action='store_true')
    parser.add_argument('--profile', action='store_true')
    parser.add_argument('--ablations', action='store_true')
    parser.add_argument('--seed', type=int, default=20261009)
    parser.add_argument('--repetitions', type=int, choices=(9,27))
    parser.add_argument('--diagnostic', action='store_true')
    args = parser.parse_args()
    build = args.build.resolve()
    binary = build / ('time-measure-acceleration-synthetic-experiments' if args.synthetic else 'time-measure-acceleration-experiments')
    if args.guard_reference:
        if not args.synthetic or args.diagnostic: raise ValueError('guard reference requires normal synthetic measurement')
        binary = build / 'time-measure-acceleration-guard-reference'
    if args.code_view:
        if not args.synthetic or args.diagnostic or args.guard_reference: raise ValueError('code view requires separate normal synthetic measurement')
        binary = build / 'time-measure-acceleration-code-view'
    if args.diagnostic: binary = build / 'time-diagnose-acceleration-experiments'
    files = [binary, build / 'CMakeCache.txt', build / 'research-whole-blocks.cpp', build / 'research-rom-blocks.cpp',
             build / 'research-expanded-rom-blocks.cpp', build / 'acceleration-roms/rom-corpus.json',
             build / 'acceleration-expanded-roms/rom-corpus.json']
    def hashes():
        return {str(p.relative_to(build)): binding.digest(p) for p in files}
    source = binding.source_digest(ROOT)
    before = hashes()
    command = [str(binary)]
    command.extend(['--seed', str(args.seed)])
    if args.ablations: command.append('--ablations')
    if args.repetitions: command.extend(['--repetitions',str(args.repetitions)])
    if args.smoke:
        command.append('--smoke')
    elif args.screen and not args.synthetic:
        command.append('--screen')
    if args.variant:
        command.extend(['--variant', args.variant])
    if args.case is not None:
        command.extend(['--case', str(args.case)])
    if args.no_windows:
        command.append('--no-windows')
    if args.profile:
        command.append('--profile')
    def environment():
        return dict(affinity=sorted(os.sched_getaffinity(0)),governors={str(p):p.read_text().strip() for p in Path('/sys/devices/system/cpu').glob('cpu*/cpufreq/scaling_governor')})
    env_before=environment();load_before=os.getloadavg();started=time.time_ns()
    raw = subprocess.check_output(command, cwd=ROOT)
    finished=time.time_ns();env_after=environment()
    if env_before!=env_after:raise RuntimeError('governor/affinity changed during measurement')
    report = json.loads(raw)
    if before != hashes() or source != binding.source_digest(ROOT):
        raise RuntimeError('source/build changed during measurement')
    report['comparisons'] = summarize(report)
    compiler_path=re.search(r'^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING)=(.+)$',(build/'CMakeCache.txt').read_text(),re.M)
    if not compiler_path:raise RuntimeError('missing configured compiler')
    report['binding'] = dict(sourceSha256=source, build=before, command=command,
                             host=platform.uname()._asdict(), affinity=sorted(__import__('os').sched_getaffinity(0)), hostLoad=__import__('os').getloadavg(), compiler=subprocess.check_output([compiler_path.group(1), '--version'], text=True), compilerPath=compiler_path.group(1), environmentBefore=env_before, environmentAfter=env_after, startedUnixNs=started, finishedUnixNs=finished, hostLoadBefore=load_before,
                             cpuInfo=Path('/proc/cpuinfo').read_text(), governors={str(p): p.read_text().strip() for p in
                             Path('/sys/devices/system/cpu').glob('cpu*/cpufreq/scaling_governor')})
    report['decision'] = 'supplemental research; no admission; native ARM64 timing deferred; historical acceptance corpus retained; v2 qualification is a separate collector'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
