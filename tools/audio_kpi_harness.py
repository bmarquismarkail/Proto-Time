#!/usr/bin/env python3
"""Run same-session audio KPI comparisons for Proto-Time.

The harness intentionally lives outside emulator runtime code. It runs a fixed
interleaved matrix, stores raw JSONL/log artifacts, and writes measured-only
summary statistics so audio transport comparisons are not made across unrelated
host sessions.
"""

from __future__ import annotations

import argparse
import json
import os
import shlex
import statistics
import subprocess
import time
from pathlib import Path
from typing import Any


DEFAULT_SCENARIOS = (
    ("balanced", 8, 1),
    ("balanced", 8, 2),
    ("deterministic_test", 8, 1),
    ("deterministic_test", 8, 2),
)


def run_text(args: list[str], cwd: Path) -> str:
    result = subprocess.run(args, cwd=cwd, text=True, capture_output=True, check=False)
    return result.stdout.strip()


def build_type(build_dir: Path) -> str | None:
    cache = build_dir / "CMakeCache.txt"
    if not cache.exists():
        return None
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("CMAKE_BUILD_TYPE:"):
            return line.split("=", 1)[-1].strip() or None
    return None


def read_last_jsonl(path: Path) -> dict[str, Any] | None:
    if not path.exists():
        return None
    last: dict[str, Any] | None = None
    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                last = json.loads(line)
            except json.JSONDecodeError:
                continue
    return last


def get_path(data: dict[str, Any] | None, path: tuple[str, ...], default: Any = None) -> Any:
    current: Any = data
    for key in path:
        if not isinstance(current, dict) or key not in current:
            return default
        current = current[key]
    return current


def div(num: Any, den: Any) -> float | None:
    try:
        if den in (0, None):
            return None
        return float(num) / float(den)
    except (TypeError, ValueError):
        return None


def metrics_from_diag(diag: dict[str, Any] | None, wall_clock_seconds: float) -> dict[str, Any]:
    drain_requested = get_path(diag, ("audio", "drain", "requested_samples_total"), 0)
    drain_ready = get_path(diag, ("audio", "drain", "drained_ready_samples_total"), 0)
    append_calls = get_path(diag, ("audio", "append_recent_pcm", "call_count"), 0)
    appended = get_path(diag, ("audio", "append_recent_pcm", "samples_appended_total"), 0)
    return {
        "underruns": get_path(diag, ("audio", "underruns_after_priming")),
        "silence_samples": get_path(diag, ("audio", "silence_samples_after_priming")),
        "requested_samples": get_path(diag, ("audio", "append_recent_pcm", "engine_samples_requested_total")),
        "accepted_samples": get_path(diag, ("audio", "append_recent_pcm", "engine_samples_accepted_total")),
        "rejected_samples": get_path(diag, ("audio", "append_recent_pcm", "engine_samples_rejected_total")),
        "truncated_samples": get_path(diag, ("audio", "append_recent_pcm", "engine_samples_truncated_total")),
        "drained_ready_samples": drain_ready,
        "drain_requested_samples": drain_requested,
        "drained_requested_ratio": div(drain_ready, drain_requested),
        "append_calls": append_calls,
        "samples_per_append": div(appended, append_calls),
        "source_empty_failures": get_path(diag, ("audio", "worker", "production_source_empty_failures")),
        "ready_full_failures": get_path(diag, ("audio", "worker", "production_ready_queue_full_failures")),
        "produced_blocks": get_path(diag, ("audio", "worker", "produced_blocks")),
        "wake_produced_blocks_0": get_path(diag, ("audio", "worker", "wake_produced_blocks_0")),
        "wake_produced_blocks_1": get_path(diag, ("audio", "worker", "wake_produced_blocks_1")),
        "wake_produced_blocks_2": get_path(diag, ("audio", "worker", "wake_produced_blocks_2")),
        "wake_produced_blocks_3_plus": get_path(diag, ("audio", "worker", "wake_produced_blocks_3_plus")),
        "queue_depth_last": get_path(diag, ("audio", "ready_queue", "depth_last")),
        "queue_depth_high": get_path(diag, ("audio", "ready_queue", "depth_high_water")),
        "queue_depth_low": get_path(diag, ("audio", "ready_queue", "depth_low_water")),
        "effective_speed": get_path(diag, ("effective_emulation_speed",)),
        "frontend_merged_ticks": get_path(diag, ("timing", "frontend_ticks_merged")),
        "wake_jitter_over_2ms": get_path(diag, ("timing", "wake_jitter_over_2ms")),
        "callback_count": get_path(diag, ("audio", "drain", "callback_count")),
        "total_rendered_samples": drain_ready,
        "emulated_cycles": get_path(diag, ("emulated_cycles",)),
        "host_elapsed_ns": get_path(diag, ("host_elapsed_ns",)),
        "wall_clock_seconds": wall_clock_seconds,
    }


def observed_audio_config(diag: dict[str, Any] | None) -> dict[str, Any]:
    callback_size = div(
        get_path(diag, ("audio", "drain", "requested_samples_total"), 0),
        get_path(diag, ("audio", "drain", "callback_count"), 0),
    )
    return {
        "frame_chunk_size": get_path(diag, ("audio", "source_packet", "samples_last")),
        "ring_capacity_samples": get_path(diag, ("audio", "ring_buffer_capacity_samples")),
        "callback_size": callback_size,
        "source_sample_rate": get_path(diag, ("audio", "source_packet", "sample_rate_last")),
        "channel_count": get_path(diag, ("audio", "source_packet", "channel_count_last")),
        "ready_queue_configured_chunks": get_path(diag, ("audio", "ready_queue", "configured_chunks")),
        "batch_configured_chunks": get_path(diag, ("audio", "batching", "configured_chunks")),
    }


def summarize(rows: list[dict[str, Any]]) -> dict[str, Any]:
    metric_keys = sorted({key for row in rows for key in row["metrics"].keys()})
    out: dict[str, Any] = {"run_count": len(rows), "metrics": {}}
    for key in metric_keys:
        vals = [row["metrics"].get(key) for row in rows]
        nums = [float(v) for v in vals if isinstance(v, (int, float)) and v is not None]
        if not nums:
            continue
        out["metrics"][key] = {
            "avg": sum(nums) / len(nums),
            "min": min(nums),
            "max": max(nums),
            "median": statistics.median(nums),
        }
    return out


def scenario_name(profile: str, depth: int, batch: int) -> str:
    return f"{profile}-depth{depth}-batch{batch}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", default=".", type=Path)
    parser.add_argument("--emulator", default="./build-working/timeEmulator", type=Path)
    parser.add_argument("--build-dir", default="./build-working", type=Path)
    parser.add_argument("--rom", default="/home/brandon/Downloads/mm.gg", type=Path)
    parser.add_argument("--output-dir", default="/tmp/proto-time-phase67", type=Path)
    parser.add_argument("--warmup-runs", default=1, type=int)
    parser.add_argument("--measured-runs", default=3, type=int)
    parser.add_argument("--timeout-seconds", default=30, type=int)
    parser.add_argument("--diagnostics-interval-ms", default=1000, type=int)
    parser.add_argument("--sdl-video-driver", default="dummy")
    parser.add_argument("--sdl-audio-driver", default=None)
    parser.add_argument("--core", default="gamegear")
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    emulator = args.emulator if args.emulator.is_absolute() else (repo / args.emulator)
    build_dir = args.build_dir if args.build_dir.is_absolute() else (repo / args.build_dir)

    git_commit = run_text(["git", "rev-parse", "HEAD"], repo)
    dirty_status = run_text(["git", "status", "--short"], repo)
    base_metadata = {
        "git_commit": git_commit,
        "dirty_tree": bool(dirty_status),
        "dirty_status": dirty_status.splitlines(),
        "build_type": build_type(build_dir),
        "emulator": str(emulator),
        "rom": str(args.rom),
        "core": args.core,
        "output_dir": str(output_dir),
        "warmup_runs": args.warmup_runs,
        "measured_runs": args.measured_runs,
        "timeout_seconds": args.timeout_seconds,
        "diagnostics_interval_ms": args.diagnostics_interval_ms,
        "scenarios": [
            {"profile": profile, "audio_depth": depth, "audio_batch": batch}
            for profile, depth, batch in DEFAULT_SCENARIOS
        ],
    }

    run_rows: list[dict[str, Any]] = []
    order_index = 0
    total_rounds = args.warmup_runs + args.measured_runs
    for round_index in range(total_rounds):
        warmup = round_index < args.warmup_runs
        measured_index = None if warmup else round_index - args.warmup_runs + 1
        for profile, depth, batch in DEFAULT_SCENARIOS:
            order_index += 1
            kind = "warmup" if warmup else "measured"
            name = scenario_name(profile, depth, batch)
            stem = f"{order_index:03d}-{kind}-{name}-round{round_index + 1}"
            diag_path = output_dir / f"{stem}.jsonl"
            log_path = output_dir / f"{stem}.log"
            meta_path = output_dir / f"{stem}.meta.json"
            command = [
                str(emulator),
                "--core", args.core,
                "--rom", str(args.rom),
                "--timing-profile", profile,
                "--audio-ready-queue-chunks", str(depth),
                "--audio-batch-chunks", str(batch),
                "--diagnostics-report", str(diag_path),
                "--diagnostics-interval-ms", str(args.diagnostics_interval_ms),
            ]
            env = os.environ.copy()
            if args.sdl_video_driver:
                env["SDL_VIDEODRIVER"] = args.sdl_video_driver
            if args.sdl_audio_driver:
                env["SDL_AUDIODRIVER"] = args.sdl_audio_driver
            started = time.monotonic()
            with log_path.open("w", encoding="utf-8") as log:
                result = subprocess.run(
                    ["timeout", f"{args.timeout_seconds}s", *command],
                    cwd=repo,
                    env=env,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    check=False,
                )
            wall = time.monotonic() - started
            diag = read_last_jsonl(diag_path)
            row = {
                **base_metadata,
                "run_order_index": order_index,
                "round_index": round_index + 1,
                "measured_index": measured_index,
                "warmup": warmup,
                "profile": profile,
                "audio_depth": depth,
                "audio_batch": batch,
                "workload": "gamegear_rom",
                "command_line": " ".join(shlex.quote(part) for part in command),
                "return_code": result.returncode,
                "diagnostics_path": str(diag_path),
                "log_path": str(log_path),
                "metadata_path": str(meta_path),
                "observed_audio": observed_audio_config(diag),
                "metrics": metrics_from_diag(diag, wall),
            }
            meta_path.write_text(json.dumps(row, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            run_rows.append(row)

    measured = [row for row in run_rows if not row["warmup"]]
    grouped: dict[str, list[dict[str, Any]]] = {}
    for row in measured:
        grouped.setdefault(scenario_name(row["profile"], row["audio_depth"], row["audio_batch"]), []).append(row)
    summary = {
        "metadata": base_metadata,
        "generated_at_unix": time.time(),
        "run_count": len(run_rows),
        "measured_run_count": len(measured),
        "runs": run_rows,
        "summary_by_scenario": {name: summarize(rows) for name, rows in sorted(grouped.items())},
    }
    summary_path = output_dir / "summary.json"
    manifest_path = output_dir / "manifest.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    manifest_path.write_text(json.dumps({"runs": run_rows}, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {summary_path}")
    print(f"wrote {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
