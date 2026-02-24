// SPDX-License-Identifier: Apache-2.0
#include <endo-language/builtins/BuiltinSignatures.hpp>

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
    // clang-format off

    // print(text: string) -> void
    bindResolved(rt.registerFunction("print")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void), resolve, "print", 1);

    // println(text: string) -> void
    bindResolved(rt.registerFunction("println")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void), resolve, "println", 1);

    // env.has(key: string) -> bool
    bindResolved(rt.registerFunction("env.has")
        .param<CoreVM::CoreString>("key")
        .returnType(CoreVM::LiteralType::Boolean), resolve, "env.has", 1);

    // env.get(key: string) -> string
    bindResolved(rt.registerFunction("env.get")
        .param<CoreVM::CoreString>("key")
        .returnType(CoreVM::LiteralType::String), resolve, "env.get", 1);

    // which_find(program: string) -> number (Option<string>)
    bindResolved(rt.registerFunction("which_find")
        .param<CoreVM::CoreString>("program")
        .returnType(CoreVM::LiteralType::Number), resolve, "which_find", 1);

    // export(name: string, value: string) -> void (F#-style two-param)
    bindResolved(rt.registerFunction("export")
        .param<CoreVM::CoreString>("name")
        .param<CoreVM::CoreString>("value")
        .returnType(CoreVM::LiteralType::Void), resolve, "export", 2);

    // export(name: string) -> void (F#-style one-param)
    bindResolved(rt.registerFunction("export")
        .param<CoreVM::CoreString>("name")
        .returnType(CoreVM::LiteralType::Void), resolve, "export", 1);

    // --- List operations ---

    // list_concat(left: number, right: number) -> number
    bindResolved(rt.registerFunction("list_concat")
        .param<CoreVM::CoreNumber>("left")
        .param<CoreVM::CoreNumber>("right")
        .returnType(CoreVM::LiteralType::Number), resolve, "list_concat", 2);

    // list_head(list: number) -> number (Option)
    bindResolved(rt.registerFunction("list_head")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number), resolve, "list_head", 1);

    // list_tail(list: number) -> number
    bindResolved(rt.registerFunction("list_tail")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number), resolve, "list_tail", 1);

    // list_length(list: number) -> number
    bindResolved(rt.registerFunction("list_length")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number), resolve, "list_length", 1);

    // list_isEmpty(list: number) -> bool
    bindResolved(rt.registerFunction("list_isEmpty")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Boolean), resolve, "list_isEmpty", 1);

    // list_sort(list: number) -> number
    bindResolved(rt.registerFunction("list_sort")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number), resolve, "list_sort", 1);

    // list_distinct(list: number) -> number
    bindResolved(rt.registerFunction("list_distinct")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number), resolve, "list_distinct", 1);

    // list_sort_pairs(pairs: number) -> number
    bindResolved(rt.registerFunction("list_sort_pairs")
        .param<CoreVM::CoreNumber>("pairs")
        .returnType(CoreVM::LiteralType::Number), resolve, "list_sort_pairs", 1);

    // list_group_pairs(pairs: number) -> number
    bindResolved(rt.registerFunction("list_group_pairs")
        .param<CoreVM::CoreNumber>("pairs")
        .returnType(CoreVM::LiteralType::Number), resolve, "list_group_pairs", 1);

    // list_nth(index: number, list: number) -> number (Option)
    bindResolved(rt.registerFunction("list_nth")
        .param<CoreVM::CoreNumber>("index")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number), resolve, "list_nth", 2);

    // list_last(list: number) -> number (Option)
    bindResolved(rt.registerFunction("list_last")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number), resolve, "list_last", 1);

    // list_replicate(count: number, value: number) -> number
    bindResolved(rt.registerFunction("list_replicate")
        .param<CoreVM::CoreNumber>("count")
        .param<CoreVM::CoreNumber>("value")
        .returnType(CoreVM::LiteralType::Number), resolve, "list_replicate", 2);

    // list_char_range(start: number, end: number) -> number
    bindResolved(rt.registerFunction("list_char_range")
        .param<CoreVM::CoreNumber>("start")
        .param<CoreVM::CoreNumber>("end")
        .returnType(CoreVM::LiteralType::Number), resolve, "list_char_range", 2);

    // list_range(start: number, step: number, end: number) -> number
    bindResolved(rt.registerFunction("list_range")
        .param<CoreVM::CoreNumber>("start")
        .param<CoreVM::CoreNumber>("step")
        .param<CoreVM::CoreNumber>("end")
        .returnType(CoreVM::LiteralType::Number), resolve, "list_range", 3);

    // list_to_string(obj: number) -> string
    bindResolved(rt.registerFunction("list_to_string")
        .param<CoreVM::CoreNumber>("obj")
        .returnType(CoreVM::LiteralType::String), resolve, "list_to_string", 1);

    // object_to_string(obj: number) -> string
    bindResolved(rt.registerFunction("object_to_string")
        .param<CoreVM::CoreNumber>("obj")
        .returnType(CoreVM::LiteralType::String), resolve, "object_to_string", 1);

    // display_result(value: number) -> void
    bindResolved(rt.registerFunction("display_result")
        .param<CoreVM::CoreNumber>("value")
        .returnType(CoreVM::LiteralType::Void), resolve, "display_result", 1);

    // --- String operations ---

    // string_repeat(str: string, count: number) -> string
    bindResolved(rt.registerFunction("string_repeat")
        .param<CoreVM::CoreString>("str")
        .param<CoreVM::CoreNumber>("count")
        .returnType(CoreVM::LiteralType::String), resolve, "string_repeat", 2);

    // string_replace(old_str: string, new_str: string, text: string) -> string
    bindResolved(rt.registerFunction("string_replace")
        .param<CoreVM::CoreString>("old_str")
        .param<CoreVM::CoreString>("new_str")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::String), resolve, "string_replace", 3);

    // string_split(delimiter: string, text: string) -> number (list)
    bindResolved(rt.registerFunction("string_split")
        .param<CoreVM::CoreString>("delimiter")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Number), resolve, "string_split", 2);

    // string_join(separator: string, list: number) -> string
    bindResolved(rt.registerFunction("string_join")
        .param<CoreVM::CoreString>("separator")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::String), resolve, "string_join", 2);

    // string_trim(text: string) -> string
    bindResolved(rt.registerFunction("string_trim")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::String), resolve, "string_trim", 1);

    // string_toLower(text: string) -> string
    bindResolved(rt.registerFunction("string_toLower")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::String), resolve, "string_toLower", 1);

    // string_toUpper(text: string) -> string
    bindResolved(rt.registerFunction("string_toUpper")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::String), resolve, "string_toUpper", 1);

    // string_contains(haystack: string, needle: string) -> bool
    bindResolved(rt.registerFunction("string_contains")
        .param<CoreVM::CoreString>("haystack")
        .param<CoreVM::CoreString>("needle")
        .returnType(CoreVM::LiteralType::Boolean), resolve, "string_contains", 2);

    // string_startsWith(text: string, prefix: string) -> bool
    bindResolved(rt.registerFunction("string_startsWith")
        .param<CoreVM::CoreString>("text")
        .param<CoreVM::CoreString>("prefix")
        .returnType(CoreVM::LiteralType::Boolean), resolve, "string_startsWith", 2);

    // string_endsWith(text: string, suffix: string) -> bool
    bindResolved(rt.registerFunction("string_endsWith")
        .param<CoreVM::CoreString>("text")
        .param<CoreVM::CoreString>("suffix")
        .returnType(CoreVM::LiteralType::Boolean), resolve, "string_endsWith", 2);

    // --- Formatting helpers ---

    // format_datetime(epoch: number) -> string
    bindResolved(rt.registerFunction("format_datetime")
        .param<CoreVM::CoreNumber>("epoch")
        .returnType(CoreVM::LiteralType::String), resolve, "format_datetime", 1);

    // format_mode(mode: number) -> string
    bindResolved(rt.registerFunction("format_mode")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::String), resolve, "format_mode", 1);

    // format_number(separator: string, number: number) -> string
    bindResolved(rt.registerFunction("format_number")
        .param<CoreVM::CoreString>("separator")
        .param<CoreVM::CoreNumber>("number")
        .returnType(CoreVM::LiteralType::String), resolve, "format_number", 2);

    // format_number(number: number) -> string (uses user locale)
    bindResolved(rt.registerFunction("format_number")
        .param<CoreVM::CoreNumber>("number")
        .returnType(CoreVM::LiteralType::String), resolve, "format_number", 1);

    // mode_isReadable(mode: number) -> bool
    bindResolved(rt.registerFunction("mode_isReadable")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::Boolean), resolve, "mode_isReadable", 1);

    // mode_isWritable(mode: number) -> bool
    bindResolved(rt.registerFunction("mode_isWritable")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::Boolean), resolve, "mode_isWritable", 1);

    // mode_isExecutable(mode: number) -> bool
    bindResolved(rt.registerFunction("mode_isExecutable")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::Boolean), resolve, "mode_isExecutable", 1);

    // --- Random number generation ---

    // rand() -> number
    bindResolved(rt.registerFunction("rand")
        .returnType(CoreVM::LiteralType::Number), resolve, "rand", 0);

    // rand(min: number, max: number) -> number
    bindResolved(rt.registerFunction("rand")
        .param<CoreVM::CoreNumber>("min")
        .param<CoreVM::CoreNumber>("max")
        .returnType(CoreVM::LiteralType::Number), resolve, "rand", 2);

    // --- Size operations ---

    // size_from_bytes(n: number) -> number (Size record)
    bindResolved(rt.registerFunction("size_from_bytes")
        .param<CoreVM::CoreNumber>("n")
        .returnType(CoreVM::LiteralType::Number), resolve, "size_from_bytes", 1);

    // size_from_kb(n: number) -> number (Size record)
    bindResolved(rt.registerFunction("size_from_kb")
        .param<CoreVM::CoreNumber>("n")
        .returnType(CoreVM::LiteralType::Number), resolve, "size_from_kb", 1);

    // size_from_mb(n: number) -> number (Size record)
    bindResolved(rt.registerFunction("size_from_mb")
        .param<CoreVM::CoreNumber>("n")
        .returnType(CoreVM::LiteralType::Number), resolve, "size_from_mb", 1);

    // size_from_gb(n: number) -> number (Size record)
    bindResolved(rt.registerFunction("size_from_gb")
        .param<CoreVM::CoreNumber>("n")
        .returnType(CoreVM::LiteralType::Number), resolve, "size_from_gb", 1);

    // size_from_tb(n: number) -> number (Size record)
    bindResolved(rt.registerFunction("size_from_tb")
        .param<CoreVM::CoreNumber>("n")
        .returnType(CoreVM::LiteralType::Number), resolve, "size_from_tb", 1);

    // --- DateTime operations ---

    // datetime_now() -> number (DateTime record)
    bindResolved(rt.registerFunction("datetime_now")
        .returnType(CoreVM::LiteralType::Number), resolve, "datetime_now", 0);

    // datetime_from_epoch(epoch: number) -> number (DateTime record)
    bindResolved(rt.registerFunction("datetime_from_epoch")
        .param<CoreVM::CoreNumber>("epoch")
        .returnType(CoreVM::LiteralType::Number), resolve, "datetime_from_epoch", 1);

    // --- HTTP fetch ---

    // fetch(url: string) -> number (Result)
    bindResolved(rt.registerFunction("fetch")
        .param<CoreVM::CoreString>("url")
        .returnType(CoreVM::LiteralType::Number), resolve, "fetch", 1);

    // fetch(url: string, headers: number) -> number (Result)
    bindResolved(rt.registerFunction("fetch")
        .param<CoreVM::CoreString>("url")
        .param<CoreVM::CoreNumber>("headers")
        .returnType(CoreVM::LiteralType::Number), resolve, "fetch", 2);

    // clang-format on
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
                                  CoreVM::LiteralType type)
    {
        auto& prop = rt.registerProperty(std::string(name), type);

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
                                          CoreVM::LiteralType type)
    {
        auto& prop = rt.registerProperty(std::string(name), type);
        if (auto getterCb = resolve(name, 0))
            prop.onGet(*getterCb);
        else
            prop.onGet([](CoreVM::Params&) {});
        // No onSet() — property remains read-only
    }
} // namespace

void registerPromptPropertyBuiltins(CoreVM::Runtime& rt, CallbackResolver const& resolve)
{
    // clang-format off
    registerPropertyResolved(rt, resolve, "shell_prompt_preset", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "shell_prompt_indicator", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "shell_prompt_layout", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "shell_prompt_separator", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "shell_prompt_transient", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "shell_prompt_duration_threshold", CoreVM::LiteralType::Number);
    registerPropertyResolved(rt, resolve, "shell_prompt_spacing", CoreVM::LiteralType::Number);
    registerPropertyResolved(rt, resolve, "shell_exit_confirm_timeout", CoreVM::LiteralType::Number);
    registerPropertyResolved(rt, resolve, "shell_ls_icons", CoreVM::LiteralType::Boolean);
    registerPropertyResolved(rt, resolve, "shell_ls_directory_slash", CoreVM::LiteralType::Boolean);
    registerReadOnlyPropertyResolved(rt, resolve, "shell_is_interactive", CoreVM::LiteralType::Boolean);
    // clang-format on
}

// ---------------------------------------------------------------------------
// Agent configuration properties
// ---------------------------------------------------------------------------

void registerAgentConfigPropertyBuiltins(CoreVM::Runtime& rt, CallbackResolver const& resolve)
{
    // clang-format off

    // --- Top-level agent settings ---
    registerPropertyResolved(rt, resolve, "agent_provider", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_prompt_indicator", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_max_tool_result_size", CoreVM::LiteralType::Number);
    registerPropertyResolved(rt, resolve, "agent_log_tool_uses", CoreVM::LiteralType::Boolean);

    // --- Claude provider ---
    registerPropertyResolved(rt, resolve, "agent_claude_api_key", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_claude_api_key_env", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_claude_model", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_claude_max_tokens", CoreVM::LiteralType::Number);
    registerPropertyResolved(rt, resolve, "agent_claude_thinking_mode", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_claude_prompt_caching", CoreVM::LiteralType::Boolean);
    registerPropertyResolved(rt, resolve, "agent_claude_auth_type", CoreVM::LiteralType::String);

    // --- OpenAI provider ---
    registerPropertyResolved(rt, resolve, "agent_openai_api_key", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_openai_api_key_env", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_openai_model", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_openai_base_url", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_openai_max_tokens", CoreVM::LiteralType::Number);
    registerPropertyResolved(rt, resolve, "agent_openai_thinking_mode", CoreVM::LiteralType::String);

    // --- OpenAI-compatible provider ---
    registerPropertyResolved(rt, resolve, "agent_openai_compat_api_key", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_openai_compat_api_key_env", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_openai_compat_model", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_openai_compat_base_url", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_openai_compat_max_tokens", CoreVM::LiteralType::Number);
    registerPropertyResolved(rt, resolve, "agent_openai_compat_thinking_mode", CoreVM::LiteralType::String);

    // --- Gemini provider ---
    registerPropertyResolved(rt, resolve, "agent_gemini_api_key", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_gemini_api_key_env", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_gemini_model", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_gemini_max_tokens", CoreVM::LiteralType::Number);
    registerPropertyResolved(rt, resolve, "agent_gemini_thinking_mode", CoreVM::LiteralType::String);

    // --- Plan mode ---
    registerPropertyResolved(rt, resolve, "agent_plan_mode_enabled", CoreVM::LiteralType::Boolean);
    registerPropertyResolved(rt, resolve, "agent_plan_mode_pause_between_steps", CoreVM::LiteralType::Boolean);
    registerPropertyResolved(rt, resolve, "agent_plan_mode_max_exploration_turns", CoreVM::LiteralType::Number);

    // --- Explore sub-agent ---
    registerPropertyResolved(rt, resolve, "agent_explore_max_turns", CoreVM::LiteralType::Number);

    // --- Session ---
    registerPropertyResolved(rt, resolve, "agent_auto_resume", CoreVM::LiteralType::Boolean);
    registerPropertyResolved(rt, resolve, "agent_session_replay", CoreVM::LiteralType::Boolean);

    // --- Trace ---
    registerPropertyResolved(rt, resolve, "agent_trace_enabled", CoreVM::LiteralType::Boolean);
    registerPropertyResolved(rt, resolve, "agent_trace_default_path", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_trace_max_files", CoreVM::LiteralType::Number);

    // --- Permissions ---
    registerPropertyResolved(rt, resolve, "agent_permissions_policy", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_trusted_tool", CoreVM::LiteralType::Object);
    registerPropertyResolved(rt, resolve, "agent_blocked_pattern", CoreVM::LiteralType::Object);

    // --- Web search ---
    registerPropertyResolved(rt, resolve, "agent_web_search_engine", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_web_search_api_key", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_web_search_cx", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_web_search_max_results", CoreVM::LiteralType::Number);

    // --- Error recovery ---
    registerPropertyResolved(rt, resolve, "agent_error_recovery_action", CoreVM::LiteralType::String);
    registerPropertyResolved(rt, resolve, "agent_error_recovery_model", CoreVM::LiteralType::String);

    // clang-format on
}

// ---------------------------------------------------------------------------
// User-facing builtin names for shell completion
// ---------------------------------------------------------------------------

std::vector<std::string> userFacingBuiltinNames()
{
    return {
        // Shell builtins
        "cat",
        "cd",
        "exit",
        "export",
        "rm",
        "set",
        "unset",
        "read",
        "sleep",
        "true",
        "false",
        "jobs",
        "fg",
        "bg",
        "wait",
        "bind",
        "which",
        // Control flow keywords (also completable)
        "if",
        "then",
        "else",
        "elif",
        "for",
        "while",
        "do",
        "end",
        "in",
        "return",
        "break",
        "continue",
    };
}

} // namespace endo
