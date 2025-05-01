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
#pragma once

#include <hvt/api.h>

#if __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#elif _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4003)
#pragma warning(disable : 4100)
#pragma warning(disable : 4127)
#pragma warning(disable : 4244)
#pragma warning(disable : 4305)
#endif

#include <pxr/base/gf/frustum.h>
#include <pxr/imaging/glf/simpleLight.h>
#include <pxr/imaging/hdx/shadowMatrixComputation.h>

#if __clang__
#pragma clang diagnostic pop
#elif _MSC_VER
#pragma warning(pop)
#endif

namespace HVT_NS
{
class ShadowMatrixComputation : public PXR_NS::HdxShadowMatrixComputation
{
public:
    ShadowMatrixComputation(const PXR_NS::GfRange3f& worldBox, const PXR_NS::GlfSimpleLight& light);

    // TODO: we don't respect viewport / framing / policy changes

    std::vector<PXR_NS::GfMatrix4d> Compute();
    std::vector<PXR_NS::GfMatrix4d> Compute(
        const PXR_NS::GfVec4f& viewport, PXR_NS::CameraUtilConformWindowPolicy policy) override;
    std::vector<PXR_NS::GfMatrix4d> Compute(
        const PXR_NS::CameraUtilFraming& framing, PXR_NS::CameraUtilConformWindowPolicy policy) override;

    // update and flag as dirty if the world box or light direction has changed
    bool update(const PXR_NS::GfRange3f& worldBox, const PXR_NS::GlfSimpleLight& light);

    // helpers to just update light or world box
    bool update(const PXR_NS::GfRange3f& worldBox);
    bool update(const PXR_NS::GlfSimpleLight& light);

private:
    bool update(const PXR_NS::GfRange3f& worldBox, const PXR_NS::GfVec3f& lightDir,
        const PXR_NS::GfVec3f& lightPosition);
    bool needsUpdate(const PXR_NS::GfRange3f& worldBox, const PXR_NS::GfVec3f& lightDir,
        const PXR_NS::GfVec3f& lightPosition) const;
    void updateShadowMatrix(PXR_NS::GfMatrix4d& matrix);

private:
    PXR_NS::GfMatrix4d _shadowMatrix;
    PXR_NS::GfRange3f _worldBox;
    PXR_NS::GfVec3f _lightDir;
    PXR_NS::GfVec3f _lightPosition;
    bool _isDirectionalLight;
    bool _dirty { true };
};

} // namespace HVT_NS
