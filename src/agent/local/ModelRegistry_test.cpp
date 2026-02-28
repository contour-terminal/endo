// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <agent/local/ModelRegistry.hpp>

using namespace endo::agent::local;

namespace
{

class TempDir
{
  public:
    TempDir(): _path(std::filesystem::temp_directory_path() / "endo_test_model_registry")
    {
        std::filesystem::remove_all(_path);
        std::filesystem::create_directories(_path);
    }

    ~TempDir() { std::filesystem::remove_all(_path); }

    [[nodiscard]] auto path() const -> std::filesystem::path const& { return _path; }

    void createFile(std::string_view name, size_t sizeBytes = 0) const
    {
        auto file = std::ofstream(_path / name, std::ios::binary);
        if (sizeBytes > 0)
        {
            auto const data = std::string(sizeBytes, '\0');
            file.write(data.data(), static_cast<std::streamsize>(data.size()));
        }
    }

  private:
    std::filesystem::path _path;
};

} // namespace

TEST_CASE("agent.local.model_registry.curated_models_non_empty", "[agent][local]")
{
    auto const models = curatedModels();
    REQUIRE_FALSE(models.empty());
    CHECK(models.size() == 4);
}

TEST_CASE("agent.local.model_registry.curated_models_have_variants", "[agent][local]")
{
    for (auto const& model: curatedModels())
    {
        CHECK_FALSE(model.name.empty());
        CHECK_FALSE(model.displayName.empty());
        CHECK_FALSE(model.description.empty());
        CHECK_FALSE(model.architecture.empty());
        CHECK(model.parameterCount > 0);
        CHECK_FALSE(model.variants.empty());

        for (auto const& variant: model.variants)
        {
            CHECK_FALSE(variant.quantization.empty());
            CHECK_FALSE(variant.url.empty());
            CHECK(variant.fileSizeBytes > 0);
            CHECK(variant.ramRequired > 0);
            CHECK_FALSE(variant.filename.empty());
        }
    }
}

TEST_CASE("agent.local.model_registry.find_exact_match", "[agent][local]")
{
    auto const* model = findCuratedModel("qwen2.5-coder-7b");
    REQUIRE(model != nullptr);
    CHECK(model->name == "qwen2.5-coder-7b");
    CHECK(model->displayName == "Qwen 2.5 Coder 7B");
}

TEST_CASE("agent.local.model_registry.find_substring_match", "[agent][local]")
{
    auto const* model = findCuratedModel("coder-7b");
    REQUIRE(model != nullptr);
    CHECK(model->name == "qwen2.5-coder-7b");
}

TEST_CASE("agent.local.model_registry.find_case_insensitive", "[agent][local]")
{
    auto const* model = findCuratedModel("QWEN2.5-CODER-7B");
    REQUIRE(model != nullptr);
    CHECK(model->name == "qwen2.5-coder-7b");
}

TEST_CASE("agent.local.model_registry.find_no_match", "[agent][local]")
{
    auto const* model = findCuratedModel("nonexistent-model-xyz");
    CHECK(model == nullptr);
}

TEST_CASE("agent.local.model_registry.model_storage_dir_non_empty", "[agent][local]")
{
    auto const dir = modelStorageDir();
    CHECK_FALSE(dir.empty());
}

TEST_CASE("agent.local.model_registry.discover_empty_dir", "[agent][local]")
{
    auto const dir = TempDir {};
    auto const models = discoverLocalModels(dir.path());
    CHECK(models.empty());
}

TEST_CASE("agent.local.model_registry.discover_finds_gguf_files", "[agent][local]")
{
    auto const dir = TempDir {};
    dir.createFile("model-a.gguf", 100);
    dir.createFile("model-b.gguf", 200);
    dir.createFile("readme.txt", 50);
    dir.createFile("weights.bin", 300);

    auto const models = discoverLocalModels(dir.path());
    REQUIRE(models.size() == 2);

    // Should be sorted by filename
    CHECK(models[0].filename == "model-a.gguf");
    CHECK(models[1].filename == "model-b.gguf");
    CHECK(models[0].fileSizeBytes == 100);
    CHECK(models[1].fileSizeBytes == 200);
}

TEST_CASE("agent.local.model_registry.discover_nonexistent_dir", "[agent][local]")
{
    auto const models = discoverLocalModels("/nonexistent/directory/xyz");
    CHECK(models.empty());
}

TEST_CASE("agent.local.model_registry.format_bytes_values", "[agent][local]")
{
    CHECK(formatBytes(0) == "0 bytes");
    CHECK(formatBytes(512) == "512 bytes");
    CHECK(formatBytes(1023) == "1023 bytes");
    CHECK(formatBytes(1024) == "1.0 KB");
    CHECK(formatBytes(1536) == "1.5 KB");
    CHECK(formatBytes(1048576) == "1.0 MB");
    CHECK(formatBytes(1572864) == "1.5 MB");
    CHECK(formatBytes(1073741824) == "1.0 GB");
    CHECK(formatBytes(4'700'000'000) == "4.4 GB");
}
