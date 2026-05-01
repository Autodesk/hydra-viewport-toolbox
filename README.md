[![CI Full: (Mac-Linux-Windows Deb-Rel)](https://github.com/Autodesk/hydra-viewport-toolbox/actions/workflows/ci-full.yaml/badge.svg?branch=main)](https://github.com/Autodesk/hydra-viewport-toolbox/actions/workflows/ci-full.yaml?query=branch%3Amain+workflow%3A%22CI%20Full%22)
[![CI Tests: (Linux Deb)](https://github.com/Autodesk/hydra-viewport-toolbox/actions/workflows/ci-test.yaml/badge.svg?branch=main)](https://github.com/Autodesk/hydra-viewport-toolbox/actions/workflows/ci-test.yaml?query=branch%3Amain+workflow%3A%22CI%20GPU%20Tests%22)
[![Coverage](https://Autodesk.github.io/hydra-viewport-toolbox/coverage/coverage.svg)](https://Autodesk.github.io/hydra-viewport-toolbox/coverage/)

# Hydra Viewport Toolbox
The **Hydra Viewport Toolbox** (HVT) is a library of utilities that can be used by an application to simplify the use of [OpenUSD](https://openusd.org) Hydra for the application's graphics viewports. The utilities can be used together or independently to add common viewport functionality and improve the performance and visual quality of viewports.

HVT currently includes the following features but it is being expanded to include even more.

- Layering of Hydra render delegate output, optionally from different render delegates ("passes").
- Management of multiple viewports.
- Hydra task management, supporting application-defined lists of tasks.
- Management of data commonly needed for tasks, e.g. render buffers and lighting.
- Tasks for features commonly needed for viewports, e.g. antialiasing and ambient occlusion.
- User interaction for common operations, e.g. selection and camera manipulation.

HVT is developed and maintained by Autodesk. The contents of this repository are fully open source under [the Apache license](LICENSE), with [feature requests and code contributions](CONTRIBUTING.md) welcome!

## Quick Start

To build the project locally using the default configuration:

```bash
cmake --preset debug
cmake --build --preset debug
```

### Windows:
**Prerequisites:** You need Visual Studio's x64 development environment loaded. Use one of these methods:

**Option 1 (Recommended):** Open **"x64 Native Tools Command Prompt for VS 2022"** from the Start Menu, then:
```cmd
cmake --preset debug
cmake --build --preset debug
```

**Option 2:** Configure your current PowerShell session:
> ** Note:** Replace [Edition] with your installed Visual Studio edition (e.g., Community, Professional, or Enterprise). If you have a different version (e.g., 2019), adjust the 2022 part of the path as well.
```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\[Edition]\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64
cmake --preset debug
cmake --build --preset debug
```

> ** Why?** Windows requires the Visual Studio x64 toolchain environment for proper compiler and SDK paths.

This uses the built-in vcpkg manifest and cmake presets to automatically configure dependencies and build paths. No additional setup is needed beyond the platform prerequisites above.

For more information or to customize the configuration, see [Using CMake Presets](#using-cmake-presets) and [vcpkg Integration](#vcpkg-integration).

## Continuous Integration (CI)

CI builds and tests are run via GitHub Actions using shared CMake presets.

### CI GPU Tests

A specialized workflow (`ci-test.yaml`) executes hardware-accelerated tests on a dedicated Linux VM equipped with an NVIDIA GPU. This workflow automatically allocates the cloud VM at the start of the job and deallocates it upon completion.

It runs exclusively on:
- Every Pull Request (after approval for external contributors).
- Every push to the `main` branch.

It will not be able to spawn GPU VM for other scenarios.

These tests ensure that HVT features work correctly on actual GPU hardware and modern NVIDIA drivers.

### CI Full

A full matrix workflow (`ci-full.yaml`) tests on Linux, macOS, and Windows with both Debug and Release configurations. This does not run by default on every PR.
To run it manually, use the “Run workflow” button under the “Actions” tab on GitHub after pushing your branch.
It also runs when a PR merges into `main`.

### CI Minimal

A lightweight CI workflow (`ci-minimal.yaml`) used for quick verification. 
This workflow does not run automatically. It can be run manually with custom options to test a specific platform (`windows`, `linux`, or `macos`) and configuration (`debug` or `release`) by selecting from the “Run workflow” button in GitHub Actions.

## vcpkg Integration

This project uses vcpkg in manifest mode to manage third-party dependencies cleanly and automatically.

### Zero-Setup Build

All required vcpkg steps (initialization, bootstrapping, and toolchain setup) are fully automated. You do not need to install vcpkg manually or set any variables—just configure and build using one of the provided CMake presets (see below).
If the externals/vcpkg/ submodule is missing or uninitialized, the build system will fetch and bootstrap it for you.

### How it Works

The logic is handled in [`cmake/VcpkgSetup.cmake`](./cmake/VcpkgSetup.cmake).

Unit tests enabled or no USD installation path provided, enables vcpkg: 
-	the vcpkg submodule will be fetched, vcpkg will be bootstrapped, toolchain will be set.

This ensures seamless support for both standalone builds and builds as part of larger projects.

Look [here](./docs/vcpkg.md) for some vcpkg details.

### USD Integration

If `OPENUSD_INSTALL_PATH` is not set, the vcpkg `usd-minimal` feature is enabled by default.

You can override this to use a local OpenUSD install by setting `OPENUSD_INSTALL_PATH` from env or cmake.

## Using CMake Presets

This project uses CMake Presets to define consistent and shareable build configurations across local development and CI.

### Building with CMake Presets

This project provides CMakePresets.json-based builds for easy setup. Presets ensure consistent options and automatic toolchain configuration.

To configure and build:
```bash
cmake --preset debug       # Configure Debug build with vcpkg
cmake --build --preset debug
```

Or to use a specific OpenUSD install:
```bash
# Requires setting OPENUSD_INSTALL_PATH env to be set before calling cmake
export OPENUSD_INSTALL_PATH=/path/to/local/usd
cmake --preset debug
cmake --build --preset debug
```

### Running Tests with CMake Presets
Use the test preset to run the test suite:
```bash
ctest --preset debug
```

### Running all with CMake Presets
Use the preset to run all with one command:
```bash
cmake --workflow --preset debug
```
### Custom Builds with CMake User Presets

If the default presets do not fit your needs you can create your own build configurations by adding a `CMakeUserPresets.json` file in the project root. This file is excluded by `.gitignore`, so it won’t interfere with version control or shared presets.

Refer to the [CMake documentation](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html) for details.
