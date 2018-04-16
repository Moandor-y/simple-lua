#include <iostream>
#include <string>
#include <vector>

#include "lexer.hpp"
#include "parser.hpp"

namespace {
using slua::Lexeme;
using slua::Lexer;
using slua::Parser;
using slua::ParserException;
using slua::node::Assignment;
using slua::node::Binop;
using slua::node::ExpList;
using slua::node::Expr;
using slua::node::ExprStat;
using slua::node::FuncCall;
using slua::node::IfStat;
using slua::node::LiteralInt;
using slua::node::Nil;
using slua::node::PrimaryExp;
using slua::node::SimpleExpr;
using slua::node::StatList;
using slua::node::Statement;
using slua::node::SubExpr;
using slua::node::SuffixedExp;
using slua::node::Symbol;
using slua::node::TestThenBlock;
using slua::node::Unop;

std::vector<std::string> stack;
std::vector<std::string> symbols;
std::vector<std::string> string_literals;

void Print() {
  for (const auto& node : stack) {
    std::cout << node << ' ';
  }
  std::cout << '\n';
}

void Visit(const StatList&);
void Visit(const Statement&);
void Visit(const IfStat&);
void Visit(const TestThenBlock&);
void Visit(const Expr&);
void Visit(const SubExpr&);
void Visit(const SimpleExpr&);
void Visit(const Unop&);
void Visit(const Binop&);
void Visit(const SuffixedExp&);
void Visit(const PrimaryExp&);
void Visit(const ExprStat&);
void Visit(const Assignment&);
void Visit(const FuncCall&);
void Visit(const ExpList&);

struct Visitor {
  template <typename T>
  void operator()(const T& v) const {
    Visit(v.get());
  }

  void operator()(Statement::NullStat) const {}

  void operator()(Nil) const {
    stack.emplace_back("nil");
    Print();
    stack.pop_back();
  }

  void operator()(Symbol symbol) const {
    stack.emplace_back("sym");
    stack.back().push_back(symbols[symbol.name].front());
    Print();
    stack.pop_back();
  }

  void operator()(LiteralInt literal) const {
    stack.emplace_back("int");
    stack.back().append(std::to_string(literal.value));
    Print();
    stack.pop_back();
  }
};

void Visit(const StatList& stat_list) {
  stack.emplace_back("stat_list");
  Print();
  for (const auto& stat : stat_list.stats) {
    Visit(stat.get());
  }
  stack.pop_back();
}

void Visit(const Statement& statement) {
  stack.emplace_back("stat");
  Print();
  std::visit(Visitor{}, statement.stat);
  stack.pop_back();
}

void Visit(const IfStat& if_stat) {
  stack.emplace_back("if");
  Print();
  for (const auto& cond_block : if_stat.cond_blocks) {
    Visit(cond_block.get());
  }
  if (if_stat.else_block.has_value()) {
    Visit(if_stat.else_block.value());
  }
  stack.pop_back();
}

void Visit(const TestThenBlock& test_then_block) {
  stack.emplace_back("then");
  Print();
  Visit(test_then_block.cond);
  Visit(test_then_block.then);
  stack.pop_back();
}

void Visit(const Expr& expr) {
  stack.emplace_back("expr");
  Print();
  Visit(expr.expr);
  stack.pop_back();
}

void Visit(const SubExpr& subexpr) {
  stack.emplace_back("subexpr");
  Print();
  std::visit(Visitor{}, subexpr.expr);
  stack.pop_back();
}

void Visit(const SimpleExpr& expr) {
  stack.emplace_back("simpleexp");
  Print();
  std::visit(Visitor{}, expr.expr);
  stack.pop_back();
}

void Visit(const Unop& unop) {
  switch (unop.type) {
    case slua::node::Unop::kNot:
      stack.emplace_back("not");
      break;
    case slua::node::Unop::kMinus:
      stack.emplace_back("un-");
      break;
    case slua::node::Unop::kLen:
      stack.emplace_back("#");
      break;
    case slua::node::Unop::kBitNot:
      stack.emplace_back("un~");
      break;
  }
  Print();
  Visit(unop.expr);
  stack.pop_back();
}

void Visit(const Binop& binop) {
  switch (binop.type) {
    case slua::node::Binop::kAdd:
      stack.emplace_back("+");
      break;
    case slua::node::Binop::kSub:
      stack.emplace_back("-");
      break;
    case slua::node::Binop::kMul:
      stack.emplace_back("*");
      break;
    case slua::node::Binop::kMod:
      stack.emplace_back("%");
      break;
    case slua::node::Binop::kPow:
      stack.emplace_back("^");
      break;
    case slua::node::Binop::kDiv:
      stack.emplace_back("/");
      break;
    case slua::node::Binop::kIntDiv:
      stack.emplace_back("//");
      break;
    case slua::node::Binop::kBitAnd:
      stack.emplace_back("&");
      break;
    case slua::node::Binop::kBitOr:
      stack.emplace_back("|");
      break;
    case slua::node::Binop::kBitXor:
      stack.emplace_back("~");
      break;
    case slua::node::Binop::kShl:
      stack.emplace_back("<<");
      break;
    case slua::node::Binop::kShr:
      stack.emplace_back(">>");
      break;
    case slua::node::Binop::kConcat:
      stack.emplace_back("..");
      break;
    case slua::node::Binop::kNotEq:
      stack.emplace_back("~=");
      break;
    case slua::node::Binop::kEq:
      stack.emplace_back("==");
      break;
    case slua::node::Binop::kLess:
      stack.emplace_back("<");
      break;
    case slua::node::Binop::kLessEq:
      stack.emplace_back("<=");
      break;
    case slua::node::Binop::kGreater:
      stack.emplace_back(">");
      break;
    case slua::node::Binop::kGreaterEq:
      stack.emplace_back(">=");
      break;
    case slua::node::Binop::kAnd:
      stack.emplace_back("and");
      break;
    case slua::node::Binop::kOr:
      stack.emplace_back("or");
      break;

    case slua::node::Binop::kSize:
      throw std::runtime_error{"Unexpected binop type"};
  }
  Visit(binop.lhs);
  Visit(binop.rhs);
  stack.pop_back();
}

void Visit(const SuffixedExp& expr) {
  stack.emplace_back("suffixedexp");
  Print();
  std::visit(Visitor{}, expr.expr);
  stack.pop_back();
}

void Visit(const PrimaryExp& expr) {
  stack.emplace_back("primaryexp");
  Print();
  std::visit(Visitor{}, expr.expr);
  stack.pop_back();
}

void Visit(const ExprStat& stat) {
  stack.emplace_back("exprstat");
  Print();
  std::visit(Visitor{}, stat.expr);
  stack.pop_back();
}

void Visit(const Assignment& assignment) {
  stack.emplace_back("assign");
  Print();
  Visit(assignment.lhs);
  Visit(assignment.rhs);
  stack.pop_back();
}

void Visit(const FuncCall& func_call) {
  stack.emplace_back("call");
  Print();
  Visit(func_call.func);
  if (func_call.args.has_value()) {
    Visit(func_call.args.value());
  }
  stack.pop_back();
}

void Visit(const ExpList& exp_list) {
  stack.emplace_back("exps");
  Print();
  for (const auto& exp : exp_list.exps) {
    Visit(exp.get());
  }
  stack.pop_back();
}
}  // namespace

int main() {
  std::string input;
  char buffer[0x1000];
  while (true) {
    std::cin.read(buffer, sizeof(buffer));
    if (std::cin.gcount() == 0) {
      break;
    }
    input.append(buffer, std::cin.gcount());
  }
  auto lexemes = Lexer{}.Lex(input, symbols, string_literals);
  try {
    Parser parser{lexemes};
    Visit(parser.root());
  } catch (const ParserException& e) {
    std::cout << e.what() << '\n';
  }
  return 0;
}
