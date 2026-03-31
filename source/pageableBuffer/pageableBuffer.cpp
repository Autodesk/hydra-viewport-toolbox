// Copyright 2026 Autodesk, Inc.
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
#include <hvt/pageableBuffer/pageableBuffer.h>

#include <hvt/pageableBuffer/pageFileManager.h>
#include <hvt/pageableBuffer/pageableBufferManager.h>
#include <hvt/pageableBuffer/pageableMemoryMonitor.h>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/stringUtils.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

HdPageableBufferCore::HdPageableBufferCore(size_t size, HdBufferUsage usage,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
    DestructionCallback destructionCallback) :
    mUsage(usage),
    mSize(size),
    mDestructionCallback(std::move(destructionCallback)),
    mPageFileManager(const_cast<std::unique_ptr<HdPageFileManager>&>(pageFileManager)),
    mMemoryMonitor(const_cast<std::unique_ptr<HdMemoryMonitor>&>(memoryMonitor))
{
    CreateSceneBuffer();
}

HdPageableBufferCore::~HdPageableBufferCore()
{
    ReleaseDiskPage();
    ReleaseSceneBuffer();
    ReleaseRendererBuffer();

    // Notify HdPageableBufferManager of removing from the list.
    if (mDestructionCallback)
    {
        mDestructionCallback();
    }
}

// std::span-based memory access methods
TfSpan<const std::byte> HdPageableBufferCore::GetSceneMemorySpan() const noexcept
{
    return {};
}

TfSpan<std::byte> HdPageableBufferCore::GetSceneMemorySpan() noexcept
{
    return {};
}

TfSpan<const std::byte> HdPageableBufferCore::GetRendererMemorySpan() const noexcept
{
    return {};
}

TfSpan<std::byte> HdPageableBufferCore::GetRendererMemorySpan() noexcept
{
    return {};
}

bool HdPageableBufferCore::PageToSceneMemory(bool /*force*/)
{
    if (HasSceneBuffer())
    {
        return true; // Already in scene memory
    }

    CreateSceneBuffer();

    // Try to load from disk first.
    if (!HasValidDiskBuffer() ||
        !mPageFileManager->LoadPage(*mPageEntry, GetSceneMemorySpan().data()))
    {
        if (HasRendererBuffer())
        {
            // Copy from hardware memory
            auto srcSpan = GetRendererMemorySpan();
            auto dstSpan = GetSceneMemorySpan();
            std::copy(srcSpan.begin(), srcSpan.end(), dstSpan.begin());
        }
        else
        {
            ReleaseSceneBuffer();
            return false;
        }
    }

    return true;
}

bool HdPageableBufferCore::PageToRendererMemory(bool /*force*/)
{
    if (HasRendererBuffer())
    {
        return true; // Already in hardware memory
    }

    CreateRendererBuffer();

    if (HasSceneBuffer())
    {
        // Copy from scene memory
        auto srcSpan = GetSceneMemorySpan();
        auto dstSpan = GetRendererMemorySpan();
        std::copy(srcSpan.begin(), srcSpan.end(), dstSpan.begin());
    }
    else
    {
        // Try to load from disk.
        if (!HasValidDiskBuffer() ||
            !mPageFileManager->LoadPage(*mPageEntry, GetRendererMemorySpan().data()))
        {
            ReleaseRendererBuffer();
            return false;
        }
    }

    return true;
}

bool HdPageableBufferCore::PageToDisk(bool /*force*/)
{
    if (HasValidDiskBuffer())
    {
        // Update page with current data
        if (HasSceneBuffer())
        {
            mPageFileManager->UpdatePage(*mPageEntry, GetSceneMemorySpan().data());
        }
        else if (HasRendererBuffer())
        {
            mPageFileManager->UpdatePage(*mPageEntry, GetRendererMemorySpan().data());
        }
        return true; // Already on disk
    }

    // Create page handle and write to disk
    const void* sourceData = nullptr;
    if (HasRendererBuffer())
    {
        sourceData = GetRendererMemorySpan().data();
    }
    else if (HasSceneBuffer())
    {
        sourceData = GetSceneMemorySpan().data();
    }
    else
    {
        return false; // No data to page
    }

    mPageEntry = mPageFileManager->CreatePageEntry(sourceData, mSize);
    if (!mPageEntry)
    {
        return false;
    }

    mBufferState = static_cast<HdBufferState>(
        static_cast<int>(mBufferState) | static_cast<int>(HdBufferState::DiskBuffer));
    return true;
}

bool HdPageableBufferCore::SwapSceneToDisk(bool force, HdBufferState releaseBuffer)
{
    if (!HasSceneBuffer())
    {
        return false;
    }

    if (!PageToDisk(force))
    {
        return false;
    }

    // Remove other buffers.
    if (static_cast<int>(releaseBuffer) & static_cast<int>(HdBufferState::RendererBuffer))
    {
        ReleaseRendererBuffer();
    }
    if (static_cast<int>(releaseBuffer) & static_cast<int>(HdBufferState::SceneBuffer))
    {
        ReleaseSceneBuffer();
    }

    return true;
}

bool HdPageableBufferCore::SwapRendererToDisk(bool force, HdBufferState releaseBuffer)
{
    if (!HasRendererBuffer())
    {
        return false;
    }

    if (!PageToDisk(force))
    {
        return false;
    }

    // Remove other buffers.
    if (static_cast<int>(releaseBuffer) & static_cast<int>(HdBufferState::RendererBuffer))
    {
        ReleaseRendererBuffer();
    }
    if (static_cast<int>(releaseBuffer) & static_cast<int>(HdBufferState::SceneBuffer))
    {
        ReleaseSceneBuffer();
    }

    return true;
}

bool HdPageableBufferCore::SwapToSceneMemory(bool force, HdBufferState releaseBuffer)
{
    if (!PageToSceneMemory(force))
    {
        return false;
    }

    // Remove other buffers.
    if (static_cast<int>(releaseBuffer) & static_cast<int>(HdBufferState::RendererBuffer))
    {
        ReleaseRendererBuffer();
    }
    if (static_cast<int>(releaseBuffer) & static_cast<int>(HdBufferState::DiskBuffer))
    {
        ReleaseDiskPage();
    }
    return true;
}

bool HdPageableBufferCore::SwapToRendererMemory(bool force, HdBufferState releaseBuffer)
{
    if (!PageToRendererMemory(force))
    {
        return false;
    }

    // Remove other buffers.
    if (static_cast<int>(releaseBuffer) & static_cast<int>(HdBufferState::SceneBuffer))
    {
        ReleaseSceneBuffer();
    }
    if (static_cast<int>(releaseBuffer) & static_cast<int>(HdBufferState::DiskBuffer))
    {
        ReleaseDiskPage();
    }

    return true;
}

void HdPageableBufferCore::CreateSceneBuffer()
{
    if (HasSceneBuffer())
        return;

    mMemoryMonitor->AddSceneMemory(mSize);
    mBufferState = static_cast<HdBufferState>(
        static_cast<int>(mBufferState) | static_cast<int>(HdBufferState::SceneBuffer));
}

void HdPageableBufferCore::CreateRendererBuffer()
{
    if (HasRendererBuffer())
        return;

    mMemoryMonitor->AddRendererMemory(mSize);
    mBufferState = static_cast<HdBufferState>(
        static_cast<int>(mBufferState) | static_cast<int>(HdBufferState::RendererBuffer));
}

void HdPageableBufferCore::ReleaseSceneBuffer() noexcept
{
    if (HasSceneBuffer())
    {
        mMemoryMonitor->ReduceSceneMemory(mSize);
        mBufferState = static_cast<HdBufferState>(
            static_cast<int>(mBufferState) & ~static_cast<int>(HdBufferState::SceneBuffer));
    }
}

void HdPageableBufferCore::ReleaseRendererBuffer() noexcept
{
    if (HasRendererBuffer())
    {
        mMemoryMonitor->ReduceRendererMemory(mSize);
        mBufferState = static_cast<HdBufferState>(
            static_cast<int>(mBufferState) & ~static_cast<int>(HdBufferState::RendererBuffer));
    }
}

void HdPageableBufferCore::ReleaseDiskPage() noexcept
{
    if (mPageEntry)
    {
        mPageFileManager->ReleasePage(*mPageEntry);
        mPageEntry.reset();
    }
    mBufferState = static_cast<HdBufferState>(
        static_cast<int>(mBufferState) & ~static_cast<int>(HdBufferState::DiskBuffer));
}

} // namespace HVT_NS
