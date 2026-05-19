#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "context/RetrievalEvidenceRef.h"
#include "error/ErrorInfo.h"

namespace dasall::knowledge {

namespace detail {

[[nodiscard]] inline bool has_unique_values(const std::vector<std::string>& values) {
	std::vector<std::string> sorted_values = values;
	std::sort(sorted_values.begin(), sorted_values.end());
	return std::adjacent_find(sorted_values.begin(), sorted_values.end()) == sorted_values.end();
}

[[nodiscard]] inline bool has_error_shape(
		const std::optional<dasall::contracts::ErrorInfo>& error) {
	if (!error.has_value()) {
		return true;
	}

	return error->failure_type.has_value() && error->retryable.has_value() &&
				 error->safe_to_replan.has_value() && error->details.code.has_value() &&
				 !error->details.message.empty() && !error->details.stage.empty() &&
				 !error->source_ref.ref_type.empty() && !error->source_ref.ref_id.empty();
}

}  // namespace detail

enum class KnowledgeQueryKind : std::uint8_t {
	FactLookup = 0,
	ProcedureLookup = 1,
	DiagnosticContext = 2,
	PolicyEvidence = 3,
	MultiHop = 4,
};

enum class RetrievalMode : std::uint8_t {
	LexicalOnly = 0,
	DenseOnly = 1,
	Hybrid = 2,
};

enum class FreshnessState : std::uint8_t {
	Fresh = 0,
	StaleAllowed = 1,
	StaleRejected = 2,
	Unknown = 3,
};

enum class TrustLevel : std::uint8_t {
	Trusted = 0,
	Quarantined = 1,
	Unregistered = 2,
};

enum class AuthorityLevel : std::uint8_t {
	Normative = 0,
	Reference = 1,
	Advisory = 2,
};

enum class SourceKind : std::uint8_t {
	File = 0,
	ConfigSnapshot = 1,
	CuratedBundle = 2,
};

enum class SourceFormat : std::uint8_t {
	Markdown = 0,
	Yaml = 1,
	Text = 2,
};

enum class RefreshStatus : std::uint8_t {
	Accepted = 0,
	Busy = 1,
	Completed = 2,
	Failed = 3,
};

enum class HealthState : std::uint8_t {
	Unknown = 0,
	Healthy = 1,
	Degraded = 2,
	Unhealthy = 3,
};

struct KnowledgeQuery {
	std::string request_id;
	std::optional<std::string> profile_id;
	std::optional<std::string> session_id;
	std::optional<std::string> goal_id;
	std::string query_text;
	KnowledgeQueryKind query_kind = KnowledgeQueryKind::FactLookup;
	std::vector<std::string> domain_tags;
	std::vector<std::string> allowed_corpora;
	std::optional<std::string> latest_observation_digest_summary;
	std::optional<std::string> belief_state_summary;
	std::size_t top_k = 8U;
	std::size_t max_context_projection_items = 6U;
	bool allow_stale = false;
	std::size_t retrieval_evidence_budget_hint = 0U;

	[[nodiscard]] bool has_consistent_values() const {
		return !request_id.empty() && !query_text.empty() && top_k > 0U &&
					 max_context_projection_items > 0U &&
					 detail::has_unique_values(domain_tags) &&
					 detail::has_unique_values(allowed_corpora);
	}
};

struct EvidenceSlice {
	std::string evidence_id;
	std::string snippet;
	std::string citation_ref;
	float confidence = 0.0F;
	FreshnessState freshness = FreshnessState::Unknown;
	std::vector<std::string> tags;

	[[nodiscard]] bool has_consistent_values() const {
		return !evidence_id.empty() && !snippet.empty() && !citation_ref.empty() &&
					 confidence >= 0.0F && confidence <= 1.0F &&
					 detail::has_unique_values(tags);
	}
};

struct EvidenceBundle {
	std::vector<EvidenceSlice> slices;
	std::vector<std::string> context_projection;
	std::vector<std::string> omitted_sources;
	bool degraded = false;
	bool evidence_insufficient = false;
	std::string coverage_notes;

	[[nodiscard]] bool has_consistent_values() const {
		return std::all_of(slices.begin(), slices.end(), [](const EvidenceSlice& slice) {
						 return slice.has_consistent_values();
					 }) &&
					 context_projection.size() <= slices.size() &&
					 detail::has_unique_values(omitted_sources);
	}
};

struct CorpusDescriptor {
	std::string corpus_id;
	std::string display_name;
	std::string source_uri;
	TrustLevel trust_level = TrustLevel::Trusted;
	AuthorityLevel authority_level = AuthorityLevel::Reference;
	SourceKind source_kind = SourceKind::File;
	std::vector<SourceFormat> allowed_formats{SourceFormat::Markdown};
	std::vector<std::string> include_globs;
	std::vector<std::string> exclude_globs;
	std::vector<RetrievalMode> supported_modes{RetrievalMode::LexicalOnly};
	std::string active_snapshot_id;
	std::int64_t last_updated_ms = 0;
	std::vector<std::string> tags;
	std::map<std::string, std::string> metadata;

	[[nodiscard]] bool has_consistent_values() const {
		return !corpus_id.empty() && !display_name.empty() && !source_uri.empty() &&
					 !allowed_formats.empty() && !include_globs.empty() &&
					 !supported_modes.empty() && last_updated_ms >= 0 &&
					 detail::has_unique_values(tags) && metadata.contains("baseline_class") &&
					 metadata.contains("owner_module") && metadata.contains("refresh_strategy") &&
					 metadata.contains("default_language");
	}
};

struct KnowledgeConfigSnapshot {
	bool knowledge_enabled = false;
	bool vector_enabled = false;
	RetrievalMode retrieval_mode_default = RetrievalMode::LexicalOnly;
	std::string profile_id;
	std::size_t evidence_budget_tokens = 0U;
	std::size_t max_context_projection_items = 0U;
	std::int64_t catalog_refresh_interval_ms = 0;
	std::int64_t catalog_expire_after_ms = 0;
	bool allow_stale_read = false;
	std::int64_t failure_backoff_ms = 0;
	std::int64_t request_deadline_ms = 0;
	bool allow_budget_degrade = false;
	std::size_t max_parallel_recall = 1U;
	std::int64_t sparse_recall_timeout_ms = 0;
	std::int64_t dense_recall_timeout_ms = 0;
	std::int64_t ingest_timeout_ms = 0;

	[[nodiscard]] bool has_consistent_values() const {
		if (!knowledge_enabled) {
			return retrieval_mode_default == RetrievalMode::LexicalOnly;
		}

		if (!vector_enabled && retrieval_mode_default != RetrievalMode::LexicalOnly) {
			return false;
		}

		return evidence_budget_tokens > 0U && max_context_projection_items > 0U &&
					 catalog_refresh_interval_ms > 0 &&
					 catalog_expire_after_ms >= catalog_refresh_interval_ms &&
					 failure_backoff_ms >= 0 && request_deadline_ms > 0 &&
					 max_parallel_recall > 0U && sparse_recall_timeout_ms > 0 &&
					 dense_recall_timeout_ms > 0 && ingest_timeout_ms > 0;
	}
};

struct KnowledgeRetrieveResult {
	bool ok = false;
	RetrievalMode mode = RetrievalMode::LexicalOnly;
	std::optional<EvidenceBundle> evidence;
	std::vector<dasall::contracts::RetrievalEvidenceRef> retrieval_evidence_refs;
	std::optional<dasall::contracts::ErrorInfo> error;

	[[nodiscard]] bool has_consistent_values() const {
		if (ok && error.has_value()) {
			return false;
		}

		if (evidence.has_value() &&
				retrieval_evidence_refs.size() > evidence->slices.size()) {
			return false;
		}

		return (!evidence.has_value() || evidence->has_consistent_values()) &&
					 std::all_of(retrieval_evidence_refs.begin(),
								retrieval_evidence_refs.end(),
								[](const dasall::contracts::RetrievalEvidenceRef& ref) {
									return ref.has_consistent_values();
								}) &&
					 detail::has_error_shape(error) && (ok || error.has_value());
	}
};

struct CorpusChangeSet {
	std::vector<std::string> added_sources;
	std::vector<std::string> updated_sources;
	std::vector<std::string> removed_sources;

	[[nodiscard]] bool has_consistent_values() const {
		return detail::has_unique_values(added_sources) &&
					 detail::has_unique_values(updated_sources) &&
					 detail::has_unique_values(removed_sources);
	}
};

struct RefreshResult {
	RefreshStatus status = RefreshStatus::Failed;
	std::string refresh_id;
	std::optional<dasall::contracts::ErrorInfo> error;

	[[nodiscard]] bool has_consistent_values() const {
		if (!detail::has_error_shape(error)) {
			return false;
		}

		switch (status) {
			case RefreshStatus::Accepted:
				return !refresh_id.empty() && !error.has_value();
			case RefreshStatus::Busy:
				return true;
			case RefreshStatus::Completed:
				return !refresh_id.empty() && !error.has_value();
			case RefreshStatus::Failed:
				return error.has_value();
		}

		return false;
	}
};

struct KnowledgeHealthSnapshot {
	HealthState state = HealthState::Unknown;
	std::string active_snapshot_id;
	FreshnessState freshness_state = FreshnessState::Unknown;
	bool vector_backend_available = false;
	bool last_known_good_available = false;
	std::uint64_t degraded_return_count = 0U;
	bool refresh_in_flight = false;
	std::optional<RefreshStatus> last_refresh_status;
	std::vector<std::string> reason_codes;

	[[nodiscard]] bool has_consistent_values() const {
		if (!detail::has_unique_values(reason_codes)) {
			return false;
		}

		if (last_refresh_status.has_value() &&
				*last_refresh_status == RefreshStatus::Busy) {
			return false;
		}

		if (state == HealthState::Healthy) {
			return !active_snapshot_id.empty() &&
					 freshness_state == FreshnessState::Fresh &&
					 vector_backend_available && degraded_return_count == 0U &&
					 reason_codes.empty();
		}

		return true;
	}
};

}  // namespace dasall::knowledge