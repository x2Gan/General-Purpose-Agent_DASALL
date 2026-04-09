#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "adapters/AdapterRouter.h"

namespace dasall::services::internal {

class ServiceTraceBridge;

enum class AdapterTransportOutcome {
  acknowledged,
  timeout,
  unreachable,
  rejected,
  partial,
};

struct AdapterInvocationRequest {
  std::string request_id;
  std::string capability_id;
  std::string target_id;
  AdapterRouteRequestKind request_kind = AdapterRouteRequestKind::action;
  std::string operation_name;
  std::string payload_json;
};

struct AdapterInvocationResult {
  AdapterTransportOutcome transport_outcome = AdapterTransportOutcome::rejected;
  std::string provider_status_code;
  std::string payload_json;
  std::uint32_t latency_ms = 0U;
  std::vector<std::string> side_effects;
  std::vector<std::string> evidence_refs;
};

struct AdapterReceipt {
  std::string receipt_ref;
  std::string adapter_id;
  AdapterRouteKind route_kind = AdapterRouteKind::local_service;
  std::string target_id;
  AdapterTransportOutcome transport_outcome = AdapterTransportOutcome::rejected;
  std::string provider_status_code;
  std::string payload_json;
  std::uint32_t latency_ms = 0U;
  std::vector<std::string> side_effects;
  std::vector<std::string> evidence_refs;
};

class IAdapterInvoker {
 public:
  virtual ~IAdapterInvoker() = default;

  [[nodiscard]] virtual std::string_view adapter_id() const = 0;
  [[nodiscard]] virtual AdapterRouteKind route_kind() const = 0;
  [[nodiscard]] virtual AdapterInvocationResult invoke(
      const AdapterInvocationRequest& request) const = 0;
};

struct AdapterBridgeDependencies {
  std::vector<const IAdapterInvoker*> invokers;
  ServiceTraceBridge* trace_bridge = nullptr;
};

class AdapterBridge {
 public:
  explicit AdapterBridge(AdapterBridgeDependencies dependencies);

  [[nodiscard]] AdapterReceipt invoke(const AdapterSelection& selection,
                                      const AdapterInvocationRequest& request) const;

 private:
  AdapterBridgeDependencies dependencies_;
};

[[nodiscard]] std::string_view transport_outcome_name(AdapterTransportOutcome transport_outcome);

}  // namespace dasall::services::internal