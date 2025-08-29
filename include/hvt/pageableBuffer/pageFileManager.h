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

#include <hvt/api.h>
#include <hvt/pageableBuffer/pageableMemoryMonitor.h> // Constants

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace HVT_NS
{

class HdBufferPageHandle;

struct HVT_API HdFreeListEntry
{
    std::ptrdiff_t offset = 0;
    size_t size           = 0;

    HdFreeListEntry() = default;
    HdFreeListEntry(std::ptrdiff_t offset, size_t size) : offset(offset), size(size) {}
};

class HVT_API HdPageFileEntry
{
public:
    HdPageFileEntry(const std::string& filename, size_t pageId);
    ~HdPageFileEntry();

    std::ptrdiff_t FindPageFileGap(size_t size);
    size_t NextOffset() const { return mNextOffset; }
    bool SetNextOffset(std::ptrdiff_t offset);
    void AddFreeListEntry(std::ptrdiff_t offset, size_t size);
    void ConsolidateFreeList();

    size_t PageFileId() const { return mPageId; }
    size_t SizeLimit() const { return mSizeLimit; }
    const std::string& FileName() const { return mFileName; }

    bool WriteData(std::ptrdiff_t offset, const void* data, size_t size);
    bool ReadData(std::ptrdiff_t offset, void* data, size_t size);

private:
    const std::string mFileName;
    const size_t mPageId;
    const size_t mSizeLimit;
    std::ptrdiff_t mNextOffset = 0;
    std::vector<HdFreeListEntry> mFreeList;
    bool mFreeListConsolidated = true;
    mutable std::mutex mFileMutex;
};

class HVT_API HdPageFileManager
{
public:
    ~HdPageFileManager();

    std::unique_ptr<HdBufferPageHandle> CreatePageHandle(const void* data, size_t size);
    bool LoadPage(const HdBufferPageHandle& handle, void* data);
    bool UpdatePage(const HdBufferPageHandle& handle, const void* data);
    void DeletePage(const HdBufferPageHandle& handle);

    size_t GetTotalDiskUsage() const;
    void PrintPagerStats() const;

    static constexpr size_t MAX_PAGE_FILE_SIZE = static_cast<size_t>(1.8) * ONE_GiB;

private:
    // By design, only HdPageableBufferManager can create and hold it.
    HdPageFileManager(std::filesystem::path pageFileDirectory);

    // Disable copy and move
    HdPageFileManager(const HdPageFileManager&) = delete;
    HdPageFileManager(HdPageFileManager&&)      = delete;

    HdPageFileEntry* GetCurrentPageFileEntry() const;
    bool CreatePageFile();

    std::vector<std::unique_ptr<HdPageFileEntry>> mPageFileEntries;
    mutable std::mutex mSyncMutex;

    std::filesystem::path mPageFileDirectory =
        std::filesystem::temp_directory_path() / "hvt_temp_pages";

    template <typename, typename>
    friend class HdPageableBufferManager;
};

} // namespace HVT_NS