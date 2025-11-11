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

#include <RenderingFramework/iOSTestHelpers.h>

#import <Foundation/Foundation.h>

namespace TestHelpers
{

std::pair<bool, std::string> getTestResult(const char* filePath)
{
    std::ifstream file(filePath, std::ios::in | std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Cannot open stdout.");
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    if (content.find("FAILED TEST") != std::string::npos)
    {
        return std::make_pair(false, content);
    }
    else
    {
        return std::make_pair(true, content);
    }
}

std::string mainBundlePath()
{
    return [[[NSBundle mainBundle] bundlePath] UTF8String];
}

std::string documentDirectoryPath()
{
    return [[NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES) objectAtIndex:0] UTF8String];
}
} // namespace TestHelpers
