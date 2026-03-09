// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/util/assert.hpp>
#include <CoreVM/vm/ObjectPool.hpp>

#include <algorithm>
#include <cstring>

namespace CoreVM
{

ObjectPool::ObjectPool(size_t slabCapacity): _slabCapacity(slabCapacity)
{
    for (size_t i = 0; i < kNumSizeClasses; ++i)
        _sizeClasses[i].cellSize = kSizeClasses[i];
}

size_t ObjectPool::sizeClassIndex(size_t allocSize) noexcept
{
    for (size_t i = 0; i < kNumSizeClasses; ++i)
    {
        if (allocSize <= kSizeClasses[i])
            return i;
    }
    // Should never happen for supported object sizes.
    COREVM_ASSERT(false, "allocation size exceeds maximum size class (256 bytes)");
    return kNumSizeClasses - 1;
}

void ObjectPool::addSlab(size_t sizeClassIdx)
{
    auto& sc = _sizeClasses[sizeClassIdx];
    auto slabSize = std::max(_slabCapacity, sc.cellSize);
    // Round down to a whole number of cells.
    slabSize = (slabSize / sc.cellSize) * sc.cellSize;

    // make_unique<uint8_t[]>(n) value-initializes (zeroes all bytes).
    // This ensures unallocated cells have refCount == 0.
    auto memory = std::make_unique<uint8_t[]>(slabSize);
    sc.bumpPtr = memory.get();
    sc.bumpEnd = memory.get() + slabSize;
    sc.slabs.push_back({ std::move(memory), slabSize });
}

TypedObject* ObjectPool::allocate(TypeDescriptor const* type)
{
    COREVM_ASSERT(type != nullptr, "Cannot allocate object with null type descriptor");

    auto const allocSize = TypedObject::allocationSize(type);
    auto const idx = sizeClassIndex(allocSize);
    auto& sc = _sizeClasses[idx];

    uint8_t* cell = nullptr;

    // Try free list first (O(1) reuse of freed cells).
    if (sc.freeList)
    {
        cell = static_cast<uint8_t*>(sc.freeList);
        sc.freeList = *reinterpret_cast<void**>(cell);
    }
    else
    {
        // Bump-allocate from current slab.
        if (!sc.bumpPtr || sc.bumpPtr >= sc.bumpEnd)
            addSlab(idx);

        cell = sc.bumpPtr;
        sc.bumpPtr += sc.cellSize;
    }

    // Initialize the TypedObject header and slots.
    auto* obj = reinterpret_cast<TypedObject*>(cell);
    obj->type = type;
    obj->refCount.store(1, std::memory_order_relaxed);
    obj->tag = 0;
    std::memset(obj->padding, 0, sizeof(obj->padding));

    // Zero-initialize all payload slots.
    auto* slots = obj->slots();
    for (uint16_t i = 0; i < type->slotCount; ++i)
        slots[i] = 0;

    ++_liveCount;
    ++_totalAllocations;

    return obj;
}

void ObjectPool::deallocate(TypedObject* obj) noexcept
{
    COREVM_ASSERT(obj != nullptr, "Cannot deallocate null object");

    // Compute size class BEFORE overwriting the type pointer with the free-list link.
    auto const allocSize = TypedObject::allocationSize(obj->type);
    auto const idx = sizeClassIndex(allocSize);
    auto& sc = _sizeClasses[idx];

    // Mark the cell as dead (refCount = 0 is the liveness sentinel).
    obj->refCount.store(0, std::memory_order_relaxed);

    // Push onto intrusive free list: store next-free pointer in the type field position.
    *reinterpret_cast<void**>(obj) = sc.freeList;
    sc.freeList = obj;

    --_liveCount;
}

bool ObjectPool::owns(void const* ptr) const noexcept
{
    if (!ptr)
        return false;

    auto const p = reinterpret_cast<uintptr_t>(ptr);

    for (auto const& sc: _sizeClasses)
    {
        for (auto const& slab: sc.slabs)
        {
            auto const start = reinterpret_cast<uintptr_t>(slab.memory.get());
            auto const end = start + slab.size;
            if (p >= start && p < end)
            {
                // Verify the pointer is at a valid cell boundary.
                if ((p - start) % sc.cellSize != 0)
                    return false;

                // Verify the cell is live (refCount > 0).
                auto const* obj = reinterpret_cast<TypedObject const*>(ptr);
                return obj->refCount.load(std::memory_order_relaxed) > 0;
            }
        }
    }
    return false;
}

void ObjectPool::forEachLiveObject(std::function<void(TypedObject&)> const& visitor)
{
    for (auto const& sc: _sizeClasses)
    {
        for (auto const& slab: sc.slabs)
        {
            auto* start = slab.memory.get();
            auto* end = start + slab.size;
            for (auto* cell = start; cell < end; cell += sc.cellSize)
            {
                auto* obj = reinterpret_cast<TypedObject*>(cell);
                if (obj->refCount.load(std::memory_order_relaxed) > 0)
                    visitor(*obj);
            }
        }
    }
}

} // namespace CoreVM
