#pragma once

#include <optional>
#include <string>

#include "ServiceTypes.h"

namespace dasall::services::internal {

struct ContextNormalizationResult {
  std::optional<ServiceCallContext> context;
  std::string error;

  [[nodiscard]] bool ok() const { return context.has_value(); }
};

class ServiceContextBuilder {
 public:
  [[nodiscard]] ContextNormalizationResult normalize_context(
      const ServiceCallContext& candidate) const;
};

}  // namespace dasall::services::internal