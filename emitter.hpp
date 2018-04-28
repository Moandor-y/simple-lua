#ifndef SLUA_EMITTER_HPP_
#define SLUA_EMITTER_HPP_

#include <string>
#include <vector>

#include "parser.hpp"

namespace slua {
class Emitter {
 public:
  Emitter(const Parser& parser, const std::vector<std::string>& symbols,
          const std::vector<std::string>& str_literals)
      : parser_{parser}, symbols_{symbols}, str_literals_{str_literals} {}
  void EmitObjectFile(const std::string& filename) const;

 private:
  const Parser& parser_;
  const std::vector<std::string>& symbols_;
  const std::vector<std::string>& str_literals_;
};
}  // namespace slua

#endif  // SLUA_EMITTER_HPP_
