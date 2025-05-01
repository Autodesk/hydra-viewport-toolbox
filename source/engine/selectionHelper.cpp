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

#include "selectionHelper.h"

#include <hvt/engine/taskUtils.h>

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

#include <pxr/imaging/hdx/tokens.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

SelectionHelper::SelectionHelper(SdfPath const& taskManagerUid) : _taskManagerUid(taskManagerUid)
{
    _selectionTracker = std::make_shared<HdxSelectionTracker>();
}

void SelectionHelper::SetSelection(HdSelectionSharedPtr selection)
{
    _selectionTracker->SetSelection(selection);
}

void SelectionHelper::SetVisualizeAOV(TfToken const& name)
{
    if (_viewportAovName == name)
    {
        return;
    }

    if (name == HdAovTokens->color)
    {
        _bufferPaths.primIdBufferPath     = GetAovPath(_taskManagerUid, HdAovTokens->primId);
        _bufferPaths.instanceIdBufferPath = GetAovPath(_taskManagerUid, HdAovTokens->instanceId);
        _bufferPaths.elementIdBufferPath  = GetAovPath(_taskManagerUid, HdAovTokens->elementId);
        _bufferPaths.depthBufferPath      = GetAovPath(_taskManagerUid, HdAovTokens->depth);
    }
    else
    {
        _bufferPaths.primIdBufferPath     = SdfPath::EmptyPath();
        _bufferPaths.instanceIdBufferPath = SdfPath::EmptyPath();
        _bufferPaths.elementIdBufferPath  = SdfPath::EmptyPath();
        _bufferPaths.depthBufferPath      = SdfPath::EmptyPath();
    }

    _viewportAovName = name;
}

SelectionBufferPaths const& SelectionHelper::GetBufferPaths() const
{
    return _bufferPaths;
}

SelectionBufferPaths& SelectionHelper::GetBufferPaths()
{
    return _bufferPaths;
}

SelectionSettings const& SelectionHelper::GetSettings() const
{
    return _settings;
}

SelectionSettings& SelectionHelper::GetSettings()
{
    return _settings;
}

void SelectionHelper::SetSelectionContextData(HdEngine* engine)
{
    VtValue selectionValue(_selectionTracker);
    engine->SetTaskContextData(HdxTokens->selectionState, selectionValue);
}

HdxSelectionTrackerSharedPtr const& SelectionHelper::GetSelectionTracker() const
{
    return _selectionTracker;
}

HdxSelectionTrackerSharedPtr SelectionHelper::GetSelectionTracker()
{
    return _selectionTracker;
}

} // namespace HVT_NS