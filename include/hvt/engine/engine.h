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
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#endif
// clang-format on

#include <pxr/imaging/hd/task.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
// clang-format on

#include <memory>

namespace HVT_NS
{

/// The Engine is the main entry point for executing Hydra tasks.
///
/// This class is modeled after pxr::HdEngine but exists within the HVT namespace
/// to provide a customizable execution environment for Hydra rendering.
///
/// The Engine maintains task context data that can be shared across tasks
/// during execution. This context data is a key-value store where keys are
/// TfTokens and values are VtValues.
///
class HVT_API Engine
{
public:
    /// Constructor.
    Engine();

    /// Destructor.
    virtual ~Engine();

    /// \name Task Context Data Management
    /// @{

    /// Adds or updates the value associated with the token.
    /// \param id The token key for the context data.
    /// \param data The value to associate with the token.
    void SetTaskContextData(PXR_NS::TfToken const& id, PXR_NS::VtValue const& data);

    /// If found, will return the value from the task context data associated with the token.
    /// \param id The token key to look up.
    /// \param data Output parameter to receive the value if found.
    /// \return true if the key was found, false otherwise.
    bool GetTaskContextData(PXR_NS::TfToken const& id, PXR_NS::VtValue* data) const;

    /// Removes the specified token from the task context data.
    /// \param id The token key to remove.
    void RemoveTaskContextData(PXR_NS::TfToken const& id);

    /// Removes all keys from the task context data.
    void ClearTaskContextData();

    /// @}

    /// Execute tasks.
    /// \param index The render index.
    /// \param tasks The list of tasks to execute.
    void Execute(PXR_NS::HdRenderIndex* index, PXR_NS::HdTaskSharedPtrVector* tasks);

    /// Execute tasks identified by their paths.
    /// \param index The render index.
    /// \param taskPaths The paths of the tasks to execute.
    void Execute(PXR_NS::HdRenderIndex* index, PXR_NS::SdfPathVector const& taskPaths);

    /// Returns true if all tasks identified by their paths are converged.
    /// \param index The render index.
    /// \param taskPaths The paths of the tasks to check.
    /// \return true if all tasks are converged, false otherwise.
    bool AreTasksConverged(PXR_NS::HdRenderIndex* index, PXR_NS::SdfPathVector const& taskPaths);

private:
    /// The task context data shared across tasks during execution.
    /// This is a map from TfToken to VtValue.
    PXR_NS::HdTaskContext _taskContext;
};

using EnginePtr = std::unique_ptr<Engine>;

} // namespace HVT_NS
