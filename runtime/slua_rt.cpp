#include <cstdarg>
#include <cstdint>

#include <algorithm>
#include <exception>
#include <functional>
#include <iostream>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include <gsl/gsl>

extern "C" {
#include "slua.h"
}

namespace {
using std::cerr;
using std::copy;
using std::cout;
using std::function;
using std::list;
using std::string;
using std::terminate;
using std::unordered_map;
using std::vector;

using gsl::make_span;
using gsl::span;

unordered_map<string, function<SluaValue(vector<SluaValue>&)>>
    builtin_functions;

list<SluaTable> tables;
unordered_map<SluaTable*, list<SluaTable>::iterator> table_iters;
}  // namespace

extern "C" {
void slua_main();

SluaValue slua_print(vector<SluaValue>& args) noexcept {
  bool first = true;
  for (SluaValue& value : args) {
    if (first) {
      first = false;
    } else {
      cout << ' ';
    }
    switch (value.type) {
      case kSluaValueNil:
        cout << "nil";
        break;
      case kSluaValueBool:
        cout << (value.value.bool_val != 0 ? "true" : "false");
        break;
      case kSluaValueInteger:
        cout << value.value.int_val;
        break;
      case kSluaValueFloat:
        cout << value.value.float_val;
        break;
      default:
        cout << "Unrecognized value";
        break;
    }
  }
  cout << '\n';
  return SluaValue{kSluaValueNil, {0}};
}

[[noreturn]] void slua_runtime_error(const char* message) noexcept {
  cerr << message << '\n';
  terminate();
}

SluaValue slua_invoke_builtin(const char* name, int count, ...) noexcept {
  auto iter = builtin_functions.find(name);
  if (iter == builtin_functions.end()) {
    using namespace std::string_literals;
    slua_runtime_error(
        ("Error: builtin function "s + name + " not found").c_str());
  }
  va_list args;
  va_start(args, count);
  vector<SluaValue> arg_list;
  arg_list.reserve(count);
  for (int i = 0; i < count; ++i) {
    arg_list.push_back(va_arg(args, SluaValue));
  }
  return iter->second(arg_list);
}

SluaValue slua_add(SluaValue, SluaValue) noexcept {
  slua_runtime_error("Not implemented");
}

SluaValue slua_sub(SluaValue, SluaValue) noexcept {
  slua_runtime_error("Not implemented");
}

SluaValue slua_mul(SluaValue, SluaValue) noexcept {
  slua_runtime_error("Not implemented");
}

SluaValue slua_div(SluaValue, SluaValue) noexcept {
  slua_runtime_error("Not implemented");
}

SluaValue slua_idiv(SluaValue, SluaValue) noexcept {
  slua_runtime_error("Not implemented");
}

SluaValue slua_lt(SluaValue, SluaValue) noexcept {
  slua_runtime_error("Not implemented");
}

SluaValue slua_le(SluaValue, SluaValue) noexcept {
  slua_runtime_error("Not implemented");
}

SluaValue slua_gt(SluaValue, SluaValue) noexcept {
  slua_runtime_error("Not implemented");
}

SluaValue slua_ge(SluaValue, SluaValue) noexcept {
  slua_runtime_error("Not implemented");
}

SluaValue slua_eq(SluaValue, SluaValue) noexcept {
  slua_runtime_error("Not implemented");
}

SluaValue slua_len(SluaValue value) noexcept {
  if (value.type != kSluaValueTable) {
    slua_runtime_error("Error: cannot get length of value");
  }
  SluaValue result{};
  result.type = kSluaValueInteger;
  result.value.int_val =
      static_cast<SluaTable*>(value.value.address)->array_size - 1;
  return result;
}

SluaTable* slua_table_new() noexcept {
  tables.emplace_front();
  SluaTable* ptr = &tables.front();
  table_iters[ptr] = tables.begin();
  ptr->array_ptr = new SluaValue[2]{};
  ptr->array_capacity = 2;
  ptr->array_size = 1;
#ifndef NDEBUG
  cout << "Table allocated: " << ptr << '\n';
#endif
  return ptr;
}

void slua_table_ref_inc(SluaTable* table) noexcept {
  ++table->ref_count;
#ifndef NDEBUG
  cout << "Table " << table << " ref_count increased to: " << table->ref_count
       << '\n';
#endif
}

void slua_table_ref_dec(SluaTable* table) noexcept {
  --table->ref_count;
#ifndef NDEBUG
  cout << "Table " << table << " ref_count decreased to: " << table->ref_count
       << '\n';
#endif
  if (table->ref_count == 0) {
#ifndef NDEBUG
    cout << "Table deallocated: " << table << '\n';
#endif
    for (SluaValue& value : make_span(table->array_ptr, table->array_size)) {
      if (value.type == kSluaValueTable) {
        slua_table_ref_dec(static_cast<SluaTable*>(value.value.address));
      }
    }
    delete[] table->array_ptr;
    auto iter = table_iters.find(table);
    tables.erase(iter->second);
    table_iters.erase(iter);
  }
}

void slua_table_array_grow(SluaTable* table) noexcept {
  span<SluaValue> values{table->array_ptr, table->array_size};
  if (table->array_capacity == 0) {
    table->array_ptr = new SluaValue[2];
    table->array_capacity = 2;
  } else {
    table->array_capacity *= 2;
    table->array_ptr = new SluaValue[table->array_capacity];
  }
  copy(values.begin(), values.end(), table->array_ptr);
  delete[] values.data();
#ifndef NDEBUG
  cout << "Table array grown: " << table << '\n';
#endif
}

SluaValue* slua_table_access(SluaValue lhs, SluaValue rhs) noexcept {
  if (lhs.type != kSluaValueTable || rhs.type != kSluaValueInteger) {
    slua_runtime_error("Error: cannot perform table access");
  }
  using gsl::index;
  index i = rhs.value.int_val;
  SluaTable* table = static_cast<SluaTable*>(lhs.value.address);
  if (i == table->array_capacity) {
    slua_table_array_grow(table);
  }
  if (i < table->array_capacity) {
    while (table->array_size <= i) {
      table->array_ptr[table->array_size] = {};
      ++table->array_size;
    }
    return &table->array_ptr[i];
  }
  slua_runtime_error("Hash access not implemented");
}
}

int main() {
  builtin_functions.emplace("print", &slua_print);
  slua_main();
  return 0;
}
