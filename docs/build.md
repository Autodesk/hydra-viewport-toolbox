# Building HVT

How to configure, build, and troubleshoot the Hydra Viewport Toolbox. For the user-facing
quick start see [README.md](../README.md); for advanced vcpkg options see [vcpkg.md](vcpkg.md).

Use **CMake presets only** — do not invent custom configure flags unless explicitly asked
(see `CMakePresets.json`).

```bash
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Or configure, build, and test in one step:

```bash
cmake --workflow --preset debug
```

**Available presets:** `debug`, `debugstatic`, `release`, `relwithdebinfo`, `asan`, `ubsan` (Linux/macOS only),
`debugwithvulkan`, `releasewithvulkan`. Substitute the preset name in the commands above
(e.g. `cmake --preset asan`).

Static libraries use the `debugstatic` preset (`BUILD_SHARED_LIBS=OFF`):

```bash
cmake --workflow --preset debugstatic
cmake --install build/debugstatic
```

## Installed package layout

HVT ships as a **single CMake target** (`hvt::hvt`). Internal sub-libraries (engine, tasks, geometry,
etc.) are compiled as object libraries and linked into that one library — they are not installed or
exported separately (except under Emscripten, which still links static sub-libraries).

After `cmake --install build/<preset>`, consumers use:

```cmake
find_package(pxr CONFIG REQUIRED)
find_package(hvt CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE hvt::hvt)
```

## Build layout

| Path | Purpose |
|------|---------|
| `build/<preset>/` | Configure + build tree (e.g. `build/debug/bin/hvt_test`) |
| `install/<preset>/` | Install prefix set by presets |
| `build/<preset>/compile_commands.json` | Generated for IDE/clang tooling (`CMAKE_EXPORT_COMPILE_COMMANDS`) |
| `build/<preset>/vcpkg_installed/<triplet>/include/pxr/` | OpenUSD/Hydra headers when built via vcpkg — grep here to check a Hydra API |

- **OpenUSD headers:** with vcpkg the `pxr/` tree is under `build/<preset>/vcpkg_installed/`
  (triplet e.g. `arm64-osx-hvt`). With `OPENUSD_INSTALL_PATH` set, the headers are in that install
  tree instead — outside the repo, so use the variable's value to locate them.
- **Windows:** use the x64 Visual Studio developer environment (see [README.md](../README.md)).
- **Local preset overrides:** `CMakeUserPresets.json` at repo root is gitignored — use for personal
  presets, not committed changes.
- **Generated headers:** `include/hvt/namespace.h` is created at configure time — do not hand-edit.

## vcpkg & OpenUSD

- Manifest mode via `vcpkg.json`; setup and bootstrap logic live in `cmake/VcpkgSetup.cmake`
  (auto-initializes the `externals/vcpkg/` submodule when missing).
- **Never edit** ports or files inside `externals/vcpkg/`.
- **Default OpenUSD:** vcpkg `usd-minimal` feature when `OPENUSD_INSTALL_PATH` is unset.
- **Local OpenUSD:** `export OPENUSD_INSTALL_PATH=/path/to/usd/install` **before**
  `cmake --preset debug` (or any preset).
- **First configure is slow** — vcpkg bootstraps and builds OpenUSD + test deps from source.
  Subsequent incremental builds are fast; re-run `cmake --preset` only when presets, the manifest,
  or top-level CMake files change.
- **Advanced vcpkg options** (custom triplet, release-only deps, NuGet cache): see [vcpkg.md](vcpkg.md).

## Fix compile errors (agent workflow)

When asked to build and fix until success:

1. Configure once (`cmake --preset debug`) unless already configured or CMake/vcpkg inputs changed.
2. Build (`cmake --build --preset debug`); fix **source** errors reported by the compiler.
3. Re-build only — do not reconfigure after ordinary `.cpp`/`.h` fixes.
4. Match patterns in neighboring files (includes, `PXR_NAMESPACE_USING_DIRECTIVE`, task params).
5. Do not patch `externals/vcpkg/`, generated headers, or CI-only cache assumptions.

If **configure** fails, see Build troubleshooting below — most first-time failures are environment
or dependency issues, not application source bugs.

## Build speed

A clean build of the core library is dominated by the compiler front-end: parsing the OpenUSD
headers is roughly 98% of a typical translation unit, while code generation is negligible.

Three settings address this. The presets turn all of them on, so a preset build needs no change;
the raw CMake defaults differ only for `ENABLE_COMPILER_CACHE`, which is off unless a preset or an
explicit `-D` enables it.

| Option | Default | Effect |
|--------|---------|--------|
| `ENABLE_PRECOMPILED_HEADERS` | `ON` | Parses the common OpenUSD and standard library headers once into `source/pch.h` instead of once per translation unit |
| `ENABLE_COMPILER_CACHE` | `OFF` | Routes compilation through `ccache`/`sccache` when one is on `PATH` |
| `ENABLE_LIMITED_DEBUG_INFO` | `ON` | Emits limited instead of standalone debug info, **`RelWithDebInfo` only** |

**Precompiled headers.** There are two header lists: [`source/pch.h`](../source/pch.h) for the
library and [`test/pch.h`](../test/pch.h) for the tests. Read the rules at the top of each before
adding to it. Two lists are needed because the tests do not compile with the library's
preprocessor definitions, and a precompiled header can only be reused by a target whose
definitions match. Within each group only the first target compiles the header — the rest reuse
it through CMake's `REUSE_FROM`, which keeps the debug build tree about a gigabyte smaller than
one precompiled header per target.

A precompiled header hides missing `#include`s, so a source file can compile without including
what it uses. Verify with:

```bash
cmake --preset debug -DENABLE_PRECOMPILED_HEADERS=OFF
cmake --build --preset debug
```

**Public header cost.** Application code that includes HVT headers pays for whatever they pull in,
and cannot use our precompiled header.

**Compiler cache.** This does nothing for a clean build; it pays off on rebuilds and branch
switches, where a full recompile becomes ~4s. Install it (`brew install ccache`, `apt install
ccache`, `choco install ccache`) — the configure summary reports whether one was found. It is off
in the raw CMake defaults so that projects embedding HVT do not silently inherit a compiler
launcher; the presets opt in.

The cache lives outside the build tree, so deleting a build directory leaves it untouched. Each
clone compiles into its own ccache *namespace*, reported by the configure summary, and that is what
makes one clone's entries clearable on their own — `ccache --evict-namespace <namespace>`, versus
`ccache -C`, which discards every project's. A namespace partitions the keys, not the size budget:
all of them share the one `max_size` and its LRU.

**Limited debug info.** Apple's Clang defaults to standalone debug info, emitting a full
definition for every type a translation unit mentions even when another one already emits it. It
is most of the object bytes: for the core library, 187 MB of objects becomes 85 MB without it.

**Debug info on MSVC.** Windows builds use `/Z7`, which puts the debug information in the object
files, rather than `/Zi`, which puts it in a separate compile PDB. The linker still produces a
full PDB, so debugging is unchanged; only the object files get bigger.

## Build troubleshooting

Most first-time failures are environment/dependency issues during **configure**, not code errors:

- **Ninja generator required.** The presets use `Ninja` (see `CMakePresets.json`). If configure
  fails immediately with a generator error, install `ninja` (and CMake ≥ 3.26).
- **System libraries for building OpenUSD + test deps from source.** These are not vendored; CI
  installs them (see `.github/workflows/ci-steps.yaml`):
  - Linux (apt): `libxmu-dev libxi-dev libgl-dev libxrandr-dev libxinerama-dev libxcursor-dev
    libltdl-dev autoconf autoconf-archive automake libtool mono-complete python3-venv
    libglu1-mesa-dev freeglut3-dev`
  - macOS (brew): `mono`
- **First configure builds OpenUSD from source** (no local prebuilt cache — the binary cache is
  CI-only), so it is slow and needs disk/RAM. To skip it entirely, point at a prebuilt install:
  `export OPENUSD_INSTALL_PATH=/path/to/usd` before `cmake --preset`.
- **When a dependency build fails, read the real error** in
  `externals/vcpkg/buildtrees/<package>/*.log` — the top-level CMake output only reports that the
  vcpkg step failed.
- **vcpkg submodule** is initialized automatically. If `VCPKG_ROOT` is set in your environment it
  must point to a valid clone, otherwise configure aborts.
- **Windows:** run from the x64 Visual Studio developer environment (see README).

## Fast iteration (single tests)

All tests compile into `hvt_test` at `build/<preset>/bin/hvt_test`. Prefer a targeted run over
the full suite: `./build/debug/bin/hvt_test --gtest_list_tests`, then
`--gtest_filter=Suite.testName` (or `ctest --preset debug -R <regex>`). See [testing.md](testing.md)
for the full test workflow, the gtest suite-name pitfall, baselines, and platform skips.

Tests need a GPU/display; in headless environments use a build-only check
(`cmake --build --preset debug`).
