#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>

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
using std::int64_t;
using std::list;
using std::memcpy;
using std::size_t;
using std::strcmp;
using std::string;
using std::strlen;
using std::terminate;
using std::to_string;
using std::unordered_map;
using std::vector;

using gsl::make_span;
using gsl::span;

using TableHash = unordered_map<SluaValue, SluaValue>;
}  // namespace

extern "C" {
[[noreturn]] void slua_runtime_error(const char* message) noexcept {
  cerr << message << '\n';
  terminate();
}
}  // extern "C"

namespace {
unordered_map<string, function<SluaValue(vector<SluaValue>&)>>
    builtin_functions;

list<SluaTable> tables;
unordered_map<SluaTable*, list<SluaTable>::iterator> table_iters;

string ToString(SluaValue val) {
  switch (val.type) {
    case kSluaValueNil:
      return "nil";
    case kSluaValueBool:
      return (val.value.bool_val != 0 ? "true" : "false");
    case kSluaValueInteger:
      return to_string(val.value.int_val);
    case kSluaValueFloat:
      return to_string(val.value.float_val);
    case kSluaValueString:
      return val.value.str_val;
    default:
      return "<Unrecognized value>";
  }
}

char* NewString(const string& str) noexcept {
  char* storage = new char[str.size() + 1];
  memcpy(storage, str.c_str(), str.size() + 1);
  return storage;
}
}  // namespace

namespace std {
template <>
struct hash<SluaValue> {
  using argument_type = SluaValue;
  using result_type = size_t;

  result_type operator()(const argument_type& value) const noexcept {
    switch (value.type) {
      case kSluaValueBool:
        return hash<int>{}(value.value.bool_val);
      case kSluaValueInteger:
        return hash<int64_t>{}(value.value.int_val);
      case kSluaValueFloat:
        return hash<double>{}(value.value.float_val);
      case kSluaValueString:
        return hash<string>{}(value.value.str_val);
      case kSluaValueTable:
      case kSluaValueFunction:
      case kSluaValueBuiltinFunction:
        return hash<void*>{}(value.value.address);
      default:
        slua_runtime_error("Cannot hash value");
    }
  }
};
}  // namespace std

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
    cout << ToString(value);
  }
  cout << '\n';
  return SluaValue{kSluaValueNil, {0}};
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

SluaValue slua_mod(SluaValue, SluaValue) noexcept {
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

SluaValue slua_concat(SluaValue lhs, SluaValue rhs) noexcept {
  string str = ToString(lhs) + ToString(rhs);
  SluaValue result{};
  result.type = kSluaValueString;
  result.value.str_val = NewString(str);
  return result;
}

SluaValue slua_len(SluaValue value) noexcept {
  if (value.type != kSluaValueTable) {
    slua_runtime_error("Error: cannot get length of value");
  }
  const SluaTable& table = *static_cast<const SluaTable*>(value.value.address);
  const TableHash& hash = *static_cast<const TableHash*>(table.hash_ptr);
  SluaValue result{};
  result.type = kSluaValueInteger;
  result.value.int_val = table.array_size - 1 + hash.size();
  return result;
}

SluaTable* slua_table_new() noexcept {
  tables.emplace_front();
  SluaTable* ptr = &tables.front();
  table_iters[ptr] = tables.begin();
  ptr->array_ptr = new SluaValue[2]{};
  ptr->array_capacity = 2;
  ptr->array_size = 1;
  ptr->hash_ptr = new TableHash{};
#ifndef NDEBUG
  cout << "Table allocated: " << ptr << '\n';
#endif
  return ptr;
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
  if (lhs.type != kSluaValueTable || rhs.type == kSluaValueNil) {
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

  TableHash& table_hash = *static_cast<TableHash*>(table->hash_ptr);
  return &table_hash[rhs];
}
}  // extern "C"

bool operator==(const SluaValue& lhs, const SluaValue& rhs) noexcept {
  if (lhs.type != rhs.type) {
    return false;
  }
  switch (lhs.type) {
    case kSluaValueNil:
      return true;
    case kSluaValueBool:
      return lhs.value.bool_val == rhs.value.bool_val;
    case kSluaValueInteger:
      return lhs.value.int_val == rhs.value.int_val;
    case kSluaValueFloat:
      return lhs.value.float_val == rhs.value.float_val;
    case kSluaValueString:
      return strcmp(lhs.value.str_val, rhs.value.str_val) == 0;
    case kSluaValueTable:
    case kSluaValueFunction:
    case kSluaValueBuiltinFunction:
      return lhs.value.address == rhs.value.address;
    default:
      slua_runtime_error("Cannot compare values");
  }
}

int main() {
  builtin_functions.emplace("print", &slua_print);
  slua_main();
  return 0;
}
