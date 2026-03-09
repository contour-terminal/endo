// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <CoreVM/types/TypedObject.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace CoreVM
{

/// Slab-based object pool with O(1) allocate, deallocate, and pointer validation.
///
/// Objects are allocated from size-class slabs. Each size class maintains its own
/// chain of slabs and an intrusive free list for O(1) reuse of freed cells.
///
/// THREAD-SAFETY: This pool is designed for per-Runner (per-thread) use.
/// No internal locking is performed. Each thread should own its own ObjectPool.
class ObjectPool
{
  public:
    /// @param slabCapacity Minimum slab size in bytes (rounded up to fit whole cells).
    explicit ObjectPool(size_t slabCapacity = 4096);
    ~ObjectPool() = default;

    ObjectPool(ObjectPool const&) = delete;
    ObjectPool& operator=(ObjectPool const&) = delete;
    ObjectPool(ObjectPool&&) = default;
    ObjectPool& operator=(ObjectPool&&) = default;

    /// O(1) allocate: pop from free list or bump-allocate in current slab.
    /// The returned object has refCount=1, tag=0, and zero-initialized slots.
    /// @param type Type descriptor for the object (must not be null).
    [[nodiscard]] TypedObject* allocate(TypeDescriptor const* type);

    /// O(1) deallocate: mark cell as dead and push onto free list.
    /// @param obj Object previously allocated from this pool (must not be null).
    void deallocate(TypedObject* obj) noexcept;

    /// O(1) pointer validation via slab address-range checks.
    /// Returns true only if ptr points to a live (refCount > 0) object
    /// at a valid cell boundary within one of this pool's slabs.
    [[nodiscard]] bool owns(void const* ptr) const noexcept;

    /// Returns the number of currently live (allocated and not deallocated) objects.
    [[nodiscard]] size_t liveCount() const noexcept { return _liveCount; }

    /// Returns the cumulative number of allocations (monotonically increasing).
    [[nodiscard]] size_t totalAllocations() const noexcept { return _totalAllocations; }

    /// Iterate all live objects across all size classes (for GC sweep).
    /// Skips freed cells (refCount == 0) and uninitialized bump space.
    void forEachLiveObject(std::function<void(TypedObject&)> const& visitor);

  private:
    /// Size classes in bytes, covering objects from 0 slots (16 bytes) to ~30 slots (256 bytes).
    static constexpr size_t kSizeClasses[] = { 16, 24, 32, 48, 64, 96, 128, 256 };
    static constexpr size_t kNumSizeClasses = sizeof(kSizeClasses) / sizeof(kSizeClasses[0]);

    /// A contiguous memory region divided into fixed-size cells.
    struct Slab
    {
        std::unique_ptr<uint8_t[]> memory; ///< Owned memory block (value-initialized to zero).
        size_t size;                       ///< Total bytes in this slab.
    };

    /// Per-size-class allocation state.
    struct SizeClass
    {
        size_t cellSize = 0;        ///< Cell size in bytes for this class.
        void* freeList = nullptr;   ///< Head of intrusive free list (freed cells).
        uint8_t* bumpPtr = nullptr; ///< Next cell to bump-allocate in current slab.
        uint8_t* bumpEnd = nullptr; ///< End of current slab.
        std::vector<Slab> slabs;    ///< All slabs for this size class.
    };

    /// Maps an allocation size to its size class index.
    [[nodiscard]] static size_t sizeClassIndex(size_t allocSize) noexcept;

    /// Adds a new slab to the size class at the given index.
    void addSlab(size_t sizeClassIdx);

    SizeClass _sizeClasses[kNumSizeClasses];
    size_t _liveCount = 0;
    size_t _totalAllocations = 0;
    size_t _slabCapacity;
};

} // namespace CoreVM
