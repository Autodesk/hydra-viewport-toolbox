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

#include <hvt/engine/syncDelegate.h>

#include "engine/delegateStreamUtils.h"

PXR_NAMESPACE_USING_DIRECTIVE

namespace hvt
{

namespace
{
using ValueMapByKey = TfHashMap<TfToken, VtValue, TfToken::HashFunctor>;
using ValueMap      = TfHashMap<SdfPath, ValueMapByKey, SdfPath::Hash>;

template <typename T>
T GetParameter(const ValueMap& values, SdfPath const& id, TfToken const& key)
{
    VtValue vParams;
    ValueMapByKey vCache;

    TF_VERIFY(TfMapLookup(values, id, &vCache) && TfMapLookup(vCache, key, &vParams) &&
            vParams.IsHolding<T>(),
        "Failed to get parameter from value map.");

    return vParams.Get<T>();
}

} // anonymous namespace

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wc++20-extensions"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif

TF_DEFINE_PRIVATE_TOKENS(_tokens,
    (renderBufferDescriptor)
    (renderTags)
    (materialNetworkMap)
);

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

SyncDelegate::SyncDelegate(SdfPath const& uid, HdRenderIndex* pRenderIndex) :
    HdSceneDelegate(pRenderIndex, uid)
{
}

bool SyncDelegate::HasValue(SdfPath const& id, TfToken const& key) const
{
    ValueMapByKey valueMapByKey;
    return TfMapLookup(_values, id, &valueMapByKey) && valueMapByKey.count(key);
}

VtValue SyncDelegate::GetValue(SdfPath const& id, TfToken const& key) const
{
    // Get the value pointer and return the value if it exists.
    if (const VtValue* valuePtr = GetValuePtr(id, key))
    {
        return *valuePtr;
    }

    return VtValue();
}

const VtValue* SyncDelegate::GetValuePtr(SdfPath const& id, TfToken const& key) const
{
    // Search the first level of the map.
    if (const ValueMapByKey* vcache = TfMapLookupPtr(_values, id))
    {
        // Search the second level of the map, return a pointer to the value if it exists.
        return TfMapLookupPtr(*vcache, key);
    }

    return nullptr;
};

VtValue SyncDelegate::Get(SdfPath const& id, TfToken const& key)
{
    return GetValue(id, key);
}

void SyncDelegate::SetValue(
    SdfPath const& id, TfToken const& key, VtValue const& value)
{
    _values[id][key] = value;
}

GfMatrix4d SyncDelegate::GetTransform(SdfPath const& id)
{
    // Extract from value cache.
    if (ValueMapByKey* vcache = TfMapLookupPtr(_values, id))
    {
        if (VtValue* val = TfMapLookupPtr(*vcache, HdTokens->transform))
        {
            if (val->IsHolding<GfMatrix4d>())
            {
                return val->Get<GfMatrix4d>();
            }
        }
    }

    TF_CODING_ERROR(
        "Unexpected call to GetTransform for %s in TaskManager's "
        "internal scene delegate.\n",
        id.GetText());
    return GfMatrix4d(1.0);
}

VtValue SyncDelegate::GetLightParamValue(SdfPath const& id, TfToken const& paramName)
{
    return Get(id, paramName);
}

VtValue SyncDelegate::GetMaterialResource(SdfPath const& id)
{
    return Get(id, _tokens->materialNetworkMap);
}

HdRenderBufferDescriptor SyncDelegate::GetRenderBufferDescriptor(SdfPath const& id)
{
    return GetParameter<HdRenderBufferDescriptor>(_values, id, _tokens->renderBufferDescriptor);
}

TfTokenVector SyncDelegate::GetTaskRenderTags(SdfPath const& taskId)
{
    if (HasValue(taskId, _tokens->renderTags))
    {
        return GetParameter<TfTokenVector>(_values, taskId, _tokens->renderTags);
    }
    return TfTokenVector();
}

std::ostream& operator<<(std::ostream& content, SyncDelegate const& syncDelegate)
{
    content << syncDelegate._values;
    return content;
}

} // namespace hvt
