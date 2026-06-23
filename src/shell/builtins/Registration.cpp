// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.hpp>
#include <shell/commands/BindCommand.hpp>
#include <shell/commands/FindCommand.hpp>
#include <shell/commands/FindExpression.hpp>
#include <shell/commands/JobsCommand.hpp>
#include <shell/commands/LsCommand.hpp>
#include <shell/commands/PsCommand.hpp>
#include <shell/output/OutputParser.hpp>
#include <shell/ui/PromptColorResolver.hpp>
#include <shell/ui/PromptPresets.hpp>
#include <shell/util/CommandResolver.hpp>

#include <endo-language/builtins/BuiltinImpls.hpp>
#include <endo-language/builtins/BuiltinSignatures.hpp>

#include <CoreVM/types/TypeDescriptor.hpp>
#include <CoreVM/types/TypedObject.hpp>

#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
    #include <agent/auth/OAuthFlow.hpp>
    #include <agent/providers/ProviderFactory.hpp>
    #include <agent/tools/WebSearchTool.hpp>
#endif
#include <platform/Process.hpp>
#include <platform/Types.hpp>

#if defined(_WIN32)
    #include <platform/windows/WindowsFileInfoProvider.hpp>
    #include <platform/windows/WindowsProcessProvider.hpp>
#elif defined(__APPLE__)
    #include <platform/darwin/DarwinProcessProvider.hpp>
    #include <platform/linux/LinuxFileInfoProvider.hpp>
#else
    #include <platform/linux/LinuxFileInfoProvider.hpp>
    #include <platform/linux/LinuxProcessProvider.hpp>
#endif

#include <algorithm>
#include <ranges>
#include <sstream>

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
    registerAgentConfigBuiltins();
    registerMcpBuiltins();
    registerCompleterBuiltins();
    registerDirectoryConfigBuiltins();
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
            CommandResolver resolver(_env, _fs);
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

    // Markdown render builtin
    _runtime.registerFunction("markdown_render")
        .param<CoreVM::CoreNumber>("md")
        .returnType(CoreVM::LiteralType::Void)
        .bind(&Shell::builtinMarkdownRender, this);

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
    // Delegate to the unified descriptor-driven registration.
    // Shell-specific builtins (print, println, env.has, env.get, which_find, export,
    // display_result, fetch) are already registered by other Shell registration functions
    // (registerOutputBuiltins, registerEnvironmentBuiltins, etc.) which run first.
    // Runtime::find() returns the first match, so those take precedence.
    endo::registerFSharpBuiltins(
        _runtime, [](std::string_view name, size_t arity) -> std::optional<CoreVM::NativeCallback::Functor> {
            return endo::builtins::resolveSharedImpl(name, arity);
        });
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
#elif defined(__APPLE__)
            DarwinProcessProvider provider;
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

    // F# structured_bind builtin: returns list<KeyBindingInfo> from keybindings
    _runtime.registerFunction("structured_bind")
        .returnType(CoreVM::LiteralType::Number)
        .bind([this](CoreVM::Params& args) {
            BindCommand cmd(prompt.keyBindings());
            auto* result = cmd.execute(*_runner);
            args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(result)));
        });

    // F# structured_find builtin: returns list<FileInfo> from find command
    _runtime.registerFunction("structured_find")
        .param<CoreVM::CoreString>("args")
        .returnType(CoreVM::LiteralType::Number)
        .bind([this](CoreVM::Params& args) {
            auto const argsStr = std::string(args.getString(1));
            // Split null-separated args string
            std::vector<std::string> findArgs;
            size_t start = 0;
            for (size_t i = 0; i <= argsStr.size(); ++i)
            {
                if (i == argsStr.size() || argsStr[i] == '\0')
                {
                    if (i > start)
                        findArgs.emplace_back(argsStr.substr(start, i - start));
                    start = i + 1;
                }
            }
            auto parsed = find::parseFindArgs(findArgs);
            if (!parsed.has_value())
            {
                // On parse error, return empty list
                auto* list = _runner->allocObject(CoreVM::BuiltinTypeId::List);
                list->tag = 0; // Nil
                args.setResult(static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
                return;
            }
            auto& [options, expression] = parsed.value();
            FindCommand cmd(std::move(options), std::move(expression));
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
                        (void) _processManager.wait(*pidResult); // NOLINT(bugprone-unused-return-value)

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
            if (auto const result = _fs.readFile(filePath); result.has_value())
                contents = *result;

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
            if (auto const result = _fs.readFile(filePath); result.has_value())
                contents = *result;

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
                        (void) _processManager.wait(*pidResult); // NOLINT(bugprone-unused-return-value)
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
                        (void) _processManager.wait(*pidResult); // NOLINT(bugprone-unused-return-value)
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
    _runtime.registerProperty("shell_prompt_preset", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) {
            args.setResult(std::string(prompt.promptConfig().name));
        })
        .onSet([this](CoreVM::Params& args) {
            auto const& name = args.getString(1);
            prompt.setPromptConfig(promptPreset(name, prompt.terminal().colorScheme()));
        });

    _runtime.registerProperty("shell_prompt_indicator", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) {
            // When the user has assigned a zero-argument function, the getter
            // reflects the currently-effective value by invoking it. This matches
            // what the prompt actually renders and avoids the surprising case
            // where a successful `<- fun () -> "…"` leaves the getter reporting
            // the prior static string.
            auto const& config = prompt.promptConfig();
            if (config.indicatorFn.has_value())
            {
                if (auto result = invokePromptCallback(*config.indicatorFn))
                {
                    args.setResult(std::move(*result));
                    return;
                }
            }
            args.setResult(std::string(config.indicator));
        })
        .onSet([this](CoreVM::Params& args) {
            auto config = prompt.promptConfig();
            config.indicator = std::string(args.getString(1)) + " ";
            config.indicatorFn.reset(); // Static string clears any prior dynamic override.
            prompt.setPromptConfig(std::move(config));
        })
        // Function overload: `shell_prompt_indicator <- myFn` where `myFn : () -> string`.
        // Stores the function's user-facing name (without the `fsharp.` compiler prefix);
        // the prompt render path looks it up each render via the Shell's invocation helper.
        .onSet(CoreVM::LiteralType::Function, [this](CoreVM::Params& args) {
            auto config = prompt.promptConfig();
            auto const* fn = args.getFunction(1);
            if (fn)
            {
                auto const& fullName = fn->name();
                auto const prefix = std::string_view { "fsharp." };
                auto const name = fullName.starts_with(prefix)
                                      ? fullName.substr(prefix.size())
                                      : fullName;
                config.indicatorFn = std::string(name);
            }
            else
            {
                config.indicatorFn.reset();
            }
            prompt.setPromptConfig(std::move(config));
        });
    _runtime.registerPropertySetterOverload("shell_prompt_indicator", CoreVM::LiteralType::Function);

    _runtime.registerProperty("shell_prompt_layout", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) {
            auto const& config = prompt.promptConfig();
            switch (config.layout) {
                case PromptLayoutKind::SingleLine: args.setResult(std::string("single-line")); break;
                case PromptLayoutKind::TwoLine: args.setResult(std::string("two-line")); break;
                case PromptLayoutKind::Boxed: args.setResult(std::string("boxed")); break;
                case PromptLayoutKind::Powerline: args.setResult(std::string("powerline")); break;
            }
        })
        .onSet([this](CoreVM::Params& args) {
            auto const& kind = args.getString(1);
            auto config = prompt.promptConfig();
            if (kind == "single-line") config.layout = PromptLayoutKind::SingleLine;
            else if (kind == "two-line") config.layout = PromptLayoutKind::TwoLine;
            else if (kind == "boxed") config.layout = PromptLayoutKind::Boxed;
            else if (kind == "powerline") config.layout = PromptLayoutKind::Powerline;
            prompt.setPromptConfig(std::move(config));
        });

    _runtime.registerProperty("shell_prompt_separator", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) {
            auto const& config = prompt.promptConfig();
            switch (config.separator) {
                case SeparatorStyle::None: args.setResult(std::string("none")); break;
                case SeparatorStyle::Bar: args.setResult(std::string("bar")); break;
                case SeparatorStyle::Powerline: args.setResult(std::string("powerline")); break;
                case SeparatorStyle::Rounded: args.setResult(std::string("rounded")); break;
                case SeparatorStyle::Boxed: args.setResult(std::string("boxed")); break;
            }
        })
        .onSet([this](CoreVM::Params& args) {
            auto const& style = args.getString(1);
            auto config = prompt.promptConfig();
            if (style == "none") config.separator = SeparatorStyle::None;
            else if (style == "bar") config.separator = SeparatorStyle::Bar;
            else if (style == "powerline") config.separator = SeparatorStyle::Powerline;
            else if (style == "rounded") config.separator = SeparatorStyle::Rounded;
            else if (style == "boxed") config.separator = SeparatorStyle::Boxed;
            prompt.setPromptConfig(std::move(config));
        });

    _runtime.registerProperty("shell_prompt_transient", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) {
            auto const& config = prompt.promptConfig();
            switch (config.transient) {
                case TransientMode::Off: args.setResult(std::string("off")); break;
                case TransientMode::Minimal: args.setResult(std::string("minimal")); break;
                case TransientMode::Arrow: args.setResult(std::string("arrow")); break;
            }
        })
        .onSet([this](CoreVM::Params& args) {
            auto const& mode = args.getString(1);
            auto config = prompt.promptConfig();
            if (mode == "off") config.transient = TransientMode::Off;
            else if (mode == "minimal") config.transient = TransientMode::Minimal;
            else if (mode == "arrow") config.transient = TransientMode::Arrow;
            prompt.setPromptConfig(std::move(config));
        });

    _runtime.registerProperty("shell_prompt_duration_threshold", CoreVM::LiteralType::Number)
        .onGet([this](CoreVM::Params& args) {
            args.setResult(static_cast<CoreVM::CoreNumber>(prompt.promptConfig().durationThresholdMs));
        })
        .onSet([this](CoreVM::Params& args) {
            auto config = prompt.promptConfig();
            config.durationThresholdMs = args.getInt(1);
            prompt.setPromptConfig(std::move(config));
        });

    _runtime.registerProperty("shell_prompt_spacing", CoreVM::LiteralType::Number)
        .onGet([this](CoreVM::Params& args) {
            args.setResult(static_cast<CoreVM::CoreNumber>(prompt.promptConfig().promptSpacing));
        })
        .onSet([this](CoreVM::Params& args) {
            auto config = prompt.promptConfig();
            config.promptSpacing =
                static_cast<int>(std::clamp(args.getInt(1), int64_t { 0 }, int64_t { 1 }));
            prompt.setPromptConfig(std::move(config));
        });

    _runtime.registerProperty("shell_exit_confirm_timeout", CoreVM::LiteralType::Number)
        .onGet([this](CoreVM::Params& args) {
            args.setResult(static_cast<CoreVM::CoreNumber>(prompt.promptConfig().exitConfirmTimeoutMs));
        })
        .onSet([this](CoreVM::Params& args) {
            auto config = prompt.promptConfig();
            config.exitConfirmTimeoutMs = std::max(int64_t { 0 }, args.getInt(1));
            prompt.setPromptConfig(std::move(config));
        });

    _runtime.registerProperty("shell_ls_icons", CoreVM::LiteralType::Boolean)
        .onGet([this](CoreVM::Params& args) { args.setResult(_lsIcons); })
        .onSet([this](CoreVM::Params& args) { _lsIcons = args.getBool(1); });

    _runtime.registerProperty("shell_ls_directory_slash", CoreVM::LiteralType::Boolean)
        .onGet([this](CoreVM::Params& args) { args.setResult(_lsDirectorySlash); })
        .onSet([this](CoreVM::Params& args) { _lsDirectorySlash = args.getBool(1); });

    _runtime.registerProperty("shell_is_interactive", CoreVM::LiteralType::Boolean)
        .onGet([this](CoreVM::Params& args) { args.setResult(_interactive); });

    // --- Prompt color overrides ---

    // Background (special: supports "transparent")
    _runtime.registerProperty("shell_prompt_color_background", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) {
            auto const& ov = prompt.promptConfig().colorOverrides;
            if (ov.transparentBackground)
                args.setResult(std::string("transparent"));
            else if (ov.background)
                args.setResult(formatColorSpec(*ov.background));
            else
                args.setResult(std::string("theme"));
        })
        .onSet([this](CoreVM::Params& args) {
            auto const& value = args.getString(1);
            auto config = prompt.promptConfig();
            if (value == "transparent" || value == "default")
            {
                config.colorOverrides.transparentBackground = true;
                config.colorOverrides.background.reset();
            }
            else if (value == "theme" || value == "reset")
            {
                config.colorOverrides.transparentBackground = false;
                config.colorOverrides.background.reset();
            }
            else if (auto parsed = parseColorSpec(value))
            {
                config.colorOverrides.transparentBackground = false;
                config.colorOverrides.background = *parsed;
            }
            prompt.setPromptConfig(std::move(config));
        });

    // Table-driven registration for non-background color properties.
    // Each property accepts either a static string ("#RRGGBB", gradient, "theme")
    // or a zero-arg F# function returning such a string. The function form is
    // invoked at every prompt render, enabling context-sensitive colors (e.g.
    // exit-code-driven indicator color).
    auto registerColorProp = [this](
        char const* name,
        std::string_view colorKey,
        std::optional<ColorSpec> PromptColorOverrides::* field)
    {
        auto const key = std::string { colorKey }; // captured by value in each lambda
        _runtime.registerProperty(name, CoreVM::LiteralType::String)
            .onGet([this, field, key](CoreVM::Params& args) {
                auto const& ov = prompt.promptConfig().colorOverrides;
                // Dynamic form wins: reflect the currently-effective color by invoking
                // the user-assigned function, so reading the property matches what
                // the prompt renders.
                if (auto it = ov.colorFns.find(key); it != ov.colorFns.end())
                {
                    if (auto result = invokePromptCallback(it->second))
                    {
                        args.setResult(std::move(*result));
                        return;
                    }
                }
                args.setResult((ov.*field) ? formatColorSpec(*(ov.*field)) : std::string("theme"));
            })
            .onSet([this, field, key](CoreVM::Params& args) {
                auto const& value = args.getString(1);
                auto config = prompt.promptConfig();
                auto assigned = false;
                if (value == "theme" || value == "reset")
                {
                    (config.colorOverrides.*field).reset();
                    assigned = true;
                }
                else if (auto parsed = parseColorSpec(value))
                {
                    config.colorOverrides.*field = *parsed;
                    assigned = true;
                }
                // Only clear a prior dynamic resolver when the static assignment actually
                // took effect — an unparseable string (typo) should not silently drop the
                // user's previously-configured function.
                if (assigned)
                    config.colorOverrides.colorFns.erase(key);
                prompt.setPromptConfig(std::move(config));
            })
            // Function overload: `shell_prompt_color_X <- myFn` where `myFn : () -> string`.
            .onSet(CoreVM::LiteralType::Function, [this, key](CoreVM::Params& args) {
                auto config = prompt.promptConfig();
                auto const* fn = args.getFunction(1);
                if (fn)
                {
                    auto const& fullName = fn->name();
                    auto const prefix = std::string_view { "fsharp." };
                    auto const fnName = fullName.starts_with(prefix) ? fullName.substr(prefix.size())
                                                                     : fullName;
                    config.colorOverrides.colorFns[key] = std::string(fnName);
                }
                else
                {
                    config.colorOverrides.colorFns.erase(key);
                }
                prompt.setPromptConfig(std::move(config));
            });
        _runtime.registerPropertySetterOverload(name, CoreVM::LiteralType::Function);
    };

    registerColorProp("shell_prompt_color_path",            "path",            &PromptColorOverrides::path);
    registerColorProp("shell_prompt_color_git_clean",       "git_clean",       &PromptColorOverrides::gitClean);
    registerColorProp("shell_prompt_color_git_dirty",       "git_dirty",       &PromptColorOverrides::gitDirty);
    registerColorProp("shell_prompt_color_git_staged",      "git_staged",      &PromptColorOverrides::gitStaged);
    registerColorProp("shell_prompt_color_indicator",       "indicator",       &PromptColorOverrides::indicator);
    registerColorProp("shell_prompt_color_indicator_error", "indicator_error", &PromptColorOverrides::indicatorError);
    registerColorProp("shell_prompt_color_exit_code",       "exit_code",       &PromptColorOverrides::exitCode);
    registerColorProp("shell_prompt_color_duration",        "duration",        &PromptColorOverrides::duration);
    registerColorProp("shell_prompt_color_hostname",        "hostname",        &PromptColorOverrides::hostname);
    registerColorProp("shell_prompt_color_separator",       "separator",       &PromptColorOverrides::separator);
    registerColorProp("shell_prompt_color_badge",           "badge",           &PromptColorOverrides::badge);
    registerColorProp("shell_prompt_color_badge_text",      "badge_text",      &PromptColorOverrides::badgeText);
    registerColorProp("shell_prompt_color_clock",           "clock",           &PromptColorOverrides::clock);
    // clang-format on

    // Internal helper invoked by `invokePromptCallback` to capture the return value
    // of a user-defined prompt function. Not intended for direct user use.
    _runtime.registerFunction("__prompt_capture_string")
        .param<CoreVM::CoreString>("value")
        .returnType(CoreVM::LiteralType::Void)
        .bind([this](CoreVM::Params& args) { _promptCallbackResult = std::string(args.getString(1)); });
}

#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
void Shell::registerAgentConfigBuiltins()
{
    // clang-format off

    // --- Top-level agent settings ---

    _runtime.registerProperty("agent_provider", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.activeProvider)); })
        .onSet([this](CoreVM::Params& args) {
            auto const& name = args.getString(1);
            if (name == "claude" || name == "openai" || name == "gemini" || name == "openai_compat" || name == "local")
            {
                agentConfig.activeProvider = std::string(name);
                _agentProviderFactory.reset();
            }
        });

    _runtime.registerProperty("agent_prompt_indicator", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.promptIndicator)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.promptIndicator = std::string(args.getString(1)); });

    _runtime.registerProperty("agent_max_tool_result_size", CoreVM::LiteralType::Number)
        .onGet([this](CoreVM::Params& args) { args.setResult(static_cast<CoreVM::CoreNumber>(agentConfig.maxToolResultSize)); })
        .onSet([this](CoreVM::Params& args) {
            auto const bytes = args.getInt(1);
            if (bytes > 0)
                agentConfig.maxToolResultSize = static_cast<size_t>(bytes);
        });

    _runtime.registerProperty("agent_log_tool_uses", CoreVM::LiteralType::Boolean)
        .onGet([this](CoreVM::Params& args) { args.setResult(agentConfig.logToolUses); })
        .onSet([this](CoreVM::Params& args) { agentConfig.logToolUses = args.getBool(1); });

    // --- Claude provider ---

    _runtime.registerProperty("agent_claude_api_key", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.claude.apiKey)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.claude.apiKey = std::string(args.getString(1)); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_claude_api_key_env", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.claude.apiKeyEnv)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.claude.apiKeyEnv = std::string(args.getString(1)); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_claude_model", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.claude.model)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.claude.model = std::string(args.getString(1)); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_claude_max_tokens", CoreVM::LiteralType::Number)
        .onGet([this](CoreVM::Params& args) { args.setResult(static_cast<CoreVM::CoreNumber>(agentConfig.claude.maxTokens)); })
        .onSet([this](CoreVM::Params& args) { auto const n = args.getInt(1); if (n > 0) { agentConfig.claude.maxTokens = static_cast<size_t>(n); _agentProviderFactory.reset(); } });

    _runtime.registerProperty("agent_claude_thinking_mode", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agent::thinkingModeToString(agentConfig.claude.thinkingMode))); })
        .onSet([this](CoreVM::Params& args) { agentConfig.claude.thinkingMode = agent::thinkingModeFromString(args.getString(1)); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_claude_prompt_caching", CoreVM::LiteralType::Boolean)
        .onGet([this](CoreVM::Params& args) { args.setResult(agentConfig.claude.promptCaching); })
        .onSet([this](CoreVM::Params& args) { agentConfig.claude.promptCaching = args.getBool(1); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_claude_auth_type", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) {
            // Return the effective auth type based on the current preference.
            auto const& pref = agentConfig.claude.authPreference;
            if (pref == "oauth")
            {
                auto const oauthStore = agent::loadOAuthStore();
                if (oauthStore.claude.has_value() && !agent::isTokenExpired(*oauthStore.claude))
                    args.setResult(std::string("oauth"));
                else
                    args.setResult(std::string("none"));
            }
            else if (pref == "api_key")
            {
                if (agent::resolveProviderApiKey(agentConfig.claude.apiKey, agentConfig.claude.apiKeyEnv).has_value())
                    args.setResult(std::string("api_key"));
                else
                    args.setResult(std::string("none"));
            }
            else
            {
                // "auto": report what would actually be used.
                auto const oauthStore = agent::loadOAuthStore();
                if (oauthStore.claude.has_value() && !agent::isTokenExpired(*oauthStore.claude))
                    args.setResult(std::string("oauth"));
                else if (agent::resolveProviderApiKey(agentConfig.claude.apiKey, agentConfig.claude.apiKeyEnv).has_value())
                    args.setResult(std::string("api_key"));
                else
                    args.setResult(std::string("none"));
            }
        })
        .onSet([this](CoreVM::Params& args) {
            auto const value = std::string(args.getString(1));
            if (value == "oauth" || value == "api_key" || value == "auto")
            {
                agentConfig.claude.authPreference = value;
                _agentProviderFactory.reset();
            }
        });

    // --- OpenAI provider ---

    _runtime.registerProperty("agent_openai_api_key", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.openai.apiKey)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.openai.apiKey = std::string(args.getString(1)); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_openai_api_key_env", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.openai.apiKeyEnv)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.openai.apiKeyEnv = std::string(args.getString(1)); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_openai_model", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.openai.model)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.openai.model = std::string(args.getString(1)); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_openai_base_url", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.openai.baseUrl)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.openai.baseUrl = std::string(args.getString(1)); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_openai_max_tokens", CoreVM::LiteralType::Number)
        .onGet([this](CoreVM::Params& args) { args.setResult(static_cast<CoreVM::CoreNumber>(agentConfig.openai.maxTokens)); })
        .onSet([this](CoreVM::Params& args) { auto const n = args.getInt(1); if (n > 0) { agentConfig.openai.maxTokens = static_cast<size_t>(n); _agentProviderFactory.reset(); } });

    _runtime.registerProperty("agent_openai_thinking_mode", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agent::thinkingModeToString(agentConfig.openai.thinkingMode))); })
        .onSet([this](CoreVM::Params& args) { agentConfig.openai.thinkingMode = agent::thinkingModeFromString(args.getString(1)); _agentProviderFactory.reset(); });

    // --- OpenAI-compatible provider ---

    _runtime.registerProperty("agent_openai_compat_api_key", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.openaiCompat.apiKey)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.openaiCompat.apiKey = std::string(args.getString(1)); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_openai_compat_api_key_env", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.openaiCompat.apiKeyEnv)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.openaiCompat.apiKeyEnv = std::string(args.getString(1)); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_openai_compat_model", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.openaiCompat.model)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.openaiCompat.model = std::string(args.getString(1)); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_openai_compat_base_url", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.openaiCompat.baseUrl)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.openaiCompat.baseUrl = std::string(args.getString(1)); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_openai_compat_max_tokens", CoreVM::LiteralType::Number)
        .onGet([this](CoreVM::Params& args) { args.setResult(static_cast<CoreVM::CoreNumber>(agentConfig.openaiCompat.maxTokens)); })
        .onSet([this](CoreVM::Params& args) { auto const n = args.getInt(1); if (n > 0) { agentConfig.openaiCompat.maxTokens = static_cast<size_t>(n); _agentProviderFactory.reset(); } });

    _runtime.registerProperty("agent_openai_compat_thinking_mode", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agent::thinkingModeToString(agentConfig.openaiCompat.thinkingMode))); })
        .onSet([this](CoreVM::Params& args) { agentConfig.openaiCompat.thinkingMode = agent::thinkingModeFromString(args.getString(1)); _agentProviderFactory.reset(); });

    // --- Gemini provider ---

    _runtime.registerProperty("agent_gemini_api_key", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.gemini.apiKey)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.gemini.apiKey = std::string(args.getString(1)); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_gemini_api_key_env", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.gemini.apiKeyEnv)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.gemini.apiKeyEnv = std::string(args.getString(1)); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_gemini_model", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.gemini.model)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.gemini.model = std::string(args.getString(1)); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_gemini_max_tokens", CoreVM::LiteralType::Number)
        .onGet([this](CoreVM::Params& args) { args.setResult(static_cast<CoreVM::CoreNumber>(agentConfig.gemini.maxTokens)); })
        .onSet([this](CoreVM::Params& args) { auto const n = args.getInt(1); if (n > 0) { agentConfig.gemini.maxTokens = static_cast<size_t>(n); _agentProviderFactory.reset(); } });

    _runtime.registerProperty("agent_gemini_thinking_mode", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agent::thinkingModeToString(agentConfig.gemini.thinkingMode))); })
        .onSet([this](CoreVM::Params& args) { agentConfig.gemini.thinkingMode = agent::thinkingModeFromString(args.getString(1)); _agentProviderFactory.reset(); });

    // --- Plan mode ---

    _runtime.registerProperty("agent_plan_mode_enabled", CoreVM::LiteralType::Boolean)
        .onGet([this](CoreVM::Params& args) { args.setResult(agentConfig.planMode.enabled); })
        .onSet([this](CoreVM::Params& args) { agentConfig.planMode.enabled = args.getBool(1); });

    _runtime.registerProperty("agent_plan_mode_pause_between_steps", CoreVM::LiteralType::Boolean)
        .onGet([this](CoreVM::Params& args) { args.setResult(agentConfig.planMode.pauseBetweenSteps); })
        .onSet([this](CoreVM::Params& args) { agentConfig.planMode.pauseBetweenSteps = args.getBool(1); });

    _runtime.registerProperty("agent_plan_mode_max_exploration_turns", CoreVM::LiteralType::Number)
        .onGet([this](CoreVM::Params& args) { args.setResult(static_cast<CoreVM::CoreNumber>(agentConfig.planMode.maxExplorationTurns)); })
        .onSet([this](CoreVM::Params& args) { auto const n = args.getInt(1); if (n > 0) agentConfig.planMode.maxExplorationTurns = static_cast<size_t>(n); });

    // --- Explore sub-agent ---

    _runtime.registerProperty("agent_explore_max_turns", CoreVM::LiteralType::Number)
        .onGet([this](CoreVM::Params& args) { args.setResult(static_cast<CoreVM::CoreNumber>(agentConfig.explore.maxTurns)); })
        .onSet([this](CoreVM::Params& args) { auto const n = args.getInt(1); if (n > 0) agentConfig.explore.maxTurns = static_cast<size_t>(n); });

    // --- Session ---

    _runtime.registerProperty("agent_auto_resume", CoreVM::LiteralType::Boolean)
        .onGet([this](CoreVM::Params& args) { args.setResult(agentConfig.session.autoResume); })
        .onSet([this](CoreVM::Params& args) { agentConfig.session.autoResume = args.getBool(1); });

    _runtime.registerProperty("agent_session_replay", CoreVM::LiteralType::Boolean)
        .onGet([this](CoreVM::Params& args) { args.setResult(agentConfig.session.showResumeContext); })
        .onSet([this](CoreVM::Params& args) { agentConfig.session.showResumeContext = args.getBool(1); });

    // --- Trace ---

    _runtime.registerProperty("agent_trace_enabled", CoreVM::LiteralType::Boolean)
        .onGet([this](CoreVM::Params& args) { args.setResult(agentConfig.trace.enabled); })
        .onSet([this](CoreVM::Params& args) { agentConfig.trace.enabled = args.getBool(1); });

    _runtime.registerProperty("agent_trace_default_path", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.trace.defaultPath)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.trace.defaultPath = std::string(args.getString(1)); });

    _runtime.registerProperty("agent_trace_max_files", CoreVM::LiteralType::Number)
        .onGet([this](CoreVM::Params& args) { args.setResult(static_cast<CoreVM::CoreNumber>(agentConfig.trace.maxFiles)); })
        .onSet([this](CoreVM::Params& args) { auto const n = args.getInt(1); if (n > 0) agentConfig.trace.maxFiles = static_cast<size_t>(n); });

    _runtime.registerProperty("agent_trace_terminal", CoreVM::LiteralType::Boolean)
        .onGet([this](CoreVM::Params& args) { args.setResult(agentConfig.trace.terminal); })
        .onSet([this](CoreVM::Params& args) { agentConfig.trace.terminal = args.getBool(1); });

    // --- Permissions ---

    _runtime.registerProperty("agent_permissions_policy", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agent::permissionPolicyToString(agentConfig.permissions.policy))); })
        .onSet([this](CoreVM::Params& args) {
            auto const& value = args.getString(1);
            agentConfig.permissions.policy = agent::permissionPolicyFromString(value);
        });

    _runtime.registerProperty("agent_trusted_tool", CoreVM::LiteralType::Object)
        .onGet([this](CoreVM::Params& args) {
            auto const& tools = agentConfig.permissions.trustedTools;
            CoreVM::TypedObject* list = nullptr;
            for (auto const& tool: tools | std::views::reverse)
            {
                auto* str = args.caller()->newString(tool);
                list = args.caller()->makeConsCell(
                    reinterpret_cast<uintptr_t>(str), list, CoreVM::LiteralType::String);
            }
            args.setResult(
                static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
        })
        .onSet([this](CoreVM::Params& args) {
            auto* list = args.getObject(1);
            agentConfig.permissions.trustedTools.clear();
            for (auto* cur = list; cur && cur->tag == 1;
                 cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1)))
            {
                auto const* str = reinterpret_cast<CoreVM::CoreString const*>(cur->getSlot(0));
                agentConfig.permissions.trustedTools.emplace_back(*str);
            }
        });

    _runtime.registerProperty("agent_blocked_pattern", CoreVM::LiteralType::Object)
        .onGet([this](CoreVM::Params& args) {
            auto const& patterns = agentConfig.permissions.blockedPatterns;
            CoreVM::TypedObject* list = nullptr;
            for (auto const& pattern: patterns | std::views::reverse)
            {
                auto* str = args.caller()->newString(pattern);
                list = args.caller()->makeConsCell(
                    reinterpret_cast<uintptr_t>(str), list, CoreVM::LiteralType::String);
            }
            args.setResult(
                static_cast<CoreVM::CoreNumber>(reinterpret_cast<uintptr_t>(list)));
        })
        .onSet([this](CoreVM::Params& args) {
            auto* list = args.getObject(1);
            agentConfig.permissions.blockedPatterns.clear();
            for (auto* cur = list; cur && cur->tag == 1;
                 cur = reinterpret_cast<CoreVM::TypedObject*>(cur->getSlot(1)))
            {
                auto const* str = reinterpret_cast<CoreVM::CoreString const*>(cur->getSlot(0));
                agentConfig.permissions.blockedPatterns.emplace_back(*str);
            }
        });

    // --- Web search ---

    _runtime.registerProperty("agent_web_search_engine", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(webSearchConfig.engine)); })
        .onSet([this](CoreVM::Params& args) {
            auto const& engine = args.getString(1);
            if (engine == "duckduckgo" || engine == "brave" || engine == "google")
                webSearchConfig.engine = std::string(engine);
        });

    _runtime.registerProperty("agent_web_search_api_key", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(webSearchConfig.apiKey)); })
        .onSet([this](CoreVM::Params& args) { webSearchConfig.apiKey = std::string(args.getString(1)); });

    _runtime.registerProperty("agent_web_search_cx", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(webSearchConfig.cx)); })
        .onSet([this](CoreVM::Params& args) { webSearchConfig.cx = std::string(args.getString(1)); });

    _runtime.registerProperty("agent_web_search_max_results", CoreVM::LiteralType::Number)
        .onGet([this](CoreVM::Params& args) { args.setResult(static_cast<CoreVM::CoreNumber>(webSearchConfig.maxResults)); })
        .onSet([this](CoreVM::Params& args) {
            auto const count = args.getInt(1);
            if (count > 0 && count <= 20)
                webSearchConfig.maxResults = static_cast<size_t>(count);
        });

    // --- Error recovery ---

    _runtime.registerProperty("agent_error_recovery_action", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) {
            args.setResult(std::string(agent::errorRecoveryActionToString(agentConfig.errorRecovery.action)));
        })
        .onSet([this](CoreVM::Params& args) {
            agentConfig.errorRecovery.action = agent::errorRecoveryActionFromString(args.getString(1));
        });

    _runtime.registerProperty("agent_error_recovery_model", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.errorRecovery.model)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.errorRecovery.model = std::string(args.getString(1)); });

    // --- Local llama.cpp provider ---

    _runtime.registerProperty("agent_local_model_path", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.local.modelPath)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.local.modelPath = std::string(args.getString(1)); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_local_model_dir", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.local.modelDir)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.local.modelDir = std::string(args.getString(1)); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_local_gpu_layers", CoreVM::LiteralType::Number)
        .onGet([this](CoreVM::Params& args) { args.setResult(static_cast<CoreVM::CoreNumber>(agentConfig.local.gpuLayers)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.local.gpuLayers = static_cast<int32_t>(args.getInt(1)); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_local_context_size", CoreVM::LiteralType::Number)
        .onGet([this](CoreVM::Params& args) { args.setResult(static_cast<CoreVM::CoreNumber>(agentConfig.local.contextSize)); })
        .onSet([this](CoreVM::Params& args) { auto const n = args.getInt(1); if (n > 0) { agentConfig.local.contextSize = static_cast<size_t>(n); _agentProviderFactory.reset(); } });

    _runtime.registerProperty("agent_local_threads", CoreVM::LiteralType::Number)
        .onGet([this](CoreVM::Params& args) { args.setResult(static_cast<CoreVM::CoreNumber>(agentConfig.local.threads)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.local.threads = static_cast<int32_t>(args.getInt(1)); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_local_batch_size", CoreVM::LiteralType::Number)
        .onGet([this](CoreVM::Params& args) { args.setResult(static_cast<CoreVM::CoreNumber>(agentConfig.local.batchSize)); })
        .onSet([this](CoreVM::Params& args) { auto const n = args.getInt(1); if (n > 0) { agentConfig.local.batchSize = static_cast<size_t>(n); _agentProviderFactory.reset(); } });

    _runtime.registerProperty("agent_local_temperature", CoreVM::LiteralType::Number)
        .onGet([this](CoreVM::Params& args) { args.setResult(static_cast<CoreVM::CoreNumber>(static_cast<int64_t>(agentConfig.local.temperature * 100))); })
        .onSet([this](CoreVM::Params& args) { agentConfig.local.temperature = static_cast<float>(args.getInt(1)) / 100.0f; _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_local_flash_attention", CoreVM::LiteralType::Boolean)
        .onGet([this](CoreVM::Params& args) { args.setResult(agentConfig.local.flashAttention); })
        .onSet([this](CoreVM::Params& args) { agentConfig.local.flashAttention = args.getBool(1); _agentProviderFactory.reset(); });

    _runtime.registerProperty("agent_local_max_tokens", CoreVM::LiteralType::Number)
        .onGet([this](CoreVM::Params& args) { args.setResult(static_cast<CoreVM::CoreNumber>(agentConfig.local.maxTokens)); })
        .onSet([this](CoreVM::Params& args) { auto const n = args.getInt(1); if (n > 0) { agentConfig.local.maxTokens = static_cast<size_t>(n); _agentProviderFactory.reset(); } });

    _runtime.registerProperty("agent_local_chat_template", CoreVM::LiteralType::String)
        .onGet([this](CoreVM::Params& args) { args.setResult(std::string(agentConfig.local.chatTemplate)); })
        .onSet([this](CoreVM::Params& args) { agentConfig.local.chatTemplate = std::string(args.getString(1)); _agentProviderFactory.reset(); });
    // clang-format on
}
#else  // !ENDO_ENABLE_AGENT — register no-op stubs so init.endo scripts don't break
void Shell::registerAgentConfigBuiltins()
{
    // clang-format off
    auto noopSet = [](CoreVM::Params&) {};

    auto registerStringProp = [&](char const* name) {
        _runtime.registerProperty(name, CoreVM::LiteralType::String)
            .onGet([](CoreVM::Params& args) { args.setResult(std::string {}); })
            .onSet(noopSet);
    };
    auto registerNumberProp = [&](char const* name) {
        _runtime.registerProperty(name, CoreVM::LiteralType::Number)
            .onGet([](CoreVM::Params& args) { args.setResult(CoreVM::CoreNumber(0)); })
            .onSet(noopSet);
    };
    auto registerBoolProp = [&](char const* name) {
        _runtime.registerProperty(name, CoreVM::LiteralType::Boolean)
            .onGet([](CoreVM::Params& args) { args.setResult(false); })
            .onSet(noopSet);
    };
    auto registerObjectProp = [&](char const* name) {
        _runtime.registerProperty(name, CoreVM::LiteralType::Number)
            .onGet([](CoreVM::Params& args) { args.setResult(CoreVM::CoreNumber(0)); })
            .onSet(noopSet);
    };

    for (auto const* name : {
        "agent_provider", "agent_prompt_indicator",
        "agent_claude_api_key", "agent_claude_api_key_env", "agent_claude_model",
        "agent_claude_thinking_mode", "agent_claude_auth_type",
        "agent_openai_api_key", "agent_openai_api_key_env", "agent_openai_model",
        "agent_openai_base_url", "agent_openai_thinking_mode",
        "agent_openai_compat_api_key", "agent_openai_compat_api_key_env",
        "agent_openai_compat_model", "agent_openai_compat_base_url",
        "agent_openai_compat_thinking_mode",
        "agent_gemini_api_key", "agent_gemini_api_key_env", "agent_gemini_model",
        "agent_gemini_thinking_mode",
        "agent_permissions_policy",
        "agent_web_search_engine", "agent_web_search_api_key", "agent_web_search_cx",
        "agent_error_recovery_action", "agent_error_recovery_model",
        "agent_local_model_path", "agent_local_model_dir", "agent_local_chat_template",
        "agent_trace_default_path",
    }) registerStringProp(name);

    for (auto const* name : {
        "agent_max_tool_result_size",
        "agent_claude_max_tokens", "agent_openai_max_tokens",
        "agent_openai_compat_max_tokens", "agent_gemini_max_tokens",
        "agent_plan_mode_max_exploration_turns", "agent_explore_max_turns",
        "agent_trace_max_files", "agent_web_search_max_results",
        "agent_local_gpu_layers", "agent_local_context_size",
        "agent_local_threads", "agent_local_batch_size",
        "agent_local_temperature", "agent_local_max_tokens",
    }) registerNumberProp(name);

    for (auto const* name : {
        "agent_log_tool_uses", "agent_claude_prompt_caching",
        "agent_plan_mode_enabled", "agent_plan_mode_pause_between_steps",
        "agent_auto_resume", "agent_session_replay",
        "agent_trace_enabled", "agent_trace_terminal",
        "agent_local_flash_attention",
    }) registerBoolProp(name);

    registerObjectProp("agent_trusted_tool");
    registerObjectProp("agent_blocked_pattern");
    // clang-format on
}
#endif // ENDO_ENABLE_AGENT

#if defined(ENDO_ENABLE_AGENT) && ENDO_ENABLE_AGENT
void Shell::registerMcpBuiltins()
{
    // clang-format off
    _runtime.registerFunction("add_mcp_server")
        .param<CoreVM::CoreString>("name")
        .param<CoreVM::CoreString>("command_line")
        .returnType(CoreVM::LiteralType::Void)
        .bind([this](CoreVM::Params& args) {
            auto const& name = args.getString(1);
            auto const& commandLine = args.getString(2);

            // Split command line on spaces: first token is command, rest are args
            auto config = agent::mcp::McpServerConfig {};
            config.name = std::string(name);

            auto iss = std::istringstream(std::string(commandLine));
            auto token = std::string {};
            if (iss >> token)
                config.command = std::move(token);
            while (iss >> token)
                config.args.push_back(std::move(token));

            mcpServerConfigs.push_back(std::move(config));
        });

    _runtime.registerFunction("set_mcp_env")
        .param<CoreVM::CoreString>("server_name")
        .param<CoreVM::CoreString>("key")
        .param<CoreVM::CoreString>("value")
        .returnType(CoreVM::LiteralType::Void)
        .bind([this](CoreVM::Params& args) {
            auto const& serverName = args.getString(1);
            auto const& key = args.getString(2);
            auto const& value = args.getString(3);

            for (auto& config: mcpServerConfigs)
            {
                if (config.name == serverName)
                {
                    config.env[std::string(key)] = std::string(value);
                    return;
                }
            }
        });

    _runtime.registerFunction("remove_mcp_server")
        .param<CoreVM::CoreString>("name")
        .returnType(CoreVM::LiteralType::Void)
        .bind([this](CoreVM::Params& args) {
            auto const& name = args.getString(1);
            std::erase_if(mcpServerConfigs,
                          [&name](auto const& config) { return config.name == name; });
        });
    // clang-format on
}
#else  // !ENDO_ENABLE_AGENT — register no-op MCP stubs
void Shell::registerMcpBuiltins()
{
    // clang-format off
    _runtime.registerFunction("add_mcp_server")
        .param<CoreVM::CoreString>("name")
        .param<CoreVM::CoreString>("command_line")
        .returnType(CoreVM::LiteralType::Void)
        .bind([](CoreVM::Params&) {});

    _runtime.registerFunction("set_mcp_env")
        .param<CoreVM::CoreString>("server_name")
        .param<CoreVM::CoreString>("key")
        .param<CoreVM::CoreString>("value")
        .returnType(CoreVM::LiteralType::Void)
        .bind([](CoreVM::Params&) {});

    _runtime.registerFunction("remove_mcp_server")
        .param<CoreVM::CoreString>("name")
        .returnType(CoreVM::LiteralType::Void)
        .bind([](CoreVM::Params&) {});
    // clang-format on
}
#endif // ENDO_ENABLE_AGENT

void Shell::registerCompleterBuiltins()
{
    // clang-format off
    auto completerRegisterCallback = [this](CoreVM::Params& args) {
        auto command = std::string(args.getString(1));
        auto functionName = std::string(args.getString(2));
        _completerFunctions.registerFunction(std::move(command), std::move(functionName));
    };

    // Completion.register dispatches to this (new canonical name)
    _runtime.registerFunction("completer_register")
        .param<CoreVM::CoreString>("command")
        .param<CoreVM::CoreString>("function_name")
        .returnType(CoreVM::LiteralType::Void)
        .bind(completerRegisterCallback);

    // Backward compatibility alias
    _runtime.registerFunction("register_completer")
        .param<CoreVM::CoreString>("command")
        .param<CoreVM::CoreString>("function_name")
        .returnType(CoreVM::LiteralType::Void)
        .bind(completerRegisterCallback);

    // Bridge function: walks a list and collects completions into _collectedCompletions.
    // Handles both List<String> (backward compat) and List<CompletionEntry> (new).
    _runtime.registerFunction("__collect_completions")
        .param(CoreVM::LiteralType::Number, "list")
        .returnType(CoreVM::LiteralType::Void)
        .bind([this](CoreVM::Params& args) {
            _collectedCompletions.clear();
            auto const rawList = static_cast<uint64_t>(args.getInt(1));
            auto* runner = args.caller();
            if (!runner->isKnownObject(rawList))
                return;
            auto* list = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(rawList));
            while (list && list->type->id == CoreVM::BuiltinTypeId::List && list->tag == 1)
            {
                auto const head = list->getSlot(0);
                if (runner->isKnownString(head))
                {
                    auto const* str = reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(head));
                    _collectedCompletions.push_back({ .text = *str });
                }
                else if (runner->isKnownObject(head))
                {
                    auto* obj = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(head));
                    if (obj->type->id == CoreVM::BuiltinTypeId::CompletionEntry)
                    {
                        auto const* text = reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(obj->getSlot(0)));
                        CollectedCompletion entry { .text = *text };
                        if (obj->tag >= 1)
                        {
                            auto const* desc = reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(obj->getSlot(1)));
                            entry.description = *desc;
                        }
                        if (obj->tag >= 2)
                        {
                            auto const* detail = reinterpret_cast<CoreVM::CoreString const*>(static_cast<uintptr_t>(obj->getSlot(2)));
                            entry.detail = *detail;
                        }
                        _collectedCompletions.push_back(std::move(entry));
                    }
                }
                list = reinterpret_cast<CoreVM::TypedObject*>(static_cast<uintptr_t>(list->getSlot(1)));
            }
        });
    // clang-format on

    // run_script -- execute an .endo script in the current shell context
    _runtime.registerFunction("run_script")
        .param<CoreVM::CoreString>("path")
        .returnType(CoreVM::LiteralType::Number)
        .bind(&Shell::builtinRunScript, this);
}

} // namespace endo
