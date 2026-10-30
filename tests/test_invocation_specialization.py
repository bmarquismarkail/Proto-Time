#!/usr/bin/env python3
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import measure_invocation_specialization as experiment
import acceleration_criteria_v2 as criteria


class InvocationEvidence(unittest.TestCase):
    def fixture(self):
        report = dict(schema='proto-time-invocation-specialization-v1', qualification=False,
            admissionChanged=False, diagnosticBuild=False, profileTimings=False,
            registerStrategy='generic-host', guardStrategy='unchanged-cached-metadata',
            repetitions=1, instructions=512, correctness=[], samples=[],
            lowerBounds=[], policyLookups=[])
        for core in experiment.CORES:
            for workload in experiment.WORKLOADS:
                report['correctness'].append(dict(core=core, workload=workload, status='passed',
                    seeds=[0, 127, 128, 255], romSha256='rom', instructionsPerSeedAndBackend=256,
                    branchCoverage=[dict(backend=b, taken=2, notTaken=2) for b in experiment.BACKENDS[1:]]))
                for order, backend in enumerate(experiment.BACKENDS):
                    native = backend in ('emitted', 'typed')
                    report['samples'].append(dict(core=core, workload=workload, backend=backend,
                        repeat=0, order=order, instructions=512, cycles=2048, romSha256='rom',
                        emittedInstructions=400 if native else 0, nonEmittedInstructions=112 if native else 512,
                        emittedInvocations=60 if native else 0, partialEntryInstructions=0,
                        nanoseconds=500 if backend == 'typed' else 1000,
                        finalState=dict(instructions=512, cycles=2048, fingerprint='same')))
                report['policyLookups'].append(dict(core=core, workload=workload, repeat=0,
                    calls=1024, nanoseconds=1000, qualification=False))
        for order, backend in enumerate(('erased', 'typed')):
            report['lowerBounds'].append(dict(backend=backend, repeat=0, order=order,
                calls=2048, nanoseconds=1000, qualification=False))
        return report

    def test_equivalent_screen(self):
        rows = experiment.summarize(self.fixture())
        self.assertEqual(len(rows), 12)
        self.assertTrue(all(r['ratioOfMediansGainPercent'] == 50 for r in rows))
        self.assertTrue(all(r['qualification'] is False for r in rows))

    def test_rejects_incomplete_or_unmatched_evidence(self):
        edits = [
            lambda r: r['samples'].pop(),
            lambda r: r['samples'].append(copy.deepcopy(r['samples'][0])),
            lambda r: r['correctness'].pop(),
            lambda r: r['correctness'][0]['branchCoverage'][0].update(taken=0),
            lambda r: r['samples'][0]['finalState'].update(fingerprint='different'),
            lambda r: r['samples'][4].update(emittedInvocations=61),
            lambda r: r['samples'][4].update(romSha256='different'),
            lambda r: r['samples'][0].update(nanoseconds=0),
            lambda r: r['samples'][0].update(order=4),
            lambda r: r['samples'][0].update(repeat=1),
            lambda r: r.update(qualification=True),
            lambda r: r.update(profileTimings=True),
            lambda r: r.update(registerStrategy='direct'),
            lambda r: r['lowerBounds'].pop(),
            lambda r: r['lowerBounds'][0].update(qualification=True),
            lambda r: r['policyLookups'].pop(),
            lambda r: r['policyLookups'].append(copy.deepcopy(r['policyLookups'][0])),
        ]
        for edit in edits:
            with self.subTest(edit=edit):
                report = self.fixture(); edit(report)
                with self.assertRaises(ValueError): experiment.summarize(report)

    def test_concrete_gamegear_controls_and_rejection(self):
        report = self.fixture()
        report['schema'] = 'proto-time-concrete-gamegear-v1'
        for field in ('correctness', 'samples', 'policyLookups'):
            report[field] = [r for r in report[field] if r['core'] == 'gamegear']
        for row in report['correctness']:
            row['branchCoverage'].append(dict(backend='concrete', taken=2, notTaken=2))
        for row in list(report['samples']):
            if row['backend'] == 'typed':
                concrete = copy.deepcopy(row)
                concrete.update(backend='concrete', order=5, nanoseconds=250)
                report['samples'].append(concrete)
        rows = experiment.summarize(report)
        self.assertEqual(len(rows), 9)
        self.assertEqual({r['comparator'] for r in rows}, {'emitted', 'typed', 'baseline'})
        self.assertTrue(all(r['candidate'] == 'concrete' for r in rows))
        for field in ('emittedInstructions', 'emittedInvocations', 'partialEntryInstructions'):
            invalid = copy.deepcopy(report)
            invalid['samples'][-1][field] += 1
            with self.assertRaises(ValueError): experiment.summarize(invalid)
        report['repetitions'] = 9
        with self.assertRaises(ValueError): criteria.analyze_report(report, 'concrete')

    def test_screen_cannot_enter_v2_admission(self):
        report = self.fixture()
        report['repetitions'] = 9
        with self.assertRaises(ValueError): criteria.analyze_report(report, 'typed')


if __name__ == '__main__': unittest.main()
