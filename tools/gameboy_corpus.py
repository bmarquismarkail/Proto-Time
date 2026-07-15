#!/usr/bin/env python3
"""Run a deterministic Game Boy ROM corpus across Proto-Time CPU backends.

ROM bytes are read from loose .gb/.gbc files or directly from zip members. They
are staged in a fresh temporary directory for every invocation, so corpus input
and adjacent battery saves are never modified or reused between backends.
"""

from __future__ import annotations

import argparse
from collections import Counter
import dataclasses
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tempfile
from typing import Any, Optional, Sequence
import zipfile
import statistics


SUPPORTED_SUFFIXES = {".gb", ".gbc"}
DEFAULT_MAX_ROM_BYTES = 16 * 1024 * 1024
RESULT_SCHEMA = "proto-time-gameboy-corpus-v1"


@dataclasses.dataclass(frozen=True)
class CorpusCase:
    source: Path
    relative_source: str
    member: Optional[str]
    size: int

    @property
    def locator(self) -> str:
        return self.relative_source if self.member is None else f"{self.relative_source}::{self.member}"

    @property
    def suffix(self) -> str:
        return Path(self.member or self.source.name).suffix.lower()

    @property
    def case_id(self) -> str:
        return hashlib.sha256(self.locator.encode("utf-8", "surrogateescape")).hexdigest()[:16]


@dataclasses.dataclass(frozen=True)
class DiscoveryIssue:
    source: str
    reason: str


def _relative_display(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def discover_cases(root: Path, max_rom_bytes: int) -> tuple[list[CorpusCase], list[DiscoveryIssue]]:
    """Discover supported loose ROMs and every supported member of each zip."""
    cases: list[CorpusCase] = []
    issues: list[DiscoveryIssue] = []
    candidates: list[Path] = []

    def record_walk_error(error: OSError) -> None:
        issues.append(DiscoveryIssue(error.filename or root.as_posix(), f"unable to enumerate directory: {error}"))

    for directory, dirnames, filenames in os.walk(root, onerror=record_walk_error, followlinks=False):
        dirnames.sort(key=lambda name: (name.casefold(), name))
        filenames.sort(key=lambda name: (name.casefold(), name))
        candidates.extend(Path(directory) / filename for filename in filenames)
    candidates.sort(key=lambda path: (_relative_display(path, root).casefold(), _relative_display(path, root)))

    for path in candidates:
        relative = _relative_display(path, root)
        suffix = path.suffix.lower()
        if suffix in SUPPORTED_SUFFIXES:
            try:
                size = path.stat().st_size
            except OSError as error:
                issues.append(DiscoveryIssue(relative, f"unable to stat ROM: {error}"))
                continue
            if size <= 0:
                issues.append(DiscoveryIssue(relative, "ROM is empty"))
            elif size > max_rom_bytes:
                issues.append(DiscoveryIssue(relative, f"ROM exceeds {max_rom_bytes} byte safety limit"))
            else:
                cases.append(CorpusCase(path, relative, None, size))
            continue
        if suffix != ".zip":
            continue

        try:
            with zipfile.ZipFile(path) as archive:
                members = sorted(
                    (
                        info for info in archive.infolist()
                        if not info.is_dir()
                        and PurePosixPath(info.filename).suffix.lower() in SUPPORTED_SUFFIXES
                    ),
                    key=lambda info: (info.filename.casefold(), info.filename),
                )
                if not members:
                    issues.append(DiscoveryIssue(relative, "archive has no .gb or .gbc members"))
                duplicate_names = {
                    name for name, count in Counter(info.filename for info in members).items()
                    if count > 1
                }
                for info in members:
                    label = f"{relative}::{info.filename}"
                    if info.filename in duplicate_names:
                        issues.append(DiscoveryIssue(label, "duplicate archive member name is ambiguous"))
                    elif info.flag_bits & 0x1:
                        issues.append(DiscoveryIssue(label, "encrypted archive member is unsupported"))
                    elif info.file_size <= 0:
                        issues.append(DiscoveryIssue(label, "ROM is empty"))
                    elif info.file_size > max_rom_bytes:
                        issues.append(DiscoveryIssue(label, f"ROM exceeds {max_rom_bytes} byte safety limit"))
                    else:
                        cases.append(CorpusCase(path, relative, info.filename, info.file_size))
        except (OSError, zipfile.BadZipFile, RuntimeError) as error:
            issues.append(DiscoveryIssue(relative, f"unable to inspect archive: {error}"))

    cases.sort(key=lambda case: (case.locator.casefold(), case.locator))
    return cases, issues


def read_case(case: CorpusCase, max_rom_bytes: int) -> bytes:
    if case.member is None:
        data = case.source.read_bytes()
    else:
        with zipfile.ZipFile(case.source) as archive:
            info = archive.getinfo(case.member)
            if info.file_size > max_rom_bytes:
                raise ValueError("archive member grew beyond the configured safety limit")
            data = archive.read(info)
    if not data:
        raise ValueError("ROM is empty")
    if len(data) > max_rom_bytes:
        raise ValueError("ROM exceeds the configured safety limit")
    if len(data) != case.size:
        raise ValueError(f"ROM size changed after discovery ({case.size} -> {len(data)})")
    return data


def select_deterministic_sample(cases: Sequence[CorpusCase], count: int, seed: str) -> list[CorpusCase]:
    ranked = sorted(
        cases,
        key=lambda case: hashlib.sha256(
            seed.encode("utf-8", "surrogateescape") + b"\0" +
            case.locator.encode("utf-8", "surrogateescape")
        ).digest(),
    )
    selected = ranked[:count]
    return sorted(selected, key=lambda case: (case.locator.casefold(), case.locator))


def rom_metadata(data: bytes) -> dict[str, Any]:
    title_bytes = data[0x134:0x144] if len(data) >= 0x144 else b""
    title = title_bytes.split(b"\0", 1)[0].decode("ascii", "replace").strip()
    return {
        "sha256": hashlib.sha256(data).hexdigest(),
        "bytes": len(data),
        "title": title,
        "cartridge_type": data[0x147] if len(data) > 0x147 else None,
        "rom_size_code": data[0x148] if len(data) > 0x148 else None,
        "ram_size_code": data[0x149] if len(data) > 0x149 else None,
    }


def read_last_diagnostic(path: Path) -> dict[str, Any]:
    last: Optional[dict[str, Any]] = None
    with path.open("r", encoding="utf-8") as source:
        for line_number, line in enumerate(source, 1):
            if not line.strip():
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError as error:
                raise ValueError(f"invalid diagnostics JSON on line {line_number}: {error}") from error
            if not isinstance(value, dict):
                raise ValueError(f"diagnostics line {line_number} is not an object")
            last = value
    if last is None:
        raise ValueError("diagnostics report has no samples")
    return last


def diagnostic_contract(sample: dict[str, Any]) -> dict[str, Any]:
    state = sample.get("deterministic_state")
    if not isinstance(state, dict):
        raise ValueError("diagnostics are missing deterministic_state")
    required = {
        "retired_instructions": sample.get("retired_instructions"),
        "emulated_cycles": sample.get("emulated_cycles"),
        "state_schema": state.get("schema"),
        "state_fingerprint": state.get("fingerprint"),
    }
    if not isinstance(required["retired_instructions"], int):
        raise ValueError("diagnostics are missing integer retired_instructions")
    if not isinstance(required["emulated_cycles"], int):
        raise ValueError("diagnostics are missing integer emulated_cycles")
    if not isinstance(required["state_schema"], str) or not isinstance(required["state_fingerprint"], str):
        raise ValueError("diagnostics are missing a Game Boy state fingerprint")
    return required


def run_backend(
    emulator: Path,
    rom: bytes,
    suffix: str,
    mode: str,
    repetition: int,
    steps: int,
    timeout_seconds: float,
    case_dir: Path,
) -> dict[str, Any]:
    run_name = f"{mode}-{repetition}"
    log_path = case_dir / f"{run_name}.log"
    diagnostics_path = case_dir / f"{run_name}.jsonl"
    outcome: dict[str, Any] = {"mode": mode, "repetition": repetition, "status": "error"}

    with tempfile.TemporaryDirectory(prefix="proto-time-gb-corpus-") as temporary:
        staged_rom = Path(temporary) / f"corpus{suffix}"
        staged_rom.write_bytes(rom)
        command = [
            str(emulator), "--core", "gameboy", "--rom", str(staged_rom),
            "--steps", str(steps), "--cpu-mode", mode, "--headless",
            "--no-audio", "--audio-backend", "dummy", "--unthrottled",
            "--diagnostics-report", str(diagnostics_path),
            "--diagnostics-interval-ms", "2147483647",
        ]
        try:
            with log_path.open("wb") as output:
                completed = subprocess.run(
                    command,
                    stdout=output,
                    stderr=subprocess.STDOUT,
                    timeout=timeout_seconds,
                    check=False,
                    cwd=temporary,
                )
            outcome["exit_code"] = completed.returncode
            if completed.returncode != 0:
                outcome["reason"] = f"emulator exited with status {completed.returncode}"
                return outcome
        except subprocess.TimeoutExpired:
            outcome["reason"] = f"emulator exceeded {timeout_seconds:g}s timeout"
            outcome["timed_out"] = True
            return outcome
        except OSError as error:
            outcome["reason"] = f"unable to execute emulator: {error}"
            return outcome

    try:
        sample = read_last_diagnostic(diagnostics_path)
        contract = diagnostic_contract(sample)
    except (OSError, ValueError) as error:
        outcome["reason"] = str(error)
        return outcome

    outcome.update(contract)
    outcome["host_elapsed_ns"] = sample.get("host_elapsed_ns")
    outcome["effective_cycles_per_second"] = sample.get("effective_cycles_per_second")
    outcome["cpu_block_cache"] = sample.get("cpu_block_cache")
    if contract["retired_instructions"] != steps:
        outcome["reason"] = (
            f"retired {contract['retired_instructions']} instructions; expected {steps}"
        )
        return outcome
    outcome["status"] = "ok"
    outcome.pop("reason", None)
    diagnostics_path.unlink(missing_ok=True)
    return outcome


def assess_runs(runs: Sequence[dict[str, Any]]) -> tuple[str, list[str]]:
    failures = [run.get("reason", "backend run failed") for run in runs if run.get("status") != "ok"]
    if failures:
        return "error", failures
    contracts = {
        (run["retired_instructions"], run["emulated_cycles"], run["state_schema"], run["state_fingerprint"])
        for run in runs
    }
    if len(contracts) != 1:
        by_run = {
            f"{run['mode']}#{run['repetition']}": {
                "retired_instructions": run["retired_instructions"],
                "emulated_cycles": run["emulated_cycles"],
                "state_schema": run["state_schema"],
                "state_fingerprint": run["state_fingerprint"],
            }
            for run in runs
        }
        return "mismatch", [f"backend deterministic contracts differ: {json.dumps(by_run, sort_keys=True)}"]
    return "pass", []


def summarize_case_performance(runs: Sequence[dict[str, Any]]) -> dict[str, Any]:
    """Summarize unbiased terminal diagnostics without imposing an IR gate."""
    by_mode: dict[str, list[dict[str, Any]]] = {}
    for run in runs:
        elapsed = run.get("host_elapsed_ns")
        if run.get("status") != "ok" or not isinstance(elapsed, int) or elapsed <= 0:
            continue
        by_mode.setdefault(str(run["mode"]), []).append(run)

    modes: dict[str, dict[str, Any]] = {}
    for mode, mode_runs in by_mode.items():
        elapsed_values = [int(run["host_elapsed_ns"]) for run in mode_runs]
        cps_values = [
            float(run["effective_cycles_per_second"])
            for run in mode_runs
            if isinstance(run.get("effective_cycles_per_second"), (int, float))
        ]
        modes[mode] = {
            "samples": len(mode_runs),
            "median_host_elapsed_ns": statistics.median(elapsed_values),
            "median_effective_cycles_per_second": statistics.median(cps_values) if cps_values else None,
        }

    baseline = modes.get("baseline", {}).get("median_host_elapsed_ns")
    if isinstance(baseline, (int, float)) and baseline > 0:
        for mode_data in modes.values():
            elapsed = mode_data["median_host_elapsed_ns"]
            mode_data["speedup_vs_baseline"] = baseline / elapsed

    for backend in ("ir", "native"):
        backend_runs = by_mode.get(backend, [])
        executions = fallbacks = entries = continuations = 0
        for run in backend_runs:
            stats = run.get("cpu_block_cache")
            if not isinstance(stats, dict):
                continue
            def metric(name: str) -> int:
                value = stats.get(name)
                return value if isinstance(value, int) and value >= 0 else 0
            executions += metric("ir_executions")
            fallbacks += metric("ir_fallbacks")
            entries += metric("ir_block_entries")
            continuations += metric("ir_block_continuations")
        if backend_runs:
            total_dispatches = executions + fallbacks
            modes.setdefault(backend, {})["dispatch_coverage"] = (
                executions / total_dispatches if total_dispatches else None
            )
            modes[backend]["average_continuations_per_entry"] = (
                continuations / entries if entries else None
            )
    return {"modes": modes}


def summarize_corpus_performance(results: Sequence[dict[str, Any]]) -> dict[str, Any]:
    speedups: dict[str, list[float]] = {}
    dispatch = {
        mode: {"executions": 0, "fallbacks": 0, "entries": 0, "continuations": 0}
        for mode in ("ir", "native")
    }
    for result in results:
        performance = result.get("performance")
        if not isinstance(performance, dict):
            continue
        for mode, mode_data in performance.get("modes", {}).items():
            speedup = mode_data.get("speedup_vs_baseline")
            if isinstance(speedup, (int, float)) and speedup > 0:
                speedups.setdefault(mode, []).append(float(speedup))
        for run in result.get("runs", []):
            mode = run.get("mode")
            if mode not in dispatch or run.get("status") != "ok":
                continue
            stats = run.get("cpu_block_cache")
            if not isinstance(stats, dict):
                continue
            def metric(name: str) -> int:
                value = stats.get(name)
                return value if isinstance(value, int) and value >= 0 else 0
            dispatch[mode]["executions"] += metric("ir_executions")
            dispatch[mode]["fallbacks"] += metric("ir_fallbacks")
            dispatch[mode]["entries"] += metric("ir_block_entries")
            dispatch[mode]["continuations"] += metric("ir_block_continuations")

    modes: dict[str, dict[str, Any]] = {}
    for mode, values in speedups.items():
        modes[mode] = {
            "cases": len(values),
            "median_speedup_vs_baseline": statistics.median(values),
            "geometric_mean_speedup_vs_baseline": math.exp(
                sum(math.log(value) for value in values) / len(values)
            ),
            "min_speedup_vs_baseline": min(values),
            "max_speedup_vs_baseline": max(values),
        }
    summary = {
        "modes": modes,
        "interpretation": "informational; only deterministic state/cycle agreement is a corpus gate",
    }
    for mode, values in dispatch.items():
        total = values["executions"] + values["fallbacks"]
        summary[f"{mode}_dispatch_coverage"] = (
            values["executions"] / total if total else None
        )
        summary[f"{mode}_average_continuations_per_entry"] = (
            values["continuations"] / values["entries"] if values["entries"] else None
        )
    return summary


def json_dump(path: Path, value: Any) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(path)


def parse_modes(value: str) -> list[str]:
    modes = [item.strip() for item in value.split(",") if item.strip()]
    invalid = [item for item in modes if item not in {"baseline", "block", "ir", "native"}]
    if invalid or not modes:
        raise argparse.ArgumentTypeError("modes must be a comma-separated subset of baseline,block,ir,native")
    if "baseline" not in modes:
        raise argparse.ArgumentTypeError("baseline must be included as the correctness oracle")
    return list(dict.fromkeys(modes))


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom-root", type=Path, default=Path("/data/roms/gb"))
    parser.add_argument("--emulator", type=Path, default=Path("build-working/timeEmulator"))
    parser.add_argument("--output-dir", type=Path, default=Path("logs/gameboy-corpus"))
    parser.add_argument("--steps", type=int, default=100_000)
    parser.add_argument("--timeout", type=float, default=60.0, help="seconds per backend invocation")
    parser.add_argument("--modes", type=parse_modes, default=parse_modes("baseline,block,ir,native"))
    parser.add_argument("--repeat", type=int, default=1, help="runs per ROM/backend")
    parser.add_argument("--limit", type=int, help="run the first N deterministically sorted cases")
    parser.add_argument("--sample", type=int, help="run N cases selected by a stable hash")
    parser.add_argument("--sample-seed", default="proto-time-phase-11d")
    parser.add_argument("--match", help="regular expression applied to archive/member locator")
    parser.add_argument("--max-rom-bytes", type=int, default=DEFAULT_MAX_ROM_BYTES)
    parser.add_argument("--strict-inputs", action="store_true", help="fail if discovery found malformed inputs")
    parser.add_argument("--keep-passing-logs", action="store_true")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    if args.steps <= 0 or args.timeout <= 0 or args.repeat <= 0 or args.max_rom_bytes <= 0:
        raise SystemExit("steps, timeout, repeat, and max-rom-bytes must be positive")
    if args.limit is not None and args.limit <= 0:
        raise SystemExit("limit must be positive")
    if args.sample is not None and args.sample <= 0:
        raise SystemExit("sample must be positive")
    if args.limit is not None and args.sample is not None:
        raise SystemExit("limit and sample are mutually exclusive")
    root = args.rom_root.resolve()
    emulator = args.emulator.resolve()
    if not root.is_dir():
        raise SystemExit(f"ROM root is not a directory: {root}")
    if not emulator.is_file() or not os.access(emulator, os.X_OK):
        raise SystemExit(f"emulator is not executable: {emulator}")

    cases, issues = discover_cases(root, args.max_rom_bytes)
    if args.match:
        try:
            matcher = re.compile(args.match)
        except re.error as error:
            raise SystemExit(f"invalid --match expression: {error}") from error
        cases = [case for case in cases if matcher.search(case.locator)]
    if args.sample is not None:
        cases = select_deterministic_sample(cases, args.sample, args.sample_seed)
    if args.limit is not None:
        cases = cases[:args.limit]
    if not cases:
        raise SystemExit("no runnable Game Boy ROMs were discovered")

    run_id = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ") + f"-{os.getpid()}"
    run_dir = args.output_dir.resolve() / run_id
    run_dir.mkdir(parents=True, exist_ok=False)
    manifest = {
        "schema": RESULT_SCHEMA,
        "rom_root": root.as_posix(),
        "emulator": emulator.as_posix(),
        "steps": args.steps,
        "timeout_seconds": args.timeout,
        "modes": args.modes,
        "repeat": args.repeat,
        "selection": {
            "limit": args.limit,
            "sample": args.sample,
            "sample_seed": args.sample_seed if args.sample is not None else None,
            "match": args.match,
        },
        "cases": [case.locator for case in cases],
        "discovery_issues": [dataclasses.asdict(issue) for issue in issues],
    }
    json_dump(run_dir / "manifest.json", manifest)

    print(f"Discovered {len(cases)} runnable ROM case(s); results: {run_dir}")
    results: list[dict[str, Any]] = []
    for index, case in enumerate(cases, 1):
        case_dir = run_dir / case.case_id
        case_dir.mkdir()
        result: dict[str, Any] = {
            "case_id": case.case_id,
            "locator": case.locator,
            "status": "error",
            "reasons": [],
            "runs": [],
        }
        try:
            rom = read_case(case, args.max_rom_bytes)
            result["rom"] = rom_metadata(rom)
            for mode in args.modes:
                for repetition in range(1, args.repeat + 1):
                    result["runs"].append(
                        run_backend(
                            emulator, rom, case.suffix, mode, repetition, args.steps,
                            args.timeout, case_dir,
                        )
                    )
            result["status"], result["reasons"] = assess_runs(result["runs"])
            result["performance"] = summarize_case_performance(result["runs"])
        except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as error:
            result["reasons"] = [f"unable to read ROM: {error}"]
        if result["status"] == "pass" and not args.keep_passing_logs:
            for log in case_dir.glob("*.log"):
                log.unlink()
            try:
                case_dir.rmdir()
            except OSError:
                pass
        results.append(result)
        print(f"[{index}/{len(cases)}] {result['status'].upper():8} {case.locator}")
        if result["status"] != "pass":
            for reason in result["reasons"]:
                print(f"  {reason}")

    counts = {status: sum(result["status"] == status for result in results) for status in ("pass", "mismatch", "error")}
    summary = {
        "schema": RESULT_SCHEMA,
        "counts": counts,
        "selected_cases": len(cases),
        "discovery_issue_count": len(issues),
        "strict_inputs": args.strict_inputs,
        "performance": summarize_corpus_performance(results),
        "success": counts["mismatch"] == 0 and counts["error"] == 0 and (not args.strict_inputs or not issues),
    }
    json_dump(run_dir / "results.json", results)
    json_dump(run_dir / "summary.json", summary)
    print(
        f"Summary: {counts['pass']} passed, {counts['mismatch']} mismatched, "
        f"{counts['error']} errored, {len(issues)} discovery issue(s)"
    )
    return 0 if summary["success"] else 1


if __name__ == "__main__":
    sys.exit(main())
