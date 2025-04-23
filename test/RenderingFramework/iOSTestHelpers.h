//
// Copyright 2023 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//

#include <filesystem>
#include <fstream>

#ifdef __APPLE__
#include "TargetConditionals.h"
#endif

namespace TestHelpers
{
std::pair<bool, std::string> getTestResult(const char* filePath);
std::string mainBundlePath();
std::string documentDirectoryPath();
} // namespace TestHelpers
