// Copyright 2026 Autodesk, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

/// \file
/// Precompiled header shared by every HVT sub-library.
///
/// HVT compiles are dominated by the compiler front-end: parsing the OpenUSD headers accounts for
/// roughly 98% of a typical translation unit, while code generation is a rounding error. A single
/// header, pxr/base/vt/visitValue.h, costs about 2.9 seconds on its own and is pulled in by
/// pxr/imaging/hd/dataSource.h, so nearly every HVT source file pays for it. Parsing these headers
/// once into a precompiled header instead of once per translation unit cuts a clean build of the
/// core library by roughly 3x.
///
/// Rules for this file:
/// - Only third-party headers (OpenUSD and the C++ standard library) belong here. They are stable,
///   so the precompiled header rarely needs to be rebuilt. Adding an HVT header would make every
///   sub-library recompile whenever that header changes.
/// - Only add a header that is both widely included and expensive to parse. A cheap header saves
///   nothing and still enlarges the precompiled header on disk.
/// - Source files must keep including everything they use. The precompiled header is an
///   optimization, not a substitute for correct includes; configure with
///   -DENABLE_PRECOMPILED_HEADERS=OFF to verify that.

// C++ standard library.
#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// OpenUSD base.
#include <pxr/pxr.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>

// OpenUSD imaging.
// NOTE: hd/dataSource.h is the single most expensive header in the build; see the note above.
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdSt/tokens.h>
#include <pxr/imaging/hdx/task.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/usd/sdf/path.h>
