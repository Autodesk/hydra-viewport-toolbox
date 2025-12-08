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

#include <hvt/engine/renderIndexProxy.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wunused-parameter"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/imaging/hd/driver.h>
#include <pxr/imaging/hdSt/renderDelegate.h>
#include <pxr/imaging/hdSt/resourceRegistry.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

RenderIndexProxy::RenderIndexProxy(const std::string& rendererName, HdDriver* hgiDriver)
{
    HdRenderSettingsMap settingsMap;
#ifdef ADSK_OPENUSD_PENDING
    Hgi* hgi = hgiDriver ? hgiDriver->driver.GetWithDefault<Hgi*>() : nullptr;
    if (hgi && hgiDriver->name == HgiTokens->renderDriver)
    {
        HdRendererCreateArgs rendererCreateArgs;
        rendererCreateArgs.hgi = hgi;
        rendererCreateArgs.gpuEnabled = true;
        settingsMap.insert(std::make_pair(TfToken{"rendererCreateArgs"}, VtValue{rendererCreateArgs}));
    }
#endif

    HdRendererPluginRegistry& registry = HdRendererPluginRegistry::GetInstance();
    _renderDelegate = registry.CreateRenderDelegate(TfToken(rendererName), settingsMap);
    if (_renderDelegate)
    {
        _renderIndex.reset(HdRenderIndex::New(_renderDelegate.Get(), { hgiDriver }));
    }
}

RenderIndexProxy::~RenderIndexProxy()
{
    // Perform a safe CPU & GPU memory cleanup.

    if (_renderDelegate)
    {
        // Clear storm - simple buffer clear as we hold storm around for secondary graphics
        auto hdStResourceRegistry =
            dynamic_cast<HdStResourceRegistry*>(_renderDelegate->GetResourceRegistry().get());
        if (hdStResourceRegistry)
        {
            // Remove any entries associated with expired dispatch buffers.
            hdStResourceRegistry->GarbageCollectDispatchBuffers();
            // Remove any entries associated with expired misc buffers.
            hdStResourceRegistry->GarbageCollectBufferResources();
        }
        // Clear other render delegates
        else
        {
            // Cleanup for all the other render delegates (i.e., not Storm).
            _renderDelegate->GetResourceRegistry()->GarbageCollect();
        }
    }

// #define DISPLAY_RESOURCE_REGISTRY_CONTENT
#ifdef DISPLAY_RESOURCE_REGISTRY_CONTENT

    // Display current resource registry status.

    if (_renderDelegate)
    {
        auto resourceRegistry =
            dynamic_cast<HdStResourceRegistry*>(_renderDelegate->GetResourceRegistry().get());
        auto status = resourceRegistry->GetResourceAllocation();
        std::cout << "HdStResourceRegistry:\n";
        for (const auto& entry : status)
        {
            std::cout << "      " << entry.first << " " << entry.second << "\n";
        }
    }
#endif

    // The order is important.
    _renderIndex    = nullptr;
    _renderDelegate = nullptr;
}

} // namespace HVT_NS
