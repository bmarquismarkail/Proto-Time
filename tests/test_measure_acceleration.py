#!/usr/bin/env python3
"""Measurement reports must not summarize incomplete or divergent evidence."""
import copy
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    'measure_acceleration', Path(__file__).resolve().parents[1] / 'tools/measure_acceleration.py')
measure = importlib.util.module_from_spec(spec)
spec.loader.exec_module(measure)


class ComparisonTests(unittest.TestCase):
    def corpus(self):
        report = {'measuredInstructions': 100, 'repetitions': 2,
                  'correctness': [], 'samples': []}
        for core in ('gameboy', 'gamegear'):
            for workload in ('compute-heavy', 'ram-heavy', 'mixed-devices'):
                report['correctness'].append(dict(core=core, workload=workload, status='passed',
                    backends=['cached-block', 'portable-ir', 'emitted']))
                for repeat in range(2):
                    for backend, nanos in (('baseline', 100), ('cached-block', 80),
                                           ('portable-ir', 90), ('emitted', 60)):
                        report['samples'].append(dict(core=core, workload=workload,
                            backend=backend, repeat=repeat, nanoseconds=nanos + repeat * 8,
                            romSha256='rom', fingerprint='state', registers=[1, 2], cycles=400,
                            warmupFingerprint='warm', warmupCycles=200, instructions=100,
                            emittedInstructions=75 if backend == 'emitted' else 0,
                            nonEmittedInstructions=25 if backend == 'emitted' else 100))
        return report

    def test_fastest_validated_comparator_and_raw_pairs(self):
        report = self.corpus()
        summaries = measure.summarize(report, 'whole-block-corpus')
        self.assertEqual(len(summaries), 24)
        self.assertEqual(len(report['comparisons']), 6)
        comparison = report['comparisons'][0]
        self.assertEqual(comparison['comparator'], 'cached-block')
        self.assertAlmostEqual(comparison['improvementPercent'], 100 * (1 - 64 / 84))
        self.assertEqual([p['emittedFraction'] for p in comparison['pairedSamples']], [0.75, 0.75])

    def test_rejects_divergent_or_incomplete_evidence(self):
        original = self.corpus()
        for key, value in (('fingerprint', 'different'), ('cycles', 401),
                           ('warmupFingerprint', 'different'), ('romSha256', 'different'),
                           ('instructions', 99), ('emittedInstructions', 1), ('nanoseconds', 0)):
            with self.subTest(key=key):
                report = copy.deepcopy(original)
                report['samples'][3][key] = value
                with self.assertRaises(ValueError):
                    measure.summarize(report, 'whole-block-corpus')
        for mutation in ('missing', 'duplicate', 'failed', 'missing-case'):
            with self.subTest(mutation=mutation):
                report = copy.deepcopy(original)
                if mutation == 'missing':
                    report['samples'].pop(3)
                elif mutation == 'duplicate':
                    report['samples'].append(report['samples'][3])
                elif mutation == 'failed':
                    report['correctness'][0]['status'] = 'failed'
                else:
                    report['samples'] = [s for s in report['samples']
                                         if (s['core'], s['workload']) != ('gameboy', 'compute-heavy')]
                with self.assertRaises(ValueError):
                    measure.summarize(report, 'whole-block-corpus')

    def test_existing_experiment_formats(self):
        core = {'samples': [dict(core='gb', workload='compute', backend='baseline', nanoseconds=2)]}
        self.assertEqual(measure.summarize(core, 'core-baselines')[0]['medianNanoseconds'], 2)
        model = {'samples': [dict(fixture=0, portable={'nanoseconds': 10}, emitted={'nanoseconds': 3})]}
        self.assertEqual(len(measure.summarize(model, 'whole-block-model')), 2)


if __name__ == '__main__':
    unittest.main()
