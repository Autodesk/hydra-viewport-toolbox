// Copyright 2026 Autodesk, Inc.
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
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#pragma clang diagnostic ignored "-Wunused-parameter"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#endif
// clang-format on

#include <pxr/pxr.h>
#include <pxr/usd/sdf/path.h>

#include <pxr/imaging/hdx/api.h>
#include <pxr/imaging/hdx/task.h>

#if defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

namespace HVT_NS
{

/// Parameters for EmptyRenderTask (placeholder for custom task data).
struct HVT_API EmptyRenderTaskParams {};

/// A no-op Hydra task intended as a starting point for custom render work.
///
/// Register with \c TaskManager::AddTask like other HVT tasks; \c Prepare and \c Execute
/// perform no rendering until you add implementation.
class HVT_API EmptyRenderTask : public PXR_NS::HdxTask
{
public:
    EmptyRenderTask(PXR_NS::HdSceneDelegate* delegate, PXR_NS::SdfPath const& uid);
    ~EmptyRenderTask() override;

    void Prepare(PXR_NS::HdTaskContext* ctx, PXR_NS::HdRenderIndex* renderIndex) override;

    void Execute(PXR_NS::HdTaskContext* ctx) override;

    static PXR_NS::TfToken const& GetToken();

protected:
    void _Sync(PXR_NS::HdSceneDelegate* delegate, PXR_NS::HdTaskContext* ctx,
        PXR_NS::HdDirtyBits* dirtyBits) override;

private:
    EmptyRenderTask()                                  = delete;
    EmptyRenderTask(const EmptyRenderTask&)            = delete;
    EmptyRenderTask& operator=(const EmptyRenderTask&) = delete;

    EmptyRenderTaskParams _params;
};

/// VtValue requirements
/// @{
HVT_API std::ostream& operator<<(std::ostream& out, EmptyRenderTaskParams const& pv);
HVT_API bool operator==(EmptyRenderTaskParams const& lhs, EmptyRenderTaskParams const& rhs);
HVT_API bool operator!=(EmptyRenderTaskParams const& lhs, EmptyRenderTaskParams const& rhs);
/// @}

} // namespace HVT_NS
