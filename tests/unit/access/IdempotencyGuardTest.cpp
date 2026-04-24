#include <exception>
#include <iostream>
#include <string>

#include "AdmissionController.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_request() {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = "pkt-001";
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http";
  request.subject_identity.actor_ref = "user://tenant-a/alice";
  request.request_context["idempotency_key"] = "idem-constant";
  return request;
}

void marks_second_inflight_request_as_conflict_hit() {
  using dasall::access::AccessAdmissionView;
  using dasall::access::AdmissionController;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessAdmissionView config;
  config.max_inflight_requests = 8;
  config.idempotency_window_ms = 60000;

  AdmissionController controller(config);
  const auto first = controller.admit(make_request());
  const auto second = controller.admit(make_request());

  assert_true(first.admitted, "first request should pass and create inflight record");
  assert_true(second.conflict_hit,
              "second request with same idempotency signature should conflict");
  assert_true(second.reject_reason.has_value(),
              "conflict path should expose reject reason");
  assert_equal(std::string("idempotency_conflict"), *second.reject_reason,
               "conflict path should map to idempotency_conflict");
}

}  // namespace

int main() {
  try {
    marks_second_inflight_request_as_conflict_hit();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
