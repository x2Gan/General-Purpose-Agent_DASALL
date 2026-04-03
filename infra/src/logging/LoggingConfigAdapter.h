#pragma once

#include <optional>

#include "config/IConfigCenter.h"
#include "logging/ILogConfigurator.h"

namespace dasall::infra::logging {

class LoggingConfigAdapter final : public ILogConfigurator {
 public:
  explicit LoggingConfigAdapter(const config::IConfigCenter& config_center);

  [[nodiscard]] LoggingConfigApplyResult load_and_apply();
  [[nodiscard]] LoggingConfigApplyResult apply(const LoggingConfig& config) override;

  [[nodiscard]] bool has_active_config() const {
    return active_config_.has_value();
  }

  [[nodiscard]] const LoggingConfig& active_config() const {
    return *active_config_;
  }

 private:
  const config::IConfigCenter* config_center_ = nullptr;
  std::optional<LoggingConfig> active_config_;
};

}  // namespace dasall::infra::logging