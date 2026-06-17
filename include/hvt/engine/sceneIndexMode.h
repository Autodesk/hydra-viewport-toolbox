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
#pragma once

#include <hvt/api.h>

namespace HVT_NS
{

/// Returns whether the scene-index (SI) backend is selected.
///
/// HVT supports two rendering backends:
///  - scene-index (SI): the Hydra 2.0 retained-scene-index based path (default);
///  - scene-delegate (SD): the legacy HdSceneDelegate based path.
///
/// The backend is selected at FramePass construction time. The default is SI, overridable through
/// the \c HVT_USE_SCENE_INDEX environment variable and at runtime via SetUseSceneIndex().
///
/// \note On USD versions that lack HdLegacyTaskSchema (e.g. 24.11) the SI backend cannot be built,
/// so this always returns false there regardless of the environment variable or SetUseSceneIndex().
HVT_API bool UseSceneIndex();

/// Selects the rendering backend used by FramePass instances created afterwards.
/// \param useSceneIndex true to use the scene-index (SI) backend, false for the scene-delegate (SD)
///        backend.
/// \note Has no effect on USD versions where the SI backend is unavailable (the backend then stays
/// SD). Already-constructed FramePass instances keep the backend they were created with.
HVT_API void SetUseSceneIndex(bool useSceneIndex);

} // namespace HVT_NS
