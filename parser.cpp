#include "parser.hpp"

#include <cassert>

#include <array>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

#include "util.hpp"

namespace slua {
namespace {
using std::array;
using std::decay_t;
using std::get;
using std::is_same_v;
using std::move;
using std::optional;
using std::reference_wrapper;
using std::runtime_error;
using std::vector;
using std::visit;

using node::Assignment;
using node::Binop;
using node::Constructor;
using node::ExpList;
using node::ExpType;
using node::Expr;
using node::ExprStat;
using node::Field;
using node::FieldSel;
using node::ForStat;
using node::FuncBody;
using node::FuncCall;
using node::FuncExpr;
using node::FuncName;
using node::FuncStat;
using node::IfStat;
using node::Index;
using node::LiteralBool;
using node::LiteralFloat;
using node::LiteralInt;
using node::LiteralString;
using node::LocalFunc;
using node::LocalStat;
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
}  // namespace

namespace node {
ExpType SuffixedExp::type() const noexcept {
  return visit(
      [](const auto& ref) {
        using T = decay_t<decltype(ref.get())>;
        if constexpr (is_same_v<T, PrimaryExp>) {
          return ref.get().type();
        } else if constexpr (is_same_v<T, FuncCall>) {
          return ExpType::kRight;
        } else if constexpr (is_same_v<T, Index> || is_same_v<T, FieldSel>) {
          return ExpType::kLeft;
        } else {
          static_assert(FalseType<T>::value);
        }
      },
      expr);
}

ExpType PrimaryExp::type() const noexcept {
  return visit(
      [](const auto& ref) {
        using T = decay_t<decltype(ref)>;
        if constexpr (is_same_v<T, Symbol>) {
          return ExpType::kLeft;
        } else {
          using U = decay_t<decltype(ref.get())>;
          if constexpr (is_same_v<U, Expr>) {
            return ExpType::kRight;
          } else {
            static_assert(FalseType<U>::value);
          }
        }
      },
      expr);
}
}  // namespace node

namespace {
struct BinPriority {
  int left;
  int right;
};

using Priority = array<BinPriority, Binop::kSize>;

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

Parser::Parser(const vector<Lexeme>& lexemes)
    : lexemes_(lexemes), root_(ParseStatList()) {}

SimpleExpr& Parser::ParseSimpleExpr() {
  // SimpleExpr -> nil | int | float | string | SuffixedExp
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
    case Lexeme::Type::kLiteralString:
      nodes_.emplace_back(
          SimpleExpr{LiteralString{current().data.string_value}});
      Next();
      break;
    case Lexeme::Type::kKeywordFalse:
      nodes_.emplace_back(SimpleExpr{LiteralBool{false}});
      Next();
      break;
    case Lexeme::Type::kKeywordTrue:
      nodes_.emplace_back(SimpleExpr{LiteralBool{true}});
      Next();
      break;
    case Lexeme::Type::kLeftBrace:
      nodes_.emplace_back(SimpleExpr{ParseConstructor()});
      break;
    case Lexeme::Type::kKeywordFunction:
      nodes_.emplace_back(SimpleExpr{ParseFuncExpr()});
      break;
    default:
      nodes_.emplace_back(SimpleExpr{ParseSuffixedExp()});
      break;
  }
  return get<SimpleExpr>(nodes_.back());
}

Expr& Parser::ParseSubExpr(int priority) {
  // Expr -> unop Expr | SimpleExpr | Expr binop Expr
  Expr* lhs;
  if (is_unop()) {
    Unop::Type op = unop_type();
    Next();
    Expr& expr = ParseSubExpr(kPriorityUnary);
    nodes_.emplace_back(Unop{op, expr});
    nodes_.emplace_back(Expr{get<Unop>(nodes_.back())});
    lhs = &get<Expr>(nodes_.back());
  } else {
    nodes_.emplace_back(Expr{ParseSimpleExpr()});
    lhs = &get<Expr>(nodes_.back());
  }
  while (is_binop()) {
    Binop::Type op = binop_type();
    if (kPriority[op].left <= priority) {
      break;
    }
    Next();
    Expr& rhs = ParseSubExpr(kPriority[op].right);
    nodes_.emplace_back(Binop{op, *lhs, rhs});
    nodes_.emplace_back(Expr{get<Binop>(nodes_.back())});
    lhs = &get<Expr>(nodes_.back());
  }
  return get<Expr>(nodes_.back());
}

Expr& Parser::ParseExpr() { return ParseSubExpr(0); }

StatList& Parser::ParseStatList() {
  // StatList -> { Statement }
  nodes_.emplace_back(StatList{});
  StatList& stat_list = get<StatList>(nodes_.back());
  while (!is_block_follow()) {
    stat_list.stats.emplace_back(ParseStatement());
  }
  return stat_list;
}

Statement& Parser::ParseStatement() {
  // Statement -> ';' | IfStat | ForStat | FuncStat | RetStat | LocalStat |
  //              LocalFunc | break | ExprStat
  nodes_.emplace_back(Statement{});
  Statement& statement = get<Statement>(nodes_.back());
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
    case Lexeme::Type::kKeywordLocal:
      if (lookahead().type == Lexeme::Type::kKeywordFunction) {
        statement.stat = ParseLocalFunc();
      } else {
        statement.stat = ParseLocalStat();
      }
      break;
    case Lexeme::Type::kKeywordBreak:
      statement.stat = Statement::BreakStat{};
      Next();
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
  IfStat& if_stat = get<IfStat>(nodes_.back());
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
  return get<TestThenBlock>(nodes_.back());
}

SuffixedExp& Parser::ParseSuffixedExp() {
  // SuffixedExp -> FieldSel | Index | PrimaryExp | FuncCall
  nodes_.emplace_back(SuffixedExp{ParsePrimaryExp()});
  while (true) {
    SuffixedExp& expr = get<SuffixedExp>(nodes_.back());
    switch (current().type) {
      case Lexeme::Type::kDot:
        nodes_.emplace_back(SuffixedExp{ParseFieldSel(expr)});
        break;
      case Lexeme::Type::kLeftSquareBracket:
        nodes_.emplace_back(SuffixedExp{ParseIndex(expr)});
        break;
      case Lexeme::Type::kLeftBracket:
        nodes_.emplace_back(SuffixedExp{ParseFuncCall(expr)});
        break;
      default:
        return expr;
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
  return get<PrimaryExp>(nodes_.back());
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
  return get<ExprStat>(nodes_.back());
}

Assignment& Parser::ParseAssignment(SuffixedExp& lhs) {
  // Assignment -> SuffixedExp '=' Expr
  Match(Lexeme::Type::kAssignment);
  Expr& rhs = ParseExpr();
  nodes_.emplace_back(Assignment{lhs, rhs});
  return get<Assignment>(nodes_.back());
}

FuncCall& Parser::ParseFuncCall(SuffixedExp& func) {
  // FuncCall -> SuffixedExp '(' [ ExpList ] ')'
  Match(Lexeme::Type::kLeftBracket);
  optional<reference_wrapper<const ExpList>> args;
  if (current().type != Lexeme::Type::kRightBracket) {
    args = ParseExpList();
  }
  Match(Lexeme::Type::kRightBracket);
  nodes_.emplace_back(FuncCall{func, args});
  return get<FuncCall>(nodes_.back());
}

ExpList& Parser::ParseExpList() {
  // ExpList -> Expr { ',' Expr }
  nodes_.emplace_back(ExpList{});
  ExpList& exp_list = get<ExpList>(nodes_.back());
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
    nodes_.emplace_back(Expr{get<SimpleExpr>(nodes_.back())});
    step = &get<Expr>(nodes_.back());
  }
  Match(Lexeme::Type::kKeywordDo);
  StatList& body = ParseStatList();
  Match(Lexeme::Type::kKeywordEnd);
  nodes_.emplace_back(ForStat{symbol, init, limit, *step, body});
  return get<ForStat>(nodes_.back());
}

Constructor& Parser::ParseConstructor() {
  // Constructor -> '{' [ Field { Sep Field } [Sep] ] '}'
  // Sep -> ',' | ';'
  Match(Lexeme::Type::kLeftBrace);
  nodes_.emplace_back(Constructor{});
  Constructor& constructor = get<Constructor>(nodes_.back());
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
        throw runtime_error{"Not implemented"};
      }
      break;
    default:
      nodes_.emplace_back(Field{ParseExpr()});
      break;
  }
  return get<Field>(nodes_.back());
}

Index& Parser::ParseIndex(SuffixedExp& lhs) {
  // Index -> SuffixedExp '[' Expr ']'
  Match(Lexeme::Type::kLeftSquareBracket);
  nodes_.emplace_back(Index{lhs, ParseExpr()});
  Match(Lexeme::Type::kRightSquareBracket);
  return get<Index>(nodes_.back());
}

FuncStat& Parser::ParseFuncStat() {
  // FuncStat -> function FuncName FuncBody
  Match(Lexeme::Type::kKeywordFunction);
  FuncName& name = ParseFuncName();
  nodes_.emplace_back(FuncStat{name, ParseFuncBody()});
  return get<FuncStat>(nodes_.back());
}

FuncName& Parser::ParseFuncName() {
  // FuncName -> symbol
  if (current().type == Lexeme::Type::kSymbol) {
    nodes_.emplace_back(FuncName{Symbol{current().data.symbol_name}});
    Next();
    return get<FuncName>(nodes_.back());
  }
  SyntaxError();
}

RetStat& Parser::ParseRetStat() {
  // RetStat -> return [ expr ] [ ';' ]
  Match(Lexeme::Type::kKeywordReturn);
  nodes_.emplace_back(RetStat{});
  RetStat& ret = get<RetStat>(nodes_.back());
  if (is_block_follow() || current().type == Lexeme::Type::kSemicolon) {
    if (current().type == Lexeme::Type::kSemicolon) {
      Next();
    }
    return ret;
  }
  ret.expr = ParseExpr();
  return ret;
}

LocalStat& Parser::ParseLocalStat() {
  // LocalStat -> local symbol '=' Expr
  Match(Lexeme::Type::kKeywordLocal);
  if (current().type != Lexeme::Type::kSymbol) {
    SyntaxError();
  }
  Symbol name{current().data.symbol_name};
  Next();
  Match(Lexeme::Type::kAssignment);
  Expr& expr = ParseExpr();
  nodes_.emplace_back(LocalStat{name, expr});
  return get<LocalStat>(nodes_.back());
}

LocalFunc& Parser::ParseLocalFunc() {
  // LocalFunc -> local function symbol FuncBody
  Match(Lexeme::Type::kKeywordLocal);
  Match(Lexeme::Type::kKeywordFunction);
  if (current().type != Lexeme::Type::kSymbol) {
    SyntaxError();
  }
  Symbol name{current().data.symbol_name};
  Next();
  FuncBody& body = ParseFuncBody();
  nodes_.emplace_back(LocalFunc{name, body});
  return get<LocalFunc>(nodes_.back());
}

FuncBody& Parser::ParseFuncBody() {
  // FuncBody -> '(' [ symbol { ',' symbol } ] ')' StatList end
  Match(Lexeme::Type::kLeftBracket);
  vector<Symbol> params;
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
  nodes_.emplace_back(FuncBody{move(params), body});
  return get<FuncBody>(nodes_.back());
}

FuncExpr& Parser::ParseFuncExpr() {
  // FuncExpr -> function FuncBody
  Match(Lexeme::Type::kKeywordFunction);
  nodes_.emplace_back(FuncExpr{ParseFuncBody()});
  return get<FuncExpr>(nodes_.back());
}

FieldSel& Parser::ParseFieldSel(SuffixedExp& lhs) {
  // FieldSel -> SuffixedExp '.' symbol
  Match(Lexeme::Type::kDot);
  if (current().type != Lexeme::Type::kSymbol) {
    SyntaxError();
  }
  nodes_.emplace_back(FieldSel{lhs, Symbol{current().data.symbol_name}});
  Next();
  return get<FieldSel>(nodes_.back());
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
