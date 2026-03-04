// SPDX-License-Identifier: Apache-2.0
#include <endo-language/builtins/BuiltinSignatures.hpp>
#include <endo-language/builtins/PropertyDescriptors.hpp>
#include <endo-language/builtins/StdlibDescriptors.hpp>

#include <array>
#include <string>

namespace endo
{

namespace
{
    /// Helper that binds a resolved callback or a no-op default.
    void bindResolved(CoreVM::NativeCallback& reg,
                      CallbackResolver const& resolve,
                      std::string_view name,
                      size_t arity)
    {
        if (auto cb = resolve(name, arity))
            reg.bind(*cb);
        else
            reg.bind([](CoreVM::Params&) {});
    }
} // namespace

// ---------------------------------------------------------------------------
// F# language builtins
// ---------------------------------------------------------------------------

void registerFSharpBuiltins(CoreVM::Runtime& rt, CallbackResolver const& resolve)
{
    for (auto const& desc: stdlibDescriptors())
    {
        if (desc.vmName.empty())
            continue; // IR-generated or registered elsewhere, no VM registration needed

        auto& reg = rt.registerFunction(std::string(desc.vmName));
        for (auto const& p: desc.params)
            reg.param(p.type, std::string(p.name));
        reg.returnType(desc.returnType);
        bindResolved(reg, resolve, desc.vmName, desc.params.size());
    }
}

// ---------------------------------------------------------------------------
// Shell command builtins
// ---------------------------------------------------------------------------

void registerShellBuiltins(CoreVM::Runtime& rt, CallbackResolver const& resolve)
{
    // clang-format off

    // exit(code: number) -> void
    bindResolved(rt.registerFunction("exit")
        .param<CoreVM::CoreNumber>("code")
        .returnType(CoreVM::LiteralType::Void), resolve, "exit", 1);

    // export(name: string) -> void (shell-style)
    bindResolved(rt.registerFunction("export")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Void), resolve, "export", 1);

    // export(name: string, value: string) -> void (shell-style)
    bindResolved(rt.registerFunction("export")
        .param<std::string>("name")
        .param<std::string>("value")
        .returnType(CoreVM::LiteralType::Void), resolve, "export", 2);

    // set(name: string, value: string) -> bool
    bindResolved(rt.registerFunction("set")
        .param<std::string>("name")
        .param<std::string>("value")
        .returnType(CoreVM::LiteralType::Boolean), resolve, "set", 2);

    // cd() -> bool
    bindResolved(rt.registerFunction("cd")
        .returnType(CoreVM::LiteralType::Boolean), resolve, "cd", 0);

    // cd(path: string) -> bool
    bindResolved(rt.registerFunction("cd")
        .param<std::string>("path")
        .returnType(CoreVM::LiteralType::Boolean), resolve, "cd", 1);

    // unset(name: string) -> bool
    bindResolved(rt.registerFunction("unset")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Boolean), resolve, "unset", 1);

    // read() -> string
    bindResolved(rt.registerFunction("read")
        .returnType(CoreVM::LiteralType::String), resolve, "read", 0);

    // read(args: string[]) -> string
    bindResolved(rt.registerFunction("read")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::String), resolve, "read", 1);

    // jobs() -> number
    bindResolved(rt.registerFunction("jobs")
        .returnType(CoreVM::LiteralType::Number), resolve, "jobs", 0);

    // fg() -> number
    bindResolved(rt.registerFunction("fg")
        .returnType(CoreVM::LiteralType::Number), resolve, "fg", 0);

    // fg(job_id: number) -> number
    bindResolved(rt.registerFunction("fg")
        .param<CoreVM::CoreNumber>("job_id")
        .returnType(CoreVM::LiteralType::Number), resolve, "fg", 1);

    // bg() -> number
    bindResolved(rt.registerFunction("bg")
        .returnType(CoreVM::LiteralType::Number), resolve, "bg", 0);

    // bg(job_id: number) -> number
    bindResolved(rt.registerFunction("bg")
        .param<CoreVM::CoreNumber>("job_id")
        .returnType(CoreVM::LiteralType::Number), resolve, "bg", 1);

    // wait() -> number
    bindResolved(rt.registerFunction("wait")
        .returnType(CoreVM::LiteralType::Number), resolve, "wait", 0);

    // wait(job_id: number) -> number
    bindResolved(rt.registerFunction("wait")
        .param<CoreVM::CoreNumber>("job_id")
        .returnType(CoreVM::LiteralType::Number), resolve, "wait", 1);

    // bind() -> number
    bindResolved(rt.registerFunction("bind")
        .returnType(CoreVM::LiteralType::Number), resolve, "bind", 0);

    // bind(args: string[]) -> number
    bindResolved(rt.registerFunction("bind")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number), resolve, "bind", 1);

    // which() -> number
    bindResolved(rt.registerFunction("which")
        .returnType(CoreVM::LiteralType::Number), resolve, "which", 0);

    // which(args: string[]) -> number
    bindResolved(rt.registerFunction("which")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number), resolve, "which", 1);

    // callproc(args: string[]) -> number
    bindResolved(rt.registerFunction("callproc")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number), resolve, "callproc", 1);

    // callproc(last_in_chain: bool, args: string[]) -> number
    bindResolved(rt.registerFunction("callproc")
        .param<bool>("last_in_chain")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number), resolve, "callproc", 2);

    // getvar(name: string) -> string
    bindResolved(rt.registerFunction("getvar")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::String), resolve, "getvar", 1);

    // getvar.exitstatus() -> string/number (varies by consumer)
    bindResolved(rt.registerFunction("getvar.exitstatus")
        .returnType(CoreVM::LiteralType::Number), resolve, "getvar.exitstatus", 0);

    // getvar.processid() -> string
    bindResolved(rt.registerFunction("getvar.processid")
        .returnType(CoreVM::LiteralType::String), resolve, "getvar.processid", 0);

    // getvar.backgroundid() -> string
    bindResolved(rt.registerFunction("getvar.backgroundid")
        .returnType(CoreVM::LiteralType::String), resolve, "getvar.backgroundid", 0);

    // getvar.positional(index: number) -> string
    bindResolved(rt.registerFunction("getvar.positional")
        .param<CoreVM::CoreNumber>("index")
        .returnType(CoreVM::LiteralType::String), resolve, "getvar.positional", 1);

    // setvar.exitstatus(code: number) -> void
    bindResolved(rt.registerFunction("setvar.exitstatus")
        .param<CoreVM::CoreNumber>("code")
        .returnType(CoreVM::LiteralType::Void), resolve, "setvar.exitstatus", 1);

    // register_completer(command: string, function_name: string) -> void
    bindResolved(rt.registerFunction("register_completer")
        .param<CoreVM::CoreString>("command")
        .param<CoreVM::CoreString>("function_name")
        .returnType(CoreVM::LiteralType::Void), resolve, "register_completer", 2);

    // clang-format on
}

// ---------------------------------------------------------------------------
// Internal VM builtins
// ---------------------------------------------------------------------------

void registerInternalBuiltins(CoreVM::Runtime& rt, CallbackResolver const& resolve)
{
    // clang-format off

    // --- Command building ---

    // internal.cmd_start(cmd: string) -> void
    bindResolved(rt.registerFunction("internal.cmd_start")
        .param<CoreVM::CoreString>("cmd")
        .returnType(CoreVM::LiteralType::Void), resolve, "internal.cmd_start", 1);

    // internal.cmd_arg(arg: string) -> void
    bindResolved(rt.registerFunction("internal.cmd_arg")
        .param<CoreVM::CoreString>("arg")
        .returnType(CoreVM::LiteralType::Void), resolve, "internal.cmd_arg", 1);

    // internal.cmd_exec() -> number
    bindResolved(rt.registerFunction("internal.cmd_exec")
        .returnType(CoreVM::LiteralType::Number), resolve, "internal.cmd_exec", 0);

    // internal.cmd_exec_piped(last_in_chain: bool) -> number
    bindResolved(rt.registerFunction("internal.cmd_exec_piped")
        .param<bool>("last_in_chain")
        .returnType(CoreVM::LiteralType::Number), resolve, "internal.cmd_exec_piped", 1);

    // internal.cmd_exec_piped_background(command: string) -> number
    bindResolved(rt.registerFunction("internal.cmd_exec_piped_background")
        .param<std::string>("command")
        .returnType(CoreVM::LiteralType::Number), resolve, "internal.cmd_exec_piped_background", 1);

    // --- Redirections ---

    // internal.redirect_start() -> void
    bindResolved(rt.registerFunction("internal.redirect_start")
        .returnType(CoreVM::LiteralType::Void), resolve, "internal.redirect_start", 0);

    // internal.redirect_input(target_fd: number, path: string) -> void
    bindResolved(rt.registerFunction("internal.redirect_input")
        .param<CoreVM::CoreNumber>("target_fd")
        .param<std::string>("path")
        .returnType(CoreVM::LiteralType::Void), resolve, "internal.redirect_input", 2);

    // internal.redirect_output(source_fd: number, path: string, append: bool) -> void
    bindResolved(rt.registerFunction("internal.redirect_output")
        .param<CoreVM::CoreNumber>("source_fd")
        .param<std::string>("path")
        .param<bool>("append")
        .returnType(CoreVM::LiteralType::Void), resolve, "internal.redirect_output", 3);

    // internal.redirect_fd_dup(source_fd: number, target_fd: number) -> void
    bindResolved(rt.registerFunction("internal.redirect_fd_dup")
        .param<CoreVM::CoreNumber>("source_fd")
        .param<CoreVM::CoreNumber>("target_fd")
        .returnType(CoreVM::LiteralType::Void), resolve, "internal.redirect_fd_dup", 2);

    // internal.redirect_heredoc(target_fd: number, content: string) -> void
    bindResolved(rt.registerFunction("internal.redirect_heredoc")
        .param<CoreVM::CoreNumber>("target_fd")
        .param<std::string>("content")
        .returnType(CoreVM::LiteralType::Void), resolve, "internal.redirect_heredoc", 2);

    // internal.redirect_herestring(target_fd: number, content: string) -> void
    bindResolved(rt.registerFunction("internal.redirect_herestring")
        .param<CoreVM::CoreNumber>("target_fd")
        .param<std::string>("content")
        .returnType(CoreVM::LiteralType::Void), resolve, "internal.redirect_herestring", 2);

    // internal.redirect_end() -> void
    bindResolved(rt.registerFunction("internal.redirect_end")
        .returnType(CoreVM::LiteralType::Void), resolve, "internal.redirect_end", 0);

    // --- Command substitution ---

    // internal.subst_start() -> void
    bindResolved(rt.registerFunction("internal.subst_start")
        .returnType(CoreVM::LiteralType::Void), resolve, "internal.subst_start", 0);

    // internal.subst_end() -> string
    bindResolved(rt.registerFunction("internal.subst_end")
        .returnType(CoreVM::LiteralType::String), resolve, "internal.subst_end", 0);

    // --- Process substitution ---

    // internal.procsubst_fork(is_write: bool) -> number
    bindResolved(rt.registerFunction("internal.procsubst_fork")
        .param<bool>("is_write")
        .returnType(CoreVM::LiteralType::Number), resolve, "internal.procsubst_fork", 1);

    // internal.procsubst_exit() -> void
    bindResolved(rt.registerFunction("internal.procsubst_exit")
        .returnType(CoreVM::LiteralType::Void), resolve, "internal.procsubst_exit", 0);

    // internal.procsubst_get_path() -> string
    bindResolved(rt.registerFunction("internal.procsubst_get_path")
        .returnType(CoreVM::LiteralType::String), resolve, "internal.procsubst_get_path", 0);

    // internal.procsubst_cleanup() -> void
    bindResolved(rt.registerFunction("internal.procsubst_cleanup")
        .returnType(CoreVM::LiteralType::Void), resolve, "internal.procsubst_cleanup", 0);

    // --- File operations ---

    // internal.open_read(path: string) -> number
    bindResolved(rt.registerFunction("internal.open_read")
        .param<std::string>("path")
        .returnType(CoreVM::LiteralType::Number), resolve, "internal.open_read", 1);

    // internal.open_write(path: string, oflags: number) -> number
    bindResolved(rt.registerFunction("internal.open_write")
        .param<std::string>("path")
        .param<CoreVM::CoreNumber>("oflags")
        .returnType(CoreVM::LiteralType::Number), resolve, "internal.open_write", 2);

    // --- For-loop support ---

    // internal.for_init(var: string) -> void
    bindResolved(rt.registerFunction("internal.for_init")
        .param<std::string>("var")
        .returnType(CoreVM::LiteralType::Void), resolve, "internal.for_init", 1);

    // internal.for_add_item(item: string) -> void
    bindResolved(rt.registerFunction("internal.for_add_item")
        .param<std::string>("item")
        .returnType(CoreVM::LiteralType::Void), resolve, "internal.for_add_item", 1);

    // internal.for_has_more() -> bool
    bindResolved(rt.registerFunction("internal.for_has_more")
        .returnType(CoreVM::LiteralType::Boolean), resolve, "internal.for_has_more", 0);

    // internal.for_next(var: string) -> void
    bindResolved(rt.registerFunction("internal.for_next")
        .param<std::string>("var")
        .returnType(CoreVM::LiteralType::Void), resolve, "internal.for_next", 1);

    // internal.for_cleanup() -> void
    bindResolved(rt.registerFunction("internal.for_cleanup")
        .returnType(CoreVM::LiteralType::Void), resolve, "internal.for_cleanup", 0);

    // --- Case matching ---

    // internal.case_match(word: string, pattern: string) -> bool
    bindResolved(rt.registerFunction("internal.case_match")
        .param<std::string>("word")
        .param<std::string>("pattern")
        .returnType(CoreVM::LiteralType::Boolean), resolve, "internal.case_match", 2);

    // --- Shell function support ---

    // internal.function_register(name: string) -> void
    bindResolved(rt.registerFunction("internal.function_register")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Void), resolve, "internal.function_register", 1);

    // internal.function_call(name: string) -> number
    bindResolved(rt.registerFunction("internal.function_call")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Number), resolve, "internal.function_call", 1);

    // --- Shell expansion ---

    // expand.tilde(suffix: string) -> string
    bindResolved(rt.registerFunction("expand.tilde")
        .param<CoreVM::CoreString>("suffix")
        .returnType(CoreVM::LiteralType::String), resolve, "expand.tilde", 1);

    // expand.tilde_user(user: string, suffix: string) -> string
    bindResolved(rt.registerFunction("expand.tilde_user")
        .param<CoreVM::CoreString>("user")
        .param<CoreVM::CoreString>("suffix")
        .returnType(CoreVM::LiteralType::String), resolve, "expand.tilde_user", 2);

    // expand.glob(pattern: string) -> void
    bindResolved(rt.registerFunction("expand.glob")
        .param<CoreVM::CoreString>("pattern")
        .returnType(CoreVM::LiteralType::Void), resolve, "expand.glob", 1);

    // expand.to_string(value: number) -> string
    bindResolved(rt.registerFunction("expand.to_string")
        .param<CoreVM::CoreNumber>("value")
        .returnType(CoreVM::LiteralType::String), resolve, "expand.to_string", 1);

    // expand.arith_to_string(value: number) -> string
    bindResolved(rt.registerFunction("expand.arith_to_string")
        .param<CoreVM::CoreNumber>("value")
        .returnType(CoreVM::LiteralType::String), resolve, "expand.arith_to_string", 1);

    // expand.arith_getvar(name: string) -> number
    bindResolved(rt.registerFunction("expand.arith_getvar")
        .param<CoreVM::CoreString>("name")
        .returnType(CoreVM::LiteralType::Number), resolve, "expand.arith_getvar", 1);

    // expand.arith_pow(base: number, exp: number) -> number
    bindResolved(rt.registerFunction("expand.arith_pow")
        .param<CoreVM::CoreNumber>("base")
        .param<CoreVM::CoreNumber>("exp")
        .returnType(CoreVM::LiteralType::Number), resolve, "expand.arith_pow", 2);

    // --- Parameter expansion ---

    // expand.param_length(var: string) -> string
    bindResolved(rt.registerFunction("expand.param_length")
        .param<std::string>("var")
        .returnType(CoreVM::LiteralType::String), resolve, "expand.param_length", 1);

    // expand.param_default(var: string, default_value: string) -> string
    bindResolved(rt.registerFunction("expand.param_default")
        .param<std::string>("var")
        .param<std::string>("default_value")
        .returnType(CoreVM::LiteralType::String), resolve, "expand.param_default", 2);

    // expand.param_alternate(var: string, alternate: string) -> string
    bindResolved(rt.registerFunction("expand.param_alternate")
        .param<std::string>("var")
        .param<std::string>("alternate")
        .returnType(CoreVM::LiteralType::String), resolve, "expand.param_alternate", 2);

    // expand.param_assign(var: string, default_value: string) -> string
    bindResolved(rt.registerFunction("expand.param_assign")
        .param<std::string>("var")
        .param<std::string>("default_value")
        .returnType(CoreVM::LiteralType::String), resolve, "expand.param_assign", 2);

    // expand.param_error(var: string, error_msg: string) -> string
    bindResolved(rt.registerFunction("expand.param_error")
        .param<std::string>("var")
        .param<std::string>("error_msg")
        .returnType(CoreVM::LiteralType::String), resolve, "expand.param_error", 2);

    // expand.param_remove_prefix(var: string, pattern: string, longest: bool) -> string
    bindResolved(rt.registerFunction("expand.param_remove_prefix")
        .param<std::string>("var")
        .param<std::string>("pattern")
        .param<bool>("longest")
        .returnType(CoreVM::LiteralType::String), resolve, "expand.param_remove_prefix", 3);

    // expand.param_remove_suffix(var: string, pattern: string, longest: bool) -> string
    bindResolved(rt.registerFunction("expand.param_remove_suffix")
        .param<std::string>("var")
        .param<std::string>("pattern")
        .param<bool>("longest")
        .returnType(CoreVM::LiteralType::String), resolve, "expand.param_remove_suffix", 3);

    // expand.param_replace(var: string, pattern: string, replacement: string, all: bool) -> string
    bindResolved(rt.registerFunction("expand.param_replace")
        .param<std::string>("var")
        .param<std::string>("pattern")
        .param<std::string>("replacement")
        .param<bool>("all")
        .returnType(CoreVM::LiteralType::String), resolve, "expand.param_replace", 4);

    // clang-format on
}

// ---------------------------------------------------------------------------
// Structured data builtins
// ---------------------------------------------------------------------------

void registerStructuredBuiltins(CoreVM::Runtime& rt, CallbackResolver const& resolve)
{
    // clang-format off

    // structured_ps() -> number (list)
    bindResolved(rt.registerFunction("structured_ps")
        .returnType(CoreVM::LiteralType::Number), resolve, "structured_ps", 0);

    // structured_ls(path: string) -> number (list)
    bindResolved(rt.registerFunction("structured_ls")
        .param<CoreVM::CoreString>("path")
        .returnType(CoreVM::LiteralType::Number), resolve, "structured_ls", 1);

    // structured_jobs() -> number (list)
    bindResolved(rt.registerFunction("structured_jobs")
        .returnType(CoreVM::LiteralType::Number), resolve, "structured_jobs", 0);

    // structured_find(args: string) -> number (list)
    bindResolved(rt.registerFunction("structured_find")
        .param<CoreVM::CoreString>("args")
        .returnType(CoreVM::LiteralType::Number), resolve, "structured_find", 1);

    // --- Output definition commands ---

    // structured_docker_ps() -> number (list)
    bindResolved(rt.registerFunction("structured_docker_ps")
        .returnType(CoreVM::LiteralType::Number), resolve, "structured_docker_ps", 0);

    // structured_docker_images() -> number (list)
    bindResolved(rt.registerFunction("structured_docker_images")
        .returnType(CoreVM::LiteralType::Number), resolve, "structured_docker_images", 0);

    // structured_git_log() -> number (list)
    bindResolved(rt.registerFunction("structured_git_log")
        .returnType(CoreVM::LiteralType::Number), resolve, "structured_git_log", 0);

    // structured_git_status() -> number (list)
    bindResolved(rt.registerFunction("structured_git_status")
        .returnType(CoreVM::LiteralType::Number), resolve, "structured_git_status", 0);

    // --- Data source wrappers ---

    // open_json(path: string, schema_desc: string, type_id: number) -> number (list)
    bindResolved(rt.registerFunction("open_json")
        .param<CoreVM::CoreString>("path")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number), resolve, "open_json", 3);

    // open_csv(path: string, schema_desc: string, type_id: number) -> number (list)
    bindResolved(rt.registerFunction("open_csv")
        .param<CoreVM::CoreString>("path")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number), resolve, "open_csv", 3);

    // from_json(source_cmd: string, schema_desc: string, type_id: number) -> number (list)
    bindResolved(rt.registerFunction("from_json")
        .param<CoreVM::CoreString>("source_cmd")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number), resolve, "from_json", 3);

    // from_csv(source_cmd: string, schema_desc: string, type_id: number) -> number (list)
    bindResolved(rt.registerFunction("from_csv")
        .param<CoreVM::CoreString>("source_cmd")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number), resolve, "from_csv", 3);

    // clang-format on
}

// ---------------------------------------------------------------------------
// Prompt configuration properties
// ---------------------------------------------------------------------------

namespace
{
    /// Registers a property and binds getter/setter via the CallbackResolver.
    /// The property name is passed once to avoid duplication.
    void registerPropertyResolved(CoreVM::Runtime& rt,
                                  CallbackResolver const& resolve,
                                  std::string_view name,
                                  CoreVM::LiteralType type,
                                  std::string_view desc = {})
    {
        auto& prop = rt.registerProperty(std::string(name), type);
        if (!desc.empty())
            prop.description(std::string(desc));

        if (auto getterCb = resolve(name, 0))
            prop.onGet(*getterCb);
        else
            prop.onGet([](CoreVM::Params&) {});

        if (auto setterCb = resolve(name, 1))
            prop.onSet(*setterCb);
        else
            prop.onSet([](CoreVM::Params&) {});
    }

    /// Registers a read-only property (getter only, no setter).
    /// The IRGenerator checks `NativeProperty::hasSetter()` and emits a compile-time
    /// "Cannot assign to read-only property" error when assignment is attempted.
    void registerReadOnlyPropertyResolved(CoreVM::Runtime& rt,
                                          CallbackResolver const& resolve,
                                          std::string_view name,
                                          CoreVM::LiteralType type,
                                          std::string_view desc = {})
    {
        auto& prop = rt.registerProperty(std::string(name), type);
        if (!desc.empty())
            prop.description(std::string(desc));
        if (auto getterCb = resolve(name, 0))
            prop.onGet(*getterCb);
        else
            prop.onGet([](CoreVM::Params&) {});
        // No onSet() — property remains read-only
    }
} // namespace

void registerPromptPropertyBuiltins(CoreVM::Runtime& rt, CallbackResolver const& resolve)
{
    for (auto const& desc: promptPropertyDescriptors())
    {
        if (desc.readOnly)
            registerReadOnlyPropertyResolved(rt, resolve, desc.name, desc.type, desc.description);
        else
            registerPropertyResolved(rt, resolve, desc.name, desc.type, desc.description);
    }
}

// ---------------------------------------------------------------------------
// Agent configuration properties
// ---------------------------------------------------------------------------

void registerAgentConfigPropertyBuiltins(CoreVM::Runtime& rt, CallbackResolver const& resolve)
{
    for (auto const& desc: agentPropertyDescriptors())
    {
        if (desc.readOnly)
            registerReadOnlyPropertyResolved(rt, resolve, desc.name, desc.type, desc.description);
        else
            registerPropertyResolved(rt, resolve, desc.name, desc.type, desc.description);
    }
}

// ---------------------------------------------------------------------------
// User-facing builtin names for shell completion
// ---------------------------------------------------------------------------

std::vector<std::string> userFacingBuiltinNames()
{
    auto builtins = userFacingBuiltins();
    std::vector<std::string> names;
    names.reserve(builtins.size());
    for (auto& b: builtins)
        names.push_back(std::move(b.name));
    return names;
}

namespace
{
    /// @brief Describes a non-property shell builtin or keyword for completion.
    struct ShellBuiltinDescriptor
    {
        std::string_view name;
        std::string_view description;
        std::string_view detail;
    };

    // clang-format off
    constexpr std::array shellBuiltinDescriptors = {
        ShellBuiltinDescriptor { .name="cat", .description="builtin", .detail="**cat** -- builtin\n\nConcatenates and displays file contents." },
        ShellBuiltinDescriptor { .name="cd", .description="builtin", .detail="**cd** -- builtin\n\nChanges the current working directory.\n\n```\ncd /tmp\ncd ~\n```" },
        ShellBuiltinDescriptor { .name="exit", .description="builtin", .detail="**exit** -- builtin\n\nExits the shell with an optional exit code.\n\n```\nexit\nexit 1\n```" },
        ShellBuiltinDescriptor { .name="export", .description="builtin", .detail="**export** -- builtin\n\nSets an environment variable.\n\n```\nexport PATH=\"/usr/bin:$PATH\"\n```" },
        ShellBuiltinDescriptor { .name="mv", .description="builtin", .detail="**mv** -- builtin\n\nMoves or renames files and directories." },
        ShellBuiltinDescriptor { .name="rm", .description="builtin", .detail="**rm** -- builtin\n\nRemoves files and directories." },
        ShellBuiltinDescriptor { .name="set", .description="builtin", .detail="**set** -- builtin\n\nSets a shell variable." },
        ShellBuiltinDescriptor { .name="unset", .description="builtin", .detail="**unset** -- builtin\n\nRemoves a shell variable." },
        ShellBuiltinDescriptor { .name="read", .description="builtin", .detail="**read** -- builtin\n\nReads a line of input into a variable." },
        ShellBuiltinDescriptor { .name="sleep", .description="builtin", .detail="**sleep** -- builtin\n\nPauses execution for a given duration.\n\n```\nsleep 2\n```" },
        ShellBuiltinDescriptor { .name="true", .description="builtin", .detail="**true** -- builtin\n\nReturns exit code 0 (success)." },
        ShellBuiltinDescriptor { .name="false", .description="builtin", .detail="**false** -- builtin\n\nReturns exit code 1 (failure)." },
        ShellBuiltinDescriptor { .name="jobs", .description="builtin", .detail="**jobs** -- builtin\n\nLists background jobs." },
        ShellBuiltinDescriptor { .name="fg", .description="builtin", .detail="**fg** -- builtin\n\nBrings a background job to the foreground." },
        ShellBuiltinDescriptor { .name="bg", .description="builtin", .detail="**bg** -- builtin\n\nResumes a suspended job in the background." },
        ShellBuiltinDescriptor { .name="wait", .description="builtin", .detail="**wait** -- builtin\n\nWaits for background jobs to complete." },
        ShellBuiltinDescriptor { .name="bind", .description="builtin", .detail="**bind** -- builtin\n\nBinds a key sequence to a command." },
        ShellBuiltinDescriptor { .name="which", .description="builtin", .detail="**which** -- builtin\n\nLocates a command in `$PATH`." },
        ShellBuiltinDescriptor { .name="echo", .description="builtin", .detail="**echo** -- builtin\n\nPrints arguments to stdout.\n\n```\necho \"hello world\"\n```" },
        ShellBuiltinDescriptor { .name="grep", .description="builtin", .detail="**grep** -- builtin\n\nSearches for patterns in files.\n\n```\ngrep -rn TODO src/\n```" },
        ShellBuiltinDescriptor { .name="timeout", .description="builtin", .detail="**timeout** -- builtin\n\nRun a command with a time limit.\n\n```\ntimeout 5 sleep 10\ntimeout -s KILL -k 2 30 long_running_cmd\n```" },
        ShellBuiltinDescriptor { .name="print", .description="F# print function", .detail="**print** -- builtin\n\nPrints a value without newline (F# style).\n\n```\nprint 42\nprint \"hello\"\n```" },
        ShellBuiltinDescriptor { .name="println", .description="F# print with newline", .detail="**println** -- builtin\n\nPrints a value followed by a newline (F# style).\n\n```\nprintln \"hello world\"\n```" },
        // MCP server management
        ShellBuiltinDescriptor { .name="add_mcp_server", .description="Register an MCP server", .detail="**add_mcp_server** -- builtin\n\nRegisters an MCP (Model Context Protocol) server for agent use." },
        ShellBuiltinDescriptor { .name="set_mcp_env", .description="Set environment variable for an MCP server", .detail="**set_mcp_env** -- builtin\n\nSets an environment variable for a registered MCP server." },
        ShellBuiltinDescriptor { .name="remove_mcp_server", .description="Remove an MCP server", .detail="**remove_mcp_server** -- builtin\n\nRemoves a previously registered MCP server." },
    };

    constexpr std::array keywordDescriptors = {
        ShellBuiltinDescriptor { .name="if", .description="builtin", .detail="**if** -- shell keyword\n\n```\nif condition then\n  body\nfi\n```" },
        ShellBuiltinDescriptor { .name="then", .description="builtin", .detail="**then** -- shell keyword\n\nFollows the condition in a shell `if` statement." },
        ShellBuiltinDescriptor { .name="else", .description="builtin", .detail="**else** -- shell keyword\n\nAlternative branch in a shell `if` statement." },
        ShellBuiltinDescriptor { .name="elif", .description="builtin", .detail="**elif** -- shell keyword\n\nElse-if branch in a conditional expression." },
        ShellBuiltinDescriptor { .name="for", .description="builtin", .detail="**for** -- shell keyword\n\n```\nfor x in [1; 2; 3] do\n  print x\ndone\n```" },
        ShellBuiltinDescriptor { .name="while", .description="builtin", .detail="**while** -- shell keyword\n\nLoop while a condition holds." },
        ShellBuiltinDescriptor { .name="do", .description="builtin", .detail="**do** -- shell keyword\n\nBegins the body of a for/while loop." },
        ShellBuiltinDescriptor { .name="in", .description="builtin", .detail="**in** -- shell keyword\n\nSeparates variable from iterable in for loops." },
        ShellBuiltinDescriptor { .name="return", .description="builtin", .detail="**return** -- shell keyword\n\nReturns from a function." },
        ShellBuiltinDescriptor { .name="break", .description="builtin", .detail="**break** -- shell keyword\n\nExits the innermost loop." },
        ShellBuiltinDescriptor { .name="continue", .description="builtin", .detail="**continue** -- shell keyword\n\nSkips to the next iteration of the innermost loop." },
    };
    // clang-format on
} // namespace

std::vector<BuiltinInfo> userFacingBuiltins()
{
    std::vector<BuiltinInfo> result;
    result.reserve(shellBuiltinDescriptors.size() + keywordDescriptors.size()
                   + allPropertyDescriptors().size());

    // Shell builtins
    for (auto const& desc: shellBuiltinDescriptors)
        result.push_back(
            { std::string(desc.name), std::string(desc.description), false, std::string(desc.detail) });

    // Keywords
    for (auto const& desc: keywordDescriptors)
        result.push_back(
            { std::string(desc.name), std::string(desc.description), false, std::string(desc.detail) });

    // Properties (from PropertyDescriptors)
    for (auto const& desc: allPropertyDescriptors())
        result.push_back(
            { std::string(desc.name), std::string(desc.description), true, std::string(desc.detail) });

    return result;
}

} // namespace endo
