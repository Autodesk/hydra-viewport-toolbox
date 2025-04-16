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

#include <pxr/imaging/hd/driver.h>
#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/tokens.h>

#include <gtest/gtest.h>

TEST(test2, BasicAssertions)
{
#if defined(__APPLE__)
    auto hgi = pxr::Hgi::CreatePlatformDefaultHgi();
#else
    auto hgi = pxr::Hgi::CreateNamedHgi(pxr::HgiTokens->OpenGL);
#endif

    ASSERT_TRUE(hgi);
    ASSERT_TRUE(hgi->IsBackendSupported());
}
