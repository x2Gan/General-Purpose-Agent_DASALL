#pragma once

#include <optional>
#include <string_view>

#include "IPolicyGate.h"

namespace dasall::tools::policy {

class ToolPolicyGate final : public tools::IPolicyGate {
 public:
  [[nodiscard]] tools::ToolAdmissionDecision evaluate(
      const tools::ToolAdmissionRequest& request,
      const tools::ToolPolicyView& policy_view) override;

 private:
  [[nodiscard]] std::optional<tools::ToolAdmissionDecision> check_policy_view(
      const tools::ToolPolicyView& policy_view) const;
  [[nodiscard]] std::optional<tools::ToolAdmissionDecision> check_allowed_domain(
      const tools::ToolAdmissionRequest& request,
      const tools::ToolPolicyView& policy_view) const;
  [[nodiscard]] std::optional<tools::ToolAdmissionDecision> check_visibility(
      const tools::ToolAdmissionRequest& request,
      const tools::ToolPolicyView& policy_view) const;
  [[nodiscard]] std::optional<tools::ToolAdmissionDecision> check_confirmation(
      const tools::ToolAdmissionRequest& request,
      const tools::ToolPolicyView& policy_view) const;
  [[nodiscard]] std::optional<tools::ToolAdmissionDecision> check_safe_mode(
      const tools::ToolAdmissionRequest& request,
      const tools::ToolPolicyView& policy_view) const;
  [[nodiscard]] bool matches_visibility_rule(
      const tools::ToolAdmissionRequest& request,
      std::string_view rule) const;
};

}  // namespace dasall::tools::policy