# Git hooks

Optional local git hooks (pre-commit and pre-push) for faster feedback before CI. Hooks live in `.githooks/` and are
**opt-in per clone**: git never installs them automatically, and nothing in the project requires
them. CI remains the check that gates merges.

## Pre-commit hook

`.githooks/pre-commit` is a quick check of what you are about to commit. It needs no build tree:

- **Formatting:** `git-clang-format --staged` checks only the staged lines of `.h`, `.cpp` and `.mm`
  files under `include/`, `source/` and `test/`, so touching an older file does not demand
  reformatting all of it. Pure renames and the vendored files under `test/RenderingFramework/stb/`
  are skipped. An actual formatting diff blocks the commit, and so does a `git-clang-format` failure
  (the check is never silently skipped when the tool is installed). It is skipped (with a message)
  if `git-clang-format` is not installed. Fix with `git-clang-format --staged` and re-stage.
- **License header:** every newly added `.h`, `.cpp`, `.mm`, `.glslfx` and template (`.h.in`,
  `.cpp.in`) must carry the Apache-2.0 header for the current year (see the
  `openusd-coding-style` skill). Other file types, renamed files, anything under `externals/` and
  the vendored files under `test/RenderingFramework/stb/` are not checked. Both checks are skipped
  when committing a conflicted merge, since the staged changes come from both sides.

## Pre-push hook

`.githooks/pre-push` runs a small local check when you push a branch, so compile breakage and
How-to failures surface before CI:

- **What runs:** configure and build the `debug` preset (the same `build/debug/` tree you use,
  so a debug tree configured with other options is reconfigured), then run the How-to tests
  (`howTo.*`) in a single `hvt_test` process under `build/<preset>/bin/` (or
  `build/<preset>/bin/<Config>/` on multi-config generators). If no `hvt_test` binary is found,
  only the build is checked. The push is **aborted** if no enabled test matches `howTo.*` or the
  test run fails. The hook never uses `ctest` (one process per test is far too slow). The full
  unit suite and the other presets stay CI's job.
- **When:** every `git push` of a branch other than `main`/`master` that sends new commits. Tag
  pushes, branch deletions and refs that are already up to date on the remote are skipped.
- **Only the checked-out, clean commit:** the check builds your working tree, so it validates
  the pushed commit only when `HEAD` is that commit, tracked files have no uncommitted changes,
  and no source or CMake file is untracked (a file listed in a committed `CMakeLists.txt` but
  never added would otherwise be built without being pushed). Submodule content changes are
  ignored (a vcpkg bootstrap can leave `externals/vcpkg` modified), but a submodule checked out
  at a different commit than the recorded one aborts the check. When you push several refs, only the
  one at `HEAD` is checked and the others get a note. A push that cannot be validated (no ref at
  `HEAD`, uncommitted tracked changes, untracked build inputs) is **aborted**, like the
  hvt-extensions copy of this hook; bypass with `--no-verify`.
- **On failure:** configure, build, or How-to test failures abort the push. The build is
  incremental (`build/debug/`), so repeat pushes are fast when
  the check runs.

The How-to tests need a GPU (headless Vulkan on Linux/Windows with the default `debug` preset;
Metal on macOS). A display server is not required for Vulkan on Linux. On a machine with no GPU,
use `git push --no-verify` and rely on CI. See [testing.md](testing.md).

## Setup (once per clone)

```bash
git config core.hooksPath .githooks
git config core.hooksPath        # -> .githooks
```

Enabling the hooks is each developer's own choice. Agents don't enable them or run them on your
behalf; if a hook is active and rejects a commit or push, they fix or report the failure and never
bypass it with `--no-verify` unless you ask.

## Customizing

- **Preset:** `git config hooks.prePushPreset <preset>` (default `debug`).
- **Bypass once:** `git commit --no-verify` or `git push --no-verify`.
- **Disable:** `git config --unset core.hooksPath`.

Local hooks are a convenience, not enforcement: they are opt-in and can be bypassed, so they
never replace CI and branch protection on `main`.

## Windows

The hook works under Git for Windows (hooks run through its bundled bash). `.gitattributes`
forces LF endings for `.githooks/*`, because a CRLF shebang would fail before the script starts.
Push from the x64 Visual Studio developer environment so `cl` and `ninja` are on `PATH`.
