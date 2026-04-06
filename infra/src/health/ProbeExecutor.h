#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "health/ProbeRegistry.h"

namespace dasall::infra {

struct ProbeExecutorOptions {
  std::size_t unhealthy_consecutive_failures = 3;
};

class ProbeExecutor {
 public:
  explicit ProbeExecutor(ProbeRegistry& registry,
                         ProbeExecutorOptions options = {});

  [[nodiscard]] ProbeResult execute_once(const ProbeDescriptor& descriptor);
  [[nodiscard]] std::vector<ProbeResult> execute_batch(std::string_view group);
  [[nodiscard]] std::size_t consecutive_failure_count(
      std::string_view probe_name) const;

 private:
  [[nodiscard]] ProbeResult make_missing_probe_result(
      const ProbeDescriptor& descriptor) const;
  [[nodiscard]] ProbeResult make_timeout_result(const ProbeDescriptor& descriptor,
                                                std::int64_t latency_ms);
  [[nodiscard]] ProbeResult make_exception_result(const ProbeDescriptor& descriptor,
                                                  std::string detail_suffix);
  [[nodiscard]] ProbeResult normalize_result(const ProbeDescriptor& descriptor,
                                             ProbeResult result,
                                             std::int64_t latency_ms);
  [[nodiscard]] ProbeStatus resolve_failure_status(std::string_view probe_name) const;

  ProbeRegistry& registry_;
  ProbeExecutorOptions options_;
  std::map<std::string, std::size_t> consecutive_failures_;
};

}  // namespace dasall::infra