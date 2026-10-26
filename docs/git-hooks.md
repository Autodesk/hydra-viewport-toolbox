# Git hooks

Optional local git hooks for faster feedback before CI. Hooks live in `.githooks/` and are
**opt-in per clone**: git never installs them automatically, and nothing in the project requires
them. CI remains the check that gates merges.

## Pre-push hook

`.githooks/pre-push` runs a small local check when you push a branch, so compile breakage and
How-to failures surface before CI:

- **What runs:** configure and build the `debug` preset, then run the How-to tests (`howTo.*`)
  in a single `hvt_test` process. The full unit suite and the other presets stay CI's job.
- **When:** every `git push` of a branch other than `main`/`master`. Tag pushes and branch
  deletions are skipped.
- **Only the checked-out, clean commit:** the check builds your working tree, so it validates
  the pushed commit only when `HEAD` is that commit and tracked files have no uncommitted
  changes (untracked files are ignored). Otherwise the push goes through and the hook prints
  `NOT VALIDATED locally` with the reason; CI still runs.
- **On failure:** the push is aborted. The build is incremental (`build/debug/`), so repeat
  pushes are fast.

The How-to tests need a GPU/display; on a headless machine skip the hook (`--no-verify`) and
rely on CI.

## Sanitizer check (on demand)

Sanitizers are too slow for every push, so they are not part of the hook. Run them when you want
them (for example before opening a PR that touches memory or pointer handling):

```bash
cmake --workflow --preset verify
```

The `verify` preset builds `build/verify/` with AddressSanitizer and UndefinedBehaviorSanitizer
together (Linux/macOS only) and runs the How-to tests. The full per-sanitizer runs (`asan`,
`ubsan`) are in CI; see [build.md](build.md) for the presets and [testing.md](testing.md) for the
test workflow.

## Setup (once per clone)

```bash
git config core.hooksPath .githooks
git config core.hooksPath        # -> .githooks
```

Enabling the hook is each developer's own choice. Agents don't enable it or run it on your
behalf; if it is active and rejects a push, they fix or report the failure and never bypass it
with `--no-verify` unless you ask.

## Customizing

- **Preset:** change `PRESET` at the top of `.githooks/pre-push`.
- **Bypass once:** `git push --no-verify`.
- **Disable:** `git config --unset core.hooksPath`.

Local hooks are a convenience, not enforcement: they are opt-in and can be bypassed, so they
never replace CI and branch protection on `main`.

## Windows

The hook works under Git for Windows (hooks run through its bundled bash). `.gitattributes`
forces LF endings for `.githooks/*`, because a CRLF shebang would fail before the script starts.
Push from the x64 Visual Studio developer environment so `cl` and `ninja` are on `PATH`. The
`verify` preset is not available on Windows.
