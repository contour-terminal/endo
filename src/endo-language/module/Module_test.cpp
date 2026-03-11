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

#include <filesystem>
#include <fstream>

using namespace endo;
using namespace endo::test;

// =============================================================================
// Parser Tests
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

// =============================================================================
// Module Loader Tests
// =============================================================================

namespace
{
/// RAII helper for creating temporary module files for testing.
struct TempModuleDir
{
    std::filesystem::path dir;

    TempModuleDir(): dir(std::filesystem::temp_directory_path() / "endo_module_test")
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
        ModuleLoader loader(rt.runtime, rt.report);
        loader.addSearchPath(tmpDir.dir);

        auto resolved = loader.resolveModulePath("Math", std::nullopt);
        REQUIRE(resolved.has_value());
        CHECK(resolved->filename() == "Math.endo");
    }

    SECTION("resolves nested module")
    {
        tmpDir.writeNestedModule("Geometry", "Circle", "let area (r: int) : int = r * r * 3");
        ModuleLoader loader(rt.runtime, rt.report);
        loader.addSearchPath(tmpDir.dir);

        auto resolved = loader.resolveModulePath("Geometry.Circle", std::nullopt);
        REQUIRE(resolved.has_value());
        CHECK(resolved->filename() == "Circle.endo");
    }

    SECTION("returns nullopt for missing module")
    {
        ModuleLoader loader(rt.runtime, rt.report);
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

    ModuleLoader loader(rt.runtime, rt.report);
    loader.addSearchPath(tmpDir.dir);

    SECTION("import-once: second load returns same descriptor")
    {
        auto const* first = loader.loadModule("Math");
        REQUIRE(first != nullptr);
        auto const* second = loader.loadModule("Math");
        CHECK(first == second); // Same pointer — cached
    }
}

TEST_CASE("module.loader.compilation", "[module][loader]")
{
    auto& rt = TestRuntime::instance();
    rt.clearErrors();

    TempModuleDir tmpDir;

    SECTION("compiles simple module with functions")
    {
        tmpDir.writeModule("Math", "let square (x: int) : int = x * x\nlet cube (x: int) : int = x * x * x");
        ModuleLoader loader(rt.runtime, rt.report);
        loader.addSearchPath(tmpDir.dir);

        auto const* desc = loader.loadModule("Math");
        REQUIRE(desc != nullptr);
        CHECK(desc->name == "Math");
        CHECK(desc->functions.contains("square"));
        CHECK(desc->functions.contains("cube"));
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

    ModuleLoader loader(rt.runtime, rt.report);
    loader.addSearchPath(tmpDir.dir);

    auto names = loader.availableModuleNames();
    CHECK(std::ranges::find(names, "Math") != names.end());
    CHECK(std::ranges::find(names, "Utils") != names.end());
    CHECK(std::ranges::find(names, "lowercase") == names.end());
}

// =============================================================================
// IRGenerator Integration Tests (inline modules)
// =============================================================================

TEST_CASE("module.inline.basic", "[module][codegen]")
{
    SECTION("inline module with qualified access")
    {
        auto output = executeSessionAndGetOutput({
            R"(
module Helpers =
    let double (x: int) : int = x * 2
print (Helpers.double 5)
)" });
        CHECK(output == "10");
    }
}

TEST_CASE("module.inline.multiple_functions", "[module][codegen]")
{
    SECTION("multiple functions in inline module")
    {
        auto output = executeSessionAndGetOutput({
            R"(
module Math =
    let square (x: int) : int = x * x
    let cube (x: int) : int = x * x * x
print (Math.square 4)
)" });
        CHECK(output == "16");
    }
}

TEST_CASE("module.inline.repl_persistence", "[module][codegen]")
{
    SECTION("inline module persists across REPL prompts")
    {
        auto output = executeSessionAndGetOutput({
            R"(
module Helpers =
    let double (x: int) : int = x * 2
)",
            "print (Helpers.double 7)" });
        CHECK(output == "14");
    }
}

TEST_CASE("module.inline.multiple_modules", "[module][codegen]")
{
    SECTION("two inline modules in one prompt")
    {
        auto output = executeSessionAndGetOutput({
            R"(
module A =
    let f (x: int) : int = x + 1
module B =
    let g (x: int) : int = x * 10
print (A.f (B.g 3))
)" });
        CHECK(output == "31");
    }
}

TEST_CASE("module.inline.qualified_call_chain", "[module][codegen]")
{
    SECTION("chained qualified calls from different modules")
    {
        auto output = executeSessionAndGetOutput({
            R"(
module Math =
    let square (x: int) : int = x * x
module Utils =
    let inc (x: int) : int = x + 1
print (Utils.inc (Math.square 5))
)" });
        CHECK(output == "26");
    }
}

// =============================================================================
// File-based module tests
// =============================================================================

TEST_CASE("module.file.import_and_call", "[module][codegen]")
{
    auto& rt = TestRuntime::instance();
    rt.clearErrors();

    TempModuleDir tmpDir;
    tmpDir.writeModule("Math", "let square (x: int) : int = x * x\nlet cube (x: int) : int = x * x * x");

    FSharpPersistentState state;
    state.moduleLoader = std::make_shared<ModuleLoader>(rt.runtime, rt.report);
    state.moduleLoader->addSearchPath(tmpDir.dir);

    SECTION("import and call via qualified access")
    {
        rt.clearOutput();
        auto const source = std::string("import Math\nprint (Math.square 6)");
        Parser parser(rt.runtime, rt.report, std::make_unique<StringSource>(source));
        auto ast = parser.parse();
        REQUIRE(ast != nullptr);

        auto ir = IRGenerator::generate(*ast, rt.report, rt.runtime, &state);
        REQUIRE(ir != nullptr);

        CoreVM::TargetCodeGenerator codegen;
        auto prog = codegen.generate(ir.get());
        REQUIRE(prog != nullptr);
        REQUIRE(prog->link(&rt.runtime, &rt.report));

        auto const* fn = prog->findFunction("@main");
        REQUIRE(fn != nullptr);
        CoreVM::Runner::Globals globals;
        CoreVM::Runner runner(fn, nullptr, &globals, CoreVM::RuntimeConfig::defaultConfig(), nullptr);
        runner.run();
        CHECK(rt.output() == "36");
    }
}

TEST_CASE("module.loader.circular_dependency", "[module][loader]")
{
    auto& rt = TestRuntime::instance();
    rt.clearErrors();

    TempModuleDir tmpDir;
    // A imports B, B imports A → circular
    tmpDir.writeModule("ModA", "import ModB\nlet f (x: int) : int = x");
    tmpDir.writeModule("ModB", "import ModA\nlet g (x: int) : int = x");

    ModuleLoader loader(rt.runtime, rt.report);
    loader.addSearchPath(tmpDir.dir);

    auto const* desc = loader.loadModule("ModA");
    // Should fail due to circular dependency (or return nullptr with error)
    // The exact behavior depends on how deep the loading goes before detecting the cycle
    CHECK((desc == nullptr || rt.hasErrors()));
}

// =============================================================================
// Private visibility tests
// =============================================================================

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

    SECTION("let export private is a parser error")
    {
        auto ast = parse("let export private x = 1");
        CHECK(ast == nullptr);
    }
}

TEST_CASE("module.parser.let_private_ast_printer", "[module][parser]")
{
    auto printed = parseAndPrintAST("let private secret x = x * 2");
    CHECK(printed == "let private secret x = (x * 2)");
}

TEST_CASE("module.inline.private_access_denied", "[module][codegen]")
{
    SECTION("private function cannot be called from outside module")
    {
        CHECK(generatesIRWithError(R"(
module Secret =
    let private helper (x: int) : int = x + 1
    let public_fn (x: int) : int = helper x
print (Secret.helper 5)
)",
                                   "private"));
    }
}

TEST_CASE("module.inline.private_internal_access", "[module][codegen]")
{
    SECTION("simple private function succeeds in IR generation")
    {
        CHECK(generatesIRSuccessfully(R"(
module Math =
    let private sq (x: int) : int = x * x
)"));
    }

    SECTION("private function can be called from within module")
    {
        auto output = executeSessionAndGetOutput({
            R"(
module Math =
    let private sq (x: int) : int = x * x
    let cube (x: int) : int = (sq x) * x
print (Math.cube 3)
)" });
        CHECK(output == "27");
    }
}

TEST_CASE("module.inline.value_binding_access", "[module][codegen]")
{
    SECTION("access value binding via qualified name")
    {
        auto output = executeSessionAndGetOutput({
            R"(
module Constants =
    let pi = 3
    let e = 2
print (Constants.pi)
)" });
        CHECK(output == "3");
    }

    SECTION("access multiple value bindings")
    {
        auto output = executeSessionAndGetOutput({
            R"(
module Constants =
    let pi = 3
    let e = 2
print (Constants.pi + Constants.e)
)" });
        CHECK(output == "5");
    }
}

TEST_CASE("module.file.private_enforcement", "[module][codegen]")
{
    auto& rt = TestRuntime::instance();
    rt.clearErrors();

    TempModuleDir tmpDir;
    tmpDir.writeModule("Priv",
                       "let private helper (x: int) : int = x + 1\n"
                       "let public_fn (x: int) : int = helper x");

    FSharpPersistentState state;
    state.moduleLoader = std::make_shared<ModuleLoader>(rt.runtime, rt.report);
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
// Module Signature tests
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

TEST_CASE("module.signature.loader_integration", "[module][signature]")
{
    auto& rt = TestRuntime::instance();
    rt.clearErrors();

    TempModuleDir tmpDir;
    tmpDir.writeModule("Checked", "let square (x: int) : int = x * x");

    SECTION("module with matching signature loads successfully")
    {
        auto sigPath = tmpDir.dir / "Checked.endoi";
        {
            std::ofstream(sigPath) << "val square : int -> int\n";
        }

        ModuleLoader loader(rt.runtime, rt.report);
        loader.addSearchPath(tmpDir.dir);

        auto const* desc = loader.loadModule("Checked");
        CHECK(desc != nullptr);
    }

    SECTION("module with mismatched signature fails to load")
    {
        auto sigPath = tmpDir.dir / "Checked.endoi";
        {
            std::ofstream(sigPath) << "val nonexistent : int -> int\n";
        }

        ModuleLoader loader(rt.runtime, rt.report);
        loader.addSearchPath(tmpDir.dir);

        auto const* desc = loader.loadModule("Checked");
        CHECK(desc == nullptr);
    }
}

// =============================================================================
// Value binding tests (evaluate once, open support, dedup)
// =============================================================================

TEST_CASE("module.inline.computed_value_binding", "[module][codegen]")
{
    SECTION("computed value evaluated once and loaded on each access")
    {
        auto output = executeSessionAndGetOutput({
            R"(
module Config =
    let value = 2 + 3
print (Config.value + Config.value)
)" });
        CHECK(output == "10");
    }
}

TEST_CASE("module.inline.value_binding_via_open", "[module][codegen]")
{
    SECTION("value bindings accessible via open (unqualified)")
    {
        auto output = executeSessionAndGetOutput({
            R"(
module Constants =
    let pi = 3
    let e = 2
open Constants
print (pi + e)
)" });
        CHECK(output == "5");
    }
}

TEST_CASE("module.inline.value_binding_selective_open", "[module][codegen]")
{
    SECTION("selective open brings only listed value bindings")
    {
        auto output = executeSessionAndGetOutput({
            R"(
module Constants =
    let pi = 3
    let e = 2
open Constants with (pi)
print pi
)" });
        CHECK(output == "3");
    }
}

TEST_CASE("module.inline.value_and_function_mix", "[module][codegen]")
{
    SECTION("module with both value bindings and functions")
    {
        auto output = executeSessionAndGetOutput({
            R"(
module Utils =
    let offset = 10
    let add (x: int) : int = x + offset
print (Utils.add 5)
)" });
        CHECK(output == "15");
    }
}

TEST_CASE("module.inline.open_deduplication", "[module][codegen]")
{
    SECTION("repeated open does not crash or duplicate entries")
    {
        auto output = executeSessionAndGetOutput({
            R"(
module Math =
    let square (x: int) : int = x * x
open Math
open Math
print (square 4)
)" });
        CHECK(output == "16");
    }
}

TEST_CASE("module.loader.available_nested_modules", "[module][loader]")
{
    auto& rt = TestRuntime::instance();
    rt.clearErrors();

    TempModuleDir tmpDir;
    tmpDir.writeModule("Math", "let x = 1");
    tmpDir.writeNestedModule("Geometry", "Circle", "let area (r: int) : int = r * r * 3");

    ModuleLoader loader(rt.runtime, rt.report);
    loader.addSearchPath(tmpDir.dir);

    auto names = loader.availableModuleNames();
    CHECK(std::ranges::find(names, "Math") != names.end());
    CHECK(std::ranges::find(names, "Geometry.Circle") != names.end());
}
