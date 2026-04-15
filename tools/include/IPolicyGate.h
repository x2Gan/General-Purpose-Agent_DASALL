#pragma once

#include <optional>
#include <string>
#include <vector>

namespace dasall::tools {

enum class ToolAdmissionEffect {
	deny,
	allow,
};

struct ToolPolicyView {
	std::string effective_profile_id;
	bool safe_mode_enabled = true;
	bool high_risk_confirmation_required = true;
	std::string audit_level;
	std::vector<std::string> allowed_tool_domains;
	std::vector<std::string> tool_visibility_rules;
};

struct ToolAdmissionRequest {
	std::string tool_name;
	std::vector<std::string> required_scopes;
	std::optional<std::string> caller_domain;
	bool high_risk = false;
	bool confirmation_present = false;
	bool route_proven = false;
};

struct ToolAdmissionDecision {
	ToolAdmissionEffect effect = ToolAdmissionEffect::deny;
	std::string reason_code;
	bool confirmation_required = false;
	bool retryable = false;

	[[nodiscard]] bool allowed() const {
		return effect == ToolAdmissionEffect::allow;
	}
};

class IPolicyGate {
 public:
	virtual ~IPolicyGate() = default;

	virtual ToolAdmissionDecision evaluate(
			const ToolAdmissionRequest& request,
			const ToolPolicyView& policy_view) = 0;
};

}  // namespace dasall::tools