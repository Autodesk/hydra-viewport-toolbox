//
// Copyright 2025 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//

#include <RenderingUtils/ImageUtils.h>

#include <iomanip> // for std::precision()

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

#include <RenderingUtils/stb/stb_image.h>
#include <RenderingUtils/stb/stb_image_write.h>

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

bool compareImages(const std::string& filePath1, const std::string& filePath2, uint8_t threshold,
    uint16_t pixelCountThreshold)
{
    // A simple structure that reads a image file using STB.
    struct LoadPNG
    {
        // Constructor.
        explicit LoadPNG(const std::string& filePath, int& width, int& height, int& channels)
        {
            // Use STB to load the specified file as a RGBA image.
            pData = ::stbi_load(filePath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
            if (!pData)
            {
                throw std::runtime_error(std::string("Error for ") + filePath +
                    ": missing or corrupted file: " + stbi_failure_reason());
            }

            // Make sure the image has RGBA channels.
            if (channels != STBI_rgb_alpha)
            {
                throw std::runtime_error(
                    std::string("Error for ") + filePath + ": wrong number of channels");
            }
        }

        // Destructor.
        ~LoadPNG() { ::stbi_image_free(pData); }

        unsigned char* pData = nullptr;
    };

    // Function that compares data for two pixels and returns the maximum value difference between
    // them among the available channels. For example, [10,10,10,255] vs. [12,10,10,255] returns 2.
    auto fnDiff = [](uint8_t pix1[], uint8_t pix2[], int numChannels) -> int
    {
        int maxDiff = 0;
        for (int i = 0; i < numChannels; i++)
        {
            maxDiff = std::max(maxDiff, std::abs(pix2[i] - pix1[i]));
        }

        return maxDiff;
    };

    // Load the two input files as images.
    int width1 = -1, height1 = -1, channels1 = -1;
    LoadPNG file1(filePath1, width1, height1, channels1);
    int width2 = -1, height2 = -1, channels2 = -1;
    LoadPNG file2(filePath2, width2, height2, channels2);

    // Make sure the images have the same dimensions and number of channels.
    if (width1 != width2 || height1 != height2 || channels1 != channels2)
    {
        throw std::runtime_error("The images are incompatible");
    }

    // Get the pixel data for both images.
    uint8_t* pPix1 = file1.pData;
    uint8_t* pPix2 = file2.pData;

    // Iterate the image pixels and count the number of pixels that have one more channel values
    // that exceed the specified threshold, and determine the largest difference.
    int maxDiff        = 0;
    int countPixelDiff = 0;
    for (int y = 0; y < height1; ++y)
    {
        for (int x = 0; x < width1; ++x)
        {
            // Compute the largest difference among the pixel channel values and record whether it
            // exceeds the specified threshold.
            int currentDiff = fnDiff(pPix1, pPix2, channels1);
            if (currentDiff > threshold)
            {
                maxDiff = std::max(maxDiff, currentDiff);
                countPixelDiff++;
            }

            // Go to the data for the next pixel.
            pPix1 += 4;
            pPix2 += 4;
        }
    }

    // If the threshold was exceeded for more pixels than the pixel count threshold, then create a
    // readable string report and throw an exception with the report.
    if (countPixelDiff > pixelCountThreshold)
    {
        float percentDiff = (100.0f * countPixelDiff) / (width1 * height1);
        std::stringstream str;
        str << std::setprecision(2) << "Image comparison failed: " << countPixelDiff
            << " pixel(s) (" << percentDiff << "%) with max difference " << maxDiff << "/256.\n"
            << "\tBaseline: " << filePath2 << "\n\tComputed: " << filePath1;
        throw std::runtime_error(str.str());
    }

    return true;
}

} // namespace RenderingUtils
