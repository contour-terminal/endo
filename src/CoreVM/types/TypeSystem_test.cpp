// SPDX-License-Identifier: Apache-2.0
#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypeRegistry.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace CoreVM;

// =============================================================================
// TypeDescriptor Tests
// =============================================================================

TEST_CASE("TypeDescriptor.getVariant")
{
    TypeDescriptor desc;
    desc.kind = TypeKind::Sum;
    desc.variants = { { "None", 0 }, { "Some", 1 } };

    CHECK(desc.getVariant(0) != nullptr);
    CHECK(desc.getVariant(0)->name == "None");
    CHECK(desc.getVariant(0)->payloadSlots == 0);

    CHECK(desc.getVariant(1) != nullptr);
    CHECK(desc.getVariant(1)->name == "Some");
    CHECK(desc.getVariant(1)->payloadSlots == 1);

    CHECK(desc.getVariant(2) == nullptr);
}

TEST_CASE("TypeDescriptor.getVariantTag")
{
    TypeDescriptor desc;
    desc.kind = TypeKind::Sum;
    desc.variants = { { "Error", 1 }, { "Ok", 1 } };

    CHECK(desc.getVariantTag("Error") == 0);
    CHECK(desc.getVariantTag("Ok") == 1);
    CHECK(desc.getVariantTag("Unknown") == -1);
}

TEST_CASE("TypeDescriptor.getField")
{
    TypeDescriptor desc;
    desc.kind = TypeKind::Product;
    desc.fields = { { "x", 0 }, { "y", 1 }, { "z", 2 } };

    CHECK(desc.getField(0) != nullptr);
    CHECK(desc.getField(0)->name == "x");
    CHECK(desc.getField(0)->offset == 0);

    CHECK(desc.getField(2) != nullptr);
    CHECK(desc.getField(2)->name == "z");

    CHECK(desc.getField(3) == nullptr);
}

TEST_CASE("TypeDescriptor.getFieldByName")
{
    TypeDescriptor desc;
    desc.kind = TypeKind::Product;
    desc.fields = { { "name", 0 }, { "age", 1 } };

    CHECK(desc.getFieldByName("name") != nullptr);
    CHECK(desc.getFieldByName("name")->offset == 0);

    CHECK(desc.getFieldByName("age") != nullptr);
    CHECK(desc.getFieldByName("age")->offset == 1);

    CHECK(desc.getFieldByName("unknown") == nullptr);
}

// =============================================================================
// TypeRegistry Tests
// =============================================================================

TEST_CASE("TypeRegistry.builtins")
{
    TypeRegistry registry;

    // Option type should be registered
    const auto* optionType = registry.get(BuiltinTypeId::Option);
    REQUIRE(optionType != nullptr);
    CHECK(optionType->name == "Option");
    CHECK(optionType->kind == TypeKind::Sum);
    CHECK(optionType->variants.size() == 2);
    CHECK(optionType->getVariantTag("None") == 0);
    CHECK(optionType->getVariantTag("Some") == 1);

    // Result type should be registered
    const auto* resultType = registry.get(BuiltinTypeId::Result);
    REQUIRE(resultType != nullptr);
    CHECK(resultType->name == "Result");
    CHECK(resultType->kind == TypeKind::Sum);
    CHECK(resultType->variants.size() == 2);
    CHECK(resultType->getVariantTag("Error") == 0);
    CHECK(resultType->getVariantTag("Ok") == 1);
}

TEST_CASE("TypeRegistry.optionType_convenience")
{
    TypeRegistry registry;
    CHECK(registry.optionType() != nullptr);
    CHECK(registry.optionType()->id == BuiltinTypeId::Option);
}

TEST_CASE("TypeRegistry.resultType_convenience")
{
    TypeRegistry registry;
    CHECK(registry.resultType() != nullptr);
    CHECK(registry.resultType()->id == BuiltinTypeId::Result);
}

TEST_CASE("TypeRegistry.getByName")
{
    TypeRegistry registry;

    CHECK(registry.getByName("Option") != nullptr);
    CHECK(registry.getByName("Option")->id == BuiltinTypeId::Option);

    CHECK(registry.getByName("Result") != nullptr);
    CHECK(registry.getByName("Result")->id == BuiltinTypeId::Result);

    CHECK(registry.getByName("NonExistent") == nullptr);
}

TEST_CASE("TypeRegistry.registerSumType")
{
    TypeRegistry registry;

    auto* colorType = registry.registerSumType("Color",
                                               {
                                                   { "Red", 0 },
                                                   { "Green", 0 },
                                                   { "Blue", 0 },
                                                   { "RGB", 3 }, // 3 slots for r, g, b
                                               });

    REQUIRE(colorType != nullptr);
    CHECK(colorType->name == "Color");
    CHECK(colorType->kind == TypeKind::Sum);
    CHECK(colorType->variants.size() == 4);
    CHECK(colorType->slotCount == 3); // Max of all variant payloads

    CHECK(colorType->getVariantTag("Red") == 0);
    CHECK(colorType->getVariantTag("RGB") == 3);

    // Should be retrievable
    CHECK(registry.getByName("Color") == colorType);
    CHECK(registry.get(colorType->id) == colorType);
}

TEST_CASE("TypeRegistry.registerProductType")
{
    TypeRegistry registry;

    auto* pointType = registry.registerProductType("Point",
                                                   {
                                                       { "x", 0 },
                                                       { "y", 0 },
                                                   });

    REQUIRE(pointType != nullptr);
    CHECK(pointType->name == "Point");
    CHECK(pointType->kind == TypeKind::Product);
    CHECK(pointType->fields.size() == 2);
    CHECK(pointType->slotCount == 2);

    // Fields should have offsets assigned
    CHECK(pointType->fields[0].offset == 0);
    CHECK(pointType->fields[1].offset == 1);

    CHECK(registry.getByName("Point") == pointType);
}

TEST_CASE("TypeRegistry.registerFunctionType")
{
    TypeRegistry registry;

    auto* closureType = registry.registerFunctionType("Closure_1", 2);

    REQUIRE(closureType != nullptr);
    CHECK(closureType->name == "Closure_1");
    CHECK(closureType->kind == TypeKind::Function);
    CHECK(closureType->captureCount == 2);
    CHECK(closureType->slotCount == 2);

    CHECK(registry.getByName("Closure_1") == closureType);
}

TEST_CASE("TypeRegistry.size")
{
    TypeRegistry registry;
    size_t initialSize = registry.size(); // Includes builtins

    registry.registerSumType("Custom1", { { "A", 0 } });
    CHECK(registry.size() == initialSize + 1);

    registry.registerProductType("Custom2", { { "f", 0 } });
    CHECK(registry.size() == initialSize + 2);
}

// =============================================================================
// TypedObject Tests
// =============================================================================

TEST_CASE("TypedObject.allocationSize")
{
    TypeDescriptor desc;
    desc.slotCount = 0;
    CHECK(TypedObject::allocationSize(&desc) == sizeof(TypedObject));

    desc.slotCount = 1;
    CHECK(TypedObject::allocationSize(&desc) == sizeof(TypedObject) + sizeof(SlotValue));

    desc.slotCount = 3;
    CHECK(TypedObject::allocationSize(&desc) == sizeof(TypedObject) + 3 * sizeof(SlotValue));
}

TEST_CASE("TypedObject.refCount_retain_release")
{
    // Simulate allocation (normally done by Runner)
    TypeDescriptor desc;
    desc.kind = TypeKind::Sum;
    desc.slotCount = 1;

    std::vector<uint8_t> storage(TypedObject::allocationSize(&desc));
    auto* obj = reinterpret_cast<TypedObject*>(storage.data());
    obj->type = &desc;
    obj->refCount.store(1, std::memory_order_relaxed);
    obj->tag = 0;

    CHECK(obj->refCount.load() == 1);

    retainObject(obj);
    CHECK(obj->refCount.load() == 2);

    retainObject(obj);
    CHECK(obj->refCount.load() == 3);

    CHECK(releaseObject(obj) == false); // Not last ref
    CHECK(obj->refCount.load() == 2);

    CHECK(releaseObject(obj) == false);
    CHECK(obj->refCount.load() == 1);

    CHECK(releaseObject(obj) == true); // Last ref - should free
}

TEST_CASE("TypedObject.isUniqueRef")
{
    TypeDescriptor desc;
    desc.slotCount = 0;

    std::vector<uint8_t> storage(TypedObject::allocationSize(&desc));
    auto* obj = reinterpret_cast<TypedObject*>(storage.data());
    obj->type = &desc;
    obj->refCount.store(1, std::memory_order_relaxed);

    CHECK(isUniqueRef(obj) == true);

    retainObject(obj);
    CHECK(isUniqueRef(obj) == false);

    (void) releaseObject(obj);
    CHECK(isUniqueRef(obj) == true);
}

TEST_CASE("TypedObject.retain_release_nullptr")
{
    // Should be safe to call with nullptr
    retainObject(nullptr);
    CHECK(releaseObject(nullptr) == false);
    CHECK(isUniqueRef(nullptr) == false);
}

TEST_CASE("TypedObject.slots")
{
    TypeDescriptor desc;
    desc.kind = TypeKind::Sum;
    desc.slotCount = 2;

    std::vector<uint8_t> storage(TypedObject::allocationSize(&desc));
    auto* obj = reinterpret_cast<TypedObject*>(storage.data());
    obj->type = &desc;
    obj->refCount.store(1, std::memory_order_relaxed);

    // Set slot values
    obj->setSlot(0, 42);
    obj->setSlot(1, 123);

    CHECK(obj->getSlot(0) == 42);
    CHECK(obj->getSlot(1) == 123);

    // Also test via slots() pointer
    CHECK(obj->slots()[0] == 42);
    CHECK(obj->slots()[1] == 123);
}

TEST_CASE("TypedObject.isSumType")
{
    TypeDescriptor sumDesc;
    sumDesc.kind = TypeKind::Sum;
    sumDesc.slotCount = 0;

    TypeDescriptor prodDesc;
    prodDesc.kind = TypeKind::Product;
    prodDesc.slotCount = 0;

    std::vector<uint8_t> storage1(TypedObject::allocationSize(&sumDesc));
    auto* sumObj = reinterpret_cast<TypedObject*>(storage1.data());
    sumObj->type = &sumDesc;

    std::vector<uint8_t> storage2(TypedObject::allocationSize(&prodDesc));
    auto* prodObj = reinterpret_cast<TypedObject*>(storage2.data());
    prodObj->type = &prodDesc;

    CHECK(sumObj->isSumType() == true);
    CHECK(sumObj->isProductType() == false);

    CHECK(prodObj->isSumType() == false);
    CHECK(prodObj->isProductType() == true);
}

TEST_CASE("TypedObject.isVariant")
{
    TypeDescriptor desc;
    desc.kind = TypeKind::Sum;
    desc.slotCount = 1;
    desc.variants = { { "None", 0 }, { "Some", 1 } };

    std::vector<uint8_t> storage(TypedObject::allocationSize(&desc));
    auto* obj = reinterpret_cast<TypedObject*>(storage.data());
    obj->type = &desc;
    obj->tag = 0;

    CHECK(obj->isVariant("None") == true);
    CHECK(obj->isVariant("Some") == false);

    obj->tag = 1;
    CHECK(obj->isVariant("None") == false);
    CHECK(obj->isVariant("Some") == true);

    CHECK(obj->isVariant("Unknown") == false);
}
