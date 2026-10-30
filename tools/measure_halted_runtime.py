#!/usr/bin/env python3
"""Collect isolated shared halted-runtime screens without backend admission."""
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
BACKENDS = ('baseline', 'cached-block', 'portable-ir', 'emitted')


def summarize(report):
    if (report.get('schema') != 'proto-time-halted-runtime-v1' or report.get('qualification') is not False
            or report.get('admissionChanged') is not False or report.get('repetitions') not in (1,9)
            or report.get('haltSlicePollLimit') != 128):
        raise ValueError('not an isolated shared-runtime screen')
    expected={(c,w) for c in CORES for w in ('collect-forward','collect-reverse')}
    variants={(b,a) for b in BACKENDS for a in (False,True)}
    checked={}
    for row in report['correctness']:
        key=(row['core'],row['workload'],row['backend'],row['advance'])
        if key[:2] not in expected or type(row['advance']) is not bool or key[2:] not in variants or key in checked or row['status']!='passed':
            raise ValueError('invalid correctness matrix')
        if row['retirementsPerWindow']!=128:raise ValueError('incorrect differential window')
        checked[key]=row
    if set(checked)!={(*c,*v) for c in expected for v in variants}:raise ValueError('missing correctness')
    for case in expected:
        controls=[checked[(*case,*v)] for v in variants]
        if len({(r['windowsSha256'],r['timelineSha256'],r['romSha256']) for r in controls})!=1:raise ValueError('differential traces differ')
    cases=collections.defaultdict(lambda:collections.defaultdict(dict));states={}
    for row in report['samples']:
        key=(row['core'],row['workload']);variant=(row['backend'],row['advance']);repeat=row['repeat']
        if key not in expected or type(row['advance']) is not bool or variant not in variants or type(repeat) is not int or not 0<=repeat<report['repetitions'] or variant in cases[key][repeat]:
            raise ValueError('duplicate/unexpected sample')
        if type(row['nanoseconds']) is not int or row['nanoseconds']<=0:raise ValueError('invalid timing')
        if row['romSha256']!=checked[(*key,*variant)]['romSha256'] or row['timelineSha256']!=checked[(*key,*variant)]['timelineSha256']:
            raise ValueError('incorrect endpoint binding')
        if row['emittedInstructions']+row['nonEmittedInstructions']!=row['instructions']:raise ValueError('incorrect logical accounting')
        if variant[0]=='emitted' and row['emittedInstructions']<=0:raise ValueError('no emitted work')
        endpoint=(row['finalState'],row['timelineSha256'],row['warmupState'])
        if key in states and states[key]!=endpoint:raise ValueError('different warmed/final state')
        states[key]=endpoint
        stats=row['haltedAdvance']
        if variant[1]:
            if not stats['bulkCalls'] or stats['logicalIdlePolls']<2*stats['bulkCalls']:raise ValueError('candidate never reduced polling')
        elif stats['bulkCalls'] or stats['logicalIdlePolls']:raise ValueError('control used candidate')
        cases[key][repeat][variant]=row
    if set(cases)!=expected:raise ValueError('missing workload')
    comparisons=[]
    for key,repeats in sorted(cases.items()):
        if set(repeats)!=set(range(report['repetitions'])):raise ValueError('incomplete repetitions')
        for pair in repeats.values():
            if set(pair)!=variants or {x['order'] for x in pair.values()}!=set(range(8)):raise ValueError('incomplete rotation')
        medians={v:statistics.median(pair[v]['nanoseconds'] for pair in repeats.values()) for v in variants}
        def comparison(candidate,comparator,kind):
            logs=[math.log(repeats[i][candidate]['nanoseconds']/repeats[i][comparator]['nanoseconds']) for i in range(report['repetitions'])]
            stats=criteria.paired_statistics(logs) if report['repetitions']==9 else {}
            if 'activeBenefit' in stats:stats['pairedBenefit']=stats.pop('activeBenefit')
            return dict(core=key[0],workload=key[1],kind=kind,candidate=dict(backend=candidate[0],advance=candidate[1]),
                comparator=dict(backend=comparator[0],advance=comparator[1]),
                ratioOfMediansGainPercent=100*(1-medians[candidate]/medians[comparator]),qualification=False,**stats)
        comparisons.extend(comparison((b,True),(b,False),'shared-runtime') for b in BACKENDS)
        fastest=min(BACKENDS[:3],key=lambda b:(medians[(b,True)],b))
        comparisons.append(comparison(('emitted',True),(fastest,True),'matched-emitted'))
    return comparisons


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
    parser.add_argument('--smoke', action='store_true')
    args = parser.parse_args()
    build, output = args.build.resolve(), args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists() or output.with_suffix('.raw.json').exists() or output.with_suffix('.registration.json').exists():
        raise ValueError('use fresh output; retained collections are immutable')
    binary = build/'time-measure-halted-runtime'
    files = [binary, build/'CMakeCache.txt', build/'research-whole-blocks.cpp', build/'research-rom-blocks.cpp', build/'acceleration-roms/rom-corpus.json']
    def hashes(): return {str(p.relative_to(build)): binding.digest(p) for p in files}
    before, source, env = hashes(), binding.source_digest(ROOT), environment()
    command = [str(binary)]
    if args.smoke: command += ['--smoke']
    registration = dict(schema='proto-time-halted-screen-registration-v1',
        qualification=False, sourceSha256=source, build=before, command=command,
        criteriaBinding=criteria.criteria_binding(), environment=env,
        repetitions=1 if args.smoke else 9, lowerBoundEligible=False,
        hypothesis='Fresh device deadlines reduce halted polling overhead identically for all comparators; emitted competitiveness is separate.')
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
        'Authored gameplay corpus; independent ROM diversity and live acceptance remain separate.',
        'HALT entry slices can include wake instructions; only haltedAdvance.logicalIdlePolls counts skipped idle work.',
        'Observed differential windows force fallback; endpoint/tick checks cover optimized intervals.',
        'Native ARM64 timing remains deferred.']
    output.write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__': main()
