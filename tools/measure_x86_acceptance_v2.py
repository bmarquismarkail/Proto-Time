#!/usr/bin/env python3
"""Collect/check preregistered v2 research qualification; never enable a backend."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import subprocess
import sys

import acceleration_criteria_v2 as criteria
import measure_acceleration as binding
import measure_x86_acceptance as legacy

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = 'proto-time-x86-acceptance-v2'


def latency_gates(text):
    gates = re.findall(r'gate (headless_\w+) observed_ns=(\d+) limit_ns=(\d+) enforced=(\w+) result=(\w+)', text)
    # CTest -V --output-on-failure replays the same failed-test output.
    # Accept only an exact replay, never conflicting or extra measurements.
    if len(gates) == 8 and gates[:4] == gates[4:]:
        gates = gates[:4]
    if len(gates) != 4 or {g[0] for g in gates} != legacy.REQUIRED_GATES:
        raise ValueError('missing/conflicting exact latency metrics')
    return gates


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_bound(directory, bundle, name):
    if Path(name).name != name or name not in bundle['artifacts']:
        raise ValueError('missing/escaping evidence artifact')
    path = directory / name
    if digest(path) != bundle['artifacts'][name]: raise ValueError('changed artifact: ' + name)
    return path


def check(directory, root=ROOT):
    bundle = json.loads((directory / 'evidence-v2.json').read_text())
    if bundle['schema'] != SCHEMA or bundle['sourceSha256'] != binding.source_digest(root):
        raise ValueError('stale/incompatible v2 evidence')
    if bundle['criteriaBinding'] != criteria.criteria_binding(root): raise ValueError('criteria/authority changed')
    registration_path = read_bound(directory,bundle,'registration.json')
    registration = json.loads(registration_path.read_text())
    if registration['sourceSha256'] != bundle['sourceSha256'] or registration['criteriaBinding'] != bundle['criteriaBinding']:
        raise ValueError('registration source/criteria changed')
    if registration['repetitions'] not in (9,27) or (registration['repetitions']==27 and not registration['followupOf']):
        raise ValueError('unregistered follow-up')
    if registration['repetitions']==27:
        prior_directory=Path(registration['followupOf'])
        if digest(prior_directory/'evidence-v2.json')!=registration['followupBundleSha256']:
            raise ValueError('prior follow-up evidence changed')
        prior_registration=json.loads((prior_directory/'registration.json').read_text())
        if prior_registration['repetitions']!=9:raise ValueError('only one follow-up allowed')
        prior=check(prior_directory,root)
        if not any(c['status']=='inconclusive' for c in prior['cores'].values()):raise ValueError('prior result not inconclusive')
        if any(prior['registration'][k]!=registration[k] for k in ('sourceSha256','candidate','variant','criteriaBinding')):
            raise ValueError('follow-up identity changed')
    build=Path(bundle['build'])
    if digest(build/'CMakeCache.txt') != bundle['cmakeCacheSha256'] or 'BMMQ_ENABLE_TSAN:BOOL=ON' in (build/'CMakeCache.txt').read_text():
        raise ValueError('stale/sanitized build')
    if bundle['host']['system']!='Linux' or bundle['host']['machine']!='x86_64': raise ValueError('native x86-64 required')
    for name, sha in bundle['binaries'].items():
        p=Path(name);legacy.require_x86_binary(p)
        if digest(p)!=sha: raise ValueError('changed executable')
    sessions=[]
    expected_files={'registration.json','full-ctest.log'}|{f'latency-{i}.log' for i in range(9)}
    for session_index in range(2):
        session_id=f'confirmation-{session_index}'
        names=[f'{session_id}-rom.json',f'{session_id}-active.json'];expected_files.update(names)
        rom,active=[json.loads(read_bound(directory,bundle,name).read_text()) for name in names]
        for report in (rom,active):
            b=report['binding']
            if b['sourceSha256']!=bundle['sourceSha256'] or b['host']!=bundle['host'] or report['repetitions']!=registration['repetitions']:
                raise ValueError('changed session provenance')
            for relative,sha in b['build'].items():
                if Path(relative).is_absolute() or '..' in Path(relative).parts or digest(build/relative)!=sha:
                    raise ValueError('changed session build artifact')
            expected_binary='time-measure-acceleration-experiments' if report is rom else 'time-measure-acceleration-synthetic-experiments'
            command=[str(build/expected_binary),'--seed',str(registration['orderSeeds'][session_index])]
            if registration['repetitions']==27:command+=['--repetitions','27']
            if report is rom:command+=['--variant',registration['variant']]
            if b['command']!=command:raise ValueError('changed candidate command/order')
            if report['orderSeed']!=registration['orderSeeds'][session_index]:raise ValueError('changed order seed')
        if any(s['direct']!=(registration['candidate']=='direct') for s in rom['samples']):raise ValueError('ROM/synthetic candidate entry differs')
        build_binding={**rom['binding']['build'],**active['binding']['build']}
        sessions.append(dict(sessionId=session_id, sourceSha256=bundle['sourceSha256'],
            criteriaBinding=bundle['criteriaBinding'],candidate=registration['candidate'],
            buildBinding=build_binding,registrationSha256=digest(registration_path),
            active=criteria.analyze_report(active,registration['candidate']),
            representative=criteria.analyze_report(rom,'emitted',registration['variant'])))
    if set(bundle['artifacts'])!=expected_files: raise ValueError('incomplete/unexpected evidence artifacts')
    discovery=json.loads(subprocess.check_output(['ctest','--test-dir',str(build),'--show-only=json-v1'],text=True))
    all_tests={t['name'] for t in discovery['tests']}
    performance={t['name'] for t in discovery['tests'] if any(p['name']=='LABELS' and 'performance' in p['value'] for p in t['properties'])}
    if not legacy.REQUIRED_TESTS<=performance:raise ValueError('required performance tests missing')
    if set(bundle['testBinaries'])!={t['command'][0] for t in discovery['tests']}:
        raise ValueError('discovered test executables changed')
    for path,sha in bundle['testBinaries'].items():
        if digest(Path(path))!=sha:raise ValueError('test executable changed')
    full_text=read_bound(directory,bundle,'full-ctest.log').read_text()
    full_passed=legacy.check_test_log(full_text,all_tests)
    latency_ok=full_passed and bundle['fullCtestExitCode']==0
    failures=[]
    if not latency_ok:failures.append('full discovered CTest suite failed')
    for i in range(9):
        text=read_bound(directory,bundle,f'latency-{i}.log').read_text()
        passed=legacy.check_test_log(text,performance)
        gates=latency_gates(text)
        if any(int(g[2])!=16000000 or g[3]!='true' for g in gates):raise ValueError('latency limits changed/disabled')
        passed=passed and bundle['latencyExitCodes'][i]==0 and all(int(g[1])<16000000 and g[4]=='pass' for g in gates)
        if not passed:failures.append(f'latency repetition {i}: gate failed')
        latency_ok &= passed
    result=criteria.decisions(sessions,latency_ok,diverse_active_roms=False)
    result.update(criteriaBinding=bundle['criteriaBinding'],sourceSha256=bundle['sourceSha256'],
                  registration=registration,sessions=sessions,latencyFailures=failures,
                  activeRomDiversity='not-run; required before default promotion')
    return result


def collect(build,directory,variant='halt512-reuse',candidate='emitted',followup=None):
    if directory.exists() and any(directory.iterdir()):raise ValueError('use a fresh evidence directory; prior failures are immutable')
    if platform.system()!='Linux' or platform.machine()!='x86_64':raise ValueError('native Linux x86-64 required')
    directory.mkdir(parents=True,exist_ok=True)
    source=binding.source_digest(ROOT);cb=criteria.criteria_binding()
    repetitions=27 if followup else 9
    if followup:
        prior=check(followup)
        if not any(c['status']=='inconclusive' for c in prior['cores'].values()):raise ValueError('follow-up requires an inconclusive prior result')
        if prior['registration']['repetitions']!=9:raise ValueError('only one 27-repeat follow-up allowed')
        if any(digest(build/relative)!=sha for relative,sha in prior['sessions'][0]['buildBinding'].items()):raise ValueError('follow-up effective build changed')
        if prior['sourceSha256']!=source or prior['registration']['variant']!=variant or prior['registration']['candidate']!=candidate:
            raise ValueError('follow-up candidate changed')
    registration=dict(schema='proto-time-acceleration-registration-v2',sourceSha256=source,criteriaBinding=cb,
        variant=variant,candidate=candidate,repetitions=repetitions,orderSeeds=[20261009,20261110],
        activeWorkloads=criteria.load_criteria()['activeBenefitWorkloads'],followupOf=str(followup) if followup else None,
        diverseActiveRomClaims=False,followupBundleSha256=digest(followup/'evidence-v2.json') if followup else None)
    (directory/'registration.json').write_text(json.dumps(registration,indent=2)+'\n')
    discovery=json.loads(subprocess.check_output(['ctest','--test-dir',str(build),'--show-only=json-v1'],text=True))
    test_binaries={t['command'][0]:digest(Path(t['command'][0])) for t in discovery['tests']}
    binaries={str(build/name):digest(build/name) for name in ('time-measure-acceleration-experiments','time-measure-acceleration-synthetic-experiments')}
    for name in binaries:legacy.require_x86_binary(Path(name))
    bundle=dict(schema=SCHEMA,sourceSha256=source,criteriaBinding=cb,build=str(build),
        cmakeCacheSha256=digest(build/'CMakeCache.txt'),host=platform.uname()._asdict(),
        binaries=binaries,testBinaries=test_binaries,artifacts={},latencyExitCodes=[])
    for i in range(2):
        for kind in ('rom','active'):
            output=directory/f'confirmation-{i}-{kind}.json'
            command=[sys.executable,str(ROOT/'tools/measure_acceleration_experiments.py'),'--build',str(build),'--seed',str(registration['orderSeeds'][i]),'--output',str(output)]
            if kind=='active':command+=['--synthetic']
            else:command+=['--variant',variant]
            if repetitions==27:command+=['--repetitions','27']
            print(f'Confirmation {i+1}/2: {kind}',flush=True)
            subprocess.run(command,cwd=ROOT,check=True)
    for i in range(9):
        print(f'Latency {i+1}/9',flush=True)
        with (directory/f'latency-{i}.log').open('w') as log:
            r=subprocess.run(['ctest','--test-dir',str(build),'-V','-L','performance','--output-on-failure'],stdout=log,stderr=subprocess.STDOUT)
        bundle['latencyExitCodes'].append(r.returncode)
    print('Full CTest',flush=True)
    with (directory/'full-ctest.log').open('w') as log:
        r=subprocess.run(['ctest','--test-dir',str(build),'--output-on-failure'],stdout=log,stderr=subprocess.STDOUT)
    bundle['fullCtestExitCode']=r.returncode
    if binding.source_digest(ROOT)!=source or criteria.criteria_binding()!=cb:raise ValueError('source/criteria changed during collection')
    for name in [*binaries,*test_binaries]:
        if digest(Path(name))!=(binaries.get(name) or test_binaries[name]):raise ValueError('binary changed during collection')
    names=['registration.json','full-ctest.log']+[f'latency-{i}.log' for i in range(9)]+[f'confirmation-{i}-{kind}.json' for i in range(2) for kind in ('rom','active')]
    bundle['artifacts']={name:digest(directory/name) for name in names}
    (directory/'evidence-v2.json').write_text(json.dumps(bundle,indent=2)+'\n')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,default=Path('build-working'));p.add_argument('--output',type=Path,required=True);p.add_argument('--check',action='store_true');p.add_argument('--variant',default='halt512-reuse');p.add_argument('--candidate',choices=('emitted','direct'),default='emitted');p.add_argument('--followup-of',type=Path)
    args=p.parse_args()
    try:
        if not args.check:collect(args.build.resolve(),args.output.resolve(),args.variant,args.candidate,args.followup_of)
        result=check(args.output.resolve())
    except (ValueError,KeyError,TypeError,OverflowError,FileNotFoundError,subprocess.CalledProcessError) as e:
        result=dict(schema='proto-time-acceleration-decision-v2',status='not-run',reason=str(e),admissionChanged=False,defaultPolicyChanged=False)
    args.output.mkdir(parents=True,exist_ok=True);(args.output/'decision-v2.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result.get('cores',result),indent=2))
    return 0 if 'cores' in result and all(c['competitiveOptInEligible'] for c in result['cores'].values()) else 1

if __name__=='__main__':sys.exit(main())
