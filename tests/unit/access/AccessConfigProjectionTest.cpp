#include <exception>
#include <iostream>

#include "AccessTypes.h"
#include "support/TestAssertions.h"

namespace {

void access_bootstrap_config_fields_are_defined() {
  dasall::access::AccessBootstrapConfig config;
  config.bootstrap_revision = "bootstrap-rev-001";
  config.entry_type = "daemon";
  config.listen_ref = "uds:///tmp/dasall.sock";
  config.allowed_protocols = {"uds", "http"};
  config.auth_provider_ref = "secret://auth/provider-1";
  config.ownership_token_hmac_secret_ref = "secret://hmac/access";

  dasall::tests::support::assert_equal(
      std::string("bootstrap-rev-001"),
      config.bootstrap_revision,
      "AccessBootstrapConfig.bootstrap_revision should be writable");
  dasall::tests::support::assert_equal(
      std::string("daemon"),
      config.entry_type,
      "AccessBootstrapConfig.entry_type should be writable");
  dasall::tests::support::assert_equal(
      std::string("strict"),
      config.peer_auth_mode,
      "AccessBootstrapConfig.peer_auth_mode should default to strict");
  dasall::tests::support::assert_true(
      config.auth_provider_ref.has_value(),
      "AccessBootstrapConfig.auth_provider_ref should be optional and writable");
}

void access_snapshot_fingerprint_fields_are_defined() {
  dasall::access::SnapshotVersionFingerprint fingerprint;
  fingerprint.bootstrap_revision = "bootstrap-rev-002";
  fingerprint.effective_profile_id = "profile-edge-balanced";
  fingerprint.runtime_policy_generation = 42;

  dasall::tests::support::assert_equal(
      std::string("bootstrap-rev-002"),
      fingerprint.bootstrap_revision,
      "SnapshotVersionFingerprint.bootstrap_revision should be writable");
  dasall::tests::support::assert_equal(
      std::string("profile-edge-balanced"),
      fingerprint.effective_profile_id,
      "SnapshotVersionFingerprint.effective_profile_id should be writable");
  dasall::tests::support::assert_equal(
      42,
      static_cast<int>(fingerprint.runtime_policy_generation),
      "SnapshotVersionFingerprint.runtime_policy_generation should be writable");
}

void access_governance_views_are_defined() {
  dasall::access::AccessAuthView auth_view;
  dasall::access::AccessAdmissionView admission_view;
  dasall::access::AccessPublishView publish_view;
  dasall::access::AccessRuntimeGovernanceView governance_view;

  auth_view.peer_auth_mode = "strict";
  auth_view.strict_auth_required = true;

  admission_view.idempotency_window_ms = 60000;
  admission_view.default_deny = true;

  publish_view.result_replay_ttl_ms = 120000;
  publish_view.max_payload_bytes = 2048;

  governance_view.runtime_budget_profile = "edge_balanced";
  governance_view.security_default_effect = "deny";

  dasall::tests::support::assert_true(
      auth_view.strict_auth_required,
      "AccessAuthView.strict_auth_required should be writable");
  dasall::tests::support::assert_true(
      admission_view.default_deny,
      "AccessAdmissionView.default_deny should be writable");
  dasall::tests::support::assert_equal(
      2048,
      publish_view.max_payload_bytes,
      "AccessPublishView.max_payload_bytes should be writable");
  dasall::tests::support::assert_equal(
      std::string("deny"),
      governance_view.security_default_effect,
      "AccessRuntimeGovernanceView.security_default_effect should be writable");
}

}  // namespace

int main() {
  try {
    access_bootstrap_config_fields_are_defined();
    access_snapshot_fingerprint_fields_are_defined();
    access_governance_views_are_defined();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}
