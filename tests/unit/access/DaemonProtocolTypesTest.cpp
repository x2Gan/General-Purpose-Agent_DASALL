#include <exception>
#include <iostream>
#include <string>

#include "daemon/DaemonProtocolTypes.h"
#include "support/TestAssertions.h"

namespace {

void test_request_frame_defaults_freeze_schema_and_async_preference() {
  using dasall::access::daemon::DaemonAsyncPreference;
  using dasall::access::daemon::UdsRequestFrame;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const UdsRequestFrame frame;
  assert_equal(std::string("1"), frame.schema_version,
               "request frame should freeze daemon schema_version to 1");
  assert_true(frame.request_id.empty(),
              "request frame should default request_id to empty before decode");
  assert_equal(static_cast<int>(DaemonAsyncPreference::PreferSync),
               static_cast<int>(frame.async_preference),
               "request frame should default to synchronous preference");
}

void test_response_frame_defaults_freeze_schema_and_disposition() {
  using dasall::access::daemon::UdsResponseDisposition;
  using dasall::access::daemon::UdsResponseFrame;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  const UdsResponseFrame frame;
  assert_equal(std::string("1"), frame.schema_version,
               "response frame should freeze daemon schema_version to 1");
  assert_equal(static_cast<int>(UdsResponseDisposition::Rejected),
               static_cast<int>(frame.disposition),
               "response frame should default disposition to rejected");
  assert_true(!frame.receipt_ref.has_value(),
              "response frame should not expose receipt_ref before publishing");
}

void test_command_taxonomy_classifies_v1_commands() {
  using dasall::access::daemon::DaemonCommandKind;
  using dasall::access::daemon::classify_daemon_command;
  using dasall::tests::support::assert_equal;

  assert_equal(static_cast<int>(DaemonCommandKind::Ping),
               static_cast<int>(classify_daemon_command("ping")),
               "ping should map to Ping taxonomy");
  assert_equal(static_cast<int>(DaemonCommandKind::Run),
               static_cast<int>(classify_daemon_command("run")),
               "run should map to Run taxonomy");
  assert_equal(static_cast<int>(DaemonCommandKind::Status),
               static_cast<int>(classify_daemon_command("status")),
               "status should map to Status taxonomy");
  assert_equal(static_cast<int>(DaemonCommandKind::Cancel),
               static_cast<int>(classify_daemon_command("cancel")),
               "cancel should map to Cancel taxonomy");
  assert_equal(static_cast<int>(DaemonCommandKind::Diagnostics),
               static_cast<int>(classify_daemon_command("diag")),
               "diag should map to Diagnostics taxonomy");
  assert_equal(static_cast<int>(DaemonCommandKind::Knowledge),
               static_cast<int>(classify_daemon_command("knowledge")),
               "knowledge should map to Knowledge taxonomy");
}

void test_command_taxonomy_preserves_read_only_and_mutating_split() {
  using dasall::access::daemon::DaemonCommandKind;
  using dasall::access::daemon::is_mutating_daemon_command;
  using dasall::access::daemon::is_read_only_daemon_command;
  using dasall::tests::support::assert_true;

  assert_true(is_read_only_daemon_command(DaemonCommandKind::Ping),
              "ping should be read-only");
  assert_true(is_read_only_daemon_command(DaemonCommandKind::Status),
              "status should be read-only");
  assert_true(is_read_only_daemon_command(DaemonCommandKind::Readiness),
              "readiness should be read-only");
  assert_true(is_read_only_daemon_command(DaemonCommandKind::Diagnostics),
              "diag should be read-only");
  assert_true(is_mutating_daemon_command(DaemonCommandKind::Run),
              "run should be treated as mutating command");
  assert_true(is_mutating_daemon_command(DaemonCommandKind::Cancel),
              "cancel should be treated as mutating command");
  assert_true(!is_read_only_daemon_command(DaemonCommandKind::Knowledge) &&
                  !is_mutating_daemon_command(DaemonCommandKind::Knowledge),
              "knowledge command should leave operation mutability to the access handler");
}

void test_unknown_command_maps_to_unknown_kind() {
  using dasall::access::daemon::DaemonCommandKind;
  using dasall::access::daemon::UdsRequestFrame;
  using dasall::tests::support::assert_equal;

  UdsRequestFrame frame;
  frame.command = "tail-logs";

  assert_equal(static_cast<int>(DaemonCommandKind::Unknown),
               static_cast<int>(frame.command_kind()),
               "unknown daemon command should map to Unknown taxonomy");
}

void test_legacy_aliases_stay_compatible_with_v1_run_and_diag() {
  using dasall::access::daemon::DaemonCommandKind;
  using dasall::access::daemon::classify_daemon_command;
  using dasall::tests::support::assert_equal;

  assert_equal(static_cast<int>(DaemonCommandKind::Run),
               static_cast<int>(classify_daemon_command("submit")),
               "submit alias should stay compatible with Run taxonomy");
  assert_equal(static_cast<int>(DaemonCommandKind::Diagnostics),
               static_cast<int>(classify_daemon_command("diagnostics")),
               "diagnostics alias should stay compatible with Diagnostics taxonomy");
}

}  // namespace

int main() {
  try {
    test_request_frame_defaults_freeze_schema_and_async_preference();
    test_response_frame_defaults_freeze_schema_and_disposition();
    test_command_taxonomy_classifies_v1_commands();
    test_command_taxonomy_preserves_read_only_and_mutating_split();
    test_unknown_command_maps_to_unknown_kind();
    test_legacy_aliases_stay_compatible_with_v1_run_and_diag();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonProtocolTypesTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}