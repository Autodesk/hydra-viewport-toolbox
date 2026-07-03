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

#include "renderBufferManagerSIImpl.h"

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#endif
// clang-format on

#include <pxr/imaging/hd/renderBuffer.h>
#include <pxr/imaging/hd/renderBufferSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/retainedSceneIndex.h>
#include <pxr/imaging/hdSt/tokens.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

namespace
{

class RenderBufferDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(RenderBufferDataSource)

    GfVec3i dimensions;
    HdFormat format;
    bool multiSampled { true };
    uint32_t msaaSampleCount { 4 };

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (name == HdRenderBufferSchemaTokens->dimensions)
            return HdRetainedTypedSampledDataSource<GfVec3i>::New(dimensions);
        if (name == HdRenderBufferSchemaTokens->format)
            return HdRetainedTypedSampledDataSource<HdFormat>::New(format);
        if (name == HdRenderBufferSchemaTokens->multiSampled)
            return HdRetainedTypedSampledDataSource<bool>::New(multiSampled);
        if (name == HdStRenderBufferTokens->stormMsaaSampleCount)
            return HdRetainedTypedSampledDataSource<uint32_t>::New(msaaSampleCount);
        return nullptr;
    }

    TfTokenVector GetNames() override
    {
        // clang-format off
        static const TfTokenVector result = {
            HdRenderBufferSchemaTokens->dimensions,
            HdRenderBufferSchemaTokens->format,
            HdRenderBufferSchemaTokens->multiSampled,
            HdStRenderBufferTokens->stormMsaaSampleCount
        };
        // clang-format on

        return result;
    }

private:
    RenderBufferDataSource(
        GfVec3i const& dimensions, HdFormat format, bool multiSampled, uint32_t msaaSampleCount) :
        dimensions(dimensions),
        format(format),
        multiSampled(multiSampled),
        msaaSampleCount(msaaSampleCount)
    {
    }
};

HD_DECLARE_DATASOURCE_HANDLES(RenderBufferDataSource);

} // anonymous namespace

RenderBufferManagerSIImpl::RenderBufferManagerSIImpl(
    HdRenderIndex* pRenderIndex, HdRetainedSceneIndexRefPtr const& retainedSceneIndex) :
    RenderBufferManagerImpl(pRenderIndex), _retainedSceneIndex(retainedSceneIndex)
{
}

RenderBufferManagerSIImpl::~RenderBufferManagerSIImpl()
{
    if (_retainedSceneIndex && !_aovBufferIds.empty())
    {
        HdSceneIndexObserver::RemovedPrimEntries entries;
        for (auto const& id : _aovBufferIds)
        {
            entries.push_back({ id });
        }
        _retainedSceneIndex->RemovePrims(entries);
    }
}

void RenderBufferManagerSIImpl::InsertRenderBuffer(
    SdfPath const& id, HdRenderBufferDescriptor const& desc, size_t msaaSampleCount)
{
    const uint32_t msaaCount = desc.multiSampled ? static_cast<uint32_t>(msaaSampleCount) : 1;
    _retainedSceneIndex->AddPrims({ { id, HdPrimTypeTokens->renderBuffer,
        HdRetainedContainerDataSource::New(HdRenderBufferSchema::GetSchemaToken(),
            RenderBufferDataSource::New(
                desc.dimensions, desc.format, desc.multiSampled, msaaCount)) } });
}

void RenderBufferManagerSIImpl::RemoveRenderBuffers(SdfPathVector const& ids)
{
    HdSceneIndexObserver::RemovedPrimEntries removedEntries;
    for (auto const& id : ids)
    {
        removedEntries.push_back({ id });
    }
    if (!removedEntries.empty())
    {
        _retainedSceneIndex->RemovePrims(removedEntries);
    }
}

void RenderBufferManagerSIImpl::UpdateRenderBufferDescriptors(GfVec3i const& dimensions,
    bool multiSampled, size_t msaaSampleCount, bool descriptorSpecsChanged,
    bool msaaSampleCountChanged)
{
    const uint32_t newMsaaCount = multiSampled ? static_cast<uint32_t>(msaaSampleCount) : 1;

    for (auto const& id : _aovBufferIds)
    {
        HdSceneIndexPrim prim = _retainedSceneIndex->GetPrim(id);
        if (!prim.dataSource)
        {
            continue;
        }
        RenderBufferDataSourceHandle ds = RenderBufferDataSource::Cast(
            HdRenderBufferSchema::GetFromParent(prim.dataSource).GetContainer());
        if (!ds)
        {
            continue;
        }

        bool dirty = false;
        if (descriptorSpecsChanged &&
            (ds->dimensions != dimensions || ds->multiSampled != multiSampled))
        {
            ds->dimensions   = dimensions;
            ds->multiSampled = multiSampled;
            dirty            = true;
        }
        if (msaaSampleCountChanged && ds->msaaSampleCount != newMsaaCount)
        {
            ds->msaaSampleCount = newMsaaCount;
            dirty               = true;
        }
        if (dirty)
        {
            _retainedSceneIndex->DirtyPrims(
                { { id, HdDataSourceLocatorSet { HdRenderBufferSchema::GetDefaultLocator() } } });
        }
    }
}

} // namespace HVT_NS
