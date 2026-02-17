// SPDX-License-Identifier: Apache-2.0
#include <shell/CommandResolver.hpp>
#include <shell/OutputParser.hpp>
#include <shell/Platform.hpp>
#include <shell/Process.hpp>
#include <shell/PromptPresets.hpp>
#include <shell/Shell.hpp>
#include <shell/commands/JobsCommand.hpp>
#include <shell/commands/LsCommand.hpp>
#include <shell/commands/PsCommand.hpp>

#include <endo-language/BuiltinImpls.hpp>

#if defined(_WIN32)
    #include <shell/platform/WindowsFileInfoProvider.hpp>
    #include <shell/platform/WindowsProcessProvider.hpp>
#else
    #include <shell/platform/LinuxFileInfoProvider.hpp>
    #include <shell/platform/LinuxProcessProvider.hpp>
#endif

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace endo
{

void Shell::registerBuiltinFunctions()
{
    registerEnvironmentBuiltins();
    registerProcessBuiltins();
    registerIOBuiltins();
    registerCommandBuilderBuiltins();
    registerExpansionBuiltins();
    registerFlowControlBuiltins();
    registerJobControlBuiltins();
    registerUserCommandBuiltins();
    registerOutputBuiltins();
    registerLanguageBuiltins();
    registerStructuredBuiltins();
    registerPromptBuiltins();
}

void Shell::registerEnvironmentBuiltins()
{
    // clang-format off
    _runtime.registerFunction("exit")
        .param<CoreVM::CoreNumber>("code")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinExit, this);

    _runtime.registerFunction("export")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinExport, this);

    _runtime.registerFunction("export")
        .param<std::string>("name")
        .param<std::string>("value")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinSetAndExport, this);

    _runtime.registerFunction("cd")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinChDirHome, this);

    _runtime.registerFunction("cd")
        .param<std::string>("path")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinChDir, this);

    _runtime.registerFunction("set")
        .param<std::string>("name")
        .param<std::string>("value")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinSet, this);

    _runtime.registerFunction("unset")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinUnset, this);

    _runtime.registerFunction("getvar")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinGetVar, this);

    // F#-style env builtin: env.has checks existence, env.get retrieves the value
    _runtime.registerFunction("env.has")
        .param<CoreVM::CoreString>("key")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind([this](CoreVM::Params& args) {
            auto const& key = args.getString(1);
            args.setResult(_env.get(key).has_value());
        });

    _runtime.registerFunction("env.get")
        .param<CoreVM::CoreString>("key")
        .returnType(CoreVM::LiteralType::String)
        .bind([this](CoreVM::Params& args) {
            auto const& key = args.getString(1);
            auto val = _env.get(key);
            args.setResult(std::string(val.value_or("")));
        });

    _runtime.registerFunction("getvar.exitstatus")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinGetExitStatus, this);

    _runtime.registerFunction("setvar.exitstatus")
        .param<CoreVM::CoreNumber>("code")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinSetExitStatus, this);

    _runtime.registerFunction("getvar.processid")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinGetProcessId, this);

    _runtime.registerFunction("getvar.backgroundid")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinGetBackgroundId, this);

    _runtime.registerFunction("getvar.positional")
        .param<CoreVM::CoreNumber>("index")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinGetPositional, this);
    // clang-format on
}

void Shell::registerProcessBuiltins()
{
    // clang-format off
    _runtime.registerFunction("callproc")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinCallProcess, this);

    _runtime.registerFunction("callproc")
        .param<bool>("last_in_chain")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinCallProcessShellPiped, this);
    // clang-format on
}

void Shell::registerIOBuiltins()
{
    // clang-format off
    _runtime.registerFunction("read")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinReadDefault, this);

    _runtime.registerFunction("read")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinRead, this);

    _runtime.registerFunction("internal.open_read")
        .param<std::string>("path")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinOpenRead, this);

    _runtime.registerFunction("internal.open_write")
        .param<std::string>("path")
        .param<CoreVM::CoreNumber>("oflags")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinOpenWrite, this);

    _runtime.registerFunction("internal.redirect_start")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectStart, this);

    _runtime.registerFunction("internal.redirect_input")
        .param<CoreVM::CoreNumber>("target_fd")
        .param<std::string>("path")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectInput, this);

    _runtime.registerFunction("internal.redirect_output")
        .param<CoreVM::CoreNumber>("source_fd")
        .param<std::string>("path")
        .param<bool>("append")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectOutput, this);

    _runtime.registerFunction("internal.redirect_fd_dup")
        .param<CoreVM::CoreNumber>("source_fd")
        .param<CoreVM::CoreNumber>("target_fd")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectFdDup, this);

    _runtime.registerFunction("internal.redirect_heredoc")
        .param<CoreVM::CoreNumber>("target_fd")
        .param<std::string>("content")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectHeredoc, this);

    _runtime.registerFunction("internal.redirect_herestring")
        .param<CoreVM::CoreNumber>("target_fd")
        .param<std::string>("content")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectHerestring, this);

    _runtime.registerFunction("internal.redirect_end")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinRedirectEnd, this);

    _runtime.registerFunction("internal.subst_start")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinSubstStart, this);

    _runtime.registerFunction("internal.subst_end")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinSubstEnd, this);

    _runtime.registerFunction("internal.procsubst_fork")
        .param<bool>("is_write")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinProcSubstFork, this);

    _runtime.registerFunction("internal.procsubst_exit")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinProcSubstExit, this);

    _runtime.registerFunction("internal.procsubst_get_path")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinProcSubstGetPath, this);

    _runtime.registerFunction("internal.procsubst_cleanup")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinProcSubstCleanup, this);
    // clang-format on
}

void Shell::registerCommandBuilderBuiltins()
{
    // clang-format off
    _runtime.registerFunction("internal.cmd_start")
        .param<std::string>("program")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinCmdStart, this);

    _runtime.registerFunction("internal.cmd_arg")
        .param<std::string>("arg")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinCmdArg, this);

    _runtime.registerFunction("internal.cmd_exec")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinCmdExec, this);

    _runtime.registerFunction("internal.cmd_exec_piped")
        .param<bool>("last_in_chain")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinCmdExecPiped, this);

    _runtime.registerFunction("internal.cmd_exec_piped_background")
        .param<std::string>("command")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinCmdExecPipedBackground, this);
    // clang-format on
}

void Shell::registerExpansionBuiltins()
{
    // clang-format off
    _runtime.registerFunction("expand.tilde")
        .param<std::string>("suffix")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandTilde, this);

    _runtime.registerFunction("expand.tilde_user")
        .param<std::string>("user")
        .param<std::string>("suffix")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandTildeUser, this);

    _runtime.registerFunction("expand.glob")
        .param<std::string>("pattern")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinExpandGlob, this);

    _runtime.registerFunction("expand.arith_to_string")
        .param<CoreVM::CoreNumber>("value")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinArithToString, this);

    _runtime.registerFunction("expand.arith_getvar")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinArithGetVar, this);

    _runtime.registerFunction("expand.arith_pow")
        .param<CoreVM::CoreNumber>("base")
        .param<CoreVM::CoreNumber>("exp")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinArithPow, this);

    _runtime.registerFunction("expand.param_length")
        .param<std::string>("var")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamLength, this);

    _runtime.registerFunction("expand.param_default")
        .param<std::string>("var")
        .param<std::string>("default_value")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamDefault, this);

    _runtime.registerFunction("expand.param_alternate")
        .param<std::string>("var")
        .param<std::string>("alternate")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamAlternate, this);

    _runtime.registerFunction("expand.param_assign")
        .param<std::string>("var")
        .param<std::string>("default_value")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamAssign, this);

    _runtime.registerFunction("expand.param_error")
        .param<std::string>("var")
        .param<std::string>("error_msg")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamError, this);

    _runtime.registerFunction("expand.param_remove_prefix")
        .param<std::string>("var")
        .param<std::string>("pattern")
        .param<bool>("longest")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamRemovePrefix, this);

    _runtime.registerFunction("expand.param_remove_suffix")
        .param<std::string>("var")
        .param<std::string>("pattern")
        .param<bool>("longest")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamRemoveSuffix, this);

    _runtime.registerFunction("expand.param_replace")
        .param<std::string>("var")
        .param<std::string>("pattern")
        .param<std::string>("replacement")
        .param<bool>("all")
        .returnType(CoreVM::LiteralType::String)
        .bind(&Shell::builtinExpandParamReplace, this);
    // clang-format on
}

void Shell::registerFlowControlBuiltins()
{
    // clang-format off
    _runtime.registerFunction("internal.for_init")
        .param<std::string>("var")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinForInit, this);

    _runtime.registerFunction("internal.for_add_item")
        .param<std::string>("item")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinForAddItem, this);

    _runtime.registerFunction("internal.for_has_more")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinForHasMore, this);

    _runtime.registerFunction("internal.for_next")
        .param<std::string>("var")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinForNext, this);

    _runtime.registerFunction("internal.for_cleanup")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinForCleanup, this);

    _runtime.registerFunction("internal.case_match")
        .param<std::string>("word")
        .param<std::string>("pattern")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(&Shell::builtinCaseMatch, this);

    _runtime.registerFunction("internal.function_register")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinFunctionRegister, this);

    _runtime.registerFunction("internal.function_call")
        .param<std::string>("name")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinFunctionCall, this);
    // clang-format on
}

void Shell::registerJobControlBuiltins()
{
    // clang-format off
    _runtime.registerFunction("jobs")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinJobs, this);

    _runtime.registerFunction("fg")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinFg, this);

    _runtime.registerFunction("fg")
        .param<CoreVM::CoreNumber>("job_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinFg, this);

    _runtime.registerFunction("bg")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinBg, this);

    _runtime.registerFunction("bg")
        .param<CoreVM::CoreNumber>("job_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinBg, this);

    _runtime.registerFunction("wait")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinWait, this);

    _runtime.registerFunction("wait")
        .param<CoreVM::CoreNumber>("job_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinWait, this);
    // clang-format on
}

void Shell::registerUserCommandBuiltins()
{
    // clang-format off
    _runtime.registerFunction("bind")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinBind, this);

    _runtime.registerFunction("bind")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinBind, this);

    _runtime.registerFunction("which")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinWhich, this);

    _runtime.registerFunction("which")
        .param<std::vector<std::string>>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinWhich, this);

    // F# which builtin: returns Option<string> for program lookup via PATH
    _runtime.registerFunction("which_find")
        .param<CoreVM::CoreString>("program")
        .returnType(CoreVM::LiteralType::Number)
        .bind([this](CoreVM::Params& args) {
            auto const& program = args.getString(1);
            CommandResolver resolver(_env);
            auto const info = resolver.resolve(program);
            if (info.type == CommandType::External)
            {
                auto* pathStr = args.caller()->newString(info.tooltip);
                auto* some = args.caller()->makeSomeOption(reinterpret_cast<uintptr_t>(pathStr),
                                                           CoreVM::LiteralType::String);
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(some)));
            }
            else
            {
                auto* none = args.caller()->makeNoneOption();
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(none)));
            }
        });
    // clang-format on
}

void Shell::registerOutputBuiltins()
{
    // clang-format off
    _runtime.registerFunction("print")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinPrint, this);

    _runtime.registerFunction("println")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinPrintln, this);

    // Bare expression display: auto-display a value with table rendering for lists of records
    _runtime.registerFunction("display_result")
        .param<CoreVM::CoreNumber>("value")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinDisplayResult, this);

    // HTTP fetch builtins: fetch(url) and fetch(url, headers)
    _runtime.registerFunction("fetch")
        .param<CoreVM::CoreString>("url")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinFetch, this);

    _runtime.registerFunction("fetch")
        .param<CoreVM::CoreString>("url")
        .param<CoreVM::CoreNumber>("headers")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinFetchWithHeaders, this);
    // clang-format on
}

void Shell::registerLanguageBuiltins()
{
    // clang-format off
    // F# list/string/object builtins — delegate to shared implementations (BuiltinImpls.hpp)
    _runtime.registerFunction("list_to_string")
        .param<CoreVM::CoreNumber>("obj")
        .returnType(CoreVM::LiteralType::String)
        .bind(endo::builtins::listToString);

    _runtime.registerFunction("list_concat")
        .param<CoreVM::CoreNumber>("left")
        .param<CoreVM::CoreNumber>("right")
        .returnType(CoreVM::LiteralType::Number)
        .bind(endo::builtins::listConcat);

    _runtime.registerFunction("list_head")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind(endo::builtins::listHead);

    _runtime.registerFunction("list_tail")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind(endo::builtins::listTail);

    _runtime.registerFunction("list_length")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind(endo::builtins::listLength);

    _runtime.registerFunction("list_isEmpty")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(endo::builtins::listIsEmpty);

    _runtime.registerFunction("list_sort")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind(endo::builtins::listSort);

    _runtime.registerFunction("list_distinct")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind(endo::builtins::listDistinct);

    _runtime.registerFunction("list_sort_pairs")
        .param<CoreVM::CoreNumber>("pairs")
        .returnType(CoreVM::LiteralType::Number)
        .bind(endo::builtins::listSortPairs);

    _runtime.registerFunction("list_group_pairs")
        .param<CoreVM::CoreNumber>("pairs")
        .returnType(CoreVM::LiteralType::Number)
        .bind(endo::builtins::listGroupPairs);

    _runtime.registerFunction("list_nth")
        .param<CoreVM::CoreNumber>("index")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind(endo::builtins::listNth);

    _runtime.registerFunction("list_last")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::Number)
        .bind(endo::builtins::listLast);

    _runtime.registerFunction("list_replicate")
        .param<CoreVM::CoreNumber>("count")
        .param<CoreVM::CoreNumber>("value")
        .returnType(CoreVM::LiteralType::Number)
        .bind(endo::builtins::listReplicate);

    _runtime.registerFunction("list_char_range")
        .param<CoreVM::CoreNumber>("startOrd")
        .param<CoreVM::CoreNumber>("endOrd")
        .returnType(CoreVM::LiteralType::Number)
        .bind(endo::builtins::listCharRange);

    _runtime.registerFunction("object_to_string")
        .param<CoreVM::CoreNumber>("obj")
        .returnType(CoreVM::LiteralType::String)
        .bind(endo::builtins::objectToString);

    _runtime.registerFunction("string_repeat")
        .param<CoreVM::CoreString>("str")
        .param<CoreVM::CoreNumber>("count")
        .returnType(CoreVM::LiteralType::String)
        .bind(endo::builtins::stringRepeat);

    _runtime.registerFunction("string_trim")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::String)
        .bind(endo::builtins::stringTrim);

    _runtime.registerFunction("string_toLower")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::String)
        .bind(endo::builtins::stringToLower);

    _runtime.registerFunction("string_toUpper")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::String)
        .bind(endo::builtins::stringToUpper);

    _runtime.registerFunction("string_contains")
        .param<CoreVM::CoreString>("haystack")
        .param<CoreVM::CoreString>("needle")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(endo::builtins::stringContains);

    _runtime.registerFunction("string_startsWith")
        .param<CoreVM::CoreString>("text")
        .param<CoreVM::CoreString>("prefix")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(endo::builtins::stringStartsWith);

    _runtime.registerFunction("string_endsWith")
        .param<CoreVM::CoreString>("text")
        .param<CoreVM::CoreString>("suffix")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(endo::builtins::stringEndsWith);

    _runtime.registerFunction("string_replace")
        .param<CoreVM::CoreString>("old_str")
        .param<CoreVM::CoreString>("new_str")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::String)
        .bind(endo::builtins::stringReplace);

    _runtime.registerFunction("string_split")
        .param<CoreVM::CoreString>("delimiter")
        .param<CoreVM::CoreString>("text")
        .returnType(CoreVM::LiteralType::Number)
        .bind(endo::builtins::stringSplit);

    _runtime.registerFunction("string_join")
        .param<CoreVM::CoreString>("separator")
        .param<CoreVM::CoreNumber>("list")
        .returnType(CoreVM::LiteralType::String)
        .bind(endo::builtins::stringJoin);

    // Helper builtins for FileInfo mode/mtime formatting and testing
    _runtime.registerFunction("format_datetime")
        .param<CoreVM::CoreNumber>("epoch")
        .returnType(CoreVM::LiteralType::String)
        .bind(endo::builtins::formatDatetime);

    _runtime.registerFunction("format_mode")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::String)
        .bind(endo::builtins::formatMode);

    _runtime.registerFunction("mode_isReadable")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(endo::builtins::modeIsReadable);

    _runtime.registerFunction("mode_isWritable")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(endo::builtins::modeIsWritable);

    _runtime.registerFunction("mode_isExecutable")
        .param<CoreVM::CoreNumber>("mode")
        .returnType(CoreVM::LiteralType::Boolean)
        .bind(endo::builtins::modeIsExecutable);

    // F# rand builtins
    _runtime.registerFunction("rand")
        .returnType(CoreVM::LiteralType::Number)
        .bind(endo::builtins::randNoArgs);

    _runtime.registerFunction("rand")
        .param<CoreVM::CoreNumber>("min")
        .param<CoreVM::CoreNumber>("max")
        .returnType(CoreVM::LiteralType::Number)
        .bind(endo::builtins::randRange);
    // clang-format on
}

void Shell::registerStructuredBuiltins()
{
    // clang-format off
    // F# structured_ps builtin: returns list<ProcessInfo> from platform process provider
    _runtime.registerFunction("structured_ps")
        .returnType(CoreVM::LiteralType::Number)
        .bind([this](CoreVM::Params& args) {
#if defined(_WIN32)
            WindowsProcessProvider provider;
#else
            LinuxProcessProvider provider;
#endif
            PsCommand cmd(provider);
            auto* result = cmd.execute(*_runner);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        });

    // F# structured_ls builtin: returns list<FileInfo> from platform file info provider
    _runtime.registerFunction("structured_ls")
        .param<CoreVM::CoreString>("path")
        .returnType(CoreVM::LiteralType::Number)
        .bind([this](CoreVM::Params& args) {
            auto const path = args.getString(1);
#if defined(_WIN32)
            WindowsFileInfoProvider provider;
#else
            LinuxFileInfoProvider provider;
#endif
            LsCommand cmd(provider, std::string(path));
            auto* result = cmd.execute(*_runner);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        });

    // F# structured_jobs builtin: returns list<JobInfo> from the shell's job table
    _runtime.registerFunction("structured_jobs")
        .returnType(CoreVM::LiteralType::Number)
        .bind([this](CoreVM::Params& args) {
            // Bridge JobTable to JobProvider interface inline
            class ShellJobProvider final: public JobProvider
            {
              public:
                explicit ShellJobProvider(JobTable const& table): _table(table) {}
                [[nodiscard]] std::vector<JobEntry> listJobs() const override
                {
                    std::vector<JobEntry> result;
                    for (auto const* job: _table.listJobs())
                    {
                        result.push_back(JobEntry {
                            .id = job->id,
                            .state = std::string(toString(job->state)),
                            .command = job->command,
                            .pid = static_cast<int64_t>(job->pgid),
                        });
                    }
                    return result;
                }

              private:
                JobTable const& _table;
            };

            ShellJobProvider provider(jobTable);
            JobsCommand cmd(provider);
            auto* result = cmd.execute(*_runner);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        });

    // Register structured command callbacks for output definitions
    for (auto const& def: _outputDefinitions.definitions())
    {
        for (auto const& variant: def.variants)
        {
            auto const callbackName = "structured_" + variant.fsharpName;
            auto const* variantPtr = &variant;
            auto const command = def.command;

            _runtime.registerFunction(callbackName)
                .returnType(CoreVM::LiteralType::Number)
                .bind([this, variantPtr, command](CoreVM::Params& args) {
                    // Build the command to run
                    auto const& cmd = variantPtr->commandToRun.value_or(command);

                    // Execute the command and capture stdout via pipe
                    auto pipeResult = createPipe();
                    if (!pipeResult.has_value())
                    {
                        auto* nil = args.caller()->allocObject(CoreVM::BuiltinTypeId::List);
                        nil->tag = 0;
                        args.setResult(
                            static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(nil)));
                        return;
                    }
                    auto& pipe = pipeResult.value();

                    SpawnConfig config;
                    config.program = "/bin/sh";
                    config.arguments = { "sh", "-c", cmd };
                    config.stdinFd = _tty.inputFd();
                    config.stdoutFd = pipe->writer();
                    config.stderrFd = standardError();

                    auto pidResult = _processManager.spawn(config);
                    pipe->closeWriter();

                    // Read all output
                    std::string output;
                    char buf[4096];
                    while (true)
                    {
                        auto const n = platformRead(pipe->reader(), buf, sizeof(buf));
                        if (n <= 0)
                            break;
                        output.append(buf, static_cast<size_t>(n));
                    }
                    pipe->closeReader();

                    if (pidResult.has_value())
                        (void) _processManager.wait(*pidResult);

                    // Parse the output into structured records
                    CoreVM::TypedObject* result = nullptr;
                    if (variantPtr->parser.type == ParserConfig::Type::Json)
                        result = OutputParser::parseJson(*args.caller(), output, *variantPtr);
                    else
                        result = OutputParser::parseFields(*args.caller(), output, *variantPtr);

                    args.setResult(
                        static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
                });
        }
    }

    // --- Data source wrappers (open-json, open-csv, from-json, from-csv) ---

    _runtime.registerFunction("open_json")
        .param<CoreVM::CoreString>("path")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind([this](CoreVM::Params& args) {
            auto const& filePath = args.getString(1);
            auto const& schemaDesc = args.getString(2);
            auto const typeId = static_cast<uint16_t>(args.getInt(3));

            // Read file contents
            std::string contents;
            try
            {
                auto const path = std::filesystem::path(filePath);
                if (std::filesystem::exists(path))
                {
                    auto ifs = std::ifstream(path);
                    contents.assign(std::istreambuf_iterator<char>(ifs),
                                    std::istreambuf_iterator<char>());
                }
            }
            catch (...)
            {
            }

            // Build variant from schema descriptor
            auto variant = OutputParser::buildVariantFromDesc(schemaDesc, typeId, ParserConfig::Type::Json);

            // Auto-detect JSON format: first non-whitespace '[' → Array, else → Lines
            auto const firstNonWs = contents.find_first_not_of(" \t\r\n");
            if (firstNonWs != std::string::npos && contents[firstNonWs] == '[')
                variant.parser.format = ParserConfig::Format::Array;
            else
                variant.parser.format = ParserConfig::Format::Lines;

            auto* result = OutputParser::parseJson(*args.caller(), contents, variant);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        });

    _runtime.registerFunction("open_csv")
        .param<CoreVM::CoreString>("path")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind([this](CoreVM::Params& args) {
            auto const& filePath = args.getString(1);
            auto const& schemaDesc = args.getString(2);
            auto const typeId = static_cast<uint16_t>(args.getInt(3));

            // Read file contents
            std::string contents;
            try
            {
                auto const path = std::filesystem::path(filePath);
                if (std::filesystem::exists(path))
                {
                    auto ifs = std::ifstream(path);
                    contents.assign(std::istreambuf_iterator<char>(ifs),
                                    std::istreambuf_iterator<char>());
                }
            }
            catch (...)
            {
            }

            auto variant = OutputParser::buildVariantFromDesc(schemaDesc, typeId, ParserConfig::Type::Fields);

            // Auto-detect CSV header: check if first line matches schema field names
            if (!contents.empty())
            {
                auto const firstNewline = contents.find('\n');
                auto const firstLine = contents.substr(0, firstNewline);
                if (OutputParser::detectCsvHeader(firstLine, variant.parser.fieldSeparator, variant.schema))
                {
                    // Skip the header line
                    if (firstNewline != std::string::npos)
                        contents = contents.substr(firstNewline + 1);
                    else
                        contents.clear();
                }
            }

            auto* result = OutputParser::parseFields(*args.caller(), contents, variant);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        });

    _runtime.registerFunction("from_json")
        .param<CoreVM::CoreString>("source_cmd")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind([this](CoreVM::Params& args) {
            auto const& sourceCmd = args.getString(1);
            auto const& schemaDesc = args.getString(2);
            auto const typeId = static_cast<uint16_t>(args.getInt(3));

            // Spawn source command and capture output
            std::string output;
            if (!sourceCmd.empty())
            {
                auto pipeResult = createPipe();
                if (pipeResult.has_value())
                {
                    auto& pipe = pipeResult.value();
                    SpawnConfig config;
                    config.program = "/bin/sh";
                    config.arguments = { "sh", "-c", std::string(sourceCmd) };
                    config.stdinFd = _tty.inputFd();
                    config.stdoutFd = pipe->writer();
                    config.stderrFd = standardError();

                    auto pidResult = _processManager.spawn(config);
                    pipe->closeWriter();

                    char buf[4096];
                    while (true)
                    {
                        auto const n = platformRead(pipe->reader(), buf, sizeof(buf));
                        if (n <= 0)
                            break;
                        output.append(buf, static_cast<size_t>(n));
                    }
                    pipe->closeReader();
                    if (pidResult.has_value())
                        (void) _processManager.wait(*pidResult);
                }
            }

            auto variant = OutputParser::buildVariantFromDesc(schemaDesc, typeId, ParserConfig::Type::Json);

            auto const firstNonWs = output.find_first_not_of(" \t\r\n");
            if (firstNonWs != std::string::npos && output[firstNonWs] == '[')
                variant.parser.format = ParserConfig::Format::Array;
            else
                variant.parser.format = ParserConfig::Format::Lines;

            auto* result = OutputParser::parseJson(*args.caller(), output, variant);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        });

    _runtime.registerFunction("from_csv")
        .param<CoreVM::CoreString>("source_cmd")
        .param<CoreVM::CoreString>("schema_desc")
        .param<CoreVM::CoreNumber>("type_id")
        .returnType(CoreVM::LiteralType::Number)
        .bind([this](CoreVM::Params& args) {
            auto const& sourceCmd = args.getString(1);
            auto const& schemaDesc = args.getString(2);
            auto const typeId = static_cast<uint16_t>(args.getInt(3));

            // Spawn source command and capture output
            std::string output;
            if (!sourceCmd.empty())
            {
                auto pipeResult = createPipe();
                if (pipeResult.has_value())
                {
                    auto& pipe = pipeResult.value();
                    SpawnConfig config;
                    config.program = "/bin/sh";
                    config.arguments = { "sh", "-c", std::string(sourceCmd) };
                    config.stdinFd = _tty.inputFd();
                    config.stdoutFd = pipe->writer();
                    config.stderrFd = standardError();

                    auto pidResult = _processManager.spawn(config);
                    pipe->closeWriter();

                    char buf[4096];
                    while (true)
                    {
                        auto const n = platformRead(pipe->reader(), buf, sizeof(buf));
                        if (n <= 0)
                            break;
                        output.append(buf, static_cast<size_t>(n));
                    }
                    pipe->closeReader();
                    if (pidResult.has_value())
                        (void) _processManager.wait(*pidResult);
                }
            }

            auto variant = OutputParser::buildVariantFromDesc(schemaDesc, typeId, ParserConfig::Type::Fields);

            // Auto-detect CSV header
            if (!output.empty())
            {
                auto const firstNewline = output.find('\n');
                auto const firstLine = output.substr(0, firstNewline);
                if (OutputParser::detectCsvHeader(firstLine, variant.parser.fieldSeparator, variant.schema))
                {
                    if (firstNewline != std::string::npos)
                        output = output.substr(firstNewline + 1);
                    else
                        output.clear();
                }
            }

            auto* result = OutputParser::parseFields(*args.caller(), output, variant);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        });
    // clang-format on
}

void Shell::registerPromptBuiltins()
{
    // clang-format off
    _runtime.registerFunction("set_prompt_preset")
        .param<CoreVM::CoreString>("name")
        .returnType(CoreVM::LiteralType::Void)
        .bind([this](CoreVM::Params& args) {
            auto const& name = args.getString(1);
            prompt.setPromptConfig(promptPreset(name));
        });

    _runtime.registerFunction("set_prompt_indicator")
        .param<CoreVM::CoreString>("chars")
        .returnType(CoreVM::LiteralType::Void)
        .bind([this](CoreVM::Params& args) {
            auto config = prompt.promptConfig();
            config.indicator = std::string(args.getString(1)) + " ";
            prompt.setPromptConfig(std::move(config));
        });

    _runtime.registerFunction("set_prompt_layout")
        .param<CoreVM::CoreString>("kind")
        .returnType(CoreVM::LiteralType::Void)
        .bind([this](CoreVM::Params& args) {
            auto const& kind = args.getString(1);
            auto config = prompt.promptConfig();
            if (kind == "single-line") config.layout = PromptLayoutKind::SingleLine;
            else if (kind == "two-line") config.layout = PromptLayoutKind::TwoLine;
            else if (kind == "boxed") config.layout = PromptLayoutKind::Boxed;
            else if (kind == "powerline") config.layout = PromptLayoutKind::Powerline;
            prompt.setPromptConfig(std::move(config));
        });

    _runtime.registerFunction("set_prompt_separator")
        .param<CoreVM::CoreString>("style")
        .returnType(CoreVM::LiteralType::Void)
        .bind([this](CoreVM::Params& args) {
            auto const& style = args.getString(1);
            auto config = prompt.promptConfig();
            if (style == "none") config.separator = SeparatorStyle::None;
            else if (style == "bar") config.separator = SeparatorStyle::Bar;
            else if (style == "powerline") config.separator = SeparatorStyle::Powerline;
            else if (style == "rounded") config.separator = SeparatorStyle::Rounded;
            else if (style == "boxed") config.separator = SeparatorStyle::Boxed;
            prompt.setPromptConfig(std::move(config));
        });

    _runtime.registerFunction("set_prompt_transient")
        .param<CoreVM::CoreString>("mode")
        .returnType(CoreVM::LiteralType::Void)
        .bind([this](CoreVM::Params& args) {
            auto const& mode = args.getString(1);
            auto config = prompt.promptConfig();
            if (mode == "off") config.transient = TransientMode::Off;
            else if (mode == "minimal") config.transient = TransientMode::Minimal;
            else if (mode == "arrow") config.transient = TransientMode::Arrow;
            prompt.setPromptConfig(std::move(config));
        });

    _runtime.registerFunction("set_prompt_duration_threshold")
        .param<CoreVM::CoreNumber>("ms")
        .returnType(CoreVM::LiteralType::Void)
        .bind([this](CoreVM::Params& args) {
            auto config = prompt.promptConfig();
            config.durationThresholdMs = args.getInt(1);
            prompt.setPromptConfig(std::move(config));
        });

    _runtime.registerFunction("set_prompt_spacing")
        .param<CoreVM::CoreNumber>("lines")
        .returnType(CoreVM::LiteralType::Void)
        .bind([this](CoreVM::Params& args) {
            auto config = prompt.promptConfig();
            config.promptSpacing =
                static_cast<int>(std::clamp(args.getInt(1), int64_t { 0 }, int64_t { 1 }));
            prompt.setPromptConfig(std::move(config));
        });
    // clang-format on
}

} // namespace endo
