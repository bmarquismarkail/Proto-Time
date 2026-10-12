#!/usr/bin/env python3
"""Collect equivalent-work invocation screens; empty controls cannot qualify."""
import argparse
import collections
import json
import math
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import tarfile
import time

import acceleration_criteria_v2 as criteria
import measure_acceleration as binding

ROOT = Path(__file__).resolve().parents[1]
CORES = ('gameboy', 'gamegear')
WORKLOADS = ('compute-heavy', 'ram-heavy', 'mixed-devices')
BACKENDS = ('baseline', 'cached-block', 'portable-ir', 'emitted', 'typed')


def summarize(report):
    concrete = report.get('schema') == 'proto-time-concrete-gamegear-v1'
    cores = ('gamegear',) if concrete else CORES
    backends = BACKENDS + ('concrete',) if concrete else BACKENDS
    candidate = 'concrete' if concrete else 'typed'
    if (report.get('schema') not in ('proto-time-invocation-specialization-v1', 'proto-time-concrete-gamegear-v1')
            or report.get('qualification') is not False
            or report.get('admissionChanged') is not False
            or report.get('diagnosticBuild') is not False
            or report.get('profileTimings') is not False
            or report.get('registerStrategy') != 'generic-host'
            or report.get('guardStrategy') != 'unchanged-cached-metadata'
            or report.get('repetitions') not in (1, 9)):
        raise ValueError('not an isolated equivalent-work screen')
    expected = {(core, workload) for core in cores for workload in WORKLOADS}
    checked = {}
    for row in report['correctness']:
        key = (row['core'], row['workload'])
        if key not in expected or key in checked or row['status'] != 'passed':
            raise ValueError('invalid correctness matrix')
        if row['seeds'] != [0, 127, 128, 255] or row['instructionsPerSeedAndBackend'] != (256 if report['repetitions'] == 1 else 1024):
            raise ValueError('incomplete differential windows')
        coverage = row['branchCoverage']
        if (len(coverage) != len(backends) - 1 or {x['backend'] for x in coverage} != set(backends[1:])
                or any(x['taken'] <= 0 or x['notTaken'] <= 0 for x in coverage)):
            raise ValueError('incomplete branch/backend parity')
        checked[key] = row
    if set(checked) != expected:
        raise ValueError('missing correctness')
    cases = collections.defaultdict(lambda: collections.defaultdict(dict))
    states = {}
    for row in report['samples']:
        key = (row['core'], row['workload'])
        backend, repeat = row['backend'], row['repeat']
        if (key not in expected or backend not in backends
                or type(repeat) is not int or not 0 <= repeat < report['repetitions']
                or backend in cases[key][repeat]):
            raise ValueError('duplicate/unexpected sample')
        if type(row['nanoseconds']) is not int or row['nanoseconds'] <= 0:
            raise ValueError('invalid timing')
        count = row['instructions']
        if (count != report['instructions'] or count != (512 if report['repetitions'] == 1 else 100000)
                or row['emittedInstructions'] + row['nonEmittedInstructions'] != count
                or row['romSha256'] != checked[key]['romSha256']
                or row['finalState']['instructions'] != count
                or row['finalState']['cycles'] != row['cycles']):
            raise ValueError('incorrect coverage/endpoint binding')
        if backend in ('emitted', 'typed', 'concrete') and not (row['emittedInstructions'] > 0 and row['nonEmittedInstructions'] > 0):
            raise ValueError('missing real emitted work')
        if key in states and states[key] != row['finalState']:
            raise ValueError('different warmed or final state')
        states[key] = row['finalState']
        cases[key][repeat][backend] = row
    if set(cases) != expected:
        raise ValueError('missing workload')
    rows = []
    for key, repeats in sorted(cases.items()):
        if set(repeats) != set(range(report['repetitions'])):
            raise ValueError('incomplete repetitions')
        for pair in repeats.values():
            if set(pair) != set(backends) or {x['order'] for x in pair.values()} != set(range(len(backends))):
                raise ValueError('incomplete rotation')
            for field in ('emittedInstructions', 'nonEmittedInstructions', 'emittedInvocations', 'partialEntryInstructions'):
                if any(pair['emitted'][field] != pair[b][field] for b in backends[4:]):
                    raise ValueError('different emitted instruction stream')
        medians = {backend: statistics.median(pair[backend]['nanoseconds'] for pair in repeats.values()) for backend in backends}
        fastest = min(BACKENDS[:3], key=lambda b: (medians[b], b))
        for comparator in (('emitted', 'typed', fastest) if concrete else ('emitted', fastest)):
            logs = [math.log(repeats[i][candidate]['nanoseconds'] / repeats[i][comparator]['nanoseconds']) for i in range(report['repetitions'])]
            stats = criteria.paired_statistics(logs) if report['repetitions'] == 9 else {}
            rows.append(dict(core=key[0], workload=key[1], comparator=comparator,
                candidate=candidate, mediansNanoseconds=medians,
                ratioOfMediansGainPercent=100*(1-medians[candidate]/medians[comparator]),
                qualification=False, **stats))
    for field, identities, required in (
            ('lowerBounds', ('backend', 'repeat'), {(b, i) for b in ('erased', 'typed') for i in range(report['repetitions'])}),
            ('policyLookups', ('core', 'workload', 'repeat'), {(c, w, i) for c, w in expected for i in range(report['repetitions'])})):
        seen = set()
        for row in report[field]:
            key = tuple(row[x] for x in identities)
            if (key not in required or key in seen or row.get('qualification') is not False
                    or row['calls'] <= 0 or type(row['nanoseconds']) is not int or row['nanoseconds'] <= 0):
                raise ValueError('invalid diagnostic control')
            seen.add(key)
        if seen != required:
            raise ValueError('missing diagnostic controls')
    return rows


def environment():
    return dict(affinity=sorted(os.sched_getaffinity(0)),
        governors={str(p): p.read_text().strip() for p in Path('/sys/devices/system/cpu').glob('cpu*/cpufreq/scaling_governor')})


def observations():
    return dict(load=os.getloadavg(),
        frequencies={str(p): p.read_text().strip() for p in Path('/sys/devices/system/cpu').glob('cpu*/cpufreq/scaling_cur_freq')},
        thermal={str(p): p.read_text().strip() for p in Path('/sys/class/thermal').glob('thermal_zone*/temp')})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=Path('build-working'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seed', type=int, default=20261011)
    parser.add_argument('--smoke', action='store_true')
    parser.add_argument('--concrete-gamegear', action='store_true')
    args = parser.parse_args()
    build, output = args.build.resolve(), args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists() or output.with_suffix('.raw.json').exists() or output.with_suffix('.registration.json').exists():
        raise ValueError('use fresh output; retained collections are immutable')
    binary = build/'time-measure-invocation-specialization'
    files = [binary, build/'CMakeCache.txt', build/'research-whole-blocks.cpp']
    def hashes(): return {str(p.relative_to(build)): binding.digest(p) for p in files}
    before, source, env = hashes(), binding.source_digest(ROOT), environment()
    command = [str(binary), '--seed', str(args.seed)]
    if args.smoke: command += ['--smoke']
    if args.concrete_gamegear: command += ['--concrete-gamegear']
    registration = dict(schema='proto-time-invocation-screen-registration-v1',
        qualification=False, sourceSha256=source, build=before, command=command,
        criteriaBinding=criteria.criteria_binding(), environment=env,
        repetitions=1 if args.smoke else 9, lowerBoundEligible=False,
        hypothesis=('Concrete Game Gear host reduces operation dispatch with unchanged generated operations, guards, register strategy and retirement.' if args.concrete_gamegear else 'Typed invocation hooks reduce orchestration cost with unchanged generated operations, guards, host access and retirement.'))
    output.with_suffix('.registration.json').write_text(json.dumps(registration, indent=2)+'\n')
    names = sorted(set(n.decode() for n in subprocess.check_output(['git', 'ls-files', '-co', '--exclude-standard', '-z'], cwd=ROOT).split(b'\0') if n))
    archive = output.with_suffix('.source.tar.gz')
    with tarfile.open(archive, 'w:gz') as tar:
        for name in names:
            if (ROOT/name).is_file(): tar.add(ROOT/name, arcname=name)
    output.with_suffix('.patch').write_bytes(subprocess.check_output(['git', 'diff', '--binary'], cwd=ROOT))
    observed_before = observations()
    started = time.time_ns()
    process = subprocess.run(command, cwd=ROOT, capture_output=True)
    finished = time.time_ns()
    output.with_suffix('.raw.json').write_bytes(process.stdout)
    output.with_suffix('.stderr.log').write_bytes(process.stderr)
    process.check_returncode()
    report = json.loads(process.stdout)
    report['comparisons'] = summarize(report)
    if source != binding.source_digest(ROOT) or before != hashes() or env != environment():
        raise RuntimeError('source/build/environment changed; raw evidence retained')
    cache = (build/'CMakeCache.txt').read_text()
    compiler = re.search(r'^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING)=(.+)$', cache, re.M)
    if not compiler: raise ValueError('missing compiler')
    report['binding'] = dict(sourceSha256=source, build=before, command=command,
        archiveSha256=binding.digest(archive), registrationSha256=binding.digest(output.with_suffix('.registration.json')),
        criteriaBinding=registration['criteriaBinding'], environment=env,
        observationsBefore=observed_before, observationsAfter=observations(),
        startedUnixNs=started, finishedUnixNs=finished, host=platform.uname()._asdict(),
        compiler=subprocess.check_output([compiler.group(1), '--version'], text=True),
        cpuInfo=Path('/proc/cpuinfo').read_text())
    report['limits'] = ['Engineering screen only; not v2 qualification.',
        'Empty hook controls omit architectural effects and cannot qualify.',
        'Policy lookup timings include lookup bodies and are not subtracted from active timings.',
        'Native ARM64 timing remains deferred; gameplay/live latency are separate obligations.']
    output.write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__': main()
