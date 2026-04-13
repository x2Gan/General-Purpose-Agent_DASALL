#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../../infra/include/logging/ILogger.h"
#include "../../../infra/include/metrics/IMeter.h"
#include "../../../infra/include/metrics/IMetricsProvider.h"
#include "../../../infra/include/tracing/ISpan.h"
#include "../../../infra/include/tracing/ITracer.h"
#include "../../../llm/src/LLMManager.h"
#include "../../../llm/src/route/AdapterRegistry.h"
#include "../../mocks/include/MockLLMAdapter.h"

namespace dasall::tests::integration::llm_support {

inline constexpr std::string_view kPromptAssetRoot = "/home/gangan/DASALL/llm/assets/prompts";

class RecordingLogger final : public dasall::infra::logging::ILogger {
 public:
  dasall::infra::logging::LogWriteResult log(
      const dasall::infra::logging::LogEvent& event) override {
    events.push_back(event);
    return dasall::infra::logging::LogWriteResult::success();
  }

  dasall::infra::logging::LogWriteResult flush(
      const dasall::infra::logging::LogFlushDeadline&) override {
    return dasall::infra::logging::LogWriteResult::success();
  }

  void set_level(dasall::infra::logging::LogLevel level) override {
    last_level = level;
  }

  std::vector<dasall::infra::logging::LogEvent> events;
  dasall::infra::logging::LogLevel last_level =
      dasall::infra::logging::LogLevel::Info;
};

class RecordingMeter final : public dasall::infra::metrics::IMeter {
 public:
  std::optional<dasall::infra::metrics::InstrumentHandle> create_counter(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":counter",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_gauge(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":gauge",
    };
  }

  std::optional<dasall::infra::metrics::InstrumentHandle> create_histogram(
      const dasall::infra::metrics::MetricIdentity& identity) override {
    created_identities.push_back(identity);
    return dasall::infra::metrics::InstrumentHandle{
        .instrument_key = identity.name + ":histogram",
    };
  }

  dasall::infra::metrics::MetricsOperationStatus record(
      const dasall::infra::metrics::MetricSample& sample) override {
    recorded_samples.push_back(sample);
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://llm/integration");
  }

  std::vector<dasall::infra::metrics::MetricIdentity> created_identities;
  std::vector<dasall::infra::metrics::MetricSample> recorded_samples;
};

class RecordingMetricsProvider final
    : public dasall::infra::metrics::IMetricsProvider {
 public:
  explicit RecordingMetricsProvider(std::shared_ptr<RecordingMeter> meter)
      : meter_(std::move(meter)) {}

  dasall::infra::metrics::MetricsOperationStatus init(
      const dasall::infra::metrics::MetricsProviderConfig&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://llm/provider-init");
  }

  std::shared_ptr<dasall::infra::metrics::IMeter> get_meter(
      const dasall::infra::metrics::MeterScope& scope) override {
    last_scope = scope;
    return meter_;
  }

  dasall::infra::metrics::MetricsOperationStatus force_flush(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://llm/provider-flush");
  }

  dasall::infra::metrics::MetricsOperationStatus shutdown(
      const dasall::infra::metrics::MetricsCallDeadline&) override {
    return dasall::infra::metrics::MetricsOperationStatus::success(
        "metrics://llm/provider-shutdown");
  }

  dasall::infra::metrics::MeterScope last_scope{};

 private:
  std::shared_ptr<RecordingMeter> meter_;
};

inline std::string hex_id(std::uint64_t value, std::size_t width) {
  std::ostringstream builder;
  builder << std::hex << std::nouppercase << std::setfill('0')
          << std::setw(static_cast<int>(width)) << value;
  auto encoded = builder.str();
  if (encoded.size() > width) {
    encoded = encoded.substr(encoded.size() - width);
  }
  if (encoded.size() < width) {
    encoded.insert(encoded.begin(), width - encoded.size(), '0');
  }
  if (std::all_of(encoded.begin(), encoded.end(), [](const char ch) {
        return ch == '0';
      })) {
    encoded.back() = '1';
  }
  return encoded;
}

class RecordingSpan final : public dasall::infra::tracing::ISpan {
 public:
  explicit RecordingSpan(dasall::infra::tracing::TraceContext context)
      : context_(std::move(context)) {}

  void set_attribute(std::string_view key,
                     const dasall::infra::tracing::TraceAttributeValue& value) override {
    attributes[std::string(key)] = value;
  }

  void add_event(std::string_view,
                 const dasall::infra::tracing::TraceAttributeMap&) override {}

  void set_status(dasall::infra::tracing::SpanStatusCode code,
                  std::string_view message) override {
    status_code = code;
    status_message = std::string(message);
  }

  dasall::infra::tracing::SpanEndResult end(
      std::optional<std::int64_t> end_ts_unix_ms = std::nullopt) override {
    return dasall::infra::tracing::SpanEndResult{
        .end_ts_unix_ms = end_ts_unix_ms,
        .status_code = status_code,
        .status_message = status_message,
        .dropped_attr_count = 0U,
    };
  }

  dasall::infra::tracing::TraceContext get_context() const override {
    return context_;
  }

  dasall::infra::tracing::TraceContext context_;
  dasall::infra::tracing::TraceAttributeMap attributes;
  dasall::infra::tracing::SpanStatusCode status_code =
      dasall::infra::tracing::SpanStatusCode::Unset;
  std::string status_message;
};

struct StartedSpanRecord {
  dasall::infra::tracing::SpanDescriptor descriptor;
  std::shared_ptr<RecordingSpan> span;
};

class RecordingTracer final : public dasall::infra::tracing::ITracer {
 public:
  std::shared_ptr<dasall::infra::tracing::ISpan> start_span(
      const dasall::infra::tracing::SpanDescriptor& descriptor,
      const dasall::infra::tracing::TraceContext*) override {
    auto span = std::make_shared<RecordingSpan>(
        dasall::infra::tracing::TraceContext{
            .trace_id = hex_id(++trace_seed_,
                               dasall::infra::tracing::kTraceIdHexLength),
            .span_id = hex_id(++span_seed_,
                              dasall::infra::tracing::kSpanIdHexLength),
            .trace_flags = 0x01U,
            .trace_state = std::string(),
            .parent_span_id = std::string(),
            .state = dasall::infra::tracing::TraceContextState::Active,
            .is_remote = false,
        });
    started_spans.push_back(StartedSpanRecord{
        .descriptor = descriptor,
        .span = span,
    });
    return span;
  }

  void with_active_span(
      const std::shared_ptr<dasall::infra::tracing::ISpan>&,
      const dasall::infra::tracing::ActiveSpanCallback& fn) override {
    if (fn) {
      fn();
    }
  }

  dasall::infra::tracing::TraceContext current_context() const override {
    return dasall::infra::tracing::TraceContext::noop();
  }

  std::vector<StartedSpanRecord> started_spans;

 private:
  std::uint64_t trace_seed_ = 0U;
  std::uint64_t span_seed_ = 0U;
};

inline const std::string* find_log_attr(
    const dasall::infra::logging::LogEvent& event,
    std::string_view key) {
  const auto it = event.attrs.find(std::string(key));
  if (it == event.attrs.end()) {
    return nullptr;
  }

  return &it->second;
}

inline const dasall::infra::metrics::MetricSample* find_sample(
    const std::vector<dasall::infra::metrics::MetricSample>& samples,
    std::string_view identity_name) {
  const auto it = std::find_if(samples.begin(), samples.end(), [&](const auto& sample) {
    return sample.identity_ref.name == identity_name;
  });
  if (it == samples.end()) {
    return nullptr;
  }

  return &*it;
}

inline std::optional<std::string> trace_attr_as_string(
    const dasall::infra::tracing::TraceAttributeMap& attrs,
    std::string_view key) {
  const auto* attr = dasall::infra::tracing::find_trace_attribute(attrs, key);
  if (attr == nullptr) {
    return std::nullopt;
  }

  if (const auto* value = std::get_if<std::string>(attr)) {
    return *value;
  }

  return std::nullopt;
}

inline std::optional<std::uint64_t> trace_attr_as_uint64(
    const dasall::infra::tracing::TraceAttributeMap& attrs,
    std::string_view key) {
  const auto* attr = dasall::infra::tracing::find_trace_attribute(attrs, key);
  if (attr == nullptr) {
    return std::nullopt;
  }

  if (const auto* value = std::get_if<std::uint64_t>(attr)) {
    return *value;
  }

  return std::nullopt;
}

inline bool has_result_tag(const dasall::llm::LLMManagerResult& result,
                           const std::string& expected_tag) {
  return result.response.has_value() && result.response->tags.has_value() &&
         std::find(result.response->tags->begin(),
                   result.response->tags->end(),
                   expected_tag) != result.response->tags->end();
}

inline bool has_result_tag_prefix(const dasall::llm::LLMManagerResult& result,
                                  const std::string& prefix) {
  return result.response.has_value() && result.response->tags.has_value() &&
         std::find_if(result.response->tags->begin(),
                      result.response->tags->end(),
                      [&](const std::string& tag) {
                        return tag.rfind(prefix, 0U) == 0U;
                      }) != result.response->tags->end();
}

inline dasall::llm::route::AdapterRegistration make_registration(
    std::string provider_id,
    std::string model_id,
    std::string adapter_id,
    std::string deployment_type,
    std::vector<std::string> capability_tags,
    std::shared_ptr<dasall::tests::mocks::MockLLMAdapter> adapter) {
  return dasall::llm::route::AdapterRegistration{
      .provider_id = std::move(provider_id),
      .model_id = std::move(model_id),
      .adapter_id = std::move(adapter_id),
      .deployment_type = std::move(deployment_type),
      .capability_tags = std::move(capability_tags),
      .supports_streaming = false,
      .adapter = std::move(adapter),
  };
}

}  // namespace dasall::tests::integration::llm_support