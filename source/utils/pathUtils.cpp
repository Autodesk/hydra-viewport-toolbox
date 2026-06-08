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
#include <cstdlib>
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

namespace HVT_NS
{

namespace
{

[[maybe_unused]] constexpr int EXE_PATH_SIZE { 2048 };

std::string CleanPath(std::string const& path)
{
    // Bypass canonicalisation for inputs that std::filesystem::canonical
    // either can't resolve (empty path) or that would round-trip to themselves
    // anyway (filesystem root).
    if (path.empty() || path == "/")
    {
        return path;
    }

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
#elif defined(__EMSCRIPTEN__)
        // No native executable on WASM.
        exe = "/";
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

// TODO: OGSMOD-7219 Standardize usage of lowercase "resource" folder (gizmos/assets).
[[maybe_unused]] constexpr const char* kResourceSubdir = "Resources";

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
    const char* localAppPath         = std::getenv("LOCAL_APP_PATH");
    std::filesystem::path assetsPath = localAppPath ? localAppPath : "";
    // TODO: OGSMOD-7219 Standardize usage of lowercase "resource" folder (gizmos/assets).
    return assetsPath.append("Resources");
#elif defined(__EMSCRIPTEN__)
    // WASM has no native executable directory. By convention, application
    // resources are mounted in the Emscripten virtual filesystem at /Resources/
    // via `--preload-file <src>@/Resources/...` link options. Callers that need
    // a different layout must use SetResourceDirectory() before any
    // Get*Path() call.
    return std::filesystem::path("/") / kResourceSubdir;
#else
    return GetCurrentProcessDirectory().append(kResourceSubdir);
#endif
}

std::filesystem::path GetDefaultMaterialXDirectory()
{
    // Root Location where MaterialX "libraries" are expected to be located.
    // For MacOS they are in Application Bundle/Contents/Frameworks and for others they are in the
    // same folder as the executable.
    // For iOS, Frameworks is under the root of bundle, like "Application Bundle/Frameworks"
#if defined(__APPLE__)
    NSBundle* bundle       = [NSBundle mainBundle];
    NSString* resourcePath = [bundle resourcePath];
#if !TARGET_OS_IPHONE
    resourcePath = [resourcePath stringByDeletingLastPathComponent];
#endif
    NSString* frameworkPath = [resourcePath stringByAppendingPathComponent:@"Frameworks"];
    return std::filesystem::path([frameworkPath UTF8String]);
#else
    return GetCurrentProcessDirectory();
#endif
}

} // namespace HVT_NS
