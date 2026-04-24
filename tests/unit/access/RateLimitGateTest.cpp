#include <exception>
#include <iostream>
#include <string>

#include "AdmissionController.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_request(
    const std::string& packet_id,
    const std::string& idempotency_key) {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = packet_id;
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http";
  request.subject_identity.actor_ref = "user://tenant-a/alice";
  request.request_context["idempotency_key"] = idempotency_key;
  return request;
}

void rejects_request_when_inflight_quota_is_exhausted() {
  using dasall::access::AccessAdmissionView;
  using dasall::access::AdmissionController;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessAdmissionView config;
  config.max_inflight_requests = 1;
  config.idempotency_window_ms = 60000;

  AdmissionController controller(config);
  const auto first = controller.admit(make_request("pkt-001", "idem-1"));
  const auto second = controller.admit(make_request("pkt-002", "idem-2"));

  assert_true(first.admitted, "first request should occupy the only inflight slot");
  assert_true(!second.admitted, "second request should be rejected by inflight quota");
  assert_true(second.reject_reason.has_value(),
              "quota rejection should expose reject reason");
  assert_equal(std::string("concurrency_limit_exceeded"), *second.reject_reason,
               "quota rejection reason should be concurrency_limit_exceeded");
}

}  // namespace

int main() {
  try {
    rejects_request_when_inflight_quota_is_exhausted();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
