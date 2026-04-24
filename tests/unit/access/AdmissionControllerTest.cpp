#include <exception>
#include <iostream>
#include <string>

#include "AdmissionController.h"
#include "support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::access::RuntimeDispatchRequest make_request(
    const std::string& packet_id,
    const std::string& actor_ref,
    const std::string& idempotency_key) {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = packet_id;
  request.packet.entry_type = "gateway";
  request.packet.protocol_kind = "http";
  request.subject_identity.actor_ref = actor_ref;
  request.request_context["idempotency_key"] = idempotency_key;
  return request;
}

void admits_request_and_returns_ticket() {
  using dasall::access::AccessAdmissionView;
  using dasall::access::AdmissionController;
  using dasall::tests::support::assert_true;

  AccessAdmissionView config;
  config.max_inflight_requests = 8;
  config.idempotency_window_ms = 60000;

  AdmissionController controller(config);
  const auto result = controller.admit(make_request("pkt-001", "user://tenant-a/alice", "idem-1"));

  assert_true(result.admitted, "normal request should pass admission");
  assert_true(result.ticket_ref.has_value(), "admitted request should return a ticket ref");
}

}  // namespace

int main() {
  try {
    admits_request_and_returns_ticket();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
