// SPDX-License-Identifier: Apache-2.0
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include <platform/testing/MockFileInfoProvider.hpp>

#if !defined(_WIN32)
    #include <platform/linux/LinuxFileInfoProvider.hpp>
#endif

using namespace endo::platform;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helper: RAII temp directory with known files for filesystem tests.
// ---------------------------------------------------------------------------
struct TempDir
{
    fs::path path;

    TempDir()
    {
        path = fs::temp_directory_path() / "endo_ls_test";
        fs::create_directories(path);
    }

    void createFile(std::string const& name, std::string const& content = "") const
    {
        std::ofstream ofs(path / name);
        ofs << content;
    }

    void createSubdir(std::string const& name) const { fs::create_directories(path / name); }

    ~TempDir() { fs::remove_all(path); }
};

// ===========================================================================
// MockFileInfoProvider tests
// ===========================================================================

TEST_CASE("MockFileInfoProvider.directory_lookup", "[platform][mock]")
{
    testing::MockFileInfoProvider mock;
    mock.setEntries(".",
                    { { .name = "a.txt", .size = 10, .mode = 0644, .mtime = 100, .isDir = false },
                      { .name = "b.md", .size = 20, .mode = 0644, .mtime = 200, .isDir = false } });

    auto entries = mock.listDirectory(".");
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].name == "a.txt");
    CHECK(entries[1].name == "b.md");
}

TEST_CASE("MockFileInfoProvider.single_file_lookup", "[platform][mock]")
{
    testing::MockFileInfoProvider mock;
    mock.setFileEntry("a.txt", { .name = "a.txt", .size = 10, .mode = 0644, .mtime = 100, .isDir = false });

    auto entries = mock.listDirectory("a.txt");
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].name == "a.txt");
    CHECK(entries[0].size == 10);
}

TEST_CASE("MockFileInfoProvider.glob_filtering", "[platform][mock]")
{
    testing::MockFileInfoProvider mock;
    mock.setEntries(".",
                    { { .name = "a.txt", .size = 10, .mode = 0644, .mtime = 100, .isDir = false },
                      { .name = "b.txt", .size = 20, .mode = 0644, .mtime = 200, .isDir = false },
                      { .name = "c.md", .size = 30, .mode = 0644, .mtime = 300, .isDir = false } });

    auto entries = mock.listDirectory("*.txt");
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].name == "a.txt");
    CHECK(entries[1].name == "b.txt");
}

TEST_CASE("MockFileInfoProvider.glob_no_match", "[platform][mock]")
{
    testing::MockFileInfoProvider mock;
    mock.setEntries(".", { { .name = "a.txt", .size = 10, .mode = 0644, .mtime = 100, .isDir = false } });

    auto entries = mock.listDirectory("*.xyz");
    CHECK(entries.empty());
}

TEST_CASE("MockFileInfoProvider.glob_with_directory_prefix", "[platform][mock]")
{
    testing::MockFileInfoProvider mock;
    mock.setEntries("subdir",
                    { { .name = "x.txt", .size = 5, .mode = 0644, .mtime = 100, .isDir = false },
                      { .name = "y.md", .size = 15, .mode = 0644, .mtime = 200, .isDir = false } });

    auto entries = mock.listDirectory("subdir/*.txt");
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].name == "x.txt");
}

TEST_CASE("MockFileInfoProvider.nonexistent_returns_empty", "[platform][mock]")
{
    testing::MockFileInfoProvider mock;
    auto entries = mock.listDirectory("/no/such/path");
    CHECK(entries.empty());
}

// ===========================================================================
// LinuxFileInfoProvider tests (real filesystem, Linux only)
// ===========================================================================

#if !defined(_WIN32)

TEST_CASE("LinuxFileInfoProvider.listDirectory_directory", "[platform][linux]")
{
    TempDir tmp;
    tmp.createFile("alpha.txt", "hello");
    tmp.createFile("beta.md", "world");
    tmp.createSubdir("gamma");

    LinuxFileInfoProvider provider;
    auto entries = provider.listDirectory(tmp.path.string());

    REQUIRE(entries.size() == 3);
    // Sorted by name
    CHECK(entries[0].name == "alpha.txt");
    CHECK(entries[1].name == "beta.md");
    CHECK(entries[2].name == "gamma");
    CHECK(entries[2].isDir == true);
}

TEST_CASE("LinuxFileInfoProvider.listDirectory_single_file", "[platform][linux]")
{
    TempDir tmp;
    tmp.createFile("target.txt", "content");

    LinuxFileInfoProvider provider;
    auto entries = provider.listDirectory((tmp.path / "target.txt").string());

    REQUIRE(entries.size() == 1);
    CHECK(entries[0].name == "target.txt");
    CHECK(entries[0].size == 7); // "content" is 7 bytes
    CHECK(entries[0].isDir == false);
}

TEST_CASE("LinuxFileInfoProvider.listDirectory_single_directory_entry", "[platform][linux]")
{
    TempDir tmp;
    tmp.createSubdir("mydir");

    LinuxFileInfoProvider provider;
    // When path points to a directory, it lists contents (even if empty).
    auto entries = provider.listDirectory((tmp.path / "mydir").string());
    CHECK(entries.empty()); // empty directory
}

TEST_CASE("LinuxFileInfoProvider.listDirectory_glob_pattern", "[platform][linux]")
{
    TempDir tmp;
    tmp.createFile("a.txt", "aaa");
    tmp.createFile("b.txt", "bbb");
    tmp.createFile("c.md", "ccc");
    tmp.createSubdir("d_dir");

    LinuxFileInfoProvider provider;
    auto entries = provider.listDirectory((tmp.path / "*.txt").string());

    REQUIRE(entries.size() == 2);
    CHECK(entries[0].name == "a.txt");
    CHECK(entries[1].name == "b.txt");
}

TEST_CASE("LinuxFileInfoProvider.listDirectory_glob_question_mark", "[platform][linux]")
{
    TempDir tmp;
    tmp.createFile("ab.txt");
    tmp.createFile("cd.txt");
    tmp.createFile("abc.txt");

    LinuxFileInfoProvider provider;
    auto entries = provider.listDirectory((tmp.path / "??.txt").string());

    REQUIRE(entries.size() == 2);
    CHECK(entries[0].name == "ab.txt");
    CHECK(entries[1].name == "cd.txt");
}

TEST_CASE("LinuxFileInfoProvider.listDirectory_glob_no_match", "[platform][linux]")
{
    TempDir tmp;
    tmp.createFile("a.txt");

    LinuxFileInfoProvider provider;
    auto entries = provider.listDirectory((tmp.path / "*.xyz").string());
    CHECK(entries.empty());
}

TEST_CASE("LinuxFileInfoProvider.listDirectory_lists_dangling_symlink", "[platform][linux]")
{
    // A dangling symlink fails to resolve via status() (which follows). It must still be
    // listed via the symlink_status() fallback — mirroring how the Windows provider keeps
    // App Execution Aliases (winget) visible despite their reparse points being unfollowable.
    TempDir tmp;
    tmp.createFile("real.txt", "data");
    fs::create_symlink(tmp.path / "missing_target", tmp.path / "broken.lnk");

    LinuxFileInfoProvider provider;
    auto entries = provider.listDirectory(tmp.path.string());

    REQUIRE(entries.size() == 2);
    CHECK(entries[0].name == "broken.lnk");
    CHECK(entries[0].isDir == false);
    CHECK(entries[1].name == "real.txt");
}

TEST_CASE("LinuxFileInfoProvider.populates_stat_metadata", "[platform][linux]")
{
    // The lstat-based provider must fill the disk-usage / identity fields
    // (blocks, dev, ino) for real files, not just apparent size.
    TempDir tmp;
    tmp.createFile("data.bin", std::string(8192, 'x')); // 8 KiB => several 512B blocks

    LinuxFileInfoProvider provider;
    auto entries = provider.listDirectory((tmp.path / "data.bin").string());

    REQUIRE(entries.size() == 1);
    auto const& e = entries[0];
    CHECK(e.size == 8192);
    CHECK(e.blocks > 0);     // allocated blocks reported (st_blocks)
    CHECK(e.dev != 0);       // device id reported (st_dev)
    CHECK(e.ino != 0);       // inode reported (st_ino)
    CHECK(e.isSymlink == false);
    CHECK(e.isDir == false);
}

TEST_CASE("LinuxFileInfoProvider.marks_symlink", "[platform][linux]")
{
    // A symlink must be flagged isSymlink and never reported as a directory,
    // even when it points at one (it is not followed).
    TempDir tmp;
    tmp.createSubdir("realdir");
    fs::create_symlink(tmp.path / "realdir", tmp.path / "link_to_dir");

    LinuxFileInfoProvider provider;
    auto entries = provider.listDirectory(tmp.path.string());

    REQUIRE(entries.size() == 2);
    // Sorted by name: "link_to_dir" < "realdir"
    CHECK(entries[0].name == "link_to_dir");
    CHECK(entries[0].isSymlink == true);
    CHECK(entries[0].isDir == false); // not followed -> not a directory
    CHECK(entries[1].name == "realdir");
    CHECK(entries[1].isSymlink == false);
    CHECK(entries[1].isDir == true);
}

TEST_CASE("LinuxFileInfoProvider.siblings_share_device", "[platform][linux]")
{
    // Two entries in the same directory live on the same filesystem, so their
    // st_dev must match — the invariant cross-device detection relies on.
    TempDir tmp;
    tmp.createFile("a.txt", "a");
    tmp.createFile("b.txt", "b");

    LinuxFileInfoProvider provider;
    auto entries = provider.listDirectory(tmp.path.string());

    REQUIRE(entries.size() == 2);
    CHECK(entries[0].dev == entries[1].dev);
    CHECK(entries[0].ino != entries[1].ino); // distinct files -> distinct inodes
}

TEST_CASE("LinuxFileInfoProvider.listDirectory_nonexistent", "[platform][linux]")
{
    LinuxFileInfoProvider provider;
    auto entries = provider.listDirectory("/nonexistent/path/that/does/not/exist");
    CHECK(entries.empty());
}

TEST_CASE("LinuxFileInfoProvider.listDirectory_glob_nonexistent_pattern", "[platform][linux]")
{
    LinuxFileInfoProvider provider;
    auto entries = provider.listDirectory("*.surely_nonexistent_ext_xyz");
    // If CWD has no such files, should be empty.
    CHECK(entries.empty());
}

#endif // !_WIN32
