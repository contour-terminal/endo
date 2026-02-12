// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "OutputDefinitionRegistry.hpp"

using namespace endo;

#if defined(ENDO_DEFINITIONS_DIR)
static constexpr auto DefinitionsDir = ENDO_DEFINITIONS_DIR;
#else
static constexpr auto DefinitionsDir = "";
#endif

TEST_CASE("OutputDefinitionRegistry.load_yaml_file")
{
    OutputDefinitionRegistry registry;
    auto const path = std::filesystem::path(DefinitionsDir) / "docker.endo-output.yml";
    REQUIRE(std::filesystem::exists(path));
    CHECK(registry.loadFromFile(path));
    CHECK(registry.definitions().size() == 1);
    CHECK(registry.definitions()[0].command == "docker");
    CHECK(registry.definitions()[0].variants.size() == 2);
}

TEST_CASE("OutputDefinitionRegistry.load_from_directory")
{
    OutputDefinitionRegistry registry;
    registry.loadFromDirectory(DefinitionsDir);
    CHECK(registry.definitions().size() >= 2); // docker + git
}

TEST_CASE("OutputDefinitionRegistry.match_exact_args")
{
    OutputDefinitionRegistry registry;
    registry.loadFromDirectory(DefinitionsDir);

    auto const* match = registry.findMatch("docker", { "ps" });
    REQUIRE(match != nullptr);
    CHECK(match->name == "ps");
}

TEST_CASE("OutputDefinitionRegistry.match_with_extra_args")
{
    OutputDefinitionRegistry registry;
    registry.loadFromDirectory(DefinitionsDir);

    auto const* match = registry.findMatch("docker", { "ps", "-a" });
    REQUIRE(match != nullptr);
    CHECK(match->name == "ps");
}

TEST_CASE("OutputDefinitionRegistry.no_match")
{
    OutputDefinitionRegistry registry;
    registry.loadFromDirectory(DefinitionsDir);

    auto const* match = registry.findMatch("docker", { "run" });
    CHECK(match == nullptr);
}

TEST_CASE("OutputDefinitionRegistry.git_log_match")
{
    OutputDefinitionRegistry registry;
    registry.loadFromDirectory(DefinitionsDir);

    auto const* match = registry.findMatch("git", { "log" });
    REQUIRE(match != nullptr);
    CHECK(match->name == "log");
    CHECK(match->parser.type == ParserConfig::Type::Fields);
}

TEST_CASE("OutputDefinitionRegistry.git_status_match")
{
    OutputDefinitionRegistry registry;
    registry.loadFromDirectory(DefinitionsDir);

    auto const* match = registry.findMatch("git", { "status" });
    REQUIRE(match != nullptr);
    CHECK(match->name == "status");
    CHECK(match->parser.maxFields.has_value());
    CHECK(*match->parser.maxFields == 2);
}

TEST_CASE("OutputDefinitionRegistry.docker_images_match")
{
    OutputDefinitionRegistry registry;
    registry.loadFromDirectory(DefinitionsDir);

    auto const* match = registry.findMatch("docker", { "images" });
    REQUIRE(match != nullptr);
    CHECK(match->name == "images");
    CHECK(match->schema.size() == 5);
}

TEST_CASE("OutputDefinitionRegistry.all_variants")
{
    OutputDefinitionRegistry registry;
    registry.loadFromDirectory(DefinitionsDir);

    auto const variants = registry.allVariants();
    CHECK(variants.size() >= 4); // docker ps, docker images, git log, git status
}

TEST_CASE("OutputDefinitionRegistry.record_type_name_derived")
{
    OutputDefinitionRegistry registry;
    registry.loadFromDirectory(DefinitionsDir);

    auto const* match = registry.findMatch("docker", { "ps" });
    REQUIRE(match != nullptr);
    CHECK(match->recordTypeName == "DockerPsRecord");
}

TEST_CASE("OutputDefinitionRegistry.nonexistent_directory")
{
    OutputDefinitionRegistry registry;
    // Should not crash or throw on nonexistent directory
    registry.loadFromDirectory("/nonexistent/path/that/does/not/exist");
    CHECK(registry.definitions().empty());
}
