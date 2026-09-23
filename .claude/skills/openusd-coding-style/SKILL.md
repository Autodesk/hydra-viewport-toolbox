---
name: openusd-coding-style
description: >-
  HVT/OpenUSD C++ house style. Use when writing or editing C++ in this repo
  (`.h`/`.cpp`/`.glslfx` under `include/hvt/` or `source/`) — license headers,
  clang-format rules, namespaces (HVT_NS/PXR_NS), token macros, member naming,
  and the HVT_API export macro. Read before creating a new source file or when
  matching house style.
---

# HVT / OpenUSD C++ coding style

Apply these when adding or editing C++ (`.h`/`.cpp`/`.glslfx`) in `include/hvt/` or `source/`.
Match the surrounding file first — these are the conventions it already follows.

## License header (required on every new file)

Every new `.cpp`/`.h`/`.glslfx` (and `CMakeLists.txt`, with `#` comments) begins with the
Apache-2.0 header, using the **current calendar year**. Copy from `source/tasks/aovInputTask.cpp`:

```cpp
// Copyright <current year> Autodesk, Inc.
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
```

## Formatting

`.clang-format` is authoritative (`.editorconfig` mirrors it for non-C++ files). Key rules that flow
from it:

- Microsoft base style, C++17, **column limit 100**.
- **East const** (`SdfPath const& uid`, `HgiTextureHandle const&`).
- **Pointer binds to type** (`HdSceneDelegate* delegate`, `HdTaskContext* ctx`).
- Consecutive `=` are aligned (`AlignConsecutiveAssignments`) — expect aligned assignment columns.
- Braced init has a space (`float blur { 8.0f };`).
- Do **not** disable `SortIncludes`. If a specific include order is required to compile, separate
  include blocks with a blank line and a short comment.

**Formatting scope — changed lines only, never whole files.** Many files in the tree carry
pre-existing formatting drift (pragma indentation, over-long lines, trailing whitespace) that
`clang-format -i <file>` would rewrite. Reformatting untouched lines pollutes the diff, buries the
semantic change, and creates review noise and merge conflicts. Before committing:

1. Format only the lines you touched, e.g.
   `clang-format -i --lines=<start>:<end> <file>` per edited hunk (or stage the change and use
   `git clang-format`/`clang-format-diff.py` on the staged diff).
2. Verify the result is format-clean for those ranges — e.g.
   `clang-format --lines=<start>:<end> <file> | diff - <file>` shows no difference.
3. Never run bare `clang-format -i <file>` on a file you did not (re)write entirely.

## Namespaces & tokens

- Library code lives in `namespace HVT_NS { … }` (generated in `include/hvt/namespace.h` at
  configure time — never hand-edit it).
- Reference Pixar types through the `PXR_NS::` macro in **headers** (`PXR_NS::HdxTask`,
  `PXR_NS::TfToken`, `PXR_NS::SdfPath`).
- In **`.cpp` files**, add `PXR_NAMESPACE_USING_DIRECTIVE` so the body can drop the `PXR_NS::`
  prefix.
- Define private/shader token names with `TF_DEFINE_PRIVATE_TOKENS(_tokens, …)` in the `.cpp`.
- Wrap noisy Pixar includes in `#pragma` diagnostic push/ignored/pop blocks guarded by
  `// clang-format off` / `// clang-format on` (see `blurTask.h`).

## Naming

- Private/protected data members: leading underscore (`_params`, `_pipeline`, `_shaderProgram`).
- Private/protected/virtual methods: leading underscore + PascalCase (`_Sync`, `_CreateShaderResources`).
- Explicitly `= delete` unused default/copy/copy-assign special members.

## Export macro (`HVT_API`, from `include/hvt/api.h`)

- Every **public API class and struct** in `include/hvt/` that is compiled into the library must be
  declared with `HVT_API`, e.g. `class HVT_API BlurTask`, `struct HVT_API BlurTaskParams`.
- Put `HVT_API` on the params' **free operators** declared in the header
  (`HVT_API bool operator==(BlurTaskParams const&, BlurTaskParams const&);` and `operator!=`,
  `operator<<`).
- **Exceptions:** templates and header-only/inline types must omit `HVT_API` (MSVC C2491) — see
  `pageableConcepts.h`, `pageableStrategies.h`, `geometry.h`. Scene-index filter subclasses of
  `HdSingleInputFilteringSceneIndexBase` put `HVT_API` on public/protected members instead of the
  class — see `include/hvt/sceneIndex/boundingBoxSceneIndex.h`.

## See also

- Creating a task end-to-end: the `create-hvt-task` skill.
- Baseline conventions summary: [AGENTS.md](../../../AGENTS.md) → Conventions.
