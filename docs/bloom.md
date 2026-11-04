# Bloom

## Overview

`BloomTask` is a fullscreen post-process that adds a soft glow around the bright areas of the
color AOV. It is an **extension** to stock Hydra, which has no bloom task. Like the other
viewport post-process tasks (SSAO, FXAA), it reads the color AOV, draws into the intermediate
color AOV with an `HdxFullscreenShader`, and then toggles the render targets.

Source: `include/hvt/tasks/bloomTask.h`, `source/tasks/bloomTask.cpp`,
`include/hvt/resources/shaders/bloom.glslfx`.

## Algorithm

For each pixel, the fragment shader (`Bloom::Fragment`):

1. Samples the scene color at the pixel.
2. Samples a 3x3 grid of neighbors, spaced `blurRadius` texels apart.
3. For each neighbor, computes the luminance (Rec. 709 weights) and keeps the part above
   `threshold`. The neighbor color is scaled by that excess.
4. Averages the nine contributions and adds the result to the scene color, scaled by
   `intensity`. The alpha channel is unchanged.

## Parameters

`BloomTaskParams`:

| Member | Default | Description |
|--------|---------|-------------|
| `threshold` | `0.75` | Luminance below which a pixel does not contribute to the bloom. |
| `intensity` | `0.5` | Multiplier applied to the bloom before it is added to the scene color. |
| `blurRadius` | `4.0` | Spacing, in texels, between the samples of the 3x3 kernel. |

`BloomTaskParams` provides `operator==`, `operator!=` and `operator<<`. The task uses the
equality to detect parameter changes.

## Usage

Add the task before the color correction task, so the glow is added in linear space:

```cpp
const SdfPath colorCorrectionTask = taskManager->GetTaskPath(HdxPrimitiveTokens->colorCorrectionTask);

taskManager->AddTask<hvt::BloomTask>(hvt::BloomTask::GetToken(), hvt::BloomTaskParams(),
    fnCommit, colorCorrectionTask, hvt::TaskManager::InsertionOrder::insertBefore);
```

The commit function (`fnCommit`) reads the current `BloomTaskParams` with `fnGetValue`, edits
them from the application state, and writes them back with `fnSetValue`. See
[taskmgr.md](taskmgr.md) for the commit-function mechanics.

The task needs the color AOV and the intermediate color AOV in the task context. A frame pass
created with the default task list provides both.

## Tests

- Unit tests: `BloomTaskParams_*` in `test/tests/testTaskHelpers.cpp` (equality and defaults).
- How-to: `test/howTos/howTo23_UseBloomTask.cpp`, which renders with an image baseline
  (`test/data/baselines/howTo/useBloomTask_osx.png`).

## Limitations

- The blur is a single 3x3 pass, so a large `blurRadius` shows distinct ghost copies instead
  of a smooth glow.
- The baseline exists only for macOS (Metal). Other platforms need their own baseline.
