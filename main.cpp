#include <cstddef>

#include <fstream>
#include <iostream>
#include <string>

#include "emitter.hpp"
#include "lexer.hpp"
#include "parser.hpp"

using std::cin;
using std::cout;
using std::ifstream;
using std::string;
using std::vector;

using slua::Emitter;
using slua::Lexeme;
using slua::Lexer;
using slua::Parser;
using slua::ParserException;

int main() {
  ifstream stream{"in.lua"};
  cin.rdbuf(stream.rdbuf());

  string input;
  char buffer[0x1000];
  while (true) {
    cin.read(buffer, sizeof(buffer));
    if (cin.gcount() == 0) {
      break;
    }
    input.append(buffer, cin.gcount());
  }

  Lexer lexer{};
  vector<string> symbols;
  vector<string> string_literals;
  vector<Lexeme> lexemes = lexer.Lex(input, symbols, string_literals);

  try {
    Parser parser{lexemes};
    Emitter emitter{parser, symbols, string_literals};
    emitter.EmitObjectFile("output.o");
  } catch (const ParserException& e) {
    cout << e.what() << '\n';
    return EXIT_FAILURE;
  }
  return 0;
}
