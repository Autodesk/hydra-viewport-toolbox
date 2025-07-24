//
// Copyright 2023 by Autodesk, Inc.  All rights reserved.
//
// This computer source code and related instructions and comments
// are the unpublished confidential and proprietary information of
// Autodesk, Inc. and are protected under applicable copyright and
// trade secret law.  They may not be disclosed to, copied or used
// by any third party without the prior written consent of Autodesk, Inc.
//

#pragma once

#if __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdeprecated-declarations"
    #pragma clang diagnostic ignored "-Wunused-parameter"
#elif defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable : 4244)
    #pragma warning(disable : 4305)
    #pragma warning(disable : 4275)
    #pragma warning(disable : 4100)
#endif

#include <pxr/base/gf/matrix4d.h>

#if __clang__
    #pragma clang diagnostic pop
#elif defined(_MSC_VER)
    #pragma warning(pop)
#endif

#include <cmath>
#include <sstream>
#include <type_traits>

namespace RenderingUtils
{

template <typename T>
void compare(const T& ref, const T& res, const char* filename, int line)
{
    size_t nbElems = 0;
    if constexpr (std::is_same<T, pxr::GfMatrix4d>::value)
    {
        nbElems = 16;
    }
    else if constexpr (std::is_same<T, pxr::GfVec3d>::value)
    {
        nbElems = 3;
    }
    else
    {
        throw std::runtime_error("Unsupported type!");
    }

    const double* ref_data = ref.data();
    const double* res_data = res.data();

    // The comparison is based on a range around the expected value.

    for (size_t idx = 0; idx < nbElems; ++idx)
    {
        const double fabs_ref  = std::fabs(ref_data[idx]);
        const double threshold = fabs_ref ? fabs_ref * 0.001 : 0.001;

        if ((ref_data[idx] - threshold) <= res_data[idx] &&
            res_data[idx] <= (ref_data[idx] + threshold))
        {
            continue;
        }

        std::stringstream err;
        err << filename << ":" << line << ": ";
        err << "ref[" << idx << "] = " << ref_data[idx] << " != ";
        err << "res[" << idx << "] = " << res_data[idx];
        throw std::runtime_error(err.str());
    }
}

/// Read a PNG image.
std::string readImage(const std::string& filePath, int& width, int& height, int& channels);

/// Compares two images using a threshold.
///
/// Returns true if the images are similar and throws an exception if more than the number pixels
/// defined by the pixelCountThreshhold differ by more than the threshold amount in one or more
/// channels.
bool compareImages(const std::string& filePath1, const std::string& filePath2,
    uint8_t threshold = 1, uint8_t pixelCountThreshold = 0);

} // namespace RenderingUtils

#define TST_ASSERT(a, b) RenderingUtils::compare(a, b, __FILE__, __LINE__)
