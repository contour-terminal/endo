// SPDX-License-Identifier: Apache-2.0
#include <endo-language/ide/TypeRegistryCompletionAdapter.hpp>

#include <CoreVM/types/TypeRegistry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace endo;

namespace
{

/// @brief Helper to check if a specific text is in the candidates.
bool hasCandidate(std::vector<CompletionCandidate> const& items, std::string const& text)
{
    return std::ranges::any_of(items, [&](auto const& c) { return c.text == text; });
}

} // namespace

// =============================================================================
// builtinRecordFields tests
// =============================================================================

TEST_CASE("TypeRegistryAdapter.builtinRecordFields.contains_ProcessInfo", "[completion][adapter]")
{
    CoreVM::TypeRegistry registry;
    auto fields = builtinRecordFields(registry);
    REQUIRE(fields.contains("ProcessInfo"));
    auto const& processFields = fields.at("ProcessInfo");
    auto hasPid = std::ranges::any_of(processFields, [](auto const& f) { return f.name == "pid"; });
    auto hasUser = std::ranges::any_of(processFields, [](auto const& f) { return f.name == "user"; });
    auto hasCpu = std::ranges::any_of(processFields, [](auto const& f) { return f.name == "cpu"; });
    CHECK(hasPid);
    CHECK(hasUser);
    CHECK(hasCpu);
}

TEST_CASE("TypeRegistryAdapter.builtinRecordFields.contains_FileInfo", "[completion][adapter]")
{
    CoreVM::TypeRegistry registry;
    auto fields = builtinRecordFields(registry);
    REQUIRE(fields.contains("FileInfo"));
    auto const& fileFields = fields.at("FileInfo");
    auto hasName = std::ranges::any_of(fileFields, [](auto const& f) { return f.name == "name"; });
    auto hasSize = std::ranges::any_of(fileFields, [](auto const& f) { return f.name == "size"; });
    CHECK(hasName);
    CHECK(hasSize);
}

TEST_CASE("TypeRegistryAdapter.builtinRecordFields.FileInfo_nested_types", "[completion][adapter]")
{
    CoreVM::TypeRegistry registry;
    auto fields = builtinRecordFields(registry);
    REQUIRE(fields.contains("FileInfo"));
    auto const& fileFields = fields.at("FileInfo");

    // size should have nested type "Size"
    auto sizeIt = std::ranges::find_if(fileFields, [](auto const& f) { return f.name == "size"; });
    REQUIRE(sizeIt != fileFields.end());
    CHECK(sizeIt->typeName == "Size");

    // mtime should have nested type "DateTime"
    auto mtimeIt = std::ranges::find_if(fileFields, [](auto const& f) { return f.name == "mtime"; });
    REQUIRE(mtimeIt != fileFields.end());
    CHECK(mtimeIt->typeName == "DateTime");

    // mode should have nested type "FileMode"
    auto modeIt = std::ranges::find_if(fileFields, [](auto const& f) { return f.name == "mode"; });
    REQUIRE(modeIt != fileFields.end());
    CHECK(modeIt->typeName == "FileMode");
}

TEST_CASE("TypeRegistryAdapter.builtinRecordFields.contains_DateTime", "[completion][adapter]")
{
    CoreVM::TypeRegistry registry;
    auto fields = builtinRecordFields(registry);
    REQUIRE(fields.contains("DateTime"));
    auto const& dtFields = fields.at("DateTime");
    auto hasYear = std::ranges::any_of(dtFields, [](auto const& f) { return f.name == "year"; });
    auto hasEpoch = std::ranges::any_of(dtFields, [](auto const& f) { return f.name == "epoch"; });
    CHECK(hasYear);
    CHECK(hasEpoch);
}

TEST_CASE("TypeRegistryAdapter.builtinRecordFields.contains_Size", "[completion][adapter]")
{
    CoreVM::TypeRegistry registry;
    auto fields = builtinRecordFields(registry);
    REQUIRE(fields.contains("Size"));
    auto const& sizeFields = fields.at("Size");
    auto hasBytes = std::ranges::any_of(sizeFields, [](auto const& f) { return f.name == "bytes"; });
    CHECK(hasBytes);
}

TEST_CASE("TypeRegistryAdapter.builtinRecordFields.excludes_tuples", "[completion][adapter]")
{
    CoreVM::TypeRegistry registry;
    auto fields = builtinRecordFields(registry);
    CHECK(!fields.contains("Tuple2"));
    CHECK(!fields.contains("Tuple3"));
}

// =============================================================================
// builtinModuleFunctions tests
// =============================================================================

TEST_CASE("TypeRegistryAdapter.builtinModuleFunctions.contains_Option", "[completion][adapter]")
{
    CoreVM::TypeRegistry registry;
    auto funcs = builtinModuleFunctions(registry);
    REQUIRE(funcs.contains("Option"));
    auto const& optFuncs = funcs.at("Option");
    auto hasMap = std::ranges::any_of(optFuncs, [](auto const& f) { return f.name == "map"; });
    auto hasBind = std::ranges::any_of(optFuncs, [](auto const& f) { return f.name == "bind"; });
    auto hasDefault = std::ranges::any_of(optFuncs, [](auto const& f) { return f.name == "defaultValue"; });
    CHECK(hasMap);
    CHECK(hasBind);
    CHECK(hasDefault);
}

TEST_CASE("TypeRegistryAdapter.builtinModuleFunctions.contains_DateTime", "[completion][adapter]")
{
    CoreVM::TypeRegistry registry;
    auto funcs = builtinModuleFunctions(registry);
    REQUIRE(funcs.contains("DateTime"));
    auto const& dtFuncs = funcs.at("DateTime");
    auto hasNow = std::ranges::any_of(dtFuncs, [](auto const& f) { return f.name == "now"; });
    auto hasFromEpoch = std::ranges::any_of(dtFuncs, [](auto const& f) { return f.name == "fromEpoch"; });
    CHECK(hasNow);
    CHECK(hasFromEpoch);
}

TEST_CASE("TypeRegistryAdapter.builtinModuleFunctions.contains_Size", "[completion][adapter]")
{
    CoreVM::TypeRegistry registry;
    auto funcs = builtinModuleFunctions(registry);
    REQUIRE(funcs.contains("Size"));
    auto const& sizeFuncs = funcs.at("Size");
    auto hasFromBytes = std::ranges::any_of(sizeFuncs, [](auto const& f) { return f.name == "fromBytes"; });
    auto hasFromKB = std::ranges::any_of(sizeFuncs, [](auto const& f) { return f.name == "fromKB"; });
    CHECK(hasFromBytes);
    CHECK(hasFromKB);
    CHECK(sizeFuncs.size() == 5); // fromBytes, fromKB, fromMB, fromGB, fromTB
}

TEST_CASE("TypeRegistryAdapter.builtinModuleFunctions.contains_FileMode", "[completion][adapter]")
{
    CoreVM::TypeRegistry registry;
    auto funcs = builtinModuleFunctions(registry);
    REQUIRE(funcs.contains("FileMode"));
    auto const& fmFuncs = funcs.at("FileMode");
    auto hasFromBits = std::ranges::any_of(fmFuncs, [](auto const& f) { return f.name == "fromBits"; });
    CHECK(hasFromBits);
}

// =============================================================================
// constructorCandidatesFromRegistry tests
// =============================================================================

TEST_CASE("TypeRegistryAdapter.constructorCandidates.returns_Some_None_Ok_Error", "[completion][adapter]")
{
    CoreVM::TypeRegistry registry;
    auto ctors = constructorCandidatesFromRegistry(registry);
    REQUIRE(ctors.size() == 4);
    CHECK(hasCandidate(ctors, "Some"));
    CHECK(hasCandidate(ctors, "None"));
    CHECK(hasCandidate(ctors, "Ok"));
    CHECK(hasCandidate(ctors, "Error"));
}

TEST_CASE("TypeRegistryAdapter.constructorCandidates.kind_is_constructor", "[completion][adapter]")
{
    CoreVM::TypeRegistry registry;
    auto ctors = constructorCandidatesFromRegistry(registry);
    for (auto const& c: ctors)
        CHECK(c.kind == CompletionKind::Constructor);
}

TEST_CASE("TypeRegistryAdapter.constructorCandidates.excludes_List", "[completion][adapter]")
{
    CoreVM::TypeRegistry registry;
    auto ctors = constructorCandidatesFromRegistry(registry);
    CHECK(!hasCandidate(ctors, "Cons"));
    CHECK(!hasCandidate(ctors, "Nil"));
}

// =============================================================================
// moduleFunctionStdLibCandidates tests
// =============================================================================

TEST_CASE("TypeRegistryAdapter.moduleFunctionStdLibCandidates.returns_module_functions",
          "[completion][adapter]")
{
    CoreVM::TypeRegistry registry;
    auto candidates = moduleFunctionStdLibCandidates(registry);
    CHECK(!candidates.empty());
    CHECK(hasCandidate(candidates, "DateTime.now"));
    CHECK(hasCandidate(candidates, "DateTime.fromEpoch"));
    CHECK(hasCandidate(candidates, "Size.fromBytes"));
    CHECK(hasCandidate(candidates, "Size.fromKB"));
    CHECK(hasCandidate(candidates, "Size.fromMB"));
    CHECK(hasCandidate(candidates, "Size.fromGB"));
    CHECK(hasCandidate(candidates, "Size.fromTB"));
    CHECK(hasCandidate(candidates, "FileMode.fromBits"));
    CHECK(hasCandidate(candidates, "Option.map"));
    CHECK(hasCandidate(candidates, "Option.bind"));
    CHECK(hasCandidate(candidates, "Option.defaultValue"));
}

TEST_CASE("TypeRegistryAdapter.moduleFunctionStdLibCandidates.kind_is_function", "[completion][adapter]")
{
    CoreVM::TypeRegistry registry;
    auto candidates = moduleFunctionStdLibCandidates(registry);
    for (auto const& c: candidates)
        CHECK(c.kind == CompletionKind::Function);
}
