#!/usr/bin/env bash
#
# Validate the working tree against the CMake presets: configure, build, then run the unit tests
# by calling the hvt_test binary directly (ctest starts one process per test, which is far too
# slow for a validation loop).
#
# By default every preset reported by `cmake --list-presets=configure` is validated, except the
# Vulkan ones. Presets whose condition is false on this host (e.g. asan/ubsan on Windows) are not
# listed by CMake, so they are skipped automatically.
#
# Sanitizer presets (asan, ubsan) define their runtime options in the `environment` of
# CMakePresets.json. `cmake`/`ctest --preset` apply them, but running the binary directly does
# not, so this script reads them from the presets and exports them to the test process. Options
# already set in the caller's environment are appended last, so they win on duplicate keys.
#
# Portability: bash 3.2 or newer (macOS default), Linux, and Windows through the bash bundled with
# Git for Windows (run it from Git Bash, inside the x64 Visual Studio developer environment).
# The only tools used are git, cmake, and the POSIX basics (sed, grep, tr, env). No python or jq.
#
# Usage: validate-branch.sh [options]   (see --help)

set -uo pipefail

usage() {
    cat <<'EOF'
Usage: validate-branch.sh [options]

  --presets a,b,c   Presets to validate (default: all listed presets except Vulkan ones)
  --filter EXPR     gtest filter passed to hvt_test (default: all tests)
  --build-only      Configure and build, but do not run the tests
  --fail-fast       Stop after the first preset that fails (default: run every preset)
  --jobs N          Parallel build jobs, passed to `cmake --build -j`
  --log-dir DIR     Where to write one log per preset (default: build/validate-branch)
  --list            Print the selected presets with their sanitizer environment, then exit
  -h, --help        Show this help

Exit status: 0 when every selected preset configured, built and passed; 1 otherwise.
EOF
}

PRESETS_ARG=""
FILTER="*"
BUILD_ONLY=0
FAIL_FAST=0
JOBS=""
LOG_DIR=""
LIST=0

need_value() {
    if [ "$2" -lt 2 ]; then
        echo "validate-branch: $1 needs a value" >&2
        exit 2
    fi
}

while [ $# -gt 0 ]; do
    case "$1" in
        --presets)    need_value "$1" $#; PRESETS_ARG=$2; shift 2 ;;
        --filter)     need_value "$1" $#; FILTER=$2; shift 2 ;;
        --jobs)       need_value "$1" $#; JOBS=$2; shift 2 ;;
        --log-dir)    need_value "$1" $#; LOG_DIR=$2; shift 2 ;;
        --build-only) BUILD_ONLY=1; shift ;;
        --fail-fast)  FAIL_FAST=1; shift ;;
        --list)       LIST=1; shift ;;
        -h|--help)    usage; exit 0 ;;
        *) echo "validate-branch: unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

# Presets, logs and build trees are relative to the repository root.
cd "$(git rev-parse --show-toplevel)" || exit 1
[ -n "$LOG_DIR" ] || LOG_DIR="build/validate-branch"

PRESET_ENV_SCRIPT=".claude/skills/validate-branch/preset-env.cmake"

HOST=$(uname -s)
case "$HOST" in
    MINGW*|MSYS*|CYGWIN*) IS_WINDOWS=1 ;;
    *) IS_WINDOWS=0 ;;
esac

# Native Windows programs write CRLF line endings through the bash of Git for Windows. Every
# output of cmake or hvt_test that is parsed below goes through this to drop the carriage returns.
strip_cr() { tr -d '\r'; }

for tool in git cmake; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "validate-branch: '$tool' not found in PATH" >&2
        exit 1
    fi
done

# Non-Vulkan configure presets available on this host, one per line.
available_presets() {
    cmake --list-presets=configure | strip_cr | sed -n 's/^ *"\([^"]*\)".*/\1/p' | grep -vi vulkan
}

# KEY=VALUE lines for the `environment` of a configure preset, including inherited ones. CMake
# itself reads the JSON, so nothing beyond cmake is needed on any platform.
preset_env() {
    [ -f "$PRESET_ENV_SCRIPT" ] || return 2
    cmake -DPRESETS_FILE=CMakePresets.json -DPRESET="$1" -P "$PRESET_ENV_SCRIPT" | strip_cr
    [ "${PIPESTATUS[0]}" -eq 0 ]
}

# Fill TEST_ENV with the KEY=VALUE pairs to run the tests of a preset with. Returns non-zero
# when the preset environment cannot be read, because a sanitizer run without its options is
# not the run the preset describes.
build_test_env() {
    local preset=$1 lines kv key val caller
    TEST_ENV=()
    if ! lines=$(preset_env "$preset"); then
        return 1
    fi
    while IFS= read -r kv; do
        [ -n "$kv" ] || continue
        key=${kv%%=*}
        val=${kv#*=}
        caller=${!key:-}
        if [ -n "$caller" ]; then
            val="$val:$caller"
        fi
        TEST_ENV+=("$key=$val")
    done <<EOF
$lines
EOF
}

# The hvt_test of a preset: bin/hvt_test with a single-config generator, which wins when it
# exists so a stray binary in a subdirectory is never picked. With a multi-config generator it is
# bin/<Config>/hvt_test, and the most recently built configuration is used.
find_bin() {
    local preset=$1 bin="" candidate
    for candidate in "build/$preset/bin/hvt_test" "build/$preset/bin/hvt_test.exe"; do
        if [ -x "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    for candidate in "build/$preset"/bin/*/hvt_test "build/$preset"/bin/*/hvt_test.exe; do
        [ -x "$candidate" ] || continue
        if [ -z "$bin" ] || [ "$candidate" -nt "$bin" ]; then
            bin=$candidate
        fi
    done
    [ -n "$bin" ] || return 1
    printf '%s\n' "$bin"
}

# Run every test of the preset binary in one process, with the preset environment.
run_tests() {
    local preset=$1 bin listing count
    if ! build_test_env "$preset"; then
        echo "validate-branch: cannot read the environment of preset '$preset' ($PRESET_ENV_SCRIPT)" >&2
        return 1
    fi
    if ! bin=$(find_bin "$preset"); then
        echo "validate-branch: hvt_test not found under build/$preset/bin" >&2
        return 1
    fi
    echo "validate-branch: $bin (filter: $FILTER)"
    if [ "${#TEST_ENV[@]}" -gt 0 ]; then
        printf 'validate-branch: env %s\n' "${TEST_ENV[@]}"
    fi

    # A filter that matches nothing runs 0 tests and still exits 0: fail instead of a false pass.
    # Disabled tests do not run, so they do not count as a match.
    if ! listing=$(env ${TEST_ENV[@]+"${TEST_ENV[@]}"} "$bin" --gtest_list_tests --gtest_filter="$FILTER" | strip_cr; [ "${PIPESTATUS[0]}" -eq 0 ]); then
        echo "validate-branch: could not list the tests (do they need a GPU/display?)" >&2
        return 1
    fi
    # A suite header is an unindented line, its tests are indented. Disabled suites
    # (DISABLED_Suite.) and disabled tests (DISABLED_Test) do not run, so they do not count.
    count=$(printf '%s\n' "$listing" | awk '
        /^[^ ]/ { suite = $1; next }
        /^  [^ ]/ { if (suite !~ /^DISABLED_/ && $1 !~ /^DISABLED_/) n++ }
        END { print n + 0 }')
    if [ "$count" -eq 0 ]; then
        echo "validate-branch: no test matched the filter '$FILTER'" >&2
        return 1
    fi

    env ${TEST_ENV[@]+"${TEST_ENV[@]}"} "$bin" --gtest_brief=1 --gtest_filter="$FILTER"
}

# True when $1 is one of the available presets. A loop, not `printf | grep -q`: with pipefail,
# grep -q exiting early can make printf die on SIGPIPE and fail a valid match.
is_available() {
    local candidate
    for candidate in $AVAILABLE; do
        [ "$candidate" = "$1" ] && return 0
    done
    return 1
}

# Select the presets.
AVAILABLE=$(available_presets)
SELECTED=()
if [ -n "$PRESETS_ARG" ]; then
    for name in $(printf '%s' "$PRESETS_ARG" | tr ',' ' '); do
        case "$name" in
            *[Vv]ulkan*)
                echo "validate-branch: Vulkan presets are excluded: $name" >&2
                exit 2 ;;
        esac
        if ! is_available "$name"; then
            echo "validate-branch: preset '$name' is not available on this host." \
                "Available: $(printf '%s ' $AVAILABLE)" >&2
            exit 2
        fi
        SELECTED+=("$name")
    done
else
    while IFS= read -r name; do
        [ -n "$name" ] && SELECTED+=("$name")
    done <<EOF
$AVAILABLE
EOF
fi
if [ "${#SELECTED[@]}" -eq 0 ]; then
    echo "validate-branch: no preset to validate" >&2
    exit 2
fi

if [ "$LIST" -eq 1 ]; then
    for preset in "${SELECTED[@]}"; do
        echo "$preset"
        if build_test_env "$preset"; then
            [ "${#TEST_ENV[@]}" -eq 0 ] || printf '    %s\n' "${TEST_ENV[@]}"
        else
            echo "    (environment unreadable: see $PRESET_ENV_SCRIPT)"
        fi
    done
    exit 0
fi

mkdir -p "$LOG_DIR"

# The build uses the working tree, not the commits: say so when they differ.
if [ -n "$(git status --porcelain --untracked-files=no --ignore-submodules=dirty)" ]; then
    echo "note: uncommitted changes to tracked files are part of this validation"
fi
untracked=$(git ls-files --others --exclude-standard | grep -v '\(^\|/\)\.DS_Store$')
if [ -n "$untracked" ]; then
    echo "note: untracked files are in the working tree and may be built (add or remove them to"
    echo "      validate exactly what gets pushed):"
    printf '%s\n' "$untracked" | head -n 10 | sed 's/^/  /'
    if [ "$(printf '%s\n' "$untracked" | wc -l)" -gt 10 ]; then
        echo "  ..."
    fi
fi
# Host prerequisites the presets do not provide. Notes only: other setups can still work.
if [ "$IS_WINDOWS" -eq 1 ] && ! command -v cl >/dev/null 2>&1; then
    echo "note: 'cl' is not in PATH. The presets use Ninja and need the x64 Visual Studio developer"
    echo "      environment: start Git Bash from the 'x64 Native Tools Command Prompt' (docs/build.md)."
fi
if [ "$HOST" = "Linux" ] && [ "$BUILD_ONLY" -eq 0 ] && [ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
    echo "note: no DISPLAY or WAYLAND_DISPLAY: the rendering tests need a display (CI uses Xvfb or"
    echo "      X on :99). Use --build-only on a headless machine."
fi
echo "Presets: ${SELECTED[*]}"
echo "Logs:    $LOG_DIR/<preset>.log"
echo

# Run one step with its output appended to the preset log. Sets STEP_STATUS to ok, FAILED or
# crashed (exit status above 128 means the process died on a signal: a sanitizer abort or trap).
STEP_STATUS=""
step() {
    local preset=$1 label=$2 log=$3 rc started
    shift 3
    started=$SECONDS
    {
        echo "=== $label ($(date '+%H:%M:%S')) ==="
        echo "+ $*"
    } >>"$log"
    "$@" >>"$log" 2>&1
    rc=$?
    if [ "$rc" -eq 0 ]; then
        STEP_STATUS="ok"
    elif [ "$rc" -gt 128 ]; then
        STEP_STATUS="crashed"
    else
        STEP_STATUS="FAILED"
    fi
    printf '  %-9s %-8s %4ss\n' "$label" "$STEP_STATUS" "$((SECONDS - started))"
    if [ "$rc" -ne 0 ]; then
        echo "  exit status $rc, log: $log"
        # Build and test failures first, then the end of the log for context.
        grep -E 'error:|FAILED|\[  FAILED  \]|ERROR: (Address|Leak)Sanitizer|runtime error:' "$log" \
            | tail -n 15 | sed 's/^/    /'
        echo "    ..."
        tail -n 8 "$log" | sed 's/^/    /'
        if [ "$rc" -gt 128 ] && [ "$label" = "test" ]; then
            case "$preset" in
                ubsan) echo "    ubsan builds trap on error and print no message: rerun the failing" \
                           "test under a debugger, or with the asan/debug preset." ;;
                asan)  echo "    the sanitizer report and stack trace are in the log above." ;;
            esac
        fi
    fi
    return "$rc"
}

RESULTS=()
overall=0
for preset in "${SELECTED[@]}"; do
    log="$LOG_DIR/$preset.log"
    : >"$log"
    echo "== $preset"
    started=$SECONDS
    cfg="-" bld="-" tst="-"

    step "$preset" configure "$log" cmake --preset "$preset"
    cfg=$STEP_STATUS
    if [ "$cfg" = "ok" ]; then
        build_cmd=(cmake --build --preset "$preset")
        [ -z "$JOBS" ] || build_cmd+=(-j "$JOBS")
        step "$preset" build "$log" "${build_cmd[@]}"
        bld=$STEP_STATUS
    fi
    if [ "$bld" = "ok" ]; then
        if [ "$BUILD_ONLY" -eq 1 ]; then
            tst="skipped"
        else
            step "$preset" test "$log" run_tests "$preset"
            tst=$STEP_STATUS
        fi
    fi

    if [ "$cfg" != "ok" ] || [ "$bld" != "ok" ] || { [ "$tst" != "ok" ] && [ "$tst" != "skipped" ]; }; then
        overall=1
    fi
    RESULTS+=("$preset|$cfg|$bld|$tst|$((SECONDS - started))")
    echo
    if [ "$overall" -ne 0 ] && [ "$FAIL_FAST" -eq 1 ]; then
        echo "stopping after the first failure (--fail-fast)"
        echo
        break
    fi
done

echo "Summary"
printf '  %-16s %-10s %-10s %-10s %s\n' preset configure build test time
for row in "${RESULTS[@]}"; do
    IFS='|' read -r p c b t s <<EOF
$row
EOF
    printf '  %-16s %-10s %-10s %-10s %ss\n' "$p" "$c" "$b" "$t" "$s"
done
if [ "$overall" -eq 0 ]; then
    echo "validate-branch: all presets passed"
else
    echo "validate-branch: FAILED (see the logs in $LOG_DIR)"
fi
exit "$overall"
