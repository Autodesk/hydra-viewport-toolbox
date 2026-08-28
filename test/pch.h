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
/// Precompiled header shared by the test targets.
///
/// The test-side counterpart to source/pch.h, and it follows the same rules. It is a separate
/// file because the test targets do not compile with the library's preprocessor definitions
/// (they lack HVT_BUILD and add GLEW_STATIC), so they cannot reuse the library's precompiled
/// header, and because they need headers the library does not, notably GoogleTest.
///
/// Rules for this file:
/// - Only third-party headers (OpenUSD, GoogleTest and the C++ standard library) belong here.
///   Adding an HVT header, public or private, would make every test recompile whenever that
///   header changes.
/// - Only add a header that is both widely included and expensive to parse.
/// - Test sources must keep including everything they use; configure with
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

// GoogleTest.
#include <gtest/gtest.h>

// OpenUSD base.
#include <pxr/pxr.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>

// OpenUSD imaging and USD.
// NOTE: hdx/renderSetupTask.h and hd/dataSource.h dominate: the former carries
// HdxRenderTaskParams, which hvt/engine/basicLayerParams.h holds by value, and the latter pulls
// pxr/base/vt/visitValue.h. Together they are most of what a test translation unit parses.
// NOTE: No pxr/imaging/glf header belongs here. glf/simpleLightingContext.h reaches
// garch/glApi.h, which defines the __gl_h_ guard; GLEW then refuses to be included after it, and
// the test framework includes GL/glew.h itself. A precompiled header is force-included ahead of
// everything, so it would always lose that race.
#include <pxr/imaging/hd/dataSource.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/repr.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hdSt/tokens.h>
#include <pxr/imaging/hdx/pickTask.h>
#include <pxr/imaging/hdx/renderSetupTask.h>
#include <pxr/imaging/hdx/task.h>
#include <pxr/imaging/hdx/tokens.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/stage.h>
