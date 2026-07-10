// Copyright 2026 Autodesk, Inc.
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

#ifndef HVT_OUTLINE_PICK_ID_CONFIG_H
#define HVT_OUTLINE_PICK_ID_CONFIG_H

#include <pxr/pxr.h>

// Selects how the outline pick path stores prim IDs, in lockstep across the pick
// shader technique, the primId AOV, and the mask input textures.
//
// On USD 24.11 (PXR_VERSION 2411) and older, sampling an integer (R32I) AOV via
// texelFetch inside a compute shader returns garbage for non-zero values on HgiGL,
// so the primId is stored as a float (R32F) and rounded back to int in the mask
// shader. USD 25.x+ reads integer AOVs correctly and uses the integer path.
//
// Touchpoints that branch on this switch:
//   1. outlinePrimIdsTask.cpp - pick shader technique + primId AOV format/clear
//   2. outlineMaskTask.cpp    - primId input texture format
// The mask shader itself is version-agnostic: decodePrimId() reads either format.
#if PXR_VERSION <= 2411
#  define HVT_OUTLINE_PICK_ID_AS_FLOAT 1
#else
#  define HVT_OUTLINE_PICK_ID_AS_FLOAT 0
#endif

#endif // HVT_OUTLINE_PICK_ID_CONFIG_H
