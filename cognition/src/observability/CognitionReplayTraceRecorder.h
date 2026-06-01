#pragma once

#include <cstdint>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "observability/CognitionTelemetry.h"

namespace dasall::cognition::observability {

struct ReplayFailureSampleRule {
  bool enabled = false;
  double sample_rate = 1.0;
};

struct ReplayFailureSamplingConfig {
  ReplayFailureSampleRule cognition_schema_violation;
  ReplayFailureSampleRule reflection_abort_safe;
  ReplayFailureSampleRule response_fallback_used;
};

struct ReplayTraceRecorderConfig {
  std::string output_dir;
  std::string enabled_profile_id = "build-ci/replay";
  ReplayFailureSamplingConfig failure_sampling;
};

[[nodiscard]] inline constexpr double clamp_replay_sample_rate(const double sample_rate) {
  return sample_rate <= 0.0 ? 0.0 : (sample_rate >= 1.0 ? 1.0 : sample_rate);
}

[[nodiscard]] inline std::uint64_t replay_sampling_hash(std::string_view value) {
  constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;

  std::uint64_t hash = kOffsetBasis;
  for (const unsigned char ch : value) {
    hash ^= static_cast<std::uint64_t>(ch);
    hash *= kPrime;
  }
  return hash;
}

[[nodiscard]] inline bool should_sample_replay_failure(
    const ReplayFailureSampleRule& rule,
    std::string_view sampling_key) {
  if (!rule.enabled) {
    return false;
  }

  const auto sample_rate = clamp_replay_sample_rate(rule.sample_rate);
  if (sample_rate <= 0.0) {
    return false;
  }
  if (sample_rate >= 1.0) {
    return true;
  }

  const long double bucket = static_cast<long double>(replay_sampling_hash(sampling_key));
  const long double upper_bound =
      static_cast<long double>(std::numeric_limits<std::uint64_t>::max());
  return (bucket / upper_bound) < static_cast<long double>(sample_rate);
}

[[nodiscard]] inline std::string sanitize_replay_trace_component(
    std::string_view value) {
  std::string sanitized;
  sanitized.reserve(value.size());
  for (const unsigned char ch : value) {
    if (std::isalnum(ch) != 0) {
      sanitized.push_back(static_cast<char>(ch));
    } else {
      sanitized.push_back('_');
    }
  }

  if (sanitized.empty()) {
    return "unknown";
  }

  return sanitized;
}

[[nodiscard]] inline std::string replay_trace_file_name(
    std::string_view request_id,
    std::string_view stage,
    std::string_view event_name) {
  constexpr std::string_view kPrefix = "replay.trace.";

  std::string suffix{event_name};
  if (suffix.rfind(kPrefix.data(), 0) == 0) {
    suffix.erase(0, kPrefix.size());
  }
  for (char& ch : suffix) {
    if (ch == '.') {
      ch = '_';
    }
  }

  return sanitize_replay_trace_component(request_id) + "__" +
         sanitize_replay_trace_component(stage) + "__" +
         sanitize_replay_trace_component(suffix) + ".trace";
}

class ReplayTraceRecorder final : public ICognitionTelemetrySink {
 public:
  explicit ReplayTraceRecorder(ReplayTraceRecorderConfig config)
      : config_(std::move(config)) {}

  void emit_log(const TelemetryEvent& event) override { maybe_record(event); }
  void emit_metric(const TelemetryMetric&) override {}
  void emit_trace(const TelemetryEvent&) override {}
  void emit_audit(const TelemetryEvent&) override {}

 private:
  [[nodiscard]] bool is_replay_trace_event(const TelemetryEvent& event) const {
    return !config_.output_dir.empty() && event.name.rfind("replay.trace.", 0) == 0;
  }

  [[nodiscard]] bool profile_enabled(const TelemetryEvent& event) const {
    return config_.enabled_profile_id.empty() ||
           event.context.profile_id == config_.enabled_profile_id;
  }

  [[nodiscard]] const std::string* find_serialized_value(
      const std::vector<TelemetryField>& fields) const {
    for (const auto& field : fields) {
      if (field.key == "serialized_value") {
        return &field.value;
      }
    }

    return nullptr;
  }

  [[nodiscard]] bool should_sample_schema_violation(
      const TelemetryEvent& event,
      std::string_view serialized_value) const {
    return event.name.rfind("replay.trace.", 0) == 0 &&
           event.name.size() >= 7 &&
           event.name.rfind(".result") == event.name.size() - 7 &&
           serialized_value.find("structured_projection.schema_violation:") !=
               std::string_view::npos;
  }

  [[nodiscard]] bool should_sample_reflection_abort_safe(
      const TelemetryEvent& event,
      std::string_view serialized_value) const {
    return event.name == "replay.trace.reflect.result" &&
           serialized_value.find("decision_kind=AbortSafe") != std::string_view::npos;
  }

  [[nodiscard]] bool should_sample_response_fallback(
      const TelemetryEvent& event,
      std::string_view serialized_value) const {
    return event.name == "replay.trace.build.result" &&
           serialized_value.find("fallback_used=true") != std::string_view::npos;
  }

  [[nodiscard]] std::string sampling_key(const TelemetryEvent& event,
                                         std::string_view category) const {
    return event.context.request_id + "|" + event.context.stage + "|" + event.name + "|" +
           std::string(category);
  }

  [[nodiscard]] std::filesystem::path output_root() const {
    return std::filesystem::path{config_.output_dir};
  }

  [[nodiscard]] std::filesystem::path failure_sample_root(
      std::string_view category) const {
    return output_root() / "failure_samples" / std::string(category);
  }

  void write_trace_file(const std::filesystem::path& output_path,
                        const std::string& serialized_value) const {
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      throw std::runtime_error("failed to open replay trace file: " +
                               output_path.string());
    }

    output << serialized_value;
    if (serialized_value.empty() || serialized_value.back() != '\n') {
      output << '\n';
    }
  }

  void record_replay_trace(const TelemetryEvent& event,
                           const std::string& serialized_value) const {
    namespace fs = std::filesystem;

    const fs::path root = output_root();
    fs::create_directories(root);
    write_trace_file(root / replay_trace_file_name(event.context.request_id,
                                                   event.context.stage,
                                                   event.name),
                     serialized_value);
  }

  void copy_request_trace_corpus(std::string_view request_id,
                                 const std::filesystem::path& destination_root) const {
    namespace fs = std::filesystem;

    fs::create_directories(destination_root);
    const auto request_prefix = sanitize_replay_trace_component(request_id) + "__";
    for (const auto& entry : fs::directory_iterator(output_root())) {
      if (!entry.is_regular_file()) {
        continue;
      }

      const auto file_name = entry.path().filename().string();
      if (file_name.rfind(request_prefix, 0) != 0) {
        continue;
      }

      std::error_code error;
      fs::copy_file(entry.path(),
                    destination_root / entry.path().filename(),
                    fs::copy_options::overwrite_existing,
                    error);
      if (error) {
        throw std::runtime_error("failed to copy replay failure sample: " +
                                 entry.path().string() + " -> " +
                                 (destination_root / entry.path().filename()).string() +
                                 ": " + error.message());
      }
    }
  }

  void maybe_record_failure_sample(const TelemetryEvent& event,
                                   std::string_view category,
                                   const ReplayFailureSampleRule& rule) const {
    if (!should_sample_replay_failure(rule, sampling_key(event, category))) {
      return;
    }

    copy_request_trace_corpus(event.context.request_id, failure_sample_root(category));
  }

  void maybe_record(const TelemetryEvent& event) {
    if (!is_replay_trace_event(event)) {
      return;
    }

    if (!profile_enabled(event)) {
      return;
    }

    const auto* serialized_value = find_serialized_value(event.fields);
    if (serialized_value == nullptr) {
      return;
    }

    record_replay_trace(event, *serialized_value);

    if (should_sample_schema_violation(event, *serialized_value)) {
      maybe_record_failure_sample(event,
                                  "cognition.schema_violation",
                                  config_.failure_sampling.cognition_schema_violation);
    }
    if (should_sample_reflection_abort_safe(event, *serialized_value)) {
      maybe_record_failure_sample(event,
                                  "reflection.abort_safe",
                                  config_.failure_sampling.reflection_abort_safe);
    }
    if (should_sample_response_fallback(event, *serialized_value)) {
      maybe_record_failure_sample(event,
                                  "response.fallback_used",
                                  config_.failure_sampling.response_fallback_used);
    }
  }

  ReplayTraceRecorderConfig config_;
};

[[nodiscard]] inline std::shared_ptr<ICognitionTelemetrySink>
make_replay_trace_recorder(ReplayTraceRecorderConfig config) {
  return std::make_shared<ReplayTraceRecorder>(std::move(config));
}

}  // namespace dasall::cognition::observability