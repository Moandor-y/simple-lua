#ifndef SLUA_H_
#define SLUA_H_

#include <stdint.h>

enum SluaValueType : int64_t {
  kSluaValueNil,
  kSluaValueBool,
  kSluaValueInteger,
  kSluaValueFloat,
  kSluaValueString,
  kSluaValueTable,
  kSluaValueFunction,
  kSluaValueBuiltinFunction,
};

struct SluaValue {
  SluaValueType type;
  union {
    int64_t int_val;
    double float_val;
    int bool_val;
    const char* str_val;
    void* address;
  } value;
};

struct SluaTable {
  int64_t array_size;
  int64_t array_capacity;
  SluaValue* array_ptr;
  void* hash_ptr;
};

#endif  // SLUA_H_
