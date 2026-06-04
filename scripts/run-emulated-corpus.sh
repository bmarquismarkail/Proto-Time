#!/bin/bash
# Proto-Time Emulated Corpus Runner
# Runs all ROMs/binary through the emulator to detect crashes, hangs, errors
# Note: .zip files are extracted before running (emulator doesn't support archives)

# Configuration
EMULATOR="${EMULATOR:-/data/src/Proto-Time/build-working/timeEmulator}"
CORPUS_DIR="${CORPUS_DIR:-/data/roms}"
LOG_DIR="${LOG_DIR:-/data/src/Proto-Time/logs/emulated-corpus}"
TIMEOUT="${TIMEOUT:-60}"
STEPS="${STEPS:-100000}"
DIAGNOSTICS_INTERVAL_MS="${DIAGNOSTICS_INTERVAL_MS:-1000}"

# Validate
[[ -x "$EMULATOR" ]] || { echo "ERROR: Emulator not found: $EMULATOR" >&2; exit 1; }
[[ -d "$CORPUS_DIR" ]] || { echo "ERROR: Corpus dir not found: $CORPUS_DIR" >&2; exit 1; }
mkdir -p "$LOG_DIR"

json_value() {
    local json="$1"
    local key="$2"
    local value

    value=$(printf '%s\n' "$json" | sed -n "s/.*\"$key\":\\([^,}]*\\).*/\\1/p" | head -1)
    value="${value#\"}"
    value="${value%\"}"
    printf '%s' "$value"
}

diagnostics_summary() {
    local diag="$1"
    if [[ ! -s "$diag" ]]; then
        echo "diagnostics=missing"
        return
    fi

    local sample
    sample=$(tail -n 1 "$diag")
    if [[ "$sample" != \{* ]]; then
        echo "diagnostics=unparseable"
        return
    fi

    local elapsed cycles speed cps profile frames_presented frames_submitted
    elapsed=$(json_value "$sample" "host_elapsed_ns")
    cycles=$(json_value "$sample" "emulated_cycles")
    speed=$(json_value "$sample" "effective_emulation_speed")
    cps=$(json_value "$sample" "effective_cycles_per_second")
    profile=$(json_value "$sample" "active_timing_profile")
    frames_submitted=$(json_value "$sample" "frames_submitted")
    frames_presented=$(json_value "$sample" "frames_presented")

    echo "diagnostics=ok host_elapsed_ns=${elapsed:-unknown} emulated_cycles=${cycles:-unknown} speed=${speed:-unknown} cycles_per_second=${cps:-unknown} timing_profile=${profile:-unknown} frames_submitted=${frames_submitted:-unknown} frames_presented=${frames_presented:-unknown}"
}

failure_reason_from_output() {
    local rc="$1"
    local output="$2"
    local reason=""

    if [[ $rc -eq 124 ]]; then
        reason="timeout"
    elif [[ $rc -ne 0 ]]; then
        reason="exit=$rc"
    fi

    if grep -qiE "segmentation fault|segfault|abort|core dumped" "$output" 2>/dev/null; then
        reason="${reason:+$reason, }crash"
    fi
    if grep -qiE "assertion" "$output" 2>/dev/null; then
        reason="${reason:+$reason, }assertion"
    fi
    if grep -qiE "exception|terminate called" "$output" 2>/dev/null; then
        reason="${reason:+$reason, }exception"
    fi
    if grep -qiE "fatal" "$output" 2>/dev/null; then
        reason="${reason:+$reason, }fatal"
    fi
    if grep -qiE "unable to open file" "$output" 2>/dev/null; then
        reason="${reason:+$reason, }file_open"
    fi
    if grep -qiE "address is not (readable|writable|mapped)|extends past mapped range" "$output" 2>/dev/null; then
        reason="${reason:+$reason, }memory_map"
    fi
    if grep -qiE "unimplemented opcode" "$output" 2>/dev/null; then
        reason="${reason:+$reason, }unimplemented_opcode"
    fi

    printf '%s' "$reason"
}

# Function to extract ROM from zip and return the extracted ROM path
extract_zip() {
    local zip="$1"
    local target_rom="$2"
    
    # Clean up any previous temp dirs
    rm -rf /tmp/emulator-corpus-* 2>/dev/null
    
    local tmpdir=$(mktemp -d "/tmp/emulator-corpus-XXXXXX")
    if [[ -z "$tmpdir" || ! -d "$tmpdir" ]]; then
        echo "Failed to create temp directory" >&2
        return 1
    fi
    
    if unzip -o -q "$zip" -d "$tmpdir" 2>/dev/null; then
        # Find the specific ROM file inside the zip
        local target_path="$tmpdir/$target_rom"
        if [[ -f "$target_path" ]]; then
            echo "$target_path"
            return 0
        fi
        
        # If exact path not found, try to find a matching filename
        local found=""
        for ext in gb gbc gg; do
            local pattern="$tmpdir/${target_rom%.*}.*$ext"
            if [[ -f "$pattern" ]]; then
                found="$pattern"
                break
            fi
        done
        
        if [[ -n "$found" && -f "$found" ]]; then
            echo "$found"
            return 0
        fi
        
        echo "No ROM file found matching '$target_rom'" >&2
        rm -rf "$tmpdir"
        return 1
    fi
    
    echo "Failed to unzip" >&2
    rm -rf "$tmpdir"
    return 1
}

# Find corpus files. Unsupported extensions are counted and skipped below.
mapfile -d '' ROMS < <(find "$CORPUS_DIR" -maxdepth 3 -type f -print0 2>/dev/null)

[[ ${#ROMS[@]} -gt 0 ]] || { echo "ERROR: No ROM files found" >&2; exit 1; }

echo "=========================================="
echo "PROTO-TIME EMULATED CORPUS RUNNER"
echo "=========================================="
echo "Emulator: $EMULATOR"
echo "Corpus: $CORPUS_DIR"
echo "Logs: $LOG_DIR"
echo "Timeout: ${TIMEOUT}s, Steps: $STEPS"
echo "=========================================="
echo ""

TOTAL=0
PASSED=0
UNSUPPORTED=0
FAILED=0

for ROM in "${ROMS[@]}"; do
    [[ -f "$ROM" ]] || continue
    TOTAL=$((TOTAL + 1))
    NAME=$(basename "$ROM")
    EXT="${NAME##*.}"
    
    # Create short, unique log filename using hash
    HASH=$(printf '%s' "$ROM" | md5sum | cut -d' ' -f1)
    TS=$(date +%Y%m%d_%H%M%S)
    LOG="$LOG_DIR/${HASH}_${TS}.log"
    LOGERR="$LOG.err"
    ROMFILE="$ROM"

    # Determine core from file extension
    case "$EXT" in
        gb|gbc)
            CORE="gameboy"
            ;;
        gg)
            CORE="gamegear"
            ;;
        zip)
            # Will be extracted, extension determined from contents
            CORE="gameboy"  # default, will be overridden if possible
            ;;
        *)
            echo "[$TOTAL/$TOTAL] $NAME (EXT: $EXT) - UNSUPPORTED"
            UNSUPPORTED=$((UNSUPPORTED + 1))
            continue
            ;;
    esac
    
    echo "[$TOTAL/$TOTAL] $NAME (core: $CORE)"
    
    # Handle .zip files - extract the ROM first
    if [[ "$EXT" == "zip" ]]; then
        # Get the ROM name from the zip contents (handle spaces in filenames)
        # Find line with ROM extension and extract everything from column 4 onwards
        ROM_IN_ZIP=$(unzip -l "$ROM" 2>/dev/null | awk '/\.gb$/ || /\.gbc$/ || /\.gg$/ {for(i=4;i<=NF;i++) printf $i" "; print ""}' | head -1 | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')
        
        if [[ -z "$ROM_IN_ZIP" ]]; then
            echo "  UNSUPPORTED: Could not find supported ROM inside zip archive"
            UNSUPPORTED=$((UNSUPPORTED + 1))
            continue
        fi
        
        # Check if extracted ROM has supported extension
        EXTRACTED_EXT="${ROM_IN_ZIP##*.}"
        if [[ "$EXTRACTED_EXT" != "gb" && "$EXTRACTED_EXT" != "gbc" && "$EXTRACTED_EXT" != "gg" ]]; then
            echo "  UNSUPPORTED: Extracted ROM has unsupported extension: $EXTRACTED_EXT"
            UNSUPPORTED=$((UNSUPPORTED + 1))
            continue
        fi
        
        # Determine core from extracted ROM extension
        case "$EXTRACTED_EXT" in
            gb|gbc)
                CORE="gameboy"
                ;;
            gg)
                CORE="gamegear"
                ;;
        esac
        
        # Extract the ROM
        EXTRACTED_ROM=$(extract_zip "$ROM" "$ROM_IN_ZIP")
        if [[ -z "$EXTRACTED_ROM" ]]; then
            echo "  FAILED: Could not extract ROM from zip"
            {
                echo "=== CORPUS RUN LOG ==="
                echo "File: $NAME | Status: FAIL"
                echo "FAIL: Extraction failed"
                echo ""
                echo "ZIP CONTENTS:"
                unzip -l "$ROM" 2>/dev/null | head -20
            } > "$LOG"
            rm -f "$LOG.err" "$LOG.diag" 2>/dev/null
            FAILED=$((FAILED + 1))
            continue
        fi
        
        ROMFILE="$EXTRACTED_ROM"
        echo "  Extracted: $ROM_IN_ZIP (core: $CORE)"
    fi
    
    # Run emulator (capture both stdout and stderr for error detection)
    if timeout "$TIMEOUT" "$EMULATOR" --core "$CORE" --rom "$ROMFILE" --steps "$STEPS" \
        --headless --audio-backend dummy --no-audio --unthrottled \
        --diagnostics-report "$LOG.diag" --diagnostics-interval-ms "$DIAGNOSTICS_INTERVAL_MS" > "$LOGERR" 2>&1; then
        RC=0
    else
        RC=$?
    fi
    
    # Check for failures
    STATUS="PASS"
    FAIL="$(failure_reason_from_output "$RC" "$LOGERR")"

    if [[ -n "$FAIL" ]]; then
        STATUS="FAIL"
    fi

    DIAG_SUMMARY="$(diagnostics_summary "$LOG.diag")"
    
    # Clean up extracted ROM if we extracted one
    if [[ "$EXT" == "zip" && -f "$ROMFILE" ]]; then
        rm -f "$ROMFILE" 2>/dev/null
    fi
    
    # Write log
    {
        echo "=== CORPUS RUN LOG ==="
        echo "File: $NAME | Core: $CORE | Exit: $RC | Status: $STATUS"
        echo "FAIL: ${FAIL:-none}"
        echo "ROM Source Path: $ROM"
        echo "ROM Path: $ROMFILE"
        echo "ROM Bytes: $(stat -c '%s' "$ROMFILE" 2>/dev/null || echo unknown)"
        echo "Steps Requested: $STEPS"
        echo "Timeout Seconds: $TIMEOUT"
        echo "Diagnostics Summary: $DIAG_SUMMARY"
        echo "----------------------------------------"
        echo "STDOUT/STDERR:"
        cat "$LOGERR"
        echo ""
        echo "DIAGNOSTICS:"
        if [[ -f "$LOG.diag" ]]; then
            cat "$LOG.diag"
        else
            echo "(none)"
        fi
    } > "$LOG"
    
    # Clean up temp files
    rm -f "$LOG.err" "$LOG.diag" 2>/dev/null
    
    if [[ "$STATUS" == "PASS" ]]; then
        PASSED=$((PASSED + 1))
    else
        FAILED=$((FAILED + 1))
        echo "  FAILED: $FAIL"
        echo "  $DIAG_SUMMARY"
        echo "  Log: $LOG"
    fi
done

echo ""
echo "=========================================="
echo "SUMMARY: $TOTAL files | Passed: $PASSED | Unsupported: $UNSUPPORTED | Failed: $FAILED"
echo "=========================================="

# Show breakdown if there are failed items
if [[ $FAILED -gt 0 ]]; then
    echo "Log files: $(ls "$LOG_DIR"/*.log 2>/dev/null | wc -l)"
    ls "$LOG_DIR"/*.log 2>/dev/null | head -20
fi
echo "=========================================="

if [[ $FAILED -gt 0 ]]; then
    echo "RESULT: FAILED"
    echo "=========================================="
    exit 1
elif [[ $PASSED -gt 0 ]]; then
    echo "RESULT: PASSED"
    echo "=========================================="
    exit 0
else
    echo "RESULT: NO RUNNABLE FILES (all unsupported)"
    echo "=========================================="
    exit 0
fi
