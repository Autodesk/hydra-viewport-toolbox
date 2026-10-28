# Grey-scale render task

`GreyScaleTask` is a fullscreen post-process that reads the Hydra `color` AOV and writes a
luminance-only image back through the usual `color` / `colorIntermediate` ping-pong used by other
HVT post tasks (blur, FXAA, compose).

## Luminance

The fragment shader uses Rec. 601 weights (`0.299`, `0.587`, `0.114`) on linear RGB. Alpha is
preserved from the source color.

## Pipeline placement

Register the task on a `TaskManager` before `presentTask` (after color correction) so the viewport
shows greyscale output:

```cpp
const SdfPath presentTask =
    framePass->GetTaskManager()->GetTaskPath(HdxPrimitiveTokens->presentTask);

framePass->GetTaskManager()->AddTask<hvt::GreyScaleTask>(
    hvt::GreyScaleTask::GetToken(), hvt::GreyScaleTaskParams(), nullptr, presentTask,
    hvt::TaskManager::InsertionOrder::insertBefore);
```

`GreyScaleTaskParams` is empty; no per-frame commit function is required unless you extend the
params struct later.

## Source layout

| Item | Location |
|------|----------|
| Task API | `include/hvt/tasks/greyScaleTask.h` |
| Implementation | `source/tasks/greyScaleTask.cpp` |
| Shader | `include/hvt/resources/shaders/greyScale.glslfx` |

## Tests

- Params and token: `test/tests/testTaskHelpers.cpp`
- End-to-end usage: `test/howTos/howTo23_UseGreyScaleRenderTask.cpp`
