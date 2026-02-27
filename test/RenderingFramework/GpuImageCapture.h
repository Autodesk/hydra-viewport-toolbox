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

#pragma once

#include <hvt/engine/framePass.h>

#include <pxr/imaging/hgi/hgi.h>
#include <pxr/imaging/hgi/types.h>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace TestHelpers
{

/// \brief Captures the color AOV and writes it.
/// \note Captures the color AOV from a FramePass via HGI CopyTextureGpuToCpu and writes it to a PNG file.
class GpuImageCapture
{
public:
    GpuImageCapture() = default;

    /// Sets the flag to flip the image vertically.
    /// \param flip If true, flips the image vertically.
    void flipVertically(bool flip) { _flipVertically = flip; }
    /// Returns the flag to flip the image vertically.
    /// \return True if the image is flipped vertically.
    bool flipVertically() const { return _flipVertically; }

    /// Reads back the color AOV texture from the given FramePass to CPU memory.
    /// \note Must be called while the FramePass/RenderIndex instances are still alive.
    /// \param framePass The frame pass whose color AOV to capture.
    /// \param hgi The HGI instance to use for the blit command.
    /// \param width The image width in pixels.
    /// \param height The image height in pixels.
    void capture(hvt::FramePass* framePass, pxr::Hgi* hgi, int width, int height);

    /// Writes the captured pixel data to a PNG file.
    /// \param filePath The full output file path (including extension).
    /// \param width The image width in pixels.
    /// \param height The image height in pixels.
    /// \return True if the file was written successfully.
    bool writePng(std::filesystem::path const& filePath, int width, int height) const;

    /// Returns true if captured data is available.
    [[nodiscard]] bool hasCapturedData() const;

private:
    std::vector<uint8_t> _pixelData;
    pxr::HgiFormat _format = pxr::HgiFormatInvalid;
    bool _flipVertically = false;
};

} // namespace TestHelpers
