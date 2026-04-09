#include "system/SystemSnapshotLane.h"

#include <chrono>
#include <sstream>
#include <string_view>
#include <utility>

namespace dasall::services::internal {

namespace {

[[nodiscard]] std::string escape_json_string(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());

  for (const char ch : value) {
    if (ch == '\\' || ch == '"') {
      escaped.push_back('\\');
    }
    escaped.push_back(ch);
  }

  return escaped;
}

[[nodiscard]] std::string build_string_array_json(const std::vector<std::string>& values) {
  std::ostringstream builder;
  builder << '[';
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0U) {
      builder << ',';
    }
    builder << '"' << escape_json_string(values[index]) << '"';
  }
  builder << ']';
  return builder.str();
}

[[nodiscard]] std::string json_or_null(const std::string& value) {
  return value.empty() ? "null" : value;
}

}  // namespace

SystemSnapshotLane::SystemSnapshotLane(SystemSnapshotLaneDependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

InternalSystemSnapshot SystemSnapshotLane::query_snapshot(
    const ServiceCallContext&,
    const InternalSnapshotQuery& request) const {
  const auto captured_at_ms = current_time_ms();
  const auto infra_health_json = dependencies_.load_infra_health_snapshot
                                     ? dependencies_.load_infra_health_snapshot()
                                     : std::string{};
  if (infra_health_json.empty() && request.strict_health) {
    return make_snapshot_error("infra health snapshot unavailable", captured_at_ms);
  }

  bool degraded = infra_health_json.empty();
  const auto platform_snapshot_json = request.include_platform_snapshot &&
                                              dependencies_.load_platform_snapshot
                                          ? dependencies_.load_platform_snapshot()
                                          : std::string{};
  if (request.include_platform_snapshot && platform_snapshot_json.empty()) {
    degraded = true;
  }

  const auto resource_summary_json = dependencies_.load_resource_summary
                                         ? dependencies_.load_resource_summary()
                                         : std::string{};
  if (resource_summary_json.empty()) {
    degraded = true;
  }

  std::vector<std::string> service_instances;
  if (request.include_service_registry) {
    if (dependencies_.load_service_instances) {
      service_instances = dependencies_.load_service_instances();
    } else {
      degraded = true;
    }
  }

  std::ostringstream snapshot_builder;
  snapshot_builder << "{\"timestamp_ms\":" << captured_at_ms
                   << ",\"degraded\":" << (degraded ? "true" : "false")
                   << ",\"infra_health_ok\":" << (infra_health_json.empty() ? "false" : "true")
                   << ",\"infra_health\":" << json_or_null(infra_health_json)
                   << ",\"platform_snapshot\":" << json_or_null(platform_snapshot_json)
                   << ",\"resource_summary\":" << json_or_null(resource_summary_json)
                   << ",\"service_instances\":" << build_string_array_json(service_instances)
                   << '}';

  return InternalSystemSnapshot{
      .snapshot_json = snapshot_builder.str(),
      .captured_at_ms = captured_at_ms,
      .service_instances = std::move(service_instances),
      .infra_health_ok = !infra_health_json.empty(),
      .degraded = degraded,
      .error_message = {},
  };
}

InternalSystemSnapshot SystemSnapshotLane::make_snapshot_error(const std::string& message,
                                                               std::uint64_t captured_at_ms) const {
  return InternalSystemSnapshot{
      .snapshot_json = {},
      .captured_at_ms = captured_at_ms,
      .service_instances = {},
      .infra_health_ok = false,
      .degraded = true,
      .error_message = message,
  };
}

std::uint64_t SystemSnapshotLane::current_time_ms() const {
  if (dependencies_.now_ms) {
    return dependencies_.now_ms();
  }

  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

}  // namespace dasall::services::internal