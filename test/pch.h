//
// Copyright 2026 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//

#pragma once

// Precompiled header for the HVT test support libraries: the OpenUSD headers that
// virtually every test translation unit pulls in. Deliberately limited to third-party
// headers so editing an HVT header does not invalidate the PCH.
//
// GoogleTest is NOT included here because the RenderingFramework libraries do not
// depend on it. Test executables should use pchTests.h instead.

#define _USE_MATH_DEFINES

// STL headers.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#pragma clang diagnostic ignored "-Wundefined-var-template"
#elif _MSC_VER
__pragma(warning(push))
__pragma(warning(disable : 4003))
#endif

#include <pxr/base/vt/value.h>

#include <pxr/pxr.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec4f.h>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/token.h>

#include <pxr/usd/sdf/path.h>

#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/tokens.h>

#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>

#if __clang__
#pragma clang diagnostic pop
#elif _MSC_VER
__pragma(warning(pop))
#endif
