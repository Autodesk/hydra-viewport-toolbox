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
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wdeprecated-copy"
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"
#pragma clang diagnostic ignored "-Wdtor-name"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
// clang-format on

#include <pxr/imaging/hd/driver.h>
#include <pxr/imaging/hgi/hgi.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace HVT_NS
{

/// Defines the Hgi instance to use (only one) e.g. OpenGL or Metal one.
class HVT_API HgiInstance
{
public:
    /// Get the Hgi Instance.
    static HgiInstance& instance();

    HgiInstance(const HgiInstance&)            = delete;
    HgiInstance& operator=(const HgiInstance&) = delete;

    /// Is the singleton enabled?
    bool isEnabled() const;

    /// \brief Creates the Hgi backend.
    /// To create a Hgi backend of choice, pass the token here. Otherwise the default supported Hgi
    /// on current platform gets created e.g., using HgiTokens->Vulkan or
    /// PXR_NS::TfToken("HgiVulkan") to explicitly create a HgiVulkan backend.
    ///
    /// \note Only pass tokens of backends that are explicitly supported on the current platform.
    /// For the Windows platform, the HgiGL and HgiVulkan backends are the only supported.
    ///
    /// \param hgiTokenOfChoice The Request backend name e.g., metal.
    void create(const PXR_NS::TfToken& hgiTokenOfChoice = PXR_NS::TfToken(""));

    /// \brief Destroys the Hgi backend.
    /// \note: That's important to explicitly destroy the backend before leaving the application.
    void destroy();

    /// Get the hgi driver information.
    PXR_NS::HdDriver* hgiDriver();

    /// Get the Hgi implementation.
    PXR_NS::Hgi* hgi();

    /// Get the renderer name.
    std::string const& defaultSceneRendererName();

protected:
    HgiInstance()  = default;
    ~HgiInstance() = default;
};

} // namespace HVT_NS
