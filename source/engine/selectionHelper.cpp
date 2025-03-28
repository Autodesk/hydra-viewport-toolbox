//
// Copyright 2025 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//

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

namespace hvt
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

} // namespace hvt