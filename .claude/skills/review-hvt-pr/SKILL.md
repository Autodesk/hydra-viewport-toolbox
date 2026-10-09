---
name: review-hvt-pr
description: >-
  HVT code-review conventions. Use when reviewing a pull request or branch diff
  in this repo, or self-reviewing before opening a PR — the review procedure
  and severity order, and the issues HVT reviewers repeatedly flag: compiler
  pragmas and C++17 portability, OpenUSD version guards, Hydra task dirty bits
  and params, Hgi resource lifetime, error handling, test and baseline quality,
  CMake/CI, comment verbosity, and PR scope/merge gates. Read before reviewing
  a PR or before asking for review.
---

# HVT code-review conventions

Apply these when reviewing a PR, or when self-reviewing before opening one. Coding style is in the
`openusd-coding-style` skill and task structure is in `create-hvt-task`, so neither is repeated
here.

## Procedure

1. Read the PR description, then the full diff (`gh pr diff <n>` or `git diff main...`).
2. Go through the sections below. For each finding, give the `file:line`, a concrete failure
   scenario (inputs or platform → wrong output, crash, or build break) and a fix.
3. Rank findings in this order: **build break / correctness / leak** > **portability / USD
   version** > **tests** > **API / design** > **comments / style**. Drop anything `clang-format`
   would fix.
4. Flag only lines the PR touches. Restyling untouched code creates review noise.

## Portability

- **Compiler pragmas:** guard Clang-only warnings (`-Wgnu-zero-variadic-macro-arguments`,
  `-Wc++20-extensions`) with `#if defined(__clang__)` and `#pragma clang diagnostic`, **never**
  `__GNUC__`. Clang also defines `__GNUC__`, so GCC on Linux sees the Clang-only warning names and
  fails with `-Werror=pragmas`. Every `push` needs a matching `pop` on every branch, MSVC included.
- **Pragma scope:** push/pop wraps only the offending `#include`s. Fix warnings in private code
  instead of silencing them in CMake. Public headers must compile without warnings.
- **C++17 only:** no `<compare>`, designated initializers or other C++20 features.
- **Preprocessor directives** are not indented.
- **Paths:** watch for `abc//cde` and trailing-slash differences between Windows and *nix.

## OpenUSD versions

- HVT builds against every OpenUSD version from 24.11 through the latest dev. Any new Hydra or
  USD API needs a `PXR_VERSION` guard.
- **Guard direction:** check `<=` vs `>=`, and that the guard matches the baseline folder it
  selects.
- Avoid version `#if`s in public headers, because callers would need them too.
- **Code copied from OpenUSD `hdx`:** diff it against upstream. Watch for pipeline state that was
  silently dropped (e.g. `stencilTestEnabled`, `alphaToCoverageEnable`).

## Hydra tasks

- **Dirty bits:** every early `return` in `_Sync` handles dirty bits deliberately. Clearing them
  after a failed params fetch makes the failure permanent. Not clearing them re-syncs every
  frame. Apply the same fix to all **sibling tasks**.
- **Params:** `operator==` covers every field. Don't compare fields by hand elsewhere.
- **Setters** that write a task's local params copy are overwritten by the next `_Sync` and never
  take effect.
- **Tokens:** use the shared tokens / `GetToken()` for task names and `HdTaskContext` names, not
  string literals. Don't build `TfToken("…")` every frame or inside loops.
- **Installing twice:** a second manager on the same `FramePass` must fail cleanly. Check
  `HasTask` before `AddTask`, validate the anchor (`atPos`), and only record the manager as
  installed once installation has succeeded.
- **AOV buffers:** sizes and MSAA sample counts must match before a buffer is copied or reused.
  Include resolve buffers, and check every buffer, not only depth.
- **Render outputs** belong to the caller. Explicit `renderOutputs` take precedence over anything
  `FramePass` infers.

## GPU resources and concurrency

- Destroy Hgi resources on failure paths (e.g. shader compilation failed, or `_renderIndex` is
  null) and when a feature is switched off.
- Don't rebuild pipelines or textures every frame, or on every resize, when nothing they depend on
  changed.
- Watch for ownership mismatches between `HgiUniquePtr` and `shared_ptr`.
- No mutable `static` GPU caches; use members instead. Use the correct atomic ordering, and
  RAII-managed counters so an exception can't leak them.

## Error handling

- A factory must not return `nullptr` to callers that dereference it. Either fail with
  `TF_CODING_ERROR` / `TF_FATAL_ERROR`, or check the result at the call site.
- Use `TF_RUNTIME_ERROR` / `TF_CODING_ERROR` with a specific message. A supported configuration
  gets `TF_DEBUG`, not `TF_WARN`. Report a warning that would otherwise repeat every frame only
  once.

## Tests and baselines

- **A behavior change or fix needs a test that fails without it.** If no test is feasible, the PR
  must say why.
- **Assert something real.** Comparing against a constructor default proves nothing.
  `TF_CODING_ERROR` does not fail a gtest, so wrap the code in `TfErrorMark` and check it.
  `EXPECT_NO_THROW` cannot detect a use-after-free.
- **Baselines must tell a fix from a regression:** use several prim IDs, asymmetric framing, and a
  zoom where the effect is visible. Every changed baseline needs a reason in the PR. Check that
  the computed baseline name actually reaches the image comparison.
- **Hygiene:** no console output by default and no silenced errors. Restore global state (cwd, env
  vars) with an RAII guard.
- **Platform skips and thresholds** apply only to the affected backend and carry a comment
  explaining why. Prefer a `HgiDeviceCapabilitiesBits…` check over platform macros.

## CMake and CI

- Prefer presets over new CMake logic. Turn repeated blocks into a helper function.
- New CMake variables, env vars and macros use an `HVT_` prefix, and related names match. No
  downstream product names.
- Use `PROJECT_BINARY_DIR`, not `CMAKE_BINARY_DIR`. List new headers in the right
  `_PUBLIC`/`_PRIVATE_HEADER_FILES`.
- Test-helper libraries are static (`hvt_test_framework` links `GTest::gtest` PUBLIC by
  design — `TestFlags.h` includes `<gtest/gtest.h>`).
- CI cleanup steps use `if: always()`. External contributors never trigger GPU runs on unreviewed
  commits.

## Comments and API

- Remove verbose or AI-generated narration, multi-line comments on trivial code, and stale Doxygen.
  Explain a magic number in one line.
- Is each new public type necessary? Does each name match what the code does (e.g. an `_ApplyX`
  that only returns a value should be `_GetX`)? Adding `[[nodiscard]]` to an existing API breaks
  callers that build with `-Werror`.
- A large design gets a `docs/*.md`. When behavior or the recommended entry point changes, update
  `docs/`, `test/README.md` and the `AGENTS.md` pointers. A **convention** change (style, review,
  commit, task structure) must also update the `.claude/skills/` file that encodes it — a skill that
  contradicts the code teaches the wrong pattern.

## PR scope and merge gates

- Split out bug fixes and breaking renames. No drive-by reformatting, moved lines or accidental
  files.
- The description must match what shipped, including behavior changes and baseline moves. Update
  it after review-driven changes.
- Engine or API changes don't merge until the downstream builds pass.

## Review output

```text
Summary: <what the PR does, verdict>

Blocking
- path/file.cpp:123 — <defect>. Scenario: <inputs/platform → failure>. Fix: <suggestion>.

Should fix
- …

Merge gates: [ ] GCC / MSVC / Clang  [ ] USD 24.11 + latest  [ ] tests & baselines justified
             [ ] downstream builds green
```
