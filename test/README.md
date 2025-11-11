<h1 align="center">How To Examples</h1>

The goal is to provide useful examples on various aspects of `USD` and on all the helpers part of the `Hydra Viewport Toolbox` project.


:information_source: The list of examples is ordered from very basic first steps to much more advanced concepts. But you do not need to follow the order if you look for a specific information.


# Table of content

- [How to compile `Hydra Viewport Toolbox` in my environment](#HowTo00)
- [How to create an Hgi instance](#HowTo01)
- [How to create one frame pass](#HowTo02)
- [How to create two frame passes](#HowTo03)
- [How to create a custom render task](#HowTo04)
- [How to use the SSAO (Ambient Occlusion) task](#HowTo05)
- [How to use the FXAA (Anti-aliasing) task](#HowTo06)
- [How to include or exclude prims from a frame pass](#HowTo07)
- [How to display the bounding box of a scene](#HowTo08)
- [How to display the wire frame of a scene](#HowTo09)
- [How to explicitly create the list of tasks](#HowTo10)
- [How to use the SkyDome task](#HowTo11)

# How to compile `Hydra Viewport Toolbox` in my environment <a name="HowTo00"></a>


## Clone the repo

Below are the usual steps to correctly create your local clone of `Hydra Viewport Toolbox`.
```sh
git clone --recurse-submodules https://git.autodesk.com/GFX/hydra-viewport-toolbox.git hvt
cd hvt
mkdir _build
cd _build
```
or
```sh
git clone --recurse-submodules https://git.autodesk.com/GFX/hydra-viewport-toolbox.git hvt
cd hvt
cmake --preset debug
cmake --build --preset debug
```

## Use a locally built `USD`

Now, you have to specify that you want to build `Viewport Toolbox` from your local `USD` using the cmake option `OPENUSD_INSTALL_PATH` which must point to the install directory of your local compilation.

```sh
cmake -GNinja -DOPENUSD_INSTALL_PATH=../../usd_1/_install -DCMAKE_INSTALL_PREFIX=../_install ../.
ninja
ninja install
```

## Easy debugging

If for any reason you want to run and/or debug the `HowTo` examples you can run all of them.

```sh
./bin/hvt_test
```
or
```sh
ctest --preset debug
```

## Advanced topics

:information_source: For more details such as platform specific aspects, refer to [HowToBuild](../README.md) README file.

# How to create an Hgi instance <a name="HowTo01"></a>

This example (refer to [HowTo01_CreateHgiImplementation.cpp](HowTo01_CreateHgiImplementation.cpp) for the implementation details) demonstrates how to create and destroy an Hgi (i.e., Hydra Graphics Interface) implementation. In fact, the Hgi API is an abstraction of a backend (e.g., OpenGL, Metal, etc.).

:warning: Only one Hgi instance must be created.

The way to initialize the default [pxr::Hgi](https://openusd.org/dev/api/class_hgi.html) implementation is:

```cpp
pxr::HgiUniquePtr hgi = pxr::Hgi::CreatePlatformDefaultHgi();
```

The way to initialize a specific [pxr::Hgi](https://openusd.org/dev/api/class_hgi.html) implementation is:

```cpp
pxr::HgiUniquePtr hgi = pxr::Hgi::CreateNamedHgi(pxr::HgiTokens->Metal);
```

:information_source: To have the complete list of supported backends, check [here](https://openusd.org/dev/api/imaging_2hgi_2tokens_8h_source.html)  

The way to declare an [pxr::HdDriver](https://openusd.org/release/api/class_hd_driver.html) is:

```cpp
pxr::HdDriver hgiDriver;
hgiDriver.name   = pxr::HgiTokens->renderDriver;
hgiDriver.driver = pxr::VtValue(hgi.get());
```

# How to create one frame pass <a name="HowTo02"></a>

This example (refer to [HowTo02_CreateOneFramePass.cpp](HowTo02_CreateOneFramePass.cpp) for implementation details) demonstrates how to create one frame pass using the Storm render delegate.

## Create the frame pass

There are three steps to fully create a frame pass from an arbitrary `USD` scene file.

<strong>Step 1</strong>: Below is the code to create a [render index](https://openusd.org/release/api/class_hd_render_index.html) instance for a specific hgi implementation with Storm as the renderer.

```cpp
hvt::RenderIndexProxyPtr renderIndex;
hvt::RendererDescriptor renderDesc;
renderDesc.hgiDriver    = &hgiDriver;
renderDesc.rendererName = "HdStormRendererPlugin";
hvt::ViewportEngine::CreateRenderer(renderIndex, renderDesc);
```

<strong>Step 2</strong>: Below is the code to create a scene index.

```cpp
pxr::HdSceneIndexBaseRefPtr sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage);
renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, pxr::SdfPath::AbsoluteRootPath());

```

<strong>Step 3</strong>: Below is the code to create a frame pass.

A frame pass is the class used to render (or select) a collection of prims using a set of input parameters and render state. The class internally contains a task controller to generate the list of render tasks and an engine to render them.

```cpp
hvt::FramePassDescriptor passDesc;
passDesc.renderIndex = renderIndex->RenderIndex();
passDesc.uid         = pxr::SdfPath("/sceneFramePass");
hvt::FramePassPtr sceneFramePass = hvt::ViewportEngine::CreateFramePass(passDesc);
```

## Update the frame pass

Once a frame pass is created its parameters can be set on it to control how the rendering and selection are performed. Default values are fine unless the rendering step needs values different from the defaults. For example, the view and projection matrices must change after loading a new scene or moving the camera.

```cpp
auto& params = sceneFramePass->params();

params.renderBufferSize          = pxr::GfVec2i(context.width(), context.height());
params.viewInfo.viewport         = { { 0, 0 }, { context.width(), context.height() } };
params.viewInfo.viewMatrix       = stage.viewMatrix();
params.viewInfo.projectionMatrix = stage.projectionMatrix();

<...>
```

:information_source: The complete list of parameters are defined [here](https://git.autodesk.com/GFX/hydra-viewport-toolbox/blob/main/include/hvt/framePass.h)  

## Render the frame pass

The code must now create all the render tasks and render them by doing:

```cpp
sceneFramePass->Render();
```

In fact the call is decomposed in two steps:

* `const pxr::HdTaskSharedPtrVector renderTasks = sceneFramePass->GetRenderTasks();` to get the list of render tasks.
* `sceneFramePass->Render(renderTasks);` to sequentially execute the render tasks.

# How to create two frame passes <a name="HowTo03"></a>

This example (refer to [HowTo03_CreateTwoFramePasses.cpp](HowTo03_CreateTwoFramePasses.cpp) for implementation details) demonstrates how to create two frame passes.

In order to create two frame passes, the idea is to repeat the [HowTo02](#HowTo02) example to create the two frame passes, use the [HowTo01b](#HowTo01b) example to load the selected manipulator asset and finally, to add the needed glue between the two frame passes to make it work.

## Select the render of one of the frame pass

When creating the [render index](https://openusd.org/release/api/class_hd_render_index.html) a dev can select a specific render to use instead of the default one i.e., `Storm` by updating the following line:

```cpp
renderDesc.rendererName = "HdStormRendererPlugin";
```

When having multiple frame passes, dev can select a renderer for the scene pass and another one for the secondary pass(es). For example it could be `HdEmbreeRendererPlugin` for the scene pass and the default `HdStormRendererPlugin` for all the secondary frame passes.

## Update & render the frame passes

As explained in the previous example, the code below updates the first render frame but it delays the display to let the second frame pass also update the render buffers.

```cpp
{
    auto& params = framePass1->params();

    <...>

    // Do not display right now, wait for the second frame pass.
    params.enablePresentation = false;

    framePass1->Render();
}
```

The code below gets the `color` and `depth` render buffers from the first frame pass so the second frame uses them. That's for now the default composition between frame passes.

```cpp
// Get the input AOV's from the first frame pass and use them in all overlays so the
// overlay's draw into the same color and depth buffers.

auto& pass = mainFramePass.sceneFramePass;
hvt::RenderBufferBindings inputAOVs = pass->GetRenderBufferBindingsForNextPass(
    { pxr::HdAovTokens->color, pxr::HdAovTokens->depth });
```

Finally, the code below updates the second frame pass, renders without clearing the background as the render buffers already contain the final render of the first frame pass, and finally displays the result (i.e. by default `params.enablePresentation` is true).

```cpp
{
    auto& params = framePass2->params();

    <...>

    // Do not clear the background as it contains the previous frame pass result.
    params.clearBackground = false;
    params.backgroundColor = pxr::GfVec4f(0.0f, 0.0f, 0.0f, 0.0f);

    // Get the list of tasks to render but use the render buffers from the main frame pass.
    const pxr::HdTaskSharedPtrVector renderTasks = framePass2->GetRenderTasks(inputAOVs);

    framePass2->Render(renderTasks);
}
```

# How to create a custom render task <a name="HowTo04"></a>

This example (refer to [HowTo04_CreateACustomRenderTask.cpp](HowTo04_CreateACustomRenderTask.cpp) for implementation details) demonstrates how to create and render a custom render task like the `blur` task from the `Viewport Toolbox` resources.

Take the [HowTo02](#HowTo02) example to create the frame pass and then, add the custom render task.

## Implement it

As the `blur` task is a rendering task there are mainly two parts to create i.e., the glslfx containing the shader program and the task itself i.e., [pxr::HdxTask](https://openusd.org/release/api/class_hdx_task.html).

As example:

* The `blur` shader program is [here](../include/hvt/resources/shaders/blur.glslfx).
* The `blur` task include is [here](../include/hvt/tasks/blurTask.h)

There are only few virtual methods to implement.

```cpp
/// The blur parameters.
struct VIEWPORT_Export BlurTaskParams
{
    /// The amount of Blur to apply.
    float blurAmount = 0.5f;

    /// The name of the aov to blur.
    pxr::TfToken aovName = pxr::HdAovTokens->color;
};

/// The blur render task.
class BlurTask : public pxr::HdxTask
{
public:
    BlurTask(pxr::HdSceneDelegate* delegate, pxr::SdfPath const& id);
    ~BlurTask() override;

    void Prepare(pxr::HdTaskContext* ctx, pxr::HdRenderIndex* renderIndex) override { ... }

    void Execute(pxr::HdTaskContext* ctx) override { ... }

protected:
    void _Sync(pxr::HdSceneDelegate* delegate, pxr::HdTaskContext* ctx, 
        pxr::HdDirtyBits* dirtyBits) override { ...}

    BlurTaskParams _params;
};
```

* The `_Sync()` synchronizes the blur parameters with the render task parameters.
* The `Prepare()` performs any preprocessing works (before the execution).
* The `Execute()` applies the blur image processing i.e. it executes the shader program in that case.


In most cases, the `_Sync()` implementation can be quite generic:
```cpp
void BlurTask::_Sync(HdSceneDelegate* delegate, HdTaskContext* ctx, HdDirtyBits* dirtyBits)
{
    if ((*dirtyBits) & HdChangeTracker::DirtyParams)
    {
        BlurTaskParams params;
        if (_GetTaskParams(delegate, &params))
        {
            _params = params;
        }
    }

    *dirtyBits = HdChangeTracker::Clean;
}
```

## Add it in the frame pass

The code below adds the `blur` task to the frame pass.

```cpp
// Adds the 'blur' custom task to the frame pass.

{
    // Defines the blur task update function.

    // In that case, there is no need for any update.
    auto fnCommit =
        [&](hvt::TaskManager::GetTaskValueFn const& /*fnGetValue*/,
            hvt::TaskManager::SetTaskValueFn const& /*fnSetValue*/) {};

    // Adds the blur task i.e., 'blurTask' before the color correction one.

    const pxr::SdfPath colorCorrectionTask =
        main->GetTaskManager()->GetTaskPath("colorCorrectionTask");

    const pxr::SdfPath blurPath =
        main->GetTaskManager()->AddTask<hvt::BlurTask>(
            hvt::BlurTask::GetToken(), fnCommit, colorCorrectionTask,
            hvt::TaskManager::InsertionOrder::insertBefore);

    // Sets the default value.

    hvt::BlurTaskParams blurParams;
    blurParams.blurAmount = 8.0f;
    main->GetTaskManager()->SetTaskValue(blurPath, pxr::HdTokens->params, pxr::VtValue(blurParams));
}
```

When the `blur` value is dynamically changeable, the update method (i.e., `fnCommit`) can be:
```cpp
// Lets define the application parameters.
struct AppParams
{
    float blur { 8.0f };
    //...
} app;

// Defines the update function callback.
auto fnCommit =
    [&](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
        hvt::TaskManager::SetTaskValueFn const& fnSetValue) {
        const pxr::VtValue value = fnGetValue(pxr::HdTokens->params);
        hvt::BlurTaskParams params = value.Get<hvt::BlurTaskParams>();
        params.blurAmount = app.blur;
        fnSetValue(pxr::HdTokens->params, pxr::VtValue(params));
    };
```

If the app needs to enable/disable the task, the code is:
```cpp
const pxr::SdfPath blurTask = main->GetTaskManager()->GetTaskPath("blurTask");
main->GetTaskManager()->EnableTask(blurTask, myApp->enableBlur);
```

The last step is to render the frame pass using the updated list of render passes.

```cpp
// Renders the updated list of render tasks.
sceneFramePass->Render();
```

:information_source: The last step encapsulates the [pxr::HdEngine](https://openusd.org/release/api/class_hd_engine.html) usage.


It uses the frame pass engine instance to execute the list of render tasks.

```cpp
float FramePass::Render(const pxr::HdTaskSharedPtrVector& renderTasks)
{
    // Render using a list of render tasks.
    _engine->Execute(
        _taskController->GetRenderIndex(), const_cast<HdTaskSharedPtrVector*>(&renderTasks));
    return 1.0f;
}
```

Internally, the engine updates the render task parameters, prepares them and renders them. To better understand the code could be simplified like:

```cpp
Engine::Execute(HdRenderIndex *index, HdTaskSharedPtrVector *tasks)
{
    index->SyncAll(tasks, &_taskContext);

    for (size_t taskNum = 0; taskNum < numTasks; ++taskNum)
    {
        const HdTaskSharedPtr &task = (*tasks)[taskNum];
        task->Prepare(&_taskContext, index);
    }

    for (size_t taskNum = 0; taskNum < numTasks; ++taskNum)
    {
        const HdTaskSharedPtr &task = (*tasks)[taskNum];
        task->Execute(&_taskContext);
    }
}
```

# How to use the SSAO (Ambient Occlusion) task <a name="HowTo05"></a>

This example (refer to [HowTo05_UseSSAORenderTask.cpp](HowTo05_UseSSAORenderTask.cpp) for implementation details) demonstrates how to use the `SSAO`  (i.e., Ambient Occlusion render task) task from the `Hydra Viewport Toolbox` resources.

:information_source: Specifically, `Hydra Viewport Toolbox` task implements "screen-space ambient occlusion" (SSAO), which computes ambient occlusion in real-time using image-space information.

Follow the [HowTo02](#HowTo02) example to create a frame pass and then add the SSAO task as a custom render task.
To visualize the ambient occlusion (ao) buffer `only`, the variable in `isShowOnlyEnabled` needs to be `true`. It will update a flag in the shader and output the occlusion result only.

```cpp
 // Adds ssao custom task to the frame pass

 {
     // Defines ssao task update function

     auto fnCommit = [&](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
                         hvt::TaskManager::SetTaskValueFn const& fnSetValue) {
         const pxr::VtValue value = fnGetValue(pxr::HdTokens->params);
         hvt::SSAOTaskParams params = value.Get<hvt::SSAOTaskParams>();
         params.ao = app.ao;

         auto renderParams                = sceneFramePass->params().renderParams;
         params.view.cameraID             = renderParams.camera;
         params.view.framing              = renderParams.framing;
         params.view.overrideWindowPolicy = renderParams.overrideWindowPolicy;

         params.ao.isEnabled         = true;
         params.ao.isShowOnlyEnabled = true;
         params.ao.amount            = 2.0f;
         params.ao.sampleRadius      = 10.0f;

         fnSetValue(pxr::HdTokens->params, pxr::VtValue(params));
     };

     // Adds the ssao task i.e., 'ssaoTask' before the color correction one.

     const pxr::SdfPath colorCorrectionTask = sceneFramePass->GetTaskManager()->GetTaskPath(
         pxr::HdxPrimitiveTokens->colorCorrectionTask);

     const pxr::SdfPath ssaoPath =
         sceneFramePass->GetTaskManager()->AddTask<hvt::SSAOTask>(
             hvt::SSAOTask::GetToken(), fnCommit, colorCorrectionTask,
             hvt::TaskManager::InsertionOrder::insertBefore);

     // Sets the default value.

     hvt::SSAOTaskParams ssaoParams;
     ssaoParams.ao = app.ao;

     auto renderParams                    = sceneFramePass->params().renderParams;
     ssaoParams.view.cameraID             = renderParams.camera;
     ssaoParams.view.framing              = renderParams.framing;
     ssaoParams.view.overrideWindowPolicy = renderParams.overrideWindowPolicy;

     ssaoParams.ao.isEnabled         = true;
     ssaoParams.ao.isShowOnlyEnabled = true;
     ssaoParams.ao.amount            = 2.0f;
     ssaoParams.ao.sampleRadius      = 10.0f;

     sceneFramePass->GetTaskManager()->SetTaskValue(
         ssaoPath, pxr::HdTokens->params, pxr::VtValue(ssaoParams));
```

# How to use the FXAA (Anti-aliasing) task <a name="HowTo06"></a>

The example in [HowTo06_UseFXAARenderTask.cpp](HowTo06_UseFXAARenderTask.cpp) demonstrates how to use the `FXAA`  render task of the `Hydra Viewport Toolbox`.  It implements the "Fast Approximate Anti-aliasing" algorithm, which applies an image wide blur filter to smooth out aliasing effects. 

Follow the [HowTo02](#HowTo02) example to create a frame pass and then add the FXAA task as a custom render task. 

```cpp
// Adds the 'FXAA' custom task to the frame pass.

{
    // Defines the anti-aliasing task update function.

    auto fnCommit =
        [&](hvt::TaskManager::GetTaskValueFn const& fnGetValue,
            hvt::TaskManager::SetTaskValueFn const& fnSetValue) {
            const pxr::VtValue value = fnGetValue(pxr::HdTokens->params);
            hvt::FXAATaskParams params = value.Get<hvt::FXAATaskParams>();
            params.resolution = myApp->fxaaResolution;
            fnSetValue(pxr::HdTokens->params, pxr::VtValue(params));
        };

    // Adds the anti-aliasing task i.e., 'fxaaTask'.

    const pxr::SdfPath colorCorrectionTask =
        sceneFramePass->GetTaskManager()->GetTaskPath("colorCorrectionTask");

    // Note: Inserts the FXAA render task into the task list after color correction.

    const pxr::SdfPath fxaaPath =
        sceneFramePass->GetTaskManager()->AddTask<hvt::FXAATask>(
            pxr::TfToken("fxaaTask"), fnCommit, colorCorrectionTask,
            hvt::TaskManager::InsertionOrder::insertAfter);

    // Sets the default value.

    hvt::FXAATaskParams fxaaParams;
    fxaaParams.resolution = myApp->fxaaResolution;
    sceneFramePass->GetTaskManager()->SetTaskValue(
        fxaaPath, pxr::HdTokens->params, pxr::VtValue(fxaaParams));
}
```

Note that this task can also be used as a custom render task by other task controllers such as the one in the `USD` library, as explained in the [HowTo04](#HowTo04) example.

# How to include or exclude prims from a frame pass <a name="HowTo07"></a>

This example (refer to [HowTo07_UseIncludeExclude.cpp](HowTo07_UseIncludeExclude.cpp) for implementation details) demonstrates how to include or exclude prims from a frame pass.

It takes the [HowTo02](#HowTo02) example to create the frame pass and then, demonstrates the inclusion or exclusion of geometry prims.

## Implementation

The code below creates the default collection used by the frame passes (which by default includes all the prims) and only excludes the geometry prims from the grid.

```cpp
pxr::HdRprimCollection collection { hvt::FramePassParams().collection };
collection.SetExcludePaths({ gridPath });
```

The code below creates the default collection used by the frame passes but it only includes geometry prims from the grid.

```cpp
pxr::HdRprimCollection collection { hvt::FramePassParams().collection };
collection.SetRootPath(gridPath);
```

The code below gets the parameters from the frame pass and set the new collection.

```cpp
auto& params = sceneFramePass->params();
params.collection = collection;
```

# How to display the bounding box of a scene <a name="HowTo08"></a>

This example (refer to [HowTo08_UseBoundingBoxSceneIndex.cpp](HowTo08_UseBoundingBoxSceneIndex.cpp) for implementation details) demonstrates how to use a scene index filter like the 'Bounding box' one.

It takes the [HowTo02](#HowTo02) example to create a single frame pass and it uses the scene index to add the filter.

From the [USD documentation](https://openusd.org/dev/api/_page__hydra__getting__started__guide.html#Hydra_Getting_Started_Filters):

`
It's fairly straightforward to implement a scene index by referring to an input scene index for data access, but then selectively overriding the input scene data. This can be thought of as the lazy programming version of running a transformation on the scene at load time. We call this pattern a scene index filter, and provide a base class for this behavior in HdSingleInputFilteringSceneIndexBase.`

## Implementation using a scene index filter

Note: When the code accesses to a stage it can create the associated scene index using the method `ViewportEngine::CreateUSDSceneIndex()`.

The code to insert scene indice filters is then:

```cpp
pxr::HdSceneIndexBaseRefPtr sceneIndex  = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
sceneIndex = hvt::SceneIndexUtils::BoundingBoxSceneIndex::New(sceneIndex);
renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, pxr::SdfPath::AbsoluteRootPath());
```

## Implementation using a scene index filter based on a USD asset feature

The bounding box feature exists using the `DrawMode` USD asset feature. The insertion order is then slightly different compare to the previous case as it requires to insert the filter before the scene index creation i.e., the `DrawMode` is an USD feature, not an Hydra feature.

```cpp
auto AppendOverridesSceneIndices =
    [](pxr::HdSceneIndexBaseRefPtr const& inputScene) -> pxr::HdSceneIndexBaseRefPtr {
    return hvt::SceneIndexUtils::DrawModeSceneIndex::New(inputScene);
};

sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(
    stage.stage(), AppendOverridesSceneIndices);
```

The `DrawMode` scene index implementation can be:

```cpp
static HdContainerDataSourceHandle const& _DataSourceForcingBoundsDrawMode()
{
    static pxr::HdContainerDataSourceHandle result =
        pxr::HdRetainedContainerDataSource::New(UsdImagingGeomModelSchema::GetSchemaToken(),
            pxr::UsdImagingGeomModelSchema::Builder()
                .SetApplyDrawMode(pxr::HdRetainedTypedSampledDataSource<bool>::New(true))
                .SetDrawMode(pxr::HdRetainedTypedSampledDataSource<TfToken>::New(
                    pxr::UsdImagingGeomModelSchemaTokens->bounds))
                .SetDrawModeColor(pxr::HdRetainedTypedSampledDataSource<pxr::GfVec3f>::New(
                    pxr::GfVec3f { 0.0f, 1.0f, 0.0f })) // The line color.
                .Build());
    return result;
}

pxr::HdSceneIndexPrim DrawModeSceneIndex::GetPrim(const pxr::SdfPath& primPath) const
{
    pxr::HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);

    if (prim.primType == pxr::HdPrimTypeTokens->mesh)
    {
        prim.dataSource = pxr::HdOverlayContainerDataSource::New(
            _DataSourceForcingBoundsDrawMode(), prim.dataSource);
    }

    return prim;
}
```

# How to display the wire frame of a scene <a name="HowTo09"></a>

This example (refer to [HowTo09_UseWireFrameSceneIndex.cpp](HowTo09_UseWireFrameSceneIndex.cpp) for implementation details) demonstrates how to display a wire frame of an arbitrary scene.

The example provides two different ways to display a wire frame. The last case combines different render delegates
to use the capabilities of each render delegate.

## Implementation using the collection representation

The implementation demonstrates how to display the wire frame of an arbitrary scene using the collection representation. The only difference with the [HowTo02](#HowTo02) (which creates a single frame pass) is to change the default collection representation of the geometry.

```cpp
auto& params = sceneFramePass->params();

<...>

// Changes the geometry representation.
params.collection = pxr::HdRprimCollection(
    pxr::HdTokens->geometry, pxr::HdReprSelector(pxr::HdReprTokens->wire));

sceneFramePass->Render();
```

Note: The `HD_REPR_TOKENS` define lists all the existing representations supported by USD/Hydra.
Refer to [pxr/imaging/hd/tokens.h](https://github.com/PixarAnimationStudios/OpenUSD/blob/release/pxr/imaging/hd/tokens.h) for the details.

## Implementation using scene index filters (`Storm` specific)

The implementation demonstrates how to display the wire frame of an arbitrary scene using scene index filters.
Unfortunately, that's specific to the `Storm` render delegate.

```cpp
// Step 2 - Adds the 'wireframe' scene index.

sceneIndex = hvt::ViewportEngine::CreateUSDSceneIndex(stage.stage());
sceneIndex = hvt::SceneIndexUtils::DisplayStyleOverrideSceneIndex::New(sceneIndex);
sceneIndex = hvt::SceneIndexUtils::WireFrameSceneIndex::New(sceneIndex);

renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, pxr::SdfPath::AbsoluteRootPath());
```

# How to explicitly create the list of tasks <a name="HowTo10"></a>

This example (refer to [HowTo10_CustomListOfTasks.cpp](HowTo10_CustomListOfTasks.cpp) for implementation details) demonstrates how to create one frame pass using the Storm render delegate.

The code contains three ways to manually create the list of tasks.

## How to create the default list of tasks

```cpp
hvt::FramePassDescriptor frameDesc;
frameDesc.renderIndex = renderIndex->RenderIndex();
frameDesc.uid         = pxr::SdfPath("/sceneFramePass");

// Manually creates the default list of tasks.

framePass = std::make_unique<hvt::FramePass>(frameDesc.uid.GetText());
framePass->Initialize(frameDesc);
framePass->CreateDefaultTasks();
```

## How to externally create the default list of tasks.

```cpp
hvt::FramePassDescriptor frameDesc;
frameDesc.renderIndex = renderIndex->RenderIndex();
frameDesc.uid         = pxr::SdfPath("/sceneFramePass");

// Manually creates the default list of tasks.

framePass = std::make_unique<hvt::FramePass>(frameDesc.uid.GetText());
framePass->Initialize(frameDesc);

// Note: When the render delegate is Storm, the creation is as below.

const hvt::FramePassParams& params = framePass->params();

const auto getLayerSettings = 
    [&framePass]() -> hvt::BasicLayerParams const* {
    return &framePass->params();
};

hvt::TaskCreation::CreateDefaultTasks(framePass->GetTaskManager(),
    framePass->GetRenderBufferAccessor(), framePass->GetLightingAccessor(),
    framePass->GetSelectionSettingsAccessor(), getLayerSettings, false);
```

## How to externally create the minimal list of tasks

```cpp
hvt::FramePassDescriptor frameDesc;
frameDesc.renderIndex = renderIndex->RenderIndex();
frameDesc.uid         = pxr::SdfPath("/sceneFramePass");

// Manually creates the minimal list of tasks.

framePass = std::make_unique<hvt::FramePass>(frameDesc.uid.GetText());
framePass->Initialize(frameDesc);

const auto getLayerSettings = 
    [&framePass]() -> hvt::BasicLayerParams const* {
    return &framePass->params();
};

hvt::TaskCreation::CreateMinimalTasks(framePass->GetTaskManager(),
    framePass->GetRenderBufferAccessor(), framePass->GetLightingAccessor(),
    getLayerSettings);
```

# How to use the Sky Dome task <a name="HowTo11"></a>
This example (refer to [HowTo11_UseSkyDomeTask.cpp](HowTo11_UseSkyDomeTask.cpp) for implementation details) demonstrates how to add a Sky Dome render task before all existing default render tasks.
An important detail that can easily be overlooked is the necessity to have a valid dome light in the frame pass viewInfo parameter:
```
pxr::GlfSimpleLight domeLight;
domeLight.SetID(pxr::SdfPath("DomeLight"));
domeLight.SetIsDomeLight(true);
params.viewInfo.lights.push_back(domeLight);
```
Other than this detail, the Sky Dome task is typically inserted before other render tasks, as it should be the farthest geometry and should be rendered first.