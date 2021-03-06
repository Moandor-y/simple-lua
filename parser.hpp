#ifndef SLUA_PARSER_HPP_
#define SLUA_PARSER_HPP_

#include <cstdint>

#include <functional>
#include <list>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <gsl/gsl>

#include "lexer.hpp"

namespace slua {
class ParserException : public std::runtime_error {
  using std::runtime_error::runtime_error;
};

namespace node {
enum class ExpType {
  kVoid,
  kLeft,
  kRight,
};

struct StatList;
struct Statement;
struct IfStat;
struct TestThenBlock;
struct Expr;
struct SimpleExpr;
struct Unop;
struct Binop;
struct SuffixedExp;
struct PrimaryExp;
struct ExprStat;
struct Assignment;
struct FuncCall;
struct ExpList;
struct ForStat;
struct Constructor;
struct Field;
struct Index;
struct FuncStat;
struct FuncName;
struct RetStat;
struct LocalStat;
struct LocalFunc;
struct FuncBody;
struct FuncExpr;
struct FieldSel;
struct MethodCall;
struct FuncNameFieldSel;
struct FuncNameMethodSel;
struct DoStat;

struct Nil {};

struct LiteralInt {
  std::int64_t value;
};

struct LiteralFloat {
  double value;
};

struct LiteralString {
  gsl::index value;
};

struct LiteralBool {
  bool value;
};

struct Symbol {
  gsl::index name;
};

struct StatList {
  std::vector<std::reference_wrapper<const Statement>> stats;
};

struct Statement {
  struct NullStat {};
  struct BreakStat {};

  std::variant<NullStat, BreakStat, std::reference_wrapper<const IfStat>,
               std::reference_wrapper<const ExprStat>,
               std::reference_wrapper<const ForStat>,
               std::reference_wrapper<const FuncStat>,
               std::reference_wrapper<const RetStat>,
               std::reference_wrapper<const LocalStat>,
               std::reference_wrapper<const LocalFunc>,
               std::reference_wrapper<const DoStat>>
      stat;
};

struct IfStat {
  std::vector<std::reference_wrapper<const TestThenBlock>> cond_blocks;
  std::optional<std::reference_wrapper<const StatList>> else_block;
};

struct TestThenBlock {
  const Expr& cond;
  const StatList& then;
};

struct Expr {
  std::variant<std::reference_wrapper<const SimpleExpr>,
               std::reference_wrapper<const Unop>,
               std::reference_wrapper<const Binop>>
      expr;
};

struct SimpleExpr {
  std::variant<Nil, LiteralInt, LiteralFloat, LiteralString, LiteralBool,
               std::reference_wrapper<const SuffixedExp>,
               std::reference_wrapper<const Constructor>,
               std::reference_wrapper<const FuncExpr>>
      expr;
};

struct Unop {
  enum Type {
    kNot,
    kMinus,
    kBitNot,
    kLen,
  };

  Type type;
  const Expr& expr;
};

struct Binop {
  enum Type {
    kAdd,
    kSub,
    kMul,
    kMod,
    kPow,
    kDiv,
    kIntDiv,
    kBitAnd,
    kBitOr,
    kBitXor,
    kShl,
    kShr,
    kConcat,
    kNotEq,
    kEq,
    kLess,
    kLessEq,
    kGreater,
    kGreaterEq,
    kAnd,
    kOr,

    kSize,
  };

  Type type;
  Expr& lhs;
  Expr& rhs;
};

struct SuffixedExp {
  std::variant<std::reference_wrapper<const PrimaryExp>,
               std::reference_wrapper<const FuncCall>,
               std::reference_wrapper<const Index>,
               std::reference_wrapper<const FieldSel>,
               std::reference_wrapper<const MethodCall>>
      expr;

  ExpType type() const noexcept;
};

struct PrimaryExp {
  std::variant<Symbol, std::reference_wrapper<const Expr>> expr;

  ExpType type() const noexcept;
};

struct ExprStat {
  std::variant<std::reference_wrapper<const SuffixedExp>,
               std::reference_wrapper<const Assignment>>
      expr;
};

struct Assignment {
  const SuffixedExp& lhs;
  const Expr& rhs;
};

struct FuncCall {
  SuffixedExp& func;
  std::optional<std::reference_wrapper<const ExpList>> args;
};

struct ExpList {
  std::vector<std::reference_wrapper<const Expr>> exps;
};

struct ForStat {
  Symbol symbol;
  const Expr& initial;
  const Expr& limit;
  const Expr& step;
  const StatList& body;
};

struct Constructor {
  std::vector<std::reference_wrapper<const Field>> fields;
};

struct Field {
  std::variant<std::reference_wrapper<const Expr>> field;
};

struct Index {
  const SuffixedExp& lhs;
  const Expr& rhs;
};

struct FuncStat {
  const FuncName& name;
  const FuncBody& body;
};

struct FuncName {
  std::variant<std::reference_wrapper<const FuncNameFieldSel>,
               std::reference_wrapper<const FuncNameMethodSel>, Symbol>
      name;
};

struct RetStat {
  std::optional<std::reference_wrapper<const Expr>> expr;
};

struct LocalStat {
  Symbol name;
  const Expr& expr;
};

struct LocalFunc {
  Symbol name;
  const FuncBody& body;
};

struct FuncBody {
  std::vector<Symbol> params;
  const StatList& body;
};

struct FuncExpr {
  const FuncBody& body;
};

struct FieldSel {
  const SuffixedExp& lhs;
  Symbol rhs;
};

struct MethodCall {
  const SuffixedExp& lhs;
  Symbol method;
  std::optional<std::reference_wrapper<const ExpList>> args;
};

struct FuncNameFieldSel {
  const FuncName& lhs;
  Symbol name;
};

struct FuncNameMethodSel {
  const FuncName& lhs;
  Symbol name;
};

struct DoStat {
  const StatList& body;
};

using Node =
    std::variant<Nil, StatList, Statement, IfStat, TestThenBlock, Expr,
                 SimpleExpr, Unop, Binop, SuffixedExp, PrimaryExp, ExprStat,
                 Assignment, FuncCall, ExpList, ForStat, Constructor, Field,
                 Index, FuncStat, FuncName, RetStat, LocalStat, LocalFunc,
                 FuncBody, FuncExpr, FieldSel, MethodCall, FuncNameFieldSel,
                 FuncNameMethodSel, DoStat>;
}  // namespace node

class Parser {
 public:
  Parser(const std::vector<Lexeme>& lexemes);
  node::StatList& root() noexcept { return root_; };
  const node::StatList& root() const noexcept { return root_; }

 private:
  const std::vector<Lexeme>& lexemes_;
  gsl::index pos_{};
  std::list<node::Node> nodes_;
  node::StatList& root_;

  const Lexeme& current() const noexcept {
    if (pos_ < static_cast<gsl::index>(lexemes_.size())) {
      return lexemes_[pos_];
    }
    return kNullLexeme;
  }

  const Lexeme& lookahead() const noexcept {
    if (pos_ + 1 < static_cast<gsl::index>(lexemes_.size())) {
      return lexemes_[pos_ + 1];
    }
    return kNullLexeme;
  }

  bool has_next() const noexcept {
    return pos_ + 1 < static_cast<gsl::index>(lexemes_.size());
  }

  void Next() { ++pos_; }

  [[noreturn]] void SyntaxError() const {
    using namespace std::string_literals;
    throw ParserException{"Syntax Error at line "s +
                          std::to_string(current().line_number)};
  }

  void Match(Lexeme::Type type) {
    if (current().type != type) {
      SyntaxError();
    }
    Next();
  }

  node::SimpleExpr& ParseSimpleExpr();
  node::Expr& ParseSubExpr(int priority);
  node::Expr& ParseExpr();
  node::StatList& ParseStatList();
  node::Statement& ParseStatement();
  node::IfStat& ParseIfStat();
  node::TestThenBlock& ParseTestThenBlock();
  node::SuffixedExp& ParseSuffixedExp();
  node::PrimaryExp& ParsePrimaryExp();
  node::ExprStat& ParseExprStat();
  node::Assignment& ParseAssignment(node::SuffixedExp& lhs);
  node::FuncCall& ParseFuncCall(node::SuffixedExp& func);
  node::ExpList& ParseExpList();
  node::ForStat& ParseForStat();
  node::Constructor& ParseConstructor();
  node::Field& ParseField();
  node::Index& ParseIndex(node::SuffixedExp& lhs);
  node::FuncStat& ParseFuncStat();
  node::FuncName& ParseFuncName();
  node::RetStat& ParseRetStat();
  node::LocalStat& ParseLocalStat();
  node::LocalFunc& ParseLocalFunc();
  node::FuncBody& ParseFuncBody();
  node::FuncExpr& ParseFuncExpr();
  node::FieldSel& ParseFieldSel(node::SuffixedExp& lhs);
  node::MethodCall& ParserMethodCall(node::SuffixedExp& lhs);
  node::FuncNameFieldSel& ParseFuncNameFieldSel(node::FuncName& lhs);
  node::FuncNameMethodSel& ParseFuncNameMethodSel(node::FuncName& lhs);
  node::DoStat& ParseDoStat();
  bool is_block_follow() const noexcept;
  bool is_unop() const noexcept;
  node::Unop::Type unop_type() const noexcept;
  bool is_binop() const noexcept;
  node::Binop::Type binop_type() const noexcept;
};
}  // namespace slua

#endif  // SLUA_PARSER_HPP_
