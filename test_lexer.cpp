#include <iostream>
#include <string>
#include <vector>

#include <gsl/gsl>

#include "lexer.hpp"

namespace {
using slua::Lexeme;
using slua::Lexer;
}  // namespace

int main() {
  using gsl::index;

  std::string input;
  char buffer[0x1000];
  while (true) {
    std::cin.read(buffer, sizeof(buffer));
    if (std::cin.gcount() == 0) {
      break;
    }
    input.append(buffer, std::cin.gcount());
  }

  std::vector<std::string> symbols;
  std::vector<std::string> string_literals;
  auto lexemes = Lexer{}.Lex(input, symbols, string_literals);

  for (const auto& lexeme : lexemes) {
    std::cout << lexeme.line_number << ' ' << static_cast<int>(lexeme.type)
              << ' ';
    switch (lexeme.type) {
      case Lexeme::Type::kLiteralInteger:
        std::cout << lexeme.data.integer_value;
        break;
      case Lexeme::Type::kLiteralFloat:
        std::cout << lexeme.data.float_value;
        break;
      case Lexeme::Type::kLiteralString:
        std::cout << lexeme.data.string_value;
        break;
      case Lexeme::Type::kSymbol:
        std::cout << lexeme.data.symbol_name;
        break;
      default:
        break;
    }
    std::cout << '\n';
  }
  for (index i = 0; i < static_cast<index>(symbols.size()); ++i) {
    std::cout << i << ' ' << symbols[i] << '\n';
  }
  for (index i = 0; i < static_cast<index>(string_literals.size()); ++i) {
    std::cout << i << ' ' << string_literals[i] << '\n';
  }
  return 0;
}
