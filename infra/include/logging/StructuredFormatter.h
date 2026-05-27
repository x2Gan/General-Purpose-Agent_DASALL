#pragma once

#include <string_view>

#include "logging/ILogConfigurator.h"
#include "logging/LogTypes.h"

namespace dasall::infra::logging {

struct StructuredFormatterOptions {
  LoggingFormat format = LoggingFormat::JsonLine;
};

class StructuredFormatter {
 public:
  static constexpr std::string_view kSchemaVersion =
      "dasall.logging.event.v1";

  explicit StructuredFormatter(StructuredFormatterOptions options = {});

  void set_options(StructuredFormatterOptions options);

  [[nodiscard]] LoggingFormat format_type() const {
    return options_.format;
  }

  [[nodiscard]] LogEvent format(const LogEvent& event) const;

 private:
  StructuredFormatterOptions options_{};
};

}  // namespace dasall::infra::logging