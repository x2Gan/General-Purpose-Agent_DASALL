#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "LLMSubsystemConfig.h"
#include "route/ModelSelectionHint.h"
#include "route/ResolvedModelRoute.h"

#include "../provider/ProviderCatalogRepository.h"

namespace dasall::llm::route {

struct ModelRouterHealthState {
  std::string provider_id;
  std::string model_id;
  bool blocked = false;
  std::uint32_t consecutive_failures = 0U;

  [[nodiscard]] std::string route_key() const;
};

struct ModelRouterHealthSnapshot {
  std::vector<ModelRouterHealthState> route_states;

  [[nodiscard]] const ModelRouterHealthState* find_route_state(std::string_view provider_id,
                                                               std::string_view model_id) const;
  [[nodiscard]] bool route_is_blocked(std::string_view provider_id,
                                      std::string_view model_id) const;
  [[nodiscard]] std::uint32_t consecutive_failures_for(std::string_view provider_id,
                                                       std::string_view model_id) const;
};

struct ModelRouterResolveResult {
  std::optional<ResolvedModelRoute> resolved_route;
  std::vector<std::string> selection_reason_codes;

  [[nodiscard]] bool has_route() const {
    return resolved_route.has_value();
  }
};

class ModelRouter {
 public:
  bool init(const LLMSubsystemConfig& config);

  [[nodiscard]] ModelRouterResolveResult resolve(
      const ModelSelectionHint& selection_hint,
      const provider::ProviderCatalogSnapshot& catalog_snapshot,
      const ModelRouterHealthSnapshot& health_snapshot = {}) const;

 private:
  LLMSubsystemConfig config_;
  bool initialized_ = false;
};

}  // namespace dasall::llm::route