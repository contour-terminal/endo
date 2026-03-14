// SPDX-License-Identifier: Apache-2.0
#include <shell/Shell.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include <platform/Pipe.hpp>
#include <platform/Process.hpp>
#include <platform/Types.hpp>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <unistd.h>
#endif

namespace endo
{

void Shell::builtinExit(CoreVM::Params& context)
{
    _exitCode = static_cast<int>(context.getInt(1));
    _runner->suspend();
    _quit = true;
}

void Shell::builtinChDir(CoreVM::Params& context)
{
    auto path = std::string(context.getString(1));

    if (path == "-")
    {
        auto const oldpwd = _env.get("OLDPWD");
        if (!oldpwd.has_value() || oldpwd->empty())
        {
            error("cd: OLDPWD not set");
            _exitCode = 1;
            context.setResult(false);
            return;
        }
        path = std::string(*oldpwd);
    }

    auto const result = _env.changeDirectory(path);
    if (!result.has_value())
    {
        error("Failed to change directory to '{}': {}", path, toString(result.error()));
        _exitCode = 1;
    }
    else
    {
        _env.set("OLDPWD", _env.get("PWD").value_or(""));
        _env.set("PWD", _env.currentDirectory());
        _exitCode = 0;
        emitCurrentWorkingDirectory();
        onDirectoryChanged();
    }

    context.setResult(result.has_value());
}

void Shell::builtinChDirHome(CoreVM::Params& context)
{
    auto const path = _env.get("HOME").value_or("/");

    auto const result = _env.changeDirectory(std::filesystem::path(path));
    if (!result.has_value())
    {
        error("Failed to change directory to '{}': {}", path, toString(result.error()));
        _exitCode = 1;
    }
    else
    {
        _env.set("OLDPWD", _env.get("PWD").value_or(""));
        _env.set("PWD", _env.currentDirectory());
        _exitCode = 0;
        emitCurrentWorkingDirectory();
        onDirectoryChanged();
    }

    context.setResult(result.has_value());
}

void Shell::builtinSet(CoreVM::Params& context)
{
    _env.set(context.getString(1), context.getString(2));
    context.setResult(true);
}

void Shell::builtinUnset(CoreVM::Params& context)
{
    _env.unset(context.getString(1));
    context.setResult(true);
}

void Shell::builtinGetVar(CoreVM::Params& context)
{
    auto const& name = context.getString(1);
    auto const value = _env.get(name);
    context.setResult(std::string(value.value_or("")));
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void Shell::builtinGetExitStatus(CoreVM::Params& context)
{
    context.setResult(std::to_string(_exitCode));
}

void Shell::builtinSetExitStatus(CoreVM::Params& context)
{
    _exitCode = static_cast<int>(context.getInt(1));
}

// NOLINTNEXTLINE(readability-make-member-function-const)
void Shell::builtinGetProcessId(CoreVM::Params& context)
{
    context.setResult(std::to_string(_shellPid));
}

void Shell::builtinGetBackgroundId(CoreVM::Params& context)
{
    if (_lastBackgroundPid.has_value())
        context.setResult(std::to_string(_lastBackgroundPid.value()));
    else
        context.setResult("");
}

void Shell::builtinGetPositional(CoreVM::Params& context)
{
    auto const index = static_cast<size_t>(context.getInt(1));
    if (index < _positionalParameters.size())
        context.setResult(_positionalParameters[index]);
    else
        context.setResult("");
}

void Shell::builtinSetAndExport(CoreVM::Params& context)
{
    _env.set(context.getString(1), context.getString(2));
    _env.exportVariable(context.getString(1));
}

void Shell::builtinExport(CoreVM::Params& context)
{
    _env.exportVariable(context.getString(1));
}

// ---------------------------------------------------------------------------
// source-env: Import environment variables from an external script
// ---------------------------------------------------------------------------

namespace
{

    /// @brief Supported script types for source-env, determined by file extension.
    enum class ScriptType : std::uint8_t
    {
        Batch,      ///< .bat / .cmd  (Windows cmd.exe)
        PowerShell, ///< .ps1        (pwsh / powershell.exe, cross-platform)
        Shell,      ///< .sh          (bash)
    };

    /// @brief Determines the script type from a file extension.
    /// @param ext Lowercased file extension including the dot (e.g. ".bat").
    /// @return The script type, or std::nullopt for unsupported extensions.
    [[nodiscard]] auto detectScriptType(std::string_view ext) -> std::optional<ScriptType>
    {
        if (ext == ".bat" || ext == ".cmd")
            return ScriptType::Batch;
        if (ext == ".ps1")
            return ScriptType::PowerShell;
        if (ext == ".sh")
            return ScriptType::Shell;
        return std::nullopt;
    }

    /// @brief Builds the content of a temporary wrapper script that sources the target
    ///        script and dumps all environment variables as KEY=VALUE lines.
    /// @param scriptPath Absolute path to the script to source.
    /// @param args       Optional extra arguments to pass to the script.
    /// @param type       The detected script type.
    /// @return The wrapper script content.
    [[nodiscard]] auto buildWrapperContent(std::filesystem::path const& scriptPath,
                                           std::string_view args,
                                           ScriptType type) -> std::string
    {
        auto const pathStr = scriptPath.string();
        switch (type)
        {
            case ScriptType::Batch:
                return std::format("@echo off\r\ncall \"{}\" {}\r\nset\r\n", pathStr, args);
            case ScriptType::PowerShell:
                return std::format("& '{}' {}\n"
                                   "Get-ChildItem Env: | ForEach-Object {{ \"$($_.Name)=$($_.Value)\" }}\n",
                                   pathStr,
                                   args);
            case ScriptType::Shell: return std::format(". '{}' {}\nenv\n", pathStr, args);
        }
        return {};
    }

    /// @brief Returns the file extension for wrapper temp files of the given type.
    [[nodiscard]] constexpr auto wrapperExtension(ScriptType type) -> std::string_view
    {
        switch (type)
        {
            case ScriptType::Batch: return ".bat";
            case ScriptType::PowerShell: return ".ps1";
            case ScriptType::Shell: return ".sh";
        }
        return ".sh";
    }

    /// @brief Parses environment dump output (KEY=VALUE per line) into a key-value map.
    ///
    /// Lines without '=' are treated as continuations of the previous value (handles
    /// rare multi-line environment variable values).
    ///
    /// @param output The raw stdout output from the wrapper script.
    /// @return A vector of (key, value) pairs preserving order.
    [[nodiscard]] auto parseEnvOutput(std::string_view output)
        -> std::vector<std::pair<std::string, std::string>>
    {
        auto result = std::vector<std::pair<std::string, std::string>> {};
        auto pos = std::string_view::size_type { 0 };

        while (pos < output.size())
        {
            auto const lineEnd = output.find('\n', pos);
            auto line = output.substr(
                pos, lineEnd != std::string_view::npos ? lineEnd - pos : std::string_view::npos);
            pos = lineEnd != std::string_view::npos ? lineEnd + 1 : output.size();

            // Strip trailing \r (Windows line endings)
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);

            if (line.empty())
                continue;

            auto const eq = line.find('=');
            if (eq != std::string_view::npos && eq > 0)
            {
                result.emplace_back(std::string(line.substr(0, eq)), std::string(line.substr(eq + 1)));
            }
            else if (!result.empty())
            {
                // Continuation of previous value (multi-line env var)
                result.back().second += '\n';
                result.back().second += std::string(line);
            }
        }

        return result;
    }

#if defined(_WIN32)
    /// @brief Case-insensitive string comparison for environment variable keys on Windows.
    [[nodiscard]] auto keysEqualCaseInsensitive(std::string_view a, std::string_view b) -> bool
    {
        if (a.size() != b.size())
            return false;
        return std::equal(a.begin(), a.end(), b.begin(), [](unsigned char ac, unsigned char bc) {
            return std::tolower(ac) == std::tolower(bc);
        });
    }
#endif

} // anonymous namespace

int Shell::executeInlineSourceEnv(CoreVM::CoreStringArray const& args, NativeHandle outputFd)
{
    // Usage: source-env <script-path> [extra-args...]
    if (args.size() < 2)
    {
        error("source-env: usage: source-env <script> [args...]");
        return EXIT_FAILURE;
    }

    auto const scriptPath = std::filesystem::path(std::string(args.at(1)));

    // Collect extra arguments as a single string
    auto extraArgs = std::string {};
    for (auto const i: std::views::iota(2uz, args.size()))
    {
        if (!extraArgs.empty())
            extraArgs += ' ';
        extraArgs += args.at(i);
    }

    // 1. Validate script exists
    if (!std::filesystem::exists(scriptPath))
    {
        error("source-env: script not found: {}", scriptPath.string());
        return EXIT_FAILURE;
    }

    // 2. Detect script type from extension
    auto ext = scriptPath.extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    auto const scriptType = detectScriptType(ext);
    if (!scriptType.has_value())
    {
        error("source-env: unsupported script type '{}' (expected .bat, .cmd, .ps1, or .sh)", ext);
        return EXIT_FAILURE;
    }

#if !defined(_WIN32)
    if (*scriptType == ScriptType::Batch)
    {
        error("source-env: .bat/.cmd scripts are only supported on Windows");
        return EXIT_FAILURE;
    }
#endif

    // 3. Resolve the interpreter
    auto interpreter = std::filesystem::path {};
    auto interpreterArgs = std::vector<std::string> {};

    switch (*scriptType)
    {
        case ScriptType::Batch:
            interpreter = "cmd.exe";
            interpreterArgs = { "/c" };
            break;
        case ScriptType::PowerShell: {
            // Try pwsh (PowerShell Core, cross-platform) first
            if (auto pwshPath = resolveProgram("pwsh"); pwshPath.has_value())
            {
                interpreter = *pwshPath;
            }
            else
            {
#if defined(_WIN32)
                // Fall back to powershell.exe (Windows PowerShell 5.x) on Windows
                if (auto psPath = resolveProgram("powershell.exe"); psPath.has_value())
                    interpreter = *psPath;
#endif
            }
            if (interpreter.empty())
            {
                error("source-env: PowerShell not found (install pwsh for cross-platform support)");
                return EXIT_FAILURE;
            }
            interpreterArgs = { "-NoProfile", "-ExecutionPolicy", "Bypass", "-File" };
            break;
        }
        case ScriptType::Shell: {
            if (auto bashPath = resolveProgram("bash"); bashPath.has_value())
                interpreter = *bashPath;
            else
            {
                error("source-env: bash not found in PATH");
                return EXIT_FAILURE;
            }
            break;
        }
    }

    // 4. Snapshot current environment
    auto before = std::map<std::string, std::string> {};
    for (auto const& key: _env.keys())
    {
        if (auto val = _env.get(key))
            before[key] = std::string(*val);
    }

    // 5. Create temp wrapper script
    auto const tempDir = std::filesystem::temp_directory_path();
#if defined(_WIN32)
    auto const pid = static_cast<unsigned long>(GetCurrentProcessId());
#else
    auto const pid = static_cast<unsigned long>(getpid());
#endif
    auto const wrapperPath = tempDir / std::format("endo_srcenv_{}{}", pid, wrapperExtension(*scriptType));
    auto const wrapperContent = buildWrapperContent(scriptPath, extraArgs, *scriptType);

    {
        auto ofs = std::ofstream(wrapperPath, std::ios::binary);
        if (!ofs)
        {
            error("source-env: failed to create temp wrapper: {}", wrapperPath.string());
            return EXIT_FAILURE;
        }
        ofs << wrapperContent;
    }

    // Ensure temp file cleanup on all exit paths
    struct TempFileGuard
    {
        std::filesystem::path path;

        ~TempFileGuard()
        {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } guard { wrapperPath };

    // 6. Spawn interpreter and capture stdout
    auto pipeResult = createPipe();
    if (!pipeResult.has_value())
    {
        error("source-env: failed to create pipe");
        return EXIT_FAILURE;
    }
    auto& pipe = pipeResult.value();

    auto config = SpawnConfig {};
    config.program = interpreter;
    config.arguments = std::move(interpreterArgs);
    config.arguments.push_back(wrapperPath.string());
    config.stdinFd = _tty.inputFd();
    config.stdoutFd = pipe->writer();
    config.stderrFd = standardError();

    auto pidResult = _processManager.spawn(config);
    pipe->closeWriter();

    // Read all output
    auto output = std::string {};
    char buf[4096];
    while (true)
    {
        auto const n = platformRead(pipe->reader(), buf, sizeof(buf));
        if (n <= 0)
            break;
        output.append(buf, static_cast<size_t>(n));
    }
    pipe->closeReader();

    auto childExitCode = 0;
    if (pidResult.has_value())
    {
        if (auto waitResult = _processManager.wait(*pidResult); waitResult.has_value())
            childExitCode = waitResult->exitCode;
    }
    else
    {
        error("source-env: failed to spawn interpreter: {}", interpreter.string());
        return EXIT_FAILURE;
    }

    // 7. Parse output and import changed/new variables
    auto const parsed = parseEnvOutput(output);

    for (auto const& [key, value]: parsed)
    {
        if (key.empty())
            continue;

        // Check if this variable is new or changed
        auto isNew = true;
        for (auto const& [beforeKey, beforeValue]: before)
        {
#if defined(_WIN32)
            if (keysEqualCaseInsensitive(beforeKey, key))
#else
            if (beforeKey == key)
#endif
            {
                isNew = false;
                if (beforeValue != value)
                    _env.setAndExport(key, value);
                break;
            }
        }

        if (isNew)
            _env.setAndExport(key, value);
    }

    return childExitCode;
}

} // namespace endo
