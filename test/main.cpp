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

#include <gtest/gtest.h>

#include <RenderingFramework/TestHelpers.h>
#include <RenderingFramework/UsdHelpers.h>
#include <RenderingFramework/TestFlags.h>

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);

    pxr::TfDiagnosticMgr::GetInstance().AddDelegate(new DiagnosticDelegate(""));

    int ret = EXIT_FAILURE;
    try
    {
        ret = RUN_ALL_TESTS();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Unexpected failure: " << ex.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Unexpected failure" << std::endl;
    }

    return ret;
}
