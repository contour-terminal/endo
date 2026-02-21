// SPDX-License-Identifier: Apache-2.0
#include <tui/GenericSyntaxHighlighter.hpp>
#include <tui/Theme.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <functional>
#include <string>
#include <string_view>

namespace tui
{

namespace
{
    auto endoHighlighter() -> HighlightFunction&
    {
        static HighlightFunction instance;
        return instance;
    }

    // -------------------------------------------------------------------------
    // Helper: check if character is part of an identifier
    // -------------------------------------------------------------------------

    constexpr bool isIdentChar(char ch)
    {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
    }

    constexpr bool isDigit(char ch)
    {
        return ch >= '0' && ch <= '9';
    }

    constexpr bool isHexDigit(char ch)
    {
        return isDigit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
    }

    constexpr bool isOperatorChar(char ch)
    {
        return ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '%' || ch == '=' || ch == '!'
               || ch == '<' || ch == '>' || ch == '&' || ch == '|' || ch == '^' || ch == '~' || ch == '?';
    }

    constexpr bool isPunctuationChar(char ch)
    {
        return ch == '(' || ch == ')' || ch == '{' || ch == '}' || ch == '[' || ch == ']' || ch == ','
               || ch == ';' || ch == ':' || ch == '.';
    }

    // -------------------------------------------------------------------------
    // Sorted keyword/type/builtin arrays for binary search
    // -------------------------------------------------------------------------

    // C++ keywords (sorted)
    constexpr auto cppKeywords = std::to_array<std::string_view>({
        "alignas",
        "alignof",
        "and",
        "and_eq",
        "asm",
        "auto",
        "bitand",
        "bitor",
        "bool",
        "break",
        "case",
        "catch",
        "char",
        "char16_t",
        "char32_t",
        "char8_t",
        "class",
        "co_await",
        "co_return",
        "co_yield",
        "compl",
        "concept",
        "const",
        "const_cast",
        "consteval",
        "constexpr",
        "constinit",
        "continue",
        "decltype",
        "default",
        "delete",
        "do",
        "double",
        "dynamic_cast",
        "else",
        "enum",
        "explicit",
        "export",
        "extern",
        "false",
        "float",
        "for",
        "friend",
        "goto",
        "if",
        "import",
        "inline",
        "int",
        "long",
        "module",
        "mutable",
        "namespace",
        "new",
        "noexcept",
        "not",
        "not_eq",
        "nullptr",
        "operator",
        "or",
        "or_eq",
        "private",
        "protected",
        "public",
        "register",
        "reinterpret_cast",
        "requires",
        "return",
        "short",
        "signed",
        "sizeof",
        "static",
        "static_assert",
        "static_cast",
        "struct",
        "switch",
        "template",
        "this",
        "thread_local",
        "throw",
        "true",
        "try",
        "typedef",
        "typeid",
        "typename",
        "union",
        "unsigned",
        "using",
        "virtual",
        "void",
        "volatile",
        "wchar_t",
        "while",
        "xor",
        "xor_eq",
    });

    // C++ standard library types (sorted)
    constexpr auto cppTypes = std::to_array<std::string_view>({
        "array",      "atomic",   "basic_string", "deque",      "expected",      "function",      "future",
        "int16_t",    "int32_t",  "int64_t",      "int8_t",     "list",          "map",           "monostate",
        "multimap",   "multiset", "optional",     "pair",       "promise",       "ptrdiff_t",     "set",
        "shared_ptr", "size_t",   "span",         "string",     "string_view",   "tuple",         "uint16_t",
        "uint32_t",   "uint64_t", "uint8_t",      "unique_ptr", "unordered_map", "unordered_set", "variant",
        "vector",     "weak_ptr",
    });

    // Python keywords (sorted)
    constexpr auto pythonKeywords = std::to_array<std::string_view>({
        "False", "None",     "True",  "and",    "as",   "assert", "async",  "await",    "break",
        "class", "continue", "def",   "del",    "elif", "else",   "except", "finally",  "for",
        "from",  "global",   "if",    "import", "in",   "is",     "lambda", "nonlocal", "not",
        "or",    "pass",     "raise", "return", "try",  "while",  "with",   "yield",
    });

    // Python builtin functions (sorted)
    constexpr auto pythonBuiltins = std::to_array<std::string_view>({
        "abs",     "all",       "any",        "bin",          "bool",    "bytes",    "callable", "chr",
        "dict",    "dir",       "divmod",     "enumerate",    "eval",    "exec",     "filter",   "float",
        "format",  "frozenset", "getattr",    "globals",      "hasattr", "hash",     "hex",      "id",
        "input",   "int",       "isinstance", "issubclass",   "iter",    "len",      "list",     "locals",
        "map",     "max",       "min",        "next",         "object",  "oct",      "open",     "ord",
        "pow",     "print",     "property",   "range",        "repr",    "reversed", "round",    "set",
        "setattr", "slice",     "sorted",     "staticmethod", "str",     "sum",      "super",    "tuple",
        "type",    "vars",      "zip",
    });

    // Bash keywords (sorted)
    constexpr auto bashKeywords = std::to_array<std::string_view>({
        "case",
        "do",
        "done",
        "elif",
        "else",
        "esac",
        "fi",
        "for",
        "function",
        "if",
        "in",
        "select",
        "then",
        "until",
        "while",
    });

    // Bash builtin commands (sorted)
    constexpr auto bashBuiltins = std::to_array<std::string_view>({
        "alias", "bg",       "bind",   "break",  "cd",      "command", "continue", "declare", "echo",
        "eval",  "exec",     "exit",   "export", "fg",      "getopts", "hash",     "help",    "history",
        "jobs",  "kill",     "let",    "local",  "logout",  "printf",  "pushd",    "popd",    "pwd",
        "read",  "readonly", "return", "set",    "shift",   "shopt",   "source",   "test",    "trap",
        "type",  "typeset",  "ulimit", "umask",  "unalias", "unset",   "wait",
    });

    // CMake commands (sorted)
    constexpr auto cmakeKeywords = std::to_array<std::string_view>({
        "add_compile_definitions",
        "add_compile_options",
        "add_custom_command",
        "add_custom_target",
        "add_definitions",
        "add_dependencies",
        "add_executable",
        "add_library",
        "add_subdirectory",
        "add_test",
        "cmake_minimum_required",
        "cmake_policy",
        "configure_file",
        "else",
        "elseif",
        "enable_language",
        "enable_testing",
        "endforeach",
        "endfunction",
        "endif",
        "endmacro",
        "endwhile",
        "execute_process",
        "export",
        "file",
        "find_file",
        "find_library",
        "find_package",
        "find_path",
        "find_program",
        "foreach",
        "function",
        "get_cmake_property",
        "get_property",
        "if",
        "include",
        "include_directories",
        "install",
        "link_directories",
        "link_libraries",
        "list",
        "macro",
        "mark_as_advanced",
        "math",
        "message",
        "option",
        "project",
        "return",
        "separate_arguments",
        "set",
        "set_property",
        "set_target_properties",
        "string",
        "target_compile_definitions",
        "target_compile_features",
        "target_compile_options",
        "target_include_directories",
        "target_link_directories",
        "target_link_libraries",
        "target_link_options",
        "target_sources",
        "unset",
        "while",
    });

    // CMake variables/properties keywords (sorted)
    constexpr auto cmakeTypes = std::to_array<std::string_view>({
        "AND",          "BOOL",     "CACHE",  "COMMAND",  "DEFINED",      "EXISTS",   "FATAL_ERROR",
        "FALSE",        "FILEPATH", "FORCE",  "IMPORTED", "INTERFACE",    "INTERNAL", "MATCHES",
        "NOT",          "OFF",      "ON",     "OR",       "PARENT_SCOPE", "PATH",     "PRIVATE",
        "PUBLIC",       "REQUIRED", "STATUS", "STREQUAL", "STRING",       "TRUE",     "VERSION_GREATER",
        "VERSION_LESS",
    });

    // JSON keywords (sorted)
    constexpr auto jsonKeywords = std::to_array<std::string_view>({
        "false",
        "null",
        "true",
    });

    // YAML keywords (sorted)
    constexpr auto yamlKeywords = std::to_array<std::string_view>({
        "False",
        "No",
        "Null",
        "Off",
        "On",
        "True",
        "Yes",
        "false",
        "no",
        "null",
        "off",
        "on",
        "true",
        "yes",
        "~",
    });

    // x86 assembly instructions (sorted, lowercase)
    constexpr auto asmInstructions = std::to_array<std::string_view>({
        "adc",    "add",    "and",    "bsf",     "bsr",    "bt",      "call",     "cbw",     "cdq",
        "cdqe",   "clc",    "cld",    "cli",     "cmova",  "cmovae",  "cmovb",    "cmovbe",  "cmovc",
        "cmove",  "cmovg",  "cmovge", "cmovl",   "cmovle", "cmovna",  "cmovnae",  "cmovnb",  "cmovnbe",
        "cmovnc", "cmovne", "cmovng", "cmovnge", "cmovnl", "cmovnle", "cmovno",   "cmovnp",  "cmovns",
        "cmovnz", "cmovo",  "cmovp",  "cmovs",   "cmovz",  "cmp",     "cmps",     "cmpxchg", "cpuid",
        "cqo",    "cwd",    "cwde",   "dec",     "div",    "enter",   "hlt",      "idiv",    "imul",
        "in",     "inc",    "int",    "iret",    "ja",     "jae",     "jb",       "jbe",     "jc",
        "je",     "jg",     "jge",    "jl",      "jle",    "jmp",     "jna",      "jnae",    "jnb",
        "jnbe",   "jnc",    "jne",    "jng",     "jnge",   "jnl",     "jnle",     "jno",     "jnp",
        "jns",    "jnz",    "jo",     "jp",      "js",     "jz",      "lea",      "leave",   "lock",
        "lods",   "loop",   "loope",  "loopne",  "loopnz", "loopz",   "mov",      "movs",    "movsx",
        "movsxd", "movzx",  "mul",    "neg",     "nop",    "not",     "or",       "out",     "pop",
        "popa",   "popad",  "popf",   "popfd",   "push",   "pusha",   "pushad",   "pushf",   "pushfd",
        "rcl",    "rcr",    "rdtsc",  "rep",     "repe",   "repne",   "repnz",    "repz",    "ret",
        "rol",    "ror",    "sal",    "sar",     "sbb",    "scas",    "seta",     "setae",   "setb",
        "setbe",  "setc",   "sete",   "setg",    "setge",  "setl",    "setle",    "setna",   "setnae",
        "setnb",  "setnbe", "setnc",  "setne",   "setng",  "setnge",  "setnl",    "setnle",  "setno",
        "setnp",  "setns",  "setnz",  "seto",    "setp",   "sets",    "setz",     "shl",     "shr",
        "stc",    "std",    "sti",    "stos",    "sub",    "syscall", "sysenter", "test",    "ud2",
        "xadd",   "xchg",   "xor",
    });

    // x86 registers (sorted, lowercase — matching is case-insensitive)
    constexpr auto asmRegisters = std::to_array<std::string_view>({
        "ah",    "al",     "ax",    "bh",    "bl",    "bp",    "bpl",   "bx",    "ch",    "cl",     "cr0",
        "cr2",   "cr3",    "cr4",   "cs",    "cx",    "dh",    "di",    "dil",   "dl",    "dr0",    "dr1",
        "dr2",   "dr3",    "dr6",   "dr7",   "ds",    "dx",    "eax",   "ebp",   "ebx",   "ecx",    "edi",
        "edx",   "eflags", "es",    "esi",   "esp",   "fs",    "gs",    "mm0",   "mm1",   "mm2",    "mm3",
        "mm4",   "mm5",    "mm6",   "mm7",   "r10",   "r10b",  "r10d",  "r10w",  "r11",   "r11b",   "r11d",
        "r11w",  "r12",    "r12b",  "r12d",  "r12w",  "r13",   "r13b",  "r13d",  "r13w",  "r14",    "r14b",
        "r14d",  "r14w",   "r15",   "r15b",  "r15d",  "r15w",  "r8",    "r8b",   "r8d",   "r8w",    "r9",
        "r9b",   "r9d",    "r9w",   "rax",   "rbp",   "rbx",   "rcx",   "rdi",   "rdx",   "rflags", "rip",
        "rsi",   "rsp",    "si",    "sil",   "sp",    "spl",   "ss",    "st",    "xmm0",  "xmm1",   "xmm10",
        "xmm11", "xmm12",  "xmm13", "xmm14", "xmm15", "xmm2",  "xmm3",  "xmm4",  "xmm5",  "xmm6",   "xmm7",
        "xmm8",  "xmm9",   "ymm0",  "ymm1",  "ymm10", "ymm11", "ymm12", "ymm13", "ymm14", "ymm15",  "ymm2",
        "ymm3",  "ymm4",   "ymm5",  "ymm6",  "ymm7",  "ymm8",  "ymm9",  "zmm0",  "zmm1",  "zmm10",  "zmm11",
        "zmm12", "zmm13",  "zmm14", "zmm15", "zmm2",  "zmm3",  "zmm4",  "zmm5",  "zmm6",  "zmm7",   "zmm8",
        "zmm9",
    });

    // x86 assembly directives — NASM/MASM style (sorted)
    constexpr auto asmDirectives = std::to_array<std::string_view>({
        "align", "bits",   "byte",    "common",  "db",      "dd",    "dq",    "dw",    "dword",
        "equ",   "extern", "global",  "incbin",  "include", "org",   "qword", "resb",  "resd",
        "resq",  "resw",   "section", "segment", "times",   "use16", "use32", "use64", "word",
    });

    // AT&T/GAS directives (stored without leading dot, sorted)
    constexpr auto asmGasDirectives = std::to_array<std::string_view>({
        "align",  "ascii", "asciz",        "bss",    "byte",  "comm",    "data", "endm",    "extern", "file",
        "global", "globl", "intel_syntax", "long",   "macro", "p2align", "quad", "section", "set",    "short",
        "size",   "skip",  "space",        "string", "text",  "type",    "weak", "word",    "zero",
    });

    /// @brief Converts an identifier to lowercase into a stack buffer and returns a string_view.
    /// @param src The source identifier.
    /// @param buf Buffer of at least src.size() characters.
    /// @return A string_view over the lowercase copy.
    auto toLowerInto(std::string_view src, char* buf) -> std::string_view
    {
        for (std::size_t i = 0; i < src.size(); ++i)
            buf[i] = static_cast<char>(src[i] >= 'A' && src[i] <= 'Z' ? src[i] + ('a' - 'A') : src[i]);
        return { buf, src.size() };
    }

    /// @brief Binary search in a sorted array of string_views.
    template <std::size_t N>
    constexpr bool isInSortedArray(std::array<std::string_view, N> const& arr, std::string_view word)
    {
        return std::ranges::binary_search(arr, word);
    }

    // -------------------------------------------------------------------------
    // Fill helpers
    // -------------------------------------------------------------------------

    void fillRange(HighlightMap& map, std::size_t start, std::size_t count, HighlightCategory cat)
    {
        auto const end = std::min(start + count, map.size());
        for (auto i = start; i < end; ++i)
            map[i] = cat;
    }

    // -------------------------------------------------------------------------
    // Per-character number scanning
    // -------------------------------------------------------------------------

    /// @brief Scans a numeric literal starting at pos, returns length consumed.
    std::size_t scanNumber(std::string_view line, std::size_t pos)
    {
        auto i = pos;
        auto const len = line.size();

        // Hex: 0x...
        if (i + 1 < len && line[i] == '0' && (line[i + 1] == 'x' || line[i + 1] == 'X'))
        {
            i += 2;
            while (i < len && isHexDigit(line[i]))
                ++i;
            // Suffix
            while (i < len && (line[i] == 'u' || line[i] == 'U' || line[i] == 'l' || line[i] == 'L'))
                ++i;
            return i - pos;
        }

        // Binary: 0b...
        if (i + 1 < len && line[i] == '0' && (line[i + 1] == 'b' || line[i + 1] == 'B'))
        {
            i += 2;
            while (i < len && (line[i] == '0' || line[i] == '1' || line[i] == '\''))
                ++i;
            return i - pos;
        }

        // Octal or decimal
        while (i < len && (isDigit(line[i]) || line[i] == '\''))
            ++i;

        // Decimal point
        if (i < len && line[i] == '.')
        {
            ++i;
            while (i < len && (isDigit(line[i]) || line[i] == '\''))
                ++i;
        }

        // Exponent
        if (i < len && (line[i] == 'e' || line[i] == 'E'))
        {
            ++i;
            if (i < len && (line[i] == '+' || line[i] == '-'))
                ++i;
            while (i < len && isDigit(line[i]))
                ++i;
        }

        // Suffix (f, F, l, L, u, U, ll, LL, etc.)
        while (i < len
               && (line[i] == 'f' || line[i] == 'F' || line[i] == 'l' || line[i] == 'L' || line[i] == 'u'
                   || line[i] == 'U'))
            ++i;

        return i - pos;
    }

    // -------------------------------------------------------------------------
    // C++ highlighter
    // -------------------------------------------------------------------------

    auto highlightCpp(std::string_view line, HighlightState state) -> std::pair<HighlightMap, HighlightState>
    {
        auto const len = line.size();
        auto map = HighlightMap(len, HighlightCategory::Default);
        auto pos = std::size_t { 0 };

        // Continue block comment from previous line
        if (state == HighlightState::BlockComment)
        {
            while (pos < len)
            {
                if (pos + 1 < len && line[pos] == '*' && line[pos + 1] == '/')
                {
                    map[pos] = HighlightCategory::Comment;
                    map[pos + 1] = HighlightCategory::Comment;
                    pos += 2;
                    state = HighlightState::Normal;
                    break;
                }
                map[pos] = HighlightCategory::Comment;
                ++pos;
            }
            if (state == HighlightState::BlockComment)
            {
                return { std::move(map), state };
            }
        }

        // Continue raw string from previous line
        if (state == HighlightState::RawString)
        {
            while (pos < len)
            {
                // Look for )" to end raw string (simplified — doesn't track delimiter)
                if (pos + 1 < len && line[pos] == ')' && line[pos + 1] == '"')
                {
                    map[pos] = HighlightCategory::String;
                    map[pos + 1] = HighlightCategory::String;
                    pos += 2;
                    state = HighlightState::Normal;
                    break;
                }
                map[pos] = HighlightCategory::String;
                ++pos;
            }
            if (state == HighlightState::RawString)
            {
                return { std::move(map), state };
            }
        }

        while (pos < len)
        {
            auto const ch = line[pos];

            // Line comment
            if (pos + 1 < len && ch == '/' && line[pos + 1] == '/')
            {
                fillRange(map, pos, len - pos, HighlightCategory::Comment);
                return { std::move(map), HighlightState::Normal };
            }

            // Block comment
            if (pos + 1 < len && ch == '/' && line[pos + 1] == '*')
            {
                map[pos] = HighlightCategory::Comment;
                map[pos + 1] = HighlightCategory::Comment;
                pos += 2;
                state = HighlightState::BlockComment;
                while (pos < len)
                {
                    if (pos + 1 < len && line[pos] == '*' && line[pos + 1] == '/')
                    {
                        map[pos] = HighlightCategory::Comment;
                        map[pos + 1] = HighlightCategory::Comment;
                        pos += 2;
                        state = HighlightState::Normal;
                        break;
                    }
                    map[pos] = HighlightCategory::Comment;
                    ++pos;
                }
                continue;
            }

            // Raw string R"(...)"
            if (ch == 'R' && pos + 1 < len && line[pos + 1] == '"')
            {
                map[pos] = HighlightCategory::String;
                map[pos + 1] = HighlightCategory::String;
                pos += 2;
                // Skip delimiter until (
                while (pos < len && line[pos] != '(')
                {
                    map[pos] = HighlightCategory::String;
                    ++pos;
                }
                if (pos < len)
                {
                    map[pos] = HighlightCategory::String;
                    ++pos;
                }
                state = HighlightState::RawString;
                // Search for )" on same line
                while (pos < len)
                {
                    if (pos + 1 < len && line[pos] == ')' && line[pos + 1] == '"')
                    {
                        map[pos] = HighlightCategory::String;
                        map[pos + 1] = HighlightCategory::String;
                        pos += 2;
                        state = HighlightState::Normal;
                        break;
                    }
                    map[pos] = HighlightCategory::String;
                    ++pos;
                }
                continue;
            }

            // String literals
            if (ch == '"' || ch == '\'')
            {
                auto const quote = ch;
                map[pos] = HighlightCategory::String;
                ++pos;
                while (pos < len)
                {
                    if (line[pos] == '\\' && pos + 1 < len)
                    {
                        map[pos] = HighlightCategory::String;
                        map[pos + 1] = HighlightCategory::String;
                        pos += 2;
                        continue;
                    }
                    if (line[pos] == quote)
                    {
                        map[pos] = HighlightCategory::String;
                        ++pos;
                        break;
                    }
                    map[pos] = HighlightCategory::String;
                    ++pos;
                }
                continue;
            }

            // Preprocessor directives
            if (ch == '#')
            {
                // Check if this is a preprocessor line (# at start after optional whitespace)
                auto isPreproc = true;
                for (std::size_t j = 0; j < pos; ++j)
                {
                    if (line[j] != ' ' && line[j] != '\t')
                    {
                        isPreproc = false;
                        break;
                    }
                }
                if (isPreproc)
                {
                    fillRange(map, pos, len - pos, HighlightCategory::Preprocessor);
                    return { std::move(map), HighlightState::Normal };
                }
            }

            // Numbers
            if (isDigit(ch) || (ch == '.' && pos + 1 < len && isDigit(line[pos + 1])))
            {
                auto const numLen = scanNumber(line, pos);
                fillRange(map, pos, numLen, HighlightCategory::Number);
                pos += numLen;
                continue;
            }

            // Identifiers / keywords / types
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_')
            {
                auto const start = pos;
                while (pos < len && isIdentChar(line[pos]))
                    ++pos;
                auto const word = line.substr(start, pos - start);

                if (isInSortedArray(cppKeywords, word))
                    fillRange(map, start, pos - start, HighlightCategory::Keyword);
                else if (isInSortedArray(cppTypes, word))
                    fillRange(map, start, pos - start, HighlightCategory::Type);
                else if (word.starts_with("std"))
                    fillRange(map, start, pos - start, HighlightCategory::Type);
                continue;
            }

            // Operators
            if (isOperatorChar(ch))
            {
                map[pos] = HighlightCategory::Operator;
                ++pos;
                continue;
            }

            // Punctuation
            if (isPunctuationChar(ch))
            {
                map[pos] = HighlightCategory::Punctuation;
                ++pos;
                continue;
            }

            // Default (whitespace, etc.)
            ++pos;
        }

        return { std::move(map), state };
    }

    // -------------------------------------------------------------------------
    // Python highlighter
    // -------------------------------------------------------------------------

    auto highlightPython(std::string_view line, HighlightState state)
        -> std::pair<HighlightMap, HighlightState>
    {
        auto const len = line.size();
        auto map = HighlightMap(len, HighlightCategory::Default);
        auto pos = std::size_t { 0 };

        // Continue triple-quoted string from previous line
        if (state == HighlightState::TripleQuoteString)
        {
            while (pos < len)
            {
                if (pos + 2 < len
                    && ((line[pos] == '"' && line[pos + 1] == '"' && line[pos + 2] == '"')
                        || (line[pos] == '\'' && line[pos + 1] == '\'' && line[pos + 2] == '\'')))
                {
                    map[pos] = HighlightCategory::String;
                    map[pos + 1] = HighlightCategory::String;
                    map[pos + 2] = HighlightCategory::String;
                    pos += 3;
                    state = HighlightState::Normal;
                    break;
                }
                map[pos] = HighlightCategory::String;
                ++pos;
            }
            if (state == HighlightState::TripleQuoteString)
                return { std::move(map), state };
        }

        while (pos < len)
        {
            auto const ch = line[pos];

            // Line comment
            if (ch == '#')
            {
                fillRange(map, pos, len - pos, HighlightCategory::Comment);
                return { std::move(map), state };
            }

            // Decorator
            if (ch == '@' && (pos == 0 || line[pos - 1] == ' ' || line[pos - 1] == '\t'))
            {
                auto const start = pos;
                ++pos;
                while (pos < len && isIdentChar(line[pos]))
                    ++pos;
                fillRange(map, start, pos - start, HighlightCategory::Function);
                continue;
            }

            // Triple-quoted strings
            if (pos + 2 < len
                && ((ch == '"' && line[pos + 1] == '"' && line[pos + 2] == '"')
                    || (ch == '\'' && line[pos + 1] == '\'' && line[pos + 2] == '\'')))
            {
                auto const quote3 = line.substr(pos, 3);
                map[pos] = HighlightCategory::String;
                map[pos + 1] = HighlightCategory::String;
                map[pos + 2] = HighlightCategory::String;
                pos += 3;
                state = HighlightState::TripleQuoteString;
                while (pos < len)
                {
                    if (pos + 2 < len && line.substr(pos, 3) == quote3)
                    {
                        map[pos] = HighlightCategory::String;
                        map[pos + 1] = HighlightCategory::String;
                        map[pos + 2] = HighlightCategory::String;
                        pos += 3;
                        state = HighlightState::Normal;
                        break;
                    }
                    map[pos] = HighlightCategory::String;
                    ++pos;
                }
                continue;
            }

            // Regular strings
            if (ch == '"' || ch == '\'')
            {
                auto const quote = ch;
                map[pos] = HighlightCategory::String;
                ++pos;
                while (pos < len)
                {
                    if (line[pos] == '\\' && pos + 1 < len)
                    {
                        map[pos] = HighlightCategory::String;
                        map[pos + 1] = HighlightCategory::String;
                        pos += 2;
                        continue;
                    }
                    if (line[pos] == quote)
                    {
                        map[pos] = HighlightCategory::String;
                        ++pos;
                        break;
                    }
                    map[pos] = HighlightCategory::String;
                    ++pos;
                }
                continue;
            }

            // Numbers
            if (isDigit(ch) || (ch == '.' && pos + 1 < len && isDigit(line[pos + 1])))
            {
                auto const numLen = scanNumber(line, pos);
                fillRange(map, pos, numLen, HighlightCategory::Number);
                pos += numLen;
                continue;
            }

            // Identifiers / keywords / builtins
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_')
            {
                auto const start = pos;
                while (pos < len && isIdentChar(line[pos]))
                    ++pos;
                auto const word = line.substr(start, pos - start);

                if (isInSortedArray(pythonKeywords, word))
                    fillRange(map, start, pos - start, HighlightCategory::Keyword);
                else if (isInSortedArray(pythonBuiltins, word))
                    fillRange(map, start, pos - start, HighlightCategory::Function);
                continue;
            }

            // Operators
            if (isOperatorChar(ch))
            {
                map[pos] = HighlightCategory::Operator;
                ++pos;
                continue;
            }

            // Punctuation
            if (isPunctuationChar(ch))
            {
                map[pos] = HighlightCategory::Punctuation;
                ++pos;
                continue;
            }

            ++pos;
        }

        return { std::move(map), state };
    }

    // -------------------------------------------------------------------------
    // Bash highlighter
    // -------------------------------------------------------------------------

    auto highlightBash(std::string_view line, HighlightState /*state*/)
        -> std::pair<HighlightMap, HighlightState>
    {
        auto const len = line.size();
        auto map = HighlightMap(len, HighlightCategory::Default);
        auto pos = std::size_t { 0 };

        while (pos < len)
        {
            auto const ch = line[pos];

            // Line comment
            if (ch == '#')
            {
                fillRange(map, pos, len - pos, HighlightCategory::Comment);
                return { std::move(map), HighlightState::Normal };
            }

            // Variable: $VAR or ${VAR}
            if (ch == '$')
            {
                auto const start = pos;
                ++pos;
                if (pos < len && line[pos] == '{')
                {
                    ++pos;
                    while (pos < len && line[pos] != '}')
                        ++pos;
                    if (pos < len)
                        ++pos; // skip '}'
                }
                else if (pos < len && line[pos] == '(')
                {
                    // $(...) command substitution — just color the $
                    fillRange(map, start, 1, HighlightCategory::Variable);
                    continue;
                }
                else
                {
                    while (pos < len && isIdentChar(line[pos]))
                        ++pos;
                }
                fillRange(map, start, pos - start, HighlightCategory::Variable);
                continue;
            }

            // Strings
            if (ch == '"' || ch == '\'')
            {
                auto const quote = ch;
                map[pos] = HighlightCategory::String;
                ++pos;
                while (pos < len)
                {
                    if (quote == '"' && line[pos] == '\\' && pos + 1 < len)
                    {
                        map[pos] = HighlightCategory::String;
                        map[pos + 1] = HighlightCategory::String;
                        pos += 2;
                        continue;
                    }
                    if (line[pos] == quote)
                    {
                        map[pos] = HighlightCategory::String;
                        ++pos;
                        break;
                    }
                    // Highlight variables inside double quotes
                    if (quote == '"' && line[pos] == '$')
                    {
                        auto const varStart = pos;
                        ++pos;
                        if (pos < len && line[pos] == '{')
                        {
                            ++pos;
                            while (pos < len && line[pos] != '}')
                                ++pos;
                            if (pos < len)
                                ++pos;
                        }
                        else
                        {
                            while (pos < len && isIdentChar(line[pos]))
                                ++pos;
                        }
                        fillRange(map, varStart, pos - varStart, HighlightCategory::Variable);
                        continue;
                    }
                    map[pos] = HighlightCategory::String;
                    ++pos;
                }
                continue;
            }

            // Numbers
            if (isDigit(ch))
            {
                auto const numLen = scanNumber(line, pos);
                fillRange(map, pos, numLen, HighlightCategory::Number);
                pos += numLen;
                continue;
            }

            // Identifiers / keywords / builtins
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_')
            {
                auto const start = pos;
                while (pos < len && isIdentChar(line[pos]))
                    ++pos;
                auto const word = line.substr(start, pos - start);

                if (isInSortedArray(bashKeywords, word))
                    fillRange(map, start, pos - start, HighlightCategory::Keyword);
                else if (isInSortedArray(bashBuiltins, word))
                    fillRange(map, start, pos - start, HighlightCategory::Function);
                continue;
            }

            // Operators
            if (isOperatorChar(ch) || ch == '@')
            {
                map[pos] = HighlightCategory::Operator;
                ++pos;
                continue;
            }

            // Punctuation
            if (isPunctuationChar(ch))
            {
                map[pos] = HighlightCategory::Punctuation;
                ++pos;
                continue;
            }

            ++pos;
        }

        return { std::move(map), HighlightState::Normal };
    }

    // -------------------------------------------------------------------------
    // CMake highlighter
    // -------------------------------------------------------------------------

    auto highlightCMake(std::string_view line, HighlightState /*state*/)
        -> std::pair<HighlightMap, HighlightState>
    {
        auto const len = line.size();
        auto map = HighlightMap(len, HighlightCategory::Default);
        auto pos = std::size_t { 0 };

        while (pos < len)
        {
            auto const ch = line[pos];

            // Line comment
            if (ch == '#')
            {
                fillRange(map, pos, len - pos, HighlightCategory::Comment);
                return { std::move(map), HighlightState::Normal };
            }

            // Variable reference ${...}
            if (ch == '$' && pos + 1 < len && line[pos + 1] == '{')
            {
                auto const start = pos;
                pos += 2;
                while (pos < len && line[pos] != '}')
                    ++pos;
                if (pos < len)
                    ++pos;
                fillRange(map, start, pos - start, HighlightCategory::Variable);
                continue;
            }

            // Strings
            if (ch == '"')
            {
                map[pos] = HighlightCategory::String;
                ++pos;
                while (pos < len)
                {
                    if (line[pos] == '\\' && pos + 1 < len)
                    {
                        map[pos] = HighlightCategory::String;
                        map[pos + 1] = HighlightCategory::String;
                        pos += 2;
                        continue;
                    }
                    if (line[pos] == '$' && pos + 1 < len && line[pos + 1] == '{')
                    {
                        auto const varStart = pos;
                        pos += 2;
                        while (pos < len && line[pos] != '}')
                            ++pos;
                        if (pos < len)
                            ++pos;
                        fillRange(map, varStart, pos - varStart, HighlightCategory::Variable);
                        continue;
                    }
                    if (line[pos] == '"')
                    {
                        map[pos] = HighlightCategory::String;
                        ++pos;
                        break;
                    }
                    map[pos] = HighlightCategory::String;
                    ++pos;
                }
                continue;
            }

            // Identifiers / commands / types
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_')
            {
                auto const start = pos;
                while (pos < len && (isIdentChar(line[pos]) || line[pos] == '-'))
                    ++pos;
                auto const word = line.substr(start, pos - start);

                if (isInSortedArray(cmakeKeywords, word))
                    fillRange(map, start, pos - start, HighlightCategory::Keyword);
                else if (isInSortedArray(cmakeTypes, word))
                    fillRange(map, start, pos - start, HighlightCategory::Type);
                continue;
            }

            // Punctuation
            if (isPunctuationChar(ch))
            {
                map[pos] = HighlightCategory::Punctuation;
                ++pos;
                continue;
            }

            ++pos;
        }

        return { std::move(map), HighlightState::Normal };
    }

    // -------------------------------------------------------------------------
    // Markdown highlighter
    // -------------------------------------------------------------------------

    auto highlightMarkdown(std::string_view line, HighlightState /*state*/)
        -> std::pair<HighlightMap, HighlightState>
    {
        auto const len = line.size();
        auto map = HighlightMap(len, HighlightCategory::Default);

        if (len == 0)
            return { std::move(map), HighlightState::Normal };

        // Headings: # ...
        if (line[0] == '#')
        {
            fillRange(map, 0, len, HighlightCategory::Keyword);
            return { std::move(map), HighlightState::Normal };
        }

        auto pos = std::size_t { 0 };
        while (pos < len)
        {
            auto const ch = line[pos];

            // Bold/italic markers
            if (ch == '*' || ch == '_')
            {
                map[pos] = HighlightCategory::Operator;
                ++pos;
                continue;
            }

            // Backtick code spans
            if (ch == '`')
            {
                auto const start = pos;
                ++pos;
                while (pos < len && line[pos] != '`')
                    ++pos;
                if (pos < len)
                    ++pos;
                fillRange(map, start, pos - start, HighlightCategory::String);
                continue;
            }

            // Links: [text](url)
            if (ch == '[')
            {
                auto const start = pos;
                auto const closeBracket = line.find(']', pos + 1);
                if (closeBracket != std::string_view::npos && closeBracket + 1 < len
                    && line[closeBracket + 1] == '(')
                {
                    auto const closeParen = line.find(')', closeBracket + 2);
                    if (closeParen != std::string_view::npos)
                    {
                        fillRange(map, start, closeParen + 1 - start, HighlightCategory::Function);
                        pos = closeParen + 1;
                        continue;
                    }
                }
            }

            ++pos;
        }

        return { std::move(map), HighlightState::Normal };
    }

    // -------------------------------------------------------------------------
    // JSON highlighter
    // -------------------------------------------------------------------------

    auto highlightJson(std::string_view line, HighlightState /*state*/)
        -> std::pair<HighlightMap, HighlightState>
    {
        auto const len = line.size();
        auto map = HighlightMap(len, HighlightCategory::Default);
        auto pos = std::size_t { 0 };

        while (pos < len)
        {
            auto const ch = line[pos];

            // Strings — check if this is a key (followed by : after closing quote)
            if (ch == '"')
            {
                auto const start = pos;
                map[pos] = HighlightCategory::String;
                ++pos;
                while (pos < len)
                {
                    if (line[pos] == '\\' && pos + 1 < len)
                    {
                        map[pos] = HighlightCategory::String;
                        map[pos + 1] = HighlightCategory::String;
                        pos += 2;
                        continue;
                    }
                    if (line[pos] == '"')
                    {
                        map[pos] = HighlightCategory::String;
                        ++pos;
                        break;
                    }
                    map[pos] = HighlightCategory::String;
                    ++pos;
                }

                // Check if followed by colon (making this a key)
                auto afterQuote = pos;
                while (afterQuote < len && (line[afterQuote] == ' ' || line[afterQuote] == '\t'))
                    ++afterQuote;
                if (afterQuote < len && line[afterQuote] == ':')
                    fillRange(map, start, pos - start, HighlightCategory::Type);
                continue;
            }

            // Numbers
            if (isDigit(ch) || (ch == '-' && pos + 1 < len && isDigit(line[pos + 1])))
            {
                if (ch == '-')
                {
                    map[pos] = HighlightCategory::Number;
                    ++pos;
                }
                auto const numLen = scanNumber(line, pos);
                fillRange(map, pos, numLen, HighlightCategory::Number);
                pos += numLen;
                continue;
            }

            // Keywords: true, false, null
            if ((ch >= 'a' && ch <= 'z'))
            {
                auto const start = pos;
                while (pos < len && (line[pos] >= 'a' && line[pos] <= 'z'))
                    ++pos;
                auto const word = line.substr(start, pos - start);
                if (isInSortedArray(jsonKeywords, word))
                    fillRange(map, start, pos - start, HighlightCategory::Keyword);
                continue;
            }

            // Punctuation
            if (ch == '{' || ch == '}' || ch == '[' || ch == ']' || ch == ',' || ch == ':')
            {
                map[pos] = HighlightCategory::Punctuation;
                ++pos;
                continue;
            }

            ++pos;
        }

        return { std::move(map), HighlightState::Normal };
    }

    // -------------------------------------------------------------------------
    // YAML highlighter
    // -------------------------------------------------------------------------

    auto highlightYaml(std::string_view line, HighlightState /*state*/)
        -> std::pair<HighlightMap, HighlightState>
    {
        auto const len = line.size();
        auto map = HighlightMap(len, HighlightCategory::Default);
        auto pos = std::size_t { 0 };

        // Skip leading whitespace
        while (pos < len && (line[pos] == ' ' || line[pos] == '\t'))
            ++pos;

        if (pos >= len)
            return { std::move(map), HighlightState::Normal };

        // Comment
        if (line[pos] == '#')
        {
            fillRange(map, pos, len - pos, HighlightCategory::Comment);
            return { std::move(map), HighlightState::Normal };
        }

        // Document markers
        if (line == "---" || line == "...")
        {
            fillRange(map, 0, len, HighlightCategory::Keyword);
            return { std::move(map), HighlightState::Normal };
        }

        // List marker
        if (line[pos] == '-' && pos + 1 < len && line[pos + 1] == ' ')
        {
            map[pos] = HighlightCategory::Punctuation;
            pos += 2;
        }

        // Key: value detection
        while (pos < len)
        {
            auto const ch = line[pos];

            // Check for key (identifier followed by colon)
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || ch == '-')
            {
                auto const start = pos;
                while (pos < len && line[pos] != ':' && line[pos] != ' ' && line[pos] != '\t'
                       && line[pos] != '#')
                    ++pos;
                if (pos < len && line[pos] == ':')
                {
                    fillRange(map, start, pos - start, HighlightCategory::Type);
                    map[pos] = HighlightCategory::Punctuation;
                    ++pos;
                    // Skip space after colon
                    while (pos < len && (line[pos] == ' ' || line[pos] == '\t'))
                        ++pos;

                    // Highlight the value
                    if (pos < len)
                    {
                        // Comment in value
                        auto const commentPos = line.find(" #", pos);
                        auto const valueEnd = (commentPos != std::string_view::npos) ? commentPos : len;

                        auto const valueStr = line.substr(pos, valueEnd - pos);
                        // Trim trailing whitespace for keyword check
                        auto trimmed = valueStr;
                        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t'))
                            trimmed.remove_suffix(1);

                        if (isInSortedArray(yamlKeywords, trimmed))
                            fillRange(map, pos, valueEnd - pos, HighlightCategory::Keyword);
                        else if (!trimmed.empty()
                                 && (isDigit(trimmed[0])
                                     || (trimmed[0] == '-' && trimmed.size() > 1 && isDigit(trimmed[1]))))
                            fillRange(map, pos, valueEnd - pos, HighlightCategory::Number);
                        else if (!trimmed.empty() && (trimmed[0] == '"' || trimmed[0] == '\''))
                            fillRange(map, pos, valueEnd - pos, HighlightCategory::String);

                        pos = valueEnd;
                        // Highlight trailing comment
                        if (commentPos != std::string_view::npos)
                        {
                            fillRange(map, commentPos, len - commentPos, HighlightCategory::Comment);
                            pos = len;
                        }
                    }
                    continue;
                }
                // Not a key, check if it's a keyword value
                auto const word = line.substr(start, pos - start);
                if (isInSortedArray(yamlKeywords, word))
                    fillRange(map, start, pos - start, HighlightCategory::Keyword);
                continue;
            }

            // Strings
            if (ch == '"' || ch == '\'')
            {
                auto const quote = ch;
                map[pos] = HighlightCategory::String;
                ++pos;
                while (pos < len)
                {
                    if (line[pos] == '\\' && pos + 1 < len)
                    {
                        map[pos] = HighlightCategory::String;
                        map[pos + 1] = HighlightCategory::String;
                        pos += 2;
                        continue;
                    }
                    if (line[pos] == quote)
                    {
                        map[pos] = HighlightCategory::String;
                        ++pos;
                        break;
                    }
                    map[pos] = HighlightCategory::String;
                    ++pos;
                }
                continue;
            }

            // Comment
            if (ch == '#')
            {
                fillRange(map, pos, len - pos, HighlightCategory::Comment);
                return { std::move(map), HighlightState::Normal };
            }

            // Numbers
            if (isDigit(ch))
            {
                auto const numLen = scanNumber(line, pos);
                fillRange(map, pos, numLen, HighlightCategory::Number);
                pos += numLen;
                continue;
            }

            ++pos;
        }

        return { std::move(map), HighlightState::Normal };
    }

    // -------------------------------------------------------------------------
    // Git diff highlighter
    // -------------------------------------------------------------------------

    auto highlightGitDiff(std::string_view line, HighlightState /*state*/)
        -> std::pair<HighlightMap, HighlightState>
    {
        auto const len = line.size();
        auto map = HighlightMap(len, HighlightCategory::Default);

        if (len == 0)
            return { std::move(map), HighlightState::Normal };

        // Diff headers: diff, index, ---, +++
        if (line.starts_with("diff ") || line.starts_with("index ") || line.starts_with("--- ")
            || line.starts_with("+++ "))
        {
            fillRange(map, 0, len, HighlightCategory::Comment);
            return { std::move(map), HighlightState::Normal };
        }

        // Hunk headers: @@ ... @@
        if (line.starts_with("@@"))
        {
            fillRange(map, 0, len, HighlightCategory::Keyword);
            return { std::move(map), HighlightState::Normal };
        }

        // Addition lines: +
        if (line[0] == '+')
        {
            fillRange(map, 0, len, HighlightCategory::Constructor);
            return { std::move(map), HighlightState::Normal };
        }

        // Deletion lines: -
        if (line[0] == '-')
        {
            fillRange(map, 0, len, HighlightCategory::Variable);
            return { std::move(map), HighlightState::Normal };
        }

        return { std::move(map), HighlightState::Normal };
    }

    // -------------------------------------------------------------------------
    // x86 Assembly highlighter (Intel + AT&T syntax)
    // -------------------------------------------------------------------------

    auto highlightAssembly(std::string_view line, HighlightState state)
        -> std::pair<HighlightMap, HighlightState>
    {
        auto const len = line.size();
        auto map = HighlightMap(len, HighlightCategory::Default);
        auto pos = std::size_t { 0 };

        // Continue block comment from previous line
        if (state == HighlightState::BlockComment)
        {
            while (pos < len)
            {
                if (pos + 1 < len && line[pos] == '*' && line[pos + 1] == '/')
                {
                    map[pos] = HighlightCategory::Comment;
                    map[pos + 1] = HighlightCategory::Comment;
                    pos += 2;
                    state = HighlightState::Normal;
                    break;
                }
                map[pos] = HighlightCategory::Comment;
                ++pos;
            }
            if (state == HighlightState::BlockComment)
                return { std::move(map), state };
        }

        while (pos < len)
        {
            auto const ch = line[pos];

            // Intel-style line comment: ;
            if (ch == ';')
            {
                fillRange(map, pos, len - pos, HighlightCategory::Comment);
                return { std::move(map), HighlightState::Normal };
            }

            // AT&T/GAS line comment: #
            if (ch == '#')
            {
                fillRange(map, pos, len - pos, HighlightCategory::Comment);
                return { std::move(map), HighlightState::Normal };
            }

            // C-style block comment: /* ... */
            if (pos + 1 < len && ch == '/' && line[pos + 1] == '*')
            {
                map[pos] = HighlightCategory::Comment;
                map[pos + 1] = HighlightCategory::Comment;
                pos += 2;
                state = HighlightState::BlockComment;
                while (pos < len)
                {
                    if (pos + 1 < len && line[pos] == '*' && line[pos + 1] == '/')
                    {
                        map[pos] = HighlightCategory::Comment;
                        map[pos + 1] = HighlightCategory::Comment;
                        pos += 2;
                        state = HighlightState::Normal;
                        break;
                    }
                    map[pos] = HighlightCategory::Comment;
                    ++pos;
                }
                continue;
            }

            // String literals
            if (ch == '"' || ch == '\'')
            {
                auto const quote = ch;
                map[pos] = HighlightCategory::String;
                ++pos;
                while (pos < len)
                {
                    if (line[pos] == '\\' && pos + 1 < len)
                    {
                        map[pos] = HighlightCategory::String;
                        map[pos + 1] = HighlightCategory::String;
                        pos += 2;
                        continue;
                    }
                    if (line[pos] == quote)
                    {
                        map[pos] = HighlightCategory::String;
                        ++pos;
                        break;
                    }
                    map[pos] = HighlightCategory::String;
                    ++pos;
                }
                continue;
            }

            // GAS directives: .identifier
            if (ch == '.' && pos + 1 < len
                && ((line[pos + 1] >= 'a' && line[pos + 1] <= 'z')
                    || (line[pos + 1] >= 'A' && line[pos + 1] <= 'Z') || line[pos + 1] == '_'))
            {
                auto const start = pos;
                ++pos; // skip '.'
                while (pos < len && isIdentChar(line[pos]))
                    ++pos;
                auto const directive = line.substr(start + 1, pos - start - 1);
                char lowerBuf[64];
                auto const lowerDirective = toLowerInto(directive, lowerBuf);
                if (isInSortedArray(asmGasDirectives, lowerDirective))
                    fillRange(map, start, pos - start, HighlightCategory::Preprocessor);
                continue;
            }

            // AT&T register syntax: %register
            if (ch == '%' && pos + 1 < len
                && ((line[pos + 1] >= 'a' && line[pos + 1] <= 'z')
                    || (line[pos + 1] >= 'A' && line[pos + 1] <= 'Z')))
            {
                auto const start = pos;
                ++pos; // skip '%'
                while (pos < len && isIdentChar(line[pos]))
                    ++pos;
                auto const regName = line.substr(start + 1, pos - start - 1);
                char lowerBuf[64];
                auto const lowerReg = toLowerInto(regName, lowerBuf);
                if (isInSortedArray(asmRegisters, lowerReg))
                    fillRange(map, start, pos - start, HighlightCategory::Variable);
                continue;
            }

            // AT&T immediate: $number
            if (ch == '$' && pos + 1 < len
                && (isDigit(line[pos + 1])
                    || (pos + 2 < len && line[pos + 1] == '0'
                        && (line[pos + 2] == 'x' || line[pos + 2] == 'X'))))
            {
                map[pos] = HighlightCategory::Number;
                ++pos;
                auto const numLen = scanNumber(line, pos);
                fillRange(map, pos, numLen, HighlightCategory::Number);
                pos += numLen;
                continue;
            }

            // Numbers: hex (0x), binary (0b), decimal
            if (isDigit(ch))
            {
                auto const numStart = pos;
                auto const numLen = scanNumber(line, pos);
                pos += numLen;
                // Check for Intel hex suffix (e.g., 0FFh)
                if (pos < len && (line[pos] == 'h' || line[pos] == 'H'))
                    ++pos;
                fillRange(map, numStart, pos - numStart, HighlightCategory::Number);
                continue;
            }

            // Identifiers: instructions, registers, directives, labels
            if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_')
            {
                auto const start = pos;
                while (pos < len && isIdentChar(line[pos]))
                    ++pos;
                auto const word = line.substr(start, pos - start);

                // Label: identifier followed by ':'
                if (pos < len && line[pos] == ':')
                {
                    fillRange(map, start, pos - start, HighlightCategory::Function);
                    map[pos] = HighlightCategory::Punctuation;
                    ++pos;
                    continue;
                }

                // Case-insensitive matching
                char lowerBuf[64];
                auto const lower = toLowerInto(word, lowerBuf);

                // Check instructions, including AT&T suffix stripping (b/w/l/q)
                auto isInstruction = isInSortedArray(asmInstructions, lower);
                if (!isInstruction && lower.size() > 1)
                {
                    auto const lastCh = lower.back();
                    if (lastCh == 'b' || lastCh == 'w' || lastCh == 'l' || lastCh == 'q')
                        isInstruction = isInSortedArray(asmInstructions, lower.substr(0, lower.size() - 1));
                }

                if (isInstruction)
                    fillRange(map, start, pos - start, HighlightCategory::Keyword);
                else if (isInSortedArray(asmRegisters, lower))
                    fillRange(map, start, pos - start, HighlightCategory::Variable);
                else if (isInSortedArray(asmDirectives, lower))
                    fillRange(map, start, pos - start, HighlightCategory::Preprocessor);
                continue;
            }

            // Operators / punctuation
            if (ch == '+' || ch == '-' || ch == '*')
            {
                map[pos] = HighlightCategory::Operator;
                ++pos;
                continue;
            }
            if (ch == '[' || ch == ']' || ch == '(' || ch == ')' || ch == ',')
            {
                map[pos] = HighlightCategory::Punctuation;
                ++pos;
                continue;
            }

            ++pos;
        }

        return { std::move(map), state };
    }

} // namespace

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------

auto detectLanguageFromPath(std::string_view filePath) -> LanguageId
{
    // Handle CMakeLists.txt specially
    if (filePath.ends_with("CMakeLists.txt"))
        return LanguageId::CMake;

    // Handle Makefile
    if (filePath.ends_with("Makefile") || filePath.ends_with("makefile"))
        return LanguageId::Bash;

    // Handle Dockerfile
    if (filePath.ends_with("Dockerfile"))
        return LanguageId::Bash;

    // Extract extension
    auto const dotPos = filePath.rfind('.');
    if (dotPos == std::string_view::npos)
        return LanguageId::None;

    return detectLanguageFromExtension(filePath.substr(dotPos));
}

void registerEndoHighlighter(HighlightFunction fn)
{
    endoHighlighter() = std::move(fn);
}

auto highlightLine(std::string_view line, LanguageId language, HighlightState state)
    -> std::pair<HighlightMap, HighlightState>
{
    if (line.empty() || language == LanguageId::None)
        return { HighlightMap {}, state };

    switch (language)
    {
        case LanguageId::Cpp: return highlightCpp(line, state);
        case LanguageId::Python: return highlightPython(line, state);
        case LanguageId::Bash: return highlightBash(line, state);
        case LanguageId::CMake: return highlightCMake(line, state);
        case LanguageId::Markdown: return highlightMarkdown(line, state);
        case LanguageId::Json: return highlightJson(line, state);
        case LanguageId::Yaml: return highlightYaml(line, state);
        case LanguageId::GitDiff: return highlightGitDiff(line, state);
        case LanguageId::Assembly: return highlightAssembly(line, state);
        case LanguageId::Endo:
            if (endoHighlighter())
                return endoHighlighter()(line, state);
            break;
        case LanguageId::None: break;
    }

    return { HighlightMap(line.size(), HighlightCategory::Default), state };
}

auto categoryColor(HighlightCategory cat, Theme const& theme) -> RgbColor
{
    switch (cat)
    {
        case HighlightCategory::Keyword: return theme.syntaxColors.keyword;
        case HighlightCategory::Number: return theme.syntaxColors.number;
        case HighlightCategory::String: return theme.syntaxColors.string;
        case HighlightCategory::Operator: return theme.syntaxColors.op;
        case HighlightCategory::Variable: return theme.syntaxColors.variable;
        case HighlightCategory::Constructor: return theme.syntaxColors.constructor;
        case HighlightCategory::Comment: return theme.syntaxColors.comment;
        case HighlightCategory::Type: return theme.syntaxColors.type;
        case HighlightCategory::Punctuation: return theme.syntaxColors.punctuation;
        case HighlightCategory::Function: return theme.syntaxColors.function;
        case HighlightCategory::Preprocessor: return theme.syntaxColors.keyword;
        case HighlightCategory::Default: return theme.syntaxColors.defaultText;
    }
    return theme.syntaxColors.defaultText;
}

void renderHighlightedLine(TerminalOutput& output,
                           std::string_view text,
                           HighlightMap const& highlights,
                           Style baseStyle,
                           Theme const& theme)
{
    if (text.empty())
        return;

    // Handle case where highlights map doesn't cover the full text (UTF-8 safety)
    auto const hlSize = highlights.size();

    auto pos = std::size_t { 0 };
    while (pos < text.size())
    {
        auto const cat = (pos < hlSize) ? highlights[pos] : HighlightCategory::Default;

        // Find the end of the span with the same category
        auto spanEnd = pos + 1;
        while (spanEnd < text.size() && spanEnd < hlSize && highlights[spanEnd] == cat)
            ++spanEnd;
        // Extend through any remaining bytes if we're past the highlight map
        if (spanEnd >= hlSize && pos < hlSize)
        {
            while (spanEnd < text.size())
                ++spanEnd;
        }

        auto style = baseStyle;
        style.fg = categoryColor(cat, theme);

        output.writeText(text.substr(pos, spanEnd - pos), style);
        pos = spanEnd;
    }
}

auto renderHighlightedLineToString(std::string_view text, HighlightMap const& highlights, Theme const& theme)
    -> std::string
{
    if (text.empty())
        return {};

    auto result = std::string {};
    result.reserve(text.size() * 2); // rough estimate for ANSI overhead

    auto const hlSize = highlights.size();
    auto pos = std::size_t { 0 };

    while (pos < text.size())
    {
        auto const cat = (pos < hlSize) ? highlights[pos] : HighlightCategory::Default;

        // Find the end of the span with the same category
        auto spanEnd = pos + 1;
        while (spanEnd < text.size() && spanEnd < hlSize && highlights[spanEnd] == cat)
            ++spanEnd;
        if (spanEnd >= hlSize && pos < hlSize)
        {
            while (spanEnd < text.size())
                ++spanEnd;
        }

        auto const color = categoryColor(cat, theme);
        result += std::format("\033[38;2;{};{};{}m", color.r, color.g, color.b);
        result.append(text.substr(pos, spanEnd - pos));

        pos = spanEnd;
    }

    result += "\033[m";
    return result;
}

} // namespace tui
