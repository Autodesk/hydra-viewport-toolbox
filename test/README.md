<h1 align="center">How To Examples</h1>

The goal is to provide useful examples on various aspects of `USD` and on all the helpers part of the `Viewport Toolbox` project.


:information_source: The list of examples is ordered from very basic first steps to much more advanced concepts. But you do not need to follow the order if you look for a specific information.


# Table of content

- [How to compile `Viewport Toolbox` in my environment](#HowTo00)
- [How to create an Hgi instance](#HowTo01)
- [How to load a `USD` asset in memory](#HowTo02)
- [How to create one frame pass](#HowTo03)
- [How to create two frame passes](#HowTo04)
- [How to create a custom render task](#HowTo05)
- [How to use the SSAO (Ambient Occlusion) task](#HowTo06)
- [How to use the FXAA (Anti-aliasing) task](#HowTo07)
- [How to use the `Visual Styles` USD plugin](#HowTo08)
- [How to use two different render delegates](#HowTo09)
- [How to include or exclude prims from a frame pass](#HowTo10)
- [How to display the bounding box of a scene](#HowTo11)
- [How to display the wire frame of a scene](#HowTo12)
- [How to explicitly create the list of tasks](#HowTo13)
- [How to use view cube (aka AutoCam) task](#HowTo14)

# How to compile `Viewport Toolbox` in my environment <a name="HowTo00"></a>


## Clone the repo

Below are the usual steps to correctly create your local clone of `Viewport Toolbox`.
```sh
git clone --recurse-submodules https://git.autodesk.com/GFX/hydra-viewport-toolbox.git hvt
cd hvt
mkdir _build
cd _build
```

## Use a locally built `USD`

Now, you have to specify that you want to build `Viewport Toolbox` from your local `USD` using the cmake option `pxr_DIR` which must point to the install directory of your local compilation.

```sh
cmake -GNinja -Dpxr_DIR=../../usd_1/_install -DCMAKE_INSTALL_PREFIX=../_install ../.
ninja
ninja install
```

## Easy debugging

If for any reason you want to run and/or debug the `HowTo` examples you can run all of them.

```sh
./bin/release/TestViewportToolbox
```

## Only build `Viewport Toolbox`

If for any reason you only want the `Hydra Viewport Toolbox` project you can then set the cmake option `ENABLE_VIEWPORT_TOOLBOX_ONLY` to `ON` i.e., it only enables this project (and its single dependency `CoreUtils`) without associated unit tests.

```sh
cmake -GNinja -DENABLE_VIEWPORT_TOOLBOX_ONLY=ON -DCMAKE_INSTALL_PREFIX=../_install ../.
ninja
ninja install
```

## Advanced topics

:information_source: For more details such as platform specific aspects, refer to [HowToBuild](../../../Doc/HowToBuild.md) README file.

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

# How to load a `USD` asset in memory <a name="HowTo02"></a>

## Prerequisite

During the initialization, the resource directory must be set to find the available `viewport toolbox` resources. For example, there are `USD` files defining an axis tripod, move manipulator, etc as well as various shaders. 

:information_source: That step is optional if you do not plan to use any of the `Viewport Toolbox` assets.

```cpp
// Tell the Viewport Toolbox where to find its resources.
agp::ViewportToolbox::Resources::SetResourceDirectory(resFullpath);
```

## Load an asset

The code creates a [stage](https://openusd.org/dev/api/class_usd_stage.html) from an `USD` asset (i.e., load the scene in memory). In that case, the code loads an asset part of the `Viewport Toolbox` project.

```cpp
pxr::UsdStageRefPtr stage = agp::ViewportToolbox::ViewportEngine::CreateStageFromFile(
        agp::ViewportToolbox::Resources::GetGizmoPath("axisTripod.usda").generic_u8string());
```

## Create an empty in-memory stage

Note that dev can still create an empty stage to start.

```cpp
pxr::UsdStageRefPtr stage = agp::ViewportToolbox::ViewportEngine::CreateStage("MyStageName");
```

# How to create one frame pass <a name="HowTo03"></a>

This example (refer to [HowTo02_CreateOneFramePass.cpp](HowTo02_CreateOneFramePass.cpp) for implementation details) demonstrates how to create one frame pass using the Storm render delegate.

## Create the frame pass

There are three steps to fully create a frame pass from an arbitrary `USD` scene file.

<strong>Step 1</strong>: Below is the code to create a [render index](https://openusd.org/release/api/class_hd_render_index.html) instance for a specific hgi implementation with Storm as the renderer.

```cpp
agp::ViewportToolbox::RenderIndexProxyPtr renderIndex;
agp::ViewportToolbox::RendererDescriptor renderDesc;
renderDesc.hgiDriver    = &hgiDriver;
renderDesc.rendererName = "HdStormRendererPlugin";
agp::ViewportToolbox::ViewportEngine::CreateRenderer(renderIndex, renderDesc);
```

<strong>Step 2</strong>: Below is the code to create a scene index.

```cpp
pxr::HdSceneIndexBaseRefPtr sceneIndex 
    = agp::ViewportToolbox::ViewportEngine::CreateUSDSceneIndex(stage);
renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, pxr::SdfPath::AbsoluteRootPath());

```

<strong>Step 3</strong>: Below is the code to create a frame pass.

A frame pass is the class used to render (or select) a collection of prims using a set of input parameters and render state. The class internally contains a task controller to generate the list of render tasks and an engine to render them.

```cpp
agp::ViewportToolbox::FramePassDescriptor passDesc;
passDesc.renderIndex = renderIndex->RenderIndex();
passDesc.uid         = pxr::SdfPath("/sceneFramePass");
agp::ViewportToolbox::FramePassPtr sceneFramePass
    = agp::ViewportToolbox::ViewportEngine::CreateFramePass(passDesc);
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

# How to create two frame passes <a name="HowTo04"></a>

This example (refer to [HowTo03_CreateTwoFramePasses.cpp](HowTo03_CreateTwoFramePasses.cpp) for implementation details) demonstrates how to create two frame passes using different render delegates.

In order to create two frame passes, the idea is to repeat the [HowTo03](#HowTo03) example to create the two frame passes, use the [HowTo02](#HowTo02) example to load the selected manipulator asset and finally, to add the needed glue between the two frame passes to make it work.

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

pxr::HdRenderBuffer* colorBuffer = framePass1->GetRenderBuffer(pxr::HdAovTokens->color);
pxr::HdRenderBuffer* depthBuffer = framePass1->GetRenderBuffer(pxr::HdAovTokens->depth);

const std::vector<std::pair<pxr::TfToken const&, pxr::HdRenderBuffer*>> inputAOVs = {
    { pxr::HdAovTokens->color, colorBuffer }, { pxr::HdAovTokens->depth, depthBuffer }
};
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

# How to create a custom render task <a name="HowTo05"></a>

This example (refer to [HowTo04_CreateACustomRenderTask.cpp](HowTo04_CreateACustomRenderTask.cpp) for implementation details) demonstrates how to create and render a custom render task like the `blur` task from the `Viewport Toolbox` resources.

Take the [HowTo03](#HowTo03) example to create the frame pass and then, add the custom render task.

## Implement it

As the `blur` task is a rendering task there are mainly two parts to create i.e., the glslfx containing the shader program and the task itself i.e., [pxr::HdxTask](https://openusd.org/release/api/class_hdx_task.html).

As example:

* The `blur` shader program is [here](../../../API/AGP/ViewportToolbox/Resources/Shaders/blur.glslfx).
* The `blur` task include is [here](../../../API/AGP/ViewportToolbox/RenderTasks/BlurTask.h)

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
        [&](agp::ViewportToolbox::TaskManager::GetTaskValueFn const& /*fnGetValue*/,
            agp::ViewportToolbox::TaskManager::SetTaskValueFn const& /*fnSetValue*/) {};

    // Adds the blur task i.e., 'blurTask' before the color correction one.

    const pxr::SdfPath colorCorrectionTask =
        main->GetTaskManager()->GetTaskPath("colorCorrectionTask");

    const pxr::SdfPath blurPath =
        main->GetTaskManager()->AddTask<agp::ViewportToolbox::BlurTask>(
            agp::ViewportToolbox::BlurTask::GetToken(), fnCommit, colorCorrectionTask,
            agp::ViewportToolbox::TaskManager::InsertionOrder::insertBefore);

    // Sets the default value.

    agp::ViewportToolbox::BlurTaskParams blurParams;
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
    [&](ViewportToolbox::TaskManager::GetTaskValueFn const& fnGetValue,
        ViewportToolbox::TaskManager::SetTaskValueFn const& fnSetValue) {
        const pxr::VtValue value = fnGetValue(pxr::HdTokens->params);
        ViewportToolbox::BlurTaskParams params = value.Get<ViewportToolbox::BlurTaskParams>();
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

# How to use the SSAO (Ambient Occlusion) task <a name="HowTo06"></a>

This example (refer to [HowTo05_UseSSAORenderTask.cpp](HowTo05_UseSSAORenderTask.cpp) for implementation details) demonstrates how to use the `SSAO`  (i.e., Ambient Occlusion render task) task from the `Viewport Toolbox` resources.

:information_source: Specifically, `Viewport Toolbox` task implements "screen-space ambient occlusion" (SSAO), which computes ambient occlusion in real-time using image-space information.

Follow the [HowTo03](#HowTo03) example to create a frame pass and then add the SSAO task as a custom render task.
To visualize the ambient occlusion (ao) buffer `only`, the variable in `isShowOnlyEnabled` needs to be `true`. It will update a flag in the shader and output the occlusion result only.

```cpp
 // Adds ssao custom task to the frame pass

 {
     // Defines ssao task update function

     auto fnCommit = [&](agp::ViewportToolbox::TaskManager::GetTaskValueFn const& fnGetValue,
                         agp::ViewportToolbox::TaskManager::SetTaskValueFn const& fnSetValue) {
         const pxr::VtValue value = fnGetValue(pxr::HdTokens->params);
         agp::ViewportToolbox::SSAOTaskParams params =
             value.Get<agp::ViewportToolbox::SSAOTaskParams>();
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
         sceneFramePass->GetTaskManager()->AddTask<agp::ViewportToolbox::SSAOTask>(
             agp::ViewportToolbox::SSAOTask::GetToken(), fnCommit, colorCorrectionTask,
             agp::ViewportToolbox::TaskManager::InsertionOrder::insertBefore);

     // Sets the default value.

     agp::ViewportToolbox::SSAOTaskParams ssaoParams;
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

# How to use the FXAA (Anti-aliasing) task <a name="HowTo07"></a>

The example in [HowTo07_UseFXAARenderTask.cpp](HowTo07_UseFXAARenderTask.cpp) demonstrates how to use the `FXAA`  render task of the `Viewport Toolbox`.  It implements the "Fast Approximate Anti-aliasing" algorithm, which applies an image wide blur filter to smooth out aliasing effects. 

Follow the [HowTo03](#HowTo03) example to create a frame pass and then add the FXAA task as a custom render task. 

```cpp
// Adds the 'FXAA' custom task to the frame pass.

{
    // Defines the anti-aliasing task update function.

    auto fnCommit =
        [&](agp::ViewportToolbox::TaskManager::GetTaskValueFn const& fnGetValue,
            agp::ViewportToolbox::TaskManager::SetTaskValueFn const& fnSetValue) {
            const pxr::VtValue value = fnGetValue(pxr::HdTokens->params);
            agp::ViewportToolbox::FXAATaskParams params =
                value.Get<agp::ViewportToolbox::FXAATaskParams>();
            params.resolution = myApp->fxaaResolution;
            fnSetValue(pxr::HdTokens->params, pxr::VtValue(params));
        };

    // Adds the anti-aliasing task i.e., 'fxaaTask'.

    const pxr::SdfPath colorCorrectionTask =
        sceneFramePass->GetTaskManager()->GetTaskPath("colorCorrectionTask");

    // Note: Inserts the FXAA render task into the task list after color correction.

    const pxr::SdfPath fxaaPath =
        sceneFramePass->GetTaskManager()->AddTask<agp::ViewportToolbox::FXAATask>(
            pxr::TfToken("fxaaTask"), fnCommit, colorCorrectionTask,
            agp::ViewportToolbox::TaskManager::InsertionOrder::insertAfter);

    // Sets the default value.

    agp::ViewportToolbox::FXAATaskParams fxaaParams;
    fxaaParams.resolution = myApp->fxaaResolution;
    sceneFramePass->GetTaskManager()->SetTaskValue(
        fxaaPath, pxr::HdTokens->params, pxr::VtValue(fxaaParams));
}
```

Note that this task can also be used as a custom render task by other task controllers such as the one in the `USD` library, as explained in the [HowTo05](#HowTo05) example.

# How to use the `Visual Styles` USD plugin <a name="HowTo08"></a>

The example (refer to [HowTo06_UseVisualStylesPlugin.cpp](HowTo06_UseVisualStylesPlugin.cpp) for implementation details) associated to this paragraph demonstrates how to use a USD plugin (i.e., the `Visual Styles` plugin) from the `Viewport Toolbox` resources. But the paragraph also briefly highlights how to create a USD plugin.

## How to create a `Renderer-specific Scene Index Filters` plugin

One can define a subclass of `pxr::HdSceneIndexPlugin`, either compiled into the render delegate or in an unrelated library. Upon constructing a render delegate Hydra will instantiate scene index filters linked to that render delegate automatically. The filters use their defined order, and are inserted between the render index's merging scene index and the render delegate.

### The USD plugin mechanism

The registration is done using [pxr::HdSceneIndexPluginRegistry](https://openusd.org/release/api/scene_index_plugin_registry_8h_source.html).

```cpp
// Create the plugin.
pxr::HdSceneIndexPluginRegistry::Define<agp::ViewportToolbox::HdXRayModeSceneIndexPlugin>();

// Register the plugin with USD.
pxr::HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
    pxr::TfToken(), // empty token means all renderers
    pxr::TfToken("agp::ViewportToolbox::HdXRayModeSceneIndexPlugin"),
    nullptr, // no argument data necessary
    0,       // insertion phase
    pxr::HdSceneIndexPluginRegistry::InsertionOrderAtStart);
```

The plugin code is a subclass of `pxr::HdSceneIndexPlugin`.

```cpp
class HdXRayModeSceneIndexPlugin : public pxr::HdSceneIndexPlugin
{
public:
    HdXRayModeSceneIndexPlugin() {}

protected:
    pxr::HdSceneIndexBaseRefPtr _AppendSceneIndex(const pxr::HdSceneIndexBaseRefPtr& inputScene,
        const pxr::HdContainerDataSourceHandle& inputArgs) override
    {
        HdSceneIndexBaseRefPtr result = inputScene;
        return HdXRayModeSceneIndex::New(result);
    }
};
```

The plugin declaration is done using a `plugInfo.json` file.

```json
{
    "Plugins": [
      {
        "Info": {
          "Types": {
            "agp::ViewportToolbox::HdXRayModeSceneIndexPlugin": {
              "bases": [
                "HdSceneIndexPlugin"
              ],
              "displayName": "HdXRayModeSceneIndex",
              "loadWithRenderer": "",
              "priority": 0
            }
          }
        },
        "LibraryPath": "../libagp_visual_styles.dylib",
        "Name": "agpVisualStyles",
        "ResourcePath": "resources",
        "Root": "..",
        "Type": "library"
      }
    ]
}
```

### The plugin code

For the `Visual Styles` plugin the behaviour is implemented by `HdXRayModeSceneIndex::ProcessPrim()`.

The class [pxr::HdSingleInputFilteringSceneIndexBase](https://openusd.org/release/api/class_hd_single_input_filtering_scene_index_base.html) is the abstract base class for a filtering scene index that observes a single input scene index.

The `HdXRayModeSceneIndex` implementation is in fact, a scene index which overrides the `displayOpacity` on a collection of selected prims.

```cpp
class HdXRayModeSceneIndex : public pxr::HdSingleInputFilteringSceneIndexBase
{
public:
    static HdXRayModeSceneIndexRefPtr New(const pxr::HdSceneIndexBaseRefPtr& inputScene)
    {
        return pxr::TfCreateRefPtr(new HdXRayModeSceneIndex(inputScene));
    }

    /// \name From pxr::HdSceneIndexBase
    /// @{

    /// Returns a pair of (prim type, datasource) for the object at primPath.
    pxr::HdSceneIndexPrim GetPrim(const pxr::SdfPath& primPath) const override;
    /// Returns the paths of all scene index prims located immediately below primPath.
    pxr::SdfPathVector GetChildPrimPaths(const pxr::SdfPath& primPath) const override;

    /// @}

protected:
    HdXRayModeSceneIndex(const pxr::HdSceneIndexBaseRefPtr& inputScene);

    /// \name From pxr::HdSingleInputFilteringSceneIndexBase
    /// @{

    void _PrimsAdded(const pxr::HdSceneIndexBase& sender,
        const pxr::HdSceneIndexObserver::AddedPrimEntries& entries) override;

    void _PrimsRemoved(const pxr::HdSceneIndexBase& sender,
        const pxr::HdSceneIndexObserver::RemovedPrimEntries& entries) override;

    void _PrimsDirtied(const pxr::HdSceneIndexBase& sender,
        const pxr::HdSceneIndexObserver::DirtiedPrimEntries& entries) override;

    /// @}

private:
    /// Processes a prim by adding it to the existing list of selected prims.
    /// \param primToAdd The path of the prim to process.
    /// \param sceneIndexPrim The scene index prim containing the XRay settings.
    /// \param xRayMode The current status of the XRay mode.
    /// \param xRayCollection The current list of selected prims.
    /// \return True if the prim was actually added or not.
    bool ProcessPrim(const pxr::SdfPath& primToAdd, pxr::HdSceneIndexPrim sceneIndexPrim, bool xRayMode,
        const std::set<pxr::SdfPath>& xRayCollection);

    using _PrimEntryTable = pxr::SdfPathTable<pxr::HdSceneIndexPrim>;
    _PrimEntryTable _entries;
};
```

## How to use the `Visual Styles` plugin

:warning: Do not forget to correctly set the resource path to the `Viewport Toolbox` assets so the plugin transparently loads. If needed you can check the plugin presence.

```cpp
pxr::PlugPluginPtr plugin =
pxr::PlugRegistry::GetInstance().GetPluginWithName("agpVisualStyles");
ASSERT_TRUE(plugin);
```

The code below isolates and highlights some selected prims i.e., only display some prims and hides all the others.

```cpp
agp::ViewportToolbox::XRay xRay;

// Selects some prims from the loaded model.
const pxr::SdfPath prim("/wheel_test_v4_file/wheel_test_v4/Body21");
const pxr::SdfPathSet selectedPrims { prim };

// Adds the visual effect.
xRay.create(renderIndex->RenderIndex(), false);

// Highlights the selected prims.
agp::ViewportToolbox::HighlightSelection(sceneFramePass.get(), selectedPrims);

// Isolates the selected prims.
xRay.isolateSelected(selectedPrims);
```

# How to use two different render delegates <a name="HowTo09"></a>

This example (refer to [HowTo09_UseTwoRenderDelegates.cpp](HowTo09_UseTwoRenderDelegates.cpp) for implementation details) demonstrates how to create two frame passes with render delegates using different render buffer formats.

## Handling differences in buffer formats

In order to create two frame passes (or more), the idea is to repeat the [HowTo04](#HowTo04) example to create the two frame passes. When alternating frame passes using different render buffer/texture formats, the render result from the previous frame pass must still be used as the (input) background for the current frame pass, but the render textures cannot be shared between frame passes in that situation.

For example the `HdStorm` render delegate works directly from GPU (Hgi) textures. But `HdEmbree` and `HdArnold` are CPU-based render delegates so they create CPU render buffers. In that situation the default implementation of the `HdxAovInputTask` task copies the memory data to an internally created Hgi texture which is then used by all the following render tasks (including the `HdxPresentTask` task). This case is correctly handled by the existing code. However, a render delegate implementation creates its render textures with its most appropriate format so, the formats could be different between render delegates, making a plain reuse of the render textures (like in [HowTo04](#HowTo04) example) impossible. For example, `HdEmbree` uses `HdFormatUNorm8Vec4` but `HdStorm` uses `HdFormatFloat16Vec4`.

The compose task handles the latter case by making the necessary implicit copies to correctly blend the current frame pass on top the previous frame pass result.


## Select the render delegate of one of the frame pass

When creating the [render index](https://openusd.org/release/api/class_hd_render_index.html) you can select a specific render delegate, e.g. `HdStorm`, by updating the following line:

```cpp
renderDesc.rendererName = "HdStormRendererPlugin";
```

When having multiple frame passes, you can can select the render delegate which best fits with the purpose of the frame pass. You could then end up having one render delegate for the primary pass and a different one for the secondary pass(es). For example it could be `HdEmbreeRendererPlugin` for the primary pass and the default `HdStormRendererPlugin` for all the secondary frame passes.

## Add the compose task to the frame pass

```cpp
// Adds the 'Compose' custom task to the frame pass.
manipulatorFramePass.sceneFramePass->CreateCustomTask<agp::ViewportToolbox::ComposeTask>(composeTaskId);
```

## Add the task to the list and update the task's parameters

```cpp
// Adds the 'Compose' task to the frame pass.

{
    // Defines the compose task update function.

    auto* main = manipulatorFramePass.sceneFramePass.get();

    auto fnCommit =
        [&](agp::ViewportToolbox::TaskManager::GetTaskValueFn const& fnGetValue,
            agp::ViewportToolbox::TaskManager::SetTaskValueFn const& fnSetValue) {
            const pxr::VtValue value = fnGetValue(pxr::HdTokens->params);
            agp::ViewportToolbox::ComposeTaskParams params =
                value.Get<agp::ViewportToolbox::ComposeTaskParams>();

            // Gets the color texture information from the previous frame pass.
            params.aovToken = pxr::HdAovTokens->color;
            params.aovTextureHandle =
                mainFramePass.sceneFramePass->GetRenderTexture(pxr::HdAovTokens->color);

            fnSetValue(pxr::HdTokens->params, pxr::VtValue(params));
        };

    // Adds the compose task.

    // NOTE: Usually, the compose task is right after the AOV input task, to let following
    // tasks process the AOV buffers as usual.
    const pxr::SdfPath aovInputTask = 
        main->GetTaskManager()->GetTaskPath(pxr::HdxPrimitiveTokens->aovInputTask);

    const pxr::SdfPath composePath =
        main->GetTaskManager()->AddTask<agp::ViewportToolbox::ComposeTask>(
            pxr::TfToken("composeTask"), fnCommit, aovInputTask,
            agp::ViewportToolbox::TaskManager::InsertionOrder::insertAfter);

    // Sets the default values i.e, the structure default values are fine.

    agp::ViewportToolbox::ComposeTaskParams composeParams;
    main->GetTaskManager()->SetTaskValue(
        composePath, pxr::HdTokens->params, pxr::VtValue(composeParams));
}
```

# How to include or exclude prims from a frame pass <a name="HowTo10"></a>

This example (refer to [HowTo10_UseIncludeExclude.cpp](HowTo10_UseIncludeExclude.cpp) for implementation details) demonstrates how to include or exclude prims from a frame pass.

It takes the [HowTo3](#HowTo3) example to create the frame pass and then, demonstrates the inclusion or exclusion of geometry prims.

## Implementation

The code below creates the default collection used by the frame passes (which by default includes all the prims) and only excludes the geometry prims from the grid.

```cpp
pxr::HdRprimCollection collection { agp::ViewportToolbox::FramePassParams().collection };
collection.SetExcludePaths({ gridPath });
```

The code below creates the default collection used by the frame passes but it only includes geometry prims from the grid.

```cpp
pxr::HdRprimCollection collection { agp::ViewportToolbox::FramePassParams().collection };
collection.SetRootPath(gridPath);
```

The code below gets the parameters from the frame pass and set the new collection.

```cpp
auto& params = sceneFramePass->params();
params.collection = collection;
```

# How to display the bounding box of a scene <a name="HowTo11"></a>

This example (refer to [HowTo11_UseBoundingBoxSceneIndex.cpp](HowTo11_UseBoundingBoxSceneIndex.cpp) for implementation details) demonstrates how to use a scene index filter like the 'Bounding box' one.

It takes the [HowTo3](#HowTo3) example to create a single frame pass and it uses the scene index to add the filter.

From the [USD documentation](https://openusd.org/dev/api/_page__hydra__getting__started__guide.html#Hydra_Getting_Started_Filters):

`
It's fairly straightforward to implement a scene index by referring to an input scene index for data access, but then selectively overriding the input scene data. This can be thought of as the lazy programming version of running a transformation on the scene at load time. We call this pattern a scene index filter, and provide a base class for this behavior in HdSingleInputFilteringSceneIndexBase.`

## Implementation using a scene index filter

Note: When the code accesses to a stage it can create the associated scene index using the method `ViewportEngine::CreateUSDSceneIndex()`.

The code to insert scene indice filters is then:

```cpp
pxr::HdSceneIndexBaseRefPtr sceneIndex 
    = agp::ViewportToolbox::ViewportEngine::CreateUSDSceneIndex(stage.stage());
sceneIndex = agp::ViewportToolbox::SceneIndexUtils::BoundingBoxSceneIndex::New(sceneIndex);
renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, pxr::SdfPath::AbsoluteRootPath());
```

## Implementation using a scene index filter based on a USD asset feature

The bounding box feature exists using the `DrawMode` USD asset feature. The insertion order is then slightly different compare to the previous case as it requires to insert the filter before the scene index creation i.e., the `DrawMode` is an USD feature, not an Hydra feature.

```cpp
auto AppendOverridesSceneIndices =
    [](pxr::HdSceneIndexBaseRefPtr const& inputScene) -> pxr::HdSceneIndexBaseRefPtr {
    return agp::ViewportToolbox::SceneIndexUtils::DrawModeSceneIndex::New(inputScene);
};

sceneIndex = agp::ViewportToolbox::ViewportEngine::CreateUSDSceneIndex(
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

# How to display the wire frame of a scene <a name="HowTo12"></a>

This example (refer to [HowTo12_UseWireFrameSceneIndex.cpp](HowTo12_UseWireFrameSceneIndex.cpp) for implementation details) demonstrates how to display a wire frame of an arbitrary scene.

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

sceneIndex = agp::ViewportToolbox::ViewportEngine::CreateUSDSceneIndex(stage.stage());
sceneIndex =
    agp::ViewportToolbox::SceneIndexUtils::DisplayStyleOverrideSceneIndex::New(sceneIndex);
sceneIndex = agp::ViewportToolbox::SceneIndexUtils::WireFrameSceneIndex::New(sceneIndex);

renderIndex->RenderIndex()->InsertSceneIndex(sceneIndex, pxr::SdfPath::AbsoluteRootPath());
```

## Implementation with different render delegates.

The implementation uses the `Storm` render delegate to render the wire frame and the `Embree` render delegate to render the scene. The end goal is to demonstrate that the wire frame is always displayed even if the arbitrary render delegate used to display the scene (in that case `Embree`), does not support this scene index filtering implementation.

The implementation uses the [HowTo03](#HowTo03) as starting point where the first frame pass uses the `Embree` render delegate to render the scene and the second frame pass uses the `Storm` render delegate to render the wire frame on top of the scene.

# How to explicitly create the list of tasks <a name="HowTo13"></a>

This example (refer to [HowTo13_CustomListOfTasks.cpp](HowTo13_CustomListOfTasks.cpp) for implementation details) demonstrates how to create one frame pass using the Storm render delegate.

The code contains three ways to manually create the list of tasks.

## How to create the default list of tasks

```cpp
agp::ViewportToolbox::FramePassDescriptor frameDesc;
frameDesc.renderIndex = renderIndex->RenderIndex();
frameDesc.uid         = pxr::SdfPath("/sceneFramePass");

// Manually creates the default list of tasks.

framePass = std::make_unique<agp::ViewportToolbox::FramePass>(frameDesc.uid.GetText());
framePass->Initialize(frameDesc);
framePass->CreateDefaultTasks();
```

## How to externally create the default list of tasks.

```cpp
agp::ViewportToolbox::FramePassDescriptor frameDesc;
frameDesc.renderIndex = renderIndex->RenderIndex();
frameDesc.uid         = pxr::SdfPath("/sceneFramePass");

// Manually creates the default list of tasks.

framePass = std::make_unique<agp::ViewportToolbox::FramePass>(frameDesc.uid.GetText());
framePass->Initialize(frameDesc);

// Note: When the render delegate is Storm, the creation is as below.

const agp::ViewportToolbox::FramePassParams& params = framePass->params();

const auto getLayerSettings = 
    [&framePass]() -> agp::ViewportToolbox::BasicLayerParams const* {
    return &framePass->params();
};

agp::ViewportToolbox::TaskCreation::CreateDefaultTasks(framePass->GetTaskManager(),
    framePass->GetRenderBufferAccessor(), framePass->GetLightingAccessor(),
    framePass->GetSelectionSettingsAccessor(), getLayerSettings, false);
```

## How to externally create the minimal list of tasks

```cpp
agp::ViewportToolbox::FramePassDescriptor frameDesc;
frameDesc.renderIndex = renderIndex->RenderIndex();
frameDesc.uid         = pxr::SdfPath("/sceneFramePass");

// Manually creates the minimal list of tasks.

framePass = std::make_unique<agp::ViewportToolbox::FramePass>(frameDesc.uid.GetText());
framePass->Initialize(frameDesc);

const auto getLayerSettings = 
    [&framePass]() -> agp::ViewportToolbox::BasicLayerParams const* {
    return &framePass->params();
};

agp::ViewportToolbox::TaskCreation::CreateMinimalTasks(framePass->GetTaskManager(),
    framePass->GetRenderBufferAccessor(), framePass->GetLightingAccessor(),
    getLayerSettings);
```
