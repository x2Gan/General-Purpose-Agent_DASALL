#pragma once

#include <string>

namespace dasall::llm {

struct HealthStatus {
  bool ready = false;
  bool degraded = false;
  std::string message;

  [[nodiscard]] bool is_healthy() const {
    return ready && !degraded;
  }
};

}  // namespace dasall::llm
