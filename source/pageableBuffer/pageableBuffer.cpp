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
#include <hvt/pageableBuffer/pageableBuffer.h>

#include <hvt/pageableBuffer/pageFileManager.h>
#include <hvt/pageableBuffer/pageableBufferManager.h>
#include <hvt/pageableBuffer/pageableMemoryMonitor.h>

#include <pxr/base/tf/diagnostic.h>
#include <pxr/base/tf/stringUtils.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

HdPageableBufferBase::HdPageableBufferBase(const SdfPath& path, size_t size, HdBufferUsage usage,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
    DestructionCallback destructionCallback) :
    mPath(path),
    mUsage(usage),
    mSize(size),
    mDestructionCallback(std::move(destructionCallback)),
    mPageFileManager(const_cast<std::unique_ptr<HdPageFileManager>&>(pageFileManager)),
    mMemoryMonitor(const_cast<std::unique_ptr<HdMemoryMonitor>&>(memoryMonitor))
{
    CreateSceneBuffer();
#ifdef _DEBUG
    TF_STATUS("Created buffer: %s (%zu bytes)\n", mPath.GetText(), size);
#endif
}

HdPageableBufferBase::~HdPageableBufferBase()
{
    ReleaseDiskPage();
    ReleaseSceneBuffer();
    ReleaseRendererBuffer();

    // Notify HdPageableBufferManager of removing from the list.
    if (mDestructionCallback)
    {
        mDestructionCallback(mPath);
    }
#ifdef _DEBUG
    TF_STATUS("Destroyed buffer: %s\n", mPath.GetText());
#endif
}

// std::span-based memory access methods
TfSpan<const std::byte> HdPageableBufferBase::GetSceneMemorySpan() const noexcept
{
    return TfSpan<const std::byte>();
}

TfSpan<std::byte> HdPageableBufferBase::GetSceneMemorySpan() noexcept
{
    return TfSpan<std::byte>();
}

TfSpan<const std::byte> HdPageableBufferBase::GetRendererMemorySpan() const noexcept
{
    return TfSpan<const std::byte>();
}

TfSpan<std::byte> HdPageableBufferBase::GetRendererMemorySpan() noexcept
{
    return TfSpan<std::byte>();
}

bool HdPageableBufferBase::PageToSceneMemory(bool /*force*/)
{
    if (HasSceneBuffer())
    {
        return true; // Already in scene memory
    }

#ifdef _DEBUG
    TF_STATUS("Paging %s to scene memory\n", mPath.GetText());
#endif

    CreateSceneBuffer();

    // Try to load from disk first.
    if (!HasValidDiskBuffer() ||
        !mPageFileManager->LoadPage(*mPageHandle, GetSceneMemorySpan().data()))
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

bool HdPageableBufferBase::PageToRendererMemory(bool /*force*/)
{
    if (HasRendererBuffer())
    {
        return true; // Already in hardware memory
    }

#ifdef _DEBUG
    TF_STATUS("Paging %s to hardware memory\n", mPath.GetText());
#endif

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
            !mPageFileManager->LoadPage(*mPageHandle, GetRendererMemorySpan().data()))
        {
            ReleaseRendererBuffer();
            return false;
        }
    }

    return true;
}

bool HdPageableBufferBase::PageToDisk(bool /*force*/)
{
    if (HasValidDiskBuffer())
    {
        // Update page with current data
        if (HasSceneBuffer())
        {
            mPageFileManager->UpdatePage(*mPageHandle, GetSceneMemorySpan().data());
        }
        else if (HasRendererBuffer())
        {
            mPageFileManager->UpdatePage(*mPageHandle, GetRendererMemorySpan().data());
        }
        return true; // Already on disk
    }

#ifdef _DEBUG
    TF_STATUS("Paging %s to disk\n", mPath.GetText());
#endif

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

    mPageHandle = mPageFileManager->CreatePageHandle(sourceData, mSize);
    if (!mPageHandle)
    {
        return false;
    }

    mBufferState = static_cast<HdBufferState>(
        static_cast<int>(mBufferState) | static_cast<int>(HdBufferState::DiskBuffer));
    return true;
}

bool HdPageableBufferBase::SwapSceneToDisk(bool force, HdBufferState releaseBuffer)
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

#ifdef _DEBUG
    TF_STATUS("Moved %s from scene memory to disk\n", mPath.GetText());
#endif
    return true;
}

bool HdPageableBufferBase::SwapRendererToDisk(bool force, HdBufferState releaseBuffer)
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

#ifdef _DEBUG
    TF_STATUS("Moved %s from hardware memory to disk\n", mPath.GetText());
#endif
    return true;
}

bool HdPageableBufferBase::SwapToSceneMemory(bool force, HdBufferState releaseBuffer)
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

bool HdPageableBufferBase::SwapToRendererMemory(bool force, HdBufferState releaseBuffer)
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

void HdPageableBufferBase::CreateSceneBuffer()
{
    if (HasSceneBuffer())
        return;

    mMemoryMonitor->AddSceneMemory(mSize);
    mBufferState = static_cast<HdBufferState>(
        static_cast<int>(mBufferState) | static_cast<int>(HdBufferState::SceneBuffer));
#ifdef _DEBUG
    TF_STATUS("Created scene buffer for %s (%zu bytes)\n", mPath.GetText(), mSize);
#endif
}

void HdPageableBufferBase::CreateRendererBuffer()
{
    if (HasRendererBuffer())
        return;

    mMemoryMonitor->AddRendererMemory(mSize);
    mBufferState = static_cast<HdBufferState>(
        static_cast<int>(mBufferState) | static_cast<int>(HdBufferState::RendererBuffer));
#ifdef _DEBUG
    TF_STATUS("Created hardware buffer for %s (%zu bytes)\n", mPath.GetText(), mSize);
#endif
}

void HdPageableBufferBase::ReleaseSceneBuffer() noexcept
{
    if (HasSceneBuffer())
    {
        mMemoryMonitor->ReduceSceneMemory(mSize);
        mBufferState = static_cast<HdBufferState>(
            static_cast<int>(mBufferState) & ~static_cast<int>(HdBufferState::SceneBuffer));
#ifdef _DEBUG
        TF_STATUS("Released scene buffer for %s\n", mPath.GetText());
#endif
    }
}

void HdPageableBufferBase::ReleaseRendererBuffer() noexcept
{
    if (HasRendererBuffer())
    {
        mMemoryMonitor->ReduceRendererMemory(mSize);
        mBufferState = static_cast<HdBufferState>(
            static_cast<int>(mBufferState) & ~static_cast<int>(HdBufferState::RendererBuffer));
#ifdef _DEBUG
        TF_STATUS("Released hardware buffer for %s\n", mPath.GetText());
#endif
    }
}

void HdPageableBufferBase::ReleaseDiskPage() noexcept
{
    if (mPageHandle)
    {
        // TODO
        // mPageFileManager->DeletePage(*mPageHandle);
#ifdef _DEBUG
        TF_STATUS("Released disk page for %s\n", mPath.GetText());
#endif
        mPageHandle.reset();
    }
    mBufferState = static_cast<HdBufferState>(
        static_cast<int>(mBufferState) & ~static_cast<int>(HdBufferState::DiskBuffer));
}

} // namespace HVT_NS
