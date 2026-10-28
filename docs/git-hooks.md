# Git hooks

Optional local git hooks (pre-commit and pre-push) for faster feedback before CI. Hooks live in `.githooks/` and are
**opt-in per clone**: git never installs them automatically, and nothing in the project requires
them. CI remains the check that gates merges.

## Pre-commit hook

`.githooks/pre-commit` is a quick check of what you are about to commit. It needs no build tree:

- **Formatting:** `git clang-format --staged` checks only the staged lines of `.h`, `.cpp` and `.mm`
  files under `include/`, `source/` and `test/`, so touching an older file does not demand
  reformatting all of it. Only an actual formatting diff blocks the commit. It is skipped (with a
  message) if `git-clang-format` is not installed. Fix with `git clang-format --staged` and re-stage.
- **License header:** every newly added `.h`, `.cpp`, `.mm`, `.glslfx`, CMake file (`CMakeLists.txt`,
  `.cmake`) and template (`.h.in`, `.cpp.in`, `.cmake.in`, `.rc.in`) must carry the Apache-2.0
  header for the current year (see the `openusd-coding-style` skill). Other file types, renamed
  files and anything under `externals/` are not checked.

## Pre-push hook

`.githooks/pre-push` runs a small local check when you push a branch, so compile breakage and
How-to failures surface before CI:

- **What runs:** configure and build the `debug` preset (the same `build/debug/` tree you use,
  so a debug tree configured with other options is reconfigured), then run the How-to tests
  (`howTo.*`) in a single `hvt_test` process. If no `hvt_test` binary is found, it falls back to
  `ctest`, which is slower because it starts one process per test. The full unit suite and the
  other presets stay CI's job.
- **When:** every `git push` of a branch other than `main`/`master` that sends new commits. Tag
  pushes, branch deletions and refs that are already up to date on the remote are skipped.
- **Only the checked-out, clean commit:** the check builds your working tree, so it validates
  the pushed commit only when `HEAD` is that commit and tracked files have no uncommitted
  changes (untracked files and submodules are ignored). When you push several refs, only the
  one at `HEAD` is checked. Otherwise the push goes through and the hook prints `NOT VALIDATED locally` with the
  reason; CI still runs.
- **On failure:** the push is aborted. The build is incremental (`build/debug/`), so repeat
  pushes are fast.

The How-to tests need a GPU/display; on a headless machine skip the hook (`--no-verify`) and
rely on CI.

## Setup (once per clone)

```bash
git config core.hooksPath .githooks
git config core.hooksPath        # -> .githooks
```

Enabling the hooks is each developer's own choice. Agents don't enable them or run them on your
behalf; if a hook is active and rejects a commit or push, they fix or report the failure and never
bypass it with `--no-verify` unless you ask.

## Customizing

- **Preset:** change `PRESET` at the top of `.githooks/pre-push`.
- **Bypass once:** `git commit --no-verify` or `git push --no-verify`.
- **Disable:** `git config --unset core.hooksPath`.

Local hooks are a convenience, not enforcement: they are opt-in and can be bypassed, so they
never replace CI and branch protection on `main`.

## Windows

The hook works under Git for Windows (hooks run through its bundled bash). `.gitattributes`
forces LF endings for `.githooks/*`, because a CRLF shebang would fail before the script starts.
Push from the x64 Visual Studio developer environment so `cl` and `ninja` are on `PATH`.
