#!/usr/bin/env python3

from __future__ import annotations

import json
import os
from pathlib import Path
import stat
import sys
import tempfile
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import gameboy_corpus  # noqa: E402


FAKE_EMULATOR = r'''#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sys

args = sys.argv[1:]
def value(name):
    return args[args.index(name) + 1]

mode = value("--cpu-mode")
steps = int(value("--steps"))
diagnostics = Path(value("--diagnostics-report"))
if os.environ.get("PROTO_TIME_CORPUS_TEST_UNSUPPORTED") == "1":
    print("error: ROM too large")
    raise SystemExit(1)
fingerprint = "1111222233334444"
if os.environ.get("PROTO_TIME_CORPUS_TEST_MISMATCH") == "1" and mode == "ir":
    fingerprint = "9999aaaabbbbcccc"
sample = {
    "host_elapsed_ns": 100,
    "emulated_cycles": steps * 4,
    "retired_instructions": steps,
    "effective_cycles_per_second": 1234.5,
    "deterministic_state": {"schema": "gameboy-v1", "fingerprint": fingerprint},
    "cpu_block_cache": {
        "mode": mode,
        "hits": 8 if mode == "block" else 0,
        "misses": 2 if mode == "block" else 0,
        "translations": 4 if mode == "block" else 0,
        "translated_instructions": 12 if mode == "block" else 0,
        "invalidations": 1 if mode == "block" else 0,
        "guard_failures": 0,
        "chain_continuations": 6 if mode == "block" else 0,
        "unsupported_fallbacks": 1 if mode == "block" else 0,
        "fast_eligibility_stops": 2 if mode == "block" else 0,
        "invalidation_requests": 4 if mode == "block" else 0,
        "invalidation_page_skips": 3 if mode == "block" else 0,
        "invalidation_scans": 1 if mode == "block" else 0,
        "invalidation_blocks_examined": 5 if mode == "block" else 0,
        "unsupported_fallback_opcodes": {"0xCB": 1} if mode == "block" else {},
        "fast_eligibility_stop_opcodes": {"0xCB": 2} if mode == "block" else {},
        "ir_executions": 8 if mode in {"ir", "native"} else 0,
        "ir_fallbacks": 2 if mode in {"ir", "native"} else 0,
        "ir_block_entries": 2 if mode in {"ir", "native"} else 0,
        "ir_block_continuations": 4 if mode in {"ir", "native"} else 0,
    },
}
diagnostics.write_text(json.dumps(sample) + "\n", encoding="utf-8")
'''


class CorpusToolTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="proto-time-corpus-test-")
        self.root = Path(self.temporary.name)
        self.rom_root = self.root / "roms"
        self.output = self.root / "output"
        self.rom_root.mkdir()
        self.emulator = self.root / "timeEmulator"
        self.emulator.write_text(FAKE_EMULATOR, encoding="utf-8")
        self.emulator.chmod(self.emulator.stat().st_mode | stat.S_IXUSR)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    @staticmethod
    def rom(fill: int = 0) -> bytes:
        data = bytearray([fill] * 0x8000)
        data[0x134:0x139] = b"TEST\0"
        return bytes(data)

    def invoke(self, *extra: str) -> int:
        return gameboy_corpus.main([
            "--rom-root", str(self.rom_root),
            "--emulator", str(self.emulator),
            "--output-dir", str(self.output),
            "--steps", "12",
            "--timeout", "5",
            *extra,
        ])

    def only_run_dir(self) -> Path:
        children = list(self.output.iterdir())
        self.assertEqual(len(children), 1)
        return children[0]

    def test_discovers_loose_and_all_archive_members_in_stable_order(self) -> None:
        (self.rom_root / "z.gb").write_bytes(self.rom())
        with zipfile.ZipFile(self.rom_root / "archive.zip", "w") as archive:
            archive.writestr("folder/B game.gbc", self.rom(1))
            archive.writestr("A game.gb", self.rom(2))
            archive.writestr("notes.txt", b"ignored")
        (self.rom_root / "not-a-zip.zip").write_bytes(b"broken")

        cases, issues = gameboy_corpus.discover_cases(
            self.rom_root, gameboy_corpus.DEFAULT_MAX_ROM_BYTES
        )
        self.assertEqual(
            [case.locator for case in cases],
            ["archive.zip::A game.gb", "archive.zip::folder/B game.gbc", "z.gb"],
        )
        self.assertTrue(any(issue.source == "not-a-zip.zip" for issue in issues))
        sample_a = gameboy_corpus.select_deterministic_sample(cases, 2, "seed")
        sample_b = gameboy_corpus.select_deterministic_sample(list(reversed(cases)), 2, "seed")
        self.assertEqual([case.locator for case in sample_a], [case.locator for case in sample_b])

    def test_runs_all_backends_and_records_matching_contract(self) -> None:
        with zipfile.ZipFile(self.rom_root / "game with spaces.zip", "w") as archive:
            archive.writestr("game with spaces.gb", self.rom())

        self.assertEqual(self.invoke(), 0)
        run_dir = self.only_run_dir()
        summary = json.loads((run_dir / "summary.json").read_text(encoding="utf-8"))
        results = json.loads((run_dir / "results.json").read_text(encoding="utf-8"))
        self.assertTrue(summary["success"])
        self.assertEqual(
            summary["counts"],
            {"error": 0, "mismatch": 0, "pass": 1, "unsupported": 0},
        )
        self.assertEqual(
            [run["mode"] for run in results[0]["runs"]],
            ["baseline", "block", "ir"],
        )
        self.assertEqual({run["state_fingerprint"] for run in results[0]["runs"]}, {"1111222233334444"})
        self.assertEqual(results[0]["performance"]["modes"]["ir"]["speedup_vs_baseline"], 1.0)
        self.assertEqual(summary["performance"]["modes"]["block"]["cases"], 1)
        self.assertEqual(summary["performance"]["ir_dispatch_coverage"], 0.8)
        self.assertEqual(summary["performance"]["ir_average_continuations_per_entry"], 2.0)
        block = summary["performance"]["block_cache"]
        self.assertEqual(block["lookup_hit_rate"], 0.8)
        self.assertEqual(block["average_translated_instructions"], 3.0)
        self.assertEqual(block["chain_continuations_per_hit"], 0.75)
        self.assertEqual(block["unsupported_fallbacks_per_translation"], 0.25)
        self.assertEqual(block["fast_eligibility_stops_per_translation"], 0.5)
        self.assertEqual(block["invalidation_page_skip_rate"], 0.75)
        self.assertEqual(block["average_blocks_examined_per_scan"], 5.0)
        self.assertEqual(block["unsupported_fallback_opcodes"], {"0xCB": 1})
        case_block = results[0]["performance"]["modes"]["block"]["block_cache"]
        self.assertEqual(case_block["fast_eligibility_stop_opcodes"], {"0xCB": 2})

    def test_state_difference_is_a_failure(self) -> None:
        (self.rom_root / "game.gb").write_bytes(self.rom())
        with mock.patch.dict(os.environ, {"PROTO_TIME_CORPUS_TEST_MISMATCH": "1"}):
            self.assertEqual(self.invoke(), 1)
        summary = json.loads((self.only_run_dir() / "summary.json").read_text(encoding="utf-8"))
        self.assertEqual(summary["counts"]["mismatch"], 1)

    def test_native_mode_is_explicit_and_capability_checked(self) -> None:
        (self.rom_root / "game.gb").write_bytes(self.rom())
        with mock.patch.object(gameboy_corpus, "native_backend_supported", return_value=False):
            with self.assertRaisesRegex(SystemExit, "x86-64 POSIX"):
                self.invoke("--modes", "baseline,native")

    def test_uniform_core_rejection_is_unsupported(self) -> None:
        (self.rom_root / "large.gbc").write_bytes(self.rom())
        with mock.patch.dict(os.environ, {"PROTO_TIME_CORPUS_TEST_UNSUPPORTED": "1"}):
            self.assertEqual(self.invoke(), 1)
        summary = json.loads((self.only_run_dir() / "summary.json").read_text(encoding="utf-8"))
        self.assertEqual(summary["counts"]["unsupported"], 1)
        self.assertEqual(summary["counts"]["error"], 0)


if __name__ == "__main__":
    unittest.main()
