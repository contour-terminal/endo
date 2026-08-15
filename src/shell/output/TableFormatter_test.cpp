// SPDX-License-Identifier: Apache-2.0
#include <shell/output/TableFormatter.hpp>

#include <endo-language/builtins/BuiltinImpls.hpp>

#include <CoreVM/CoreVM.hpp>
#include <CoreVM/types/TypeRegistry.hpp>
#include <CoreVM/types/TypedObject.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

using namespace endo;

namespace
{

/// @brief One FileInfo record's worth of fixture data.
struct FileFixture
{
    std::string_view name;
    int64_t size = 0;
    int64_t mode = 0644;
    bool isDir = false;
    bool isSymlink = false;
    std::string_view target;
    std::string_view path;
};

/// @brief Owns a Runner and builds FileInfo lists from fixture rows.
///
/// formatRecordTable() reads real VM records, so a table test needs a live Runner. The Runner's
/// string arena and object pool also keep the allocated records alive for the test's duration.
class RecordBuilder
{
  public:
    RecordBuilder():
        _program(makeHostProgram()),
        _runner(_program.findFunction("host"),
                nullptr,
                &_globals,
                CoreVM::RuntimeConfig::defaultConfig(),
                nullptr)
    {
    }

    /// @brief Builds a cons-list of FileInfo records, in fixture order.
    [[nodiscard]] CoreVM::TypedObject* makeFileInfoList(std::span<FileFixture const> fixtures)
    {
        auto* list = _runner.makeNilList(CoreVM::LiteralType::Object);

        for (auto const& f: fixtures | std::views::reverse)
        {
            // Through the shared builder, so these fixtures exercise the same slot layout the
            // real producers write.
            auto* record = builtins::makeFileInfoRecord(&_runner,
                                                        { .name = f.name,
                                                          .path = f.path,
                                                          .symlinkTarget = f.target,
                                                          .size = f.size,
                                                          .mode = f.mode,
                                                          .mtime = 1700000000,
                                                          .isDir = f.isDir,
                                                          .isSymlink = f.isSymlink });
            // A fixture with no path is then nulled out deliberately: the builder never emits a
            // null slot, but a record built before FileInfo carried the field would read as one,
            // and the renderer has to survive it.
            if (f.path.empty())
                record->setSlot(7, uintptr_t { 0 });

            list =
                _runner.makeConsCell(reinterpret_cast<uintptr_t>(record), list, CoreVM::LiteralType::Object);
        }
        return list;
    }

    [[nodiscard]] CoreVM::Runner* runner() noexcept { return &_runner; }

  private:
    /// @brief Builds a minimal program, only so a Runner can be constructed.
    ///
    /// The Runner dereferences its function for the stack size and program pointer, so a null
    /// one is not an option. The body is never executed — this test only needs the Runner's
    /// object pool and string arena.
    [[nodiscard]] static CoreVM::Program makeHostProgram()
    {
        auto pool = CoreVM::ConstantPool {};
        pool.setFunction("host",
                         { CoreVM::makeInstruction(CoreVM::Opcode::ILOAD, 0),
                           CoreVM::makeInstruction(CoreVM::Opcode::URET) });
        return CoreVM::Program { std::move(pool) };
    }

    CoreVM::Runner::Globals _globals;
    CoreVM::Program _program;
    CoreVM::Runner _runner;
};

/// @brief Strips every SGR and OSC escape sequence, leaving only visible text.
///
/// Used to prove that adding escapes leaves column layout byte-identical.
[[nodiscard]] std::string stripEscapes(std::string_view text)
{
    auto out = std::string {};
    for (auto i = std::size_t { 0 }; i < text.size();)
    {
        if (text[i] != '\033')
        {
            out += text[i++];
            continue;
        }
        // CSI ... final-byte  (SGR and friends)
        if (i + 1 < text.size() && text[i + 1] == '[')
        {
            i += 2;
            while (i < text.size() && (text[i] < '@' || text[i] > '~'))
                ++i;
            if (i < text.size())
                ++i;
            continue;
        }
        // OSC ... ST  (hyperlinks)
        if (i + 1 < text.size() && text[i + 1] == ']')
        {
            i += 2;
            while (i < text.size() && !(text[i] == '\033' && i + 1 < text.size() && text[i + 1] == '\\'))
                ++i;
            i = std::min(text.size(), i + 2);
            continue;
        }
        ++i; // lone ESC
    }
    return out;
}

constexpr auto Fixtures = std::array {
    FileFixture { .name = "docs", .size = 4096, .mode = 0755, .isDir = true, .path = "/tmp/d/docs" },
    FileFixture { .name = "hello.txt", .size = 42, .path = "/tmp/d/hello.txt" },
};

constexpr auto SymlinkFixture = std::array {
    FileFixture {
        .name = "cfg", .size = 3, .isSymlink = true, .target = "/etc/real.conf", .path = "/tmp/d/cfg" },
};

[[nodiscard]] TableConfig linkedConfig()
{
    return TableConfig {
        .style = TableStyle::Bordered,
        .useColor = true,
        .showIcons = true,
        .useHyperlinks = true,
        .uriHost = "box",
    };
}

/// @brief Renders @p fixtures as a table under @p config.
///
/// The Runner only has to outlive the format call, so it lives and dies inside this helper —
/// every case below would otherwise repeat the same three lines to say so.
[[nodiscard]] std::string renderTable(std::span<FileFixture const> fixtures,
                                      TableConfig const& config = linkedConfig())
{
    auto builder = RecordBuilder {};
    return formatRecordTable(builder.makeFileInfoList(fixtures), builder.runner(), config);
}

} // namespace

TEST_CASE("TableFormatter.hyperlinks.emitted_for_each_file")
{
    auto const table = renderTable(Fixtures);

    CHECK(table.find("\033]8;;file://box/tmp/d/docs\033\\") != std::string::npos);
    CHECK(table.find("\033]8;;file://box/tmp/d/hello.txt\033\\") != std::string::npos);
    CHECK(table.find("\033]8;;\033\\") != std::string::npos); // close sequence
}

TEST_CASE("TableFormatter.hyperlinks.absent_when_disabled")
{
    auto config = linkedConfig();
    config.useHyperlinks = false;
    auto const table = renderTable(Fixtures, config);

    CHECK(table.find("]8;;") == std::string::npos);
    // Colors and icons are unaffected: the switch is independent of styling.
    CHECK(table.find("\033[") != std::string::npos);
}

TEST_CASE("TableFormatter.hyperlinks.absent_in_plain_style")
{
    // Plain is what a pipe or redirect gets; it must stay free of escape sequences even if a
    // caller sets useHyperlinks.
    auto config = linkedConfig();
    config.style = TableStyle::Plain;
    config.useColor = false;
    auto const table = renderTable(Fixtures, config);

    CHECK(table.find('\033') == std::string::npos);
}

TEST_CASE("TableFormatter.hyperlinks.preserve_column_alignment")
{
    // The invariant most likely to be broken by a later edit: escapes must not change layout.
    // Rendering with and without links and stripping all escapes must yield identical text.
    auto config = linkedConfig();
    auto const linked = renderTable(Fixtures, config);
    config.useHyperlinks = false;
    auto const unlinked = renderTable(Fixtures, config);

    CHECK(stripEscapes(linked) == stripEscapes(unlinked));
}

TEST_CASE("TableFormatter.hyperlinks.padding_is_outside_the_link")
{
    // Trailing filler must not be clickable, so the close sequence has to come before the pad.
    auto const table = renderTable(Fixtures);

    auto const open = table.find("\033]8;;file://box/tmp/d/docs\033\\");
    REQUIRE(open != std::string::npos);
    auto const close = table.find("\033]8;;\033\\", open + 1);
    REQUIRE(close != std::string::npos);

    // Everything between open and close is the icon, the name and its SGR framing — never a run
    // of padding spaces.
    auto const linked = stripEscapes(table.substr(open, close - open));
    CHECK(linked.find("  ") == std::string::npos);
}

TEST_CASE("TableFormatter.hyperlinks.missing_path_slot_is_not_linked")
{
    // A record built before FileInfo carried a path (or by a producer that forgot it) must
    // render normally rather than emitting a bogus link or crashing.
    constexpr auto Pathless = std::array {
        FileFixture { .name = "orphan.txt", .size = 7 },
    };
    auto const table = renderTable(Pathless);

    CHECK(table.find("]8;;") == std::string::npos);
    CHECK(table.find("orphan.txt") != std::string::npos);
}

TEST_CASE("TableFormatter.hyperlinks.truncated_name_keeps_full_uri")
{
    // The URI comes from the path slot, not from the rendered cell, so a name clipped to fit the
    // column still points at the right file.
    constexpr auto LongName = std::array {
        FileFixture { .name = "a-very-long-file-name-that-will-certainly-be-truncated.txt",
                      .size = 1,
                      .path = "/tmp/d/a-very-long-file-name-that-will-certainly-be-truncated.txt" },
    };
    auto config = linkedConfig();
    config.maxColumnWidth = 20;
    auto const table = renderTable(LongName, config);

    CHECK(table.find("file://box/tmp/d/a-very-long-file-name-that-will-certainly-be-truncated.txt")
          != std::string::npos);
    CHECK(table.find("…") != std::string::npos); // the name itself was ellipsised
}

TEST_CASE("TableFormatter.hyperlinks.symlink_row_links_the_link_itself")
{
    // The whole "name -> target" cell is one region pointing at the link, not its target.
    auto const table = renderTable(SymlinkFixture);

    CHECK(table.find("\033]8;;file://box/tmp/d/cfg\033\\") != std::string::npos);
    CHECK(table.find("file:///etc/real.conf") == std::string::npos);
}

TEST_CASE("TableFormatter.record_builder_fills_every_declared_field")
{
    // The guard the producers never had: nothing checked producer against registry, which is how
    // `find` shipped records whose isSymlink/target slots were never written and read back as a
    // null string. Now that one builder writes the layout, this asserts it covers all of it — add
    // a field to FileInfo without teaching the builder and this fails.
    RecordBuilder builder;
    auto* list = builder.makeFileInfoList(SymlinkFixture);
    auto* record = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(list->getSlot(0)));
    REQUIRE(record != nullptr);

    auto const* descriptor = CoreVM::builtinTypes().get(CoreVM::BuiltinTypeId::FileInfo);
    REQUIRE(descriptor != nullptr);
    REQUIRE_FALSE(descriptor->fields.empty());

    for (auto const& field: descriptor->fields)
    {
        INFO("field: FileInfo." << field.name);
        REQUIRE(field.offset < descriptor->slotCount);
        auto const slot = record->getSlot(field.offset);

        switch (field.type)
        {
            case CoreVM::LiteralType::String:
                // Never null: a null CoreString misbehaves on read. Empty is fine.
                CHECK(slot != 0);
                break;
            case CoreVM::LiteralType::Object: {
                REQUIRE(builder.runner()->isKnownObject(slot));
                auto const* nested =
                    reinterpret_cast<CoreVM::TypedObject const*>(static_cast<uintptr_t>(slot));
                auto const* expected = CoreVM::builtinTypes().getByName(field.nestedTypeName);
                REQUIRE(expected != nullptr);
                CHECK(nested->type->id == expected->id);
                break;
            }
            default:
                // Booleans and numbers carry no "unset" encoding to check.
                break;
        }
    }
}
