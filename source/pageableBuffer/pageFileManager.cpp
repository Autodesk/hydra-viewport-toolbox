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

#include <hvt/pageableBuffer/pageFileManager.h>

#include <hvt/pageableBuffer/pageableBuffer.h>
#include <pxr/base/tf/stringUtils.h>

#include <algorithm>
#include <fstream>
#include <filesystem>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{

// HdPageFileEntry Implementation
HdPageFileEntry::HdPageFileEntry(const std::string& filename, size_t pageId)
    : mFileName(filename)
    , mPageId(pageId)
    , mSizeLimit(HdPageFileManager::MAX_PAGE_FILE_SIZE)
{
    // Create the file if it doesn't exist
    std::ofstream file(filename, std::ios::binary | std::ios::app);
    if (file.is_open()) {
        file.close();
        // Get current file size
        if (std::filesystem::exists(filename)) {
            mNextOffset = std::filesystem::file_size(filename);
        }
    }
}

HdPageFileEntry::~HdPageFileEntry() {
    // Clean up temp files (in a real implementation, might want to keep them)
    try {
        if (std::filesystem::exists(mFileName)) {
            std::filesystem::remove(mFileName);
        }
    } catch (...) {
        // Ignore cleanup errors
    }
}

std::ptrdiff_t HdPageFileEntry::FindPageFileGap(size_t size) {
    ConsolidateFreeList();
    
    // Find a gap that fits the size
    for (auto it = mFreeList.begin(); it != mFreeList.end(); ++it) {
        if (it->size >= size) {
            std::ptrdiff_t offset = it->offset;
            
            // If the gap is larger than needed, split it
            if (it->size > size) {
                it->offset += size;
                it->size -= size;
            } else {
                // Use the entire gap
                mFreeList.erase(it);
            }
            
            return offset;
        }
    }
    
    // No suitable gap found, use end of file
    std::ptrdiff_t offset = mNextOffset;
    if (SetNextOffset(mNextOffset + size)) {
        return offset;
    }
    return -1;
}

bool HdPageFileEntry::SetNextOffset(std::ptrdiff_t offset) {
    if (static_cast<size_t>(offset) > mSizeLimit) {
        return false;
    }
    mNextOffset = offset;
    return true;
}

void HdPageFileEntry::AddFreeListEntry(std::ptrdiff_t offset, size_t size) {
    mFreeList.emplace_back(offset, size);
    mFreeListConsolidated = false;
}

void HdPageFileEntry::ConsolidateFreeList() {
    if (mFreeListConsolidated) {
        return;
    }
    
    if (mFreeList.empty()) {
        mFreeListConsolidated = true;
        return;
    }
    
    // Sort by offset
    std::sort(mFreeList.begin(), mFreeList.end(),
              [](const HdFreeListEntry& a, const HdFreeListEntry& b) {
                  return a.offset < b.offset;
              });
    
    // Merge adjacent entries
    auto it = mFreeList.begin();
    while (it != mFreeList.end() - 1) {
        auto next = it + 1;
        if (it->offset + it->size == static_cast<size_t>(next->offset)) {
            // Merge with next entry
            it->size += next->size;
            mFreeList.erase(next);
        } else {
            ++it;
        }
    }
    
    // Remove zero-sized entries
    mFreeList.erase(
        std::remove_if(mFreeList.begin(), mFreeList.end(),
                       [](const HdFreeListEntry& entry) { return entry.size == 0; }),
        mFreeList.end());
    
    mFreeListConsolidated = true;
}

bool HdPageFileEntry::WriteData(std::ptrdiff_t offset, const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(mFileMutex);
    
    std::fstream file(mFileName, std::ios::binary | std::ios::in | std::ios::out);
    if (!file.is_open()) {
        return false;
    }
    
    file.seekp(offset);
    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    
    return file.good();
}

bool HdPageFileEntry::ReadData(std::ptrdiff_t offset, void* data, size_t size) {
    std::lock_guard<std::mutex> lock(mFileMutex);
    
    std::ifstream file(mFileName, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    file.seekg(offset);
    file.read(static_cast<char*>(data), static_cast<std::streamsize>(size));
    
    return file.good() && file.gcount() == static_cast<std::streamsize>(size);
}

// HdPageFileManager Implementation
HdPageFileManager::HdPageFileManager(std::filesystem::path pageFileDirectory) 
    : mPageFileDirectory(std::move(pageFileDirectory)) {
    // Create initial page file
    CreatePageFile();
}

HdPageFileManager::~HdPageFileManager() {
    mPageFileEntries.clear();
    
    // Clean up temp directory
    try {
        if (std::filesystem::exists(mPageFileDirectory)) {
            std::filesystem::remove_all(mPageFileDirectory);
        }
    } catch (...) {
        // Ignore cleanup errors
    }
}

std::unique_ptr<HdBufferPageHandle> HdPageFileManager::CreatePageHandle(const void* data, size_t size) {
    std::lock_guard<std::mutex> lock(mSyncMutex);
    
    auto* pageEntry = GetCurrentPageFileEntry();
    if (!pageEntry) {
        if (!CreatePageFile()) {
            return nullptr;
        }
        pageEntry = GetCurrentPageFileEntry();
    }
    
    // Try to find a gap first
    std::ptrdiff_t offset = pageEntry->FindPageFileGap(size);
    if (offset == -1) {
        // Current file is full, create new one
        if (!CreatePageFile()) {
            return nullptr;
        }
        pageEntry = GetCurrentPageFileEntry();
        offset = pageEntry->FindPageFileGap(size);
    }
    
    if (offset == -1) {
        return nullptr;
    }
    
    // Write data to file
    if (!pageEntry->WriteData(offset, data, size)) {
        return nullptr;
    }
    
    return std::make_unique<HdBufferPageHandle>(pageEntry->PageFileId(), size, offset);
}

bool HdPageFileManager::LoadPage(const HdBufferPageHandle& handle, void* data) {
    std::lock_guard<std::mutex> lock(mSyncMutex);
    
    // Find the correct page file
    for (const auto& entry : mPageFileEntries) {
        if (entry->PageFileId() == handle.PageId()) {
            return entry->ReadData(handle.Offset(), data, handle.Size());
        }
    }
    
    return false;
}

bool HdPageFileManager::UpdatePage(const HdBufferPageHandle& handle, const void* data) {
    std::lock_guard<std::mutex> lock(mSyncMutex);
    
    if (handle.PageId() >= mPageFileEntries.size()) {
        return false;
    }
    
    auto& entry = mPageFileEntries[handle.PageId()];
    return entry->WriteData(handle.Offset(), data, handle.Size());
}

void HdPageFileManager::DeletePage(const HdBufferPageHandle& handle) {
    std::lock_guard<std::mutex> lock(mSyncMutex);
    
    if (handle.PageId() >= mPageFileEntries.size()) {
        return;
    }
    
    auto& entry = mPageFileEntries[handle.PageId()];
    entry->AddFreeListEntry(handle.Offset(), handle.Size());
}

HdPageFileEntry* HdPageFileManager::GetCurrentPageFileEntry() const {
    if (mPageFileEntries.empty()) {
        return nullptr;
    }
    return mPageFileEntries.back().get();
}

bool HdPageFileManager::CreatePageFile() {
    // Create temp directory if it doesn't exist
    std::filesystem::create_directories(mPageFileDirectory);
    
    auto pageId = mPageFileEntries.size();
    std::string filename = TfStringPrintf("%s/page_%zu.bin", mPageFileDirectory.string().c_str(), pageId);
    
    auto pageEntry = std::make_unique<HdPageFileEntry>(filename, pageId);
    mPageFileEntries.push_back(std::move(pageEntry));
    
    return true;
}

size_t HdPageFileManager::GetTotalDiskUsage() const {
    std::lock_guard<std::mutex> lock(mSyncMutex);
    
    size_t total = 0;
    for (const auto& entry : mPageFileEntries) {
        try {
            if (std::filesystem::exists(entry->FileName())) {
                total += std::filesystem::file_size(entry->FileName());
            }
        } catch (...) {
            // Ignore errors
        }
    }
    
    return total;
}

void HdPageFileManager::PrintPagerStats() const {
    std::lock_guard<std::mutex> lock(mSyncMutex);
    
    auto formatBytes = [](size_t bytes) -> std::string {
        if (bytes >= 1024ULL * 1024 * 1024) {
            return TfStringPrintf("%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
        } else if (bytes >= 1024ULL * 1024) {
            return TfStringPrintf("%.2f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
        } else if (bytes >= 1024ULL) {
            return TfStringPrintf("%.2f KB", static_cast<double>(bytes) / 1024.0);
        } else {
            return TfStringPrintf("%zu bytes", bytes);
        }
    };
    
    size_t totalDiskUsage = 0;
    for (const auto& entry : mPageFileEntries) {
        try {
            if (std::filesystem::exists(entry->FileName())) {
                auto size = std::filesystem::file_size(entry->FileName());
                totalDiskUsage += size;
            }
        } catch (...) {
            TF_WARN("Error reading page file (%s) size\n", entry->FileName().c_str());
        }
    }

    TF_STATUS("=== Page Manager Statistics ===\n"
        "Page File Count: %zu\n"
        "Total Disk Usage: %s\n"
        "Max File Size: %s\n"
        "========================\n", 
        mPageFileEntries.size(),
        formatBytes(totalDiskUsage).c_str(),
        formatBytes(MAX_PAGE_FILE_SIZE).c_str()
    );
}

} // namespace HVT_NS