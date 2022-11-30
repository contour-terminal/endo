#pragma aonce

#include <memory>
#include <string>
#include <string_view>

namespace contrair
{

enum class Token
{
    Invalid,

    AmpNumber,      // '&' DIGIT+
    Backslash,      // '\'
    DollarDollar,   // $$
    DollarName,     // $NAME
    DollarNot,      // $!
    DollarNumber,   // '$' DIGIT+
    EndOfInput,     // EOF
    EndOfLine,      // LF
    Equal,          // =
    Greater,        // >
    GreaterEqual,   // >=
    GreaterGreater, // >>
    Less,           // <
    LessEqual,      // <=
    LessLess,       // <<
    LessRndOpen,    // <(
    Not,            // !
    Number,         // DIGIT+
    Pipe,           // |
    RndClose,       // )
    RndOpen,        // (
    Semicolon,      // ;
    String,         // "..." and '...'
    Word,           // space delimited text not containing any of the unescaped symbols above
};

enum class BuiltinFunction
{
    Exit,
    Cd,
    Pwd,
    Env,
    Fg,
    Bg,
    Read,   // read VAR
    Time,   // time COMMAND
    If,     // if (EXPR) COMMAND [else COMMAND]
    While,  // while (EXPR) COMMAND
};

struct SourceLocation
{
    int line;
    int column;            // grapheme-cluster aware index
    std::string_view name; // e.g. stdin, or a filename
};

struct TokenInfo
{
    Token token;
    std::string literal;
};

class Source
{
  public:
    virtual ~Source() = default;

    virtual void rewind() = 0;
    [[nodiscard]] virtual char32_t readChar() = 0;
    [[nodiscard]] virtual std::u32string readGraphemeCluster() = 0;
    [[nodiscard]] virtual SourceLocation currentSourceLocation() const noexcept = 0;
};

class StdinSource final: public Source
{
  public:
    void rewind() override;
    [[nodiscard]] char32_t readChar() override;
    [[nodiscard]] std::u32string readGraphemeCluster() override;
    [[nodiscard]] SourceLocation currentSourceLocation() const noexcept override;

  private:
    SourceLocation _location;
};

class Lexer
{
  public:
    explicit Lexer(std::unique_ptr<Source> source);
    Token nextToken();

    [[nodiscard]] Token currentToken() const noexcept { return _currentToken.token; }
    [[nodiscard]] std::string const& currentLiteral() const noexcept { return _currentToken.literal; }

  private:
    void consumeWhitespace();
    void consumeWord();
    char32_t nextChar();
    Token confirmToken(Token token);

    std::unique_ptr<Source> _source;
    char32_t _currentChar = 0;
    TokenInfo _currentToken = TokenInfo {};
    TokenInfo _nextToken = TokenInfo {};
};

} // namespace contrair
