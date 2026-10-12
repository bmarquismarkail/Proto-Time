#!/usr/bin/env python3
"""Validate the two-core completion contract; never manufacture acceptance evidence."""
import argparse
import hashlib
import json
from pathlib import Path

CORES = ("gameboy", "gamegear")
HOSTS = ("linux-x86_64", "linux-aarch64")
MATRIX = ".internal/docs/time-feature-completion.json"
REQUIRED = {
    "hardware", "space.capture", "space.snapshot", "space.analysis", "space.symbols",
    "space.exploration", "port.forward", "port.reverse", "execution.replay", "execution.ir",
    "input", "input.native", "input.netplay", "video", "video.remote", "video.debug",
    "audio", "timing", "observation", "mods.state", "visual.authoring", "visual.shader",
    "debugger", "debugger.dap", "script.file", "script.lua", "script.python", "script.javascript",
    "script.live", "providers.dynamic", "acceleration.native",
}


def source_digest(root):
    """Hash source contents, including new files, without Git/index/build dependence."""
    h = hashlib.sha256()
    roots = ["cores", "machine", "memory", "inst_cycle", "emulator", "space", "tests", "tools", "docs"]
    paths = [root / "CMakeLists.txt", root / "emulator.cpp", root / MATRIX]
    matrix=json.loads((root / MATRIX).read_text())
    paths.extend(root / r for f in matrix["features"] for r in f["references"])
    workflow=root / ".github/workflows/feature-completion.yml"
    if workflow.is_file(): paths.append(workflow)
    for folder in roots:
        paths.extend(p for p in (root / folder).rglob("*") if p.is_file() and
                     "__pycache__" not in p.parts and not p.is_symlink())
    for p in sorted(set(paths)):
        h.update(p.relative_to(root).as_posix().encode() + b"\0")
        h.update(p.read_bytes())
    return h.hexdigest()


def evaluate(root, evidence=None):
    matrix = json.loads((root / MATRIX).read_text())
    if matrix["schemaVersion"] != 1 or matrix["cores"] != list(CORES) or matrix["hosts"] != list(HOSTS):
        raise ValueError("unsupported completion contract")
    features = matrix["features"]
    ids = [f["id"] for f in features]
    if len(ids) != len(set(ids)) or set(ids) != REQUIRED:
        raise ValueError("missing, duplicate or unknown required feature")
    for f in features:
        if set(f.get("coreAcceptance",{}))!=set(CORES) or any(not v for v in f["coreAcceptance"].values()) or f.get("hostCoverage")!=list(HOSTS):
            raise ValueError("missing per-core acceptance or host coverage: "+f["id"])
        if not f["acceptance"] or not f["automated"] or not f["references"]:
            raise ValueError("feature lacks acceptance, tests or authority: " + f["id"])
        if f["allowReviewedNoGo"] != (f["id"] == "acceleration.native"):
            raise ValueError("no-go is restricted to native research")
        for reference in f["references"]:
            p = (root / reference).resolve()
            if not p.is_relative_to(root.resolve()) or not p.is_file():
                raise ValueError("missing feature authority: " + reference)
    digest = source_digest(root)
    records = {}
    configuration = None
    if evidence is not None:
        doc = json.loads(evidence.read_text())
        if doc.get("schemaVersion") != 1 or doc.get("sourceSha256") != digest:
            raise ValueError("stale or incompatible completion evidence")
        configuration = doc.get("buildConfigurations", {})
        required_config={"compiler", "compilerVersion", "buildType", "cmakeCacheSha256", "cpu", "configureCommand", "buildCommand", "testCommand"}
        if set(configuration) != set(HOSTS) or any(not isinstance(v,dict) or not required_config <= set(v) or any(not v.get(k) for k in required_config) for v in configuration.values()):
            raise ValueError("missing effective host build configurations")
        for r in doc["records"]:
            key = (r["feature"], r["core"], r["host"], r["kind"])
            if key in records or r["feature"] not in REQUIRED or r["core"] not in CORES or r["host"] not in HOSTS or r["kind"] not in {"automated", "live"}:
                raise ValueError("duplicate or unknown acceptance record")
            config_sha=hashlib.sha256(json.dumps(configuration[r["host"]], sort_keys=True, separators=(",", ":")).encode()).hexdigest()
            if r.get("buildConfigurationSha256") != config_sha:
                raise ValueError("acceptance record build configuration mismatch")
            if r["status"] not in {"passed", "failed", "not-run", "reviewed-no-go"}:
                raise ValueError("invalid acceptance status")
            if r["status"] == "reviewed-no-go" and (r["feature"] != "acceleration.native" or not r.get("reviewer")):
                raise ValueError("unreviewed or inadmissible no-go")
            artifact = (evidence.parent / r["artifact"]).resolve()
            if not artifact.is_relative_to(evidence.parent.resolve()) or not artifact.is_file():
                raise ValueError("missing/escaping acceptance artifact")
            if hashlib.sha256(artifact.read_bytes()).hexdigest() != r["artifactSha256"]:
                raise ValueError("acceptance artifact digest mismatch")
            records[key] = r
    pending = []
    for f in features:
        for core in CORES:
            for host in HOSTS:
                kinds = ["automated"] + (["live"] if f["live"] else [])
                for kind in kinds:
                    key = (f["id"], core, host, kind)
                    r = records.get(key)
                    if r is None or r["status"] not in {"passed", "reviewed-no-go"}:
                        pending.append("/".join(key))
    complete = not pending
    families = {p.name for p in (root / "cores").iterdir() if p.is_dir()}
    if families - set(CORES) and not complete:
        raise ValueError("new core directories forbidden while completion is open: " + ", ".join(sorted(families - set(CORES))))
    return {"schemaVersion": 1, "complete": complete, "sourceSha256": digest,
            "pending": pending, "buildConfigurations": configuration}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--evidence", type=Path)
    parser.add_argument("--header", type=Path)
    parser.add_argument("--source-header", type=Path)
    parser.add_argument("--require-complete", action="store_true")
    parser.add_argument("--summary", action="store_true")
    args = parser.parse_args()
    try:
        result = evaluate(args.root.resolve(), args.evidence)
        if args.source_header:
            args.source_header.parent.mkdir(parents=True, exist_ok=True)
            content = '#pragma once\n#define TIME_NETPLAY_SOURCE_SHA256 "' + result['sourceSha256'] + '"\n'
            if not args.source_header.exists() or args.source_header.read_text() != content:
                args.source_header.write_text(content)
        if args.header:
            args.header.parent.mkdir(parents=True, exist_ok=True)
            content = "#pragma once\nnamespace BMMQ::FeatureAdmission { inline constexpr bool complete = " + str(result["complete"]).lower() + "; }\n"
            if not args.header.exists() or args.header.read_text() != content:
                args.header.write_text(content)
        print(json.dumps({"complete":result["complete"],"pendingCount":len(result["pending"])} if args.summary else result))
        return int(args.require_complete and not result["complete"])
    except (ValueError, KeyError, OSError, TypeError) as error:
        print(json.dumps({"complete": False, "error": str(error)}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
