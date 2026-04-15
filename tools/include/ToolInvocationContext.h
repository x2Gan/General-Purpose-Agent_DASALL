#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dasall::profiles {

class RuntimePolicySnapshot;

}  // namespace dasall::profiles

namespace dasall::tools {

struct ToolTraceContext {
	std::optional<std::string> trace_id;
	std::optional<std::string> span_id;
	std::optional<std::string> parent_span_id;

	[[nodiscard]] bool has_root_trace() const {
		return trace_id.has_value() && !trace_id->empty();
	}
};

struct ToolConfirmationFact {
	std::optional<std::string> confirmation_id;
	std::optional<std::string> subject_ref;
	std::optional<std::string> proof_type;
	std::optional<std::int64_t> confirmed_at_ms;

	[[nodiscard]] bool has_consistent_values() const {
		return confirmation_id.has_value() && !confirmation_id->empty() &&
					 subject_ref.has_value() && !subject_ref->empty() && proof_type.has_value() &&
					 !proof_type->empty() && confirmed_at_ms.has_value() && *confirmed_at_ms > 0;
	}
};

// Invoke-scoped inputs supplied by runtime. This surface stays limited to
// caller identity, profile snapshot reference, trace propagation, and
// confirmation evidence so tools does not absorb prompt, memory, or recovery
// control responsibilities.
struct ToolInvocationContext {
	std::optional<std::string> caller_domain;
	std::optional<std::string> session_id;
	const profiles::RuntimePolicySnapshot* profile_snapshot = nullptr;
	ToolTraceContext trace;
	std::optional<std::vector<ToolConfirmationFact>> confirmation_facts;

	[[nodiscard]] bool has_profile_snapshot() const {
		return profile_snapshot != nullptr;
	}
};

}  // namespace dasall::tools