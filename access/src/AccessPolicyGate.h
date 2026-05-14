#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "AccessTypes.h"
#include "AuthenticatorChain.h"

namespace dasall::infra::policy {
class ISecurityPolicyManager;
}

namespace dasall::access {

// OperationTargetView 是 access action taxonomy 的最小查询载体。
// 它只包含策略求值所需的 operation/target 事实，不承载 backend 实现细节。
struct OperationTargetView {
  std::string operation;
  std::string target_type;
  std::string target_ref;

  [[nodiscard]] bool has_consistent_values() const;
};

// OverrideSourceFact 统一表达 runtime_override 来源和结构完整性。
// AccessPolicyGate 只检查这些事实，不执行真正 patch 应用动作。
struct OverrideSourceFact {
  std::string source_type;
  bool has_config_patch_metadata = false;
  bool path_op_summary_complete = false;
  bool ttl_valid = true;
  bool target_ref_present = true;

  [[nodiscard]] bool has_consistent_values() const;
};

// PolicyBackendSnapshot 是 v1 policy backend 的最小抽象输入。
// 通过它可在不引入 infra/policy 具体对象的情况下验证 fail-closed 决策路径。
struct PolicyBackendSnapshot {
  bool backend_available = true;
  bool allow_submit = true;
  bool allow_task_query = true;
  bool allow_diagnostics = false;
  bool allow_override = false;
  bool require_confirmation_for_override = false;
  std::string decision_ref = "policy://access/default";
};

// AccessPolicyEvaluationInput 收敛认证输出与入口包事实。
struct AccessPolicyEvaluationInput {
  AuthenticationOutcome authentication;
  InboundPacket packet;
  std::optional<SnapshotVersionFingerprint> snapshot_fingerprint;
};

// AccessPolicyEvaluationResult 是策略门唯一输出。
// allowed/requires_confirmation/reject_reason 必须互斥表达，禁止 silent fallback。
struct AccessPolicyEvaluationResult {
  bool allowed = false;
  bool requires_confirmation = false;
  AccessDecisionProof decision_proof;
  std::optional<std::string> reject_reason;

  [[nodiscard]] bool denied() const;
};

// AccessPolicyQuery 是 access -> policy seam 的最小查询载体。
// 它保留 subject/channel/environment/operation/target/fingerprint，
// 由后续 evaluator 决定如何投影到具体 backend。
struct AccessPolicyQuery {
  SubjectIdentity subject_identity;
  OperationTargetView operation_target;
  std::string entry_type;
  std::string protocol_kind;
  std::string request_id = "unknown";
  std::string session_id = "unknown";
  std::string trace_id = "unknown";
  std::optional<SnapshotVersionFingerprint> snapshot_fingerprint;
  bool sensitive_request = false;

  [[nodiscard]] bool has_consistent_values() const;
};

class IAccessPolicyEvaluator {
 public:
  virtual ~IAccessPolicyEvaluator() = default;

  [[nodiscard]] virtual AccessPolicyEvaluationResult evaluate(
    const AccessPolicyQuery& query) const = 0;
};

[[nodiscard]] std::shared_ptr<const IAccessPolicyEvaluator> make_infra_policy_evaluator(
    std::shared_ptr<dasall::infra::policy::ISecurityPolicyManager> policy_manager);

class AccessPolicyGate {
 public:
  AccessPolicyGate() = default;
  explicit AccessPolicyGate(std::shared_ptr<const IAccessPolicyEvaluator> policy_evaluator);

  [[nodiscard]] AccessPolicyEvaluationResult evaluate_submit(
    const AccessPolicyEvaluationInput& input) const;

  [[nodiscard]] AccessPolicyEvaluationResult evaluate_submit(
      const AccessPolicyEvaluationInput& input,
      const PolicyBackendSnapshot& backend) const;

  [[nodiscard]] AccessPolicyEvaluationResult evaluate_task_query(
    const AccessPolicyEvaluationInput& input,
    std::string_view task_ref) const;

  [[nodiscard]] AccessPolicyEvaluationResult evaluate_task_query(
      const AccessPolicyEvaluationInput& input,
      std::string_view task_ref,
      const PolicyBackendSnapshot& backend) const;

  [[nodiscard]] AccessPolicyEvaluationResult evaluate_diagnostics_request(
    const AccessPolicyEvaluationInput& input,
    std::string_view command_name) const;

  [[nodiscard]] AccessPolicyEvaluationResult evaluate_diagnostics_request(
      const AccessPolicyEvaluationInput& input,
      std::string_view command_name,
      const PolicyBackendSnapshot& backend) const;

  [[nodiscard]] AccessPolicyEvaluationResult evaluate_override_request(
    const AccessPolicyEvaluationInput& input,
    const OverrideSourceFact& source_fact) const;

  [[nodiscard]] AccessPolicyEvaluationResult evaluate_override_request(
      const AccessPolicyEvaluationInput& input,
      const OverrideSourceFact& source_fact,
      const PolicyBackendSnapshot& backend) const;

 private:
  [[nodiscard]] std::optional<AccessPolicyQuery> build_query_context(
      const AccessPolicyEvaluationInput& input,
      std::string_view operation,
      std::string_view target_type,
    std::string_view target_ref,
    bool sensitive_request) const;

  [[nodiscard]] AccessPolicyEvaluationResult evaluate_with_evaluator(
    const AccessPolicyEvaluationInput& input,
    std::string_view operation,
    std::string_view target_type,
    std::string_view target_ref,
    bool sensitive_request,
    bool require_local_trusted,
    const OverrideSourceFact* source_fact,
    const IAccessPolicyEvaluator& evaluator) const;

  std::shared_ptr<const IAccessPolicyEvaluator> policy_evaluator_;
};

}  // namespace dasall::access
