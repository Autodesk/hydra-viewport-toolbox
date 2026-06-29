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

#include "taskContainerSIImpl.h"

// The scene-index (SI) task backend depends on legacyTaskSchema.h / HdMakeLegacyTaskFactory, which
// only exist in USD >= 25.05. On older USD this whole translation unit compiles to nothing and the
// runtime switch is forced to the scene-delegate (SD) backend.
#if HVT_HAS_LEGACY_TASK_SCHEMA

// clang-format off
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#elif defined(_MSC_VER)
#pragma warning(push)
#endif
// clang-format on

// legacyTaskFactory.h must be included so HdLegacyTaskFactory is a COMPLETE type here.
// legacyTaskSchema.h only forward-declares it (std::shared_ptr<class HdLegacyTaskFactory>), and
// HdLegacyTaskFactory is ARCH_EXPORT_TYPE (default visibility) only when its full declaration is
// visible. clang derives a class-template specialization's type-visibility from the minimum of the
// template's and its template arguments' visibilities; with only the forward declaration, the
// argument defaults to hidden (under -fvisibility=hidden), so the
// HdRetainedTypedSampledDataSource<HdLegacyTaskFactorySharedPtr> instantiation below would get a
// hidden, per-image type_info. That type_info would not coalesce across the hvt_engine <-> libhd
// dylib boundary on macOS/clang, making HdLegacyTaskSchema::GetFactory()'s dynamic_pointer_cast
// (run inside libhd) return null -> "No factory data source in HdLegacyTaskSchema" -> crash.
#include <pxr/imaging/hd/legacyTaskFactory.h>
#include <pxr/imaging/hd/legacyTaskSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/rprimCollection.h>
#include <pxr/imaging/hd/tokens.h>

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

///////////////////////////////////////////////////////////////////////////////
// Task data source conforming to HdLegacyTaskSchema.
//
// This stores task parameters as VtValue (rather than a typed TaskParams) so
// the TaskManager can handle arbitrary task types generically through its
// public API (AddTask<T>, SetTaskValue, GetTaskValue).
//
class TaskDataSource : public HdContainerDataSource
{
public:
    using This = TaskDataSource;

    HD_DECLARE_DATASOURCE(This)

    HdLegacyTaskFactorySharedPtr factory;
    VtValue params;
    HdRprimCollection collection;
    TfTokenVector renderTags;

    HdDataSourceBaseHandle Get(const TfToken& name) override
    {
        if (name == HdLegacyTaskSchemaTokens->factory)
        {
            return HdRetainedTypedSampledDataSource<HdLegacyTaskFactorySharedPtr>::New(factory);
        }
        if (name == HdLegacyTaskSchemaTokens->parameters)
        {
            return HdRetainedTypedSampledDataSource<VtValue>::New(params);
        }
        if (name == HdLegacyTaskSchemaTokens->collection)
        {
            return HdRetainedTypedSampledDataSource<HdRprimCollection>::New(collection);
        }
        if (name == HdLegacyTaskSchemaTokens->renderTags)
        {
            return HdRetainedTypedSampledDataSource<TfTokenVector>::New(renderTags);
        }
        return nullptr;
    }

    TfTokenVector GetNames() override
    {
        // clang-format off
        static const TfTokenVector result = {
            HdLegacyTaskSchemaTokens->factory,
            HdLegacyTaskSchemaTokens->parameters,
            HdLegacyTaskSchemaTokens->collection,
            HdLegacyTaskSchemaTokens->renderTags
        };
        // clang-format on
        return result;
    }

private:
    TaskDataSource(HdLegacyTaskFactorySharedPtr const& factory, VtValue const& params,
        HdRprimCollection const& collection, TfTokenVector const& renderTags) :
        factory(factory), params(params), collection(collection), renderTags(renderTags)
    {
    }
};

HD_DECLARE_DATASOURCE_HANDLES(TaskDataSource);

// Creates a prim-level container data source for a task.
HdContainerDataSourceHandle _CreateTaskPrimDataSource(HdLegacyTaskFactorySharedPtr const& factory,
    VtValue const& params, HdRprimCollection const& collection = {},
    TfTokenVector const& renderTags = {})
{
    return HdRetainedContainerDataSource::New(HdLegacyTaskSchema::GetSchemaToken(),
        TaskDataSource::New(factory, params, collection, renderTags));
}

// Retrieves the TaskDataSource from the retained scene index for a given task path.
TaskDataSourceHandle _GetTaskDataSource(
    HdRetainedSceneIndexRefPtr const& sceneIndex, SdfPath const& path)
{
    HdSceneIndexPrim const prim = sceneIndex->GetPrim(path);
    if (!prim.dataSource)
    {
        return nullptr;
    }
    return TaskDataSource::Cast(HdLegacyTaskSchema::GetFromParent(prim.dataSource).GetContainer());
}

} // anonymous namespace

TaskContainerSIImpl::TaskContainerSIImpl(
    HdRenderIndex* /*renderIndex*/, HdRetainedSceneIndexRefPtr const& retainedSceneIndex) :
    _retainedSceneIndex(retainedSceneIndex)
{
}

void TaskContainerSIImpl::Insert(SdfPath const& taskId, TaskInsertSpec const& spec)
{
    // Create the task prim in the retained scene index. The render index discovers the task
    // through the scene index and uses the factory to instantiate the HdTask.
    HdContainerDataSourceHandle const primDataSource =
        _CreateTaskPrimDataSource(spec.siFactory, spec.params);

    _retainedSceneIndex->AddPrims({ { taskId, HdPrimTypeTokens->task, primDataSource } });
}

void TaskContainerSIImpl::RemoveTask(SdfPath const& taskId)
{
    if (_retainedSceneIndex)
    {
        _retainedSceneIndex->RemovePrims({ { taskId } });
    }
}

VtValue TaskContainerSIImpl::GetValue(SdfPath const& taskId, TfToken const& key)
{
    TaskDataSourceHandle ds = _GetTaskDataSource(_retainedSceneIndex, taskId);
    if (!ds)
    {
        TF_CODING_ERROR("Task not found: %s", taskId.GetText());
        return VtValue();
    }

    if (key == HdTokens->params)
    {
        return ds->params;
    }
    if (key == HdTokens->collection)
    {
        return VtValue(ds->collection);
    }
    if (key == HdTokens->renderTags)
    {
        return VtValue(ds->renderTags);
    }

    TF_CODING_ERROR("Unsupported task value key: %s", key.GetText());
    return VtValue();
}

bool TaskContainerSIImpl::SetValue(SdfPath const& taskId, TfToken const& key, VtValue const& newValue)
{
    TaskDataSourceHandle ds = _GetTaskDataSource(_retainedSceneIndex, taskId);
    if (!ds)
    {
        TF_CODING_ERROR("Task not found: %s", taskId.GetText());
        return false;
    }

    HdDataSourceLocatorSet dirtyLocators;

    if (key == HdTokens->params)
    {
        if (ds->params == newValue)
        {
            return true;
        }
        ds->params = newValue;
        dirtyLocators.insert(HdLegacyTaskSchema::GetParametersLocator());
    }
    else if (key == HdTokens->collection)
    {
        if (ds->collection == newValue.Get<HdRprimCollection>())
        {
            return true;
        }
        ds->collection = newValue.Get<HdRprimCollection>();
        dirtyLocators.insert(HdLegacyTaskSchema::GetCollectionLocator());
    }
    else if (key == HdTokens->renderTags)
    {
        if (ds->renderTags == newValue.Get<TfTokenVector>())
        {
            return true;
        }
        ds->renderTags = newValue.Get<TfTokenVector>();
        dirtyLocators.insert(HdLegacyTaskSchema::GetRenderTagsLocator());
    }
    else
    {
        TF_CODING_ERROR("Unsupported task value key: %s", key.GetText());
        return false;
    }

    if (!dirtyLocators.IsEmpty())
    {
        _retainedSceneIndex->DirtyPrims({ { taskId, dirtyLocators } });
    }

    return true;
}

} // namespace HVT_NS

#endif // HVT_HAS_LEGACY_TASK_SCHEMA
