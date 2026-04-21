#pragma once

#include <string_view>
#include <vector>

namespace dasall::knowledge::retrieve {

class IQueryEncoder {
 public:
  virtual ~IQueryEncoder() = default;

  [[nodiscard]] virtual std::vector<float> encode(std::string_view query_text) const = 0;
  [[nodiscard]] virtual bool available() const = 0;
};

}  // namespace dasall::knowledge::retrieve