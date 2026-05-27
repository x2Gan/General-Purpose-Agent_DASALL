#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "config/IConfigCenter.h"
#include "logging/ILogConfigurator.h"

namespace dasall::infra::logging {

class LoggingConfigAdapter final : public ILogConfigurator {
 public:
  explicit LoggingConfigAdapter(const config::IConfigCenter& config_center);

  [[nodiscard]] static bool parse_uint32_value(std::string_view serialized_value,
                                               std::uint32_t& parsed_value);

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