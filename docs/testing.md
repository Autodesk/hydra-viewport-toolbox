# Testing HVT

How to run, filter, and author HVT tests. The Tests-vs-How-tos distinction lives in
[../AGENTS.md](../AGENTS.md); build commands and presets are in [build.md](build.md).

## Run tests

```bash
cmake --preset debug && cmake --build --preset debug
ctest --preset debug
```

All tests compile into one `hvt_test` binary at `build/<preset>/bin/hvt_test`.

### Fast iteration (single tests)

Prefer a targeted subset over the full suite while iterating:

```bash
# Discover test names (suite.name)
./build/debug/bin/hvt_test --gtest_list_tests

# Run one test or a suite. gtest_filter matches "Suite.Name" and treats '.' literally, so the
# prefix before '.*' must be the exact suite name (e.g. TestOutlineTasks, not OutlineTask) —
# a wrong suite name runs 0 tests and still exits 0. Use --gtest_list_tests to confirm names.
./build/debug/bin/hvt_test --gtest_filter=howTo.createOneFramePass
./build/debug/bin/hvt_test --gtest_filter='TestOutlineTasks.*'

# Same filtering through ctest by regex (substring match on the test name)
ctest --preset debug -R howTo
ctest --preset debug -R TestOutlineTasks --output-on-failure
```

Tests require a working GPU/display (SDL2 + OpenGL on Linux/Windows, Metal on macOS). In headless
or sandboxed environments rendering tests will fail to initialize — validate with a build-only
check (`cmake --build --preset debug`) in that case.

### Environment variables

Both sharing optimizations are on by default and can be disabled when debugging a test that is
sensitive to residual GPU state (shared objects survive between tests):

| Variable | Default | Controls |
|----------|---------|----------|
| `HVT_TEST_SHARE_HGI` | `1` | Reuse one Hgi instance per backend (Vulkan, Metal) across tests instead of creating one per test context |
| `HVT_TEST_SHARE_GL_CONTEXT` | `1` | Borrow the process-wide OpenGL window and context for each test context instead of creating a window per test |

## Test layout

| Path | Purpose |
|------|---------|
| `test/tests/` | Unit and image-comparison tests — primary validation suite |
| `test/howTos/` | Usage demonstrations (also executed as tests) |
| `test/data/baselines/` | Golden PNG images for rendered output |
| `test/data/assets/` | USD scenes and other test assets |
| `test/RenderingFramework/` | Shared test utilities (image compare, traces) |

## Image comparison tests

- Compare rendered output against baselines in `test/data/baselines/`
- **Apple/Metal:** some tests skip `primId`-based rendering (non-deterministic) — see [outline.md](outline.md)
- When adding a new rendered test, add a baseline PNG and document platform skips if needed

## Adding coverage for new features

**Unit tests** (`test/tests/`) — required for validation:

1. Params equality / default tests (mirrors existing `*ParamsEquality` tests)
2. Construction / teardown via `TaskManager`
3. Rendered image test with baseline (where applicable)
4. Additional cases for edge behavior and regressions as needed

**How-to** (`test/howTos/`) — one integration example when the feature is user-facing:

1. Show the minimal end-to-end usage an application would follow
2. Document in [../test/README.md](../test/README.md) if the feature is listed there

## Tracing

See [howToCollectTraces.md](howToCollectTraces.md) — use `RenderingUtils::CollectTraces` with
`PXR_ENABLE_GLOBAL_TRACE=1`.
