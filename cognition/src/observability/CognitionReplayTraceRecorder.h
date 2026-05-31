#pragma once

#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "observability/CognitionTelemetry.h"

namespace dasall::cognition::observability {

struct ReplayTraceRecorderConfig {
  std::string output_dir;
  std::string enabled_profile_id = "build-ci/replay";
};

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
  [[nodiscard]] const std::string* find_serialized_value(
      const std::vector<TelemetryField>& fields) const {
    for (const auto& field : fields) {
      if (field.key == "serialized_value") {
        return &field.value;
      }
    }

    return nullptr;
  }

  void maybe_record(const TelemetryEvent& event) {
    if (config_.output_dir.empty() || event.name.rfind("replay.trace.", 0) != 0) {
      return;
    }

    if (!config_.enabled_profile_id.empty() &&
        event.context.profile_id != config_.enabled_profile_id) {
      return;
    }

    const auto* serialized_value = find_serialized_value(event.fields);
    if (serialized_value == nullptr) {
      return;
    }

    namespace fs = std::filesystem;
    const fs::path output_root{config_.output_dir};
    fs::create_directories(output_root);

    const fs::path output_path =
        output_root /
        replay_trace_file_name(event.context.request_id, event.context.stage, event.name);

    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      throw std::runtime_error("failed to open replay trace file: " +
                               output_path.string());
    }

    output << *serialized_value;
    if (serialized_value->empty() || serialized_value->back() != '\n') {
      output << '\n';
    }
  }

  ReplayTraceRecorderConfig config_;
};

[[nodiscard]] inline std::shared_ptr<ICognitionTelemetrySink>
make_replay_trace_recorder(ReplayTraceRecorderConfig config) {
  return std::make_shared<ReplayTraceRecorder>(std::move(config));
}

}  // namespace dasall::cognition::observability