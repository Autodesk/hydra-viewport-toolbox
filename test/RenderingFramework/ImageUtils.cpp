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

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/oiioversion.h>

#include <cstring>
#include <filesystem>
#include <iomanip> // for std::setprecision()
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

// The perceptual comparison (compare_Yee) was broken before 3.1.9.
#if OIIO_VERSION < OIIO_MAKE_VERSION(3, 1, 9)
    #error "The image comparison requires OpenImageIO 3.1.9 or later."
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

// Reads an image for a comparison, as floats in [0, 1].
OIIO::ImageBuf readForComparison(std::string const& filePath)
{
    OIIO::ImageBuf image(filePath);
    if (!image.read(0, 0, true, OIIO::TypeFloat))
    {
        throw std::runtime_error(
            "Image comparison failed to read " + filePath + ": " + image.geterror());
    }
    return image;
}

// The path of the difference image: next to the computed image.
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

// Writes the absolute difference of the images, amplified to make it visible. The alpha channel, if
// any, is made opaque: the difference of two opaque images would otherwise be transparent.
void writeDiffImage(
    OIIO::ImageBuf const& image1, OIIO::ImageBuf const& image2, std::string const& filePath)
{
    OIIO::ImageBuf diff = OIIO::ImageBufAlgo::absdiff(image1, image2);
    diff                = OIIO::ImageBufAlgo::mul(diff, 20.0f);
    if (diff.nchannels() == 4)
    {
        diff = OIIO::ImageBufAlgo::channels(
            diff, 4, std::vector<int> { 0, 1, 2, -1 }, std::vector<float> { 0, 0, 0, 1 });
    }
    if (!diff.write(filePath, OIIO::TypeUInt8))
    {
        std::cerr << "Failed to write the difference image " << filePath << ": " << diff.geterror()
                  << std::endl;
    }
}

} // anonymous namespace

bool compareImages(const std::string& filePath1, const std::string& filePath2, uint8_t threshold,
    uint16_t pixelCountThreshold, bool perceptual)
{
    for (std::string const& path : { filePath1, filePath2 })
    {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
        {
            throw std::runtime_error("Image comparison failed: missing file " + path);
        }
    }

    const OIIO::ImageBuf image1 = readForComparison(filePath1);
    const OIIO::ImageBuf image2 = readForComparison(filePath2);

    std::ostringstream str;
    OIIO::ImageSpec const& spec1 = image1.spec();
    OIIO::ImageSpec const& spec2 = image2.spec();
    if (spec1.width != spec2.width || spec1.height != spec2.height ||
        spec1.nchannels != spec2.nchannels)
    {
        str << "The images are incompatible: " << spec1.width << "x" << spec1.height << "x"
            << spec1.nchannels << " vs " << spec2.width << "x" << spec2.height << "x"
            << spec2.nchannels << ".\n\tImage 1: " << filePath1 << "\n\tImage 2: " << filePath2
            << "\n";
        throw std::runtime_error(str.str());
    }

    // A pixel fails when one of its channels differs by more than 'threshold' (in 8-bit units),
    // and the comparison fails when more than 'pixelCountThreshold' pixels fail. The half unit
    // keeps a difference of exactly 'threshold' passing despite the conversion to [0, 1].
    const float limit = (threshold + 0.5f) / 255.0f;
    OIIO::ImageBufAlgo::CompareResults result =
        OIIO::ImageBufAlgo::compare(image1, image2, limit, limit);
    if (perceptual)
    {
        // Replaces the per-channel test, as in idiff: only the perceptually different pixels fail.
        result = {};
        OIIO::ImageBufAlgo::compare_Yee(image1, image2, result);
    }
    // NOTE: result.error is not checked: compare() also sets it when any pixel fails, and the read
    // and size errors are handled above.
    if (result.nfail <= pixelCountThreshold)
    {
        return true;
    }

    const std::string diffImage = diffImagePath(filePath1, filePath2);
    writeDiffImage(image1, image2, diffImage);

    const double pixelCount = static_cast<double>(spec1.width) * spec1.height;
    str << "Image comparison failed";
    if (perceptual)
    {
        str << " (perceptual, " << pixelCountThreshold << " pixel(s) allowed): " << result.nfail
            << " pixel(s) differ";
    }
    else
    {
        str << " (max " << static_cast<int>(threshold) << "/255 per channel, "
            << pixelCountThreshold << " pixel(s) allowed): " << result.nfail << " pixel(s) differ";
    }
    str << " (" << std::setprecision(3) << 100.0 * result.nfail / pixelCount << "%), max error "
        << std::setprecision(4) << result.maxerror * 255.0 << "/255 at (" << result.maxx << ", "
        << result.maxy << ") in channel " << result.maxc << ".\n";
    str << "\tImage 1: " << filePath1 << "\n\tImage 2: " << filePath2 << "\n";
    str << "\tDiff:    " << diffImage << "\n";
    throw std::runtime_error(str.str());
}

} // namespace RenderingUtils
