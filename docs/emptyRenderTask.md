# EmptyRenderTask

## Overview

`EmptyRenderTask` is a no-op `HdxTask` shipped with HVT as a **starting point** for custom render
work. It follows the same lifecycle as production tasks (`_Sync`, `Prepare`, `Execute`) but
performs no GPU work until you extend it.

Use it when you want a registered task slot in the Hydra pipeline (ordering, commit functions,
params on the scene delegate) without copying a full post-process implementation such as
`BlurTask`.

For a complete custom task with shaders and image processing, see [howTo04](../test/howTos/howTo04_CreateACustomRenderTask.cpp) and `BlurTask`.

---

## API

| Symbol | Location |
|--------|----------|
| `EmptyRenderTask`, `EmptyRenderTaskParams` | `include/hvt/tasks/emptyRenderTask.h` |
| Implementation | `source/tasks/emptyRenderTask.cpp` |
| Task token | `EmptyRenderTask::GetToken()` → `"emptyRenderTask"` |

`EmptyRenderTaskParams` is intentionally empty; add fields as your task grows and keep
`operator==` in sync for dirty tracking (see other tasks under `include/hvt/tasks/`).

---

## Registration

Register through `TaskManager::AddTask` like any other HVT task. Typical placement is immediately
before `presentTask` so future GPU work runs after the main color pipeline:

```cpp
#include <hvt/tasks/emptyRenderTask.h>

const pxr::SdfPath presentTask =
    framePass->GetTaskManager()->GetTaskPath(pxr::HdxPrimitiveTokens->presentTask);

framePass->GetTaskManager()->AddTask<hvt::EmptyRenderTask>(
    hvt::EmptyRenderTask::GetToken(),
    hvt::EmptyRenderTaskParams(),
    fnCommit,
    presentTask,
    hvt::TaskManager::InsertionOrder::insertBefore);
```

See [taskmgr.md](taskmgr.md) for commit functions and ordering.

---

## Tests and examples

| Kind | Location |
|------|----------|
| Params / token unit tests | `test/tests/testTaskHelpers.cpp` |
| Integration How-to | `test/howTos/howTo23_UseEmptyRenderTask.cpp` |

The How-to reuses the `howTo/createOneFramePass` image baseline because the scaffold does not
change the framebuffer.

---

## Extending the scaffold

1. Add fields to `EmptyRenderTaskParams` and update `operator==` / `operator<<`.
2. Read or write `HdTaskContext` texture tokens in `Prepare` / `Execute` (see `BlurTask` and
   [framepass.md](framepass.md)).
3. Add GLSLFX under `include/hvt/resources/shaders/` if you need a fullscreen or compute pass.
4. Add unit tests in `test/tests/` and image baselines if behavior becomes visible.
