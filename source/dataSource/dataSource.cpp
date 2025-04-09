// Copyright 2025 Autodesk, Inc.
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

#include <hvt/dataSource/dataSource.h>

// TODO: Replace or remove this old logging mechanism.
//#include <CoreUtils/LogUtils.h>

// clang-format off
#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#if __clang_major__ > 11
#pragma clang diagnostic ignored "-Wdeprecated-copy-with-user-provided-copy"
#else
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#endif
#elif _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4003)
#pragma warning(disable : 4100)
#endif
// clang-format on

#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/tokens.h>

#if __clang__
#pragma clang diagnostic pop
#elif _MSC_VER
#pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace hvt
{

const GfMatrix4d& SceneDataSource::worldMatrix() const
{
    static GfMatrix4d defaultMatrix = GfMatrix4d(1.0);
    return defaultMatrix;
}

const VtDictionary& SceneDataSource::properties() const
{
    static VtDictionary emptyDict;
    return emptyDict;
}

GfBBox3d SceneDataSource::getWorldBounds(const SdfPath& /* primPath */) const
{
    //CoreUtils::logError("no implementation for getWorldBounds"); // override in scene delegate
    return GfBBox3d();
}

bool SceneDataSource::bindMaterial(
    const SdfPath& /* primPath */, const VtValue& /* mtlxDocument */)
{
    //CoreUtils::logError(
    //    "unable to bindMaterial - unsupported file type"); // override in scene delegate
    return false;
}

bool SceneDataSource::isPrimitive(const SdfPath& /* path */) const
{
    return false;
}

bool SceneDataSource::transformPrimitives(
    const SdfPathSet&, const GfVec3d&, const GfVec3d&)
{
    return false;
}

} // namespace hvt
