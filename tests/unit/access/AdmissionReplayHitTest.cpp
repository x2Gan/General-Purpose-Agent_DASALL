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
  request.request_context["idempotency_key"] = "idem-replay";
  return request;
}

void returns_replay_hit_after_completion_is_recorded() {
  using dasall::access::AccessAdmissionView;
  using dasall::access::AccessDisposition;
  using dasall::access::AdmissionController;
  using dasall::access::RuntimeDispatchResult;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  AccessAdmissionView config;
  config.max_inflight_requests = 8;
  config.idempotency_window_ms = 60000;

  AdmissionController controller(config);
  const auto first = controller.admit(make_request());
  assert_true(first.admitted && first.ticket_ref.has_value(),
              "first request should be admitted and allocate ticket");

  RuntimeDispatchResult completion;
  completion.disposition = AccessDisposition::AcceptedAsync;
  completion.receipt_ref = std::string("receipt-001");
  controller.record_completion(*first.ticket_ref, completion);

  const auto second = controller.admit(make_request());
  assert_true(second.replay_hit,
              "completed idempotency record should return replay_hit");
  assert_true(second.replay_receipt_ref.has_value(),
              "replay_hit should carry replay receipt reference");
  assert_equal(std::string("receipt-001"), *second.replay_receipt_ref,
               "replay receipt should match completion record");
}

}  // namespace

int main() {
  try {
    returns_replay_hit_after_completion_is_recorded();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}
