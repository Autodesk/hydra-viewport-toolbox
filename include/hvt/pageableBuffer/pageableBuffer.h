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
#pragma once

#include <hvt/api.h>
#include <hvt/pageableBuffer/pageableConcepts.h>

#include <pxr/pxr.h>
#include <pxr/base/tf/span.h>
#include <pxr/usd/sdf/path.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>

namespace HVT_NS
{

// Forward declarations
class HdPageFileManager;
class HdMemoryMonitor;

template <
#if defined(ENABLE_PAGING_CONCEPTS)
    HdPagingConcepts::PagingStrategyLike PagingStrategyType,
    HdPagingConcepts::BufferSelectionStrategyLike BufferSelectionStrategyType,
#else
    typename PagingStrategyType, typename BufferSelectionStrategyType,
#endif
    typename KeyType, typename KeyHash
    >
class HdPageableBufferManager;

enum class HVT_API HdBufferState
{
    Unknown        = 0,      ///< Initial state
    SceneBuffer    = 1 << 0, ///< Data in the scene
    RendererBuffer = 1 << 1, ///< Data in renderer
    DiskBuffer     = 1 << 2, ///< Data in disk
};

enum class HVT_API HdBufferUsage
{
    Static, ///< Immutable data, will be paged if possible
    Dynamic ///< Mutable data, will be paged if necessary
};

/// Descriptor for a page in the disk buffer.
class HVT_API HdBufferPageEntry
{
public:
    constexpr HdBufferPageEntry(size_t pageId, size_t size, std::ptrdiff_t offset) noexcept :
        mPageId(pageId), mSize(size), mOffset(offset)
    {
    }

    constexpr size_t PageId() const noexcept { return mPageId; }
    constexpr size_t Size() const noexcept { return mSize; }
    constexpr std::ptrdiff_t Offset() const noexcept { return mOffset; }
    constexpr bool IsValid() const noexcept { return mOffset != static_cast<std::ptrdiff_t>(-1); }

    // Comparison operators
    bool operator!=(const HdBufferPageEntry& other) const noexcept
    {
        return mPageId != other.mPageId || mOffset != other.mOffset || mSize != other.mSize;
    }

    bool operator==(const HdBufferPageEntry& other) const noexcept { return !(*this != other); }

    bool operator<(const HdBufferPageEntry& other) const noexcept
    {
        if (mPageId != other.mPageId)
            return mPageId < other.mPageId;
        if (mOffset != other.mOffset)
            return mOffset < other.mOffset;
        return mSize < other.mSize;
    }

    bool operator>(const HdBufferPageEntry& other) const noexcept { return other < *this; }

    bool operator<=(const HdBufferPageEntry& other) const noexcept { return !(other < *this); }

    bool operator>=(const HdBufferPageEntry& other) const noexcept { return !(*this < other); }

private:
    const size_t mPageId;
    const size_t mSize;
    const std::ptrdiff_t mOffset;
};

/// Non-templated base with all core paging logic.
/// Strategies and manager internals operate on this type. Contains no key.
/// Implementation maintains data consistency: once data is swapped to disk
/// it's immutable, and reads/writes trigger a page-back.
class HVT_API HdPageableBufferCore
{
public:
    using DestructionCallback = std::function<void()>;
    virtual ~HdPageableBufferCore();

    // Resource management between Scene, Renderer and disk. //////////////////
    // Page: Create new buffer and fill data. Keep the source buffer.
    [[nodiscard]] virtual bool PageToSceneMemory(bool force = false);
    [[nodiscard]] virtual bool PageToRendererMemory(bool force = false);
    [[nodiscard]] virtual bool PageToDisk(bool force = false);

    // Swap: Create new buffer and fill data. Release the source buffer.
    [[nodiscard]] virtual bool SwapSceneToDisk(bool force = false,
        HdBufferState releaseBuffer = static_cast<HdBufferState>(
            static_cast<int>(HdBufferState::SceneBuffer) |
            static_cast<int>(HdBufferState::RendererBuffer)));
    [[nodiscard]] virtual bool SwapRendererToDisk(bool force = false,
        HdBufferState releaseBuffer = static_cast<HdBufferState>(
            static_cast<int>(HdBufferState::SceneBuffer) |
            static_cast<int>(HdBufferState::RendererBuffer)));
    // clang-format off
    [[nodiscard]] virtual bool SwapToSceneMemory(bool force = false,
        HdBufferState releaseBuffer = static_cast<HdBufferState>(
            static_cast<int>(HdBufferState::RendererBuffer) |
            static_cast<int>(HdBufferState::DiskBuffer)));
    [[nodiscard]] virtual bool SwapToRendererMemory(bool force = false,
        HdBufferState releaseBuffer = static_cast<HdBufferState>(
            static_cast<int>(HdBufferState::SceneBuffer) |
            static_cast<int>(HdBufferState::DiskBuffer)));
    // clang-format on

    // Core operation sets: Release. //////////////////////////////////////////
    // Release: Release the source buffer and update the state.
    virtual void ReleaseSceneBuffer() noexcept;
    virtual void ReleaseRendererBuffer() noexcept;
    virtual void ReleaseDiskPage() noexcept;

    // Get memory as spans for safe access
    [[nodiscard]] virtual PXR_NS::TfSpan<const std::byte> GetSceneMemorySpan() const noexcept;
    [[nodiscard]] virtual PXR_NS::TfSpan<std::byte> GetSceneMemorySpan() noexcept;
    [[nodiscard]] virtual PXR_NS::TfSpan<const std::byte> GetRendererMemorySpan() const noexcept;
    [[nodiscard]] virtual PXR_NS::TfSpan<std::byte> GetRendererMemorySpan() noexcept;

    // Properties
    [[nodiscard]] constexpr size_t Size() const noexcept { return mSize; }
    void SetSize(size_t size) noexcept { mSize = size; }
    [[nodiscard]] constexpr HdBufferUsage Usage() const noexcept { return mUsage; }
    [[nodiscard]] constexpr HdBufferState GetBufferState() const noexcept { return mBufferState; }

    [[nodiscard]] constexpr unsigned int FrameStamp() const noexcept { return mFrameStamp; }
    void UpdateFrameStamp(unsigned int frame) noexcept { mFrameStamp = frame; }

    // Status
    [[nodiscard]] constexpr bool IsOverAge(unsigned int currentFrame, unsigned int ageLimit) const noexcept
    {
        return (currentFrame > mFrameStamp) && ((currentFrame - mFrameStamp) > ageLimit);
    }
    [[nodiscard]] bool HasValidDiskBuffer() const noexcept
    {
        return mPageEntry && mPageEntry->IsValid();
    }
    [[nodiscard]] constexpr bool HasSceneBuffer() const noexcept
    {
        return (static_cast<int>(mBufferState) & static_cast<int>(HdBufferState::SceneBuffer));
    }
    [[nodiscard]] constexpr bool HasRendererBuffer() const noexcept
    {
        return (static_cast<int>(mBufferState) & static_cast<int>(HdBufferState::RendererBuffer));
    }
    [[nodiscard]] constexpr bool HasDiskBuffer() const noexcept
    {
        return (static_cast<int>(mBufferState) & static_cast<int>(HdBufferState::DiskBuffer));
    }

protected:
    // By design, only HdPageableBufferManager can create buffers.
    HdPageableBufferCore(size_t size, HdBufferUsage usage,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        DestructionCallback destructionCallback);

    // Core operation sets: Creation. /////////////////////////////////////////
    // Create: Create a new buffer and update the state. No data is copied.
    virtual void CreateSceneBuffer();
    virtual void CreateRendererBuffer();

    // Helper to create aligned memory span
    template <typename T = std::byte>
    [[nodiscard]] constexpr PXR_NS::TfSpan<T> MakeSpan(
        std::unique_ptr<T[]>& ptr, size_t size) const noexcept
    {
        return ptr ? PXR_NS::TfSpan<T>(ptr.get(), size) : PXR_NS::TfSpan<T> {};
    }

#if defined(ENABLE_PAGING_CONCEPTS)
    template <HdPagingConcepts::PagingStrategyLike PagingStrategyType,
              HdPagingConcepts::BufferSelectionStrategyLike BufferSelectionStrategyType,
              typename, typename>
#else
    template <typename, typename, typename, typename>
#endif
    friend class HdPageableBufferManager;

    const HdBufferUsage mUsage;

    size_t mSize               = 0;
    HdBufferState mBufferState = HdBufferState::Unknown;
    unsigned int mFrameStamp   = 0; // Frame stamp for age tracking

    // Page handle for disk storage
    std::unique_ptr<HdBufferPageEntry> mPageEntry;

    // Type-erased destruction callback to notify BufferManager and
    // avoid cycle reference (captures key internally)
    DestructionCallback mDestructionCallback;

    // Accessor to PageFileManager & MemoryMonitor
    std::unique_ptr<HdPageFileManager>& mPageFileManager;
    std::unique_ptr<HdMemoryMonitor>& mMemoryMonitor;
};

/// Thin templated layer adding a typed key.
/// Data sources use the default KeyType = SdfPath.
template <typename KeyType = PXR_NS::SdfPath>
class HdPageableBufferBase : public HdPageableBufferCore
{
public:
    using KeyDestructionCallback = std::function<void(const KeyType&)>;
    using DestructionCallback    = KeyDestructionCallback;

    [[nodiscard]] constexpr const KeyType& Key() const noexcept { return mKey; }

    // Backward-compat alias (meaningful for SdfPath keys)
    [[nodiscard]] constexpr const KeyType& Path() const noexcept { return mKey; }

protected:
    HdPageableBufferBase(const KeyType& key, size_t size, HdBufferUsage usage,
        const std::unique_ptr<HdPageFileManager>& pageFileManager,
        const std::unique_ptr<HdMemoryMonitor>& memoryMonitor,
        KeyDestructionCallback destructionCallback) :
            HdPageableBufferCore(size, usage, pageFileManager, memoryMonitor,
                // Type-erase: capture key by value so it outlives the buffer
                [key, cb = std::move(destructionCallback)]() { if (cb) cb(key); })
            , mKey(key)
    {
    }

#if defined(ENABLE_PAGING_CONCEPTS)
    template <HdPagingConcepts::PagingStrategyLike PagingStrategyType,
              HdPagingConcepts::BufferSelectionStrategyLike BufferSelectionStrategyType,
              typename, typename>
#else
    template <typename, typename, typename, typename>
#endif
    friend class HdPageableBufferManager;

    const KeyType mKey;
};

// Convenience alias for the common SdfPath-keyed buffer
using HdPageableBuffer = HdPageableBufferBase<PXR_NS::SdfPath>;

// Hash functor for std::string keys, usable as KeyHash in HdPageableBufferManager
struct StringKeyHash
{
    size_t operator()(const std::string& key) const { return std::hash<std::string> {}(key); }
};

} // namespace HVT_NS
