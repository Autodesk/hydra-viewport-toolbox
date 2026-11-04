---
name: validate-branch
description: >-
  Validate the current branch against every CMake preset except the Vulkan ones: configure,
  build, and run all the unit tests through the hvt_test binary directly (not ctest, which is too
  slow), with the ASan/UBSan runtime options applied. Use before opening or updating a PR, after a
  change that could behave differently across configurations (Release, static libraries,
  sanitizers), or when asked to "check all presets", "run asan/ubsan", or "validate the branch".
---

# Validate the branch against all presets

CI builds the `debug`, `release`, `relwithdebinfo`, `ubsan` and `asan` presets. This skill runs the
same matrix locally, plus `debugstatic`, with one command and one summary.

## Run it

```bash
.claude/skills/validate-branch/validate-branch.sh
```

For each preset the script runs, in order: `cmake --preset`, `cmake --build --preset`, then
`hvt_test` from `build/<preset>/bin/`. The tests of a preset run in one process, which takes
seconds, where `ctest` starts one process per test and takes minutes.

| Option | Effect |
|--------|--------|
| `--presets a,b,c` | Validate only these presets, for example `--presets debug,asan` |
| `--filter EXPR` | gtest filter, for example `--filter '*Bloom*'` or `--filter 'howTo.*'` |
| `--build-only` | Configure and build, skip the tests (headless machines) |
| `--fail-fast` | Stop at the first failing preset instead of running them all |
| `--jobs N` | Build parallelism, passed to `cmake --build -j` |
| `--log-dir DIR` | One log per preset, default `build/validate-branch/<preset>.log` |
| `--list` | Show the selected presets and their sanitizer environment, then exit |

The exit status is 0 only if every selected preset configured, built and passed. A summary table
closes the run, and failures print the matching error lines plus the tail of the log.

## Platforms

The same script runs on macOS, Linux and Windows. It needs `bash` 3.2 or newer, `git` and `cmake`,
and nothing else: the sanitizer environment is read by `preset-env.cmake` (CMake parses the preset
JSON), so there is no Python or `jq` dependency.

| Host | How to run it |
|------|---------------|
| macOS | From any terminal. The system bash 3.2 is enough. |
| Linux | From any terminal, with a display for the rendering tests (`DISPLAY`, or Xvfb as CI does). |
| Windows | From **Git Bash**, started from the x64 Visual Studio developer environment (for example the "x64 Native Tools Command Prompt", then `bash`), because the presets use Ninja and need `cl` in `PATH`. The script notes it when `cl` is missing. |

Portability details that matter if you edit the script:

- Native `cmake.exe` and `hvt_test.exe` can write CRLF line endings under Git Bash. Anything
  parsed goes through `strip_cr`, and `.gitattributes` keeps the `.sh` file on LF so bash accepts
  it after a Windows checkout.
- The test binary is found at `build/<preset>/bin/hvt_test`, `hvt_test.exe`, or under
  `bin/<Config>/` for multi-config generators.
- Avoid bash 4 features (`mapfile`, associative arrays, `${var,,}`), process substitution, and
  GNU-only options of `sed`, `grep` or `date`, which the macOS tools do not have.
- The `crashed` label relies on POSIX exit statuses above 128. Windows reports crashes
  differently, but the sanitizer presets do not exist there.

## Which presets run

Every preset from `cmake --list-presets=configure` except the names containing `vulkan`:
`debug`, `debugstatic`, `release`, `relwithdebinfo`, `ubsan`, `asan`. CMake does not list a preset
whose condition is false on the host, so `ubsan` and `asan` drop out on Windows by themselves.
Naming a Vulkan preset with `--presets` is an error.

## Sanitizer environment variables

`ubsan` and `asan` define their runtime options in the `environment` of `CMakePresets.json`:

| Preset | Variable | Value |
|--------|----------|-------|
| `asan` | `ASAN_OPTIONS` | `halt_on_error=1:abort_on_error=1:print_stacktrace=1` |
| `ubsan` | `UBSAN_OPTIONS` | `halt_on_error=1:abort_on_error=1:print_stacktrace=1` |

`cmake --preset` and `ctest --preset` apply them. **Running `hvt_test` by hand does not**, so a
sanitizer run started from the shell would use the sanitizer defaults and not the CI behavior.
The script therefore reads the environment from the presets with `preset-env.cmake` (following
`inherits`, so a future change to the presets is picked up) and runs the binary with it. It prints the variables it
uses at the start of each test step. `--list` shows them without building.

To override an option for one run, set the variable in your shell. It is appended after the preset
value, and the sanitizer takes the last occurrence of a key:

```bash
ASAN_OPTIONS=detect_leaks=0 .claude/skills/validate-branch/validate-branch.sh --presets asan
```

If you run the binary yourself, export the same variables first:

```bash
ASAN_OPTIONS=halt_on_error=1:abort_on_error=1:print_stacktrace=1 build/asan/bin/hvt_test --gtest_filter='TestOutlineTasks.*'
```

Reading a failure:

- **`crashed` (exit status above 128).** `asan` aborts with `abort_on_error=1`, so the report and
  stack trace are in the log. `ubsan` is built with `-fsanitize-undefined-trap-on-error`, so it
  traps without a message. Rerun the failing test under a debugger or with the `asan` preset to
  find the cause.
- **`FAILED` in build.** Read the first `error:` line in the log. `-Werror` is on, so warnings
  that differ between configurations (for example an unused variable in Release only) fail here.
- **`FAILED` in test.** The log lists the failing `[  FAILED  ]` tests. Reproduce one with
  `build/<preset>/bin/hvt_test --gtest_filter=<Suite.Name>`, setting the sanitizer variables for
  `asan` and `ubsan`.
- **"no test matched the filter".** A filter that matches nothing runs 0 tests and exits 0 in
  gtest. The script fails instead. Check suite names with `--gtest_list_tests`.

## Things to know before running

- **First run per preset is slow.** Each preset has its own `build/<preset>/` tree with its own
  vcpkg install, so a first configure can take a long time and a lot of disk. Later runs are
  incremental. Use `--presets` to validate a subset while iterating, and run the full matrix
  once before pushing.
- **Tests need a GPU or display** (Metal on macOS, OpenGL with SDL2 on Linux and Windows). On a
  headless machine use `--build-only`; the test step reports it clearly if the test list cannot be
  produced.
- **Image baselines are per platform.** A baseline that exists only for macOS fails on other
  platforms. A failure in `release` or a sanitizer preset that does not appear in `debug` can
  be a rendering difference. Compare the computed image under `build/<preset>/computed/` before
  assuming a code bug.
- **LeakSanitizer** is not supported on macOS arm64, so `asan` there checks memory errors only.
  On Linux the leak check is on by default, and leaks inside GPU drivers can show up. Silence them
  with `ASAN_OPTIONS=detect_leaks=0` only if the leaks are not from HVT code, and say so.
- **It validates the working tree, not the commits.** The script notes uncommitted changes and
  untracked files under `include`, `source`, `test` and `cmake`. Commit or add them first if the
  result must describe what gets pushed.
- **Only the host compiler runs.** GCC and MSVC builds are not covered. CI does that.
- **Do not enable the git hooks or change `core.hooksPath` for this.** The optional pre-push hook
  checks a single preset. This skill is the broader, on-demand check.

## When a preset fails

1. Fix the cause. Do not weaken the test, skip the preset, or add a sanitizer suppression to get
   a green run.
2. Rerun only that preset: `--presets <name>`, adding `--filter` for the failing test.
3. Once it passes, rerun the full matrix, since a fix for one configuration can break another.
4. Report to the user which presets ran, which were skipped and why, and the final summary.
