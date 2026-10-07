import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("gate", ROOT / "tools/check_feature_completion.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


def reject(call):
    try:
        call()
    except ValueError:
        return
    raise AssertionError("invalid admission accepted")


with tempfile.TemporaryDirectory(prefix="time-completion-") as directory:
    root = Path(directory)
    matrix = json.loads((ROOT / gate.MATRIX).read_text())
    for f in matrix["features"]:
        for reference in f["references"]:
            p = root / reference
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text("authority\n")
    (root / gate.MATRIX).write_text(json.dumps(matrix))
    (root / "CMakeLists.txt").write_text("build configuration\n")
    (root / "emulator.cpp").write_text("source\n")
    for core in gate.CORES:
        (root / "cores" / core).mkdir(parents=True)
    result = gate.evaluate(root)
    assert not result["complete"] and result["pending"]
    (root / "cores/third").mkdir()
    reject(lambda: gate.evaluate(root))
    (root / "cores/third").rmdir()
    artifact = root / "result.txt"
    artifact.write_text("controlled test evidence\n")
    configuration={host: dict(compiler="test",compilerVersion="1",buildType="test",cmakeCacheSha256="0"*64,cpu=host,configureCommand="configure",buildCommand="build",testCommand="test") for host in gate.HOSTS}
    records = []
    for f in matrix["features"]:
        for core in gate.CORES:
            for host in gate.HOSTS:
                for kind in ["automated"] + (["live"] if f["live"] else []):
                    records.append(dict(feature=f["id"], core=core, host=host, kind=kind,
                        status="passed", buildConfigurationSha256=hashlib.sha256(json.dumps(configuration[host],sort_keys=True,separators=(",", ":")).encode()).hexdigest(), artifact="result.txt", artifactSha256=hashlib.sha256(artifact.read_bytes()).hexdigest()))
    evidence = dict(schemaVersion=1, sourceSha256=gate.source_digest(root),
        buildConfigurations=configuration, records=records)
    path = root / "evidence.json"
    def save():
        path.write_text(json.dumps(evidence))
    save()
    assert gate.evaluate(root, path)["complete"]
    ordinary = next(r for r in records if r["feature"] != "acceleration.native")
    ordinary["status"] = "not-run"
    save()
    assert not gate.evaluate(root, path)["complete"]
    ordinary["status"] = "reviewed-no-go"
    ordinary["reviewer"] = "test reviewer"
    save()
    reject(lambda: gate.evaluate(root, path))
    ordinary["status"] = "passed"
    native = next(r for r in records if r["feature"] == "acceleration.native")
    native["status"] = "reviewed-no-go"
    save()
    reject(lambda: gate.evaluate(root, path))
    native["reviewer"] = "test reviewer"
    save()
    assert gate.evaluate(root, path)["complete"]
    (root / "emulator.cpp").write_text("changed\n")
    reject(lambda: gate.evaluate(root, path))
print("completion admission, missing/stale evidence and reviewed no-go passed")
