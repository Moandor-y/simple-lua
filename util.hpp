#ifndef SLUA_UTIL_HPP_
#define SLUA_UTIL_HPP_

#include <type_traits>

namespace slua {
template <typename... Ts>
struct Overloaded : Ts... {
  using Ts::operator()...;
};

template <typename... Ts>
Overloaded(Ts...)->Overloaded<Ts...>;

template <typename T>
struct FalseType : std::false_type {};
}  // namespace slua

#endif  // SLUA_UTIL_HPP_
