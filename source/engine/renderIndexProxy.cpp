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

#include <pxr/pxr.h>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/imaging/hd/driver.h>
#include <pxr/imaging/hd/rendererPlugin.h>
#include <pxr/imaging/hdSt/renderDelegate.h>
#include <pxr/imaging/hdSt/resourceRegistry.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>

#if defined(ENABLE_VULKAN)
#include <pxr/imaging/hgiVulkan/hgi.h>
#endif

#if PXR_VERSION >= 2608
#include <pxr/imaging/hd/rendererCreateArgsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#endif

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

#if PXR_VERSION >= 2608
namespace
{

HdContainerDataSourceHandle BuildRendererCreateArgsContainer(HdDriver* hgiDriver, Hgi*& outHgi)
{
    outHgi = hgiDriver ? hgiDriver->driver.GetWithDefault<Hgi*>() : nullptr;
    if (!outHgi || hgiDriver->name != HgiTokens->renderDriver)
    {
        return HdContainerDataSourceHandle();
    }

    return HdRendererCreateArgsSchema::Builder()
        .SetGpuEnabled(HdRetainedTypedSampledDataSource<bool>::New(true))
        .SetDrivers(HdRetainedContainerDataSource::New(
            HdRendererCreateArgsSchemaTokens->hgi,
            HdRetainedTypedSampledDataSource<Hgi*>::New(outHgi)))
        .Build();
}

bool RequiresDirectStormDelegateCreation(Hgi* hgi)
{
#if defined(ENABLE_VULKAN)
    return hgi && dynamic_cast<HgiVulkan*>(hgi) != nullptr;
#else
    (void)hgi;
    return false;
#endif
}

} // namespace
#endif

RenderIndexProxy::RenderIndexProxy(const std::string& rendererName, HdDriver* hgiDriver)
{
    HdRendererPluginRegistry& registry = HdRendererPluginRegistry::GetInstance();

#if PXR_VERSION >= 2608
    Hgi* hgi = nullptr;
    HdContainerDataSourceHandle const rendererCreateArgsContainer =
        BuildRendererCreateArgsContainer(hgiDriver, hgi);

    HdRenderSettingsMap settingsMap;
    if (rendererCreateArgsContainer)
    {
        settingsMap[HdRendererCreateArgsSchemaTokens->rendererCreateArgs] =
            VtValue(rendererCreateArgsContainer);
    }

    HdRendererCreateArgsSchema const rendererCreateArgs(rendererCreateArgsContainer);

    if (RequiresDirectStormDelegateCreation(hgi))
    {
        // HdRendererPlugin::CreateDelegate() calls IsSupported() with an empty schema before
        // reading settingsMap, which probes HgiGL and breaks headless HgiVulkan tests.
        _rendererPlugin = registry.GetOrCreateRendererPlugin(TfToken(rendererName));
        if (!_rendererPlugin)
        {
            TF_RUNTIME_ERROR(
                "Could not create a render delegate for '%s': the renderer plugin is missing or "
                "reports itself as unsupported.",
                rendererName.c_str());
            return;
        }

        if (!_rendererPlugin->IsSupported(rendererCreateArgs))
        {
            TF_RUNTIME_ERROR(
                "Could not create a render delegate for '%s': the renderer plugin is missing or "
                "reports itself as unsupported.",
                rendererName.c_str());
            return;
        }

        _renderDelegate = _rendererPlugin->CreateRenderDelegate(settingsMap);
    }
    else
    {
        _renderDelegateHandle = registry.CreateRenderDelegate(TfToken(rendererName), settingsMap);
        _renderDelegate       = _renderDelegateHandle.Get();
    }
#elif defined(ADSK_OPENUSD_PENDING)
    _rendererPlugin = registry.GetOrCreateRendererPlugin(TfToken(rendererName));
    if (!_rendererPlugin)
    {
        TF_RUNTIME_ERROR(
            "Could not create a render delegate for '%s': the renderer plugin is missing or "
            "reports itself as unsupported.",
            rendererName.c_str());
        return;
    }

    // Prior to USD 26.08, the Hgi must be passed to the render delegate through
    // HdRendererCreateArgs in the render settings map.
    HdRenderSettingsMap settingsMap;
    Hgi* hgi = hgiDriver ? hgiDriver->driver.GetWithDefault<Hgi*>() : nullptr;
    if (hgi && hgiDriver->name == HgiTokens->renderDriver)
    {
        HdRendererCreateArgs rendererCreateArgs;
        rendererCreateArgs.hgi        = hgi;
        rendererCreateArgs.gpuEnabled = true;
        settingsMap.insert(
            std::make_pair(TfToken { "rendererCreateArgs" }, VtValue { rendererCreateArgs }));
    }
    _renderDelegate = settingsMap.empty() ? _rendererPlugin->CreateRenderDelegate()
                                          : _rendererPlugin->CreateRenderDelegate(settingsMap);
#else
    _rendererPlugin = registry.GetOrCreateRendererPlugin(TfToken(rendererName));
    if (!_rendererPlugin)
    {
        TF_RUNTIME_ERROR(
            "Could not create a render delegate for '%s': the renderer plugin is missing or "
            "reports itself as unsupported.",
            rendererName.c_str());
        return;
    }

    _renderDelegate = _rendererPlugin->CreateRenderDelegate();
#endif
    if (!_renderDelegate)
    {
        TF_RUNTIME_ERROR(
            "Could not create a render delegate for '%s': the renderer plugin is missing or "
            "reports itself as unsupported.",
            rendererName.c_str());
        return;
    }

    _renderIndex.reset(HdRenderIndex::New(_renderDelegate, { hgiDriver }));
    if (!_renderIndex)
    {
        TF_RUNTIME_ERROR("Could not create a render index for '%s'.", rendererName.c_str());
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
    _renderIndex = nullptr;
#if PXR_VERSION >= 2608
    if (_renderDelegateHandle)
    {
        _renderDelegateHandle = nullptr;
        _renderDelegate       = nullptr;
    }
    else if (_renderDelegate && _rendererPlugin)
    {
        _rendererPlugin->DeleteRenderDelegate(_renderDelegate);
        _renderDelegate = nullptr;
    }
    _rendererPlugin = nullptr;
#else
    if (_renderDelegate && _rendererPlugin)
    {
        _rendererPlugin->DeleteRenderDelegate(_renderDelegate);
        _renderDelegate = nullptr;
    }
    _rendererPlugin = nullptr;
#endif
}

} // namespace HVT_NS
