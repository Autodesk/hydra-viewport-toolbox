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

#include <RenderingFramework/ImageUtils.h>

#include <pxr/base/tf/getenv.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip> // for std::setprecision()
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string_view>

#if !defined(_WIN32)
    #include <sys/wait.h> // for WIFEXITED() and WEXITSTATUS()
#endif

// For STB headers, disable warnings for deprecated symbols.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif

#include <RenderingFramework/stb/stb_image.h>
#include <RenderingFramework/stb/stb_image_write.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace RenderingUtils
{

std::string readImage(const std::string& filePath, int& width, int& height, int& channels)
{
    unsigned char* raw = stbi_load(filePath.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!raw)
    {
        throw std::runtime_error("Texture File Loading failed");
    }

    std::string data;
    data.resize(width * height * channels);
    memcpy((void*)data.c_str(), (void*)raw, data.length()); // nosec

    return data;
}

bool writeImage(const std::filesystem::path& filePath, int width, int height,
    const std::vector<uint8_t>& rgbaPixels, bool flipVertically)
{
    const std::filesystem::path directory = filePath.parent_path();
    if (!std::filesystem::exists(directory))
    {
        if (!std::filesystem::create_directories(directory))
        {
            throw std::runtime_error(
                std::string("Failed to create the directory: ") + directory.string());
        }
    }
    std::filesystem::remove(filePath);

    stbi_flip_vertically_on_write(flipVertically ? 1 : 0);
    const bool result =
        stbi_write_png(filePath.string().c_str(), width, height, 4, rgbaPixels.data(), 0);
    stbi_flip_vertically_on_write(0);

    return result;
}

namespace
{

// The result of a command line execution.
struct CommandResult
{
    int exitCode = -1;
    std::string output; // stdout and stderr, interleaved.
};

// Quotes \p arg for the shell that popen() uses.
std::string quoted(std::string const& arg)
{
    return "\"" + arg + "\"";
}

// Runs \p command through the shell and captures its output.
CommandResult runCommand(std::string command)
{
    command += " 2>&1";
#if defined(_WIN32)
    // cmd.exe strips the first and last quotes of the whole command line when it starts with one.
    command    = "\"" + command + "\"";
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe)
    {
        throw std::runtime_error("Failed to run: " + command);
    }

    CommandResult result;
    std::array<char, 512> buffer {};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe))
    {
        result.output += buffer.data();
    }

#if defined(_WIN32)
    result.exitCode = _pclose(pipe);
#else
    const int status = pclose(pipe);
    result.exitCode  = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    return result;
}

// The idiff executable: the HVT_IDIFF environment variable, else the one found at configure time.
std::string const& idiffPath()
{
    static const std::string path = pxr::TfGetenv("HVT_IDIFF", HVT_IDIFF_PATH);
    return path;
}

// The OpenImageIO version of idiff, as a comparable integer (e.g., 30109 for 3.1.9), or 0 when
// unknown.
int idiffVersion()
{
    static const int version = []()
    {
        // idiff prints "OpenImageIO <major>.<minor>.<patch>" in its help text.
        const std::string output = runCommand(quoted(idiffPath()) + " --help").output;
        static const std::regex pattern(R"(OpenImageIO\s+(\d+)\.(\d+)\.(\d+))");
        std::smatch match;
        if (!std::regex_search(output, match, pattern))
        {
            return 0;
        }
        return std::stoi(match[1]) * 10000 + std::stoi(match[2]) * 100 + std::stoi(match[3]);
    }();
    return version;
}

// Throws unless idiff is from OpenImageIO 3.1.9 or later: its perceptual comparison was broken
// before. Also checked at configure time, but HVT_IDIFF can name another executable.
void checkIdiffVersion()
{
    static constexpr int kMinimumVersion = 30109;
    if (idiffVersion() < kMinimumVersion)
    {
        throw std::runtime_error("The idiff executable '" + idiffPath() +
            "' must be from OpenImageIO 3.1.9 or later (set HVT_IDIFF to another one).");
    }
}

// The path where idiff writes the difference image: next to the computed image.
std::string diffImagePath(std::string const& filePath1, std::string const& filePath2)
{
    static constexpr std::string_view kComputed = "_computed.png";
    auto endsWithComputed                       = [](std::string const& path)
    {
        return path.size() >= kComputed.size() &&
            path.compare(path.size() - kComputed.size(), kComputed.size(), kComputed) == 0;
    };

    const std::string& computed = endsWithComputed(filePath2) ? filePath2 : filePath1;
    const std::string stem      = endsWithComputed(computed)
        ? computed.substr(0, computed.size() - kComputed.size())
        : std::filesystem::path(computed).replace_extension().string();
    return stem + "_diff.png";
}

} // anonymous namespace

bool compareImages(const std::string& filePath1, const std::string& filePath2, uint8_t threshold,
    uint16_t pixelCountThreshold, bool perceptual)
{
    checkIdiffVersion();

    for (std::string const& path : { filePath1, filePath2 })
    {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
        {
            throw std::runtime_error("Image comparison failed: missing file " + path);
        }
    }

    // A pixel fails when one of its channels differs by more than 'threshold' (in 8-bit units),
    // and the comparison fails when more than 'pixelCountThreshold' pixels fail. The half unit
    // keeps a difference of exactly 'threshold' passing despite the conversion to [0, 1].
    std::ostringstream limit;
    limit << std::setprecision(9) << (threshold + 0.5) / 255.0;

    const std::string diffImage = diffImagePath(filePath1, filePath2);
    std::string command         = quoted(idiffPath());
    command += " -fail " + limit.str();
    // Warnings use the same limit, so that only failures are reported.
    command += " -warn " + limit.str();
    command += " -allowfailures " + std::to_string(pixelCountThreshold);
    if (perceptual)
    {
        // Replaces the per-channel test: idiff then only counts the perceptually different pixels.
        command += " -p";
    }
    // Only written when the images differ; the difference is amplified to make it visible.
    command += " -abs -scale 20 -od -o " + quoted(diffImage);
    command += " " + quoted(filePath1) + " " + quoted(filePath2);

    // The idiff exit codes.
    enum
    {
        kPass     = 0,
        kWarning  = 1,
        kFailure  = 2,
        kDiffSize = 3,
        kError    = 4
    };

    const CommandResult result = runCommand(command);
    if (result.exitCode == kPass || result.exitCode == kWarning)
    {
        return true;
    }

    std::ostringstream str;
    switch (result.exitCode)
    {
    case kFailure:
        str << "Image comparison failed";
        break;
    case kDiffSize:
        str << "The images are incompatible (different sizes or channels)";
        break;
    case kError:
        str << "Image comparison failed to read an image";
        break;
    default:
        str << "Failed to run idiff (exit code " << result.exitCode
            << "; set HVT_IDIFF to its path)";
        break;
    }
    if (perceptual)
    {
        str << " (perceptual, " << pixelCountThreshold << " pixel(s) allowed).\n";
    }
    else
    {
        str << " (max " << static_cast<int>(threshold) << "/255 per channel, "
            << pixelCountThreshold << " pixel(s) allowed).\n";
    }
    str << "\tImage 1: " << filePath1 << "\n\tImage 2: " << filePath2 << "\n";
    if (result.exitCode == kFailure)
    {
        str << "\tDiff:    " << diffImage << "\n";
    }
    str << "\t" << command << "\n" << result.output;
    throw std::runtime_error(str.str());
}

} // namespace RenderingUtils
