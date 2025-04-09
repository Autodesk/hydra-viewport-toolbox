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

#include <hvt/engine/hgiInstance.h>

#include "utils/pathUtils.h"

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/base/arch/env.h>
#include <pxr/imaging/hgi/tokens.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace hvt
{

namespace
{

/// That's the data to initialize based on the selected Hgi implementation.
struct HgiInstanceData
{
    HgiUniquePtr hgi;
    HdDriver hgiDriver;

    // ex: HdStormRendererPlugin, HdAuroraRendererPlugin, HdEmbreeRendererPlugin.
    std::string defaultSceneRendererName { "HdStormRendererPlugin" };

    void destroy()
    {
        // Explicitly release the Hgi resources, as this has side-effects that may need to be
        // performed manually before pipeline destruction.

        hgiDriver = {};
        hgi       = nullptr;
    }

} hgiInstanceData;

} // anonymous namespace

HgiInstance& HgiInstance::instance()
{
    static HgiInstance impl;
    return impl;
}

void HgiInstance::create(const TfToken& hgiTokenOfChoice)
{
    if (!hgiInstanceData.hgi)
    {
        // Initialize the right Hgi instance.

        if (!hgiTokenOfChoice.IsEmpty())
        {
            hgiInstanceData.hgi = Hgi::CreateNamedHgi(hgiTokenOfChoice);
        }
        else
        {
            hgiInstanceData.hgi = Hgi::CreatePlatformDefaultHgi();
        }

        if (hgiInstanceData.hgiDriver.driver.IsEmpty())
        {
            hgiInstanceData.hgiDriver.name   = HgiTokens->renderDriver;
            hgiInstanceData.hgiDriver.driver = VtValue(hgiInstanceData.hgi.get());
        }

        // FIXME: The following initialization is clearly not related to Hgi.
        // To be moved somewhere else.

        // Initialize MaterialX Data library path, this needs to be done before USD plugin discovery
        // The PXR_MTLX_STDLIB_SEARCH_PATHS is initialized by default to USD build location which
        // must updated to application specific path
        ArchSetEnv("PXR_MTLX_STDLIB_SEARCH_PATHS",
            GetDefaultMaterialXDirectory().append("libraries").string().c_str(), true);
    }
}

bool HgiInstance::isEnabled() const
{
    return hgiInstanceData.hgi != nullptr;
}

Hgi* HgiInstance::hgi()
{
    return hgiInstanceData.hgi.get();
}
HdDriver* HgiInstance::hgiDriver()
{
    return &hgiInstanceData.hgiDriver;
}

void HgiInstance::destroy()
{
    hgiInstanceData.destroy();
}

std::string const& HgiInstance::defaultSceneRendererName()
{
    return hgiInstanceData.defaultSceneRendererName;
}

} // namespace hvt
