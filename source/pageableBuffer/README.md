# Memory Paging System

The memory paging system is an extension to current OpenUSD data sources. The primary purpose of introducing memory paging system is to **reduce the system memory usage without obvious performance regression**. To simplify things at this stage, the GPU memory management can be ignored for now and, the paging system can be triggered on-demand. Meanwhile, it should be general and extensible.

Because the paging system needs to adapt to different scenarios:
- On-demand paging (e.g. consolidation).
- Automatic tracking and paging (e.g. in applications).

It does not provide a closed-form solution, but rather **building blocks**:
- **Pageable DataSources**
- **Page Buffer Manager** (encapsulating Page File Manager)
- **Paging Strategies**

Note that the paging system is expected to be applicable in different workflows with different configs within one application, it avoids anything global (e.g. a singleton buffer manager).

## Features Support

- **Three-tier memory hierarchy**: Scene Memory, Renderer Memory, Disk Storage
- **Adaptive paging**: On-demand / Automatic paging based on configurable strategies (e.g. memory pressures, age, etc.)
- **Asynchronous operations**: Background memory processing

## Architecture

### Key Concepts

1. **Buffer States**: UnKnown, System (Scene), Hardware (Renderer), Disk
2. **Buffer Usage**: Static, Dynamic
3. **Core Operations**: Buffer Creation, Data Copy, Buffer Disposal
4. **Paging Operations**:
   1. Page: Create new buffer and fill data. Keep the source buffer (e.g. `PageToDisk`).
   2. Swap: Create new buffer and fill data. Release the source buffer (e.g. `SwapSceneToDisk`).
5. **Free Crawling**: Periodic cleanup of resources

### Usages

Ideally, users should use PageBufferManager as the entry point.

Usage1: Paging On-demand (asynchronously):
```cpp
// Create a Buffer Manager when initializing
HdPageableBufferManager<..., ...> bufferManager;
 
// Create a buffer
constexpr size_t MB = 1024 * 1024;
auto buffer1 = bufferManager.CreateBuffer("AsyncBuffer1", 50 * MB);
 
// Start some async operations
auto swapFuture1 = bufferManager.SwapSystemToDiskAsync(buffer1);
 
// Wait for operations (if needed)
swapFuture1.wait();
// or, wait for all operations to complete
// bufferManager.WaitForAllOperations();
```

Usage2: Moniter & Automatic Freecrawl (recommend in background thread):
```cpp
// Create a Buffer Manager when initializing
HdPageableBufferManager<..., ...> bufferManager;
 
// Create some buffers with different characteristics using factory
constexpr size_t MB = 1024 * 1024;
auto buffer1 = bufferManager.CreateBuffer("SmallBuffer", 20 * MB, BufferUsage::Static);
auto buffer2 = bufferManager.CreateBuffer("MediumBuffer", 50 * MB, BufferUsage::Static);
auto buffer3 = bufferManager.CreateBuffer("LargeBuffer", 100 * MB, BufferUsage::Static);
 
// ...advance frames when doing the rendering
cacheManager.AdvanceFrame();
 
// In a background thread, at a certain time interval
// check 50% of buffers to see if they get chance to page out 
cacheManager.FreeCrawl(50.0f);
```

Note that for disposable intermediate data, users should directly control by themselves or using `std::move`. 

### Detailed Designs

#### Buffer

The following diagram indicates the state transition:

```mermaid
stateDiagram-v2
    state if_state <<choice>>

    [*] --> if_state: Initial State (Unknown)
    
    if_state --> SystemBuffer: CreateSystemBuffer
    if_state --> HardwareBuffer: CreateDeviceBuffer
    if_state --> DiskBuffer: CreatePageFile
    
    SystemBuffer --> HardwareBuffer: SwapToHardwareMemory
    SystemBuffer --> DiskBuffer: SwapSystemToDisk
    
    HardwareBuffer --> SystemBuffer: SwapToSystemMemory
    HardwareBuffer --> DiskBuffer: SwapHardwareToDisk
    
    DiskBuffer --> SystemBuffer: PageToSysMemory
    DiskBuffer --> HardwareBuffer: PageToHWMemory
    
    state HardwareBuffer {
        [*] --> HardwareMappedBuffer: Map
        HardwareMappedBuffer --> [*]: Unmap
        [*] --> EvictHardwareBuffer: Evict
        EvictHardwareBuffer --> [*]: MakeResident
    }

    note right of HardwareBuffer : Buffer in VRAM (in Hydra, it means buffer has been transferred to Renderer)
    note right of HardwareMappedBuffer : Buffer is mapped for GPU access via Graphics API (not used)
    note right of EvictHardwareBuffer : Buffer is evicted for saving VRAM via Graphics API (not used)
```

The ER diagram shows the key classes and their relationships in the memory paging system:

```mermaid
erDiagram
    Buffer {
        string mName
        size_t mSize
        BufferUsage mUsage
        BufferState mBufferState
        unique_ptr_byte_array mSystemBuffer
        unique_ptr_byte_array mHardwareBuffer
        unique_ptr_PageHandle mPageHandle
        void_ptr mMappedData
        atomic_int mLockCount
        int mFrameStamp
        shared_mutex mMapMutex
    }
    
    CacheManager {
        vector_shared_ptr_Buffer mBuffers
        mutex mBuffersMutex
        atomic_int mCurrentFrame
        int mAgeLimit
        thread mBackgroundThread
        atomic_bool mStopBackgroundCrawl
    }
    
    MemoryMonitor {
        atomic_size_t mUsedSystemMemory
        atomic_size_t mUsedHardwareMemory
        size_t mSystemMemoryLimit
        size_t mHardwareMemoryLimit
        float LOW_MEMORY_THRESHOLD
        float GPU_PAGING_THRESHOLD
        float CPU_PAGING_THRESHOLD
    }
    
    PageManager {
        vector_unique_ptr_PageFileEntry mPageFileEntries
        mutex mSyncMutex
        size_t MAX_PAGE_FILE_SIZE
    }
    
    PageHandle {
        size_t mPageId
        size_t mSize
        ptrdiff_t mOffset
    }
    
    PageFileEntry {
        string mFileName
        size_t mPageId
        size_t mSizeLimit
        ptrdiff_t mNextOffset
        vector_FreeListEntry mFreeList
        bool mFreeListConsolidated
        mutex mFileMutex
    }
    
    FreeListEntry {
        ptrdiff_t offset
        size_t size
    }
    
    Buffer ||--o| PageHandle : "has"
    Buffer }o--|| PageManager : "uses for disk operations"
    Buffer }o--|| MemoryMonitor : "tracks memory usage"
    
    CacheManager ||--o{ Buffer : "manages collection of"
    CacheManager }o--|| MemoryMonitor : "monitors pressure"
    
    PageManager ||--o{ PageFileEntry : "contains"
    PageManager ||--o{ PageHandle : "creates"
    
    PageFileEntry ||--o{ FreeListEntry : "contains free list"
```

Core Classes:

1. **Buffer** - The central entity that manages memory buffers across different storage locations (scene/RAM, renderer/VRAM, disk storage)
2. **CacheManager** - Manages the lifecycle and aging of multiple buffers with background processing
3. **MemoryMonitor** - Tracks memory usage and calculates memory pressure for both system and hardware memory
4. **PageManager** - Handles disk paging operations and file management

Supporting Classes:

1. **PageHandle** - Value object containing page metadata (ID, size, offset) for disk operations
2. **PageFileEntry** - Manages individual page files on disk with free space tracking
3. **FreeListEntry** - Simple data structure for tracking available free space in page files

Design Patterns:
- **RAII**: Buffer uses RAII for automatic memory management
- **Three-tier Architecture**: System RAM → Hardware/GPU Memory → Disk storage hierarchy
- **Free List Management**: Efficient disk space reuse through gap tracking

#### Paging Control

Abstract the strategy and decouple it from buffer management. Make the 1) paging, and 2) buffer selection strategies configurable. For safety and simplicity, they should be determined at the compiling time:

Configurable Strategies
```cpp
// Concept for paging strategies
// Use traits alternatively if C++20 is not supported
template<typename T>
concept PagingStrategyLike = requires(T t, const OGSDemo::Buffer& buffer, const OGSDemo::PagingContext& context) {
    { t(buffer, context) } -> std::convertible_to<OGSDemo::PagingDecision>;
} || requires(T t, const OGSDemo::Buffer& buffer, const OGSDemo::PagingContext& context) {
    { t.operator()(buffer, context) } -> std::convertible_to<OGSDemo::PagingDecision>;
};
 
// Concept for buffer selection strategies
// Use traits alternatively if C++20 is not supported
template<typename T>
concept BufferSelectionStrategyLike = requires(T t, const std::vector<std::shared_ptr<OGSDemo::Buffer>>& buffers, const OGSDemo::SelectionContext& context) {
    { t(buffers, context) } -> std::convertible_to<std::vector<std::shared_ptr<OGSDemo::Buffer>>>;
} || requires(T t, const std::vector<std::shared_ptr<OGSDemo::Buffer>>& buffers, const OGSDemo::SelectionContext& context) {
    { t.operator()(buffers, context) } -> std::convertible_to<std::vector<std::shared_ptr<OGSDemo::Buffer>>>;
};
 
// Encapsulate in CacheManager
template<HdPagingConcepts::PagingStrategyLike PagingStrategyType, 
         HdPagingConcepts::BufferSelectionStrategyLike BufferSelectionStrategyType>
class HdPageableBufferManager {
// ......
private:
    // Compile-time strategy instances (no runtime changing)
    PagingStrategyType mPagingStrategy{};
    BufferSelectionStrategyType mBufferSelectionStrategy{};
};
```

Simplified implementation details:
1. Use selection strategy to pick buffer candidates
    ```cpp
    std::vector<std::shared_ptr<HdPageableBufferBase>> selectedBuffers = mBufferSelectionStrategy(mBuffers, selectionContext);
    ```
2. For each buffer, execute paging according to paging configs
    ```cpp
    PagingDecision decision = mPagingStrategy(buffer, context);
    bool isDisposed = ExecutePagingDecision(buffer, decision);
    ```

#### Thread Mode

We propose two usages:
1. Use a background thread to perform the memory freecrawl in some certain time interval:
```mermaid
graph LR
    subgraph I["Main Thread"]
        A["Create Background Thread"] --> B["Create Buffers"]
        B --> G["Release Buffers"]
        G --> C["Stop & Join<br/>Background Thread"]
        F["CacheManager<br/>MemoryMonitor"]
    end
    
    subgraph H["Background Thread"]
        D["Monitor Memory<br/>Every 1000ms"] --> E["Auto Cleanup<br/>when needed"]
        E --> D
    end
    
    A -.-> H
    B -.-> F
    G -.-> F
    E -.-> F
    C -.-> H
    
    classDef main fill:#fff3e0
    class A,B,C,G main

    style I fill:#e1f5fe
    style H fill:#f3e5f5
    style F fill:#e8f5e8
```

2. Use a MemoryPool to finish the buffer operations asynchronously:
```mermaid
graph TD
    subgraph Caller ["Caller"]
    end
    subgraph CacheManager ["CacheManager"]
        AsyncOps["Async Operations<br/>PageToSystemMemoryAsync()<br/>SwapSystemToDiskAsync()<br/>etc."]
        ReturnObj["std::future"]
    end
    
    subgraph ThreadPool ["ThreadPool"]
        Queue["Task Queue"]
        Worker1["Worker Thread 1"]
        Worker2["Worker Thread 2"] 
        Worker3["Worker Thread ..."]
    end
    
    subgraph BufferOps ["Buffer Operations"]
        BufferWork["Actual Memory Operations<br/>Page/Swap/etc."]
    end
    
    AsyncOps -->|"Submit Tasks"| Queue
    Queue -->|"Distribute Work"| Worker1
    Queue -->|"Distribute Work"| Worker2
    Queue -->|"Distribute Work"| Worker3
    
    Worker1 --> BufferWork
    Worker2 --> BufferWork
    Worker3 --> BufferWork
    
    Caller -->|"Async Operations"| AsyncOps
    AsyncOps -.-|"Create"| ReturnObj
    ReturnObj -.->|"Return"| Caller
    BufferWork -.-> ReturnObj
    
    style CacheManager fill:#e1f5fe
    style ThreadPool fill:#f3e5f5
    style BufferOps fill:#e8f5e8

    classDef shared fill:#fff3e0
    class AsyncOps,Caller,ReturnObj shared
```
