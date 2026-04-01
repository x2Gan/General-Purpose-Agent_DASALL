#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "tracing/TraceTypes.h"

namespace dasall::infra::tracing {

class ISpan {
 public:
  virtual ~ISpan() = default;

  virtual void set_attribute(std::string_view key, const TraceAttributeValue& value) = 0;
  virtual void add_event(std::string_view name, const TraceAttributeMap& attrs) = 0;
  virtual void set_status(SpanStatusCode code, std::string_view message) = 0;
  [[nodiscard]] virtual SpanEndResult end(
      std::optional<std::int64_t> end_ts_unix_ms = std::nullopt) = 0;
  [[nodiscard]] virtual TraceContext get_context() const = 0;
};

}  // namespace dasall::infra::tracing
