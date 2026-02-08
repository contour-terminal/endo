// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "TypeDescriptor.hpp"

namespace CoreVM
{

/// Runtime value type for object slots (same as VM stack value type).
/// Named differently to avoid conflict with IR Value class.
using SlotValue = uint64_t;

/// A heap-allocated object with type information and reference counting.
///
/// TypedObject instances are allocated from an object pool managed by the Runner.
/// They use reference counting for primary memory management, with a backup
/// mark-and-sweep GC to handle cycles in long-running sessions.
///
/// Memory layout:
/// ```
/// +------------------+
/// | TypeDescriptor*  |  8 bytes - pointer to type metadata
/// +------------------+
/// | refCount         |  4 bytes - atomic reference count
/// +------------------+
/// | tag              |  1 byte  - variant tag (for sum types)
/// +------------------+
/// | padding          |  3 bytes - alignment padding
/// +------------------+
/// | slots[0]         |  8 bytes - first payload slot
/// | slots[1]         |  8 bytes - second payload slot (if any)
/// | ...              |
/// +------------------+
/// ```
struct TypedObject
{
    const TypeDescriptor* type;     ///< Type metadata (never null)
    std::atomic<uint32_t> refCount; ///< Reference count (starts at 1)
    uint8_t tag;                    ///< Variant tag for sum types
    uint8_t _padding[3];            ///< Alignment padding

    // Payload slots follow immediately after the header.
    // Access via slots() method.

    /// Returns a pointer to the payload slots.
    [[nodiscard]] SlotValue* slots() noexcept { return reinterpret_cast<SlotValue*>(this + 1); }

    /// Returns a const pointer to the payload slots.
    [[nodiscard]] const SlotValue* slots() const noexcept
    {
        return reinterpret_cast<const SlotValue*>(this + 1);
    }

    /// Gets a slot value by index.
    [[nodiscard]] SlotValue getSlot(uint8_t index) const noexcept { return slots()[index]; }

    /// Sets a slot value by index.
    void setSlot(uint8_t index, SlotValue value) noexcept { slots()[index] = value; }

    /// Returns the total allocation size for an object of the given type.
    [[nodiscard]] static size_t allocationSize(const TypeDescriptor* type) noexcept
    {
        return sizeof(TypedObject) + type->slotCount * sizeof(SlotValue);
    }

    /// Returns the allocation size for this object.
    [[nodiscard]] size_t allocationSize() const noexcept { return allocationSize(type); }

    /// Checks if this is a sum type (Option, Result, user-defined union).
    [[nodiscard]] bool isSumType() const noexcept { return type->kind == TypeKind::Sum; }

    /// Checks if this is a product type (tuple, record).
    [[nodiscard]] bool isProductType() const noexcept { return type->kind == TypeKind::Product; }

    /// Checks if this is a function/closure type.
    [[nodiscard]] bool isFunctionType() const noexcept { return type->kind == TypeKind::Function; }

    /// For sum types: checks if the current tag matches the given variant name.
    [[nodiscard]] bool isVariant(std::string_view variantName) const noexcept
    {
        if (!isSumType())
            return false;
        int expectedTag = type->getVariantTag(variantName);
        return expectedTag >= 0 && static_cast<uint8_t>(expectedTag) == tag;
    }
};

// Ensure the layout is as expected
static_assert(sizeof(TypedObject) == 16, "TypedObject header size mismatch");
static_assert(alignof(TypedObject) == 8, "TypedObject alignment mismatch");

/// Increments the reference count of an object.
/// Safe to call with nullptr.
inline void retainObject(TypedObject* obj) noexcept
{
    if (obj)
    {
        obj->refCount.fetch_add(1, std::memory_order_relaxed);
    }
}

/// Decrements the reference count and returns true if the object should be freed.
/// Safe to call with nullptr (returns false).
/// Note: The caller is responsible for actually freeing the object.
[[nodiscard]] inline bool releaseObject(TypedObject* obj) noexcept
{
    if (!obj)
        return false;

    // Use acq_rel to ensure all writes to the object are visible before destruction
    uint32_t oldCount = obj->refCount.fetch_sub(1, std::memory_order_acq_rel);
    return oldCount == 1; // Was the last reference
}

/// Checks if an object has only one reference (useful for copy-on-write).
[[nodiscard]] inline bool isUniqueRef(const TypedObject* obj) noexcept
{
    if (!obj)
        return false;
    return obj->refCount.load(std::memory_order_acquire) == 1;
}

/// GC marker flag for mark-and-sweep.
/// Stored in a separate data structure (not in the object) to avoid
/// modifying the object during GC.
enum class GCMark : uint8_t
{
    Unmarked = 0,
    Marked = 1,
};

} // namespace CoreVM
