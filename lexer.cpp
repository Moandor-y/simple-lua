#include "lexer.hpp"

#include <cctype>
#include <cstdint>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gsl/gsl>

namespace slua {
namespace {
using gsl::index;

enum class State {
  kIdle,
  kSymbol,
  kIntegerLiteral,
  kFloatLiteral,
  kStringLiteral,
  kCommentStart,
  kCommentLine,
  kCommentBlock,
};
}  // namespace

Lexer::Lexer() {
  keyword_map_["end"] = Lexeme::Type::kKeywordEnd;
  keyword_map_["if"] = Lexeme::Type::kKeywordIf;
  keyword_map_["then"] = Lexeme::Type::kKeywordThen;
  keyword_map_["else"] = Lexeme::Type::kKeywordElse;
  keyword_map_["elseif"] = Lexeme::Type::kKeywordElseif;
  keyword_map_["for"] = Lexeme::Type::kKeywordFor;
  keyword_map_["while"] = Lexeme::Type::kKeywordWhile;
  keyword_map_["nil"] = Lexeme::Type::kKeywordNil;
  keyword_map_["and"] = Lexeme::Type::kKeywordAnd;
  keyword_map_["or"] = Lexeme::Type::kKeywordOr;
  keyword_map_["not"] = Lexeme::Type::kKeywordNot;
  keyword_map_["true"] = Lexeme::Type::kKeywordTrue;
  keyword_map_["false"] = Lexeme::Type::kKeywordFalse;
  keyword_map_["break"] = Lexeme::Type::kKeywordBreak;
  keyword_map_["do"] = Lexeme::Type::kKeywordDo;
  keyword_map_["function"] = Lexeme::Type::kKeywordFunction;
  keyword_map_["in"] = Lexeme::Type::kKeywordIn;
  keyword_map_["local"] = Lexeme::Type::kKeywordLocal;
  keyword_map_["repeat"] = Lexeme::Type::kKeywordRepeat;
  keyword_map_["return"] = Lexeme::Type::kKeywordReturn;
  keyword_map_["until"] = Lexeme::Type::kKeywordUntil;
}

std::vector<Lexeme> Lexer::Lex(
    const std::string& input, std::vector<std::string>& symbols,
    std::vector<std::string>& string_literals) const {
  std::vector<Lexeme> result;
  State state = State::kIdle;
  std::string buffer;
  char quote = 0;
  std::unordered_map<std::string, int> symbol_table;
  std::unordered_map<std::string, int> string_table;
  int line_number = 1;
  for (index i = 0; i < static_cast<index>(input.size()); ++i) {
    char curr = input[i];
    char next = 0;
    if (i + 1 < static_cast<index>(input.size())) {
      next = input[i + 1];
    }
    if (curr == '\n') {
      ++line_number;
    }
    switch (state) {
      case State::kIdle:
        result.emplace_back(line_number);
        switch (curr) {
          case '+':
            result.back().type = Lexeme::Type::kPlus;
            break;
          case '-':
            if (next == '-') {
              result.pop_back();
              ++i;
              state = State::kCommentStart;
            } else {
              result.back().type = Lexeme::Type::kDash;
            }
            break;
          case '*':
            result.back().type = Lexeme::Type::kAsterisk;
            break;
          case '/':
            if (next == '/') {
              result.back().type = Lexeme::Type::kFloorDivision;
              ++i;
            } else {
              result.back().type = Lexeme::Type::kSlash;
            }
            break;
          case '%':
            result.back().type = Lexeme::Type::kPercent;
            break;
          case '^':
            result.back().type = Lexeme::Type::kCaret;
            break;
          case '#':
            result.back().type = Lexeme::Type::kHash;
            break;
          case '~':
            if (next == '=') {
              result.back().type = Lexeme::Type::kNotEqual;
              ++i;
            } else {
              result.back().type = Lexeme::Type::kTilde;
            }
            break;
          case '<':
            switch (next) {
              case '<':
                result.back().type = Lexeme::Type::kLeftShift;
                ++i;
                break;
              case '=':
                result.back().type = Lexeme::Type::kLessThanOrEqual;
                ++i;
                break;
              default:
                result.back().type = Lexeme::Type::kLessThan;
                break;
            }
            break;
          case '>':
            switch (next) {
              case '>':
                result.back().type = Lexeme::Type::kRightShift;
                ++i;
                break;
              case '=':
                result.back().type = Lexeme::Type::kGreaterThanOrEqual;
                ++i;
                break;
              default:
                result.back().type = Lexeme::Type::kGreaterThan;
                break;
            }
            break;
          case '=':
            if (next == '=') {
              result.back().type = Lexeme::Type::kEqual;
              ++i;
            } else {
              result.back().type = Lexeme::Type::kAssignment;
            }
            break;
          case '(':
            result.back().type = Lexeme::Type::kLeftBracket;
            break;
          case ')':
            result.back().type = Lexeme::Type::kRightBracket;
            break;
          case '{':
            result.back().type = Lexeme::Type::kLeftBrace;
            break;
          case '}':
            result.back().type = Lexeme::Type::kRightBrace;
            break;
          case '[':
            result.back().type = Lexeme::Type::kLeftSquareBracket;
            break;
          case ']':
            result.back().type = Lexeme::Type::kRightSquareBracket;
            break;
          case '.':
            if (next == '.') {
              result.back().type = Lexeme::Type::kConcat;
              ++i;
            } else {
              result.back().type = Lexeme::Type::kDot;
            }
            break;
          case ';':
            result.back().type = Lexeme::Type::kSemicolon;
            break;
          case ',':
            result.back().type = Lexeme::Type::kComma;
            break;
          case ':':
            result.back().type = Lexeme::Type::kColon;
            break;
          case '&':
            result.back().type = Lexeme::Type::kAmpersand;
            break;
          case '|':
            result.back().type = Lexeme::Type::kVerticalBar;
            break;
          default:
            result.pop_back();
            break;
        }
        if (std::isdigit(curr)) {
          state = State::kIntegerLiteral;
          --i;
        } else if (std::isalpha(curr) || curr == '_') {
          state = State::kSymbol;
          --i;
        } else if (curr == '\'' || curr == '"') {
          state = State::kStringLiteral;
          quote = curr;
        }
        break;
      case State::kIntegerLiteral:
        buffer.push_back(curr);
        if (next == '.') {
          state = State::kFloatLiteral;
        } else if (!std::isdigit(next)) {
          result.emplace_back(line_number);
          result.back().type = Lexeme::Type::kLiteralInteger;
          result.back().data.integer_value = std::stoll(buffer);
          state = State::kIdle;
          buffer.clear();
        }
        break;
      case State::kFloatLiteral:
        buffer.push_back(curr);
        if (!std::isdigit(next)) {
          result.emplace_back(line_number);
          result.back().type = Lexeme::Type::kLiteralFloat;
          result.back().data.float_value = std::stod(buffer);
          state = State::kIdle;
          buffer.clear();
        }
        break;
      case State::kSymbol:
        buffer.push_back(curr);
        if (!std::isalnum(next) && next != '_') {
          result.emplace_back(line_number);
          auto iter = keyword_map_.find(buffer);
          if (iter == keyword_map_.end()) {
            result.back().type = Lexeme::Type::kSymbol;
            auto table_iter = symbol_table.find(buffer);
            if (table_iter == symbol_table.end()) {
              symbol_table[buffer] = symbols.size();
              result.back().data.symbol_name = symbols.size();
              symbols.emplace_back(std::move(buffer));
              buffer = std::string{};
            } else {
              result.back().data.symbol_name = table_iter->second;
            }
          } else {
            result.back().type = iter->second;
          }
          buffer.clear();
          state = State::kIdle;
        }
        break;
      case State::kStringLiteral:
        if (curr == '\\') {
          buffer.push_back(next);
          ++i;
        } else if (curr == quote) {
          result.emplace_back(line_number);
          result.back().type = Lexeme::Type::kLiteralString;
          auto iter = string_table.find(buffer);
          if (iter == string_table.end()) {
            string_table[buffer] = string_literals.size();
            result.back().data.string_value = string_literals.size();
            string_literals.emplace_back(std::move(buffer));
            buffer = std::string{};
          } else {
            result.back().data.string_value = iter->second;
          }
          buffer.clear();
          state = State::kIdle;
        } else {
          buffer.push_back(curr);
        }
        break;
      case State::kCommentStart:
        if (curr == '[' && next == '[') {
          ++i;
          state = State::kCommentBlock;
        } else if (curr == '\n') {
          state = State::kIdle;
        } else {
          state = State::kCommentLine;
        }
        break;
      case State::kCommentLine:
        if (curr == '\n') {
          state = State::kIdle;
        }
        break;
      case State::kCommentBlock:
        if (curr == ']' && next == ']') {
          ++i;
          state = State::kIdle;
        }
        break;
    }
  }
  return result;
}
}  // namespace slua
