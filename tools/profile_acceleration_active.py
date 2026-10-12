#!/usr/bin/env python3
"""Source-bound active phase attribution. Nested timings are not additive."""
import argparse
import collections
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import time
import measure_acceleration as binding
ROOT = Path(__file__).resolve().parents[1]


def summarize(report):
    if report.get('schema') != 'proto-time-active-phase-diagnostics-v1' or not report.get('diagnosticBuild') or not report.get('profileTimings') or report.get('admissionChanged') is not False:
        raise ValueError('not an isolated diagnostic report')
    expected = {(c, w) for c in ('gameboy', 'gamegear') for w in ('compute-heavy', 'ram-heavy', 'mixed-devices')}
    correctness = [(r['core'], r['workload']) for r in report['correctness'] if r['status'] == 'passed']
    if len(report['correctness']) != 6 or len(correctness) != 6 or set(correctness) != expected:
        raise ValueError('incomplete correctness evidence')
    groups = collections.defaultdict(list)
    seen = set()
    states = {}
    for row in report['samples']:
        key = (row['core'], row['workload'], row['backend'])
        identity = (*key, row['repeat'])
        if identity in seen:
            raise ValueError('duplicate diagnostic sample')
        seen.add(identity)
        if key[:2] not in expected or key[2] not in ('baseline', 'cached-block', 'portable-ir', 'emitted', 'direct') or not 0 <= row['repeat'] < report['repetitions']:
            raise ValueError('unexpected sample')
        count = row['instructions']
        if count != report['method']['instructions'] or row['emittedInstructions'] + row['nonEmittedInstructions'] != count:
            raise ValueError('bad coverage')
        if key[:2] in states and row['finalState'] != states[key[:2]]:
            raise ValueError('endpoint mismatch')
        states[key[:2]] = row['finalState']
        phases = {p['phase']: p for p in row['steadyDiagnostics']['phases']}
        if len(phases) != 14 or phases['bind']['calls'] or row['steadyDiagnostics']['metadataCopies'] or phases['machine-retirement']['calls'] != count:
            raise ValueError('incomplete phase accounting')
        if phases['cpu-step']['calls'] + phases['cpu-retirement']['calls'] != count or phases['entry-guard']['calls'] + phases['continuation-guard']['calls'] != row['emittedInstructions']:
            raise ValueError('incomplete CPU/guard accounting')
        for p in phases.values():
            if not 0 <= p['samples'] <= p['calls'] or p['nanoseconds'] < 0:
                raise ValueError('invalid phase counters')
        groups[key].append(row)
    if len(groups) != 30 or any(len(rows) != report['repetitions'] for rows in groups.values()):
        raise ValueError('incomplete sample matrix')
    output=[]
    for key, rows in sorted(groups.items()):
        phase_rows=[]
        for phase in rows[0]['steadyDiagnostics']['phases']:
            name=phase['phase']
            data=[next(p for p in r['steadyDiagnostics']['phases'] if p['phase']==name) for r in rows]
            calls=sum(p['calls'] for p in data);samples=sum(p['samples'] for p in data);ns=sum(p['nanoseconds'] for p in data)
            mean=ns/samples if samples else None
            phase_rows.append(dict(phase=name,calls=calls,samples=samples,sampledMeanNs=mean,estimatedNsPerActiveInstruction=mean*calls/sum(r['instructions'] for r in rows) if samples else None))
        output.append(dict(core=key[0],workload=key[1],backend=key[2],medianInstrumentedNsPerInstruction=statistics.median(r['instrumentedNanoseconds']/r['instructions'] for r in rows),emittedCoverage=statistics.mean(r['emittedInstructions']/r['instructions'] for r in rows),phases=phase_rows))
    return output


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,default=Path('build-working'))
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--smoke',action='store_true')
    parser.add_argument('--repetitions',type=int,choices=range(1,28),default=3)
    args=parser.parse_args();build=args.build.resolve()
    binary=build/'time-diagnose-acceleration-active'
    files=[binary,build/'CMakeCache.txt',build/'research-whole-blocks.cpp']
    def hashes():return {str(p.relative_to(build)):binding.digest(p) for p in files}
    def environment():return dict(affinity=sorted(os.sched_getaffinity(0)),governors={str(p):p.read_text().strip() for p in Path('/sys/devices/system/cpu').glob('cpu*/cpufreq/scaling_governor')})
    before=hashes();source=binding.source_digest(ROOT);env=environment();started=time.time_ns()
    command=[str(binary),'--repetitions',str(args.repetitions)]
    if args.smoke:command.append('--smoke')
    raw=subprocess.check_output(command,cwd=ROOT)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.with_suffix('.raw.json').write_bytes(raw)
    report=json.loads(raw);report['summary']=summarize(report)
    if source!=binding.source_digest(ROOT) or before!=hashes() or env!=environment():
        raise RuntimeError('source/build/environment changed during profiling; raw evidence retained')
    compiler=re.search(r'^CMAKE_CXX_COMPILER:(?:FILEPATH|STRING)=(.+)$',(build/'CMakeCache.txt').read_text(),re.M)
    if not compiler:raise RuntimeError('missing compiler configuration')
    report['binding']=dict(sourceSha256=source,build=before,command=command,environment=env,host=platform.uname()._asdict(),startedUnixNs=started,finishedUnixNs=time.time_ns(),hostLoad=os.getloadavg(),compiler=subprocess.check_output([compiler.group(1),'--version'],text=True),cpuInfo=Path('/proc/cpuinfo').read_text())
    report['limits']=['Inclusive nested scopes must not be summed.','Clock overhead is retained; empty-scope calibration is a floor, not a subtraction correction.','Phase counters perturb execution. Instrumented totals are not throughput evidence.','CPU retirement and register phases do not cover baseline/cached CPU internals. Zero calls means outside scope.','Synthetic active patterns do not establish representative-ROM or native ARM64 performance.']
    args.output.write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':main()
