#include "bridges/ServiceLoggingBridge.h"

#include <chrono>

#include "LogEvent.h"

namespace dasall::services::internal {
namespace {

[[nodiscard]] std::int64_t current_time_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] infra::LogLevel transport_log_level(
    const AdapterTransportOutcome transport_outcome) {
  switch (transport_outcome) {
    case AdapterTransportOutcome::acknowledged:
      return infra::LogLevel::Info;
    case AdapterTransportOutcome::partial:
    case AdapterTransportOutcome::timeout:
      return infra::LogLevel::Warn;
    case AdapterTransportOutcome::unreachable:
    case AdapterTransportOutcome::rejected:
      return infra::LogLevel::Error;
  }

  return infra::LogLevel::Info;
}

[[nodiscard]] std::string trust_class_name(const AdapterTrustClass trust_class) {
  switch (trust_class) {
    case AdapterTrustClass::untrusted:
      return "untrusted";
    case AdapterTrustClass::caller_verified:
      return "caller_verified";
    case AdapterTrustClass::trusted_local:
      return "trusted_local";
  }

  return "untrusted";
}

[[nodiscard]] std::string availability_state_name(
    const AdapterAvailabilityState availability_state) {
  switch (availability_state) {
    case AdapterAvailabilityState::available:
      return "available";
    case AdapterAvailabilityState::degraded:
      return "degraded";
    case AdapterAvailabilityState::unavailable:
      return "unavailable";
    case AdapterAvailabilityState::unknown:
      return "unknown";
  }

  return "unknown";
}

[[nodiscard]] bool has_required_route_fields(const ServiceCallContext& context,
                                             const std::string& capability_id,
                                             const std::string& target_id,
                                             const std::string& operation_name,
                                             const AdapterSelection& selection) {
  return !context.request_id.empty() && !capability_id.empty() && !target_id.empty() &&
         !operation_name.empty() && !selection.adapter_id.empty();
}

}  // namespace

ServiceLoggingBridge::ServiceLoggingBridge(
    std::shared_ptr<infra::logging::ILogger> logger,
    ServiceLoggingBridgeOptions options)
    : logger_(std::move(logger)), options_(std::move(options)) {}

void ServiceLoggingBridge::set_logger(std::shared_ptr<infra::logging::ILogger> logger) {
  logger_ = std::move(logger);
}

infra::logging::LogWriteResult ServiceLoggingBridge::write_execution_route(
    const ServiceCallContext& context,
    const CapabilityTargetRef& target,
    const std::string& action,
    const AdapterSelection& selection,
    const AdapterReceipt& receipt) const {
  return write_route_event("service.execution.route",
                           context,
                           target.capability_id,
                           target.target_id,
                           "action",
                           action,
                           selection,
                           receipt);
}

infra::logging::LogWriteResult ServiceLoggingBridge::write_data_query_route(
    const ServiceCallContext& context,
    const std::string& dataset,
    const std::string& projection,
    const AdapterSelection& selection,
    const AdapterReceipt& receipt) const {
  return write_route_event("service.data.query.route",
                           context,
                           dataset,
                           dataset,
                           "query",
                           projection,
                           selection,
                           receipt);
}

infra::logging::LogWriteResult ServiceLoggingBridge::write_data_catalog_route(
    const ServiceCallContext& context,
    const std::string& target_class,
    const AdapterSelection& selection,
    const AdapterReceipt& receipt) const {
  return write_route_event("service.data.catalog.route",
                           context,
                           target_class,
                           target_class,
                           "query",
                           "catalog.list",
                           selection,
                           receipt);
}

infra::logging::LogWriteResult ServiceLoggingBridge::write_route_event(
    const std::string& event_name,
    const ServiceCallContext& context,
    const std::string& capability_id,
    const std::string& target_id,
    const std::string& request_kind,
    const std::string& operation_name,
    const AdapterSelection& selection,
    const AdapterReceipt& receipt) const {
  if (!options_.enabled) {
    return infra::logging::LogWriteResult::success();
  }
  if (logger_ == nullptr) {
    return infra::logging::LogWriteResult::failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        "service logger unavailable",
        "services.logging_bridge.write_route_event",
        "ServiceLoggingBridge");
  }
  if (!has_required_route_fields(
          context, capability_id, target_id, operation_name, selection)) {
    return infra::logging::LogWriteResult::failure(
        contracts::ResultCode::RuntimeRetryExhausted,
        "service route event missing required correlation fields",
        "services.logging_bridge.write_route_event",
        "ServiceLoggingBridge");
  }

  infra::LogEvent log_event;
  log_event.level = transport_log_level(receipt.transport_outcome);
  log_event.module = "services";
  log_event.message = event_name;
  log_event.ts = options_.now_ms ? options_.now_ms() : current_time_ms();
  log_event.attrs.emplace("event_name", event_name);
  log_event.attrs.emplace("request_id", context.request_id);
  log_event.attrs.emplace("capability_id", capability_id);
  log_event.attrs.emplace("target_id", target_id);
  log_event.attrs.emplace("request_kind", request_kind);
  log_event.attrs.emplace("operation_name", operation_name);
  log_event.attrs.emplace("route_kind", std::string(route_kind_name(selection.route_kind)));
  log_event.attrs.emplace("adapter_id", selection.adapter_id);
  log_event.attrs.emplace("trust_class", trust_class_name(selection.trust_class));
  log_event.attrs.emplace("availability_state",
                          availability_state_name(selection.availability_state));
  log_event.attrs.emplace("transport_outcome",
                          std::string(transport_outcome_name(receipt.transport_outcome)));
  log_event.attrs.emplace("provider_status_code",
                          receipt.provider_status_code.empty()
                              ? std::string("none")
                              : receipt.provider_status_code);
  log_event.attrs.emplace("latency_ms", std::to_string(receipt.latency_ms));
  log_event.attrs.emplace("side_effect_count",
                          std::to_string(receipt.side_effects.size()));
  log_event.attrs.emplace("evidence_ref_count",
                          std::to_string(receipt.evidence_refs.size()));
  return logger_->log(log_event);
}

}  // namespace dasall::services::internal