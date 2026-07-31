---
name: create-hvt-task
description: >-
  How to add a new HVT render/compute task. Use when creating a new HdxTask
  subclass or post-processing task (blur/SSAO/FXAA-style) — the file layout,
  HdxTask lifecycle, params struct, TaskManager registration, HdTaskContext
  wiring, shaders, CMake wiring, and the mandatory unit tests + docs. A task is
  never done without code, tests, AND docs. Read before adding a task.
---

# Create a new HVT task

Adding a custom render/compute task. Copy an existing task as the template — `blurTask`
(`include/hvt/tasks/blurTask.h` + `source/tasks/blurTask.cpp`) is the canonical fullscreen
post-process example; `howTo04_CreateACustomRenderTask.cpp` shows wiring it into a frame pass.
Follow the `openusd-coding-style` skill for headers, naming, and formatting.

## Definition of done — code + tests + docs are all mandatory

A new task is complete **only when all three land together**: the **code** (header/impl/CMake),
the **unit tests**, and the **docs** (How-to + index entry). There is no "code-only" or
"skeleton-only" task in this repo. If you are explicitly asked for just a skeleton, deliver it but
**call out the still-required tests and docs as follow-ups** and treat the task as unfinished until
they exist — never present code alone as complete.

## Files to create

- **Header:** `include/hvt/tasks/<name>Task.h` (or `include/hvt/tasks/outline/…` for a subgroup).
- **Implementation:** `source/tasks/<name>Task.cpp`.
- **Shader (if any):** `include/hvt/resources/shaders/<name>.glslfx`.
- **Private headers** (tokens, compute helpers) go under `source/tasks/`.

## Class shape

`class HVT_API <Name>Task : public PXR_NS::HdxTask` with:

- A params struct `struct HVT_API <Name>TaskParams` — public fields with default initializers,
  plus free operators declared in the header (`HVT_API bool operator==`, `operator!=`,
  `operator<<`). `operator==` drives dirty tracking.
- `static const PXR_NS::TfToken& GetToken();` — the task name used for registration.
- The three lifecycle overrides:

```cpp
void _Sync(HdSceneDelegate* delegate, HdTaskContext* ctx, HdDirtyBits* dirtyBits) override;
void Prepare(HdTaskContext* ctx, HdRenderIndex* renderIndex) override;
void Execute(HdTaskContext* ctx) override;
```

- `_Sync`: when `dirtyBits & HdChangeTracker::DirtyParams`, read params from the delegate; **clear
  the dirty bits on every return path** (including early returns) so Hydra stops re-syncing.
- `Prepare`: preprocessing / resource setup before execution.
- `Execute`: run the shader / composite passes.
- Explicitly `= delete` the default and copy special members.

## Wiring & registration

- Pass data between tasks through `HdTaskContext` **texture tokens** (e.g. `color`,
  `colorIntermediate`), not direct pointers. Fullscreen post-processes typically read `color` and
  ping-pong to `colorIntermediate`.
- Register with the `TaskManager` (see `include/hvt/engine/taskManager.h` and
  `taskCreationHelpers.h`):

```cpp
taskManager->AddTask<hvt::<Name>Task>(
    hvt::<Name>Task::GetToken(), hvt::<Name>TaskParams(), fnCommit,
    anchorPath, hvt::TaskManager::InsertionOrder::insertBefore);
```

- `fnCommit(GetTaskValueFn, SetTaskValueFn)` runs each frame: read `HdTokens->params`, update
  fields from app/render state, write back with `fnSetValue(HdTokens->params, VtValue(params))`.
- Find an anchor with `GetTaskManager()->GetTaskPath(<existing task token>)` and place the new
  task with `InsertionOrder::insertBefore` / `insertAfter` / `insertAtEnd`. Pipeline placement
  (e.g. before vs after color correction) affects the result — choose deliberately.

## CMake wiring (required — no globbing)

`source/tasks/CMakeLists.txt` uses **explicit file lists**. Add:

- the `.cpp` to `set(_SOURCE_FILES …)` (relative, e.g. `"<name>Task.cpp"` or `"outline/<name>.cpp"`);
- the public header to `set(_HEADER_FILES "${_TASKS_INCLUDE_DIR}/<name>Task.h")`;
- any private header to `set(_PRIVATE_HEADER_FILES …)`.

## Tests and docs (mandatory)

Every new task requires **both** tests and docs — this is not conditional on the task being
"user-facing." Code without these is an incomplete task.

**Unit tests** (`test/tests/`), per [docs/testing.md](../../../docs/testing.md):

- a `*ParamsEquality`/defaults test and a construction test via `TaskManager`;
- a rendered-image test with a baseline in `test/data/baselines/` where applicable (note
  Apple/Metal `primId` skips).

**Docs:**

- a **How-to** in `test/howTos/` showing minimal end-to-end usage — register it in
  `test/CMakeLists.txt` and list it in `test/README.md`;
- add the task to the docs index [docs/README.md](../../../docs/README.md) (the Features /
  render-tasks tables), and add a dedicated `docs/<feature>.md` for anything non-trivial.
