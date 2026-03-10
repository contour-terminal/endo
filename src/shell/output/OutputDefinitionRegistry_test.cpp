// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "OutputDefinitionRegistry.hpp"

#include <platform/NativeFileSystem.hpp>

using namespace endo;

#if defined(ENDO_DEFINITIONS_DIR)
static constexpr auto DefinitionsDir = ENDO_DEFINITIONS_DIR;
#else
static constexpr auto DefinitionsDir = "";
#endif

TEST_CASE("OutputDefinitionRegistry.load_yaml_file")
{
    auto const& fs = NativeFileSystem::instance();
    OutputDefinitionRegistry registry;
    auto const path = std::filesystem::path(DefinitionsDir) / "docker.endo-output.yml";
    REQUIRE(fs.exists(path));
    CHECK(registry.loadFromFile(path, fs));
    CHECK(registry.definitions().size() == 1);
    CHECK(registry.definitions()[0].command == "docker");
    CHECK(registry.definitions()[0].variants.size() == 2);
}

TEST_CASE("OutputDefinitionRegistry.load_from_directory")
{
    auto const& fs = NativeFileSystem::instance();
    OutputDefinitionRegistry registry;
    registry.loadFromDirectory(DefinitionsDir, fs);
    CHECK(registry.definitions().size() >= 2); // docker + git
}

TEST_CASE("OutputDefinitionRegistry.match_exact_args")
{
    auto const& fs = NativeFileSystem::instance();
    OutputDefinitionRegistry registry;
    registry.loadFromDirectory(DefinitionsDir, fs);

    auto const* match = registry.findMatch("docker", { "ps" });
    REQUIRE(match != nullptr);
    CHECK(match->name == "ps");
}

TEST_CASE("OutputDefinitionRegistry.match_with_extra_args")
{
    auto const& fs = NativeFileSystem::instance();
    OutputDefinitionRegistry registry;
    registry.loadFromDirectory(DefinitionsDir, fs);

    auto const* match = registry.findMatch("docker", { "ps", "-a" });
    REQUIRE(match != nullptr);
    CHECK(match->name == "ps");
}

TEST_CASE("OutputDefinitionRegistry.no_match")
{
    auto const& fs = NativeFileSystem::instance();
    OutputDefinitionRegistry registry;
    registry.loadFromDirectory(DefinitionsDir, fs);

    auto const* match = registry.findMatch("docker", { "run" });
    CHECK(match == nullptr);
}

TEST_CASE("OutputDefinitionRegistry.git_log_match")
{
    auto const& fs = NativeFileSystem::instance();
    OutputDefinitionRegistry registry;
    registry.loadFromDirectory(DefinitionsDir, fs);

    auto const* match = registry.findMatch("git", { "log" });
    REQUIRE(match != nullptr);
    CHECK(match->name == "log");
    CHECK(match->parser.type == ParserConfig::Type::Fields);
}

TEST_CASE("OutputDefinitionRegistry.git_status_match")
{
    auto const& fs = NativeFileSystem::instance();
    OutputDefinitionRegistry registry;
    registry.loadFromDirectory(DefinitionsDir, fs);

    auto const* match = registry.findMatch("git", { "status" });
    REQUIRE(match != nullptr);
    CHECK(match->name == "status");
    CHECK(match->parser.maxFields.has_value());
    CHECK(*match->parser.maxFields == 2);
}

TEST_CASE("OutputDefinitionRegistry.docker_images_match")
{
    auto const& fs = NativeFileSystem::instance();
    OutputDefinitionRegistry registry;
    registry.loadFromDirectory(DefinitionsDir, fs);

    auto const* match = registry.findMatch("docker", { "images" });
    REQUIRE(match != nullptr);
    CHECK(match->name == "images");
    CHECK(match->schema.size() == 5);
}

TEST_CASE("OutputDefinitionRegistry.all_variants")
{
    auto const& fs = NativeFileSystem::instance();
    OutputDefinitionRegistry registry;
    registry.loadFromDirectory(DefinitionsDir, fs);

    auto const variants = registry.allVariants();
    CHECK(variants.size() >= 4); // docker ps, docker images, git log, git status
}

TEST_CASE("OutputDefinitionRegistry.record_type_name_derived")
{
    auto const& fs = NativeFileSystem::instance();
    OutputDefinitionRegistry registry;
    registry.loadFromDirectory(DefinitionsDir, fs);

    auto const* match = registry.findMatch("docker", { "ps" });
    REQUIRE(match != nullptr);
    CHECK(match->recordTypeName == "DockerPsRecord");
}

TEST_CASE("OutputDefinitionRegistry.nonexistent_directory")
{
    auto const& fs = NativeFileSystem::instance();
    OutputDefinitionRegistry registry;
    // Should not crash or throw on nonexistent directory
    registry.loadFromDirectory("/nonexistent/path/that/does/not/exist", fs);
    CHECK(registry.definitions().empty());
}
