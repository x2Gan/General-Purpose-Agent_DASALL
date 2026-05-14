#include "AccessConfigAdapter.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "RuntimePolicySnapshot.h"

namespace dasall::access {

namespace {

[[nodiscard]] bool has_no_empty_values(const std::vector<std::string>& values) {
	return std::all_of(values.begin(), values.end(), [](const std::string& value) {
		return !value.empty();
	});
}

[[nodiscard]] bool has_unique_values(const std::vector<std::string>& values) {
	std::vector<std::string> sorted_values = values;
	std::sort(sorted_values.begin(), sorted_values.end());
	return std::adjacent_find(sorted_values.begin(), sorted_values.end()) ==
				 sorted_values.end();
}

[[nodiscard]] bool has_consistent_bootstrap_config(
		const AccessBootstrapConfig& bootstrap_config) {
	return !bootstrap_config.bootstrap_revision.empty() &&
				 !bootstrap_config.entry_type.empty() &&
				 !bootstrap_config.allowed_protocols.empty() &&
				 has_no_empty_values(bootstrap_config.allowed_protocols) &&
				 has_unique_values(bootstrap_config.allowed_protocols) &&
				 !bootstrap_config.peer_auth_mode.empty() &&
				 bootstrap_config.idempotency_window_ms > 0 &&
				 bootstrap_config.max_inflight_requests > 0 &&
				 bootstrap_config.dispatch_deadline_ms > 0 &&
				 bootstrap_config.result_replay_ttl_ms > 0 &&
				 bootstrap_config.stream_heartbeat_ms > 0 &&
				 bootstrap_config.slow_consumer_max_buffer > 0 &&
				 bootstrap_config.drain_timeout_ms > 0 &&
				 bootstrap_config.max_payload_bytes > 0 &&
				 bootstrap_config.max_user_input_bytes > 0 &&
				 !bootstrap_config.session_id_mode.empty() &&
				 has_no_empty_values(bootstrap_config.trusted_local_subjects) &&
				 has_unique_values(bootstrap_config.trusted_local_subjects) &&
				 has_no_empty_values(bootstrap_config.cors_allowed_origins) &&
				 has_unique_values(bootstrap_config.cors_allowed_origins);
}

[[nodiscard]] bool same_fingerprint(const SnapshotVersionFingerprint& lhs,
																		const SnapshotVersionFingerprint& rhs) {
	return lhs.bootstrap_revision == rhs.bootstrap_revision &&
				 lhs.effective_profile_id == rhs.effective_profile_id &&
				 lhs.runtime_policy_generation == rhs.runtime_policy_generation;
}

[[nodiscard]] int clamp_int64_to_positive_int(const std::int64_t value) {
	if (value <= 0) {
		return 0;
	}

	if (value > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
		return std::numeric_limits<int>::max();
	}

	return static_cast<int>(value);
}

[[nodiscard]] int clamp_uint64_to_positive_int(const std::uint64_t value) {
	if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
		return std::numeric_limits<int>::max();
	}

	return static_cast<int>(value);
}

[[nodiscard]] std::string format_trace_sample_ratio(const double value) {
	std::ostringstream stream;
	stream << std::fixed << std::setprecision(3) << value;
	std::string formatted = stream.str();
	while (!formatted.empty() && formatted.back() == '0') {
		formatted.pop_back();
	}
	if (!formatted.empty() && formatted.back() == '.') {
		formatted.pop_back();
	}
	return formatted.empty() ? std::string("0") : formatted;
}

[[nodiscard]] std::string build_runtime_budget_profile(
		const profiles::RuntimePolicySnapshot& snapshot) {
	const auto& runtime_budget = snapshot.runtime_budget();
	return std::string("tokens:") +
				 std::to_string(runtime_budget.max_tokens.value_or(0U)) + ";turns:" +
				 std::to_string(runtime_budget.max_turns.value_or(0U)) + ";tool_calls:" +
				 std::to_string(runtime_budget.max_tool_calls.value_or(0U)) + ";latency:" +
				 std::to_string(runtime_budget.max_latency_ms.value_or(0U)) + ";replan:" +
				 std::to_string(runtime_budget.max_replan_count.value_or(0U));
}

[[nodiscard]] std::string build_timeout_policy_profile(
		const profiles::RuntimePolicySnapshot& snapshot) {
	const auto lane_string = [](const profiles::TimeoutBudget& budget) {
		return std::to_string(budget.timeout_ms) + "/" +
					 std::to_string(budget.retry_budget) + "/" +
					 std::to_string(budget.circuit_breaker_threshold);
	};

	const auto& timeout_policy = snapshot.timeout_policy();
	return std::string("llm:") + lane_string(timeout_policy.llm) + ";tool:" +
				 lane_string(timeout_policy.tool) + ";mcp:" +
				 lane_string(timeout_policy.mcp) + ";workflow:" +
				 lane_string(timeout_policy.workflow);
}

[[nodiscard]] std::string security_default_effect(
		const profiles::RuntimePolicySnapshot& snapshot) {
	return snapshot.execution_policy().safe_mode_enabled ||
								 snapshot.execution_policy().requires_high_risk_confirmation
						 ? std::string("deny")
						 : std::string("allow");
}

[[nodiscard]] int tightened_dispatch_deadline_ms(
		const AccessBootstrapConfig& bootstrap_config,
		const profiles::RuntimePolicySnapshot& snapshot) {
	const int runtime_latency_ms = clamp_uint64_to_positive_int(
			snapshot.runtime_budget().max_latency_ms.value_or(
					static_cast<std::uint32_t>(bootstrap_config.dispatch_deadline_ms)));
	const int workflow_timeout_ms =
			clamp_int64_to_positive_int(snapshot.timeout_policy().workflow.timeout_ms);

	return std::min({bootstrap_config.dispatch_deadline_ms,
									 runtime_latency_ms,
									 workflow_timeout_ms});
}

[[nodiscard]] int tightened_drain_timeout_ms(
		const AccessBootstrapConfig& bootstrap_config,
		const profiles::RuntimePolicySnapshot& snapshot) {
	const int workflow_timeout_ms =
			clamp_int64_to_positive_int(snapshot.timeout_policy().workflow.timeout_ms);
	return std::min(bootstrap_config.drain_timeout_ms, workflow_timeout_ms);
}

}  // namespace

AccessConfigProjectionResult AccessConfigAdapter::project(
		const AccessBootstrapConfig& bootstrap_config,
		const profiles::RuntimePolicySnapshot& snapshot) const {
	if (!has_consistent_bootstrap_config(bootstrap_config)) {
		return AccessConfigProjectionResult{
				.projection = std::nullopt,
				.error = "access bootstrap config is inconsistent",
		};
	}

	if (!snapshot.has_consistent_values()) {
		return AccessConfigProjectionResult{
				.projection = std::nullopt,
				.error = "runtime policy snapshot is inconsistent",
		};
	}

	const auto fingerprint = snapshot_fingerprint(bootstrap_config, snapshot);

	std::lock_guard<std::mutex> guard(cache_mutex_);
	if (cached_projection_.has_value() && cached_fingerprint_.has_value() &&
			same_fingerprint(*cached_fingerprint_, fingerprint)) {
		return AccessConfigProjectionResult{
				.projection = *cached_projection_,
				.error = {},
		};
	}

	auto result = project_uncached(bootstrap_config, snapshot);
	if (!result.ok()) {
		return result;
	}

	cached_fingerprint_ = fingerprint;
	cached_projection_ = result.projection;
	last_known_good_projection_ = result.projection;
	return result;
}

std::optional<AccessConfigProjection> AccessConfigAdapter::last_known_good_projection() const {
	std::lock_guard<std::mutex> guard(cache_mutex_);
	return last_known_good_projection_;
}

SnapshotVersionFingerprint AccessConfigAdapter::snapshot_fingerprint(
		const AccessBootstrapConfig& bootstrap_config,
		const profiles::RuntimePolicySnapshot& snapshot) const {
	return SnapshotVersionFingerprint{
			.bootstrap_revision = bootstrap_config.bootstrap_revision,
			.effective_profile_id = snapshot.effective_profile_id(),
			.runtime_policy_generation =
					snapshot.generation() >
									static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
							? std::numeric_limits<std::int64_t>::max()
							: static_cast<std::int64_t>(snapshot.generation()),
	};
}

bool AccessConfigAdapter::is_snapshot_current(
		const SnapshotVersionFingerprint& fingerprint,
		const AccessBootstrapConfig& bootstrap_config,
		const profiles::RuntimePolicySnapshot& snapshot) const {
	return same_fingerprint(fingerprint,
													snapshot_fingerprint(bootstrap_config, snapshot));
}

AccessConfigProjectionResult AccessConfigAdapter::project_uncached(
		const AccessBootstrapConfig& bootstrap_config,
		const profiles::RuntimePolicySnapshot& snapshot) const {
	const std::string effective_security_default = security_default_effect(snapshot);
	const int dispatch_deadline_ms =
			tightened_dispatch_deadline_ms(bootstrap_config, snapshot);
	const int drain_timeout_ms = tightened_drain_timeout_ms(bootstrap_config, snapshot);
	const int max_user_input_bytes =
			std::min(bootstrap_config.max_user_input_bytes,
							 bootstrap_config.max_payload_bytes);

	AccessConfigProjection projection;
	projection.fingerprint = snapshot_fingerprint(bootstrap_config, snapshot);
	projection.auth_view = AccessAuthView{
			.peer_auth_mode = bootstrap_config.peer_auth_mode,
			.auth_provider_ref = bootstrap_config.auth_provider_ref,
			.trusted_local_subjects = bootstrap_config.trusted_local_subjects,
			.strict_auth_required =
					bootstrap_config.peer_auth_mode == "strict" ||
					effective_security_default == "deny",
	};
	projection.admission_view = AccessAdmissionView{
			.idempotency_window_ms = bootstrap_config.idempotency_window_ms,
			.max_inflight_requests = bootstrap_config.max_inflight_requests,
			.dispatch_deadline_ms = dispatch_deadline_ms,
			.default_deny = effective_security_default == "deny",
	};
	projection.publish_view = AccessPublishView{
			.result_replay_ttl_ms = bootstrap_config.result_replay_ttl_ms,
			.stream_heartbeat_ms = bootstrap_config.stream_heartbeat_ms,
			.slow_consumer_max_buffer = bootstrap_config.slow_consumer_max_buffer,
			.drain_timeout_ms = drain_timeout_ms,
			.max_payload_bytes = bootstrap_config.max_payload_bytes,
			.max_user_input_bytes = max_user_input_bytes,
			.cors_allowed_origins = bootstrap_config.cors_allowed_origins,
	};
	projection.runtime_governance_view = AccessRuntimeGovernanceView{
			.runtime_budget_profile = build_runtime_budget_profile(snapshot),
			.timeout_policy_profile = build_timeout_policy_profile(snapshot),
			.ops_log_level = snapshot.ops_policy().log_level,
			.ops_trace_sample_ratio =
					format_trace_sample_ratio(snapshot.ops_policy().trace_sample_ratio),
			.remote_diagnostics_enabled =
					snapshot.ops_policy().remote_diagnostics_enabled,
			.security_default_effect = effective_security_default,
			.diag_pull_enabled = snapshot.ops_policy().remote_diagnostics_enabled,
	};

	return AccessConfigProjectionResult{
			.projection = std::move(projection),
			.error = {},
	};
}

}  // namespace dasall::access
