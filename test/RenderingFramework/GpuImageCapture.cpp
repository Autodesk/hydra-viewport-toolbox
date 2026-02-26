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

#include <RenderingFramework/GpuImageCapture.h>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4003)
#pragma warning(disable : 4100)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif

#include <pxr/base/gf/half.h>
#include <pxr/imaging/hgi/blitCmds.h>
#include <pxr/imaging/hgi/blitCmdsOps.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

#if defined(_WIN32)
#define STBI_MSC_SECURE_CRT
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif

#include <RenderingUtils/stb/stb_image_write.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <cstring>
#include <stdexcept>

namespace
{

std::vector<uint8_t> convertToRGBA8(
    const uint8_t* srcData, size_t pixelCount, pxr::HgiFormat srcFormat)
{
    std::vector<uint8_t> rgba(pixelCount * 4);

    auto clampToByte = [](float v) -> uint8_t
    {
        return static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, v * 255.0f + 0.5f)));
    };

    switch (srcFormat)
    {
    case pxr::HgiFormatFloat16Vec4:
    {
        auto* src = reinterpret_cast<const pxr::GfHalf*>(srcData);
        for (size_t i = 0; i < pixelCount; ++i)
        {
            rgba[i * 4 + 0] = clampToByte(static_cast<float>(src[i * 4 + 0]));
            rgba[i * 4 + 1] = clampToByte(static_cast<float>(src[i * 4 + 1]));
            rgba[i * 4 + 2] = clampToByte(static_cast<float>(src[i * 4 + 2]));
            rgba[i * 4 + 3] = clampToByte(static_cast<float>(src[i * 4 + 3]));
        }
        break;
    }
    case pxr::HgiFormatFloat32Vec4:
    {
        auto* src = reinterpret_cast<const float*>(srcData);
        for (size_t i = 0; i < pixelCount; ++i)
        {
            rgba[i * 4 + 0] = clampToByte(src[i * 4 + 0]);
            rgba[i * 4 + 1] = clampToByte(src[i * 4 + 1]);
            rgba[i * 4 + 2] = clampToByte(src[i * 4 + 2]);
            rgba[i * 4 + 3] = clampToByte(src[i * 4 + 3]);
        }
        break;
    }
    case pxr::HgiFormatUNorm8Vec4:
    {
        std::memcpy(rgba.data(), srcData, pixelCount * 4);
        break;
    }
    default:
        throw std::runtime_error(
            "GpuImageCapture: unsupported render texture format for screenshot conversion");
    }

    return rgba;
}

} // anonymous namespace

namespace TestHelpers
{

void GpuImageCapture::capture(hvt::FramePass* framePass, pxr::Hgi* hgi, int width, int height)
{
    _pixelData.clear();
    _format = pxr::HgiFormatInvalid;

    if (!framePass)
    {
        throw std::runtime_error("GpuImageCapture::capture: FramePass is null");
    }

    if (!hgi)
    {
        throw std::runtime_error("GpuImageCapture::capture: Hgi instance is null");
    }

    auto colorTex = framePass->GetRenderTexture(pxr::HdAovTokens->color);
    if (!colorTex)
    {
        throw std::runtime_error(
            "GpuImageCapture::capture: color render texture not available from FramePass");
    }

    const pxr::HgiFormat srcFormat = colorTex->GetDescriptor().format;
    const pxr::GfVec3i dims(width, height, 1);
    const size_t srcByteSize = pxr::HgiGetDataSize(srcFormat, dims);

    _pixelData.resize(srcByteSize, 0);

    pxr::HgiTextureGpuToCpuOp readBackOp {};
    readBackOp.cpuDestinationBuffer      = _pixelData.data();
    readBackOp.destinationBufferByteSize = srcByteSize;
    readBackOp.destinationByteOffset     = 0;
    readBackOp.gpuSourceTexture          = colorTex;
    readBackOp.mipLevel                  = 0;
    readBackOp.sourceTexelOffset         = pxr::GfVec3i(0);

    pxr::HgiBlitCmdsUniquePtr blitCmds = hgi->CreateBlitCmds();
    blitCmds->CopyTextureGpuToCpu(readBackOp);
    hgi->SubmitCmds(blitCmds.get(), pxr::HgiSubmitWaitType::HgiSubmitWaitTypeWaitUntilCompleted);

    _format = srcFormat;
}

bool GpuImageCapture::writePng(
    const std::filesystem::path& filePath, int width, int height) const
{
    if (!hasCapturedData())
    {
        throw std::runtime_error(
            "GpuImageCapture::writePng: no captured data (capture() must be called first)");
    }

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

    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint8_t> rgbaPixels = convertToRGBA8(_pixelData.data(), pixelCount, _format);

    stbi_flip_vertically_on_write(_flipVertically ? 1 : 0);
    const bool result =
        stbi_write_png(filePath.string().c_str(), width, height, 4, rgbaPixels.data(), 0);
    stbi_flip_vertically_on_write(0);

    return result;
}

bool GpuImageCapture::hasCapturedData() const
{
    return !_pixelData.empty() && _format != pxr::HgiFormatInvalid;
}

} // namespace TestHelpers
