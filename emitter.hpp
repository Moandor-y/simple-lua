#ifndef SLUA_EMITTER_HPP_
#define SLUA_EMITTER_HPP_

#include <string>
#include <vector>

#include "parser.hpp"

namespace slua {
class Emitter {
 public:
  Emitter(const Parser& parser, const std::vector<std::string>& symbol_table)
      : parser_{parser}, symbol_table_{symbol_table} {}
  void EmitObjectFile(const std::string& filename) const;

 private:
  const Parser& parser_;
  const std::vector<std::string>& symbol_table_;
};
}  // namespace slua

#endif  // SLUA_EMITTER_HPP_
