// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>
#include <CoreVM/vm/ObjectPool.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <ranges>
#include <vector>

using namespace CoreVM;

namespace
{

// Helper type descriptors for tests.
TypeDescriptor makeProductType(uint16_t slotCount)
{
    TypeDescriptor desc;
    desc.kind = TypeKind::Product;
    desc.id = 100;
    desc.name = "Test";
    desc.slotCount = slotCount;
    return desc;
}

TypeDescriptor makeSumType(uint8_t slotCount)
{
    TypeDescriptor desc;
    desc.kind = TypeKind::Sum;
    desc.id = 101;
    desc.name = "TestSum";
    desc.slotCount = slotCount;
    desc.variants = { { "A", 0 }, { "B", slotCount } };
    return desc;
}

} // namespace

// =============================================================================
// Allocation & Deallocation
// =============================================================================

TEST_CASE("ObjectPool.allocate_single")
{
    auto desc = makeProductType(2);
    ObjectPool pool;

    auto* obj = pool.allocate(&desc);
    REQUIRE(obj != nullptr);
    CHECK(pool.liveCount() == 1);
}

TEST_CASE("ObjectPool.allocate_then_deallocate")
{
    auto desc = makeProductType(2);
    ObjectPool pool;

    auto* obj = pool.allocate(&desc);
    pool.deallocate(obj);
    CHECK(pool.liveCount() == 0);
}

TEST_CASE("ObjectPool.allocate_many")
{
    auto desc = makeProductType(1);
    ObjectPool pool;

    std::vector<TypedObject*> objects;
    objects.reserve(100);
    for (int i = 0; i < 100; ++i)
        objects.push_back(pool.allocate(&desc));

    CHECK(pool.liveCount() == 100);

    for (auto* obj: objects)
        pool.deallocate(obj);

    CHECK(pool.liveCount() == 0);
}

TEST_CASE("ObjectPool.deallocate_reverse_order")
{
    auto desc = makeProductType(1);
    ObjectPool pool;

    std::vector<TypedObject*> objects;
    objects.reserve(10);
    for (int i = 0; i < 10; ++i)
        objects.push_back(pool.allocate(&desc));

    for (auto& obj: objects | std::ranges::views::reverse)
        pool.deallocate(obj);

    CHECK(pool.liveCount() == 0);
}

TEST_CASE("ObjectPool.reuse_after_dealloc")
{
    auto desc = makeProductType(1);
    ObjectPool pool;

    auto* first = pool.allocate(&desc);
    pool.deallocate(first);

    // Should reuse the freed cell.
    auto* second = pool.allocate(&desc);
    REQUIRE(second != nullptr);
    CHECK(pool.liveCount() == 1);
    CHECK(pool.totalAllocations() == 2);
}

TEST_CASE("ObjectPool.totalAllocations_monotonic")
{
    auto desc = makeProductType(0);
    ObjectPool pool;

    (void) pool.allocate(&desc);
    CHECK(pool.totalAllocations() == 1);

    auto* obj = pool.allocate(&desc);
    CHECK(pool.totalAllocations() == 2);

    pool.deallocate(obj);
    CHECK(pool.totalAllocations() == 2); // unchanged after dealloc

    (void) pool.allocate(&desc);
    CHECK(pool.totalAllocations() == 3);
}

// =============================================================================
// Pointer Validation (owns)
// =============================================================================

TEST_CASE("ObjectPool.owns_allocated")
{
    auto desc = makeProductType(2);
    ObjectPool pool;

    auto* obj = pool.allocate(&desc);
    CHECK(pool.owns(obj));
}

TEST_CASE("ObjectPool.owns_false_for_stack_pointer")
{
    ObjectPool pool;
    uint8_t dummy[64] {};
    CHECK_FALSE(pool.owns(&dummy));
}

TEST_CASE("ObjectPool.owns_false_for_heap_pointer")
{
    ObjectPool pool;
    auto* heap = new uint8_t[64];
    CHECK_FALSE(pool.owns(heap));
    delete[] heap;
}

TEST_CASE("ObjectPool.owns_false_for_nullptr")
{
    ObjectPool pool;
    CHECK_FALSE(pool.owns(nullptr));
}

TEST_CASE("ObjectPool.owns_false_after_dealloc")
{
    auto desc = makeProductType(1);
    ObjectPool pool;

    auto* obj = pool.allocate(&desc);
    pool.deallocate(obj);
    CHECK_FALSE(pool.owns(obj));
}

TEST_CASE("ObjectPool.owns_false_for_misaligned_pointer")
{
    auto desc = makeProductType(2);
    ObjectPool pool;

    auto* obj = pool.allocate(&desc);
    // Offset by 1 byte — not at a cell boundary.
    auto* misaligned = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(obj) + 1);
    CHECK_FALSE(pool.owns(misaligned));
}

// =============================================================================
// Size Classes
// =============================================================================

TEST_CASE("ObjectPool.size_class_zero_slots")
{
    auto desc = makeProductType(0); // 16 bytes
    ObjectPool pool;
    auto* obj = pool.allocate(&desc);
    REQUIRE(obj != nullptr);
    CHECK(obj->type == &desc);
    pool.deallocate(obj);
}

TEST_CASE("ObjectPool.size_class_various_slots")
{
    ObjectPool pool;

    for (uint16_t slots: { 1, 2, 3, 4, 5, 7, 16 })
    {
        auto desc = makeProductType(slots);
        auto* obj = pool.allocate(&desc);
        REQUIRE(obj != nullptr);
        CHECK(obj->type == &desc);
        CHECK(obj->refCount.load() == 1);
        pool.deallocate(obj);
    }
}

TEST_CASE("ObjectPool.mixed_size_classes")
{
    auto desc0 = makeProductType(0); // 16 bytes
    auto desc1 = makeProductType(1); // 24 bytes
    auto desc3 = makeProductType(3); // 40 bytes → class 48
    auto desc7 = makeProductType(7); // 72 bytes → class 96
    ObjectPool pool;

    auto* a = pool.allocate(&desc0);
    auto* b = pool.allocate(&desc1);
    auto* c = pool.allocate(&desc3);
    auto* d = pool.allocate(&desc7);

    CHECK(pool.liveCount() == 4);
    CHECK(pool.owns(a));
    CHECK(pool.owns(b));
    CHECK(pool.owns(c));
    CHECK(pool.owns(d));

    pool.deallocate(a);
    pool.deallocate(b);
    pool.deallocate(c);
    pool.deallocate(d);
    CHECK(pool.liveCount() == 0);
}

// =============================================================================
// Object Initialization
// =============================================================================

TEST_CASE("ObjectPool.object_init_refcount")
{
    auto desc = makeProductType(2);
    ObjectPool pool;
    auto* obj = pool.allocate(&desc);
    CHECK(obj->refCount.load() == 1);
    pool.deallocate(obj);
}

TEST_CASE("ObjectPool.object_init_tag")
{
    auto desc = makeSumType(2);
    ObjectPool pool;
    auto* obj = pool.allocate(&desc);
    CHECK(obj->tag == 0);
    pool.deallocate(obj);
}

TEST_CASE("ObjectPool.object_init_slots_zeroed")
{
    auto desc = makeProductType(3);
    ObjectPool pool;
    auto* obj = pool.allocate(&desc);
    CHECK(obj->getSlot(0) == 0);
    CHECK(obj->getSlot(1) == 0);
    CHECK(obj->getSlot(2) == 0);
    pool.deallocate(obj);
}

TEST_CASE("ObjectPool.object_init_type_pointer")
{
    auto desc = makeProductType(1);
    ObjectPool pool;
    auto* obj = pool.allocate(&desc);
    CHECK(obj->type == &desc);
    pool.deallocate(obj);
}

// =============================================================================
// Slab Growth
// =============================================================================

TEST_CASE("ObjectPool.slab_growth")
{
    auto desc = makeProductType(1); // 24 bytes per object
    // Tiny slabs to force multiple slab allocations.
    ObjectPool pool(48); // ~2 objects per slab

    std::vector<TypedObject*> objects;
    objects.reserve(20);
    for (int i = 0; i < 20; ++i)
        objects.push_back(pool.allocate(&desc));

    CHECK(pool.liveCount() == 20);

    // All objects should be owned.
    for (auto* obj: objects)
        CHECK(pool.owns(obj));

    for (auto* obj: objects)
        pool.deallocate(obj);
    CHECK(pool.liveCount() == 0);
}

// =============================================================================
// forEachLiveObject Enumeration
// =============================================================================

TEST_CASE("ObjectPool.forEach_empty")
{
    ObjectPool pool;
    int count = 0;
    pool.forEachLiveObject([&](TypedObject&) { ++count; });
    CHECK(count == 0);
}

TEST_CASE("ObjectPool.forEach_all_live")
{
    auto desc = makeProductType(1);
    ObjectPool pool;

    for (int i = 0; i < 5; ++i)
        (void) pool.allocate(&desc);

    int count = 0;
    pool.forEachLiveObject([&](TypedObject&) { ++count; });
    CHECK(count == 5);
}

TEST_CASE("ObjectPool.forEach_skips_freed")
{
    auto desc = makeProductType(1);
    ObjectPool pool;

    std::vector<TypedObject*> objects;
    objects.reserve(5);
    for (int i = 0; i < 5; ++i)
        objects.push_back(pool.allocate(&desc));

    pool.deallocate(objects[1]);
    pool.deallocate(objects[3]);

    int count = 0;
    pool.forEachLiveObject([&](TypedObject&) { ++count; });
    CHECK(count == 3);
}

TEST_CASE("ObjectPool.forEach_multiple_size_classes")
{
    auto desc1 = makeProductType(1); // 24 bytes
    auto desc3 = makeProductType(3); // 40 bytes
    ObjectPool pool;

    (void) pool.allocate(&desc1);
    (void) pool.allocate(&desc1);
    (void) pool.allocate(&desc3);

    int count = 0;
    pool.forEachLiveObject([&](TypedObject&) { ++count; });
    CHECK(count == 3);
}

TEST_CASE("ObjectPool.forEach_interleaved_alloc_dealloc")
{
    auto desc = makeProductType(2);
    ObjectPool pool;

    auto* a = pool.allocate(&desc);
    auto* b = pool.allocate(&desc);
    pool.deallocate(a);
    auto* c = pool.allocate(&desc);
    pool.deallocate(b);

    // Only 'c' should be live.
    int count = 0;
    std::vector<TypedObject*> visited;
    pool.forEachLiveObject([&](TypedObject& obj) {
        ++count;
        visited.push_back(&obj);
    });
    CHECK(count == 1);
    CHECK(visited[0] == c);

    pool.deallocate(c);
}

// =============================================================================
// Stress Test
// =============================================================================

TEST_CASE("ObjectPool.stress_10000")
{
    auto desc = makeProductType(2);
    ObjectPool pool;

    std::vector<TypedObject*> objects;
    objects.reserve(10000);
    for (int i = 0; i < 10000; ++i)
        objects.push_back(pool.allocate(&desc));

    CHECK(pool.liveCount() == 10000);

    for (auto* obj: objects)
        pool.deallocate(obj);

    CHECK(pool.liveCount() == 0);
}
