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

#include "pathUtils.h"

#include <codecvt>
#include <locale>
#include <sstream>

#ifdef __APPLE__
    #include "TargetConditionals.h"

    #include <dlfcn.h>
    #include <libgen.h>
    #include <mach-o/dyld.h>
    #include <unistd.h>
    #define SEP '/'
    #import <Foundation/Foundation.h>
#endif

#ifdef __linux
    #include <dlfcn.h>
    #include <libgen.h>
    #include <stdio.h>
    #include <string.h>
    #include <unistd.h>
    #define SEP '/'
#endif

#ifdef _WIN32
    #include <Shlobj.h>
    #include <Shlwapi.h>
    #include <Windows.h>
    #pragma comment(lib, "shell32.lib")
    #pragma comment(lib, "shlwapi.lib")
    #define SEP '\\'
#endif

namespace hvt
{

namespace
{

constexpr int EXE_PATH_SIZE { 2048 };

std::string CleanPath(std::string const& path)
{
    const auto clean = std::filesystem::absolute(path);
    return std::filesystem::canonical(clean).string();
}

std::string GetExecutable()
{
    static std::string exe;
    if (exe.empty())
    {
#if defined(_WIN32)
#pragma warning(push)
#pragma warning(disable : 4996)
        WCHAR path[EXE_PATH_SIZE] = L"";
        // Windows use module path
        GetModuleFileNameW(NULL, path, sizeof(path) / sizeof(path[0]));
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        exe = converter.to_bytes(path);
#pragma warning(pop)
#elif defined(__APPLE__)
        char path[EXE_PATH_SIZE] = "";
        // OSX use NS exe path
        uint32_t size = EXE_PATH_SIZE;
        _NSGetExecutablePath(path, &size);
        exe = path;
#elif defined(__linux)
        char path[EXE_PATH_SIZE] = "";
        // Linux read process exe.
        const ssize_t size = readlink("/proc/self/exe", path, EXE_PATH_SIZE - 1);
        if (size != -1)
        {
            // For safety, add the null terminator when buffer is truncated to its
            // maximum size.
            path[size] = '\0';
            exe        = path;
        }
#endif

        // Clean up
        exe = CleanPath(exe);
    }
    return exe;
}

[[maybe_unused]] std::filesystem::path GetCurrentProcessDirectory()
{
    const std::string exePath = GetExecutable();
    return std::filesystem::path(exePath).parent_path();
}

} // anonymous namespace

std::filesystem::path GetDefaultResourceDirectory()
{
#if defined(__APPLE__)
#if (TARGET_OS_IPHONE == 1)
    std::filesystem::path exePath = GetCurrentProcessDirectory();
    return exePath.append("data");
#else
    NSString* resourcePath = [[NSBundle mainBundle] resourcePath];
    return std::filesystem::path([resourcePath UTF8String]);
#endif
#elif defined(__ANDROID__)
    std::filesystem::path assetsPath = getenv("LOCAL_APP_PATH");
    return assetsPath.append(
        "Resources"); // FIXME: OGSMOD-7219
                      // Standardize usage of lowercase "resource" folder (gizmos/assets).
#else
    std::filesystem::path exePath = GetCurrentProcessDirectory();
    return exePath.append(
        "Resources"); // FIXME: OGSMOD-7219
                      // Standardize usage of lowercase "resource" folder (gizmos/assets).
#endif
}

std::filesystem::path GetDefaultMaterialXDirectory()
{
    // Root Location where MaterialX "libraries" are expected to be located.
    // For MacOS they are in Application Bundle/Contents/Frameworks and for others they are in the
    // same folder as the executable.
#if defined(__APPLE__)
    NSString* frameworkPath = [[[[NSBundle mainBundle] resourcePath]
        stringByDeletingLastPathComponent] stringByAppendingPathComponent:@"Frameworks"];
    return std::filesystem::path([frameworkPath UTF8String]);
#else
    return GetCurrentProcessDirectory();
#endif
}

} // namespace hvt
