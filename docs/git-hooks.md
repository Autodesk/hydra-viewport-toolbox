# Git Hooks

Optional local git hooks for faster feedback before CI. Hooks live in `.githooks/` and are
**opt-in per clone** — git never installs them automatically, and nothing in the project requires
them. CI remains the check that gates merges.

## Pre-push build hook

`.githooks/pre-push` runs a local build check when you push a branch, so compile breakage is
caught before it reaches CI:

- **What runs:** configure + build for a lean preset set — `debug` (build-only), `asan` and
  `ubsan` (build **plus How-to tests** — `howTo.*` in `hvt_test` — so memory errors,
  undefined behavior, and integration smoke failures surface before CI). Those tests run as a
  single `hvt_test` process (one GPU/engine startup) rather than through `ctest`, which spawns
  one process per test case — far faster on macOS. The full unit suite remains CI's job.
- **When:** every `git push` to a non-`main` branch — new branches and new commits on existing
  PR branches alike. Pushes to `main`/`master`, tag pushes, and branch deletions are skipped.
- **On failure:** the push is aborted with the failing preset and step named.

The builds are incremental (each preset keeps its own `build/<preset>/` tree), so the hook is
fast after the first run when your checkout matches the commit being pushed. If you push while
on another branch or with uncommitted edits, the hook checks out that commit in a temporary
worktree under `.git/` so it always builds what will be pushed, not your dirty working tree.
That worktree has no submodules, so the hook points `VCPKG_ROOT` at your checkout's
`externals/vcpkg` (unless you already set it); its `build/<preset>/` trees start empty, so
expect a full rebuild of HVT (vcpkg dependencies come from the binary cache). Push from the
branch being pushed, with a clean tree, to keep builds incremental.
Tests run for the sanitizer presets only — they need a GPU/display — so `debug` stays
build-only; see [testing.md](testing.md) for the test workflow and CI for full validation.

## Setup (once per clone)

```bash
git config core.hooksPath .githooks
```

That's all — git now runs every executable script in `.githooks/` matching a hook name
(`pre-push`, `pre-commit`, …). Verify with:

```bash
git config core.hooksPath        # -> .githooks
```

Enabling the hook is each developer's own choice. Agents don't enable it or run it on your
behalf; if it is active and rejects a push, they fix or report the failure and never bypass it
with `--no-verify` unless you ask.

## Customizing

- **Presets:** edit the `PRESETS` array at the top of `.githooks/pre-push` (e.g. add `release`,
  `debugstatic`). Add a preset name to `TEST_PRESETS` to also run How-to tests there, or change
  `TEST_GTEST_FILTER` to widen or narrow the subset. Preset definitions live in
  [CMakePresets.json](../CMakePresets.json) — the sanitizer presets are
  Linux/macOS only, and the Vulkan presets require the Vulkan SDK.
- **Headless machines:** set `HVT_PRE_PUSH_SKIP_TESTS=1` to build only — the How-to tests need
  a GPU/display. The hook also fails when the test filter matches no tests, so a renamed suite
  cannot silently pass.
- **Run by hand:** `.githooks/pre-push` with no arguments validates `HEAD` (git always passes
  `<remote> <url>`, so no arguments means a manual run).
- **Bypass once:** `git push --no-verify`.
- **Disable:** `git config --unset core.hooksPath`.

## Limitations

Local hooks are a convenience layer, not enforcement. They are opt-in per developer and can be
bypassed with `--no-verify`, so they never replace CI and branch protection on `main` — they
just make the common failure (a preset that does not compile locally) cheap to catch before
the push.

## Windows

The hook works under Git for Windows (hooks run through its bundled bash, even when pushing
from PowerShell or cmd):

- The sanitizer presets are trimmed automatically — they are not defined for Windows in
  [CMakePresets.json](../CMakePresets.json), so the hook builds `debug` only there.
- `.gitattributes` forces LF endings for `.githooks/*`; a CRLF shebang would fail before the
  script starts.
- Push from the **x64 Visual Studio developer environment** so `cl`/`ninja` are on `PATH` —
  the same requirement as any local Windows build.
