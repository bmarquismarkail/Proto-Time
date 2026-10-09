#!/usr/bin/env python3
"""Measurement reports must not summarize incomplete or divergent evidence."""
import copy
import importlib.util
import hashlib
import json
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


class RomComparisonTests(unittest.TestCase):
    def report(self):
        root = Path(__file__).resolve().parents[1]
        path = root / 'tests/fixtures/acceleration/rom-corpus.json'
        contract = json.loads(path.read_text())
        report = dict(schema='proto-time-whole-block-roms-v1', corpus=contract,
                      contractSha256=measure.digest(path), repetitions=2,
                      correctness=[], samples=[], romBindings=[])
        for case in contract['cases']:
            scenarios = json.loads((root / 'tests/fixtures/space' / case['fixture'] / 'scenarios.json').read_text())
            scenario_hash = hashlib.sha256(json.dumps(scenarios, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
            identity = dict(core=case['core'], workload=case['workload'],
                            romSha256=case['romSha256'], scenarioSha256=scenario_hash)
            timeline, windows = [], []
            for command in scenarios:
                for _ in range(command['frames']):
                    gameplay = [0] * 18
                    for index, value in zip((0, 1, 3, 4), contract['scenarioEndpoints'][command['name']]):
                        gameplay[index] = value
                    gameplay[12] = 1
                    n = len(timeline) + 1
                    timeline.append(dict(tick=n + 2, instructions=n * 100, cycles=n * 400,
                                         registers=[0] * 20, fingerprint='a' * (16 if case['core'] == 'gameboy' else 64), gameplay=gameplay))
                for frame in sorted({0, command['frames'] - 1}):
                    windows.append(dict(scenario=command['name'], frame=frame,
                                        retirementsPerBackend=128, rowsSha256='b' * 64))
            timeline_hash = hashlib.sha256(json.dumps(timeline, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
            def row(backend, nanos):
                native = backend == 'emitted'
                instructions = timeline[-1]['instructions']
                return dict(**identity, backend=backend, nanoseconds=nanos,
                    instructions=instructions, cycles=timeline[-1]['cycles'],
                    emittedInstructions=instructions // 4 if native else 0,
                    nonEmittedInstructions=instructions - instructions // 4 if native else instructions,
                    timelineSha256=timeline_hash, finalState=timeline[-1], warmupState={'fingerprint': 'warm'},
                    warmup=dict(instructions=10, cycles=40, regions={'Init': 1}),
                    bindings=2 if native else 0, mappingChanges=2, haltPolls=1,
                    prefixedInstructions=1, shadowInstructions=1,
                    regions={region: 1 for region in contract['requiredRegions'] if region != 'Init'},
                    banks=[[0, 10], [1, 10], [2, 10]])
            report['correctness'].append(dict(**identity, status='passed',
                backends=['cached-block', 'portable-ir', 'emitted'], timeline=timeline,
                windows=windows, coverage=[row(b, 100) for b in ('cached-block', 'portable-ir', 'emitted')]))
            source_names = ('game.gb.asm', 'game.gg.asm', 'gameplay.gb.inc', 'gameplay.gg.inc',
                            'state.inc', 'assets.py', 'build.py', 'scenarios.json')
            system = 'gb' if case['core'] == 'gameboy' else 'gg'
            build = dict(sources={name: measure.digest(root / 'tests/fixtures/space' / case['fixture'] / name)
                                 for name in source_names},
                         roms={system: dict(sha256=case['romSha256'], size=65536)})
            report['romBindings'].append(dict(**identity, scenarios=scenarios, build=build))
            for repeat in range(2):
                for order, (backend, nanos) in enumerate((('baseline', 100), ('cached-block', 80), ('portable-ir', 90), ('emitted', 60))):
                    report['samples'].append(dict(**row(backend, nanos + repeat * 8), repeat=repeat, order=order))
        return report

    def test_rom_gameplay_comparisons(self):
        report = self.report()
        self.assertEqual(len(measure.summarize(report, 'whole-block-roms')), 16)
        self.assertEqual(len(report['comparisons']), 4)
        for comparison in report['comparisons']:
            self.assertEqual(comparison['comparator'], 'cached-block')
            self.assertEqual([p['emittedFraction'] for p in comparison['pairedSamples']], [0.25, 0.25])

    def test_rejects_missing_stale_and_divergent_rom_evidence(self):
        original = self.report()
        def mutate(report, kind):
            check = report['correctness'][0]
            if kind == 'missing-case': report['correctness'].pop()
            elif kind == 'duplicate-case': report['correctness'].append(check)
            elif kind == 'stale-contract': report['contractSha256'] = 'stale'
            elif kind == 'rom': report['romBindings'][0]['romSha256'] = 'wrong'
            elif kind == 'inputs': report['romBindings'][0]['scenarios'][0]['mask'] = 1
            elif kind == 'window': check['windows'].pop()
            elif kind == 'failed': check['status'] = 'failed'
            elif kind == 'missing-tick': check['timeline'].pop()
            elif kind == 'gameplay': check['timeline'][7]['gameplay'][0] = 99
            elif kind == 'state': report['samples'][3]['timelineSha256'] = 'wrong'
            elif kind == 'hardware': report['samples'][3]['regions']['ReadInput'] = 0
            elif kind == 'coverage': report['samples'][3]['emittedInstructions'] = 0
            elif kind == 'backend': report['samples'].pop(3)
            elif kind == 'duplicate': report['samples'].append(report['samples'][3])
            elif kind == 'warmup': report['samples'][3]['warmupState'] = {}
            elif kind == 'prefix': report['samples'][-1]['prefixedInstructions'] = 0
            elif kind == 'stale-source': report['romBindings'][0]['build']['sources']['assets.py'] = 'wrong'
        for kind in ('missing-case', 'duplicate-case', 'stale-contract', 'rom', 'inputs', 'window',
                     'failed', 'missing-tick', 'gameplay', 'state', 'hardware', 'coverage', 'backend',
                     'duplicate', 'warmup', 'prefix', 'stale-source'):
            with self.subTest(kind=kind):
                report = copy.deepcopy(original)
                mutate(report, kind)
                with self.assertRaises(ValueError):
                    measure.summarize(report, 'whole-block-roms')


class RomInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(__file__).resolve().parents[1] / 'tools/build_acceleration_roms.py'
        spec = importlib.util.spec_from_file_location('build_acceleration_roms', path)
        cls.tool = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.tool)

    def test_prefix_and_device_work_remain_explicit(self):
        for opcode in (0xdd, 0xfd, 0xed, 0xcb, 0x76, 0x20, 0xc3, 0xcd, 0xc9, 0xea, 0xfa, 0xd3, 0xdb, 0xfb):
            self.assertFalse(self.tool.supported(bytes([opcode])))
        for opcode in (0, 0x06, 0x3e, 0x77, 0x34, 0x87, 0xaf, 0x18):
            self.assertTrue(self.tool.supported(bytes([opcode])))

    def test_label_entry_splits_native_sequences(self):
        class Decoder:
            @staticmethod
            def instructions(data, start, end):
                return [dict(offset=str(i), bytes=data[i:i + 1].hex()) for i in range(start, end)]
        # Other routines are empty. A branch target inside the supported run
        # gets its own emitted entry; unsupported HALT is a separate segment.
        names = ['Entry', 'Irq', 'Init', 'CallBank1', 'CallBank2', 'ResetGame',
                 'ReadInput', 'Render', 'SoundTick', 'Tick', 'Attempt']
        labels = {label: 4 for name in names for label in (name, name + 'End')}
        labels.update(Entry=0, EntryEnd=4, Target=1)
        segments = self.tool.inventory(bytes([0xaf, 0x77, 0x04, 0x76]), labels, True, Decoder, Decoder)
        self.assertEqual([len(s['instructions']) for s in segments], [1, 2, 1])
        self.assertEqual([s['emitted'] for s in segments], [True, True, False])

    def test_z80_absolute_word_load_inventory(self):
        root = Path(__file__).resolve().parents[1] / 'tests/fixtures/space'
        import sys
        sys.path.insert(0, str(root / 'port-game'))
        try:
            spec = importlib.util.spec_from_file_location('reverse_ledger', root / 'port-reverse-game/ledger.py')
            ledger = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(ledger)
            # The forward GG game uses both absolute loads; the indexed reverse
            # source did not exercise these instruction boundaries previously.
            for opcode in (0x22, 0x2a, 0x32, 0x3a):
                self.assertEqual(ledger.length(bytes([opcode, 0x01, 0xc0]), 0), 3)
            self.assertEqual(ledger.length(bytes([0xdd, 0x2a, 0x01, 0xc0]), 0), 4)
        finally:
            sys.path.pop(0)


if __name__ == '__main__':
    unittest.main()
