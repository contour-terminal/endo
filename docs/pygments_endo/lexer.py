"""
Pygments lexer for the Endo language.

Endo is a modern shell language that blends POSIX shell semantics with
F#-style functional programming constructs.
"""

from pygments.lexer import RegexLexer, bygroups, include, words
from pygments.token import (
    Comment,
    Error,
    Keyword,
    Name,
    Number,
    Operator,
    Punctuation,
    String,
    Text,
    Token,
)


class EndoLexer(RegexLexer):
    """Pygments lexer for the Endo language."""

    name = "Endo"
    aliases = ["endo"]
    filenames = ["*.endo"]

    tokens = {
        "root": [
            # Whitespace
            (r"\s+", Text),

            # Single-line comments: // and #
            (r"//[^\n]*", Comment.Single),
            (r"#[^\n]*", Comment.Single),

            # Nested multi-line comments: (* ... *)
            (r"\(\*", Comment.Multiline, "comment"),

            # F# interpolated strings: $"..."
            (r'\$"', String.Interpol, "fstring"),

            # Double-quoted strings with interpolation
            (r'"', String.Double, "dqstring"),

            # Single-quoted strings (literal, no interpolation)
            (r"'[^']*'", String.Single),

            # Numeric literals — hex, octal, binary, float, integer
            (r"0[xX][0-9a-fA-F_]+", Number.Hex),
            (r"0[oO][0-7_]+", Number.Oct),
            (r"0[bB][01_]+", Number.Bin),
            (r"[0-9]+\.[0-9]+([eE][+-]?[0-9]+)?", Number.Float),
            (r"[0-9]+[eE][+-]?[0-9]+", Number.Float),
            (r"[0-9]+", Number.Integer),

            # Multi-character operators
            (r"\|>", Operator),
            (r">>", Operator),
            (r"<<", Operator),
            (r"::", Operator),
            (r"->", Operator),
            (r"<-", Operator),
            (r"\.\.", Operator),
            (r"\*\*", Operator),
            (r"==", Operator),
            (r"!=", Operator),
            (r">=", Operator),
            (r"<=", Operator),
            (r"&&", Operator),
            (r"\|\|", Operator),
            (r"\?\|", Operator),

            # Single-character operators
            (r"[|+\-*/%<>=?!@#]", Operator),

            # Shell variables
            (r"\$\{[^}]+\}", Name.Variable),
            (r"\$[?$!0-9]", Name.Variable),
            (r"\$[A-Za-z_][A-Za-z0-9_]*", Name.Variable),

            # Keywords — F# style
            (
                words(
                    (
                        "let",
                        "mut",
                        "fun",
                        "match",
                        "with",
                        "when",
                        "type",
                        "of",
                        "rec",
                        "and",
                        "as",
                        "try",
                        "finally",
                        "in",
                        "do",
                        "done",
                        "global",
                    ),
                    prefix=r"\b",
                    suffix=r"\b",
                ),
                Keyword,
            ),

            # Keywords — shell style
            (
                words(
                    (
                        "if",
                        "then",
                        "else",
                        "elif",
                        "fi",
                        "for",
                        "while",
                        "case",
                        "esac",
                        "return",
                        "break",
                        "continue",
                        "export",
                        "import",
                    ),
                    prefix=r"\b",
                    suffix=r"\b",
                ),
                Keyword,
            ),

            # Constants
            (words(("true", "false"), prefix=r"\b", suffix=r"\b"), Keyword.Constant),

            # Constructors
            (
                words(
                    ("Some", "None", "Ok", "Error"),
                    prefix=r"\b",
                    suffix=r"\b",
                ),
                Name.Builtin,
            ),

            # Type keywords
            (
                words(
                    (
                        "int",
                        "float",
                        "str",
                        "bool",
                        "unit",
                        "option",
                        "result",
                        "list",
                    ),
                    prefix=r"\b",
                    suffix=r"\b",
                ),
                Keyword.Type,
            ),

            # Builtins
            (
                words(
                    (
                        "print",
                        "println",
                        "map",
                        "filter",
                        "fold",
                        "head",
                        "tail",
                        "length",
                        "sort",
                        "reverse",
                        "zip",
                        "flatten",
                        "take",
                        "drop",
                        "find",
                        "exists",
                        "forall",
                        "reduce",
                        "distinct",
                        "lines",
                        "words",
                        "cd",
                        "echo",
                        "exit",
                        "pwd",
                        "env",
                        "fetch",
                        "each",
                        "contains",
                        "startsWith",
                        "endsWith",
                        "string_split",
                        "string_join",
                        "string_length",
                        "list_range",
                        "fst",
                        "snd",
                    ),
                    prefix=r"\b",
                    suffix=r"\b",
                ),
                Name.Builtin,
            ),

            # Identifiers
            (r"[a-zA-Z_][a-zA-Z0-9_]*", Name),

            # Punctuation
            (r"[(){}\[\];,.]", Punctuation),
        ],

        # Nested multi-line comment state: (* ... *)
        "comment": [
            (r"[^(*)]+" , Comment.Multiline),
            (r"\(\*", Comment.Multiline, "#push"),  # nested open
            (r"\*\)", Comment.Multiline, "#pop"),    # close
            (r"[(*)]", Comment.Multiline),
        ],

        # F# interpolated string: $"...{expr}..."
        "fstring": [
            (r'[^"\\{]+', String.Interpol),
            (r"\\.", String.Escape),
            (r"\{", Punctuation, "fstring-expr"),
            (r'"', String.Interpol, "#pop"),
        ],

        # Expression inside {…} within an f-string
        "fstring-expr": [
            (r"[^}]+", Name),
            (r"\}", Punctuation, "#pop"),
        ],

        # Double-quoted string with shell-style interpolation
        "dqstring": [
            (r'[^"\\$]+', String.Double),
            (r"\\.", String.Escape),
            (r"\$\{[^}]+\}", String.Interpol),
            (r"\$\([^)]+\)", String.Interpol),
            (r"\$[A-Za-z_][A-Za-z0-9_]*", String.Interpol),
            (r'"', String.Double, "#pop"),
        ],
    }
