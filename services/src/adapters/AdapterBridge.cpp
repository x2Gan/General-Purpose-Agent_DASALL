#include "adapters/AdapterBridge.h"

#include <exception>
#include <utility>

#include "bridges/ServiceTraceBridge.h"

namespace dasall::services::internal {

namespace {

[[nodiscard]] std::string make_receipt_ref(const AdapterSelection& selection,
                                           const AdapterInvocationRequest& request,
                                           AdapterTransportOutcome transport_outcome) {
  return request.request_id + ":" + selection.adapter_id + ":" +
         std::string(transport_outcome_name(transport_outcome));
}

[[nodiscard]] std::string make_error_payload(std::string_view error_code,
                                             std::string_view message) {
  return std::string("{\"error\":\"") + std::string(error_code) +
         "\",\"message\":\"" + std::string(message) + "\"}";
}

[[nodiscard]] AdapterReceipt make_bridge_failure_receipt(const AdapterSelection& selection,
                                                         const AdapterInvocationRequest& request,
                                                         AdapterTransportOutcome outcome,
                                                         std::string provider_status_code,
                                                         std::string payload_json) {
  return AdapterReceipt{
      .receipt_ref = make_receipt_ref(selection, request, outcome),
      .adapter_id = selection.adapter_id,
      .route_kind = selection.route_kind,
      .target_id = request.target_id.empty() ? selection.target_id : request.target_id,
      .transport_outcome = outcome,
      .provider_status_code = std::move(provider_status_code),
      .payload_json = std::move(payload_json),
      .latency_ms = 0U,
      .side_effects = {},
      .evidence_refs = {},
  };
}

}  // namespace

std::string_view transport_outcome_name(AdapterTransportOutcome transport_outcome) {
  switch (transport_outcome) {
    case AdapterTransportOutcome::acknowledged:
      return "acknowledged";
    case AdapterTransportOutcome::timeout:
      return "timeout";
    case AdapterTransportOutcome::unreachable:
      return "unreachable";
    case AdapterTransportOutcome::rejected:
      return "rejected";
    case AdapterTransportOutcome::partial:
      return "partial";
  }

  return "unknown_transport_outcome";
}

AdapterBridge::AdapterBridge(AdapterBridgeDependencies dependencies)
    : dependencies_(std::move(dependencies)) {}

AdapterReceipt AdapterBridge::invoke(const AdapterSelection& selection,
                                     const AdapterInvocationRequest& request) const {
  auto adapter_span = dependencies_.trace_bridge != nullptr
                          ? dependencies_.trace_bridge->start_adapter_span(selection, request)
                          : ServiceTraceSpan{};

  for (const auto* invoker : dependencies_.invokers) {
    if (invoker == nullptr || invoker->adapter_id() != selection.adapter_id) {
      continue;
    }

    if (invoker->route_kind() != selection.route_kind) {
      auto receipt = make_bridge_failure_receipt(selection,
                                                 request,
                                                 AdapterTransportOutcome::rejected,
                                                 "route_kind_mismatch",
                                                 make_error_payload("route_kind_mismatch",
                                                                    "selected route_kind does not match invoker route_kind"));
      if (dependencies_.trace_bridge != nullptr) {
        dependencies_.trace_bridge->complete_span(&adapter_span, receipt);
      }
      return receipt;
    }

    try {
      auto invoke_selected = [&]() -> AdapterReceipt {
        auto external_span = dependencies_.trace_bridge != nullptr
                                 ? dependencies_.trace_bridge->start_external_span(selection, request)
                                 : ServiceTraceSpan{};
        const auto result = dependencies_.trace_bridge != nullptr
                                ? dependencies_.trace_bridge->with_span(
                                      external_span,
                                      [&]() { return invoker->invoke(request); })
                                : invoker->invoke(request);
        auto receipt = AdapterReceipt{
            .receipt_ref = make_receipt_ref(selection, request, result.transport_outcome),
            .adapter_id = selection.adapter_id,
            .route_kind = selection.route_kind,
            .target_id = request.target_id.empty() ? selection.target_id : request.target_id,
            .transport_outcome = result.transport_outcome,
            .provider_status_code = result.provider_status_code,
            .payload_json = result.payload_json,
            .latency_ms = result.latency_ms,
            .side_effects = result.side_effects,
            .evidence_refs = result.evidence_refs,
        };
        if (dependencies_.trace_bridge != nullptr) {
          dependencies_.trace_bridge->complete_span(&external_span, receipt);
        }
        return receipt;
      };

      auto receipt = dependencies_.trace_bridge != nullptr
                         ? dependencies_.trace_bridge->with_span(adapter_span,
                                                                 invoke_selected)
                         : invoke_selected();
      if (dependencies_.trace_bridge != nullptr) {
        dependencies_.trace_bridge->complete_span(&adapter_span, receipt);
      }
      return receipt;
    } catch (const std::exception& ex) {
      auto receipt = make_bridge_failure_receipt(selection,
                                                 request,
                                                 AdapterTransportOutcome::rejected,
                                                 "bridge_exception",
                                                 make_error_payload("bridge_exception", ex.what()));
      if (dependencies_.trace_bridge != nullptr) {
        dependencies_.trace_bridge->complete_span(&adapter_span, receipt);
      }
      return receipt;
    } catch (...) {
      auto receipt = make_bridge_failure_receipt(selection,
                                                 request,
                                                 AdapterTransportOutcome::rejected,
                                                 "bridge_exception",
                                                 make_error_payload("bridge_exception",
                                                                    "unknown adapter invocation failure"));
      if (dependencies_.trace_bridge != nullptr) {
        dependencies_.trace_bridge->complete_span(&adapter_span, receipt);
      }
      return receipt;
    }
  }

  auto receipt = make_bridge_failure_receipt(selection,
                                             request,
                                             AdapterTransportOutcome::unreachable,
                                             "adapter_not_registered",
                                             make_error_payload("adapter_not_registered",
                                                                "selected adapter_id is not registered in AdapterBridge"));
  if (dependencies_.trace_bridge != nullptr) {
    dependencies_.trace_bridge->complete_span(&adapter_span, receipt);
  }
  return receipt;
}

}  // namespace dasall::services::internal