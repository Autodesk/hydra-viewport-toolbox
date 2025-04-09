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

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/base/tf/token.h>
#include <pxr/base/vt/value.h>
#include <pxr/imaging/hd/aov.h>
#include <pxr/imaging/hdx/pickFromRenderBufferTask.h>
#include <pxr/imaging/hdx/presentTask.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <ostream>

namespace
{
/// Helper struct to always return false.
template <class T>
struct always_false : std::false_type
{
};
} // namespace

namespace hvt
{

/// This function takes a hash container and returns a sorted set of its keys.
template <class HashType>
static std::set<typename HashType::key_type> GetSortedHashKeys(HashType const& hashContainer)
{
    // Create a sorted set
    std::set<typename HashType::key_type> sortedKeys;
    for (const auto& item : hashContainer)
    {
        sortedKeys.insert(item.first);
    }
    return sortedKeys;
}

/// Debugging function to output the values from a TfTokenVector instance, with more details
/// than the default operator<<.
inline std::ostream& operator<<(std::ostream& content, PXR_NS::TfTokenVector const& renderTags)
{
    content << "[";
    for (size_t i = 0; i < renderTags.size(); ++i)
    {
        if (i > 0)
        {
            content << ", ";
        }
        content << renderTags[i];
    }
    content << "]";
    return content;
}

/// Debugging function to output the values from a HdRenderBufferDescriptor instance, with more
/// details than the default operator<<.
inline std::ostream& operator<<(
    std::ostream& content, PXR_NS::HdRenderBufferDescriptor const& rbDesc)
{
    content << "dimensions: " << rbDesc.dimensions << ", "
            << "multiSampled: " << rbDesc.multiSampled << ", "
            << "format: " << rbDesc.format;
    return content;
}

/// Debugging function to output the values from a HdxPickFromRenderBufferTaskParams instance.
/// This function outputs more details than the operator<< defined in
/// hdx/pickFromRenderBufferTask.h.
inline std::ostream& operator<<(
    std::ostream& content, PXR_NS::HdxPickFromRenderBufferTaskParams const& pickRbDesc)
{
    int policyValue = pickRbDesc.overrideWindowPolicy.has_value()
        ? static_cast<int>(pickRbDesc.overrideWindowPolicy.value())
        : -1;

    content << "primIdBufferPath: " << pickRbDesc.primIdBufferPath << ", \n"
            << "instanceIdBufferPath: " << pickRbDesc.instanceIdBufferPath << ", \n"
            << "elementIdBufferPath: " << pickRbDesc.elementIdBufferPath << ", \n"
            << "normalBufferPath: " << pickRbDesc.normalBufferPath << ", \n"
            << "depthBufferPath: " << pickRbDesc.depthBufferPath << ", \n"
            << "cameraId: " << pickRbDesc.cameraId << ", \n"
            << "framing.displayWindow: " << pickRbDesc.framing.displayWindow << ", \n"
            << "framing.dataWindow: " << pickRbDesc.framing.dataWindow << ", \n"
            << "framing.pixelAspectRatio: " << pickRbDesc.framing.pixelAspectRatio << ", \n"
            << "overrideWindowPolicy: " << policyValue << ", \n"
            << "viewport: " << pickRbDesc.viewport;
    return content;
}

/// This is mainly a debugging function used for outputting the contents of a value cache map from a
/// SyncDelegate instance.
template <class TValueCache>
static std::ostream& operator<<(std::ostream& content,
    PXR_NS::TfHashMap<PXR_NS::SdfPath, TValueCache, PXR_NS::SdfPath::Hash> const& valueCacheMap)
{
    for (const auto& taskId : GetSortedHashKeys(valueCacheMap))
    {
        content << "{ " << taskId.GetString() << ":\n";

        auto taskParams = valueCacheMap.find(taskId)->second;

        for (const auto& paramName : GetSortedHashKeys(taskParams))
        {
            auto taskParamIter         = taskParams.find(paramName);
            const PXR_NS::VtValue& val = taskParamIter->second;
            content << "{ " << paramName.GetString() << ":\n";

            if (paramName == PXR_NS::TfToken("renderTags"))
            {
                hvt::operator<<(content, val.Get<PXR_NS::TfTokenVector>());
            }
            else if (paramName == PXR_NS::TfToken("renderBufferDescriptor"))
            {
                hvt::operator<<(content, val.Get<PXR_NS::HdRenderBufferDescriptor>());
            }
            else
            {
                // Check if the value is holding a specific type and use the appropriate stream
                // operator.
                if (val.IsHolding<PXR_NS::HdxPickFromRenderBufferTaskParams>())
                {
                    // Use the locally defined HdxPickFromRenderBufferTaskParams stream operator.
                    hvt::operator<<(content, val.Get<PXR_NS::HdxPickFromRenderBufferTaskParams>());
                }
                else
                {
                    // Use the default stream operator otherwise.
                    content << val;
                }
            }
            content << "}\n";
        }
        content << "}--------------------------------\n";
    }
    return content;
}

} // namespace hvt