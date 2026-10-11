#!/usr/bin/env python3
import copy
import json
import math
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import acceleration_criteria_v2 as v2


def session(name,active_gain=1,rom_gain=0):
 def rows(workloads,gain):
  return [dict(core=c,workload=w,historicalRatioOfMediansPercent=gain,
               **v2.paired_statistics([math.log(1-gain/100)]*9))
          for c in ('gameboy','gamegear') for w in workloads]
 return dict(sessionId=name,sourceSha256='source',criteriaBinding={'version':2},candidate='emitted',
             buildBinding={'binary':'hash'},registrationSha256='registration',
             active=rows(('compute-heavy','mixed-devices','ram-heavy'),active_gain),
             representative=rows(('collect-forward','collect-reverse'),rom_gain))


def rom_report(gain=0):
 report=dict(schema='proto-time-acceleration-experiments-v2',repetitions=9,retirementWindows=True,
             profileTimings=False,diagnosticBuild=False,samples=[],correctness=[])
 for core in ('gameboy','gamegear'):
  for workload in ('collect-forward','collect-reverse'):
   state=dict(finalState={'fingerprint':'state'},instructions=100,cycles=400,timelineSha256='tick',warmupState={'fingerprint':'warm'})
   report['correctness'].append(dict(variant='fixed',core=core,workload=workload,status='passed',windowCount=35,retirementsPerWindow=128,**state))
   for repeat in range(9):
    for order,backend in enumerate(('baseline','cached-block','portable-ir','emitted')):
     ns=100000 if backend=='baseline' else 110000 if backend!='emitted' else round(100000*(1-gain/100))
     report['samples'].append(dict(variant='fixed',core=core,workload=workload,repeat=repeat,order=order,backend=backend,nanoseconds=ns,
       lean=True,haltBatch=512,reuse=True,direct=False,expanded=False,copyBindings=False,
       emittedInstructions=10,bindings=1,rebindings=2,haltBatches=3,blockLengths=[0,2,4],**state))
 return report


class CriteriaTests(unittest.TestCase):
 def test_small_gain_and_neutral_gameplay_allow_opt_in_only(self):
  r=v2.decisions([session('one'),session('two')],True)
  for c in r['cores'].values():self.assertTrue(c['competitiveOptInEligible']);self.assertFalse(c['defaultEligible'])
  self.assertFalse(r['admissionChanged']);self.assertFalse(r['defaultPolicyChanged'])
 def test_default_requires_positive_gameplay_and_diversity(self):
  ss=[session('one',1,1),session('two',1,1)]
  self.assertFalse(v2.decisions(ss,True)['cores']['gamegear']['defaultEligible'])
  self.assertTrue(v2.decisions(ss,True,True)['cores']['gamegear']['defaultEligible'])
 def test_gamegear_regression_cannot_be_hidden_by_aggregate(self):
  ss=[session('one'),session('two')]
  for s in ss:
   r=next(r for r in s['representative'] if r['core']=='gamegear' and r['workload']=='collect-forward')
   r.update(v2.paired_statistics([math.log(1.14)]*9))
  result=v2.decisions(ss,True)
  self.assertTrue(result['cores']['gameboy']['competitiveOptInEligible']);self.assertFalse(result['cores']['gamegear']['competitiveOptInEligible'])
 def test_uncertain_margin_is_inconclusive(self):
  r=v2.paired_statistics([math.log(1.10)]*4+[math.log(.99)]*5)
  self.assertEqual(r['nonRegression'],'inconclusive')
 def test_exact_five_percent_boundary(self):
  self.assertEqual(v2.paired_statistics([math.log(1.05)]*9)['nonRegression'],'passed')
  self.assertEqual(v2.paired_statistics([math.log(1.0501)]*9)['nonRegression'],'failed')
 def test_no_benefit_is_not_a_pass(self):
  for c in v2.decisions([session('one',0),session('two',0)],True)['cores'].values():self.assertFalse(c['competitiveOptInEligible'])
 def test_failed_latency_blocks_both_decisions(self):
  for c in v2.decisions([session('one',2,2),session('two',2,2)],False,True)['cores'].values():self.assertFalse(c['competitiveOptInEligible']);self.assertFalse(c['defaultEligible'])
 def test_session_identity_and_missing_confirmation(self):
  a,b=session('one'),session('two')
  for key in ('sourceSha256','criteriaBinding','candidate','buildBinding','registrationSha256'):
   bad=copy.deepcopy(b);bad[key]='changed'
   with self.assertRaises(ValueError):v2.decisions([a,bad],True)
  with self.assertRaises(ValueError):v2.decisions([a],True)
  with self.assertRaises(ValueError):v2.decisions([a,a],True)
 def test_deterministic_statistics_and_invalid_pairs(self):
  logs=[math.log(.99)]*9;self.assertEqual(v2.paired_statistics(logs),v2.paired_statistics(logs))
  for values in (logs[:8],logs+[0],[float('nan')]*9,[float('inf')]*9):
   with self.assertRaises(ValueError):v2.paired_statistics(values)
 def test_recompute_raw_evidence_ignore_supplied_gain(self):
  r=rom_report(-14);r['comparisons']=[{'improvementPercent':100}]
  rows=v2.analyze_report(r,'emitted','fixed')
  self.assertTrue(all(x['nonRegression']=='failed' for x in rows))
 def test_missing_samples_wrong_windows_diagnostics(self):
  r=rom_report();bad=copy.deepcopy(r);bad['samples'].pop()
  with self.assertRaises(ValueError):v2.analyze_report(bad,'emitted','fixed')
  for key,value in [('profileTimings',True),('diagnosticBuild',True),('retirementWindows',False),('repetitions',3)]:
   bad=copy.deepcopy(r);bad[key]=value
   with self.assertRaises(ValueError):v2.analyze_report(bad,'emitted','fixed')
  bad=copy.deepcopy(r);bad['correctness'][0]['windowCount']=34
  with self.assertRaises(ValueError):v2.analyze_report(bad,'emitted','fixed')
 def test_state_and_config_drift_rejected(self):
  for key,value in [('finalState',{'fingerprint':'changed'}),('copyBindings',True),('nanoseconds',0)]:
   r=rom_report();r['samples'][-1][key]=value
   with self.assertRaises(ValueError):v2.analyze_report(r,'emitted','fixed')
 def test_v1_not_reinterpreted_as_v2(self):
  r=rom_report();r['schema']='proto-time-acceleration-experiments-v1'
  with self.assertRaises(ValueError):v2.analyze_report(r,'emitted','fixed')

class EvidenceTests(unittest.TestCase):
 def test_verbose_ctest_exact_failure_replay(self):
  import measure_x86_acceptance_v2 as m
  gates='\n'.join(f'gate {name} observed_ns=31000000 limit_ns=16000000 enforced=true result=FAIL' for name in sorted(m.legacy.REQUIRED_GATES))+'\n'
  self.assertEqual(m.latency_gates(gates+gates),m.latency_gates(gates))
  for bad in (gates+gates.replace('31000000','32000000',1),gates+gates+gates,gates.splitlines()[0]):
   with self.assertRaises(ValueError):m.latency_gates(bad)
 def fixture(self, directory):
  import hashlib
  import measure_x86_acceptance_v2 as collector
  from unittest.mock import patch
  build=directory/'build';build.mkdir();root=directory/'root';(root/'tools').mkdir(parents=True)
  c=v2.load_criteria();(root/'tools/acceleration-criteria-v2.json').write_text(json.dumps(c));authority=root/c['authority'];authority.parent.mkdir(parents=True);authority.write_text('authority')
  (build/'CMakeCache.txt').write_text('BMMQ_ENABLE_TSAN:BOOL=OFF')
  sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
  for n in ('time-measure-acceleration-experiments','time-measure-acceleration-synthetic-experiments','test'):(build/n).write_bytes(b'\x7fELF\x02\x01'+b'\x00'*12+b'\x3e\x00')
  host={'system':'Linux','machine':'x86_64'};cb=v2.criteria_binding(root)
  reg=dict(sourceSha256='source',criteriaBinding=cb,repetitions=9,followupOf=None,variant='fixed',candidate='emitted',orderSeeds=[1,2])
  (directory/'registration.json').write_text(json.dumps(reg))
  for i in range(2):
   rom=rom_report(0);active=copy.deepcopy(rom);active['schema']='proto-time-acceleration-synthetic-experiments-v2';active['samples']=[];active['correctness']=[]
   for core in ('gameboy','gamegear'):
    for w in ('compute-heavy','mixed-devices','ram-heavy'):
     active['correctness'].append(dict(core=core,workload=w,status='passed'))
     for repeat in range(9):
      for order,b in enumerate(('baseline','cached-block','portable-ir','emitted','direct')):
       active['samples'].append(dict(core=core,workload=w,repeat=repeat,order=order,backend=b,nanoseconds=100000 if b=='baseline' else 99000 if b in ('emitted','direct') else 110000,finalState={'fingerprint':'state'},instructions=100,cycles=400))
   for kind,report,binary in [('rom',rom,'time-measure-acceleration-experiments'),('active',active,'time-measure-acceleration-synthetic-experiments')]:
    report['orderSeed']=i+1;report['binding']=dict(sourceSha256='source',host=host,build={binary:sha(build/binary),'CMakeCache.txt':sha(build/'CMakeCache.txt')},command=[str(build/binary),'--seed',str(i+1)]+(['--variant','fixed'] if kind=='rom' else []))
    (directory/f'confirmation-{i}-{kind}.json').write_text(json.dumps(report))
  tests=collector.legacy.REQUIRED_TESTS;discovery={'tests':[{'name':n,'command':[str(build/'test')],'properties':[{'name':'LABELS','value':['performance']}]} for n in sorted(tests)]}
  results='\n'.join(f'{i}/5 Test #{i}: {n} .....   Passed    0.01 sec' for i,n in enumerate(sorted(tests),1))
  gates='\n'.join(f'gate {g} observed_ns=10000000 limit_ns=16000000 enforced=true result=pass' for g in sorted(collector.legacy.REQUIRED_GATES))
  (directory/'full-ctest.log').write_text(results+'\n100% tests passed out of 5\n')
  for i in range(9):(directory/f'latency-{i}.log').write_text(results+'\n'+gates+'\n')
  names=['registration.json','full-ctest.log']+[f'latency-{i}.log' for i in range(9)]+[f'confirmation-{i}-{kind}.json' for i in range(2) for kind in ('rom','active')]
  bundle=dict(schema=collector.SCHEMA,sourceSha256='source',criteriaBinding=cb,build=str(build),cmakeCacheSha256=sha(build/'CMakeCache.txt'),host=host,
   binaries={str(build/n):sha(build/n) for n in ('time-measure-acceleration-experiments','time-measure-acceleration-synthetic-experiments')},
   testBinaries={str(build/'test'):sha(build/'test')},artifacts={n:sha(directory/n) for n in names},latencyExitCodes=[0]*9,fullCtestExitCode=0)
  return collector,root,bundle,discovery,sha
 def test_bound_evidence_and_mutations(self):
  import tempfile
  from unittest.mock import patch
  for mutation in ('none','criteria','artifact','skipped','latency-limit','latency-failed','missing-pairs','full-suite-failed','candidate'):
   with self.subTest(mutation=mutation),tempfile.TemporaryDirectory() as tmp:
    directory=Path(tmp);m,root,bundle,discovery,sha=self.fixture(directory)
    if mutation=='criteria':(root/'tools/acceleration-criteria-v2.json').write_text('{}')
    if mutation=='artifact':(directory/'confirmation-0-rom.json').write_text('{}')
    for kind in ('skipped','latency-limit','latency-failed'):
     if mutation==kind:
      p=directory/'latency-0.log';t=p.read_text()
      if kind=='skipped':t=t.replace('Passed','***Skipped',1)
      if kind=='latency-limit':t=t.replace('limit_ns=16000000','limit_ns=32000000')
      if kind=='latency-failed':
       t=t.replace('observed_ns=10000000','observed_ns=31000000').replace('result=pass','result=FAIL')
       t+='\n'.join(line for line in t.splitlines() if line.startswith('gate '))+'\n'
      p.write_text(t);bundle['artifacts'][p.name]=sha(p)
    if mutation in ('missing-pairs','candidate'):
     p=directory/'confirmation-0-rom.json';r=json.loads(p.read_text())
     if mutation=='missing-pairs':r['samples'].pop()
     else:r['binding']['command'][-1]='changed'
     p.write_text(json.dumps(r));bundle['artifacts'][p.name]=sha(p)
    if mutation=='full-suite-failed':bundle['fullCtestExitCode']=8
    (directory/'evidence-v2.json').write_text(json.dumps(bundle))
    with patch.object(m.binding,'source_digest',return_value='source'),patch.object(m.subprocess,'check_output',return_value=json.dumps(discovery)):
     if mutation in ('none','latency-failed','full-suite-failed'):
      result=m.check(directory,root)
      self.assertEqual(result['cores']['gamegear']['competitiveOptInEligible'],mutation=='none');self.assertFalse(result['cores']['gamegear']['defaultEligible'])
     else:
      with self.assertRaises((ValueError,KeyError)):m.check(directory,root)

if __name__=='__main__':unittest.main()
