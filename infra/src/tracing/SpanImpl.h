#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "tracing/ISpan.h"

namespace dasall::infra::tracing {

class SpanImpl final : public ISpan {
 public:
  SpanImpl(SpanDescriptor descriptor,
           TraceContext context,
           TraceContext parent_context,
           SamplingDecision sampling_decision);

  void set_attribute(std::string_view key, const TraceAttributeValue& value) override;
  void add_event(std::string_view name, const TraceAttributeMap& attrs) override;
  void set_status(SpanStatusCode code, std::string_view message) override;
  [[nodiscard]] SpanEndResult end(
      std::optional<std::int64_t> end_ts_unix_ms = std::nullopt) override;
  [[nodiscard]] TraceContext get_context() const override;

  [[nodiscard]] const TraceContext& parent_context() const;
  [[nodiscard]] bool has_ended() const;
  [[nodiscard]] std::size_t accepted_attribute_count() const;
  [[nodiscard]] std::size_t accepted_event_count() const;
  [[nodiscard]] SpanStatusCode status_code() const;
  [[nodiscard]] const std::string& status_message() const;
  [[nodiscard]] const SamplingDecision& sampling_decision() const;
  [[nodiscard]] bool is_recording() const;
  [[nodiscard]] bool is_sampled() const;
  [[nodiscard]] const TraceAttributeMap& attributes() const;
  [[nodiscard]] const SpanEndResult& end_result() const;

 private:
  SpanDescriptor descriptor_;
  TraceContext context_;
  TraceContext parent_context_;
  SamplingDecision sampling_decision_;
  bool recording_ = true;
  bool ended_ = false;
  TraceAttributeMap attrs_;
  std::size_t event_count_ = 0;
  SpanStatusCode status_code_ = SpanStatusCode::Unset;
  std::string status_message_;
  std::uint32_t dropped_attr_count_ = 0;
  SpanEndResult end_result_{};
};

}  // namespace dasall::infra::tracing