//
// Copyright 2024 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//

#include <hvt/testFramework/testGlobalFlags.h>

namespace HVT_NS
{

namespace TestFramework
{

bool isRunningVulkan()
{
    static bool gIsRunningVulkan = false;
    return gIsRunningVulkan;
}

} // namespace TestFramework

} // namespace HVT_NS
