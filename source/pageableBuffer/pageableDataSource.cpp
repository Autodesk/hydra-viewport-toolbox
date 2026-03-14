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
#include <hvt/pageableBuffer/pageableDataSource.h>

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/quatf.h>
#include <pxr/base/gf/vec2d.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4d.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec4i.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/tokens.h>

#include <algorithm>
#include <cstring>
#include <set>

PXR_NAMESPACE_USING_DIRECTIVE

namespace HVT_NS
{
////////////////////////////////////////////////////////////////////////////////
// Packed binary format helpers
//
// Individual VtValue serialization format (HdDefaultValueSerializer):
//   [uint8 VtTypeTag] [raw POD payload bytes]
//   POD types are memcpy'd directly; string/token arrays use a
//   [size_t count] [size_t len, chars...]... variable-length encoding.
//
// Container packed buffer layout (one disk page per container):
//   [uint32 numElements]
//   per element: [uint32+chars name] [uint32+chars typeHint] [uint64 offset] [uint64 size]
//   [concatenated serialized payloads]
//   offsets are relative to the start of the payload region.
//
// Vector packed buffer layout (one disk page per vector):
//   [uint32 numElements]
//   per element: [uint32+chars typeHint] [uint64 offset] [uint64 size]
//   [concatenated serialized payloads]
//
// All packed buffers are written as a single contiguous blob into one
// HdBufferPageEntry in the page file. This avoids per-element disk I/O and
// keeps related data (e.g. points, normals, indices of a prim) co-located.
////////////////////////////////////////////////////////////////////////////////
namespace
{

// Compact type tag
// Wire format: [uint8 VtTypeTag] [payload]
// - POD arrays: payload = raw memcpy of element data (zero padding, no alignment)
// - String/Token arrays: payload = [size_t count] ([size_t len] [chars])...
enum class VtTypeTag : uint8_t
{
    Unknown = 0,
    FloatArray,
    DoubleArray,
    HalfArray,
    IntArray,
    UIntArray,
    Int64Array,
    UInt64Array,
    Vec2fArray,
    Vec2dArray,
    Vec2iArray,
    Vec3fArray,
    Vec3dArray,
    Vec3iArray,
    Vec4fArray,
    Vec4dArray,
    Vec4iArray,
    Matrix4fArray,
    Matrix4dArray,
    QuatfArray,
    QuatdArray,
    StringArray,
    TokenArray,
};

constexpr size_t kTypeTagSize = sizeof(VtTypeTag);
static_assert(kTypeTagSize == 1, "VtTypeTag must be exactly 1 byte to match the header layout");

// Deserialize POD array directly from raw pointer (zero-copy from caller's buffer)
template <typename T>
VtArray<T> DeserializePodDirect(const uint8_t* data, size_t byteSize)
{
    const size_t count = byteSize / sizeof(T);
    VtArray<T> array(count);
    if (count > 0)
    {
        std::memcpy(array.data(), data, count * sizeof(T));
    }
    return array;
}

// Serialize POD array with 1-byte type tag header into a single allocation
template <typename T>
std::vector<uint8_t> SerializePodTagged(VtTypeTag tag, const VtArray<T>& array)
{
    const size_t byteSize = array.size() * sizeof(T);
    std::vector<uint8_t> result(kTypeTagSize + byteSize);
    result[0] = static_cast<uint8_t>(tag);
    if (!array.empty())
    {
        std::memcpy(result.data() + kTypeTagSize, array.cdata(), byteSize);
    }
    return result;
}

// Deserialize a variable-length string-like array from raw payload.
template <typename ArrayT, typename FromStringFn>
VtValue DeserializeStringArrayDirect(
    const uint8_t* payload, size_t payloadSize, FromStringFn&& fromString)
{
    if (payloadSize < sizeof(size_t))
        return VtValue(ArrayT());

    // Get the count of elements
    size_t count = 0;
    std::memcpy(&count, payload, sizeof(size_t));

    // Get the elements of various lengths
    ArrayT array(count);
    size_t offset = sizeof(size_t);
    for (size_t i = 0; i < count && offset + sizeof(size_t) <= payloadSize; ++i)
    {
        size_t len = 0;
        std::memcpy(&len, payload + offset, sizeof(size_t));
        offset += sizeof(size_t);
        if (offset + len <= payloadSize)
        {
            array[i] =
                fromString(std::string(reinterpret_cast<const char*>(payload + offset), len));
            offset += len;
        }
    }
    return VtValue(array);
}

// Serialize a variable-length string-like array with type tag header.
template <typename ArrayT, typename ToStringFn>
std::vector<uint8_t> SerializeStringArrayTagged(
    VtTypeTag tag, const ArrayT& array, ToStringFn&& toString)
{
    size_t totalSize = kTypeTagSize + sizeof(size_t);
    for (const auto& elem : array)
        totalSize += sizeof(size_t) + toString(elem).size();

    // Set the type tag and the count of elements
    std::vector<uint8_t> result(totalSize);
    result[0]    = static_cast<uint8_t>(tag);
    size_t pos   = kTypeTagSize;
    size_t count = array.size();
    std::memcpy(result.data() + pos, &count, sizeof(size_t));
    pos += sizeof(size_t);

    // Set the elements of various lengths
    for (const auto& elem : array)
    {
        const std::string& s = toString(elem);
        size_t len           = s.size();
        std::memcpy(result.data() + pos, &len, sizeof(size_t));
        pos += sizeof(size_t);
        std::memcpy(result.data() + pos, s.data(), len);
        pos += len;
    }
    return result;
}

const HdDefaultValueSerializer& GetDefaultSerializer()
{
    static HdDefaultValueSerializer sDefault;
    return sDefault;
}

/// Sequential binary writer for packed serialization buffers.
class PackedBufferWriter
{
public:
    void Reserve(size_t bytes) { mBuffer.reserve(bytes); }
    void WriteU32(uint32_t v)
    {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        mBuffer.insert(mBuffer.end(), p, p + sizeof(uint32_t));
    }
    void WriteU64(uint64_t v)
    {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        mBuffer.insert(mBuffer.end(), p, p + sizeof(uint64_t));
    }
    void WriteString(const std::string& s)
    {
        WriteU32(static_cast<uint32_t>(s.size()));
        mBuffer.insert(mBuffer.end(), s.begin(), s.end());
    }
    void WriteBytes(const uint8_t* data, size_t size)
    {
        mBuffer.insert(mBuffer.end(), data, data + size);
    }
    std::vector<uint8_t> Release() { return std::move(mBuffer); }

private:
    std::vector<uint8_t> mBuffer;
};

/// Sequential binary reader for packed deserialization buffers.
class PackedBufferReader
{
public:
    explicit PackedBufferReader(const std::vector<uint8_t>& data) :
        mData(data.data()), mSize(data.size())
    {
    }
    uint32_t ReadU32()
    {
        uint32_t v = 0;
        std::memcpy(&v, mData + mPos, sizeof(uint32_t));
        mPos += sizeof(uint32_t);
        return v;
    }
    uint64_t ReadU64()
    {
        uint64_t v = 0;
        std::memcpy(&v, mData + mPos, sizeof(uint64_t));
        mPos += sizeof(uint64_t);
        return v;
    }
    std::string ReadString()
    {
        const uint32_t len = ReadU32();
        std::string s(reinterpret_cast<const char*>(mData + mPos), len);
        mPos += len;
        return s;
    }
    [[nodiscard]] size_t Position() const { return mPos; }

private:
    const uint8_t* mData = nullptr;
    size_t mSize         = 0;
    size_t mPos          = 0;
};

// Zero-copy path for HdDefaultValueSerializer (avoids intermediate vector alloc)
VtValue DeserializeElement(
    const IHdValueSerializer& serializer, const uint8_t* data, size_t size, const TfToken& typeHint)
{
    if (const auto* defaultSer = dynamic_cast<const HdDefaultValueSerializer*>(&serializer))
        return defaultSer->DeserializeFromSpan(data, size, typeHint);

    // Fall back to a copy-based path
    std::vector<uint8_t> buf(data, data + size);
    return serializer.Deserialize(buf, typeHint);
}

// Writes packed buffer to disk
bool WritePackedToDisk(const std::vector<uint8_t>& packed,
    std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager, HdBufferState& bufferState)
{
    // Reuses an existing page entry (in-place update) when one already exists
    if (pageEntry && pageEntry->IsValid())
        return pageFileManager->UpdatePage(*pageEntry, packed.data());

    // Otherwise allocates a new slot in the page file.
    pageEntry = pageFileManager->CreatePageEntry(packed.data(), packed.size());
    if (!pageEntry)
        return false;
    bufferState = static_cast<HdBufferState>(
        static_cast<int>(bufferState) | static_cast<int>(HdBufferState::DiskBuffer));
    return true;
}

// Reads the entire packed blob from the page file into memory.
std::vector<uint8_t> LoadPackedFromDisk(
    const HdBufferPageEntry& pageEntry, std::unique_ptr<HdPageFileManager>& pageFileManager)
{
    std::vector<uint8_t> packed(pageEntry.Size());
    if (!pageFileManager->LoadPage(pageEntry, packed.data()))
        return {};
    return packed;
}

} // anonymous namespace

// HdPageableDataSourceUtils Implementation ///////////////////////////////////

const HdPageableBufferBase<>::DestructionCallback
    HdPageableDataSourceUtils::kNoOpDestructionCallback = [](const SdfPath&) {};

// Writes container data using a two-pass packing approach
std::vector<uint8_t> HdPageableDataSourceUtils::SerializeContainerPacked(
    const std::map<TfToken, std::shared_ptr<HdPageableValue>>& elements,
    const IHdValueSerializer& serializer)
{
    // First, serialize each element individually to collect name, type hint, and payload bytes
    struct Entry
    {
        TfToken name, typeHint;
        std::vector<uint8_t> data;
    };
    std::vector<Entry> collected;
    collected.reserve(elements.size());
    for (const auto& [token, elem] : elements)
    {
        VtValue v = elem->GetValueIfResident();
        if (!v.IsEmpty())
            collected.push_back({ token, elem->GetDataType(), serializer.Serialize(v) });
    }
    if (collected.empty())
        return {};

    // Compute directory size to pre-allocate the output buffer
    size_t headerSize = sizeof(uint32_t);
    for (const auto& e : collected)
    {
        headerSize += sizeof(uint32_t) + e.name.GetString().size();
        headerSize += sizeof(uint32_t) + e.typeHint.GetString().size();
        headerSize += sizeof(uint64_t) * 2; // offset + size
    }
    std::vector<uint64_t> offsets;
    offsets.reserve(collected.size());
    uint64_t payloadOff = 0;
    for (const auto& e : collected)
    {
        offsets.push_back(payloadOff);
        payloadOff += e.data.size();
    }

    // Second, write the directory header followed by concatenated payloads
    // into a single contiguous buffer.
    PackedBufferWriter w;
    w.Reserve(headerSize + payloadOff);
    w.WriteU32(static_cast<uint32_t>(collected.size()));
    for (size_t i = 0; i < collected.size(); ++i)
    {
        w.WriteString(collected[i].name.GetString());
        w.WriteString(collected[i].typeHint.GetString());
        w.WriteU64(offsets[i]);
        w.WriteU64(static_cast<uint64_t>(collected[i].data.size()));
    }
    for (const auto& e : collected)
        w.WriteBytes(e.data.data(), e.data.size());
    return w.Release();
}

// Reads container back to memory
bool HdPageableDataSourceUtils::DeserializeContainerPacked(const std::vector<uint8_t>& packedData,
    std::map<TfToken, std::shared_ptr<HdPageableValue>>& elements,
    std::map<TfToken, HdContainerPageEntry>& pageEntries, const IHdValueSerializer& serializer)
{
    if (packedData.size() < sizeof(uint32_t))
        return false;

    // Read the directory to locate elements' payload regions within the
    // packed buffer
    PackedBufferReader r(packedData);
    const uint32_t n = r.ReadU32();
    struct Dir
    {
        std::string name, typeHint;
        uint64_t offset, size;
    };
    std::vector<Dir> dir;
    dir.reserve(n);
    for (uint32_t i = 0; i < n; ++i)
    {
        Dir d;
        d.name     = r.ReadString();
        d.typeHint = r.ReadString();
        d.offset   = r.ReadU64();
        d.size     = r.ReadU64();
        dir.push_back(std::move(d));
    }

    // Deserialize the elements that still exist in the live elements map
    const size_t payloadStart = r.Position();
    for (const auto& d : dir)
    {
        TfToken token{d.name};

        // Skip stale entries
        auto it = elements.find(token);
        if (it == elements.end())
            continue;

        // Deserialize the element
        const size_t dataStart = payloadStart + d.offset;
        if (dataStart + d.size > packedData.size())
            continue;
        VtValue v = DeserializeElement(
            serializer, packedData.data() + dataStart, d.size, TfToken(d.typeHint));
        if (!v.IsEmpty())
        {
            it->second->SetResidentValue(v);
            auto& pe    = pageEntries[token];
            pe.offset   = d.offset;
            pe.size     = d.size;
            pe.typeHint = TfToken(d.typeHint);
        }
    }
    return true;
}

std::vector<uint8_t> HdPageableDataSourceUtils::SerializeVectorPacked(
    const std::vector<std::shared_ptr<HdPageableValue>>& elements,
    const IHdValueSerializer& serializer)
{
    // First, serialize each element individually to collect type hint and payload bytes
    struct Entry
    {
        TfToken typeHint;
        std::vector<uint8_t> data;
    };
    std::vector<Entry> collected;
    collected.reserve(elements.size());
    for (const auto& elem : elements)
    {
        VtValue v = elem->GetValueIfResident();
        if (!v.IsEmpty())
            collected.push_back({ elem->GetDataType(), serializer.Serialize(v) });
    }
    if (collected.empty())
        return {};

    // Compute directory size to pre-allocate the output buffer
    size_t headerSize = sizeof(uint32_t); // numElements
    for (const auto& e : collected)
    {
        headerSize += sizeof(uint32_t) + e.typeHint.GetString().size(); // typeHint
        headerSize += sizeof(uint64_t) * 2;                             // offset + size
    }
    std::vector<uint64_t> offsets;
    offsets.reserve(collected.size());
    uint64_t payloadOff = 0;
    for (const auto& e : collected)
    {
        offsets.push_back(payloadOff);
        payloadOff += e.data.size();
    }

    // Second, write the directory header followed by concatenated payloads
    // into a single contiguous buffer.
    PackedBufferWriter w;
    w.Reserve(headerSize + payloadOff);
    w.WriteU32(static_cast<uint32_t>(collected.size()));
    for (size_t i = 0; i < collected.size(); ++i)
    {
        w.WriteString(collected[i].typeHint.GetString());
        w.WriteU64(offsets[i]);
        w.WriteU64(static_cast<uint64_t>(collected[i].data.size()));
    }
    for (const auto& e : collected)
        w.WriteBytes(e.data.data(), e.data.size());
    return w.Release();
}

bool HdPageableDataSourceUtils::DeserializeVectorPacked(const std::vector<uint8_t>& packedData,
    std::vector<std::shared_ptr<HdPageableValue>>& elements,
    std::vector<HdContainerPageEntry>& pageEntries, const IHdValueSerializer& serializer)
{
    if (packedData.size() < sizeof(uint32_t))
        return false;

    // Read the directory to locate elements' payload regions within the
    // packed buffer
    PackedBufferReader r(packedData);
    const uint32_t n = r.ReadU32();
    struct Dir
    {
        std::string typeHint;
        uint64_t offset, size;
    };
    std::vector<Dir> dir;
    dir.reserve(n);
    for (uint32_t i = 0; i < n; ++i)
    {
        Dir d { r.ReadString(), r.ReadU64(), r.ReadU64() };
        dir.push_back(std::move(d));
    }

    // Deserialize the elements that still exist in the live elements map
    const size_t payloadStart = r.Position();
    for (size_t i = 0; i < dir.size() && i < elements.size(); ++i)
    {
        const auto& d          = dir[i];
        const size_t dataStart = payloadStart + d.offset;
        // Skip elements that are out of bounds (or maybe corrupted)
        if (dataStart + d.size > packedData.size())
            continue;

        // Deserialize and restore the element
        VtValue v = DeserializeElement(
            serializer, packedData.data() + dataStart, d.size, TfToken(d.typeHint));
        if (!v.IsEmpty())
        {
            elements[i]->SetResidentValue(v);
            if (i < pageEntries.size())
            {
                pageEntries[i].offset   = d.offset;
                pageEntries[i].size     = d.size;
                pageEntries[i].typeHint = TfToken(d.typeHint);
            }
        }
    }
    return true;
}

// Container paging operations ////////////////////////////////////////////////
//
// Paging strategy for containers and vectors:
// - All elements share ONE disk page entry (packed). Paging in any element
//   deserializes the whole pack; paging out one element only clears its
//   in-memory VtValue (the disk copy remains valid for future page-in).
// - SwapSceneToDisk serializes all elements, writes one packed blob, then
//   clears every element's resident value.
// - SwapToSceneMemory reads the packed blob and restores all elements.
//
// Thread safety: Get/GetElement use a two-phase lock pattern:
//   1. shared_lock — fast path for resident data (read-only, no contention)
//   2. unique_lock — slow path for implicit page-in (exclusive, serialized)
// PageIn/PageOut/Swap always acquire a unique_lock.

HdDataSourceBaseHandle HdPageableDataSourceUtils::ContainerGet(
    const TfToken& name, std::map<TfToken, std::shared_ptr<HdPageableValue>>& elements,
    std::map<TfToken, HdContainerPageEntry>& pageEntries, std::shared_mutex& mutex,
    bool enableImplicitPaging, bool hasValidDiskBuffer,
    std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager, const IHdValueSerializer& serializer,
    HvtDebugCounter& accessCount, HvtDebugCounter& pageInCount)
{
    ++accessCount;
    // Fast path: data is resident
    {
        std::shared_lock<std::shared_mutex> readLock(mutex);
        auto it = elements.find(name);
        if (it == elements.end())
            return nullptr;

        if (it->second->IsDataResident())
        {
            VtValue value = it->second->GetValueIfResident();
            if (!value.IsEmpty())
                return HdRetainedSampledDataSource::New(value);
        }
    }

    // Slow path: page in from disk under exclusive lock.
    if (enableImplicitPaging && hasValidDiskBuffer)
    {
        std::unique_lock<std::shared_mutex> writeLock(mutex);
        auto it = elements.find(name);
        if (it == elements.end())
            return nullptr;

        // Load and unpack the container from disk
        if (!it->second->IsDataResident())
        {
            auto packed = LoadPackedFromDisk(*pageEntry, pageFileManager);
            if (!packed.empty())
            {
                DeserializeContainerPacked(packed, elements, pageEntries, serializer);
                ++pageInCount;
            }
        }
        VtValue value = it->second->GetValueIfResident();
        if (!value.IsEmpty())
            return HdRetainedSampledDataSource::New(value);
    }
    return nullptr;
}

bool HdPageableDataSourceUtils::ContainerPageIn(
    const TfToken& name, std::map<TfToken, std::shared_ptr<HdPageableValue>>& elements,
    std::map<TfToken, HdContainerPageEntry>& pageEntries, std::shared_mutex& mutex,
    bool hasValidDiskBuffer, std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager, const IHdValueSerializer& serializer,
    HvtDebugCounter& pageInCount)
{
    if (!hasValidDiskBuffer)
        return false;

    // Acquire the exclusive lock
    std::unique_lock<std::shared_mutex> writeLock(mutex);
    auto it = elements.find(name);
    if (it == elements.end() || it->second->IsDataResident())
        return it != elements.end();

    // Load and unpack the container from disk
    auto packed = LoadPackedFromDisk(*pageEntry, pageFileManager);
    if (!packed.empty() && DeserializeContainerPacked(packed, elements, pageEntries, serializer))
    {
        ++pageInCount;
        return true;
    }
    return false;
}

bool HdPageableDataSourceUtils::ContainerPageOut(const TfToken& name,
    std::map<TfToken, std::shared_ptr<HdPageableValue>>& elements, std::shared_mutex& mutex,
    bool hasValidDiskBuffer, std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager, HdBufferState& bufferState,
    const IHdValueSerializer& serializer, HvtDebugCounter& pageOutCount)
{
    std::unique_lock<std::shared_mutex> writeLock(mutex);
    auto it = elements.find(name);
    if (it == elements.end())
        return false;

    // If no valid disk copy exists yet, serialize and write to disk
    if (!hasValidDiskBuffer)
    {
        auto packed = SerializeContainerPacked(elements, serializer);
        if (packed.empty() || !WritePackedToDisk(packed, pageEntry, pageFileManager, bufferState))
            return false;
    }

    // Clear the resident value and update the page out count
    it->second->ClearResidentValue();
    ++pageOutCount;
    return true;
}

bool HdPageableDataSourceUtils::ContainerSwapToDisk(
    std::map<TfToken, std::shared_ptr<HdPageableValue>>& elements, bool force,
    std::shared_mutex& mutex, std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager, HdBufferState& bufferState,
    const IHdValueSerializer& serializer, HvtDebugCounter& pageOutCount)
{
    std::unique_lock<std::shared_mutex> writeLock(mutex);
    if (elements.empty() && !force)
        return false;

    // Serialize the container and write to disk
    auto packed = SerializeContainerPacked(elements, serializer);
    if (packed.empty() && !force)
        return false;

    // Write the packed buffer to disk
    if (!WritePackedToDisk(packed, pageEntry, pageFileManager, bufferState))
        return false;

    // Clear the resident values and update the buffer state
    for (auto& [token, element] : elements)
    {
        element->ClearResidentValue();
    }
    bufferState = static_cast<HdBufferState>(
        static_cast<int>(bufferState) & ~static_cast<int>(HdBufferState::SceneBuffer));
    ++pageOutCount;
    return true;
}

bool HdPageableDataSourceUtils::ContainerSwapToMemory(
    std::map<TfToken, std::shared_ptr<HdPageableValue>>& elements,
    std::map<TfToken, HdContainerPageEntry>& pageEntries, std::shared_mutex& mutex,
    bool hasValidDiskBuffer, std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager, HdBufferState& bufferState,
    const IHdValueSerializer& serializer, HvtDebugCounter& pageInCount)
{
    if (!hasValidDiskBuffer)
        return false;
    std::unique_lock<std::shared_mutex> writeLock(mutex);

    auto packed = LoadPackedFromDisk(*pageEntry, pageFileManager);
    if (packed.empty() || !DeserializeContainerPacked(packed, elements, pageEntries, serializer))
        return false;
    bufferState = static_cast<HdBufferState>(
        static_cast<int>(bufferState) | static_cast<int>(HdBufferState::SceneBuffer));
    ++pageInCount;
    return true;
}

// Vector paging operations ////////////////////////////////////////////////////
// Same packed-storage and locking strategy as containers (see above).

HdDataSourceBaseHandle HdPageableDataSourceUtils::VectorGetElement(
    size_t element, std::vector<std::shared_ptr<HdPageableValue>>& elements,
    std::vector<HdContainerPageEntry>& pageEntries, std::shared_mutex& mutex,
    bool enableImplicitPaging, bool hasValidDiskBuffer,
    std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager, const IHdValueSerializer& serializer,
    HvtDebugCounter& accessCount, HvtDebugCounter& pageInCount)
{
    ++accessCount;
    // Fast path: data is resident
    {
        std::shared_lock<std::shared_mutex> readLock(mutex);
        if (element >= elements.size())
            return nullptr;

        if (elements[element]->IsDataResident())
        {
            VtValue value = elements[element]->GetValueIfResident();
            if (!value.IsEmpty())
                return HdRetainedSampledDataSource::New(value);
        }
    }

    // Slow path: page in from disk under exclusive lock.
    if (enableImplicitPaging && hasValidDiskBuffer)
    {
        std::unique_lock<std::shared_mutex> writeLock(mutex);
        // Only page in if the element is healthy and not resident
        if (element < elements.size() && !elements[element]->IsDataResident())
        {
            auto packed = LoadPackedFromDisk(*pageEntry, pageFileManager);
            if (!packed.empty())
            {
                DeserializeVectorPacked(packed, elements, pageEntries, serializer);
                ++pageInCount;
            }
        }
        if (element < elements.size())
        {
            VtValue value = elements[element]->GetValueIfResident();
            if (!value.IsEmpty())
                return HdRetainedSampledDataSource::New(value);
        }
    }
    return nullptr;
}

bool HdPageableDataSourceUtils::VectorPageIn(
    size_t index, std::vector<std::shared_ptr<HdPageableValue>>& elements,
    std::vector<HdContainerPageEntry>& pageEntries, std::shared_mutex& mutex,
    bool hasValidDiskBuffer, std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager, const IHdValueSerializer& serializer,
    HvtDebugCounter& pageInCount)
{
    if (!hasValidDiskBuffer)
        return false;

    std::unique_lock<std::shared_mutex> writeLock(mutex);
    if (index >= elements.size() || elements[index]->IsDataResident())
        return index < elements.size();

    // Load and unpack the vector from disk
    auto packed = LoadPackedFromDisk(*pageEntry, pageFileManager);
    if (!packed.empty() && DeserializeVectorPacked(packed, elements, pageEntries, serializer))
    {
        ++pageInCount;
        return true;
    }
    return false;
}

// Lazy write — see ContainerPageOut.
bool HdPageableDataSourceUtils::VectorPageOut(
    size_t index, std::vector<std::shared_ptr<HdPageableValue>>& elements, std::shared_mutex& mutex,
    bool hasValidDiskBuffer, std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager, HdBufferState& bufferState,
    const IHdValueSerializer& serializer, HvtDebugCounter& pageOutCount)
{
    std::unique_lock<std::shared_mutex> writeLock(mutex);
    if (index >= elements.size())
        return false;

    // If no valid disk copy exists yet, serialize and write to disk
    if (!hasValidDiskBuffer)
    {
        auto packed = SerializeVectorPacked(elements, serializer);
        if (packed.empty() || !WritePackedToDisk(packed, pageEntry, pageFileManager, bufferState))
            return false;
    }
    elements[index]->ClearResidentValue();
    ++pageOutCount;
    return true;
}

bool HdPageableDataSourceUtils::VectorSwapToDisk(
    std::vector<std::shared_ptr<HdPageableValue>>& elements, bool force, std::shared_mutex& mutex,
    std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager, HdBufferState& bufferState,
    const IHdValueSerializer& serializer, HvtDebugCounter& pageOutCount)
{
    std::unique_lock<std::shared_mutex> writeLock(mutex);
    if (elements.empty() && !force)
        return false;

    // Serialize the vector and write to disk
    auto packed = SerializeVectorPacked(elements, serializer);
    if (packed.empty() && !force)
        return false;

    // Write the packed buffer to disk
    if (!WritePackedToDisk(packed, pageEntry, pageFileManager, bufferState))
        return false;

    // Clear the resident value and update the page out count
    for (auto& element : elements)
        element->ClearResidentValue();
    bufferState = static_cast<HdBufferState>(
        static_cast<int>(bufferState) & ~static_cast<int>(HdBufferState::SceneBuffer));
    ++pageOutCount;
    return true;
}

bool HdPageableDataSourceUtils::VectorSwapToMemory(
    std::vector<std::shared_ptr<HdPageableValue>>& elements,
    std::vector<HdContainerPageEntry>& pageEntries, std::shared_mutex& mutex,
    bool hasValidDiskBuffer, std::unique_ptr<HdBufferPageEntry>& pageEntry,
    std::unique_ptr<HdPageFileManager>& pageFileManager, HdBufferState& bufferState,
    const IHdValueSerializer& serializer, HvtDebugCounter& pageInCount)
{
    if (!hasValidDiskBuffer)
        return false;

    std::unique_lock<std::shared_mutex> writeLock(mutex);
    auto packed = LoadPackedFromDisk(*pageEntry, pageFileManager);
    if (packed.empty())
        return false;
    if (!DeserializeVectorPacked(packed, elements, pageEntries, serializer))
        return false;
    bufferState = static_cast<HdBufferState>(
        static_cast<int>(bufferState) | static_cast<int>(HdBufferState::SceneBuffer));
    ++pageInCount;
    return true;
}

std::string HdPageableDataSourceUtils::SampledGetBufferKey(
    HdSampledDataSource::Time time, const SdfPath& primPath, const TfToken& attributeName)
{
    return TfStringPrintf("%s_%s_%g", primPath.GetText(), attributeName.GetText(), time);
}

// HdDefaultValueSerializer Implementation ////////////////////////////////////

bool HdDefaultValueSerializer::CanSerialize(const std::type_index& type) const
{
    static const std::set<std::type_index> supportedTypes = {
        typeid(VtFloatArray),
        typeid(VtDoubleArray),
        typeid(VtIntArray),
        typeid(VtUIntArray),
        typeid(VtInt64Array),
        typeid(VtUInt64Array),
        typeid(VtHalfArray),
        typeid(VtVec2fArray),
        typeid(VtVec2dArray),
        typeid(VtVec2iArray),
        typeid(VtVec3fArray),
        typeid(VtVec3dArray),
        typeid(VtVec3iArray),
        typeid(VtVec4fArray),
        typeid(VtVec4dArray),
        typeid(VtVec4iArray),
        typeid(VtMatrix4fArray),
        typeid(VtMatrix4dArray),
        typeid(VtQuatfArray),
        typeid(VtQuatdArray),
        typeid(VtStringArray),
        typeid(VtTokenArray),
    };
    return supportedTypes.find(type) != supportedTypes.end();
}

std::vector<uint8_t> HdDefaultValueSerializer::Serialize(const VtValue& value) const
{
    // POD arrays: single allocation with 1-byte type tag prefix
    if (value.IsHolding<VtFloatArray>())
        return SerializePodTagged(VtTypeTag::FloatArray, value.UncheckedGet<VtFloatArray>());
    if (value.IsHolding<VtDoubleArray>())
        return SerializePodTagged(VtTypeTag::DoubleArray, value.UncheckedGet<VtDoubleArray>());
    if (value.IsHolding<VtHalfArray>())
        return SerializePodTagged(VtTypeTag::HalfArray, value.UncheckedGet<VtHalfArray>());
    if (value.IsHolding<VtIntArray>())
        return SerializePodTagged(VtTypeTag::IntArray, value.UncheckedGet<VtIntArray>());
    if (value.IsHolding<VtUIntArray>())
        return SerializePodTagged(VtTypeTag::UIntArray, value.UncheckedGet<VtUIntArray>());
    if (value.IsHolding<VtInt64Array>())
        return SerializePodTagged(VtTypeTag::Int64Array, value.UncheckedGet<VtInt64Array>());
    if (value.IsHolding<VtUInt64Array>())
        return SerializePodTagged(VtTypeTag::UInt64Array, value.UncheckedGet<VtUInt64Array>());
    if (value.IsHolding<VtVec2fArray>())
        return SerializePodTagged(VtTypeTag::Vec2fArray, value.UncheckedGet<VtVec2fArray>());
    if (value.IsHolding<VtVec2dArray>())
        return SerializePodTagged(VtTypeTag::Vec2dArray, value.UncheckedGet<VtVec2dArray>());
    if (value.IsHolding<VtVec2iArray>())
        return SerializePodTagged(VtTypeTag::Vec2iArray, value.UncheckedGet<VtVec2iArray>());
    if (value.IsHolding<VtVec3fArray>())
        return SerializePodTagged(VtTypeTag::Vec3fArray, value.UncheckedGet<VtVec3fArray>());
    if (value.IsHolding<VtVec3dArray>())
        return SerializePodTagged(VtTypeTag::Vec3dArray, value.UncheckedGet<VtVec3dArray>());
    if (value.IsHolding<VtVec3iArray>())
        return SerializePodTagged(VtTypeTag::Vec3iArray, value.UncheckedGet<VtVec3iArray>());
    if (value.IsHolding<VtVec4fArray>())
        return SerializePodTagged(VtTypeTag::Vec4fArray, value.UncheckedGet<VtVec4fArray>());
    if (value.IsHolding<VtVec4dArray>())
        return SerializePodTagged(VtTypeTag::Vec4dArray, value.UncheckedGet<VtVec4dArray>());
    if (value.IsHolding<VtVec4iArray>())
        return SerializePodTagged(VtTypeTag::Vec4iArray, value.UncheckedGet<VtVec4iArray>());
    if (value.IsHolding<VtMatrix4fArray>())
        return SerializePodTagged(VtTypeTag::Matrix4fArray, value.UncheckedGet<VtMatrix4fArray>());
    if (value.IsHolding<VtMatrix4dArray>())
        return SerializePodTagged(VtTypeTag::Matrix4dArray, value.UncheckedGet<VtMatrix4dArray>());
    if (value.IsHolding<VtQuatfArray>())
        return SerializePodTagged(VtTypeTag::QuatfArray, value.UncheckedGet<VtQuatfArray>());
    if (value.IsHolding<VtQuatdArray>())
        return SerializePodTagged(VtTypeTag::QuatdArray, value.UncheckedGet<VtQuatdArray>());

    // Variable-length types
    if (value.IsHolding<VtStringArray>())
    {
        return SerializeStringArrayTagged(VtTypeTag::StringArray,
            value.UncheckedGet<VtStringArray>(),
            [](const std::string& s) -> const std::string& { return s; });
    }

    if (value.IsHolding<VtTokenArray>())
    {
        return SerializeStringArrayTagged(VtTypeTag::TokenArray, value.UncheckedGet<VtTokenArray>(),
            [](const TfToken& t) -> const std::string& { return t.GetString(); });
    }

    TF_WARN("HdDefaultValueSerializer: Unsupported type for serialization: %s",
        value.GetTypeName().c_str());
    return {};
}

VtValue HdDefaultValueSerializer::DeserializeFromSpan(
    const uint8_t* data, size_t size, const TfToken& /*typeHint*/) const
{
    if (size < kTypeTagSize)
    {
        return {};
    }

    const auto tag           = static_cast<VtTypeTag>(data[0]);
    const uint8_t* payload   = data + kTypeTagSize;
    const size_t payloadSize = size - kTypeTagSize;

    switch (tag)
    {
    case VtTypeTag::FloatArray:
        return VtValue(DeserializePodDirect<float>(payload, payloadSize));
    case VtTypeTag::DoubleArray:
        return VtValue(DeserializePodDirect<double>(payload, payloadSize));
    case VtTypeTag::HalfArray:
        return VtValue(DeserializePodDirect<GfHalf>(payload, payloadSize));
    case VtTypeTag::IntArray:
        return VtValue(DeserializePodDirect<int>(payload, payloadSize));
    case VtTypeTag::UIntArray:
        return VtValue(DeserializePodDirect<unsigned int>(payload, payloadSize));
    case VtTypeTag::Int64Array:
        return VtValue(DeserializePodDirect<int64_t>(payload, payloadSize));
    case VtTypeTag::UInt64Array:
        return VtValue(DeserializePodDirect<uint64_t>(payload, payloadSize));
    case VtTypeTag::Vec2fArray:
        return VtValue(DeserializePodDirect<GfVec2f>(payload, payloadSize));
    case VtTypeTag::Vec2dArray:
        return VtValue(DeserializePodDirect<GfVec2d>(payload, payloadSize));
    case VtTypeTag::Vec2iArray:
        return VtValue(DeserializePodDirect<GfVec2i>(payload, payloadSize));
    case VtTypeTag::Vec3fArray:
        return VtValue(DeserializePodDirect<GfVec3f>(payload, payloadSize));
    case VtTypeTag::Vec3dArray:
        return VtValue(DeserializePodDirect<GfVec3d>(payload, payloadSize));
    case VtTypeTag::Vec3iArray:
        return VtValue(DeserializePodDirect<GfVec3i>(payload, payloadSize));
    case VtTypeTag::Vec4fArray:
        return VtValue(DeserializePodDirect<GfVec4f>(payload, payloadSize));
    case VtTypeTag::Vec4dArray:
        return VtValue(DeserializePodDirect<GfVec4d>(payload, payloadSize));
    case VtTypeTag::Vec4iArray:
        return VtValue(DeserializePodDirect<GfVec4i>(payload, payloadSize));
    case VtTypeTag::Matrix4fArray:
        return VtValue(DeserializePodDirect<GfMatrix4f>(payload, payloadSize));
    case VtTypeTag::Matrix4dArray:
        return VtValue(DeserializePodDirect<GfMatrix4d>(payload, payloadSize));
    case VtTypeTag::QuatfArray:
        return VtValue(DeserializePodDirect<GfQuatf>(payload, payloadSize));
    case VtTypeTag::QuatdArray:
        return VtValue(DeserializePodDirect<GfQuatd>(payload, payloadSize));

    case VtTypeTag::StringArray:
        return DeserializeStringArrayDirect<VtStringArray>(
            payload, payloadSize, [](std::string s) -> std::string { return s; });
    case VtTypeTag::TokenArray:
        return DeserializeStringArrayDirect<VtTokenArray>(
            payload, payloadSize, [](std::string s) { return TfToken(std::move(s)); });
    default:
        TF_WARN("HdDefaultValueSerializer: Unknown type tag %d", static_cast<int>(tag));
        return {};
    }
}

VtValue HdDefaultValueSerializer::Deserialize(
    const std::vector<uint8_t>& data, const TfToken& typeHint) const
{
    return DeserializeFromSpan(data.data(), data.size(), typeHint);
}

size_t HdDefaultValueSerializer::EstimateSize(const VtValue& value) const
{
    // Float arrays
    if (value.IsHolding<VtFloatArray>())
        return value.UncheckedGet<VtFloatArray>().size() * sizeof(float);
    if (value.IsHolding<VtDoubleArray>())
        return value.UncheckedGet<VtDoubleArray>().size() * sizeof(double);
    if (value.IsHolding<VtHalfArray>())
        return value.UncheckedGet<VtHalfArray>().size() * sizeof(GfHalf);

    // Integer arrays
    if (value.IsHolding<VtIntArray>())
        return value.UncheckedGet<VtIntArray>().size() * sizeof(int);
    if (value.IsHolding<VtUIntArray>())
        return value.UncheckedGet<VtUIntArray>().size() * sizeof(unsigned int);
    if (value.IsHolding<VtInt64Array>())
        return value.UncheckedGet<VtInt64Array>().size() * sizeof(int64_t);
    if (value.IsHolding<VtUInt64Array>())
        return value.UncheckedGet<VtUInt64Array>().size() * sizeof(uint64_t);

    // Vec2 arrays
    if (value.IsHolding<VtVec2fArray>())
        return value.UncheckedGet<VtVec2fArray>().size() * sizeof(GfVec2f);
    if (value.IsHolding<VtVec2dArray>())
        return value.UncheckedGet<VtVec2dArray>().size() * sizeof(GfVec2d);
    if (value.IsHolding<VtVec2iArray>())
        return value.UncheckedGet<VtVec2iArray>().size() * sizeof(GfVec2i);

    // Vec3 arrays
    if (value.IsHolding<VtVec3fArray>())
        return value.UncheckedGet<VtVec3fArray>().size() * sizeof(GfVec3f);
    if (value.IsHolding<VtVec3dArray>())
        return value.UncheckedGet<VtVec3dArray>().size() * sizeof(GfVec3d);
    if (value.IsHolding<VtVec3iArray>())
        return value.UncheckedGet<VtVec3iArray>().size() * sizeof(GfVec3i);

    // Vec4 arrays
    if (value.IsHolding<VtVec4fArray>())
        return value.UncheckedGet<VtVec4fArray>().size() * sizeof(GfVec4f);
    if (value.IsHolding<VtVec4dArray>())
        return value.UncheckedGet<VtVec4dArray>().size() * sizeof(GfVec4d);
    if (value.IsHolding<VtVec4iArray>())
        return value.UncheckedGet<VtVec4iArray>().size() * sizeof(GfVec4i);

    // Matrix arrays
    if (value.IsHolding<VtMatrix4fArray>())
        return value.UncheckedGet<VtMatrix4fArray>().size() * sizeof(GfMatrix4f);
    if (value.IsHolding<VtMatrix4dArray>())
        return value.UncheckedGet<VtMatrix4dArray>().size() * sizeof(GfMatrix4d);

    // Quaternion arrays
    if (value.IsHolding<VtQuatfArray>())
        return value.UncheckedGet<VtQuatfArray>().size() * sizeof(GfQuatf);
    if (value.IsHolding<VtQuatdArray>())
        return value.UncheckedGet<VtQuatdArray>().size() * sizeof(GfQuatd);

    // String arrays
    if (value.IsHolding<VtStringArray>())
    {
        const auto& array = value.UncheckedGet<VtStringArray>();
        size_t size       = sizeof(size_t); // count header
        for (const auto& str : array)
        {
            size += sizeof(size_t) + str.size();
        }
        return size;
    }

    // Token arrays
    if (value.IsHolding<VtTokenArray>())
    {
        const auto& array = value.UncheckedGet<VtTokenArray>();
        size_t size       = sizeof(size_t); // count header
        for (const auto& token : array)
        {
            size += sizeof(size_t) + token.GetString().size();
        }
        return size;
    }

    TF_WARN("HdDefaultValueSerializer: Unknown type for size estimation: %s",
        value.GetTypeName().c_str());
    return 1024; // Default estimate
}

// HdPageableValue Implementation /////////////////////////////////////////////
//
// HdPageableValue stores a single VtValue that can be paged to/from disk.
// Unlike container/vector data sources which pack multiple elements into one
// disk page, HdPageableValue owns its own individual HdBufferPageEntry.
//
// Disk format: the raw output of IHdValueSerializer::Serialize() — typically
// [uint8 VtTypeTag][payload] for the default serializer.
//
// Serialized cache (mSerializedCache): lazily populated on first disk write
// or span access, cleared when the source value changes. This avoids repeated
// serialization when the same value is written multiple times.

HdPageableValue::HdPageableValue(const SdfPath& path, size_t estimatedSize, HdBufferUsage usage,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    const VtValue& data, const TfToken& dataType, bool enableImplicitPaging,
    const IHdValueSerializer* serializer) :
    HdPageableBufferBase<>(
        path, estimatedSize, usage, pageFileManager, memoryMonitor, destructionCallback),
    mSourceValue(data),
    mDataType(dataType),
    mSerializer(serializer),
    mEnableImplicitPaging(enableImplicitPaging)
{
    HdPageableBufferBase<>::CreateSceneBuffer();
    mCurrentStatus = HdPagingStatus::Resident;
}

VtValue HdPageableValue::GetValue(bool* outPagedIn)
{
    if (outPagedIn)
    {
        *outPagedIn = false;
    }

    ++mAccessCount;

    // Fast path: data is resident
    if (IsDataResident())
    {
        std::shared_lock<std::shared_mutex> readLock(mDataMutex);
        return mSourceValue;
    }

    if (!mEnableImplicitPaging)
    {
        return {};
    }

    // Slow path: page in from disk under exclusive lock.
    std::unique_lock<std::shared_mutex> writeLock(mDataMutex);

    // Another thread may have paged in while we waited for the lock.
    if (HasSceneBuffer())
    {
        return mSourceValue;
    }

    mCurrentStatus = HdPagingStatus::Loading;

    // Load from disk directly into mSourceValue.
    // NOTE: DataSource doesn't have knowledge of VRAM and is packed when storing,
    // so we bypass the base PageToSceneMemory.
    if (HasValidDiskBuffer())
    {
        std::vector<uint8_t> buffer(mPageEntry->Size());
        if (mPageFileManager->LoadPage(*mPageEntry, buffer.data()))
        {
            mSourceValue = DeserializeVtValue(buffer);
            mSerializedCache.clear();
            HdPageableBufferBase<>::CreateSceneBuffer();
            ++mPageInCount;
            mCurrentStatus = HdPagingStatus::Resident;
            if (outPagedIn)
            {
                *outPagedIn = true;
            }
            return mSourceValue;
        }
    }

    mCurrentStatus = HdPagingStatus::Invalid;
    return {};
}

VtValue HdPageableValue::GetValueIfResident() const
{
    std::shared_lock<std::shared_mutex> readLock(mDataMutex);
    if (HasSceneBuffer())
    {
        return mSourceValue;
    }
    return {};
}

bool HdPageableValue::WillPageOnAccess() const
{
    return mEnableImplicitPaging && !IsDataResident() && HasValidDiskBuffer();
}

bool HdPageableValue::IsDataResident() const
{
    return HdPageableBufferBase<>::HasSceneBuffer();
}

HdPagingStatus HdPageableValue::GetStatus() const
{
    return mCurrentStatus.load();
}

bool HdPageableValue::SwapToSceneMemory(bool /*force*/, HdBufferState releaseBuffer)
{
    std::unique_lock<std::shared_mutex> writeLock(mDataMutex);

    mCurrentStatus = HdPagingStatus::Loading;

    // Bypass the base SwapToSceneMemory to load from disk directly.
    if (HasValidDiskBuffer())
    {
        std::vector<uint8_t> buffer(mPageEntry->Size());
        if (mPageFileManager->LoadPage(*mPageEntry, buffer.data()))
        {
            mSourceValue = DeserializeVtValue(buffer);
            mSerializedCache.clear();
            HdPageableBufferBase<>::CreateSceneBuffer();

            // Remove other buffers and update status
            if (static_cast<int>(releaseBuffer) & static_cast<int>(HdBufferState::RendererBuffer))
                ReleaseRendererBuffer();
            if (static_cast<int>(releaseBuffer) & static_cast<int>(HdBufferState::DiskBuffer))
                ReleaseDiskPage();

            ++mPageInCount;
            mCurrentStatus = HdPagingStatus::Resident;
            return true;
        }
    }

    mCurrentStatus = HdPagingStatus::Invalid;
    return false;
}

bool HdPageableValue::SwapSceneToDisk(bool force, HdBufferState releaseBuffer)
{
    std::unique_lock<std::shared_mutex> writeLock(mDataMutex);

    if (mSourceValue.IsEmpty() && !force)
    {
        return false;
    }

    mCurrentStatus = HdPagingStatus::Saving;

    UpdateSerializedCache();

    if (mSerializedCache.empty())
    {
        mCurrentStatus = HdPagingStatus::Invalid;
        return false;
    }

    // Try in-place update if page entry already exists with matching size;
    // otherwise release the old slot and allocate a new one.
    bool written = false;
    if (mPageEntry && mPageEntry->IsValid() && mPageEntry->Size() == mSerializedCache.size())
    {
        written = mPageFileManager->UpdatePage(*mPageEntry, mSerializedCache.data());
    }
    if (!written)
    {
        if (mPageEntry)
            mPageFileManager->ReleasePage(*mPageEntry);
        mPageEntry =
            mPageFileManager->CreatePageEntry(mSerializedCache.data(), mSerializedCache.size());
        if (!mPageEntry)
        {
            mCurrentStatus = HdPagingStatus::Invalid;
            return false;
        }
    }

    // Release other buffers and update status
    mBufferState = static_cast<HdBufferState>(
        static_cast<int>(mBufferState) | static_cast<int>(HdBufferState::DiskBuffer));
    if (static_cast<int>(releaseBuffer) & static_cast<int>(HdBufferState::SceneBuffer))
        ReleaseSceneBuffer();
    if (static_cast<int>(releaseBuffer) & static_cast<int>(HdBufferState::RendererBuffer))
        ReleaseRendererBuffer();

    mSourceValue = VtValue();
    mSerializedCache.clear();
    ++mPageOutCount;
    mCurrentStatus = HdPagingStatus::PagedOut;
    return true;
}

TfSpan<const std::byte> HdPageableValue::GetSceneMemorySpan() const noexcept
{
    UpdateSerializedCache();
    return TfSpan<const std::byte>(
        reinterpret_cast<const std::byte*>(mSerializedCache.data()), mSerializedCache.size());
}

TfSpan<std::byte> HdPageableValue::GetSceneMemorySpan() noexcept
{
    UpdateSerializedCache();
    return TfSpan<std::byte>(
        reinterpret_cast<std::byte*>(mSerializedCache.data()), mSerializedCache.size());
}

void HdPageableValue::UpdateSerializedCache() const
{
    if (mSerializedCache.empty() && !mSourceValue.IsEmpty())
    {
        mSerializedCache = SerializeVtValue(mSourceValue);
    }
}

std::vector<uint8_t> HdPageableValue::SerializeVtValue(const VtValue& value) const noexcept
{
    const auto* s = mSerializer ? mSerializer : &GetDefaultSerializer();
    return s->Serialize(value);
}

VtValue HdPageableValue::DeserializeVtValue(const std::vector<uint8_t>& data) noexcept
{
    const auto* s = mSerializer ? mSerializer : &GetDefaultSerializer();
    return s->Deserialize(data, mDataType);
}

void HdPageableValue::SetResidentValue(const VtValue& value)
{
    std::unique_lock<std::shared_mutex> writeLock(mDataMutex);
    mSourceValue = value;
    mSerializedCache.clear();
    if (!HasSceneBuffer())
    {
        HdPageableBufferBase<>::CreateSceneBuffer();
    }
    mCurrentStatus = HdPagingStatus::Resident;
    SetSize(EstimateMemoryUsage(value));
}

void HdPageableValue::ClearResidentValue()
{
    std::unique_lock<std::shared_mutex> writeLock(mDataMutex);
    mSourceValue = VtValue();
    mSerializedCache.clear();
    if (HasSceneBuffer())
    {
        HdPageableBufferBase<>::ReleaseSceneBuffer();
    }
    mCurrentStatus = HdPagingStatus::PagedOut;
}

size_t HdPageableValue::EstimateMemoryUsage(const VtValue& value) noexcept
{
    return GetDefaultSerializer().EstimateSize(value);
}

size_t HdPageableValue::EstimateMemoryUsage() const noexcept
{
    const auto* s = mSerializer ? mSerializer : &GetDefaultSerializer();
    std::shared_lock<std::shared_mutex> readLock(mDataMutex);
    return s->EstimateSize(mSourceValue);
}

// HdPageableContainerDataSource Implementation ///////////////////////////////
//
// Stores named data elements (e.g. points, normals, indices) as individual
// HdPageableValue instances in a map keyed by TfToken. All elements are packed
// into a single disk page for I/O, tracked via mContainerPageEntries which
// records each element's byte offset and size within that packed blob.

HdPageableContainerDataSource::Handle HdPageableContainerDataSource::New(
    const std::map<TfToken, VtValue>& values, const SdfPath& primPath,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging)
{
    auto result = Handle(new HdPageableContainerDataSource(primPath, pageFileManager, memoryMonitor,
        destructionCallback, usage, enableImplicitPaging));

    // Create pageable values for each element and store them in the map
    for (const auto& [token, value] : values)
    {
        auto elementPath     = primPath.AppendProperty(token);
        size_t estimatedSize = HdPageableValue::EstimateMemoryUsage(value);

        auto pageableValue = std::make_shared<HdPageableValue>(elementPath, estimatedSize, usage,
            pageFileManager, memoryMonitor, HdPageableDataSourceUtils::kNoOpDestructionCallback,
            value, token, result->mEnableImplicitPaging, result->mSerializer.get());

        result->mElements[token] = pageableValue;
        result->mContainerPageEntries[token] =
            HdContainerPageEntry { typeid(value), token, 0, estimatedSize };
    }

    return result;
}

HdPageableContainerDataSource::Handle HdPageableContainerDataSource::New(const SdfPath& primPath,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging)
{
    return Handle(new HdPageableContainerDataSource(primPath, pageFileManager, memoryMonitor,
        destructionCallback, usage, enableImplicitPaging));
}

HdPageableContainerDataSource::HdPageableContainerDataSource(const SdfPath& primPath,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging) :
    HdContainerDataSource(),
    HdPageableBufferBase<>(primPath, 0, usage, pageFileManager, memoryMonitor, destructionCallback),
    mSerializer(std::make_shared<HdDefaultValueSerializer>()),
    mEnableImplicitPaging(enableImplicitPaging)
{
}

TfTokenVector HdPageableContainerDataSource::GetNames()
{
    std::shared_lock<std::shared_mutex> readLock(mElementsMutex);
    TfTokenVector names;
    names.reserve(mElements.size());
    for (const auto& [token, _] : mElements)
        names.push_back(token);
    return names;
}

HdDataSourceBaseHandle HdPageableContainerDataSource::Get(const TfToken& name)
{
    return HdPageableDataSourceUtils::ContainerGet(
        name, mElements, mContainerPageEntries, mElementsMutex, mEnableImplicitPaging,
        HasValidDiskBuffer(), mPageEntry, mPageFileManager, *mSerializer,
        mAccessCount, mPageInCount);
}

std::map<TfToken, HdContainerPageEntry> HdPageableContainerDataSource::GetMemoryBreakdown() const
{
    std::shared_lock<std::shared_mutex> readLock(mElementsMutex);
    return mContainerPageEntries;
}

bool HdPageableContainerDataSource::IsElementResident(const TfToken& name) const
{
    std::shared_lock<std::shared_mutex> readLock(mElementsMutex);
    auto it = mElements.find(name);
    return it != mElements.end() && it->second->IsDataResident();
}

bool HdPageableContainerDataSource::PageInElement(const TfToken& name)
{
    return HdPageableDataSourceUtils::ContainerPageIn(name, mElements, mContainerPageEntries,
        mElementsMutex, HasValidDiskBuffer(), mPageEntry, mPageFileManager, *mSerializer,
        mPageInCount);
}

bool HdPageableContainerDataSource::PageOutElement(const TfToken& name)
{
    return HdPageableDataSourceUtils::ContainerPageOut(name, mElements, mElementsMutex,
        HasValidDiskBuffer(), mPageEntry, mPageFileManager, mBufferState, *mSerializer,
        mPageOutCount);
}

bool HdPageableContainerDataSource::SwapSceneToDisk(bool force, HdBufferState /*releaseBuffer*/)
{
    return HdPageableDataSourceUtils::ContainerSwapToDisk(
        mElements, force, mElementsMutex, mPageEntry, mPageFileManager, mBufferState, *mSerializer,
        mPageOutCount);
}

bool HdPageableContainerDataSource::SwapToSceneMemory(
    bool /*force*/, HdBufferState /*releaseBuffer*/)
{
    return HdPageableDataSourceUtils::ContainerSwapToMemory(
        mElements, mContainerPageEntries, mElementsMutex, HasValidDiskBuffer(), mPageEntry,
        mPageFileManager, mBufferState, *mSerializer, mPageInCount);
}

// HdPageableVectorDataSource Implementation //////////////////////////////////
//
// Index-addressed variant of the packed paging model. Elements are stored in a
// vector and packed into a single disk page identically to containers, except
// the directory uses indices instead of token names.

HdPageableVectorDataSource::Handle HdPageableVectorDataSource::New(
    const std::vector<VtValue>& values, const SdfPath& primPath,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging)
{
    auto result = Handle(new HdPageableVectorDataSource(primPath, pageFileManager, memoryMonitor,
        destructionCallback, usage, enableImplicitPaging));

    // Create pageable values for each element and store them in the vector
    for (size_t index = 0; index < values.size(); ++index)
    {
        const auto& value    = values[index];
        auto elementPath     = SdfPath(TfStringPrintf("%s_%zu", primPath.GetText(), index));
        size_t estimatedSize = HdPageableValue::EstimateMemoryUsage(value);

        TfToken dataType(TfStringPrintf("element_%zu", index));
        auto pageableValue = std::make_shared<HdPageableValue>(elementPath, estimatedSize, usage,
            pageFileManager, memoryMonitor, HdPageableDataSourceUtils::kNoOpDestructionCallback,
            value, dataType, result->mEnableImplicitPaging, result->mSerializer.get());

        result->mElements.push_back(pageableValue);
        result->mElementPageEntries.push_back(
            HdContainerPageEntry { typeid(value), dataType, 0, estimatedSize });
    }

    return result;
}

HdPageableVectorDataSource::Handle HdPageableVectorDataSource::New(const SdfPath& primPath,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging)
{
    return Handle(new HdPageableVectorDataSource(primPath, pageFileManager, memoryMonitor,
        destructionCallback, usage, enableImplicitPaging));
}

HdPageableVectorDataSource::HdPageableVectorDataSource(const SdfPath& primPath,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging) :
    HdVectorDataSource(),
    HdPageableBufferBase<>(primPath, 0, usage, pageFileManager, memoryMonitor, destructionCallback),
    mSerializer(std::make_shared<HdDefaultValueSerializer>()),
    mEnableImplicitPaging(enableImplicitPaging)
{
}

size_t HdPageableVectorDataSource::GetNumElements()
{
    std::shared_lock<std::shared_mutex> readLock(mElementsMutex);
    return mElements.size();
}

HdDataSourceBaseHandle HdPageableVectorDataSource::GetElement(size_t element)
{
    return HdPageableDataSourceUtils::VectorGetElement(
        element, mElements, mElementPageEntries, mElementsMutex, mEnableImplicitPaging,
        HasValidDiskBuffer(), mPageEntry, mPageFileManager, *mSerializer,
        mAccessCount, mPageInCount);
}

std::vector<HdContainerPageEntry> HdPageableVectorDataSource::GetMemoryBreakdown() const
{
    std::shared_lock<std::shared_mutex> readLock(mElementsMutex);
    return mElementPageEntries;
}

bool HdPageableVectorDataSource::IsElementResident(size_t index) const
{
    std::shared_lock<std::shared_mutex> readLock(mElementsMutex);
    return index < mElements.size() && mElements[index]->IsDataResident();
}

bool HdPageableVectorDataSource::PageInElement(size_t index)
{
    return HdPageableDataSourceUtils::VectorPageIn(index, mElements, mElementPageEntries,
        mElementsMutex, HasValidDiskBuffer(), mPageEntry, mPageFileManager, *mSerializer,
        mPageInCount);
}

bool HdPageableVectorDataSource::PageOutElement(size_t index)
{
    return HdPageableDataSourceUtils::VectorPageOut(index, mElements, mElementsMutex,
        HasValidDiskBuffer(), mPageEntry, mPageFileManager, mBufferState, *mSerializer,
        mPageOutCount);
}

bool HdPageableVectorDataSource::SwapSceneToDisk(bool force, HdBufferState /*releaseBuffer*/)
{
    return HdPageableDataSourceUtils::VectorSwapToDisk(
        mElements, force, mElementsMutex, mPageEntry, mPageFileManager, mBufferState, *mSerializer,
        mPageOutCount);
}

bool HdPageableVectorDataSource::SwapToSceneMemory(bool /*force*/, HdBufferState /*releaseBuffer*/)
{
    return HdPageableDataSourceUtils::VectorSwapToMemory(
        mElements, mElementPageEntries, mElementsMutex, HasValidDiskBuffer(), mPageEntry,
        mPageFileManager, mBufferState, *mSerializer, mPageInCount);
}

// HdPageableSampledDataSource Implementation /////////////////////////////////
//
// Each time sample owns an independent HdPageableValue with its own disk page.
// Unlike container/vector sources, samples are NOT packed together — each
// sample is individually pageable because time-sampled access patterns are
// typically sparse (only a few shutter offsets accessed per frame).

HdPageableSampledDataSource::Handle HdPageableSampledDataSource::New(const VtValue& value,
    const SdfPath& primPath, const TfToken& attributeName,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging)
{
    return Handle(new HdPageableSampledDataSource(value, primPath, attributeName, pageFileManager,
        memoryMonitor, destructionCallback, usage, enableImplicitPaging));
}

HdPageableSampledDataSource::Handle HdPageableSampledDataSource::New(
    const std::map<Time, VtValue>& samples, const SdfPath& primPath, const TfToken& attributeName,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging)
{
    return Handle(new HdPageableSampledDataSource(samples, primPath, attributeName, pageFileManager,
        memoryMonitor, destructionCallback, usage, enableImplicitPaging));
}

HdPageableSampledDataSource::HdPageableSampledDataSource(const VtValue& value,
    const SdfPath& primPath, const TfToken& attributeName,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging) :
    HdPageableBufferBase<>(primPath, HdPageableValue::EstimateMemoryUsage(value), usage,
        pageFileManager, memoryMonitor, destructionCallback),
    mPrimPath(primPath),
    mAttributeName(attributeName),
    mEnableImplicitPaging(enableImplicitPaging)
{
    auto buffer = std::make_shared<HdPageableValue>(SdfPath(GetBufferKey(0.0)), Size(), usage,
        pageFileManager, memoryMonitor, HdPageableDataSourceUtils::kNoOpDestructionCallback, value,
        attributeName, mEnableImplicitPaging);

    mSamples.push_back({ 0.0, std::move(buffer) });
}

HdPageableSampledDataSource::HdPageableSampledDataSource(const std::map<Time, VtValue>& samples,
    const SdfPath& primPath, const TfToken& attributeName,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage, bool enableImplicitPaging) :
    HdPageableBufferBase<>(primPath, 0, usage, pageFileManager, memoryMonitor, destructionCallback),
    mPrimPath(primPath),
    mAttributeName(attributeName),
    mEnableImplicitPaging(enableImplicitPaging)
{
    for (const auto& [time, value] : samples)
    {
        size_t estimatedSize = HdPageableValue::EstimateMemoryUsage(value);
        auto buffer =
            std::make_shared<HdPageableValue>(SdfPath(GetBufferKey(time)), estimatedSize, usage,
                pageFileManager, memoryMonitor, HdPageableDataSourceUtils::kNoOpDestructionCallback,
                value, attributeName, mEnableImplicitPaging);

        mSamples.push_back({ time, std::move(buffer) });
    }

    std::sort(mSamples.begin(), mSamples.end(),
        [](const MemorySample& a, const MemorySample& b) { return a.time < b.time; });
}

VtValue HdPageableSampledDataSource::GetSampleValue(const MemorySample& sample) const
{
    return HdPageableDataSourceUtils::GetSampleValue(sample, mEnableImplicitPaging, mPageInCount);
}

VtValue HdPageableSampledDataSource::GetValue(Time shutterOffset)
{
    return HdPageableDataSourceUtils::SampledGetValue(
        shutterOffset, mSamples, mSamplesMutex, mEnableImplicitPaging, mInterpolationMode,
        mAccessCount, mPageInCount);
}

VtValue HdPageableSampledDataSource::GetValueIfResident(Time shutterOffset) const
{
    std::shared_lock<std::shared_mutex> readLock(mSamplesMutex);
    const auto* sample = HdPageableDataSourceUtils::SampledFindSample(shutterOffset, mSamples);
    if (sample && sample->buffer->IsDataResident())
        return sample->buffer->GetValueIfResident();
    return {};
}

bool HdPageableSampledDataSource::GetContributingSampleTimesForInterval(
    Time startTime, Time endTime, std::vector<Time>* outSampleTimes)
{
    return HdPageableDataSourceUtils::SampledGetContributingTimes(
        startTime, endTime, outSampleTimes, mSamples, mSamplesMutex);
}

bool HdPageableSampledDataSource::IsSampleResident(Time time) const
{
    std::shared_lock<std::shared_mutex> readLock(mSamplesMutex);
    const auto* sample = HdPageableDataSourceUtils::SampledFindSample(time, mSamples);
    return sample && sample->buffer->IsDataResident();
}

std::vector<HdPageableSampledDataSource::Time> HdPageableSampledDataSource::GetAllSampleTimes()
    const
{
    return HdPageableDataSourceUtils::SampledGetAllTimes(mSamples, mSamplesMutex);
}

std::string HdPageableSampledDataSource::GetBufferKey(Time time) const
{
    return HdPageableDataSourceUtils::SampledGetBufferKey(time, mPrimPath, mAttributeName);
}

const HdPageableSampledDataSource::MemorySample* HdPageableSampledDataSource::FindSample(
    Time time) const
{
    return HdPageableDataSourceUtils::SampledFindSample(time, mSamples);
}

VtValue HdPageableSampledDataSource::InterpolateValues(
    const VtValue& v1, const VtValue& v2, float t)
{
    if (v1.IsHolding<VtFloatArray>() && v2.IsHolding<VtFloatArray>())
    {
        const auto& a1 = v1.UncheckedGet<VtFloatArray>();
        const auto& a2 = v2.UncheckedGet<VtFloatArray>();
        if (a1.size() == a2.size())
        {
            VtFloatArray result(a1.size());
            for (size_t i = 0; i < a1.size(); ++i)
            {
                result[i] = a1[i] * (1.0f - t) + a2[i] * t;
            }
            return VtValue(result);
        }
    }

    if (v1.IsHolding<VtVec3fArray>() && v2.IsHolding<VtVec3fArray>())
    {
        const auto& a1 = v1.UncheckedGet<VtVec3fArray>();
        const auto& a2 = v2.UncheckedGet<VtVec3fArray>();
        if (a1.size() == a2.size())
        {
            VtVec3fArray result(a1.size());
            for (size_t i = 0; i < a1.size(); ++i)
            {
                result[i] = a1[i] * (1.0f - t) + a2[i] * t;
            }
            return VtValue(result);
        }
    }

    // TODO: Add support for other types
    TF_WARN("HdPageableSampledDataSource::InterpolateValues: Unsupported interpolation types v1=%s, v2=%s",
        v1.GetTypeName().c_str(), v2.GetTypeName().c_str());

    // Unsupported interpolation — return closest value
    return t < 0.5f ? v1 : v2;
}

// HdPageableBlockDataSource Implementation ///////////////////////////////////

HdPageableBlockDataSource::Handle HdPageableBlockDataSource::New(const SdfPath& primPath,
    const std::unique_ptr<HdPageFileManager>& pageFileManager,
    const std::unique_ptr<HdMemoryMonitor>& memoryMonitor, DestructionCallback destructionCallback,
    HdBufferUsage usage)
{
    return Handle(new HdPageableBlockDataSource(
        primPath, pageFileManager, memoryMonitor, destructionCallback, usage));
}

// HdPageableDataSourceManager Implementation /////////////////////////////////

HdPageableDataSourceManager::HdPageableDataSourceManager() : HdPageableDataSourceManager(Config {})
{
}

HdPageableDataSourceManager::HdPageableDataSourceManager(const Config& config)
{
    DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = config.pageFileDirectory;
    desc.sceneMemoryLimit    = config.sceneMemoryLimit;
    desc.rendererMemoryLimit = config.rendererMemoryLimit;
    desc.ageLimit            = config.ageLimit;
    desc.numThreads          = config.numThreads;

    mBufferManager            = std::make_unique<DefaultBufferManager>(desc);
    mFreeCrawlPercentage      = config.freeCrawlPercentage;
    mFreeCrawlInterval        = config.freeCrawlIntervalMs;
    mBackgroundCleanupEnabled = config.enableBackgroundCleanup;

    InitializeDefaults();

    if (mBackgroundCleanupEnabled)
    {
        mCleanupThread = std::thread(&HdPageableDataSourceManager::BackgroundCleanupLoop, this);
    }
}

HdPageableDataSourceManager::HdPageableDataSourceManager(
    std::filesystem::path pageFileDirectory, size_t sceneMemoryLimit, size_t rendererMemoryLimit)
{
    DefaultBufferManager::InitializeDesc desc;
    desc.pageFileDirectory   = pageFileDirectory;
    desc.sceneMemoryLimit    = sceneMemoryLimit;
    desc.rendererMemoryLimit = rendererMemoryLimit;
    desc.ageLimit            = 20;
    desc.numThreads          = 2;

    mBufferManager = std::make_unique<DefaultBufferManager>(desc);

    InitializeDefaults();

    mCleanupThread = std::thread(&HdPageableDataSourceManager::BackgroundCleanupLoop, this);
}

void HdPageableDataSourceManager::InitializeDefaults()
{
    mSerializer = std::make_shared<HdDefaultValueSerializer>();
}

HdPageableDataSourceManager::~HdPageableDataSourceManager()
{
    mBackgroundCleanupEnabled = false;
    if (mCleanupThread.joinable())
    {
        mCleanupThread.join();
    }
}

std::shared_ptr<HdPageableBufferCore> HdPageableDataSourceManager::GetOrCreateBuffer(
    const SdfPath& primPath, const VtValue& data, const TfToken& dataType)
{
    auto existingBuffer = mBufferManager->FindBuffer(primPath);
    if (existingBuffer)
    {
        return existingBuffer;
    }

    size_t estimatedSize  = HdPageableValue::EstimateMemoryUsage(data);
    auto& pageFileManager = GetPageFileManager();
    auto& memoryMonitor   = GetMemoryMonitor();

    auto destructionCallback = [this](const SdfPath& path)
    {
        if (mBufferManager)
            mBufferManager->RemoveBuffer(path);
    };

    auto buffer = std::make_shared<HdPageableValue>(primPath, estimatedSize, HdBufferUsage::Static,
        pageFileManager, memoryMonitor, destructionCallback, data, dataType, true,
        mSerializer.get());

    mBufferManager->AddBuffer(primPath, buffer);

    return buffer;
}

void HdPageableDataSourceManager::SetSerializer(std::shared_ptr<IHdValueSerializer> serializer)
{
    mSerializer = std::move(serializer);
}

size_t HdPageableDataSourceManager::GetResidentBufferCount() const
{
    return mBufferManager->GetResidentBufferCount();
}

size_t HdPageableDataSourceManager::GetPagedOutBufferCount() const
{
    return mBufferManager->GetPagedOutBufferCount();
}

size_t HdPageableDataSourceManager::GetTotalMemoryUsage() const
{
    auto& monitor = mBufferManager->GetMemoryMonitor();
    return monitor->GetUsedSceneMemory() + monitor->GetUsedRendererMemory();
}

float HdPageableDataSourceManager::GetMemoryPressure() const
{
    auto& monitor = mBufferManager->GetMemoryMonitor();
    return std::max(monitor->GetSceneMemoryPressure(), monitor->GetRendererMemoryPressure());
}

void HdPageableDataSourceManager::BackgroundCleanupLoop()
{
    while (mBackgroundCleanupEnabled)
    {
        // Sleep with periodic checks for stop request
        for (int i = 0; i < 10 && mBackgroundCleanupEnabled; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(mFreeCrawlInterval.load()));
        }

        if (!mBackgroundCleanupEnabled)
        {
            break;
        }

        auto& monitor          = mBufferManager->GetMemoryMonitor();
        float scenePressure    = monitor->GetSceneMemoryPressure();
        float rendererPressure = monitor->GetRendererMemoryPressure();

        if (scenePressure > HdMemoryMonitor::LOW_MEMORY_THRESHOLD ||
            rendererPressure > HdMemoryMonitor::LOW_MEMORY_THRESHOLD)
        {
            mBufferManager->FreeCrawl(mFreeCrawlPercentage);
        }
    }
}

// Misc Utility Functions /////////////////////////////////////////////////////

namespace HdPageableDataSourceUtils
{

HdDataSourceBaseHandle CreateFromValue(const VtValue& value, const SdfPath& primPath,
    const TfToken& name, const std::shared_ptr<HdPageableDataSourceManager>& memoryManager)
{
    if (!memoryManager)
    {
        return HdRetainedSampledDataSource::New(value);
    }

    return HdPageableSampledDataSource::New(value, primPath, name,
        memoryManager->GetPageFileManager(), memoryManager->GetMemoryMonitor(),
        HdPageableDataSourceUtils::kNoOpDestructionCallback);
}

HdContainerDataSourceHandle CreateContainer(const std::map<TfToken, VtValue>& values,
    const SdfPath& primPath, const std::shared_ptr<HdPageableDataSourceManager>& memoryManager)
{
    if (!memoryManager)
    {
        std::vector<TfToken> names;
        std::vector<HdDataSourceBaseHandle> sources;
        for (const auto& [token, value] : values)
        {
            names.push_back(token);
            sources.push_back(HdRetainedSampledDataSource::New(value));
        }
        return HdRetainedContainerDataSource::New(
            static_cast<int>(names.size()), names.data(), sources.data());
    }

    return HdPageableContainerDataSource::New(values, primPath, memoryManager->GetPageFileManager(),
        memoryManager->GetMemoryMonitor(), HdPageableDataSourceUtils::kNoOpDestructionCallback);
}

HdVectorDataSourceHandle CreateVector(const std::vector<VtValue>& values, const SdfPath& primPath,
    const std::shared_ptr<HdPageableDataSourceManager>& memoryManager)
{
    if (!memoryManager)
    {
        std::vector<HdDataSourceBaseHandle> sources;
        for (const auto& value : values)
        {
            sources.push_back(HdRetainedSampledDataSource::New(value));
        }
        return HdRetainedSmallVectorDataSource::New(
            static_cast<int>(sources.size()), sources.data());
    }

    return HdPageableVectorDataSource::New(values, primPath, memoryManager->GetPageFileManager(),
        memoryManager->GetMemoryMonitor(), HdPageableDataSourceUtils::kNoOpDestructionCallback);
}

HdSampledDataSourceHandle CreateTimeSampled(
    const std::map<HdSampledDataSource::Time, VtValue>& samples, const SdfPath& primPath,
    const TfToken& name, const std::shared_ptr<HdPageableDataSourceManager>& memoryManager)
{
    if (!memoryManager)
    {
        if (!samples.empty())
        {
            return HdRetainedSampledDataSource::New(samples.begin()->second);
        }
        return HdRetainedSampledDataSource::New(VtValue());
    }

    return HdPageableSampledDataSource::New(samples, primPath, name,
        memoryManager->GetPageFileManager(), memoryManager->GetMemoryMonitor(),
        HdPageableDataSourceUtils::kNoOpDestructionCallback);
}

HdBlockDataSourceHandle CreateBlock(const VtValue& /*value*/, const SdfPath& primPath,
    const std::shared_ptr<HdPageableDataSourceManager>& memoryManager)
{
    if (!memoryManager)
    {
        return HdBlockDataSource::New();
    }

    return HdPageableBlockDataSource::New(primPath, memoryManager->GetPageFileManager(),
        memoryManager->GetMemoryMonitor(), HdPageableDataSourceUtils::kNoOpDestructionCallback,
        HdBufferUsage::Static);
}

} // namespace HdPageableDataSourceUtils

} // namespace HVT_NS
