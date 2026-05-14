#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "AccessGatewayFactory.h"
#include "IAccessGateway.h"
#include "policy/ISecurityPolicyManager.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessDisposition;
using dasall::access::GatewayAccessPipelineOptions;
using dasall::access::InboundPacket;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class StaticAllowPolicyManager final : public dasall::infra::policy::ISecurityPolicyManager {
 public:
  StaticAllowPolicyManager()
      : snapshot_{
            .snapshot_id = std::string("access-policy-snapshot-047"),
            .generation = 47,
            .version = std::string("policy-v47"),
            .mode = dasall::infra::policy::PolicyMode::Enforced,
            .effective_rules = {
                dasall::infra::policy::PolicyRuleDescriptor{
                    .rule_id = std::string("allow-access-submit"),
                  .domain = dasall::infra::policy::PolicyDomain::PolicyAdmin,
                    .subject = std::string("user"),
                    .action = std::string("submit"),
                    .target_selector = std::string("entry:gateway"),
                    .effect = dasall::infra::policy::PolicyEffect::Allow,
                    .priority = 100,
                    .mode = dasall::infra::policy::PolicyMode::Enforced,
                    .conditions = {std::string("authenticated")},
                    .reason_code = std::string("access_submit_allowed"),
                },
            },
            .created_at = std::string("2026-05-14T00:00:00Z"),
            .source_chain = {std::string("tests")},
            .last_known_good_ref = std::string("access-policy-snapshot-046"),
        } {}

  mutable std::optional<dasall::infra::policy::PolicyQueryContext> last_query;

  [[nodiscard]] dasall::infra::policy::PolicyOpResult load_policy(
      const dasall::infra::policy::PolicyBundle&) override {
    return dasall::infra::policy::PolicyOpResult::success(snapshot_.snapshot_id,
                                                          snapshot_.generation);
  }

  [[nodiscard]] dasall::infra::policy::PolicyOpResult apply_patch(
      const dasall::infra::policy::PolicyPatch&) override {
    return dasall::infra::policy::PolicyOpResult::success(snapshot_.snapshot_id,
                                                          snapshot_.generation + 1U);
  }

  [[nodiscard]] dasall::infra::policy::ValidationReport dry_run_patch(
      const dasall::infra::policy::PolicyPatch&) override {
    return {};
  }

  [[nodiscard]] dasall::infra::policy::PolicySnapshot snapshot() const override {
    return snapshot_;
  }

  [[nodiscard]] dasall::infra::policy::PolicyOpResult rollback(
      const std::string& snapshot_id) override {
    return dasall::infra::policy::PolicyOpResult::success(
        snapshot_id.empty() ? snapshot_.snapshot_id : snapshot_id,
        snapshot_.generation,
        true);
  }

  [[nodiscard]] dasall::infra::policy::PolicyDecisionRef evaluate(
      const dasall::infra::policy::PolicyQueryContext& query) const override {
    last_query = query;
    return dasall::infra::policy::PolicyDecisionRef{
        .decision = dasall::infra::policy::PolicyDecision::Allow,
        .reason_code = std::string("access_submit_allowed"),
        .matched_rule_ids = {std::string("allow-access-submit")},
        .snapshot_id = snapshot_.snapshot_id,
        .generation = snapshot_.generation,
        .evidence_ref = std::string("audit:policy/access/047"),
      .warnings = {},
    };
  }

 private:
  dasall::infra::policy::PolicySnapshot snapshot_;
};

[[nodiscard]] GatewayAccessPipelineOptions make_base_options(
    std::shared_ptr<StaticAllowPolicyManager> policy_manager,
    int* runtime_call_count) {
  GatewayAccessPipelineOptions options;
  options.bootstrap_config.bootstrap_revision = "gateway-bootstrap:047";
  options.bootstrap_config.allowed_protocols = {"http_unary"};
  options.security_policy_manager = std::move(policy_manager);
  options.runtime_dispatch_backend =
      [runtime_call_count](const RuntimeDispatchRequest&) {
        ++(*runtime_call_count);
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::Completed;
        return result;
      };
  return options;
}

[[nodiscard]] InboundPacket make_packet() {
  InboundPacket packet;
  packet.packet_id = "req-047-policy-ok";
  packet.entry_type = "gateway";
  packet.protocol_kind = "http_unary";
  packet.peer_ref = "jwt:user://tenant-a/alice";
  packet.payload = "run";
  packet.session_hint = std::string("sess-047-policy-ok");
  packet.trace_id = std::string("trace-047-policy-ok");
  return packet;
}

void submit_uses_infra_policy_manager_and_preserves_projected_query_fields() {
  int runtime_call_count = 0;
  auto policy_manager = std::make_shared<StaticAllowPolicyManager>();

  auto gateway = dasall::access::create_gateway_access_gateway(
      make_base_options(policy_manager, &runtime_call_count));
  assert_true(gateway != nullptr,
              "policy evaluator integration should build a concrete gateway");
  assert_true(gateway->init(),
              "policy evaluator integration should initialize the gateway");

  const auto result = gateway->submit(make_packet());
  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(result.disposition),
               "policy evaluator integration should allow submit through the production seam");
  assert_equal(1,
               runtime_call_count,
               "policy evaluator integration should reach runtime when policy manager allows");
  assert_true(policy_manager->last_query.has_value(),
              "policy evaluator integration should project a policy query to infra/policy");

  const auto& query = *policy_manager->last_query;
  assert_equal(std::string("access.gateway.http_unary"),
               query.module,
               "policy query should preserve entry/protocol channel in module");
  assert_equal(std::string("submit"),
               query.operation,
               "policy query should preserve operation");
  assert_equal(std::string("entry"),
               query.target_type,
               "policy query should preserve target type");
  assert_equal(std::string("gateway"),
               query.target_ref,
               "policy query should preserve target ref");
  assert_equal(std::string("user://tenant-a/alice"),
               query.actor_ref,
               "policy query should preserve actor ref");
  assert_equal(std::string("req-047-policy-ok"),
               query.request_id,
               "policy query should preserve request id");
  assert_equal(std::string("sess-047-policy-ok"),
               query.session_id,
               "policy query should preserve session id");
  assert_equal(std::string("trace-047-policy-ok"),
               query.trace_id,
               "policy query should preserve trace id");
  assert_equal(std::string("unknown"),
               query.profile_id,
               "policy query should default missing fingerprint projection to unknown profile");
}

}  // namespace

int main() {
  try {
    submit_uses_infra_policy_manager_and_preserves_projected_query_fields();
  } catch (const std::exception& ex) {
    std::cerr << "[AccessPolicyEvaluatorIntegrationTest] FAILED: "
              << ex.what() << '\n';
    return 1;
  }

  return 0;
}