// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>
#include <CoreVM/vm/GarbageCollector.hpp>
#include <CoreVM/vm/ObjectPool.hpp>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace CoreVM;

namespace
{

// Minimal type descriptors for GC tests.
TypeDescriptor makeLeafProduct()
{
    TypeDescriptor desc;
    desc.kind = TypeKind::Product;
    desc.id = 200;
    desc.name = "Leaf";
    desc.slotCount = 0;
    return desc;
}

/// A product type with N object slots (all fixed).
TypeDescriptor makeObjProduct(uint16_t slotCount, uint16_t id = 201)
{
    TypeDescriptor desc;
    desc.kind = TypeKind::Product;
    desc.id = id;
    desc.name = "ObjProd" + std::to_string(id);
    desc.slotCount = slotCount;
    for (uint16_t i = 0; i < slotCount; ++i)
    {
        desc.fields.push_back({ "f" + std::to_string(i), static_cast<uint8_t>(i), LiteralType::Object });
        desc.traceInfo.fixedObjectSlots.push_back(static_cast<uint8_t>(i));
    }
    return desc;
}

/// Option-like sum type for testing dynamic slot tracing.
TypeDescriptor makeOptionLike(uint16_t id = 210)
{
    TypeDescriptor desc;
    desc.kind = TypeKind::Sum;
    desc.id = id;
    desc.name = "OptionLike" + std::to_string(id);
    desc.slotCount = 2; // payload + type tag
    desc.variants = { { "None", 0 }, { "Some", 1 } };
    desc.traceInfo.variantFixedSlots = { {}, {} };
    desc.traceInfo.variantDynamicSlots = {
        {},
        { SlotTraceInfo::DynamicSlot { .slotIndex = 0, .typeTagSlot = 1, .tagPosition = 0 } },
    };
    return desc;
}

/// List-like sum type: Nil | Cons(head, tail)
TypeDescriptor makeListLike(uint16_t id = 220)
{
    TypeDescriptor desc;
    desc.kind = TypeKind::Sum;
    desc.id = id;
    desc.name = "ListLike" + std::to_string(id);
    desc.slotCount = 3; // head + tail + type tag
    desc.variants = { { "Nil", 0 }, { "Cons", 2 } };
    desc.traceInfo.variantFixedSlots = { {}, { 1 } }; // tail always object
    desc.traceInfo.variantDynamicSlots = {
        {},
        { SlotTraceInfo::DynamicSlot { .slotIndex = 0, .typeTagSlot = 2, .tagPosition = 0 } },
    };
    return desc;
}

/// Tuple2-like product with packed type tag.
TypeDescriptor makeTuple2Like(uint16_t id = 230)
{
    TypeDescriptor desc;
    desc.kind = TypeKind::Product;
    desc.id = id;
    desc.name = "Tuple2Like" + std::to_string(id);
    desc.slotCount = 3; // 2 elements + packed type tag
    desc.traceInfo.dynamicSlots = {
        SlotTraceInfo::DynamicSlot { .slotIndex = 0, .typeTagSlot = 2, .tagPosition = 0 },
        SlotTraceInfo::DynamicSlot { .slotIndex = 1, .typeTagSlot = 2, .tagPosition = 1 },
    };
    return desc;
}

} // namespace

// =============================================================================
// Mark Phase — Reachability
// =============================================================================

TEST_CASE("GC.single_root_preserved")
{
    auto desc = makeLeafProduct();
    ObjectPool pool;
    auto* obj = pool.allocate(&desc);

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots = { obj };
    auto collected = gc.collect(roots);

    CHECK(collected == 0);
    CHECK(pool.liveCount() == 1);
    pool.deallocate(obj);
}

TEST_CASE("GC.root_with_child_both_preserved")
{
    auto parentDesc = makeObjProduct(1, 201);
    auto childDesc = makeLeafProduct();
    ObjectPool pool;

    auto* child = pool.allocate(&childDesc);
    auto* parent = pool.allocate(&parentDesc);
    parent->setSlot(0, reinterpret_cast<uint64_t>(child));

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots = { parent };
    auto collected = gc.collect(roots);

    CHECK(collected == 0);
    CHECK(pool.liveCount() == 2);

    pool.deallocate(parent);
    pool.deallocate(child);
}

TEST_CASE("GC.no_roots_all_collected")
{
    auto desc = makeLeafProduct();
    ObjectPool pool;

    (void) pool.allocate(&desc);
    (void) pool.allocate(&desc);
    (void) pool.allocate(&desc);

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots;
    auto collected = gc.collect(roots);

    CHECK(collected == 3);
    CHECK(pool.liveCount() == 0);
}

TEST_CASE("GC.multiple_roots_union_preserved")
{
    auto desc = makeLeafProduct();
    ObjectPool pool;

    auto* a = pool.allocate(&desc);
    auto* b = pool.allocate(&desc);
    auto* c = pool.allocate(&desc); // not rooted

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots = { a, b };
    auto collected = gc.collect(roots);

    CHECK(collected == 1);
    CHECK(pool.liveCount() == 2);
    CHECK(pool.owns(a));
    CHECK(pool.owns(b));
    CHECK_FALSE(pool.owns(c));

    pool.deallocate(a);
    pool.deallocate(b);
}

// =============================================================================
// Sweep Phase — Collection
// =============================================================================

TEST_CASE("GC.empty_pool_no_crash")
{
    ObjectPool pool;
    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots;
    CHECK(gc.collect(roots) == 0);
}

TEST_CASE("GC.partial_reachability")
{
    auto desc = makeLeafProduct();
    auto parentDesc = makeObjProduct(1, 202);
    ObjectPool pool;

    auto* child = pool.allocate(&desc);
    auto* parent = pool.allocate(&parentDesc);
    parent->setSlot(0, reinterpret_cast<uint64_t>(child));
    auto* orphan1 = pool.allocate(&desc);
    auto* orphan2 = pool.allocate(&desc);
    (void) orphan1;
    (void) orphan2;

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots = { parent };
    auto collected = gc.collect(roots);

    CHECK(collected == 2);
    CHECK(pool.liveCount() == 2);

    pool.deallocate(parent);
    pool.deallocate(child);
}

// =============================================================================
// Cycle Detection
// =============================================================================

TEST_CASE("GC.two_object_cycle_collected")
{
    auto desc = makeObjProduct(1, 203);
    ObjectPool pool;

    auto* a = pool.allocate(&desc);
    auto* b = pool.allocate(&desc);
    a->setSlot(0, reinterpret_cast<uint64_t>(b));
    b->setSlot(0, reinterpret_cast<uint64_t>(a));

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots;
    auto collected = gc.collect(roots);

    CHECK(collected == 2);
    CHECK(pool.liveCount() == 0);
}

TEST_CASE("GC.three_object_cycle_collected")
{
    auto desc = makeObjProduct(1, 204);
    ObjectPool pool;

    auto* a = pool.allocate(&desc);
    auto* b = pool.allocate(&desc);
    auto* c = pool.allocate(&desc);
    a->setSlot(0, reinterpret_cast<uint64_t>(b));
    b->setSlot(0, reinterpret_cast<uint64_t>(c));
    c->setSlot(0, reinterpret_cast<uint64_t>(a));

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots;
    CHECK(gc.collect(roots) == 3);
    CHECK(pool.liveCount() == 0);
}

TEST_CASE("GC.rooted_cycle_preserved")
{
    auto desc = makeObjProduct(1, 205);
    ObjectPool pool;

    auto* a = pool.allocate(&desc);
    auto* b = pool.allocate(&desc);
    a->setSlot(0, reinterpret_cast<uint64_t>(b));
    b->setSlot(0, reinterpret_cast<uint64_t>(a));

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots = { a };
    CHECK(gc.collect(roots) == 0);
    CHECK(pool.liveCount() == 2);

    pool.deallocate(a);
    pool.deallocate(b);
}

TEST_CASE("GC.self_referential_collected")
{
    auto desc = makeObjProduct(1, 206);
    ObjectPool pool;

    auto* obj = pool.allocate(&desc);
    obj->setSlot(0, reinterpret_cast<uint64_t>(obj));

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots;
    CHECK(gc.collect(roots) == 1);
    CHECK(pool.liveCount() == 0);
}

// =============================================================================
// SlotTraceInfo Traversal Correctness
// =============================================================================

TEST_CASE("GC.list_cons_tail_traced")
{
    auto listDesc = makeListLike(221);
    auto leafDesc = makeLeafProduct();
    ObjectPool pool;

    // Build: Cons(42, Nil)
    auto* nil = pool.allocate(&listDesc);
    nil->tag = 0; // Nil

    auto* cons = pool.allocate(&listDesc);
    cons->tag = 1;                                                // Cons
    cons->setSlot(0, 42);                                         // head (Number)
    cons->setSlot(1, reinterpret_cast<uint64_t>(nil));            // tail (object)
    cons->setSlot(2, static_cast<uint64_t>(LiteralType::Number)); // type tag

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots = { cons };
    CHECK(gc.collect(roots) == 0);
    CHECK(pool.liveCount() == 2);

    pool.deallocate(cons);
    pool.deallocate(nil);
}

TEST_CASE("GC.list_cons_object_head_traced")
{
    auto listDesc = makeListLike(222);
    auto leafDesc = makeLeafProduct();
    ObjectPool pool;

    auto* nil = pool.allocate(&listDesc);
    nil->tag = 0;

    auto* headObj = pool.allocate(&leafDesc);

    auto* cons = pool.allocate(&listDesc);
    cons->tag = 1;
    cons->setSlot(0, reinterpret_cast<uint64_t>(headObj));        // head (Object)
    cons->setSlot(1, reinterpret_cast<uint64_t>(nil));            // tail
    cons->setSlot(2, static_cast<uint64_t>(LiteralType::Object)); // type tag → head is object

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots = { cons };
    CHECK(gc.collect(roots) == 0); // all 3 reachable
    CHECK(pool.liveCount() == 3);

    pool.deallocate(cons);
    pool.deallocate(headObj);
    pool.deallocate(nil);
}

TEST_CASE("GC.list_cons_number_head_not_traced")
{
    auto listDesc = makeListLike(223);
    auto leafDesc = makeLeafProduct();
    ObjectPool pool;

    auto* nil = pool.allocate(&listDesc);
    nil->tag = 0;

    auto* orphan = pool.allocate(&leafDesc); // unreachable

    auto* cons = pool.allocate(&listDesc);
    cons->tag = 1;
    cons->setSlot(0, 42);                                         // head is a Number, not object
    cons->setSlot(1, reinterpret_cast<uint64_t>(nil));            // tail
    cons->setSlot(2, static_cast<uint64_t>(LiteralType::Number)); // type tag → head NOT traced

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots = { cons };
    auto collected = gc.collect(roots);

    CHECK(collected == 1); // orphan collected
    CHECK(pool.liveCount() == 2);
    CHECK_FALSE(pool.owns(orphan));

    pool.deallocate(cons);
    pool.deallocate(nil);
}

TEST_CASE("GC.option_some_object_traced")
{
    auto optDesc = makeOptionLike(211);
    auto leafDesc = makeLeafProduct();
    ObjectPool pool;

    auto* inner = pool.allocate(&leafDesc);

    auto* some = pool.allocate(&optDesc);
    some->tag = 1; // Some
    some->setSlot(0, reinterpret_cast<uint64_t>(inner));
    some->setSlot(1, static_cast<uint64_t>(LiteralType::Object));

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots = { some };
    CHECK(gc.collect(roots) == 0);
    CHECK(pool.liveCount() == 2);

    pool.deallocate(some);
    pool.deallocate(inner);
}

TEST_CASE("GC.option_some_number_not_traced")
{
    auto optDesc = makeOptionLike(212);
    auto leafDesc = makeLeafProduct();
    ObjectPool pool;

    auto* orphan = pool.allocate(&leafDesc);

    auto* some = pool.allocate(&optDesc);
    some->tag = 1;
    some->setSlot(0, 42);
    some->setSlot(1, static_cast<uint64_t>(LiteralType::Number));

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots = { some };
    CHECK(gc.collect(roots) == 1); // orphan collected
    CHECK_FALSE(pool.owns(orphan));

    pool.deallocate(some);
}

TEST_CASE("GC.tuple2_both_objects_traced")
{
    auto tupleDesc = makeTuple2Like(231);
    auto leafDesc = makeLeafProduct();
    ObjectPool pool;

    auto* child0 = pool.allocate(&leafDesc);
    auto* child1 = pool.allocate(&leafDesc);

    auto* tuple = pool.allocate(&tupleDesc);
    tuple->setSlot(0, reinterpret_cast<uint64_t>(child0));
    tuple->setSlot(1, reinterpret_cast<uint64_t>(child1));
    tuple->setSlot(2, packTypeTag(LiteralType::Object, LiteralType::Object));

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots = { tuple };
    CHECK(gc.collect(roots) == 0);
    CHECK(pool.liveCount() == 3);

    pool.deallocate(tuple);
    pool.deallocate(child0);
    pool.deallocate(child1);
}

TEST_CASE("GC.tuple2_mixed_only_object_slot_traced")
{
    auto tupleDesc = makeTuple2Like(232);
    auto leafDesc = makeLeafProduct();
    ObjectPool pool;

    auto* child = pool.allocate(&leafDesc);
    auto* orphan = pool.allocate(&leafDesc);

    auto* tuple = pool.allocate(&tupleDesc);
    tuple->setSlot(0, reinterpret_cast<uint64_t>(child));
    tuple->setSlot(1, 99); // Number
    tuple->setSlot(2, packTypeTag(LiteralType::Object, LiteralType::Number));

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots = { tuple };
    CHECK(gc.collect(roots) == 1);
    CHECK_FALSE(pool.owns(orphan));

    pool.deallocate(tuple);
    pool.deallocate(child);
}

TEST_CASE("GC.product_fixed_object_slots")
{
    auto parentDesc = makeObjProduct(2, 207);
    auto leafDesc = makeLeafProduct();
    ObjectPool pool;

    auto* c0 = pool.allocate(&leafDesc);
    auto* c1 = pool.allocate(&leafDesc);

    auto* parent = pool.allocate(&parentDesc);
    parent->setSlot(0, reinterpret_cast<uint64_t>(c0));
    parent->setSlot(1, reinterpret_cast<uint64_t>(c1));

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots = { parent };
    CHECK(gc.collect(roots) == 0);
    CHECK(pool.liveCount() == 3);

    pool.deallocate(parent);
    pool.deallocate(c0);
    pool.deallocate(c1);
}

// =============================================================================
// Performance / Edge Cases
// =============================================================================

TEST_CASE("GC.single_object_single_root")
{
    auto desc = makeLeafProduct();
    ObjectPool pool;
    auto* obj = pool.allocate(&desc);

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots = { obj };
    CHECK(gc.collect(roots) == 0);

    pool.deallocate(obj);
}

TEST_CASE("GC.1000_objects_no_roots_all_collected")
{
    auto desc = makeLeafProduct();
    ObjectPool pool;

    for (int i = 0; i < 1000; ++i)
        (void) pool.allocate(&desc);

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots;
    CHECK(gc.collect(roots) == 1000);
    CHECK(pool.liveCount() == 0);
}

TEST_CASE("GC.idempotent_double_collect")
{
    auto desc = makeLeafProduct();
    ObjectPool pool;

    (void) pool.allocate(&desc);
    (void) pool.allocate(&desc);

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots;
    CHECK(gc.collect(roots) == 2);
    CHECK(gc.collect(roots) == 0); // nothing left to collect
    CHECK(pool.liveCount() == 0);
}

TEST_CASE("GC.deep_chain_marked")
{
    auto desc = makeObjProduct(1, 208);
    ObjectPool pool;

    // Build chain: root -> obj1 -> obj2 -> ... -> obj100
    TypedObject* prev = nullptr;
    for (int i = 0; i < 100; ++i)
    {
        auto* obj = pool.allocate(&desc);
        if (prev)
            prev->setSlot(0, reinterpret_cast<uint64_t>(obj));
        prev = obj;
    }

    // Root is the first object in the pool — find it via forEachLiveObject.
    TypedObject* root = nullptr;
    pool.forEachLiveObject([&](TypedObject& obj) {
        if (!root)
            root = &obj;
    });

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots = { root };
    CHECK(gc.collect(roots) == 0);
    CHECK(pool.liveCount() == 100);

    // Cleanup
    std::vector<TypedObject*> all;
    pool.forEachLiveObject([&](TypedObject& obj) { all.push_back(&obj); });
    for (auto* obj: all)
        pool.deallocate(obj);
}

TEST_CASE("GC.cycle_with_unreachable_acyclic_garbage")
{
    auto desc = makeObjProduct(1, 209);
    auto leafDesc = makeLeafProduct();
    ObjectPool pool;

    // Unreachable cycle.
    auto* ca = pool.allocate(&desc);
    auto* cb = pool.allocate(&desc);
    ca->setSlot(0, reinterpret_cast<uint64_t>(cb));
    cb->setSlot(0, reinterpret_cast<uint64_t>(ca));

    // Unreachable acyclic.
    (void) pool.allocate(&leafDesc);

    // Rooted object.
    auto* rooted = pool.allocate(&leafDesc);

    GarbageCollector gc(pool);
    std::vector<TypedObject*> roots = { rooted };
    CHECK(gc.collect(roots) == 3); // cycle(2) + acyclic(1)
    CHECK(pool.liveCount() == 1);

    pool.deallocate(rooted);
}
