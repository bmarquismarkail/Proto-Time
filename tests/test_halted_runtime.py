#!/usr/bin/env python3
"""Fail-closed evidence checks for the shared-runtime collector."""
import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import measure_halted_runtime as collector

def fixture():
    result=dict(schema='proto-time-halted-runtime-v1',qualification=False,admissionChanged=False,
                repetitions=1,haltSlicePollLimit=128,correctness=[],samples=[])
    for core in collector.CORES:
        for workload in ('collect-forward','collect-reverse'):
            for order,(backend,advance) in enumerate((b,a) for b in collector.BACKENDS for a in (False,True)):
                common=dict(core=core,workload=workload,backend=backend,advance=advance,
                            romSha256='rom',timelineSha256='timeline',windowsSha256='windows',retirementsPerWindow=128)
                result['correctness'].append(dict(common,status='passed'))
                result['samples'].append(dict(common,repeat=0,order=order,nanoseconds=100 if advance else 110,
                    instructions=100,emittedInstructions=10 if backend=='emitted' else 0,
                    nonEmittedInstructions=90 if backend=='emitted' else 100,
                    finalState={'state':'same'},warmupState={'state':'same'},
                    haltedAdvance=dict(bulkCalls=2 if advance else 0,logicalIdlePolls=10 if advance else 0,canonicalSteps=1)))
    return result

class Evidence(unittest.TestCase):
    def test_complete(self):
        rows=collector.summarize(fixture())
        self.assertEqual(len(rows),20)
        self.assertTrue(all(row['qualification'] is False for row in rows))
        self.assertEqual(sum(row['kind']=='matched-emitted' for row in rows),4)
    def test_missing_and_duplicate(self):
        for field in ('samples','correctness'):
            data=fixture();data[field].pop()
            with self.assertRaises(ValueError):collector.summarize(data)
            data=fixture();data[field].append(copy.deepcopy(data[field][0]))
            with self.assertRaises(ValueError):collector.summarize(data)
    def test_binding_and_state(self):
        for field in ('romSha256','timelineSha256','finalState','warmupState'):
            data=fixture();data['samples'][0][field]='changed'
            with self.assertRaises(ValueError):collector.summarize(data)
    def test_false_candidate(self):
        data=fixture();data['samples'][1]['haltedAdvance']['bulkCalls']=0
        with self.assertRaises(ValueError):collector.summarize(data)
        data=fixture();data['samples'][0]['haltedAdvance']['bulkCalls']=1
        with self.assertRaises(ValueError):collector.summarize(data)
    def test_admission_and_timing(self):
        for field in ('qualification','admissionChanged'):
            data=fixture();data[field]=True
            with self.assertRaises(ValueError):collector.summarize(data)
        data=fixture();data['samples'][0]['nanoseconds']=0
        with self.assertRaises(ValueError):collector.summarize(data)

if __name__=='__main__':unittest.main()
