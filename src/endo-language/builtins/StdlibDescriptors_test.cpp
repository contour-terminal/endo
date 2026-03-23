// SPDX-License-Identifier: Apache-2.0
#include <endo-language/builtins/StdlibDescriptors.hpp>

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <utility>

using namespace endo;

// =============================================================================
// Descriptor invariant tests
// =============================================================================

TEST_CASE("StdlibDescriptors.every_user_facing_entry_has_description", "[stdlib][invariants]")
{
    for (auto const& desc: stdlibDescriptors())
        if (!desc.userFacingName.empty())
            CHECK(!desc.description.empty());
}

TEST_CASE("StdlibDescriptors.every_user_facing_entry_has_detail", "[stdlib][invariants]")
{
    for (auto const& desc: stdlibDescriptors())
        if (!desc.userFacingName.empty())
            CHECK(!desc.detail.empty());
}

TEST_CASE("StdlibDescriptors.description_contains_signature_arrow", "[stdlib][invariants]")
{
    for (auto const& desc: stdlibDescriptors())
        if (!desc.userFacingName.empty())
        {
            INFO("Entry: " << desc.userFacingName);
            CHECK(desc.description.find("->") != std::string_view::npos);
        }
}

TEST_CASE("StdlibDescriptors.detail_starts_with_markdown_bold", "[stdlib][invariants]")
{
    for (auto const& desc: stdlibDescriptors())
        if (!desc.userFacingName.empty())
        {
            INFO("Entry: " << desc.userFacingName);
            CHECK(desc.detail.starts_with("**"));
        }
}

TEST_CASE("StdlibDescriptors.no_duplicate_user_facing_names", "[stdlib][invariants]")
{
    std::set<std::string_view> seen;
    for (auto const& desc: stdlibDescriptors())
        if (!desc.userFacingName.empty())
        {
            INFO("Duplicate: " << desc.userFacingName);
            CHECK(seen.insert(desc.userFacingName).second);
        }
}

TEST_CASE("StdlibDescriptors.sharedImpl_requires_vmName", "[stdlib][invariants]")
{
    for (auto const& desc: stdlibDescriptors())
        if (desc.sharedImpl != nullptr)
        {
            INFO("Entry with sharedImpl but no vmName (userFacing: " << desc.userFacingName << ")");
            CHECK(!desc.vmName.empty());
        }
}

TEST_CASE("StdlibDescriptors.no_vmName_arity_collisions", "[stdlib][invariants]")
{
    std::set<std::pair<std::string_view, size_t>> seen;
    for (auto const& desc: stdlibDescriptors())
        if (!desc.vmName.empty())
        {
            auto key = std::pair { desc.vmName, desc.params.size() };
            INFO("Collision: vmName=" << desc.vmName << " arity=" << desc.params.size());
            CHECK(seen.insert(key).second);
        }
}

TEST_CASE("StdlibDescriptors.user_facing_count_stability", "[stdlib][invariants]")
{
    size_t count = 0;
    for (auto const& desc: stdlibDescriptors())
        if (!desc.userFacingName.empty())
            ++count;
    CHECK(count == 59);
}
