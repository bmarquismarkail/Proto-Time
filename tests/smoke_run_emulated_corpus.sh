#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

CORPUS_DIR="$WORK_DIR/corpus"
LOG_DIR="$WORK_DIR/logs"
CALLS="$WORK_DIR/calls.tsv"
mkdir -p "$CORPUS_DIR" "$LOG_DIR"

touch "$CORPUS_DIR/pass.gb"
touch "$CORPUS_DIR/pass.gbc"
touch "$CORPUS_DIR/pass.gg"
touch "$CORPUS_DIR/skip.nes"
touch "$CORPUS_DIR/fail.gb"
touch "$CORPUS_DIR/false-pass.gb"
touch "$CORPUS_DIR/false-fail.gb"
touch "$CORPUS_DIR/memory-map.gb"
printf 'not a rom\n' > "$CORPUS_DIR/readme.txt"
(cd "$CORPUS_DIR" && zip -q unsupported.zip readme.txt)

EMULATOR="$WORK_DIR/timeEmulator"
cat > "$EMULATOR" <<'STUB'
#!/usr/bin/env bash
set -euo pipefail

core=""
rom=""
diag=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --core) core="$2"; shift 2 ;;
        --rom) rom="$2"; shift 2 ;;
        --diagnostics-report) diag="$2"; shift 2 ;;
        *) shift ;;
    esac
done

printf '%s\t%s\n' "$core" "$(basename "$rom")" >> "${PROTO_TIME_TEST_CALLS:?}"
[[ -n "$diag" ]] && printf '{"host_elapsed_ns":123,"emulated_cycles":456,"effective_emulation_speed":1.5,"effective_cycles_per_second":789,"active_timing_profile":"balanced","video":{"frames_submitted":2,"frames_presented":1}}\n' > "$diag"

case "$(basename "$rom")" in
    fail.gb) echo "ordinary failure" >&2; exit 7 ;;
    false-pass.gb) echo "fatal emulator condition" >&2; exit 0 ;;
    false-fail.gb) echo "benign run" >&2; exit 9 ;;
    memory-map.gb) echo "address is not writable" >&2; exit 0 ;;
    *) echo "ok" >&2; exit 0 ;;
esac
STUB
chmod +x "$EMULATOR"

set +e
OUTPUT=$(PROTO_TIME_TEST_CALLS="$CALLS" \
    EMULATOR="$EMULATOR" \
    CORPUS_DIR="$CORPUS_DIR" \
    LOG_DIR="$LOG_DIR" \
    TIMEOUT=5 \
    STEPS=12 \
    "$ROOT_DIR/scripts/run-emulated-corpus.sh" 2>&1)
RC=$?
set -e

if [[ $RC -eq 0 ]]; then
    echo "runner should fail when real failures are present"
    echo "$OUTPUT"
    exit 1
fi

grep -F $'gameboy\tpass.gb' "$CALLS" >/dev/null
grep -F $'gameboy\tpass.gbc' "$CALLS" >/dev/null
grep -F $'gamegear\tpass.gg' "$CALLS" >/dev/null
grep -F $'gameboy\tfail.gb' "$CALLS" >/dev/null
grep -F $'gameboy\tfalse-pass.gb' "$CALLS" >/dev/null
grep -F $'gameboy\tfalse-fail.gb' "$CALLS" >/dev/null
grep -F $'gameboy\tmemory-map.gb' "$CALLS" >/dev/null

if grep -F 'skip.nes' "$CALLS" >/dev/null; then
    echo "unsupported extension was executed"
    cat "$CALLS"
    exit 1
fi

if grep -F 'unsupported.zip' "$CALLS" >/dev/null; then
    echo "zip without supported ROM was executed"
    cat "$CALLS"
    exit 1
fi

grep -F 'SUMMARY: 10 files | Passed: 3 | Unsupported: 3 | Failed: 4' <<<"$OUTPUT" >/dev/null
grep -F 'RESULT: FAILED' <<<"$OUTPUT" >/dev/null
grep -F 'diagnostics=ok host_elapsed_ns=123 emulated_cycles=456 speed=1.5 cycles_per_second=789 timing_profile=balanced frames_submitted=2 frames_presented=1' <<<"$OUTPUT" >/dev/null

if grep -R 'skip.nes\|readme.txt\|unsupported.zip' "$LOG_DIR" >/dev/null; then
    echo "unsupported object produced a log"
    find "$LOG_DIR" -type f -maxdepth 1 -print -exec sed -n '1,8p' {} \;
    exit 1
fi

if ! grep -R 'File: false-pass.gb | Core: gameboy | Exit: 0 | Status: FAIL' "$LOG_DIR" >/dev/null; then
    echo "fatal output was not treated as a failure"
    exit 1
fi

if ! grep -R 'File: false-fail.gb | Core: gameboy | Exit: 9 | Status: FAIL' "$LOG_DIR" >/dev/null; then
    echo "nonzero exit was not treated as a failure"
    exit 1
fi

if ! grep -R 'FAIL: memory_map' "$LOG_DIR" >/dev/null; then
    echo "memory map output was not classified"
    exit 1
fi
