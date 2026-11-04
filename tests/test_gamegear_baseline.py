#!/usr/bin/env python3
"""Evidence classification must not turn missing or incomplete runs into passes."""
import importlib.util
import tempfile
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('baseline', Path(__file__).resolve().parents[1] / 'tools/gamegear_baseline.py')
b = importlib.util.module_from_spec(spec)
spec.loader.exec_module(b)


def samples(speeds):
    cycles = 0
    result = []
    for seconds, speed in enumerate(speeds):
        cycles += int(speed * b.CLOCK_HZ)
        result.append(dict(host_elapsed_ns=seconds * 10**9, emulated_cycles=cycles))
    return result


class Classification(unittest.TestCase):
    def test_incomplete_and_missing_never_pass(self):
        for rows in ([], samples([1] * 20)):
            result = b.summarize(rows, 10, False)
            self.assertNotIn('pass', result['statuses'].values())

    def test_warmup_excluded_and_interval_not_lifetime_speed(self):
        rows = samples([.2] * 11 + [1] * 10)
        result = b.summarize(rows, 10, True)
        self.assertEqual(result['statuses']['timing'], 'pass')
        self.assertAlmostEqual(result['median_speed'], 1, places=5)
        self.assertEqual(result['statuses']['audio'], 'unverified')

    def test_sustained_slow_and_isolated_jitter(self):
        self.assertEqual(b.summarize(samples([1] * 12 + [.8] * 3), 10, True)['statuses']['timing'], 'fail')
        result = b.summarize(samples([1] * 12 + [.8, 1, 1]), 10, True)
        self.assertEqual(result['outside_speed_band_count'], 1)
        self.assertFalse(result['sustained_outside_speed_band'])

    def test_underrun_overrides_good_speed(self):
        rows = samples([1] * 20)
        rows[-1]['audio'] = {'underruns_after_priming': 1, 'silence_samples_after_priming': 512}
        self.assertEqual(b.summarize(rows, 10, True)['statuses']['audio'], 'fail')

    def test_startup_loss_remains_failure_with_zero_steady_increment(self):
        rows = samples([1] * 20)
        for row in rows: row['audio'] = {'underruns_after_priming': 1}
        result = b.summarize(rows, 10, True)
        self.assertEqual(result['statuses']['audio'], 'fail')
        self.assertEqual(result['loss_by_phase']['audio.underruns_after_priming']['after_startup_increment'], 0)

    def test_rom_hash_and_sega_header(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'test.gg'
            data = bytearray(8192)
            data[0x1ff0:0x1ff8] = b'TMR SEGA'; data[-1] = 0x5a
            path.write_bytes(data)
            identity = b.rom_identity(path)
            self.assertEqual(identity['sega_headers'][0]['region_code'], 5)
            before = identity['sha256']; data[0] = 1; path.write_bytes(data)
            self.assertNotEqual(before, b.rom_identity(path)['sha256'])

    def test_fallback_missing_capture_and_early_exit_cannot_complete(self):
        with tempfile.TemporaryDirectory() as tmp:
            png = Path(tmp) / 'frame.png'; png.write_bytes(b'checkpoint')
            meta = dict(window_id=7, device_stream=[{'sink': 1}], reached_duration=True, exit_code=0,
                        case={'checkpoints': [{'name': 'title', 'at': 8}]},
                        checkpoints=[dict(name='title', path=str(png), capture={'exit_code': 0})])
            rows = [dict(host_elapsed_ns=120 * 10**9, audio={'drain': {'callback_count': 10}},
                         video={'frames_presented': 10})]
            log = 'Audio output: SDL2 audio output module'
            self.assertEqual(b.assess_run(meta, rows, [], log, 120), (True, True))
            meta['exit_code'] = 1
            self.assertEqual(b.assess_run(meta, rows, [], log, 120), (True, False))
            meta['exit_code'] = 0; png.unlink()
            self.assertEqual(b.assess_run(meta, rows, [], log, 120), (True, False))
            self.assertEqual(b.assess_run(meta, rows, [], 'continuing headless', 120), (False, False))

    def test_delivery_without_guest_indicators_is_not_input_acceptance(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest('Optional Pillow indicator analysis unavailable')
        with tempfile.TemporaryDirectory() as tmp:
            neutral = Path(tmp) / 'neutral.png'
            Image.new('RGB', (160, 144), 'white').save(neutral)
            captures = [dict(name=name + '-' + phase, path=str(neutral))
                        for name in ('up', 'down', 'left', 'right', 'button1', 'button2', 'start')
                        for phase in ('down', 'released')]
            meta = {'case': {'id': 'buttons'}, 'checkpoints': captures}
            self.assertEqual(b.check_button_images(meta)['status'], 'fail')
            meta['checkpoints'] = []
            self.assertEqual(b.check_button_images(meta)['status'], 'unverified')

    def test_soak_keeps_game_admission_without_blind_directional_movement(self):
        case = {'inputs': [{'at': 12, 'key': 'Return', 'hold': .2},
                           {'at': 40, 'key': 'Right', 'hold': 4}], 'checkpoints': []}
        soak = b.prepare_case(case, 600, 'soak')
        self.assertTrue(any(e['key'] == 'Return' for e in soak['inputs']))
        self.assertFalse(any(e['key'] in ('Left', 'Right', 'Up', 'Down') for e in soak['inputs']))
        self.assertTrue(any(e['key'] == 'x' and e['at'] > 120 for e in soak['inputs']))
        self.assertEqual(case['inputs'][1]['key'], 'Right')
        self.assertIn({'at': 595, 'name': 'soak-final'}, soak['checkpoints'])

    def test_corrupt_diagnostics_preserved_as_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'samples.jsonl'
            path.write_text('{"emulated_cycles": 1}\npartial')
            rows, errors = b.read_samples(path)
            self.assertEqual(len(rows), 1)
            self.assertEqual(len(errors), 1)
            self.assertIn('partial', path.read_text())


if __name__ == '__main__': unittest.main()
