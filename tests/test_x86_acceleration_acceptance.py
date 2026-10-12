#!/usr/bin/env python3
"""Keep acceleration acceptance closed on missing, stale or regressing evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import measure_x86_acceptance as acceptance


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tests' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class AcceptanceTests(unittest.TestCase):
    def comparisons(self):
        return [dict(core=core, workload=case, improvementPercent=12,
                     pairedSamples=[dict(repeat=i, improvementPercent=12) for i in range(9)])
                for core in ('gameboy', 'gamegear') for case in ('forward', 'reverse')]

    def test_positive_thresholds_and_corpus_median(self):
        rows = self.comparisons()
        rows[0]['improvementPercent'] = 8
        failures, gains = acceptance.thresholds(rows)
        self.assertEqual(failures, [])
        self.assertEqual(gains, {'gameboy': 10, 'gamegear': 12})

    def test_paired_regression_cannot_hide_behind_ratio_of_medians(self):
        rows = self.comparisons()
        rows[0]['improvementPercent'] = -4
        for pair in rows[0]['pairedSamples'][:5]: pair['improvementPercent'] = -6
        failures, _ = acceptance.thresholds(rows)
        self.assertIn('repeatable regression', failures[0])
        rows = self.comparisons()
        rows[0]['pairedSamples'][0]['improvementPercent'] = -30
        self.assertEqual(acceptance.thresholds(rows)[0], [])

    def test_missing_repetitions_and_nonfinite_samples(self):
        for value in (float('nan'), float('inf')):
            rows = self.comparisons()
            rows[0]['pairedSamples'][0]['improvementPercent'] = value
            with self.assertRaises(ValueError): acceptance.thresholds(rows)
        rows = self.comparisons()
        rows[0]['pairedSamples'].pop()
        with self.assertRaises(ValueError): acceptance.thresholds(rows)

    def fixture(self, directory):
        measure_tests = load('test_measure_acceleration')
        build = directory / 'build'; build.mkdir()
        (build / 'CMakeCache.txt').write_text('BMMQ_ENABLE_TSAN:BOOL=OFF\n')
        for name in ('research-whole-blocks.cpp', 'research-rom-blocks.cpp'):
            (build / name).write_text(name)
        for name in ('binary', *acceptance.BINARY_NAMES.values()):
            (build / name).write_bytes(b'\x7fELF\x02\x01'+b'\x00'*12+b'\x3e\x00')
        (build / 'acceleration-roms').mkdir()
        manifest = build / 'acceleration-roms/rom-corpus.json'
        manifest.write_text('{}')
        reports = {
            'whole-block-corpus': measure_tests.ComparisonTests().corpus(),
            'whole-block-roms': measure_tests.RomComparisonTests().report()}
        for report in reports.values():
            report['repetitions'] = 9
            first = [s for s in report['samples'] if s['repeat'] == 0]
            report['samples'] = [dict(s, repeat=i) for i in range(9) for s in first]
        baseline = dict(repetitions=9, measuredInstructions=100000, warmupInstructions=4096,
            samples=[dict(core=c, workload=w, repeat=i, backend=b, nanoseconds=100,
                          cycles=400000, fingerprint='state', romSha256='rom')
                     for c in ('gameboy', 'gamegear') for w in ('compute', 'ram', 'devices')
                     for i in range(9) for b in ('baseline', 'cached-block', 'portable-ir')])
        reports['core-baselines'] = baseline
        sha = acceptance.measure.digest
        for experiment, report in reports.items():
            report['binding'] = dict(sourceSha256='source', smoke=False, launcher=[],
                measurementKind='native-throughput', host=dict(system='Linux', machine='x86_64'),
                experiment=experiment, cmakeCacheSha256=sha(build / 'CMakeCache.txt'),
                command=[str(build / acceptance.BINARY_NAMES[experiment])],
                binarySha256=sha(build / acceptance.BINARY_NAMES[experiment]))
            if experiment != 'core-baselines':
                generated = 'research-rom-blocks.cpp' if experiment == 'whole-block-roms' else 'research-whole-blocks.cpp'
                report['binding']['generatedSourceSha256'] = sha(build / generated)
            if experiment == 'whole-block-roms':
                report['binding']['romManifestSha256'] = sha(manifest)
                report['manifestSha256'] = sha(manifest)
            (directory / (experiment + '.json')).write_text(json.dumps(report))
        latency = []
        test_results = '\n'.join(f'{i}/5 Test #{i}: {name} .....   Passed    0.01 sec'
                                 for i, name in enumerate(sorted(acceptance.REQUIRED_TESTS), 1))
        for i in range(9):
            name = f'latency-{i}.log'
            (directory / name).write_text('\n'.join(
                f'gate {g} observed_ns=15000000 limit_ns=16000000 enforced=true result=pass'
                for g in sorted(acceptance.REQUIRED_GATES)) +
                '\ngate audio_callback_p99 observed_ns=90 limit_ns=1000000 enforced=true result=pass\n' + test_results + '\n')
            latency.append(dict(repeat=i, log=name, exitCode=0))
        (directory / 'full-ctest.log').write_text(test_results + '\n100% tests passed, 0 tests failed out of 5\n')
        bundle = dict(schema='proto-time-x86-acceptance-v1', sourceSha256='source', build=str(build),
            cmakeCacheSha256=sha(build / 'CMakeCache.txt'), host=dict(system='Linux', machine='x86_64'),
            performanceTests=sorted(acceptance.REQUIRED_TESTS), performanceBinaries={str(build / 'binary'): sha(build / 'binary')},
            latency=latency, fullCtest=dict(log='full-ctest.log', exitCode=0),
            artifacts={p.name: sha(p) for p in directory.iterdir() if p.is_file()})
        return bundle, reports, build

    def test_evidence_binding_and_fail_closed_paths(self):
        for mutation in ('none', 'stale-source', 'qemu', 'smoke', 'artifact', 'binary', 'disabled-gate', 'missing-gate', 'failed-suite', 'failed-latency', 'failed-test-result', 'skipped-suite', 'skipped-performance', 'missing-test-result'):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as tmp:
                directory = Path(tmp)
                bundle, reports, build = self.fixture(directory)
                if mutation == 'stale-source': bundle['sourceSha256'] = 'old'
                elif mutation == 'qemu': reports['whole-block-roms']['binding']['launcher'] = ['qemu']
                elif mutation == 'smoke': reports['whole-block-roms']['binding']['smoke'] = True
                elif mutation == 'artifact': bundle['artifacts'].pop('core-baselines.json')
                elif mutation == 'binary': (build / 'binary').write_text('changed')
                elif mutation in ('disabled-gate', 'missing-gate'):
                    p = directory / 'latency-0.log'
                    text = p.read_text()
                    p.write_text(text.replace('enforced=true', 'enforced=false') if mutation == 'disabled-gate' else '\n'.join(text.splitlines()[1:]))
                    bundle['artifacts'][p.name] = acceptance.measure.digest(p)
                elif mutation == 'failed-suite': bundle['fullCtest']['exitCode'] = 8
                elif mutation == 'failed-latency': bundle['latency'][0]['exitCode'] = 8
                elif mutation in ('failed-test-result', 'skipped-suite', 'skipped-performance', 'missing-test-result'):
                    p = directory / ('latency-0.log' if mutation == 'skipped-performance' else 'full-ctest.log')
                    text = p.read_text()
                    if mutation == 'missing-test-result':
                        p.write_text('\n'.join(text.splitlines()[1:]) + '\n')
                    elif mutation == 'failed-test-result':
                        p.write_text(text.replace('   Passed    0.01 sec', '***Failed    0.01 sec', 1))
                    else:
                        p.write_text(text.replace('Passed    0.01 sec', '***Skipped    0.01 sec', 1))
                    bundle['artifacts'][p.name] = acceptance.measure.digest(p)
                if mutation in ('qemu', 'smoke'):
                    p = directory / 'whole-block-roms.json'
                    p.write_text(json.dumps(reports['whole-block-roms']))
                    bundle['artifacts'][p.name] = acceptance.measure.digest(p)
                (directory / 'evidence.json').write_text(json.dumps(bundle))
                discovery = {'tests': [dict(name=n, command=[str(build / 'binary')]) for n in acceptance.REQUIRED_TESTS]}
                with patch.object(acceptance.measure, 'source_digest', return_value='source'), patch.object(
                        acceptance.subprocess, 'check_output', return_value=json.dumps(discovery)):
                    if mutation in ('none', 'failed-suite', 'failed-latency', 'failed-test-result'):
                        result = acceptance.evaluate(directory)
                        self.assertEqual(result['status'], 'thresholds-met-awaiting-review' if mutation == 'none' else 'failed')
                        self.assertFalse(result['admissionChanged'])
                    else:
                        with self.assertRaises(ValueError): acceptance.evaluate(directory)


if __name__ == '__main__':
    unittest.main()
