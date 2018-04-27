#include "emitter.hpp"

#include <cstdint>

#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <gsl/gsl>

#include <llvm/ADT/Optional.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/CodeGen.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Host.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

extern "C" {
#include "runtime/slua.h"
}

#include "parser.hpp"
#include "util.hpp"

namespace slua {
namespace {
using std::decay_t;
using std::error_code;
using std::get;
using std::holds_alternative;
using std::is_same_v;
using std::make_unique;
using std::move;
using std::runtime_error;
using std::string;
using std::to_string;
using std::unique_ptr;
using std::unordered_map;
using std::vector;
using std::visit;

using gsl::index;

using llvm::BasicBlock;
using llvm::Constant;
using llvm::ConstantDataArray;
using llvm::ConstantExpr;
using llvm::ConstantFP;
using llvm::ConstantStruct;
using llvm::Function;
using llvm::FunctionType;
using llvm::GlobalVariable;
using llvm::IRBuilder;
using llvm::InitializeNativeTarget;
using llvm::InitializeNativeTargetAsmPrinter;
using llvm::LLVMContext;
using llvm::Module;
using llvm::PassManagerBuilder;
using llvm::PointerType;
using llvm::StructType;
using llvm::Target;
using llvm::TargetMachine;
using llvm::TargetOptions;
using llvm::TargetRegistry;
using llvm::Type;
using llvm::Value;
using llvm::raw_fd_ostream;
using llvm::verifyFunction;
using llvm::verifyModule;

using llvm::legacy::FunctionPassManager;
using llvm::legacy::PassManager;
using llvm::sys::fs::OpenFlags;
using llvm::sys::getDefaultTargetTriple;

using node::Assignment;
using node::Binop;
using node::Constructor;
using node::ExpType;
using node::Expr;
using node::ExprStat;
using node::Field;
using node::ForStat;
using node::FuncBody;
using node::FuncCall;
using node::FuncExpr;
using node::FuncName;
using node::FuncStat;
using node::IfStat;
using node::Index;
using node::LiteralFloat;
using node::LiteralInt;
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

struct SymbolTable {
  unordered_map<string, Value*> addr;
  unique_ptr<SymbolTable> next;
  Function* func;
};

enum class ArithOp {
  kAdd,
  kSub,
  kMul,
  kDiv,
  kIntDiv,
  kMod,
  kLess,
  kLessEq,
  kGreater,
  kGreaterEq,
  kEq,
};

enum class LogicOp {
  kAnd,
  kOr,
};

class IrEmitter {
 public:
  IrEmitter(IRBuilder<>&, Module*, const vector<string>& symbols,
            Function* main_func);

  void Emit(const StatList&);
  void Emit(const Statement&);
  void Emit(const IfStat&);
  void Emit(const ExprStat&);
  void Emit(const Assignment&);
  void Emit(const ForStat&);
  void Emit(const FuncStat&);
  void Emit(const RetStat&);
  void Emit(const LocalStat&);
  void Emit(const LocalFunc&);
  void Emit(const FuncBody&, Function*);
  void EmitAssignment(Value* addr, Value* value);
  void EmitBreak();
  Value* Eval(const Expr&);
  Value* Eval(const SimpleExpr&);
  Value* Eval(const Unop&);
  Value* Eval(const Binop&);
  Value* Eval(const SuffixedExp&);
  Value* Eval(const PrimaryExp&);
  Value* Eval(const FuncCall&);
  Value* Eval(const Constructor&);
  Value* Eval(const Field&);
  Value* Eval(const Index&);
  Value* Eval(const FuncExpr&);
  Value* EvalArith(Value*, Value*, ArithOp);
  Value* EvalLogic(Value*, Value*, LogicOp);
  Value* EvalIntArith(Value*, Value*, ArithOp);
  Value* EvalFloatArith(Value*, Value*, ArithOp);
  Value* Addr(const SuffixedExp&, bool is_local);
  Value* Addr(const PrimaryExp&, bool is_local);
  Value* Addr(const Index&);
  Value* Addr(Symbol, bool is_local);
  Value* ToBool(Value*);
  Value* LookupSymbol(const string&, bool is_local);
  Value* ExtractType(Value*);
  Value* ExtractValue(Value*);
  Value* PointerToValue(Value* ptr);
  Value* PointerToType(Value* ptr);
  Value* ValueToFloat(Value*);
  Value* CallOverloadedOp(Value*, Value*, ArithOp);
  void EnterScope();
  void LeaveScope();
  void EmitDestroyScope(SymbolTable&);
  BasicBlock* CreateBlock(const string& name);
  void TableRefInc(Value*);
  void TableRefDec(Value*);
  Value* GetTablePtr(Value*);
  void TableArrayAppend(Value* value, Value* table_ptr);
  Value* PointerToTableArraySize(Value* table_ptr);
  Value* PointerToTableArrayCapacity(Value* table_ptr);
  Value* PointerToTableArray(Value* table_ptr);
  Function* CreateFunc(Value* ptr);

  const vector<Function*> functions() const { return functions_; }

 private:
  IRBuilder<>& builder_;
  Module* module_;
  StructType* value_type_;
  StructType* table_type_;

  Function* func_add_;             // value f(value, value)
  Function* func_sub_;             // value f(value, value)
  Function* func_mul_;             // value f(value, value)
  Function* func_div_;             // value f(value, value)
  Function* func_idiv_;            // value f(value, value)
  Function* func_mod_;             // value f(value, value)
  Function* func_lt_;              // value f(value, value)
  Function* func_le_;              // value f(value, value)
  Function* func_gt_;              // value f(value, value)
  Function* func_ge_;              // value f(value, value)
  Function* func_eq_;              // value f(value, value)
  Function* func_len_;             // value f(value)
  Function* func_runtime_error_;   // void f(const char*)
  Function* func_invoke_builtin_;  // value f(const char* name, int count, ...)
  Function* func_table_new_;       // table* f()
  Function* func_table_ref_inc_;   // void f(table*)
  Function* func_table_ref_dec_;   // void f(table*)
  Function* func_table_array_grow_;  // void f(table*)
  Function* func_table_access_;      // value* f(value, value)

  Function* func_fmod_;   // double f(double, double)
  Function* func_floor_;  // double f(double)

  const vector<string>& symbols_;
  unique_ptr<SymbolTable> symbol_table_;

  vector<Function*> functions_;
  Function* curr_func_;

  index temp_name_{};

  struct {
    BasicBlock* post_block;
    SymbolTable* scope;
  } break_info_{};
};

IrEmitter::IrEmitter(IRBuilder<>& builder, Module* module,
                     const vector<string>& symbols, Function* main_func)
    : builder_{builder},
      module_{module},
      value_type_{StructType::create(
          {builder.getInt64Ty(), builder.getInt64Ty()}, "val_t")},
      table_type_{StructType::create(
          {
              builder.getInt64Ty(),                         // ref_count
              builder.getInt64Ty(),                         // array_size
              builder.getInt64Ty(),                         // array_capacity
              PointerType::getUnqual(value_type_),          // array_ptr
              PointerType::getUnqual(builder.getVoidTy()),  // hash_ptr
          },
          "table_t")},

      func_add_{Function::Create(
          FunctionType::get(value_type_, {value_type_, value_type_}, false),
          Function::ExternalLinkage, "slua_add", module)},
      func_sub_{Function::Create(
          FunctionType::get(value_type_, {value_type_, value_type_}, false),
          Function::ExternalLinkage, "slua_sub", module)},
      func_mul_{Function::Create(
          FunctionType::get(value_type_, {value_type_, value_type_}, false),
          Function::ExternalLinkage, "slua_mul", module)},
      func_div_{Function::Create(
          FunctionType::get(value_type_, {value_type_, value_type_}, false),
          Function::ExternalLinkage, "slua_div", module)},
      func_idiv_{Function::Create(
          FunctionType::get(value_type_, {value_type_, value_type_}, false),
          Function::ExternalLinkage, "slua_idiv", module)},
      func_mod_{Function::Create(
          FunctionType::get(value_type_, {value_type_, value_type_}, false),
          Function::ExternalLinkage, "slua_mod", module)},
      func_lt_{Function::Create(
          FunctionType::get(value_type_, {value_type_, value_type_}, false),
          Function::ExternalLinkage, "slua_lt", module)},
      func_le_{Function::Create(
          FunctionType::get(value_type_, {value_type_, value_type_}, false),
          Function::ExternalLinkage, "slua_le", module)},
      func_gt_{Function::Create(
          FunctionType::get(value_type_, {value_type_, value_type_}, false),
          Function::ExternalLinkage, "slua_gt", module)},
      func_ge_{Function::Create(
          FunctionType::get(value_type_, {value_type_, value_type_}, false),
          Function::ExternalLinkage, "slua_ge", module)},
      func_eq_{Function::Create(
          FunctionType::get(value_type_, {value_type_, value_type_}, false),
          Function::ExternalLinkage, "slua_eq", module)},

      func_len_{
          Function::Create(FunctionType::get(value_type_, {value_type_}, false),
                           Function::ExternalLinkage, "slua_len", module)},

      func_runtime_error_{Function::Create(
          FunctionType::get(builder.getVoidTy(),
                            {PointerType::getUnqual(builder.getInt8Ty())},
                            false),
          Function::ExternalLinkage, "slua_runtime_error", module)},

      func_invoke_builtin_{Function::Create(
          FunctionType::get(value_type_,
                            {PointerType::getUnqual(builder.getInt8Ty()),
                             builder.getInt32Ty()},
                            true),
          Function::ExternalLinkage, "slua_invoke_builtin", module)},

      func_table_new_{Function::Create(
          FunctionType::get(PointerType::getUnqual(table_type_), {}, false),
          Function::ExternalLinkage, "slua_table_new", module)},

      func_table_ref_inc_{Function::Create(
          FunctionType::get(builder.getVoidTy(),
                            {PointerType::getUnqual(table_type_)}, false),
          Function::ExternalLinkage, "slua_table_ref_inc", module)},

      func_table_ref_dec_{Function::Create(
          FunctionType::get(builder.getVoidTy(),
                            {PointerType::getUnqual(table_type_)}, false),
          Function::ExternalLinkage, "slua_table_ref_dec", module)},

      func_table_array_grow_{Function::Create(
          FunctionType::get(builder.getVoidTy(),
                            {PointerType::getUnqual(table_type_)}, false),
          Function::ExternalLinkage, "slua_table_array_grow", module)},

      func_table_access_{Function::Create(
          FunctionType::get(PointerType::getUnqual(value_type_),
                            {value_type_, value_type_}, false),
          Function::ExternalLinkage, "slua_table_access", module)},

      func_fmod_{Function::Create(
          FunctionType::get(builder.getDoubleTy(),
                            {builder.getDoubleTy(), builder.getDoubleTy()},
                            false),
          Function::ExternalLinkage, "fmod", module)},
      func_floor_{
          Function::Create(FunctionType::get(builder.getDoubleTy(),
                                             {builder.getDoubleTy()}, false),
                           Function::ExternalLinkage, "floor", module)},

      symbols_{symbols},
      symbol_table_{make_unique<SymbolTable>()},

      functions_{main_func},
      curr_func_{main_func}

{
  LLVMContext& context = builder.getContext();

  for (const char* builtin_name : {"print"}) {
    Constant* name = ConstantDataArray::getString(context, builtin_name, true);
    GlobalVariable* name_global = new GlobalVariable(
        *module, name->getType(), true, GlobalVariable::PrivateLinkage, name);
    Value* addr = new GlobalVariable(
        *module, value_type_, true, GlobalVariable::PrivateLinkage,
        ConstantStruct::get(
            value_type_,
            {
                builder.getInt64(kSluaValueBuiltinFunction),
                ConstantExpr::getInBoundsGetElementPtr(
                    name->getType(), name_global, builder.getInt32(0)),
            }));
    symbol_table_->addr[builtin_name] = addr;
  }
}

void IrEmitter::Emit(const StatList& stat_list) {
  for (const Statement& stat : stat_list.stats) {
    Emit(stat);
  }
}

void IrEmitter::Emit(const Statement& stat) {
  visit(
      Overloaded{
          [this](const auto& ref) { Emit(ref.get()); },
          [this](Statement::BreakStat) { EmitBreak(); },
          [](Statement::NullStat) {},
      },
      stat.stat);
}

void IrEmitter::Emit(const IfStat& if_stat) {
  BasicBlock* post_block = CreateBlock("if_post");

  for (const auto& then : if_stat.cond_blocks) {
    Value* cond = Eval(then.get().cond);
    Value* cmp = ToBool(cond);

    BasicBlock* then_block = CreateBlock("if_then");
    BasicBlock* next_block = CreateBlock("if_next");
    builder_.CreateCondBr(cmp, then_block, next_block);
    builder_.SetInsertPoint(then_block);

    EnterScope();
    Emit(then.get().then);
    LeaveScope();

    builder_.CreateBr(post_block);
    builder_.SetInsertPoint(next_block);
  }

  if (if_stat.else_block.has_value()) {
    Emit(if_stat.else_block.value());
  }
  builder_.CreateBr(post_block);

  builder_.SetInsertPoint(post_block);
}

Value* IrEmitter::Eval(const Expr& expr) {
  return visit([this](const auto& ref) { return Eval(ref.get()); }, expr.expr);
}

Value* IrEmitter::Eval(const SimpleExpr& expr) {
  return visit(
      Overloaded{
          [this](const auto& ref) -> Value* { return Eval(ref.get()); },
          [this](Nil) -> Value* {
            return ConstantStruct::get(
                value_type_,
                {builder_.getInt64(kSluaValueNil), builder_.getInt64(0)});
          },
          [this](const LiteralInt& literal) -> Value* {
            return ConstantStruct::get(value_type_,
                                       {builder_.getInt64(kSluaValueInteger),
                                        builder_.getInt64(literal.value)});
          },
          [this](const LiteralFloat& literal) -> Value* {
            return ConstantStruct::get(
                value_type_,
                {builder_.getInt64(kSluaValueFloat),
                 ConstantExpr::getBitCast(
                     ConstantFP::get(builder_.getDoubleTy(), literal.value),
                     builder_.getInt64Ty())});
          },
      },
      expr.expr);
}

Value* IrEmitter::Eval(const Unop& op) {
  switch (op.type) {
    case node::Unop::kLen:
      return builder_.CreateCall(func_len_, {Eval(op.expr)});
    default:
      throw runtime_error{"Not implemented"};
  }
}

Value* IrEmitter::Eval(const Binop& op) {
  Value* lhs = Eval(op.lhs);
  Value* rhs = Eval(op.rhs);
  Function* func;
  switch (op.type) {
    case node::Binop::kAdd:
      return EvalArith(lhs, rhs, ArithOp::kAdd);
    case node::Binop::kSub:
      return EvalArith(lhs, rhs, ArithOp::kSub);
    case node::Binop::kMul:
      return EvalArith(lhs, rhs, ArithOp::kMul);
    case node::Binop::kDiv:
      return EvalArith(lhs, rhs, ArithOp::kDiv);
    case node::Binop::kIntDiv:
      return EvalArith(lhs, rhs, ArithOp::kIntDiv);
    case node::Binop::kMod:
      return EvalArith(lhs, rhs, ArithOp::kMod);
    case node::Binop::kLess:
      return EvalArith(lhs, rhs, ArithOp::kLess);
    case node::Binop::kLessEq:
      return EvalArith(lhs, rhs, ArithOp::kLessEq);
    case node::Binop::kGreater:
      return EvalArith(lhs, rhs, ArithOp::kGreater);
    case node::Binop::kGreaterEq:
      return EvalArith(lhs, rhs, ArithOp::kGreaterEq);
    case node::Binop::kEq:
      return EvalArith(lhs, rhs, ArithOp::kEq);
    case node::Binop::kAnd:
      return EvalLogic(lhs, rhs, LogicOp::kAnd);
    case node::Binop::kOr:
      return EvalLogic(lhs, rhs, LogicOp::kOr);
    default:
      throw runtime_error{"Not implemented"};
  }
  return builder_.CreateCall(func, {lhs, rhs});
}

Value* IrEmitter::Eval(const SuffixedExp& expr) {
  return visit([this](const auto& ref) -> Value* { return Eval(ref.get()); },
               expr.expr);
}

Value* IrEmitter::Eval(const PrimaryExp& expr) {
  return visit(
      Overloaded{[this](const Symbol& symbol) -> Value* {
                   Value* addr = Addr(symbol, false);
                   return builder_.CreateLoad(value_type_, addr);
                 },
                 [this](const auto& ref) -> Value* { return Eval(ref.get()); }},
      expr.expr);
}

void IrEmitter::Emit(const ExprStat& stat) {
  visit(
      [this](const auto& ref) {
        using T = decay_t<decltype(ref.get())>;
        if constexpr (is_same_v<T, SuffixedExp>) {
          Eval(ref.get());
        } else {
          Emit(ref.get());
        }
      },
      stat.expr);
}

Value* IrEmitter::Eval(const FuncCall& func_call) {
  Value* func = Eval(func_call.func);
  Value* is_func = builder_.CreateICmpEQ(ExtractType(func),
                                         builder_.getInt64(kSluaValueFunction));
  Value* is_builtin_func = builder_.CreateICmpEQ(
      ExtractType(func), builder_.getInt64(kSluaValueBuiltinFunction));

  vector<Value*> args;
  if (func_call.args.has_value()) {
    for (const Expr& arg : func_call.args.value().get().exps) {
      args.push_back(Eval(arg));
    }
  }

  Value* result_ptr = builder_.CreateAlloca(value_type_);

  BasicBlock* then_block = CreateBlock("func_check_then");
  BasicBlock* post_block = CreateBlock("func_check_post");
  builder_.CreateCondBr(
      builder_.CreateNot(builder_.CreateOr(is_func, is_builtin_func)),
      then_block, post_block);

  builder_.SetInsertPoint(then_block);
  builder_.CreateCall(func_runtime_error_, builder_.CreateGlobalStringPtr(
                                               "Error: value not callable"));
  builder_.CreateUnreachable();

  builder_.SetInsertPoint(post_block);
  then_block = CreateBlock("func_type_then");
  post_block = CreateBlock("func_type_post");
  BasicBlock* else_block = CreateBlock("func_type_else");
  builder_.CreateCondBr(is_func, then_block, else_block);

  builder_.SetInsertPoint(then_block);
  EnterScope();
  Value* arg_table_ptr = LookupSymbol("_temp_" + to_string(temp_name_), true);
  ++temp_name_;
  builder_.CreateStore(builder_.getInt64(kSluaValueTable),
                       PointerToType(arg_table_ptr));
  builder_.CreateStore(
      builder_.CreatePtrToInt(builder_.CreateCall(func_table_new_),
                              builder_.getInt64Ty()),
      PointerToValue(arg_table_ptr));
  Value* arg_table = builder_.CreateLoad(arg_table_ptr);
  for (index i = 0; i < static_cast<index>(args.size()); ++i) {
    Value* index_ptr = builder_.CreateAlloca(value_type_);
    builder_.CreateStore(builder_.getInt64(kSluaValueInteger),
                         PointerToType(index_ptr));
    builder_.CreateStore(builder_.getInt64(i + 1), PointerToValue(index_ptr));
    Value* arg_ptr = builder_.CreateCall(
        func_table_access_, {arg_table, builder_.CreateLoad(index_ptr)});
    builder_.CreateStore(args[i], arg_ptr);
  }
  TableRefInc(arg_table);
  Value* result = builder_.CreateCall(
      FunctionType::get(value_type_, {value_type_}, false),
      builder_.CreateIntToPtr(ExtractValue(func),
                              PointerType::getUnqual(FunctionType::get(
                                  value_type_, {value_type_}, false))),
      {arg_table});
  builder_.CreateStore(result, result_ptr);
  LeaveScope();
  builder_.CreateBr(post_block);

  builder_.SetInsertPoint(else_block);

  args.insert(args.begin(), nullptr);
  args.insert(args.begin(),
              builder_.CreateIntToPtr(
                  ExtractValue(func),
                  PointerType::getInt8PtrTy(builder_.getContext())));
  if (func_call.args.has_value()) {
    args[1] = builder_.getInt32(func_call.args.value().get().exps.size());
  } else {
    args[1] = builder_.getInt32(0);
  }
  result = builder_.CreateCall(func_invoke_builtin_, args);
  builder_.CreateStore(result, result_ptr);
  builder_.CreateBr(post_block);

  builder_.SetInsertPoint(post_block);
  return builder_.CreateLoad(result_ptr);
}

void IrEmitter::Emit(const Assignment& assignment) {
  Value* value = Eval(assignment.rhs);
  Value* addr = Addr(assignment.lhs, false);
  EmitAssignment(addr, value);
}

void IrEmitter::EmitAssignment(Value* addr, Value* value) {
  Value* tag_table = builder_.getInt64(kSluaValueTable);

  BasicBlock* then_block = CreateBlock("assign_lhs_then");
  BasicBlock* post_block = CreateBlock("assign_lhs_post");

  Value* lhs = builder_.CreateLoad(addr);
  Value* lhs_type = ExtractType(lhs);
  Value* cmp = builder_.CreateICmpEQ(lhs_type, tag_table);
  builder_.CreateCondBr(cmp, then_block, post_block);

  builder_.SetInsertPoint(then_block);
  TableRefDec(lhs);
  builder_.CreateBr(post_block);

  builder_.SetInsertPoint(post_block);
  builder_.CreateStore(value, addr);

  then_block = CreateBlock("assign_rhs_then");
  post_block = CreateBlock("assign_rhs_post");

  Value* rhs_type = ExtractType(value);
  cmp = builder_.CreateICmpEQ(rhs_type, tag_table);
  builder_.CreateCondBr(cmp, then_block, post_block);

  builder_.SetInsertPoint(then_block);
  TableRefInc(value);
  builder_.CreateBr(post_block);

  builder_.SetInsertPoint(post_block);
}

Value* IrEmitter::Addr(const SuffixedExp& expr, bool is_local) {
  return visit(
      Overloaded{
          [this, is_local](const PrimaryExp& exp) -> Value* {
            return Addr(exp, is_local);
          },
          [this](const Index& in) -> Value* { return Addr(in); },
          [](const FuncCall&) -> Value* {
            throw ParserException{"Cannot get address of function call"};
          },
      },
      expr.expr);
}

Value* IrEmitter::Addr(const PrimaryExp& expr, bool is_local) {
  return Addr(get<Symbol>(expr.expr), is_local);
}

Value* IrEmitter::Addr(Symbol name, bool is_local) {
  return LookupSymbol(symbols_[name.name], is_local);
}

Value* IrEmitter::ToBool(Value* value) {
  Value* type = ExtractType(value);
  Value* is_bool =
      builder_.CreateICmpEQ(type, builder_.getInt64(kSluaValueBool));
  Value* is_nil = builder_.CreateICmpEQ(type, builder_.getInt64(kSluaValueNil));
  Value* bool_false = builder_.CreateAnd(
      is_bool,
      builder_.CreateICmpEQ(ExtractValue(value), builder_.getInt64(0)));
  return builder_.CreateNot(builder_.CreateOr(is_nil, bool_false));
}

Value* IrEmitter::LookupSymbol(const string& name, bool is_local) {
  SymbolTable* table = symbol_table_.get();
  auto iter = table->addr.find(name);
  while (!is_local && iter == table->addr.end() && table->next != nullptr) {
    table = table->next.get();
    iter = table->addr.find(name);
  }
  if (iter == table->addr.end()) {
    Value*& addr = is_local ? symbol_table_->addr[name] : table->addr[name];
    Constant* nil = ConstantStruct::get(
        value_type_, {builder_.getInt64(kSluaValueNil), builder_.getInt64(0)});
    if (is_local) {
      addr = builder_.CreateAlloca(value_type_);
      builder_.CreateStore(nil, addr);
    } else {
      addr = new GlobalVariable(*module_, value_type_, false,
                                GlobalVariable::PrivateLinkage, nil);
    }
    return addr;
  }
  return iter->second;
}

Value* IrEmitter::ExtractType(Value* value) {
  return builder_.CreateExtractValue(value, 0);
}

Value* IrEmitter::ExtractValue(Value* value) {
  return builder_.CreateExtractValue(value, 1);
}

void IrEmitter::EnterScope() {
  unique_ptr<SymbolTable> table = make_unique<SymbolTable>();
  table->next = move(symbol_table_);
  table->func = curr_func_;
  symbol_table_ = move(table);
}

void IrEmitter::LeaveScope() {
  if (symbol_table_->next == nullptr) {
    throw runtime_error{"Cannot leave global scope"};
  }

  EmitDestroyScope(*symbol_table_);

  unique_ptr<SymbolTable> table = move(symbol_table_->next);
  symbol_table_ = move(table);
}

void IrEmitter::EmitDestroyScope(SymbolTable& symbol_table) {
  for (const auto& symbol : symbol_table.addr) {
    Value* value_ptr = symbol.second;
    Value* value = builder_.CreateLoad(value_ptr);
    Value* type = ExtractType(value);

    BasicBlock* then_block = CreateBlock("leave_then");
    BasicBlock* post_block = CreateBlock("leave_post");

    Value* cmp =
        builder_.CreateICmpEQ(type, builder_.getInt64(kSluaValueTable));
    builder_.CreateCondBr(cmp, then_block, post_block);

    builder_.SetInsertPoint(then_block);
    TableRefDec(value);
    builder_.CreateBr(post_block);

    builder_.SetInsertPoint(post_block);
  }
}

void IrEmitter::Emit(const ForStat& for_stat) {
  BasicBlock* test_block = CreateBlock("for_test");
  BasicBlock* body_block = CreateBlock("for_body");
  BasicBlock* post_block = CreateBlock("for_post");

  EnterScope();
  auto outer_break_info = break_info_;
  break_info_ = {post_block, symbol_table_.get()};
  Value* count = Addr(for_stat.symbol, true);
  Value* init = Eval(for_stat.initial);
  Value* limit = Eval(for_stat.limit);
  Value* step = Eval(for_stat.step);
  builder_.CreateStore(init, count);

  builder_.CreateBr(test_block);

  builder_.SetInsertPoint(test_block);
  Value* value = builder_.CreateLoad(count);
  Value* cmp = EvalArith(value, limit, ArithOp::kLessEq);
  cmp = builder_.CreateICmpNE(ExtractValue(cmp), builder_.getInt64(0));
  builder_.CreateCondBr(cmp, body_block, post_block);

  builder_.SetInsertPoint(body_block);

  EnterScope();
  Emit(for_stat.body);
  LeaveScope();

  value = builder_.CreateLoad(count);
  Value* next = EvalArith(value, step, ArithOp::kAdd);
  builder_.CreateStore(next, count);
  builder_.CreateBr(test_block);

  builder_.SetInsertPoint(post_block);
  break_info_ = outer_break_info;
  LeaveScope();
}

Value* IrEmitter::PointerToValue(Value* ptr) {
  return builder_.CreateStructGEP(value_type_, ptr, 1);
}

Value* IrEmitter::PointerToType(Value* ptr) {
  return builder_.CreateStructGEP(value_type_, ptr, 0);
}

Value* IrEmitter::EvalArith(Value* lhs, Value* rhs, ArithOp op) {
  switch (op) {
    case ArithOp::kAdd:
    case ArithOp::kSub:
    case ArithOp::kMul:
    case ArithOp::kIntDiv:
    case ArithOp::kLess:
    case ArithOp::kLessEq:
    case ArithOp::kGreater:
    case ArithOp::kGreaterEq:
    case ArithOp::kMod:
    case ArithOp::kEq: {
      Value* lhs_type = ExtractType(lhs);
      Value* rhs_type = ExtractType(rhs);
      Value* lhs_value = ExtractValue(lhs);
      Value* rhs_value = ExtractValue(rhs);
      Value* table_type = builder_.getInt64(kSluaValueTable);
      Value* int_type = builder_.getInt64(kSluaValueInteger);
      Value* float_type = builder_.getInt64(kSluaValueFloat);
      Value* is_lhs_table = builder_.CreateICmpEQ(lhs_type, table_type);
      Value* is_rhs_table = builder_.CreateICmpEQ(rhs_type, table_type);
      Value* is_lhs_int = builder_.CreateICmpEQ(lhs_type, int_type);
      Value* is_rhs_int = builder_.CreateICmpEQ(rhs_type, int_type);
      Value* is_lhs_float = builder_.CreateICmpEQ(lhs_type, float_type);
      Value* is_rhs_float = builder_.CreateICmpEQ(rhs_type, float_type);

      BasicBlock* table_block = CreateBlock("arith_table");
      BasicBlock* test_int_block = CreateBlock("arith_test_int");
      BasicBlock* int_block = CreateBlock("arith_int");
      BasicBlock* test_promo_lhs_block = CreateBlock("arith_test_promo_lhs");
      BasicBlock* promo_lhs_block = CreateBlock("arith_promo_lhs");
      BasicBlock* test_promo_rhs_block = CreateBlock("arith_test_promo_rhs");
      BasicBlock* promo_rhs_block = CreateBlock("arith_promo_rhs");
      BasicBlock* test_float_block = CreateBlock("arith_test_float");
      BasicBlock* float_block = CreateBlock("arith_flaot");
      BasicBlock* error_block = CreateBlock("arith_error");
      BasicBlock* post_block = CreateBlock("arith_post");

      Value* result_ptr = builder_.CreateAlloca(value_type_);

      builder_.CreateCondBr(builder_.CreateOr(is_lhs_table, is_rhs_table),
                            table_block, test_int_block);

      builder_.SetInsertPoint(table_block);
      Value* result = CallOverloadedOp(lhs, rhs, op);
      builder_.CreateStore(result, result_ptr);
      builder_.CreateBr(post_block);

      builder_.SetInsertPoint(test_int_block);
      builder_.CreateCondBr(builder_.CreateAnd(is_lhs_int, is_rhs_int),
                            int_block, test_promo_lhs_block);

      builder_.SetInsertPoint(int_block);
      result = EvalIntArith(lhs_value, rhs_value, op);
      builder_.CreateStore(result, result_ptr);
      builder_.CreateBr(post_block);

      builder_.SetInsertPoint(test_promo_lhs_block);
      builder_.CreateCondBr(builder_.CreateAnd(is_lhs_int, is_rhs_float),
                            promo_lhs_block, test_promo_rhs_block);

      builder_.SetInsertPoint(promo_lhs_block);
      Value* promo = builder_.CreateSIToFP(lhs_value, builder_.getDoubleTy());
      Value* reinterpret =
          builder_.CreateBitCast(rhs_value, builder_.getDoubleTy());
      result = EvalFloatArith(promo, reinterpret, op);
      builder_.CreateStore(result, result_ptr);
      builder_.CreateBr(post_block);

      builder_.SetInsertPoint(test_promo_rhs_block);
      builder_.CreateCondBr(builder_.CreateAnd(is_lhs_float, is_rhs_int),
                            promo_rhs_block, test_float_block);

      builder_.SetInsertPoint(promo_rhs_block);
      reinterpret = builder_.CreateBitCast(lhs_value, builder_.getDoubleTy());
      promo = builder_.CreateSIToFP(rhs_value, builder_.getDoubleTy());
      result = EvalFloatArith(reinterpret, promo, op);
      builder_.CreateStore(result, result_ptr);
      builder_.CreateBr(post_block);

      builder_.SetInsertPoint(test_float_block);
      builder_.CreateCondBr(builder_.CreateAnd(is_lhs_float, is_rhs_float),
                            float_block, error_block);

      builder_.SetInsertPoint(float_block);
      result = EvalFloatArith(
          builder_.CreateBitCast(lhs_value, builder_.getDoubleTy()),
          builder_.CreateBitCast(rhs_value, builder_.getDoubleTy()), op);
      builder_.CreateStore(result, result_ptr);
      builder_.CreateBr(post_block);

      builder_.SetInsertPoint(error_block);
      if (op == ArithOp::kEq) {
        Value* type_cmp = builder_.CreateICmpEQ(lhs_type, rhs_type);
        Value* value_cmp = builder_.CreateICmpEQ(lhs_value, rhs_value);
        Value* eq_cmp = builder_.CreateAnd(type_cmp, value_cmp);
        builder_.CreateStore(builder_.getInt64(kSluaValueBool),
                             PointerToType(result_ptr));
        builder_.CreateStore(builder_.CreateZExt(eq_cmp, builder_.getInt64Ty()),
                             PointerToValue(result_ptr));
        builder_.CreateBr(post_block);
      } else {
        builder_.CreateCall(func_runtime_error_,
                            builder_.CreateGlobalStringPtr(
                                "Error: cannot perform arithmetic operation"));
        builder_.CreateUnreachable();
      }

      builder_.SetInsertPoint(post_block);
      return builder_.CreateLoad(result_ptr);
    }
    case ArithOp::kDiv: {
      Value* lhs_value = ValueToFloat(lhs);
      Value* rhs_value = ValueToFloat(rhs);
      Value* result = builder_.CreateBitCast(
          builder_.CreateFDiv(lhs_value, rhs_value), builder_.getInt64Ty());
      Value* result_ptr = builder_.CreateAlloca(value_type_);
      builder_.CreateStore(builder_.getInt64(kSluaValueFloat),
                           PointerToType(result_ptr));
      builder_.CreateStore(result, PointerToValue(result_ptr));
      return builder_.CreateLoad(result_ptr);
    }
    default:
      throw runtime_error{"Arithmetic op not implemented"};
  }
}

BasicBlock* IrEmitter::CreateBlock(const string& name) {
  return BasicBlock::Create(builder_.getContext(), name,
                            builder_.GetInsertBlock()->getParent());
}

Value* IrEmitter::EvalIntArith(Value* lhs_value, Value* rhs_value, ArithOp op) {
  Value* result_ptr = builder_.CreateAlloca(value_type_);
  Value* type_ptr = PointerToType(result_ptr);
  Value* value_ptr = PointerToValue(result_ptr);

  Value* int_type = builder_.getInt64(kSluaValueInteger);
  Value* bool_type = builder_.getInt64(kSluaValueBool);

  switch (op) {
    case ArithOp::kAdd:
    case ArithOp::kSub:
    case ArithOp::kMul:
    case ArithOp::kIntDiv:
    case ArithOp::kMod:
      builder_.CreateStore(int_type, type_ptr);
      break;
    case ArithOp::kLess:
    case ArithOp::kLessEq:
    case ArithOp::kGreater:
    case ArithOp::kGreaterEq:
    case ArithOp::kEq:
      builder_.CreateStore(bool_type, type_ptr);
      break;
    default:
      throw runtime_error{"Int arith op not handled"};
  }

  Value* value;
  switch (op) {
    case ArithOp::kAdd:
      value = builder_.CreateAdd(lhs_value, rhs_value);
      break;
    case ArithOp::kSub:
      value = builder_.CreateSub(lhs_value, rhs_value);
      break;
    case ArithOp::kMul:
      value = builder_.CreateMul(lhs_value, rhs_value);
      break;
    case ArithOp::kMod:
      value = builder_.CreateSRem(lhs_value, rhs_value);
      break;
    case ArithOp::kIntDiv:
      value = builder_.CreateSDiv(lhs_value, rhs_value);
      break;
    case ArithOp::kLess:
      value = builder_.CreateZExt(builder_.CreateICmpSLT(lhs_value, rhs_value),
                                  builder_.getInt64Ty());
      break;
    case ArithOp::kLessEq:
      value = builder_.CreateZExt(builder_.CreateICmpSLE(lhs_value, rhs_value),
                                  builder_.getInt64Ty());
      break;
    case ArithOp::kGreater:
      value = builder_.CreateZExt(builder_.CreateICmpSGT(lhs_value, rhs_value),
                                  builder_.getInt64Ty());
      break;
    case ArithOp::kGreaterEq:
      value = builder_.CreateZExt(builder_.CreateICmpSGE(lhs_value, rhs_value),
                                  builder_.getInt64Ty());
      break;
    case ArithOp::kEq:
      value = builder_.CreateZExt(builder_.CreateICmpEQ(lhs_value, rhs_value),
                                  builder_.getInt64Ty());
      break;
    default:
      throw runtime_error{"Int arith op not handled"};
  }
  builder_.CreateStore(value, value_ptr);

  return builder_.CreateLoad(result_ptr);
}

Value* IrEmitter::EvalFloatArith(Value* lhs_value, Value* rhs_value,
                                 ArithOp op) {
  Value* result_ptr = builder_.CreateAlloca(value_type_);
  Value* type_ptr = PointerToType(result_ptr);
  Value* value_ptr = PointerToValue(result_ptr);

  Value* float_type = builder_.getInt64(kSluaValueFloat);
  Value* bool_type = builder_.getInt64(kSluaValueBool);

  switch (op) {
    case ArithOp::kAdd:
    case ArithOp::kSub:
    case ArithOp::kMul:
    case ArithOp::kDiv:
    case ArithOp::kIntDiv:
    case ArithOp::kMod:
      builder_.CreateStore(float_type, type_ptr);
      break;
    case ArithOp::kLess:
    case ArithOp::kLessEq:
    case ArithOp::kGreater:
    case ArithOp::kGreaterEq:
    case ArithOp::kEq:
      builder_.CreateStore(bool_type, type_ptr);
      break;
    default:
      throw runtime_error{"Int arith op not handled"};
  }

  Value* result;
  switch (op) {
    case ArithOp::kAdd:
      result = builder_.CreateFAdd(lhs_value, rhs_value);
      break;
    case ArithOp::kSub:
      result = builder_.CreateFSub(lhs_value, rhs_value);
      break;
    case ArithOp::kMul:
      result = builder_.CreateFMul(lhs_value, rhs_value);
      break;
    case ArithOp::kDiv:
      result = builder_.CreateFDiv(lhs_value, rhs_value);
      break;
    case ArithOp::kIntDiv:
      result = builder_.CreateFDiv(lhs_value, rhs_value);
      result = builder_.CreateCall(func_floor_, result);
      break;
    case ArithOp::kMod:
      result = builder_.CreateCall(func_fmod_, {lhs_value, rhs_value});
      break;
    case ArithOp::kLess:
      result = builder_.CreateFCmpOLT(lhs_value, rhs_value);
      break;
    case ArithOp::kLessEq:
      result = builder_.CreateFCmpOLE(lhs_value, rhs_value);
      break;
    case ArithOp::kGreater:
      result = builder_.CreateFCmpOGT(lhs_value, rhs_value);
      break;
    case ArithOp::kGreaterEq:
      result = builder_.CreateFCmpOGE(lhs_value, rhs_value);
      break;
    case ArithOp::kEq:
      result = builder_.CreateFCmpOEQ(lhs_value, rhs_value);
      break;
    default:
      throw runtime_error{"Float arith op not handled"};
  }
  result = builder_.CreateZExtOrBitCast(result, builder_.getInt64Ty());
  builder_.CreateStore(result, value_ptr);

  return builder_.CreateLoad(result_ptr);
}

Value* IrEmitter::ValueToFloat(Value* v) {
  BasicBlock* float_block = CreateBlock("v2f_float");
  BasicBlock* test_int_block = CreateBlock("v2f_test_int");
  BasicBlock* int_block = CreateBlock("v2f_int");
  BasicBlock* error_block = CreateBlock("v2f_error");
  BasicBlock* post_block = CreateBlock("v2f_post");
  Type* float_type = builder_.getDoubleTy();
  Value* result_ptr = builder_.CreateAlloca(float_type);
  Value* type = ExtractType(v);
  Value* value = ExtractValue(v);

  Value* is_float =
      builder_.CreateICmpEQ(type, builder_.getInt64(kSluaValueFloat));
  Value* is_int =
      builder_.CreateICmpEQ(type, builder_.getInt64(kSluaValueInteger));
  builder_.CreateCondBr(is_float, float_block, test_int_block);

  builder_.SetInsertPoint(float_block);
  Value* result = builder_.CreateBitCast(value, float_type);
  builder_.CreateStore(result, result_ptr);
  builder_.CreateBr(post_block);

  builder_.SetInsertPoint(test_int_block);
  builder_.CreateCondBr(is_int, int_block, error_block);

  builder_.SetInsertPoint(int_block);
  result = builder_.CreateSIToFP(value, float_type);
  builder_.CreateStore(result, result_ptr);
  builder_.CreateBr(post_block);

  builder_.SetInsertPoint(error_block);
  builder_.CreateCall(func_runtime_error_,
                      builder_.CreateGlobalStringPtr(
                          "Error: cannot perform arithmetic operation"));
  builder_.CreateUnreachable();

  builder_.SetInsertPoint(post_block);
  return builder_.CreateLoad(result_ptr);
}

Value* IrEmitter::CallOverloadedOp(Value* lhs, Value* rhs, ArithOp op) {
  switch (op) {
    case ArithOp::kAdd:
      return builder_.CreateCall(func_add_, {lhs, rhs});
    case ArithOp::kSub:
      return builder_.CreateCall(func_sub_, {lhs, rhs});
    case ArithOp::kMul:
      return builder_.CreateCall(func_mul_, {lhs, rhs});
    case ArithOp::kDiv:
      return builder_.CreateCall(func_div_, {lhs, rhs});
    case ArithOp::kIntDiv:
      return builder_.CreateCall(func_idiv_, {lhs, rhs});
    case ArithOp::kMod:
      return builder_.CreateCall(func_mod_, {lhs, rhs});
    case ArithOp::kLess:
      return builder_.CreateCall(func_lt_, {lhs, rhs});
    case ArithOp::kLessEq:
      return builder_.CreateCall(func_le_, {lhs, rhs});
    case ArithOp::kGreater:
      return builder_.CreateCall(func_gt_, {lhs, rhs});
    case ArithOp::kGreaterEq:
      return builder_.CreateCall(func_ge_, {lhs, rhs});
    case ArithOp::kEq:
      return builder_.CreateCall(func_eq_, {lhs, rhs});
    default:
      throw runtime_error{"Operator not overloadable"};
  }
}

void IrEmitter::TableRefInc(Value* value) {
  builder_.CreateCall(func_table_ref_inc_, {GetTablePtr(value)});
}

void IrEmitter::TableRefDec(Value* value) {
  builder_.CreateCall(func_table_ref_dec_, {GetTablePtr(value)});
}

Value* IrEmitter::GetTablePtr(Value* value) {
  return builder_.CreateIntToPtr(ExtractValue(value),
                                 PointerType::getUnqual(table_type_));
}

Value* IrEmitter::Eval(const Constructor& constructor) {
  Value* table_ptr = builder_.CreateCall(func_table_new_);
  Value* value_ptr = LookupSymbol("_temp_" + to_string(temp_name_), true);
  ++temp_name_;
  builder_.CreateStore(builder_.getInt64(kSluaValueTable),
                       PointerToType(value_ptr));
  builder_.CreateStore(
      builder_.CreatePtrToInt(table_ptr, builder_.getInt64Ty()),
      PointerToValue(value_ptr));
  Value* value = builder_.CreateLoad(value_ptr);
  TableRefInc(value);

  for (const Field& field : constructor.fields) {
    TableArrayAppend(Eval(field), table_ptr);
  }

  return value;
}

void IrEmitter::TableArrayAppend(Value* value, Value* table_ptr) {
  Value* size_ptr = PointerToTableArraySize(table_ptr);
  Value* size = builder_.CreateLoad(size_ptr);
  Value* capacity_ptr = PointerToTableArrayCapacity(table_ptr);
  Value* capacity = builder_.CreateLoad(capacity_ptr);

  BasicBlock* grow_block = CreateBlock("array_append_grow");
  BasicBlock* post_block = CreateBlock("array_append_post");

  Value* cmp = builder_.CreateICmpEQ(size, capacity);
  builder_.CreateCondBr(cmp, grow_block, post_block);

  builder_.SetInsertPoint(grow_block);
  builder_.CreateCall(func_table_array_grow_, {table_ptr});
  builder_.CreateBr(post_block);

  builder_.SetInsertPoint(post_block);
  Value* array_ptr = builder_.CreateLoad(PointerToTableArray(table_ptr));
  Value* target_ptr = builder_.CreateInBoundsGEP(value_type_, array_ptr, size);
  builder_.CreateStore(value, target_ptr);

  size = builder_.CreateAdd(size, builder_.getInt64(1));
  builder_.CreateStore(size, size_ptr);

  BasicBlock* is_table_block = CreateBlock("array_append_is_table");
  post_block = CreateBlock("array_append_is_table_post");

  cmp = builder_.CreateICmpEQ(ExtractType(value),
                              builder_.getInt64(kSluaValueTable));
  builder_.CreateCondBr(cmp, is_table_block, post_block);

  builder_.SetInsertPoint(is_table_block);
  TableRefInc(value);
  builder_.CreateBr(post_block);

  builder_.SetInsertPoint(post_block);
}

Value* IrEmitter::Eval(const Field& field) {
  return visit([this](const auto& ref) { return Eval(ref.get()); },
               field.field);
}

Value* IrEmitter::Eval(const Index& in) {
  return builder_.CreateLoad(Addr(in));
}

Value* IrEmitter::Addr(const Index& in) {
  return builder_.CreateCall(func_table_access_, {Eval(in.lhs), Eval(in.rhs)});
}

Value* IrEmitter::PointerToTableArraySize(Value* table_ptr) {
  return builder_.CreateStructGEP(table_type_, table_ptr, 1);
}

Value* IrEmitter::PointerToTableArrayCapacity(Value* table_ptr) {
  return builder_.CreateStructGEP(table_type_, table_ptr, 2);
}

Value* IrEmitter::PointerToTableArray(Value* table_ptr) {
  return builder_.CreateStructGEP(table_type_, table_ptr, 3);
}

void IrEmitter::Emit(const FuncStat& func_stat) {
  Value* ptr = Addr(func_stat.name.name, false);
  Function* func = CreateFunc(ptr);
  Emit(func_stat.body, func);
}

void IrEmitter::Emit(const RetStat& ret_stat) {
  Value* ret_value;
  if (ret_stat.expr.has_value()) {
    ret_value = Eval(ret_stat.expr.value());
  } else {
    ret_value = ConstantStruct::get(
        value_type_, {builder_.getInt64(kSluaValueNil), builder_.getInt64(0)});
  }
  SymbolTable* table = symbol_table_.get();
  while (table->func == curr_func_) {
    EmitDestroyScope(*table);
    table = table->next.get();
  }
  builder_.CreateRet(ret_value);
  builder_.SetInsertPoint(CreateBlock("return_dummy"));
}

Value* IrEmitter::EvalLogic(Value* lhs, Value* rhs, LogicOp op) {
  Value* lhs_bool = ToBool(lhs);
  Value* rhs_bool = ToBool(rhs);
  Value* result_value;
  switch (op) {
    case LogicOp::kAnd:
      result_value = builder_.CreateAnd(lhs_bool, rhs_bool);
      break;
    case LogicOp::kOr:
      result_value = builder_.CreateOr(lhs_bool, rhs_bool);
      break;
    default:
      throw runtime_error{"Logical op not implemented"};
  }
  Value* result_ptr = builder_.CreateAlloca(value_type_);
  builder_.CreateStore(builder_.getInt64(kSluaValueBool),
                       PointerToType(result_ptr));
  builder_.CreateStore(builder_.CreateZExt(result_value, builder_.getInt64Ty()),
                       PointerToValue(result_ptr));
  return builder_.CreateLoad(result_ptr);
}

void IrEmitter::Emit(const FuncBody& body, Function* func) {
  BasicBlock* entry_block =
      BasicBlock::Create(builder_.getContext(), "entry", func);
  BasicBlock* outer_block = builder_.GetInsertBlock();
  builder_.SetInsertPoint(entry_block);

  Function* outer_func = curr_func_;
  curr_func_ = func;
  EnterScope();
  for (index i = 0; i < static_cast<index>(body.params.size()); ++i) {
    Value* ptr = Addr(body.params[i], true);
    Value* index = ConstantStruct::get(
        value_type_,
        {builder_.getInt64(kSluaValueInteger), builder_.getInt64(i + 1)});
    Value* source_ptr =
        builder_.CreateCall(func_table_access_, {func->arg_begin(), index});
    builder_.CreateStore(builder_.CreateLoad(source_ptr), ptr);
  }
  Emit(body.body);
  LeaveScope();
  builder_.CreateRet(ConstantStruct::get(
      value_type_, {builder_.getInt64(kSluaValueNil), builder_.getInt64(0)}));
  curr_func_ = outer_func;

  builder_.SetInsertPoint(outer_block);
}

void IrEmitter::Emit(const LocalStat& stat) {
  Value* addr = Addr(stat.name, true);
  Value* value = Eval(stat.expr);
  EmitAssignment(addr, value);
}

void IrEmitter::Emit(const LocalFunc& local_func) {
  Value* ptr = Addr(local_func.name, true);
  Function* func = CreateFunc(ptr);
  Emit(local_func.body, func);
}

Value* IrEmitter::Eval(const FuncExpr& func_expr) {
  Value* ptr = builder_.CreateAlloca(value_type_);
  Function* func = CreateFunc(ptr);
  Emit(func_expr.body, func);
  return builder_.CreateLoad(ptr);
}

Function* IrEmitter::CreateFunc(Value* ptr) {
  Function* func =
      Function::Create(FunctionType::get(value_type_, {value_type_}, false),
                       Function::ExternalLinkage, "", module_);
  builder_.CreateStore(builder_.getInt64(kSluaValueFunction),
                       PointerToType(ptr));
  builder_.CreateStore(builder_.CreateBitCast(func, builder_.getInt64Ty()),
                       PointerToValue(ptr));
  functions_.push_back(func);
  return func;
}

void IrEmitter::EmitBreak() {
  if (break_info_.post_block == nullptr || break_info_.scope == nullptr) {
    throw ParserException{"Error: cannot break here"};
  }
  SymbolTable* scope = symbol_table_.get();
  while (scope != break_info_.scope) {
    EmitDestroyScope(*scope);
    scope = scope->next.get();
  }
  builder_.CreateBr(break_info_.post_block);

  builder_.SetInsertPoint(CreateBlock("break_dummy"));
}
}  // namespace

void Emitter::EmitObjectFile(const string& filename) const {
  LLVMContext context;
  unique_ptr<Module> module = std::make_unique<Module>("slua_module", context);
  FunctionPassManager fpm{module.get()};
  PassManager pm;

  PassManagerBuilder pm_builder;
  pm_builder.OptLevel = 3;
  pm_builder.SizeLevel = 0;
  pm_builder.LoopVectorize = true;
  pm_builder.SLPVectorize = true;

  pm_builder.populateFunctionPassManager(fpm);
  pm_builder.populateModulePassManager(pm);

  fpm.doInitialization();

  Function* slua_main =
      Function::Create(FunctionType::get(Type::getVoidTy(context), {}, false),
                       Function::ExternalLinkage, "slua_main", module.get());

  IRBuilder<> builder{context};
  BasicBlock* entry_block = BasicBlock::Create(context, "entry", slua_main);
  builder.SetInsertPoint(entry_block);

  IrEmitter emitter{builder, module.get(), symbol_table_, slua_main};
  emitter.EnterScope();
  emitter.Emit(parser_.root());
  emitter.LeaveScope();
  builder.CreateRetVoid();

  for (Function* func : emitter.functions()) {
    if (verifyFunction(*func, &llvm::errs())) {
      func->print(llvm::errs());
      throw runtime_error{"Function verification failed"};
    }
  }
  if (verifyModule(*module, &llvm::errs())) {
    module->print(llvm::errs(), nullptr);
    throw runtime_error{"Module verification failed"};
  }

  for (Function* func : emitter.functions()) {
    fpm.run(*func);
  }

  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  string target_triple = getDefaultTargetTriple();
  module->setTargetTriple(target_triple);
  string error;
  const Target* target = TargetRegistry::lookupTarget(target_triple, error);
  if (target == nullptr) {
    llvm::errs() << error << '\n';
    throw runtime_error{error};
  }

  TargetOptions opt;
  TargetMachine* target_machine = target->createTargetMachine(
      target_triple, "generic", "", opt, llvm::Optional<llvm::Reloc::Model>{});
  module->setDataLayout(target_machine->createDataLayout());

  error_code ec;
  raw_fd_ostream dest{filename, ec, OpenFlags::F_None};
  if (ec) {
    throw runtime_error{"Cannot open file: " + filename};
  }

  if (target_machine->addPassesToEmitFile(pm, dest,
                                          TargetMachine::CGFT_ObjectFile)) {
    throw runtime_error{"Target machine cannot emit file"};
  }

  pm.run(*module);
}
}  // namespace slua
