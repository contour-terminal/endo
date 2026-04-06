// SPDX-License-Identifier: Apache-2.0
#include <endo-language/TestHelper.hpp>
#include <endo-language/ast/AST.hpp>
#include <endo-language/ast/ASTPrinter.hpp>
#include <endo-language/codegen/IRGenerator.hpp>
#include <endo-language/module/ModuleLoader.hpp>
#include <endo-language/module/ModuleSignature.hpp>
#include <endo-language/parser/Parser.hpp>

#include <CoreVM/CoreVM.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>

#include <platform/NativeFileSystem.hpp>

using namespace endo;
using namespace endo::test;

// =============================================================================
// Parser Tests (AST node inspection — cannot be expressed as .endo tests)
// =============================================================================

TEST_CASE("module.parser.import", "[module][parser]")
{
    SECTION("simple import")
    {
        auto ast = parse("import Math");
        REQUIRE(ast != nullptr);
        auto const* compound = dynamic_cast<ast::CompoundStmt const*>(ast.get());
        REQUIRE(compound != nullptr);
        REQUIRE(compound->statements.size() == 1);
        auto const* importStmt = dynamic_cast<ast::ImportStmt const*>(compound->statements[0].get());
        REQUIRE(importStmt != nullptr);
        CHECK(importStmt->modulePath == "Math");
    }

    SECTION("dotted import")
    {
        auto ast = parse("import Geometry.Circle");
        REQUIRE(ast != nullptr);
        auto const* compound = dynamic_cast<ast::CompoundStmt const*>(ast.get());
        REQUIRE(compound != nullptr);
        REQUIRE(compound->statements.size() == 1);
        auto const* importStmt = dynamic_cast<ast::ImportStmt const*>(compound->statements[0].get());
        REQUIRE(importStmt != nullptr);
        CHECK(importStmt->modulePath == "Geometry.Circle");
    }

    SECTION("lowercase import falls through to shell")
    {
        // "import requests" should be treated as a shell command (lowercase)
        auto ast = parse("import requests");
        REQUIRE(ast != nullptr);
        auto const* compound = dynamic_cast<ast::CompoundStmt const*>(ast.get());
        REQUIRE(compound != nullptr);
        // Should be a ProgramCall, not ImportStmt
        auto const* importStmt = dynamic_cast<ast::ImportStmt const*>(compound->statements[0].get());
        CHECK(importStmt == nullptr); // Not an import — should be shell command
    }
}

TEST_CASE("module.parser.open", "[module][parser]")
{
    SECTION("simple open")
    {
        auto ast = parse("open Math");
        REQUIRE(ast != nullptr);
        auto const* compound = dynamic_cast<ast::CompoundStmt const*>(ast.get());
        REQUIRE(compound != nullptr);
        REQUIRE(compound->statements.size() == 1);
        auto const* openStmt = dynamic_cast<ast::OpenStmt const*>(compound->statements[0].get());
        REQUIRE(openStmt != nullptr);
        CHECK(openStmt->modulePath == "Math");
        CHECK(openStmt->selectiveNames.empty());
    }

    SECTION("selective open")
    {
        auto ast = parse("open Math with (square, cube)");
        REQUIRE(ast != nullptr);
        auto const* compound = dynamic_cast<ast::CompoundStmt const*>(ast.get());
        REQUIRE(compound != nullptr);
        REQUIRE(compound->statements.size() == 1);
        auto const* openStmt = dynamic_cast<ast::OpenStmt const*>(compound->statements[0].get());
        REQUIRE(openStmt != nullptr);
        CHECK(openStmt->modulePath == "Math");
        REQUIRE(openStmt->selectiveNames.size() == 2);
        CHECK(openStmt->selectiveNames[0] == "square");
        CHECK(openStmt->selectiveNames[1] == "cube");
    }

    SECTION("lowercase open falls through to shell")
    {
        // "open file.txt" should be treated as a shell command
        auto ast = parse("open file.txt");
        REQUIRE(ast != nullptr);
        auto const* compound = dynamic_cast<ast::CompoundStmt const*>(ast.get());
        REQUIRE(compound != nullptr);
        auto const* openStmt = dynamic_cast<ast::OpenStmt const*>(compound->statements[0].get());
        CHECK(openStmt == nullptr);
    }
}

TEST_CASE("module.parser.module_decl", "[module][parser]")
{
    SECTION("simple inline module")
    {
        auto ast = parse(R"(
module Helpers =
    let double x = x * 2
)");
        REQUIRE(ast != nullptr);
        auto const* compound = dynamic_cast<ast::CompoundStmt const*>(ast.get());
        REQUIRE(compound != nullptr);
        REQUIRE_FALSE(compound->statements.empty());
        auto const* modDecl = dynamic_cast<ast::ModuleDeclStmt const*>(compound->statements[0].get());
        REQUIRE(modDecl != nullptr);
        CHECK(modDecl->name == "Helpers");
        CHECK_FALSE(modDecl->body.empty());
    }

    SECTION("lowercase module falls through to shell")
    {
        // "module load gcc" should be treated as a shell command
        auto ast = parse("module load gcc");
        REQUIRE(ast != nullptr);
        auto const* compound = dynamic_cast<ast::CompoundStmt const*>(ast.get());
        REQUIRE(compound != nullptr);
        auto const* modDecl = dynamic_cast<ast::ModuleDeclStmt const*>(compound->statements[0].get());
        CHECK(modDecl == nullptr);
    }
}

TEST_CASE("module.parser.ast_printer", "[module][parser]")
{
    SECTION("import round-trips")
    {
        auto printed = parseAndPrintAST("import Math");
        CHECK(printed == "import Math");
    }

    SECTION("open round-trips")
    {
        auto printed = parseAndPrintAST("open Math");
        CHECK(printed == "open Math");
    }

    SECTION("selective open round-trips")
    {
        auto printed = parseAndPrintAST("open Math with (square, cube)");
        CHECK(printed == "open Math with (square, cube)");
    }
}

TEST_CASE("module.parser.let_private", "[module][parser]")
{
    SECTION("parses let private")
    {
        auto ast = parse("let private helper x = x + 1");
        REQUIRE(ast != nullptr);
        auto const* compound = dynamic_cast<ast::CompoundStmt const*>(ast.get());
        REQUIRE(compound != nullptr);
        REQUIRE(compound->statements.size() == 1);
        auto const* letStmt = dynamic_cast<ast::LetBindingStmt const*>(compound->statements[0].get());
        REQUIRE(letStmt != nullptr);
        CHECK(letStmt->visibility == ast::Visibility::Private);
        CHECK(letStmt->name == "helper");
    }
}

TEST_CASE("module.parser.let_private_ast_printer", "[module][parser]")
{
    auto printed = parseAndPrintAST("let private secret x = x * 2");
    CHECK(printed == "let private secret x = (x * 2)");
}

// =============================================================================
// Module Loader Tests (internal API — pointer identity, resolution paths)
// =============================================================================

namespace
{
/// RAII helper for creating temporary module files for testing.
struct TempModuleDir
{
    std::filesystem::path dir;

    TempModuleDir():
        dir(std::filesystem::temp_directory_path()
            / ("endo_module_test_"
               + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
    {
        std::filesystem::create_directories(dir);
    }

    ~TempModuleDir() { std::filesystem::remove_all(dir); }

    void writeModule(std::string const& name, std::string const& content) const
    {
        auto path = dir / (name + ".endo");
        std::ofstream(path) << content;
    }

    void writeNestedModule(std::string const& parent,
                           std::string const& name,
                           std::string const& content) const
    {
        auto parentDir = dir / parent;
        std::filesystem::create_directories(parentDir);
        auto path = parentDir / (name + ".endo");
        std::ofstream(path) << content;
    }
};
} // namespace

TEST_CASE("module.loader.resolution", "[module][loader]")
{
    auto& rt = TestRuntime::instance();
    rt.clearErrors();

    TempModuleDir tmpDir;

    SECTION("resolves simple module")
    {
        tmpDir.writeModule("Math", "let square (x: int) : int = x * x");
        ModuleLoader loader(rt.runtime, rt.report, NativeFileSystem::instance());
        loader.addSearchPath(tmpDir.dir);

        auto resolved = loader.resolveModulePath("Math", std::nullopt);
        REQUIRE(resolved.has_value());
        CHECK(resolved->filename() == "Math.endo");
    }

    SECTION("resolves nested module")
    {
        tmpDir.writeNestedModule("Geometry", "Circle", "let area (r: int) : int = r * r * 3");
        ModuleLoader loader(rt.runtime, rt.report, NativeFileSystem::instance());
        loader.addSearchPath(tmpDir.dir);

        auto resolved = loader.resolveModulePath("Geometry.Circle", std::nullopt);
        REQUIRE(resolved.has_value());
        CHECK(resolved->filename() == "Circle.endo");
    }

    SECTION("returns nullopt for missing module")
    {
        ModuleLoader loader(rt.runtime, rt.report, NativeFileSystem::instance());
        loader.addSearchPath(tmpDir.dir);

        auto resolved = loader.resolveModulePath("NonExistent", std::nullopt);
        CHECK(!resolved.has_value());
    }
}

TEST_CASE("module.loader.caching", "[module][loader]")
{
    auto& rt = TestRuntime::instance();
    rt.clearErrors();

    TempModuleDir tmpDir;
    tmpDir.writeModule("Math", "let square (x: int) : int = x * x");

    ModuleLoader loader(rt.runtime, rt.report, NativeFileSystem::instance());
    loader.addSearchPath(tmpDir.dir);

    SECTION("import-once: second load returns same descriptor")
    {
        auto const* first = loader.loadModule("Math");
        REQUIRE(first != nullptr);
        auto const* second = loader.loadModule("Math");
        CHECK(first == second); // Same pointer — cached
    }
}

TEST_CASE("module.loader.available_modules", "[module][loader]")
{
    auto& rt = TestRuntime::instance();
    rt.clearErrors();

    TempModuleDir tmpDir;
    tmpDir.writeModule("Math", "let x = 1");
    tmpDir.writeModule("Utils", "let y = 2");
    tmpDir.writeModule("lowercase", "let z = 3"); // Not PascalCase — should be excluded

    ModuleLoader loader(rt.runtime, rt.report, NativeFileSystem::instance());
    loader.addSearchPath(tmpDir.dir);

    auto names = loader.availableModuleNames();
    CHECK(std::ranges::find(names, "Math") != names.end());
    CHECK(std::ranges::find(names, "Utils") != names.end());
    CHECK(std::ranges::find(names, "lowercase") == names.end());
}

TEST_CASE("module.loader.available_nested_modules", "[module][loader]")
{
    auto& rt = TestRuntime::instance();
    rt.clearErrors();

    TempModuleDir tmpDir;
    tmpDir.writeModule("Math", "let x = 1");
    tmpDir.writeNestedModule("Geometry", "Circle", "let area (r: int) : int = r * r * 3");

    ModuleLoader loader(rt.runtime, rt.report, NativeFileSystem::instance());
    loader.addSearchPath(tmpDir.dir);

    auto names = loader.availableModuleNames();
    CHECK(std::ranges::find(names, "Math") != names.end());
    CHECK(std::ranges::find(names, "Geometry.Circle") != names.end());
}

// =============================================================================
// File module private enforcement (internal API — descriptor inspection)
// =============================================================================

TEST_CASE("module.file.private_enforcement", "[module][codegen]")
{
    auto& rt = TestRuntime::instance();
    rt.clearErrors();

    TempModuleDir tmpDir;
    tmpDir.writeModule("Priv",
                       "let private helper (x: int) : int = x + 1\n"
                       "let public_fn (x: int) : int = helper x");

    FSharpPersistentState state;
    state.moduleLoader = std::make_shared<ModuleLoader>(rt.runtime, rt.report, NativeFileSystem::instance());
    state.moduleLoader->addSearchPath(tmpDir.dir);

    SECTION("file module private names are extracted")
    {
        auto const* desc = state.moduleLoader->loadModule("Priv");
        REQUIRE(desc != nullptr);
        CHECK(desc->isPrivate("helper"));
        CHECK(desc->isPublic("public_fn"));
    }
}

// =============================================================================
// Module Signature Tests (internal API — struct parsing, validation)
// =============================================================================

TEST_CASE("module.signature.parse", "[module][signature]")
{
    TempModuleDir tmpDir;

    SECTION("parses val declarations")
    {
        auto sigPath = tmpDir.dir / "Math.endoi";
        {
            std::ofstream(sigPath) << "val square : int -> int\n"
                                   << "val pi : float\n";
        }
        auto sig = parseModuleSignature(sigPath);
        REQUIRE(sig.has_value());
        CHECK(sig->moduleName == "Math");
        REQUIRE(sig->entries.size() == 2);
        CHECK(sig->entries[0].kind == SignatureEntry::Kind::Val);
        CHECK(sig->entries[0].name == "square");
        CHECK(sig->entries[0].signature == "int -> int");
        CHECK(sig->entries[1].name == "pi");
    }

    SECTION("parses type declarations")
    {
        auto sigPath = tmpDir.dir / "Geom.endoi";
        {
            std::ofstream(sigPath) << "type Point = { x: float; y: float }\n";
        }
        auto sig = parseModuleSignature(sigPath);
        REQUIRE(sig.has_value());
        REQUIRE(sig->entries.size() == 1);
        CHECK(sig->entries[0].kind == SignatureEntry::Kind::Type);
        CHECK(sig->entries[0].name == "Point");
    }

    SECTION("skips comments and blank lines")
    {
        auto sigPath = tmpDir.dir / "Lib.endoi";
        {
            std::ofstream(sigPath) << "// This is a comment\n"
                                   << "\n"
                                   << "val f : int -> int\n";
        }
        auto sig = parseModuleSignature(sigPath);
        REQUIRE(sig.has_value());
        CHECK(sig->entries.size() == 1);
    }
}

TEST_CASE("module.signature.validation", "[module][signature]")
{
    SECTION("matching signature validates successfully")
    {
        ModuleDescriptor desc;
        desc.name = "Math";
        desc.functions["square"] = {};

        ModuleSignature sig;
        sig.moduleName = "Math";
        sig.entries.push_back({ SignatureEntry::Kind::Val, "square", "int -> int" });

        auto errors = validateSignature(desc, sig);
        CHECK(errors.empty());
    }

    SECTION("missing function reports error")
    {
        ModuleDescriptor desc;
        desc.name = "Math";

        ModuleSignature sig;
        sig.moduleName = "Math";
        sig.entries.push_back({ SignatureEntry::Kind::Val, "square", "int -> int" });

        auto errors = validateSignature(desc, sig);
        REQUIRE(errors.size() == 1);
        CHECK(errors[0].find("missing") != std::string::npos);
        CHECK(errors[0].find("square") != std::string::npos);
    }
}

TEST_CASE("module.signature.malformed_warning", "[module][signature]")
{
    TempModuleDir tmpDir;

    SECTION("malformed val line produces warning")
    {
        auto sigPath = tmpDir.dir / "Bad.endoi";
        {
            std::ofstream(sigPath) << "val missing_colon int -> int\n"
                                   << "val good : int -> int\n";
        }
        auto sig = parseModuleSignature(sigPath);
        REQUIRE(sig.has_value());
        CHECK(sig->entries.size() == 1); // Only the good entry
        CHECK(sig->warnings.size() == 1);
        CHECK(sig->warnings[0].find("missing") != std::string::npos);
    }
}

TEST_CASE("module.signature.validation_mismatch", "[module][signature]")
{
    SECTION("missing type reports error")
    {
        ModuleDescriptor desc;
        desc.name = "Geom";
        // No types defined

        ModuleSignature sig;
        sig.moduleName = "Geom";
        sig.entries.push_back({ SignatureEntry::Kind::Type, "Point", "{ x: int; y: int }" });

        auto errors = validateSignature(desc, sig);
        REQUIRE(errors.size() == 1);
        CHECK(errors[0].find("missing type") != std::string::npos);
        CHECK(errors[0].find("Point") != std::string::npos);
    }

    SECTION("multiple missing entries report multiple errors")
    {
        ModuleDescriptor desc;
        desc.name = "Math";

        ModuleSignature sig;
        sig.moduleName = "Math";
        sig.entries.push_back({ SignatureEntry::Kind::Val, "square", "int -> int" });
        sig.entries.push_back({ SignatureEntry::Kind::Val, "cube", "int -> int" });

        auto errors = validateSignature(desc, sig);
        CHECK(errors.size() == 2);
    }
}
