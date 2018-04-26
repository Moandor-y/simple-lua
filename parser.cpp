#include "parser.hpp"

#include <cassert>

#include <array>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

#include "util.hpp"

namespace slua {
namespace node {
ExpType SuffixedExp::type() const noexcept {
  return std::visit(
      [](const auto& ref) {
        using T = std::decay_t<decltype(ref.get())>;
        if constexpr (std::is_same_v<T, PrimaryExp>) {
          return ref.get().type();
        } else if constexpr (std::is_same_v<T, FuncCall>) {
          return ExpType::kRight;
        } else if constexpr (std::is_same_v<T, Index>) {
          return ExpType::kLeft;
        } else {
          static_assert(FalseType<T>::value);
        }
      },
      expr);
}

ExpType PrimaryExp::type() const noexcept {
  return std::visit(
      [](const auto& ref) {
        using T = std::decay_t<decltype(ref)>;
        if constexpr (std::is_same_v<T, Symbol>) {
          return ExpType::kLeft;
        } else {
          using U = std::decay_t<decltype(ref.get())>;
          if constexpr (std::is_same_v<U, Expr>) {
            return ExpType::kRight;
          } else {
            static_assert(FalseType<U>::value);
          }
        }
      },
      expr);
}
}  // namespace node

using node::Assignment;
using node::Binop;
using node::Constructor;
using node::ExpList;
using node::ExpType;
using node::Expr;
using node::ExprStat;
using node::Field;
using node::ForStat;
using node::FuncCall;
using node::FuncName;
using node::FuncStat;
using node::IfStat;
using node::Index;
using node::LiteralFloat;
using node::LiteralInt;
using node::Nil;
using node::PrimaryExp;
using node::RetStat;
using node::SimpleExpr;
using node::StatList;
using node::Statement;
using node::SuffixedExp;
using node::Symbol;
using node::TestThenBlock;
using node::Unop;

namespace {
struct BinPriority {
  int left;
  int right;
};

using Priority = std::array<BinPriority, Binop::kSize>;

constexpr Priority BuildPriorityMap() {
  Priority priority{};
  priority[Binop::kAdd] = {10, 10};
  priority[Binop::kSub] = {10, 10};
  priority[Binop::kMul] = {11, 11};
  priority[Binop::kMod] = {11, 11};
  priority[Binop::kPow] = {14, 13};
  priority[Binop::kDiv] = {11, 11};
  priority[Binop::kIntDiv] = {11, 11};
  priority[Binop::kBitAnd] = {6, 6};
  priority[Binop::kBitOr] = {4, 4};
  priority[Binop::kBitXor] = {5, 5};
  priority[Binop::kShl] = {7, 7};
  priority[Binop::kShr] = {7, 7};
  priority[Binop::kConcat] = {9, 8};
  priority[Binop::kNotEq] = {3, 3};
  priority[Binop::kEq] = {3, 3};
  priority[Binop::kLess] = {3, 3};
  priority[Binop::kLessEq] = {3, 3};
  priority[Binop::kGreater] = {3, 3};
  priority[Binop::kGreaterEq] = {3, 3};
  priority[Binop::kAnd] = {2, 2};
  priority[Binop::kOr] = {1, 1};
  return priority;
}

constexpr Priority kPriority = BuildPriorityMap();
constexpr int kPriorityUnary = 12;
}  // namespace

Parser::Parser(const std::vector<Lexeme>& lexemes)
    : lexemes_(lexemes), root_(ParseStatList()) {}

SimpleExpr& Parser::ParseSimpleExpr() {
  // SimpleExpr -> nil | int | float | SuffixedExp
  switch (current().type) {
    case Lexeme::Type::kKeywordNil:
      Next();
      nodes_.emplace_back(SimpleExpr{Nil{}});
      break;
    case Lexeme::Type::kLiteralInteger:
      nodes_.emplace_back(SimpleExpr{LiteralInt{current().data.integer_value}});
      Next();
      break;
    case Lexeme::Type::kLiteralFloat:
      nodes_.emplace_back(SimpleExpr{LiteralFloat{current().data.float_value}});
      Next();
      break;
    case Lexeme::Type::kLeftBrace:
      nodes_.emplace_back(SimpleExpr{ParseConstructor()});
      break;
    default:
      nodes_.emplace_back(SimpleExpr{ParseSuffixedExp()});
      break;
  }
  return std::get<SimpleExpr>(nodes_.back());
}

Expr& Parser::ParseSubExpr(int priority) {
  // Expr -> unop Expr | SimpleExpr | Expr binop Expr
  Expr* lhs;
  if (is_unop()) {
    Unop::Type op = unop_type();
    Next();
    Expr& expr = ParseSubExpr(kPriorityUnary);
    nodes_.emplace_back(Unop{op, expr});
    nodes_.emplace_back(Expr{std::get<Unop>(nodes_.back())});
    lhs = &std::get<Expr>(nodes_.back());
  } else {
    nodes_.emplace_back(Expr{ParseSimpleExpr()});
    lhs = &std::get<Expr>(nodes_.back());
  }
  while (is_binop()) {
    Binop::Type op = binop_type();
    if (kPriority[op].left <= priority) {
      break;
    }
    Next();
    Expr& rhs = ParseSubExpr(kPriority[op].right);
    nodes_.emplace_back(Binop{op, *lhs, rhs});
    nodes_.emplace_back(Expr{std::get<Binop>(nodes_.back())});
    lhs = &std::get<Expr>(nodes_.back());
  }
  return std::get<Expr>(nodes_.back());
}

Expr& Parser::ParseExpr() { return ParseSubExpr(0); }

StatList& Parser::ParseStatList() {
  // StatList -> { Statement }
  nodes_.emplace_back(StatList{});
  StatList& stat_list = std::get<StatList>(nodes_.back());
  while (!is_block_follow()) {
    stat_list.stats.emplace_back(ParseStatement());
  }
  return stat_list;
}

Statement& Parser::ParseStatement() {
  // Statement -> ';' | IfStat | ExprStat | FuncStat
  nodes_.emplace_back(Statement{});
  Statement& statement = std::get<Statement>(nodes_.back());
  switch (current().type) {
    case Lexeme::Type::kSemicolon:
      Next();
      break;
    case Lexeme::Type::kKeywordIf:
      statement.stat = ParseIfStat();
      break;
    case Lexeme::Type::kKeywordFor:
      statement.stat = ParseForStat();
      break;
    case Lexeme::Type::kKeywordFunction:
      statement.stat = ParseFuncStat();
      break;
    case Lexeme::Type::kKeywordReturn:
      statement.stat = ParseRetStat();
      break;
    default:
      statement.stat = ParseExprStat();
      break;
  }
  return statement;
}

IfStat& Parser::ParseIfStat() {
  // IfStat -> if TestThenBlock { elseif TestThenBlock } [ else Block ] end
  Match(Lexeme::Type::kKeywordIf);
  nodes_.emplace_back(IfStat{});
  IfStat& if_stat = std::get<IfStat>(nodes_.back());
  if_stat.cond_blocks.emplace_back(ParseTestThenBlock());
  while (current().type == Lexeme::Type::kKeywordElseif) {
    Next();
    if_stat.cond_blocks.emplace_back(ParseTestThenBlock());
  }
  if (current().type == Lexeme::Type::kKeywordElse) {
    Next();
    if_stat.else_block = ParseStatList();
  }
  Match(Lexeme::Type::kKeywordEnd);
  return if_stat;
}

TestThenBlock& Parser::ParseTestThenBlock() {
  // TestThenBlock -> Expr then StatList
  Expr& cond = ParseExpr();
  Match(Lexeme::Type::kKeywordThen);
  StatList& then = ParseStatList();
  nodes_.emplace_back(TestThenBlock{cond, then});
  return std::get<TestThenBlock>(nodes_.back());
}

SuffixedExp& Parser::ParseSuffixedExp() {
  // SuffixedExp -> PrimaryExp | FuncCall
  nodes_.emplace_back(SuffixedExp{ParsePrimaryExp()});
  while (true) {
    switch (current().type) {
      case Lexeme::Type::kLeftSquareBracket:
        nodes_.emplace_back(
            SuffixedExp{ParseIndex(std::get<SuffixedExp>(nodes_.back()))});
        break;
      case Lexeme::Type::kLeftBracket:
        nodes_.emplace_back(
            SuffixedExp{ParseFuncCall(std::get<SuffixedExp>(nodes_.back()))});
        break;
      default:
        return std::get<SuffixedExp>(nodes_.back());
    }
  }
}

PrimaryExp& Parser::ParsePrimaryExp() {
  // PrimaryExp -> '(' Expr ')' | symbol
  switch (current().type) {
    case Lexeme::Type::kLeftBracket:
      Next();
      nodes_.emplace_back(PrimaryExp{ParseExpr()});
      Match(Lexeme::Type::kRightBracket);
      break;
    case Lexeme::Type::kSymbol:
      nodes_.emplace_back(PrimaryExp{Symbol{current().data.symbol_name}});
      Next();
      break;
    default:
      SyntaxError();
  }
  return std::get<PrimaryExp>(nodes_.back());
}

ExprStat& Parser::ParseExprStat() {
  // ExprStat -> Assignment | SuffixedExp
  SuffixedExp& lhs = ParseSuffixedExp();
  if (current().type == Lexeme::Type::kAssignment) {
    if (lhs.type() != ExpType::kLeft) {
      SyntaxError();
    }
    nodes_.emplace_back(ExprStat{ParseAssignment(lhs)});
  } else {
    nodes_.emplace_back(ExprStat{lhs});
  }
  return std::get<ExprStat>(nodes_.back());
}

Assignment& Parser::ParseAssignment(SuffixedExp& lhs) {
  // Assignment -> SuffixedExp '=' Expr
  Match(Lexeme::Type::kAssignment);
  Expr& rhs = ParseExpr();
  nodes_.emplace_back(Assignment{lhs, rhs});
  return std::get<Assignment>(nodes_.back());
}

FuncCall& Parser::ParseFuncCall(SuffixedExp& func) {
  // FuncCall -> SuffixedExp '(' [ ExpList ] ')'
  Match(Lexeme::Type::kLeftBracket);
  std::optional<std::reference_wrapper<const ExpList>> args;
  if (current().type != Lexeme::Type::kRightBracket) {
    args = ParseExpList();
  }
  Match(Lexeme::Type::kRightBracket);
  nodes_.emplace_back(FuncCall{func, args});
  return std::get<FuncCall>(nodes_.back());
}

ExpList& Parser::ParseExpList() {
  // ExpList -> Expr { ',' Expr }
  nodes_.emplace_back(ExpList{});
  ExpList& exp_list = std::get<ExpList>(nodes_.back());
  exp_list.exps.emplace_back(ParseExpr());
  while (current().type == Lexeme::Type::kComma) {
    Next();
    exp_list.exps.emplace_back(ParseExpr());
  }
  return exp_list;
}

ForStat& Parser::ParseForStat() {
  // ForStat -> for symbol '=' Expr ',' Expr [ ',' Expr ] do StatList end
  Match(Lexeme::Type::kKeywordFor);
  if (current().type != Lexeme::Type::kSymbol) {
    SyntaxError();
  }
  Symbol symbol{current().data.symbol_name};
  Next();
  Match(Lexeme::Type::kAssignment);
  Expr& init = ParseExpr();
  Match(Lexeme::Type::kComma);
  Expr& limit = ParseExpr();
  Expr* step;
  if (current().type == Lexeme::Type::kComma) {
    Next();
    step = &ParseExpr();
  } else {
    nodes_.emplace_back(SimpleExpr{LiteralInt{1}});
    nodes_.emplace_back(Expr{std::get<SimpleExpr>(nodes_.back())});
    step = &std::get<Expr>(nodes_.back());
  }
  Match(Lexeme::Type::kKeywordDo);
  StatList& body = ParseStatList();
  Match(Lexeme::Type::kKeywordEnd);
  nodes_.emplace_back(ForStat{symbol, init, limit, *step, body});
  return std::get<ForStat>(nodes_.back());
}

Constructor& Parser::ParseConstructor() {
  // Constructor -> '{' [ Field { Sep Field } [Sep] ] '}'
  // Sep -> ',' | ';'
  Match(Lexeme::Type::kLeftBrace);
  nodes_.emplace_back(Constructor{});
  Constructor& constructor = std::get<Constructor>(nodes_.back());
  while (current().type != Lexeme::Type::kRightBrace) {
    constructor.fields.emplace_back(ParseField());
    if (current().type != Lexeme::Type::kComma &&
        current().type != Lexeme::Type::kSemicolon) {
      break;
    }
    Next();
  }
  Match(Lexeme::Type::kRightBrace);
  return constructor;
}

Field& Parser::ParseField() {
  // Field -> Expr
  switch (current().type) {
    case Lexeme::Type::kSymbol:
      if (lookahead().type != Lexeme::Type::kAssignment) {
        nodes_.emplace_back(Field{ParseExpr()});
      } else {
        throw std::runtime_error{"Not implemented"};
      }
      break;
    default:
      nodes_.emplace_back(Field{ParseExpr()});
      break;
  }
  return std::get<Field>(nodes_.back());
}

Index& Parser::ParseIndex(SuffixedExp& lhs) {
  // Index -> '[' Expr ']'
  Match(Lexeme::Type::kLeftSquareBracket);
  nodes_.emplace_back(Index{lhs, ParseExpr()});
  Match(Lexeme::Type::kRightSquareBracket);
  return std::get<Index>(nodes_.back());
}

FuncStat& Parser::ParseFuncStat() {
  // FuncStat -> function FuncName '(' [ symbol { ',' symbol } ] ')' StatList
  //             end
  Match(Lexeme::Type::kKeywordFunction);
  FuncName& name = ParseFuncName();
  Match(Lexeme::Type::kLeftBracket);
  std::vector<Symbol> params;
  while (current().type != Lexeme::Type::kRightBracket) {
    if (current().type == Lexeme::Type::kSymbol) {
      params.emplace_back(Symbol{current().data.symbol_name});
    } else {
      SyntaxError();
    }
    Next();
    if (current().type == Lexeme::Type::kComma) {
      Next();
    }
  }
  Match(Lexeme::Type::kRightBracket);
  StatList& body = ParseStatList();
  Match(Lexeme::Type::kKeywordEnd);
  nodes_.emplace_back(FuncStat{name, std::move(params), body});
  return std::get<FuncStat>(nodes_.back());
}

FuncName& Parser::ParseFuncName() {
  // FuncName -> symbol
  if (current().type == Lexeme::Type::kSymbol) {
    nodes_.emplace_back(FuncName{Symbol{current().data.symbol_name}});
    Next();
    return std::get<FuncName>(nodes_.back());
  }
  SyntaxError();
}

RetStat& Parser::ParseRetStat() {
  // RetStat -> return [ expr ] [ ';' ]
  Match(Lexeme::Type::kKeywordReturn);
  nodes_.emplace_back(RetStat{});
  RetStat& ret = std::get<RetStat>(nodes_.back());
  if (is_block_follow() || current().type == Lexeme::Type::kSemicolon) {
    if (current().type == Lexeme::Type::kSemicolon) {
      Next();
    }
    return ret;
  }
  ret.expr = ParseExpr();
  return ret;
}

bool Parser::is_block_follow() const noexcept {
  switch (current().type) {
    case Lexeme::Type::kKeywordElse:
    case Lexeme::Type::kKeywordElseif:
    case Lexeme::Type::kKeywordEnd:
    case Lexeme::Type::kNull:
    case Lexeme::Type::kKeywordUntil:
      return true;
    default:
      return false;
  }
}

bool Parser::is_unop() const noexcept {
  switch (current().type) {
    case Lexeme::Type::kDash:
    case Lexeme::Type::kTilde:
    case Lexeme::Type::kHash:
    case Lexeme::Type::kKeywordNot:
      return true;
    default:
      return false;
  }
}

Unop::Type Parser::unop_type() const noexcept {
  switch (current().type) {
    case Lexeme::Type::kDash:
      return Unop::Type::kMinus;
    case Lexeme::Type::kTilde:
      return Unop::Type::kBitNot;
    case Lexeme::Type::kHash:
      return Unop::Type::kLen;
    case Lexeme::Type::kKeywordNot:
      return Unop::Type::kNot;
    default:
      assert(false);
  }
}

bool Parser::is_binop() const noexcept {
  switch (current().type) {
    case Lexeme::Type::kPlus:
    case Lexeme::Type::kDash:
    case Lexeme::Type::kAsterisk:
    case Lexeme::Type::kPercent:
    case Lexeme::Type::kCaret:
    case Lexeme::Type::kSlash:
    case Lexeme::Type::kFloorDivision:
    case Lexeme::Type::kTilde:
    case Lexeme::Type::kLeftShift:
    case Lexeme::Type::kRightShift:
    case Lexeme::Type::kConcat:
    case Lexeme::Type::kNotEqual:
    case Lexeme::Type::kEqual:
    case Lexeme::Type::kLessThan:
    case Lexeme::Type::kLessThanOrEqual:
    case Lexeme::Type::kGreaterThan:
    case Lexeme::Type::kGreaterThanOrEqual:
    case Lexeme::Type::kKeywordAnd:
    case Lexeme::Type::kKeywordOr:
    case Lexeme::Type::kAmpersand:
    case Lexeme::Type::kVerticalBar:
      return true;
    default:
      return false;
  }
}

Binop::Type Parser::binop_type() const noexcept {
  switch (current().type) {
    case Lexeme::Type::kPlus:
      return Binop::kAdd;
    case Lexeme::Type::kDash:
      return Binop::kSub;
    case Lexeme::Type::kAsterisk:
      return Binop::kMul;
    case Lexeme::Type::kPercent:
      return Binop::kMod;
    case Lexeme::Type::kCaret:
      return Binop::kPow;
    case Lexeme::Type::kSlash:
      return Binop::kDiv;
    case Lexeme::Type::kFloorDivision:
      return Binop::kIntDiv;
    case Lexeme::Type::kAmpersand:
      return Binop::kBitAnd;
    case Lexeme::Type::kVerticalBar:
      return Binop::kBitOr;
    case Lexeme::Type::kTilde:
      return Binop::kBitXor;
    case Lexeme::Type::kLeftShift:
      return Binop::kShl;
    case Lexeme::Type::kRightShift:
      return Binop::kShr;
    case Lexeme::Type::kConcat:
      return Binop::kConcat;
    case Lexeme::Type::kNotEqual:
      return Binop::kNotEq;
    case Lexeme::Type::kEqual:
      return Binop::kEq;
    case Lexeme::Type::kLessThan:
      return Binop::kLess;
    case Lexeme::Type::kLessThanOrEqual:
      return Binop::kLessEq;
    case Lexeme::Type::kGreaterThan:
      return Binop::kGreater;
    case Lexeme::Type::kGreaterThanOrEqual:
      return Binop::kGreaterEq;
    case Lexeme::Type::kKeywordAnd:
      return Binop::kAnd;
    case Lexeme::Type::kKeywordOr:
      return Binop::kOr;
    default:
      assert(false);
  }
}
}  // namespace slua