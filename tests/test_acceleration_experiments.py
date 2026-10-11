import importlib.util
import pathlib
import unittest
p=pathlib.Path(__file__).resolve().parents[1]/'tools/measure_acceleration_experiments.py'
import sys
sys.path.insert(0,str(p.parent))
spec=importlib.util.spec_from_file_location('experiments',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Comparisons(unittest.TestCase):
 def fixture(self):
  return {'schema':'proto-time-acceleration-experiments-v1','repetitions':1,'correctness':[dict(variant='all',core='gameboy',workload='forward',status='passed')],'samples':[
   dict(variant='all',core='gameboy',workload='forward',repeat=0,order=i,backend=backend,nanoseconds=ns,
        lean=True,haltBatch=512,reuse=True,direct=True,expanded=True,timelineSha256='a',warmupState={},finalState={},cycles=100,instructions=100,
        emittedInstructions=50,bindings=1,rebindings=2,haltBatches=3,blockLengths=[0,4])
   for i,(backend,ns) in enumerate([('baseline',100),('cached-block',80),('portable-ir',90),('emitted',70)])]}
 def test_fastest_comparator(self):
  r=m.summarize(self.fixture())[0];self.assertEqual(r['comparator'],'cached-block');self.assertAlmostEqual(r['improvementPercent'],12.5)
 def test_rejects_unequal_comparator_options(self):
  r=self.fixture();r['samples'][0]['haltBatch']=0
  with self.assertRaises(ValueError):m.summarize(r)
 def test_rejects_wrong_state(self):
  r=self.fixture();r['samples'][-1]['timelineSha256']='b'
  with self.assertRaises(ValueError):m.summarize(r)
 def test_rejects_missing_samples(self):
  r=self.fixture();r['samples'].pop()
  with self.assertRaises(ValueError):m.summarize(r)
if __name__=='__main__':unittest.main()
