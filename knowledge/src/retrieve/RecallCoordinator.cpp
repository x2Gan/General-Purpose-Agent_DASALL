#include "retrieve/RecallCoordinator.h"

#include <algorithm>
#include <utility>

#include "KnowledgeErrors.h"

namespace dasall::knowledge::retrieve {

namespace {

void append_unique(std::vector<std::string>& values, std::string value) {
  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(std::move(value));
  }
}

[[nodiscard]] SparseRetrieveResult make_sparse_failure(KnowledgeErrorCode code,
                                                       std::string message,
                                                       std::string ref_id) {
  SparseRetrieveResult result;
  result.ok = false;
  result.error = make_knowledge_error_info(code, std::move(message),
                                           "recall_coordinator.sparse_lane",
                                           std::move(ref_id));
  return result;
}

[[nodiscard]] DenseRecallResult make_dense_failure(std::vector<std::string> failure_reason_codes,
                                                   std::vector<std::string> warnings = {}) {
  DenseRecallResult result;
  result.ok = false;
  result.warnings = std::move(warnings);
  result.failure_reason_codes = std::move(failure_reason_codes);
  return result;
}

[[nodiscard]] std::string decorate_lane_reason(std::string_view lane, std::string_view reason_code) {
  return std::string(lane) + "_" + std::string(reason_code);
}

[[nodiscard]] std::string sparse_failure_reason_code(
    const std::optional<dasall::contracts::ErrorInfo>& error) {
  if (error.has_value() && error->details.code.has_value()) {
    return std::string(knowledge_error_code_name(
        static_cast<KnowledgeErrorCode>(*error->details.code)));
  }

  return "unspecified";
}

void append_warnings(std::vector<std::string>& warnings,
                     const std::vector<std::string>& additional_warnings) {
  for (const auto& warning : additional_warnings) {
    append_unique(warnings, warning);
  }
}

void clear_candidates_for_failure(RecallCandidateSet& candidates) {
  candidates.sparse_hits.clear();
  candidates.dense_hits.clear();
  candidates.sparse_succeeded = false;
  candidates.dense_succeeded = false;
  candidates.degraded = false;
}

}  // namespace

bool RecallRequest::has_consistent_values() const {
  return normalized_query.has_consistent_values() && plan.has_consistent_values() &&
         (!required_language.has_value() || !required_language->empty());
}

bool RecallCoordinatorResult::has_consistent_values() const {
  if (!candidates.has_consistent_values() || !detail::has_unique_values(failure_reason_codes)) {
    return false;
  }

  if (ok) {
    return failure_reason_codes.empty();
  }

  return !failure_reason_codes.empty() && !candidates.sparse_succeeded &&
         !candidates.dense_succeeded;
}

bool RecallCoordinatorPolicy::has_consistent_values() const {
  return max_parallel_recall > 0U && sparse_lane_timeout_ms > 0 && dense_lane_timeout_ms > 0;
}

RecallCoordinator::RecallCoordinator(RecallCoordinatorDeps deps,
                                     RecallCoordinatorPolicy policy)
    : deps_(std::move(deps)),
      policy_(policy) {}

RecallCoordinatorResult RecallCoordinator::recall(const RecallRequest& request) const {
  RecallCoordinatorResult result;
  if (!request.has_consistent_values()) {
    result.ok = false;
    result.failure_reason_codes = {"recall_request_inconsistent"};
    return result;
  }

  if (!policy_.has_consistent_values()) {
    result.ok = false;
    result.failure_reason_codes = {"recall_policy_inconsistent"};
    return result;
  }

  const bool request_sparse_lane = request.plan.mode != RetrievalMode::DenseOnly;
  const bool request_dense_lane = request.plan.mode != RetrievalMode::LexicalOnly;

  if (request_sparse_lane) {
    const auto sparse_result = run_sparse_lane(request);
    append_warnings(result.candidates.warnings, sparse_result.warnings);

    if (sparse_result.ok) {
      result.candidates.sparse_hits = sparse_result.hits;
      result.candidates.sparse_succeeded = true;
    } else {
      const auto reason_code = decorate_lane_reason(
          "sparse", sparse_failure_reason_code(sparse_result.error));
      append_unique(result.failure_reason_codes, reason_code);
      append_unique(result.candidates.warnings, reason_code);
    }
  }

  if (request_dense_lane) {
    const auto dense_result = run_dense_lane(request);
    append_warnings(result.candidates.warnings, dense_result.warnings);

    if (dense_result.ok) {
      result.candidates.dense_hits = dense_result.hits;
      result.candidates.dense_succeeded = true;
    } else {
      for (const auto& reason_code : dense_result.failure_reason_codes) {
        const auto decorated_reason = decorate_lane_reason("dense", reason_code);
        append_unique(result.failure_reason_codes, decorated_reason);
        append_unique(result.candidates.warnings, decorated_reason);
      }
    }
  }

  const bool any_success = result.candidates.sparse_succeeded || result.candidates.dense_succeeded;
  const bool all_requested_succeeded =
      (!request_sparse_lane || result.candidates.sparse_succeeded) &&
      (!request_dense_lane || result.candidates.dense_succeeded);

  if (all_requested_succeeded) {
    result.ok = true;
    return result;
  }

  if (request_sparse_lane && request_dense_lane && any_success && request.plan.allow_partial_results) {
    result.ok = true;
    result.candidates.degraded = true;
    result.failure_reason_codes.clear();
    return result;
  }

  clear_candidates_for_failure(result.candidates);
  result.ok = false;
  if (result.failure_reason_codes.empty()) {
    result.failure_reason_codes = {"all_recall_lanes_failed"};
  }
  return result;
}

SparseRetrieveResult RecallCoordinator::run_sparse_lane(const RecallRequest& request) const {
  if (!deps_.sparse_lane) {
    return make_sparse_failure(KnowledgeErrorCode::IndexUnavailable,
                               "sparse lane is unavailable",
                               "sparse_lane_unavailable");
  }

  SparseRetrieveRequest sparse_request;
  sparse_request.normalized_query = request.normalized_query;
  sparse_request.plan = request.plan;
  sparse_request.required_language = request.required_language;
  if (!sparse_request.has_consistent_values()) {
    return make_sparse_failure(KnowledgeErrorCode::QueryValidationFailed,
                               "sparse lane request is inconsistent",
                               "sparse_lane_request_inconsistent");
  }

  auto sparse_result = deps_.sparse_lane(sparse_request);
  if (!sparse_result.has_consistent_values()) {
    return make_sparse_failure(KnowledgeErrorCode::InternalError,
                               "sparse lane result is inconsistent",
                               "sparse_lane_result_inconsistent");
  }

  return sparse_result;
}

DenseRecallResult RecallCoordinator::run_dense_lane(const RecallRequest& request) const {
  if (!deps_.dense_lane) {
    return make_dense_failure({"lane_unavailable"});
  }

  DenseRecallRequest dense_request;
  dense_request.normalized_query = request.normalized_query;
  dense_request.plan = request.plan;
  dense_request.required_language = request.required_language;
  if (!dense_request.has_consistent_values()) {
    return make_dense_failure({"request_inconsistent"});
  }

  auto dense_result = deps_.dense_lane(dense_request);
  if (!dense_result.has_consistent_values()) {
    return make_dense_failure({"result_inconsistent"});
  }

  return dense_result;
}

}  // namespace dasall::knowledge::retrieve