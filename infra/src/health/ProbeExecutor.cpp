#include "health/ProbeExecutor.h"

#include "health/HealthErrors.h"

#include <chrono>
#include <exception>
#include <string_view>
#include <utility>

namespace dasall::infra {
namespace {

constexpr std::string_view kTimeoutDetailPrefix = "health://probe/timeout/";
constexpr std::string_view kExceptionDetailPrefix = "health://probe/exception/";
constexpr std::string_view kMissingDetailPrefix = "health://probe/missing/";

[[nodiscard]] std::int64_t current_time_unix_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

ProbeExecutor::ProbeExecutor(ProbeRegistry& registry, ProbeExecutorOptions options)
    : registry_(registry), options_(std::move(options)) {}

ProbeResult ProbeExecutor::execute_once(const ProbeDescriptor& descriptor) {
  if (!descriptor.has_required_fields()) {
    return make_missing_probe_result(descriptor);
  }

  IHealthProbe* probe = registry_.find_probe(descriptor.probe_name);
  if (probe == nullptr) {
    return make_missing_probe_result(descriptor);
  }

  const auto started_at = std::chrono::steady_clock::now();
  try {
    ProbeResult result = probe->probe();
    const auto finished_at = std::chrono::steady_clock::now();
    const auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                finished_at - started_at)
                                .count();

    if (latency_ms > descriptor.timeout_ms) {
      return make_timeout_result(descriptor, latency_ms);
    }

    return normalize_result(descriptor, std::move(result), latency_ms);
  } catch (const std::exception& ex) {
    (void)ex;
    return make_exception_result(descriptor, "std_exception");
  } catch (...) {
    return make_exception_result(descriptor, "unknown_exception");
  }
}

std::vector<ProbeResult> ProbeExecutor::execute_batch(std::string_view group) {
  std::vector<ProbeResult> results;
  const auto descriptors = registry_.list_by_group(group);
  results.reserve(descriptors.size());
  for (const auto& descriptor : descriptors) {
    results.push_back(execute_once(descriptor));
  }

  return results;
}

std::size_t ProbeExecutor::consecutive_failure_count(std::string_view probe_name) const {
  const auto entry = consecutive_failures_.find(std::string(probe_name));
  if (entry == consecutive_failures_.end()) {
    return 0;
  }

  return entry->second;
}

ProbeResult ProbeExecutor::make_missing_probe_result(
    const ProbeDescriptor& descriptor) const {
  const auto mapping = map_health_error_code(HealthErrorCode::ProbeNotFound);
  return ProbeResult{
      .probe_name = descriptor.probe_name,
      .status = ProbeStatus::Unhealthy,
      .latency_ms = 0,
      .error_code = mapping.result_code,
      .detail_ref = std::string(kMissingDetailPrefix) + descriptor.probe_name,
      .timestamp = current_time_unix_ms(),
  };
}

ProbeResult ProbeExecutor::make_timeout_result(const ProbeDescriptor& descriptor,
                                               std::int64_t latency_ms) {
  auto& failure_count = consecutive_failures_[descriptor.probe_name];
  ++failure_count;
  const auto mapping = map_health_error_code(HealthErrorCode::ProbeTimeout);

  return ProbeResult{
      .probe_name = descriptor.probe_name,
      .status = resolve_failure_status(descriptor.probe_name),
      .latency_ms = latency_ms,
      .error_code = mapping.result_code,
      .detail_ref = std::string(kTimeoutDetailPrefix) + descriptor.probe_name,
      .timestamp = current_time_unix_ms(),
  };
}

ProbeResult ProbeExecutor::make_exception_result(const ProbeDescriptor& descriptor,
                                                 std::string detail_suffix) {
  auto& failure_count = consecutive_failures_[descriptor.probe_name];
  ++failure_count;
  const auto mapping = map_health_error_code(HealthErrorCode::ProbeException);

  return ProbeResult{
      .probe_name = descriptor.probe_name,
      .status = resolve_failure_status(descriptor.probe_name),
      .latency_ms = 0,
      .error_code = mapping.result_code,
      .detail_ref = std::string(kExceptionDetailPrefix) + descriptor.probe_name + "/" +
                    std::move(detail_suffix),
      .timestamp = current_time_unix_ms(),
  };
}

ProbeResult ProbeExecutor::normalize_result(const ProbeDescriptor& descriptor,
                                            ProbeResult result,
                                            std::int64_t latency_ms) {
  result.probe_name = descriptor.probe_name;
  result.latency_ms = latency_ms;
  if (result.timestamp <= 0) {
    result.timestamp = current_time_unix_ms();
  }

  const bool is_success = result.status == ProbeStatus::Healthy && !result.error_code.has_value();
  if (is_success) {
    consecutive_failures_[descriptor.probe_name] = 0;
    return result;
  }

  auto& failure_count = consecutive_failures_[descriptor.probe_name];
  ++failure_count;
  result.status = resolve_failure_status(descriptor.probe_name);
  if (!result.error_code.has_value()) {
    result.error_code = map_health_error_code(HealthErrorCode::ProbeException).result_code;
  }
  if (result.detail_ref.empty()) {
    result.detail_ref = std::string(kExceptionDetailPrefix) + descriptor.probe_name + "/result_failure";
  }

  return result;
}

ProbeStatus ProbeExecutor::resolve_failure_status(std::string_view probe_name) const {
  const auto count = consecutive_failure_count(probe_name);
  if (count >= options_.unhealthy_consecutive_failures) {
    return ProbeStatus::Unhealthy;
  }

  return ProbeStatus::Degraded;
}

}  // namespace dasall::infra