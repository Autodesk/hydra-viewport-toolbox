//
// Copyright 2024 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//

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
