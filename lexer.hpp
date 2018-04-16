#ifndef SLUA_LEXER_HPP_
#define SLUA_LEXER_HPP_

#include <cstdint>

#include <string>
#include <unordered_map>
#include <vector>

#include <gsl/gsl>

namespace slua {
struct Lexeme {
  enum class Type {
    kNull = 0,

    kPlus = 1,
    kDash,
    kAsterisk,
    kSlash,
    kPercent,
    kCaret,
    kHash,
    kTilde,
    kLeftShift,
    kRightShift,
    kFloorDivision,
    kEqual,
    kNotEqual,
    kLessThanOrEqual,
    kGreaterThanOrEqual,
    kLessThan,
    kGreaterThan,
    kAssignment,
    kLeftBracket,
    kRightBracket,
    kLeftBrace,
    kRightBrace,
    kLeftSquareBracket,
    kRightSquareBracket,
    kDot,
    kConcat,
    kSemicolon,
    kComma,
    kColon,
    kAmpersand,
    kVerticalBar,

    kSymbol = 51,
    kLiteralInteger,
    kLiteralFloat,
    kLiteralString,

    kKeywordEnd = 101,
    kKeywordIf,
    kKeywordThen,
    kKeywordElse,
    kKeywordElseif,
    kKeywordFor,
    kKeywordWhile,
    kKeywordNil,
    kKeywordAnd,
    kKeywordOr,
    kKeywordNot,
    kKeywordTrue,
    kKeywordFalse,
    kKeywordBreak,
    kKeywordDo,
    kKeywordFunction,
    kKeywordIn,
    kKeywordLocal,
    kKeywordRepeat,
    kKeywordReturn,
    kKeywordUntil,
  };

  int line_number{-1};
  Type type{Type::kNull};

  union {
    gsl::index symbol_name;
    gsl::index string_value;
    std::int64_t integer_value;
    double float_value;
  } data{};

  constexpr Lexeme() noexcept = default;
  constexpr explicit Lexeme(int line_number) noexcept
      : line_number{line_number} {}
};

constexpr Lexeme kNullLexeme{};

class Lexer {
 public:
  Lexer();

  std::vector<Lexeme> Lex(const std::string& input,
                          std::vector<std::string>& symbols,
                          std::vector<std::string>& string_literals) const;

 private:
  std::unordered_map<std::string, Lexeme::Type> keyword_map_;
};
}  // namespace slua

#endif  // SLUA_LEXER_HPP_
