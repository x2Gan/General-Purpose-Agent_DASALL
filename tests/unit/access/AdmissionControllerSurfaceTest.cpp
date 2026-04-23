#include <exception>
#include <iostream>
#include <type_traits>

#include "AccessTypes.h"
#include "IAdmissionController.h"
#include "support/TestAssertions.h"

namespace {

void access_admission_result_fields_are_defined() {
  dasall::access::AccessAdmissionResult result;
  result.admitted = true;
  result.replay_hit = false;
  result.conflict_hit = false;
  result.ticket_ref = "ticket-001";
  result.reject_reason = "none";

  dasall::tests::support::assert_true(
      result.admitted,
      "AccessAdmissionResult.admitted should be writable");
  dasall::tests::support::assert_true(
      result.ticket_ref.has_value(),
      "AccessAdmissionResult.ticket_ref should be optional and writable");
  dasall::tests::support::assert_equal(
      std::string("ticket-001"),
      result.ticket_ref.value(),
      "ticket_ref value should be preserved");
}

void access_admission_result_supports_replay_and_conflict() {
  dasall::access::AccessAdmissionResult replay_result;
  replay_result.admitted = false;
  replay_result.replay_hit = true;
  replay_result.replay_receipt_ref = "receipt-xyz";

  dasall::access::AccessAdmissionResult conflict_result;
  conflict_result.admitted = false;
  conflict_result.conflict_hit = true;
  conflict_result.reject_reason = "idempotency_conflict";

  dasall::tests::support::assert_true(
      replay_result.replay_hit,
      "replay result should mark replay_hit=true");
  dasall::tests::support::assert_true(
      replay_result.replay_receipt_ref.has_value(),
      "replay result should carry replay_receipt_ref");
  dasall::tests::support::assert_true(
      conflict_result.conflict_hit,
      "conflict result should mark conflict_hit=true");
  dasall::tests::support::assert_true(
      conflict_result.reject_reason.has_value(),
      "conflict result should provide reject_reason");
}

void iadmission_controller_methods_exist() {
  static_assert(std::is_abstract_v<dasall::access::IAdmissionController>);

  constexpr bool admit_method_exists =
      std::is_invocable_v<decltype(&dasall::access::IAdmissionController::admit),
                          dasall::access::IAdmissionController*,
                          const dasall::access::RuntimeDispatchRequest&>;
  constexpr bool release_ticket_method_exists =
      std::is_invocable_v<
          decltype(&dasall::access::IAdmissionController::release_ticket),
          dasall::access::IAdmissionController*,
          const std::string&>;
  constexpr bool record_completion_method_exists =
      std::is_invocable_v<
          decltype(&dasall::access::IAdmissionController::record_completion),
          dasall::access::IAdmissionController*,
          const std::string&,
          const dasall::access::RuntimeDispatchResult&>;

  dasall::tests::support::assert_true(
      admit_method_exists,
      "IAdmissionController::admit should be defined");
  dasall::tests::support::assert_true(
      release_ticket_method_exists,
      "IAdmissionController::release_ticket should be defined");
  dasall::tests::support::assert_true(
      record_completion_method_exists,
      "IAdmissionController::record_completion should be defined");
}

}  // namespace

int main() {
  try {
    access_admission_result_fields_are_defined();
    access_admission_result_supports_replay_and_conflict();
    iadmission_controller_methods_exist();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
