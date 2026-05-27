#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "AsyncTaskRegistry.h"
#include "DaemonInProcessFixture.h"
#include "daemon/DaemonFrameCodec.h"
#include "support/TestAssertions.h"

namespace {

using namespace std::chrono_literals;

using dasall::access::AccessDisposition;
using dasall::access::AsyncTaskReceipt;
using dasall::access::AsyncTaskRegistry;
using dasall::access::DaemonAccessPipelineOptions;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::access::daemon::DaemonAsyncPreference;
using dasall::access::daemon::UdsRequestFrame;
using dasall::tests::integration::access_support::DaemonInProcessFixture;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] DaemonAccessPipelineOptions make_authority_options(
    const std::shared_ptr<AsyncTaskRegistry>& registry,
    const std::string& profile_id) {
  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.daemon_profile_id = profile_id;
  options.async_task_registry = registry;
  options.runtime_dispatch_backend = [](const RuntimeDispatchRequest& request) {
    RuntimeDispatchResult result;
    result.disposition = AccessDisposition::AcceptedAsync;
    result.receipt_ref = "receipt:" + request.packet.payload;
    return result;
  };
  return options;
}

[[nodiscard]] dasall::apps::cli::DaemonClientResponse submit_unique_async_request(
    const DaemonInProcessFixture& fixture,
    const std::string& request_id,
    const std::string& payload) {
  UdsRequestFrame frame;
  frame.request_id = request_id;
  frame.trace_id = request_id + "-trace";
  frame.session_hint = std::nullopt;
  frame.idempotency_key = request_id + "-idem";
  frame.command = "run";
  frame.args = {};
  frame.payload = payload;
  frame.async_preference = DaemonAsyncPreference::PreferAsync;
  return fixture.send_frame(frame);
}

void separate_daemon_instances_do_not_share_receipt_authority() {
  auto issuing_registry = std::make_shared<AsyncTaskRegistry>(
      "daemon-receipt-authority-v1",
      30s);
  auto sibling_registry = std::make_shared<AsyncTaskRegistry>(
      "daemon-receipt-authority-v1",
      30s);
  DaemonInProcessFixture issuing_fixture(
      make_authority_options(
          issuing_registry,
          "daemon.receipt.authority.issuing"));
  DaemonInProcessFixture sibling_fixture(
      make_authority_options(
          sibling_registry,
          "daemon.receipt.authority.sibling"));

  const auto accepted = submit_unique_async_request(
      issuing_fixture,
      "authority-instance-one",
      "authority-instance-one");
  assert_true(accepted.receipt_ref.has_value(),
              "receipt authority gate should produce a receipt_ref on the issuing daemon");

  const auto receipt_query = issuing_registry->query_receipt(*accepted.receipt_ref);
  assert_true(receipt_query.receipt.has_value(),
              "receipt authority gate should store the accepted receipt in the issuing registry");
  const auto& receipt = *receipt_query.receipt;

  const auto local_status = issuing_fixture.make_client().query_status(
      receipt.receipt_id,
      receipt.ownership_token,
      receipt.actor_ref);
  assert_true(local_status.ok(),
              "receipt authority gate should let the issuing daemon resolve its own receipt");
  assert_true(local_status.response_text.has_value(),
              "receipt authority gate should surface an active status on the issuing daemon");
  assert_equal(std::string("active"), *local_status.response_text,
               "receipt authority gate should keep issuing-daemon receipts queryable while alive");

  assert_equal(static_cast<std::size_t>(1U), issuing_fixture.receipt_active_count(),
               "receipt authority gate should expose one active receipt on the issuing daemon only");
  assert_equal(static_cast<std::size_t>(0U), sibling_fixture.receipt_active_count(),
               "receipt authority gate should keep sibling daemon registries empty before cross-instance queries");

  const auto sibling_status = sibling_fixture.make_client().query_status(
      receipt.receipt_id,
      receipt.ownership_token,
      receipt.actor_ref);
  assert_true(sibling_status.ok(),
              "receipt authority gate should parse cross-instance status responses");
  assert_true(sibling_status.error_ref.has_value(),
              "receipt authority gate should reject cross-instance receipt queries with an explicit error");
  assert_equal(std::string("status_missing"), *sibling_status.error_ref,
               "receipt authority gate should fail closed when a sibling daemon does not own the receipt authority");
  assert_equal(static_cast<std::size_t>(0U), sibling_fixture.receipt_active_count(),
               "receipt authority gate should not materialize shared receipt state on sibling daemons");

  sibling_fixture.stop();
  assert_true(sibling_fixture.daemon_stopped_cleanly(),
              "receipt authority sibling fixture should stop cleanly after the cross-instance check");
  issuing_fixture.stop();
  assert_true(issuing_fixture.daemon_stopped_cleanly(),
              "receipt authority issuing fixture should stop cleanly after the cross-instance check");
}

void daemon_restart_requires_a_new_receipt_authority_scope() {
  std::optional<AsyncTaskReceipt> issued_receipt;

  {
    auto original_registry = std::make_shared<AsyncTaskRegistry>(
        "daemon-receipt-authority-v1",
        30s);
    DaemonInProcessFixture original_fixture(
        make_authority_options(
            original_registry,
            "daemon.receipt.authority.original"));

    const auto accepted = submit_unique_async_request(
        original_fixture,
        "authority-restart-origin",
        "authority-restart-origin");
    assert_true(accepted.receipt_ref.has_value(),
                "receipt authority restart gate should produce a receipt_ref before restart");

    const auto receipt_query = original_registry->query_receipt(*accepted.receipt_ref);
    assert_true(receipt_query.receipt.has_value(),
                "receipt authority restart gate should retain the issuing receipt before shutdown");
    issued_receipt = receipt_query.receipt;

    original_fixture.stop();
    assert_true(original_fixture.daemon_stopped_cleanly(),
                "receipt authority original fixture should stop cleanly before restart validation");
  }

  assert_true(issued_receipt.has_value(),
              "receipt authority restart gate should capture the original receipt before restart");

  auto restarted_registry = std::make_shared<AsyncTaskRegistry>(
      "daemon-receipt-authority-v1",
      30s);
  DaemonInProcessFixture restarted_fixture(
      make_authority_options(
          restarted_registry,
          "daemon.receipt.authority.restarted"));

  const auto restarted_status = restarted_fixture.make_client().query_status(
      issued_receipt->receipt_id,
      issued_receipt->ownership_token,
      issued_receipt->actor_ref);
  assert_true(restarted_status.ok(),
              "receipt authority restart gate should parse post-restart status queries");
  assert_true(restarted_status.error_ref.has_value(),
              "receipt authority restart gate should return an explicit missing error after restart");
  assert_equal(std::string("status_missing"), *restarted_status.error_ref,
               "receipt authority restart gate should not treat a fresh daemon registry as authoritative for old receipts");
  assert_equal(static_cast<std::size_t>(0U), restarted_fixture.receipt_active_count(),
               "receipt authority restart gate should keep the restarted daemon registry empty for pre-restart receipts");

  restarted_fixture.stop();
  assert_true(restarted_fixture.daemon_stopped_cleanly(),
              "receipt authority restarted fixture should stop cleanly after restart validation");
}

}  // namespace

int main() {
  try {
    separate_daemon_instances_do_not_share_receipt_authority();
    daemon_restart_requires_a_new_receipt_authority_scope();
  } catch (const std::exception& ex) {
    std::cerr << "[AccessReceiptAuthorityIntegrationTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}