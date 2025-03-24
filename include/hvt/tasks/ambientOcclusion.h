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

#include <ostream>

namespace hvt
{

struct HVT_API AmbientOcclusionProperties
{
    /// Whether the effect is enabled.
    bool isEnabled = false;

    /// Whether the ambient occlusion is shown directly, without blending with the color AOV.
    bool isShowOnlyEnabled = false;

    /// The amount by which to blend the ambient occlusion with the color AOV.
    ///
    /// This has no effect if the ambient occlusion is shown directly.
    float amount = 1.0f;

    /// The distance to look for occluders, in either scene units or a fraction of the screen size,
    /// as determined by isScreenSampleRadius.
    float sampleRadius = 1.0f;

    /// Whether the sample radius should be treated as a screen-relative scale (true) or as scene
    /// units (false).
    bool isScreenSampleRadius = false;

    /// The number of samples to use when looking for occluders; this is a tradeoff between
    /// performance and quality.
    int sampleCount = 8;

    /// Whether the raw ambient occlusion result is denoised (blurred).
    bool isDenoiseEnabled = true;

    /// The sharpness of edges (as determined by depth discontinuities) when performing denoising.
    float denoiseEdgeSharpness = 1.0;

    /// Compares the ambient occlusion property values.
    bool operator==(AmbientOcclusionProperties const& other) const
    {
        return isEnabled == other.isEnabled && isShowOnlyEnabled == other.isShowOnlyEnabled &&
            amount == other.amount && sampleRadius == other.sampleRadius &&
            isScreenSampleRadius == other.isScreenSampleRadius &&
            sampleCount == other.sampleCount && isDenoiseEnabled == other.isDenoiseEnabled &&
            denoiseEdgeSharpness == other.denoiseEdgeSharpness;
    }

    /// Compares the ambient occlusion property values, and negates.
    bool operator!=(AmbientOcclusionProperties const& other) const { return !(*this == other); }

    /// Writes out the ambient occlusion property values to the stream.
    friend std::ostream& operator<<(std::ostream& out, AmbientOcclusionProperties const& pv)
    {
        out << "Ambient Occlusion Params: " << pv.isEnabled << " " << pv.isShowOnlyEnabled << " "
            << pv.amount << " " << pv.sampleRadius << " " << pv.isScreenSampleRadius << " "
            << pv.sampleCount << " " << pv.isDenoiseEnabled << " " << pv.denoiseEdgeSharpness;

        return out;
    }
};

} // namespace hvt