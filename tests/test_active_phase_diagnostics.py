#!/usr/bin/env python3
import copy
import importlib.util
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
spec=importlib.util.spec_from_file_location('active',ROOT/'tools/profile_acceleration_active.py')
active=importlib.util.module_from_spec(spec);spec.loader.exec_module(active)

class ActiveDiagnostics(unittest.TestCase):
    def fixture(self):
        names=['bind','remap','entry-guard','continuation-guard','operations','helper','helper-0','cpu-retirement','machine-retirement','register-read','register-write','dispatch','slice','cpu-step']
        report=dict(schema='proto-time-active-phase-diagnostics-v1',diagnosticBuild=True,profileTimings=True,admissionChanged=False,repetitions=1,method=dict(instructions=512),correctness=[],samples=[])
        for core in ('gameboy','gamegear'):
            for workload in ('compute-heavy','ram-heavy','mixed-devices'):
                report['correctness'].append(dict(core=core,workload=workload,status='passed'))
                for backend in ('baseline','cached-block','portable-ir','emitted','direct'):
                    phases=[dict(phase=n,calls=512 if n in ('machine-retirement','cpu-step') else 0,samples=8 if n in ('machine-retirement','cpu-step') else 0,nanoseconds=800 if n in ('machine-retirement','cpu-step') else 0) for n in names]
                    report['samples'].append(dict(core=core,workload=workload,backend=backend,repeat=0,instructions=512,emittedInstructions=0,nonEmittedInstructions=512,finalState=dict(instructions=512),instrumentedNanoseconds=51200,steadyDiagnostics=dict(phases=phases,metadataCopies=0)))
        return report
    def test_complete_matrix(self):
        rows=active.summarize(self.fixture());self.assertEqual(len(rows),30)
        phase=next(p for p in rows[0]['phases'] if p['phase']=='machine-retirement')
        self.assertEqual(phase['sampledMeanNs'],100);self.assertEqual(phase['estimatedNsPerActiveInstruction'],100)
    def test_rejects_invalid_evidence(self):
        changes=[lambda r:r['samples'].pop(),lambda r:r['samples'].append(copy.deepcopy(r['samples'][0])),lambda r:r.update(diagnosticBuild=False),lambda r:r.update(admissionChanged=True),lambda r:r['samples'][0]['finalState'].update(instructions=513),lambda r:r['samples'][0]['steadyDiagnostics'].update(metadataCopies=1),lambda r:r['correctness'].pop(),lambda r:r['samples'][0]['steadyDiagnostics']['phases'][0].update(calls=1),lambda r:r['samples'][0]['steadyDiagnostics']['phases'][8].update(calls=511)]
        for change in changes:
            with self.subTest(change=change):
                report=self.fixture();change(report)
                with self.assertRaises(ValueError):active.summarize(report)
if __name__=='__main__':unittest.main()
