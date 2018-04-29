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
      case kSluaValueString:
        cout << value.value.str_val;
        break;
      default:
        cout << "Unrecognized value";
        break;
    }
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
  ptr->hash_ptr = new TableHash{};
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
    TableHash& table_hash = *static_cast<TableHash*>(table->hash_ptr);
    for (auto& elem : table_hash) {
      SluaValue& value = elem.second;
      if (value.type == kSluaValueTable) {
        slua_table_ref_dec(static_cast<SluaTable*>(value.value.address));
      }
    }
    delete[] table->array_ptr;
    delete &table_hash;
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

char* slua_string_new(const char* str) noexcept {
  using gsl::index;
  index len = strlen(str);
  unsigned char* storage = new unsigned char[len + sizeof(int64_t) + 1];
  int64_t count = 1;
  memcpy(storage, &count, sizeof(int64_t));
#ifndef NDEBUG
  cout << "String allocated: " << static_cast<const void*>(storage) << '\n';
#endif
  return static_cast<char*>(static_cast<void*>(storage + sizeof(int64_t)));
}

void slua_string_ref_inc(char* str) noexcept {
  unsigned char* storage =
      static_cast<unsigned char*>(static_cast<void*>(str)) - sizeof(int64_t);
  int64_t count;
  memcpy(&count, storage, sizeof(int64_t));
  ++count;
  memcpy(storage, &count, sizeof(int64_t));
#ifndef NDEBUG
  cout << "String " << static_cast<const void*>(storage)
       << " ref count increased to: " << count << '\n';
#endif
}

void slua_string_ref_dec(char* str) noexcept {
  unsigned char* storage =
      static_cast<unsigned char*>(static_cast<void*>(str)) - sizeof(int64_t);
  int64_t count;
  memcpy(&count, storage, sizeof(int64_t));
  --count;
  memcpy(storage, &count, sizeof(int64_t));
#ifndef NDEBUG
  cout << "String " << static_cast<const void*>(storage)
       << " ref count decreased to: " << count << '\n';
#endif
  if (count == 0) {
#ifndef NDEBUG
    cout << "String deallocated: " << static_cast<const void*>(storage) << '\n';
#endif
    delete[] storage;
  }
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
