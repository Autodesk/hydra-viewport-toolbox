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

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wundefined-var-template"
#pragma clang diagnostic ignored "-Wextra-semi"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#pragma warning(disable : 4100)
#pragma warning(disable : 4201)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#pragma warning(disable : 4305)
#endif
// clang-format on

#include <pxr/imaging/hd/pluginRenderDelegateUniqueHandle.h>
#include <pxr/imaging/hd/renderDelegate.h>
#include <pxr/imaging/hd/renderIndex.h>
#include <pxr/imaging/hd/rendererPluginRegistry.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <memory>
#include <string>

namespace hvt
{
class HVT_API RenderIndexProxy
{
public:
    /// Constructor
    /// \param rendererName The name of the render delegate to use e.g., HdStormRendererPlugin.
    /// \param hgiDriver The Hgi driver instance.
    /// \note The caller must maintain the lifetime of the Hgi driver instance.
    RenderIndexProxy(const std::string& rendererName, PXR_NS::HdDriver* hgiDriver);

    RenderIndexProxy(const RenderIndexProxy&)            = delete;
    RenderIndexProxy& operator=(const RenderIndexProxy&) = delete;

    /// Destructor.
    ~RenderIndexProxy();

    /// Returns the render index instance.
    PXR_NS::HdRenderIndex* RenderIndex() { return _renderIndex.get(); }

private:
    PXR_NS::HdPluginRenderDelegateUniqueHandle _renderDelegate;
    std::unique_ptr<PXR_NS::HdRenderIndex> _renderIndex;
};

using RenderIndexProxyPtr = std::shared_ptr<RenderIndexProxy>;

} // namespace hvt
