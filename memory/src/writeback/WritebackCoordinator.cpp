#include "writeback/WritebackCoordinator.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "memory/ExperienceMemory.h"
#include "memory/MemoryFact.h"
#include "memory/Session.h"
#include "memory/SummaryMemory.h"
#include "memory/Turn.h"

namespace dasall::memory {
namespace {

constexpr int kMaxCoreTransactionAttempts = 3;

[[nodiscard]] std::int64_t current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void append_warning_once(std::vector<std::string>& warnings,
                         std::string warning) {
  if (std::find(warnings.begin(), warnings.end(), warning) == warnings.end()) {
    warnings.push_back(std::move(warning));
  }
}

[[nodiscard]] bool is_retryable(
    const std::optional<contracts::ResultCode>& result_code) {
  return result_code == contracts::ResultCode::RuntimeRetryExhausted;
}

[[nodiscard]] std::string sanitize_id_component(std::string_view value) {
  std::string sanitized;
  sanitized.reserve(value.size());

  for (const unsigned char ch : value) {
    if (std::isalnum(ch) != 0) {
      sanitized.push_back(static_cast<char>(std::tolower(ch)));
    } else if (!sanitized.empty() && sanitized.back() != '-') {
      sanitized.push_back('-');
    }
  }

  while (!sanitized.empty() && sanitized.back() == '-') {
    sanitized.pop_back();
  }

  return sanitized.empty() ? std::string{"memory"} : sanitized;
}

[[nodiscard]] std::string build_generated_id(std::string_view prefix,
                                             std::string_view session_id,
                                             std::string_view turn_id,
                                             int index = -1) {
  std::string generated = std::string{prefix} + "-" +
                          sanitize_id_component(session_id) + "-" +
                          sanitize_id_component(turn_id);
  if (index >= 0) {
    generated += "-" + std::to_string(index);
  }
  return generated;
}

[[nodiscard]] WritebackResult make_invalid_result(
    const char* warning_key,
    contracts::ResultCode code = contracts::ResultCode::ValidationFieldMissing) {
  WritebackResult result;
  result.result_code = code;
  result.degraded = true;
  result.warnings.push_back(warning_key);
  return result;
}

[[nodiscard]] WritebackResult make_core_failure_result(
    const std::optional<contracts::ResultCode>& code,
    const char* warning_key) {
  WritebackResult result;
  result.result_code = code.value_or(contracts::ResultCode::RuntimeRetryExhausted);
  result.degraded = true;
  result.retryable_storage_failure = is_retryable(code);
  append_warning_once(result.warnings, warning_key);
  if (result.retryable_storage_failure) {
    append_warning_once(result.warnings, "retryable_storage_failure");
  }
  return result;
}

[[nodiscard]] contracts::Session build_session_record(
    const MemoryWritebackRequest& request) {
  contracts::Session session;
  session.session_id = request.session_id;
  session.turn_ids = std::vector<std::string>{};
  session.created_at = request.turn.created_at.value_or(current_time_millis());
  session.last_active_at = session.created_at;
  return session;
}

[[nodiscard]] contracts::Turn normalize_turn(
    const MemoryWritebackRequest& request) {
  contracts::Turn turn = request.turn;
  if (!turn.session_id.has_value() || turn.session_id->empty()) {
    turn.session_id = request.session_id;
  }
  if (!turn.created_at.has_value() || *turn.created_at <= 0) {
    turn.created_at = current_time_millis();
  }
  return turn;
}

[[nodiscard]] std::optional<contracts::SummaryMemory> normalize_summary_candidate(
    const MemoryWritebackRequest& request) {
  if (!request.summary_candidate.has_value()) {
    return std::nullopt;
  }

  contracts::SummaryMemory summary = *request.summary_candidate;
  if (!summary.summary_id.has_value() || summary.summary_id->empty()) {
    summary.summary_id = build_generated_id(
        "summary", request.session_id, request.turn.turn_id.value_or("turn"));
  }
  if (!summary.session_id.has_value() || summary.session_id->empty()) {
    summary.session_id = request.session_id;
  }
  if (!summary.source_turn_ids.has_value() || summary.source_turn_ids->empty()) {
    summary.source_turn_ids = std::vector<std::string>{
        request.turn.turn_id.value_or("turn")};
  }
  if (!summary.created_at.has_value() || *summary.created_at <= 0) {
    summary.created_at = request.turn.created_at.value_or(current_time_millis());
  }
  return summary;
}

[[nodiscard]] FactCandidate normalize_fact_candidate(
    const MemoryWritebackRequest& request,
    FactCandidate candidate,
    int index) {
  if (!candidate.fact.fact_id.has_value() || candidate.fact.fact_id->empty()) {
    candidate.fact.fact_id = build_generated_id(
        "fact", request.session_id, request.turn.turn_id.value_or("turn"), index);
  }
  if (!candidate.fact.session_id.has_value() || candidate.fact.session_id->empty()) {
    candidate.fact.session_id = request.session_id;
  }
  if (!candidate.fact.source_turn_ids.has_value() ||
      candidate.fact.source_turn_ids->empty()) {
    candidate.fact.source_turn_ids = std::vector<std::string>{
        request.turn.turn_id.value_or("turn")};
  }
  if (!candidate.fact.created_at.has_value() || *candidate.fact.created_at <= 0) {
    candidate.fact.created_at = request.turn.created_at.value_or(current_time_millis());
  }
  return candidate;
}

[[nodiscard]] ExperienceCandidate normalize_experience_candidate(
    const MemoryWritebackRequest& request,
    ExperienceCandidate candidate,
    int index) {
  if (!candidate.experience.experience_id.has_value() ||
      candidate.experience.experience_id->empty()) {
    candidate.experience.experience_id = build_generated_id(
        "experience", request.session_id, request.turn.turn_id.value_or("turn"), index);
  }
  if (!candidate.experience.session_id.has_value() ||
      candidate.experience.session_id->empty()) {
    candidate.experience.session_id = request.session_id;
  }
  if (!candidate.experience.source_turn_ids.has_value() ||
      candidate.experience.source_turn_ids->empty()) {
    candidate.experience.source_turn_ids = std::vector<std::string>{
        request.turn.turn_id.value_or("turn")};
  }
  if (!candidate.experience.created_at.has_value() ||
      *candidate.experience.created_at <= 0) {
    candidate.experience.created_at =
        request.turn.created_at.value_or(current_time_millis());
  }
  return candidate;
}

[[nodiscard]] std::string build_turn_vector_text(const contracts::Turn& turn) {
  std::string text = turn.user_input.value_or("");
  if (turn.agent_response.has_value() && !turn.agent_response->empty()) {
    if (!text.empty()) {
      text += "\n";
    }
    text += *turn.agent_response;
  }
  return text;
}

}  // namespace

WritebackCoordinator::WritebackCoordinator(
    ITransactionalStore& transaction_store,
    ISessionStore& session_store,
    ISummaryStore& summary_store,
    IFactStore& fact_store,
    IExperienceStore& experience_store,
    std::unique_ptr<MemoryConflictResolver> conflict_resolver,
    IWorkingMemoryBoard& working_memory_board,
    VectorMemoryIndexAdapter* vector_index,
    std::shared_ptr<std::mutex> writer_mutex)
    : transaction_store_(transaction_store),
      session_store_(session_store),
      summary_store_(summary_store),
      fact_store_(fact_store),
      experience_store_(experience_store),
      conflict_resolver_(std::move(conflict_resolver)),
      working_memory_board_(working_memory_board),
      vector_index_(vector_index),
      writer_mutex_(std::move(writer_mutex)) {}

WritebackResult WritebackCoordinator::persist(
    const MemoryWritebackRequest& request) {
  std::unique_lock<std::mutex> writer_lock;
  if (writer_mutex_) {
    writer_lock = std::unique_lock<std::mutex>(*writer_mutex_);
  }

  if (request.session_id.empty()) {
    return make_invalid_result("writeback_session_id_missing");
  }

  MemoryWritebackRequest normalized_request = request;
  normalized_request.turn = normalize_turn(request);

  if (!contracts::validate_turn_field_rules(normalized_request.turn).ok) {
    return make_invalid_result("writeback_turn_invalid");
  }

  WritebackResult prep_result;

  normalized_request.summary_candidate = normalize_summary_candidate(request);
  if (normalized_request.summary_candidate.has_value() &&
      !contracts::validate_summary_memory_field_rules(
           *normalized_request.summary_candidate)
           .ok) {
    normalized_request.summary_candidate.reset();
    prep_result.partial = true;
    append_warning_once(prep_result.warnings, "summary_candidate_rejected");
    append_warning_once(prep_result.warnings, "partial_writeback_warning");
  }

  normalized_request.fact_candidates.clear();
  for (std::size_t index = 0; index < request.fact_candidates.size(); ++index) {
    auto candidate = normalize_fact_candidate(
        request, request.fact_candidates[index], static_cast<int>(index));
    if (!contracts::validate_memory_fact_field_rules(candidate.fact).ok) {
      prep_result.partial = true;
      append_warning_once(prep_result.warnings, "fact_candidate_rejected");
      append_warning_once(prep_result.warnings, "partial_writeback_warning");
      continue;
    }
    normalized_request.fact_candidates.push_back(std::move(candidate));
  }

  normalized_request.experience_candidates.clear();
  for (std::size_t index = 0; index < request.experience_candidates.size(); ++index) {
    auto candidate = normalize_experience_candidate(
        request, request.experience_candidates[index], static_cast<int>(index));
    if (!contracts::validate_experience_memory_field_rules(candidate.experience).ok) {
      prep_result.partial = true;
      append_warning_once(prep_result.warnings, "experience_candidate_rejected");
      append_warning_once(prep_result.warnings, "partial_writeback_warning");
      continue;
    }
    normalized_request.experience_candidates.push_back(std::move(candidate));
  }

  auto result = persist_core_transaction(normalized_request);
  result.partial = result.partial || prep_result.partial;
  for (const auto& warning : prep_result.warnings) {
    append_warning_once(result.warnings, warning);
  }

  if (result.result_code.has_value()) {
    return result;
  }

  persist_derived_data(normalized_request, result);
  persist_vector_sidecar(normalized_request, result);
  update_working_board(normalized_request, result);
  return result;
}

WritebackResult WritebackCoordinator::persist_core_transaction(
    const MemoryWritebackRequest& request) {
  for (int attempt = 0; attempt < kMaxCoreTransactionAttempts; ++attempt) {
    auto transaction = transaction_store_.begin_immediate();

    const auto session_bundle =
        session_store_.load_session_bundle(SessionLoadRequest{.session_id = request.session_id,
                                                             .recent_turn_limit = 1});
    if (!session_bundle.session.session_id.has_value() ||
        session_bundle.session.session_id->empty()) {
      const auto create_result = session_store_.create_session(build_session_record(request));
      if (!create_result.ok) {
        transaction->rollback();
        if (is_retryable(create_result.result_code) &&
            attempt + 1 < kMaxCoreTransactionAttempts) {
          continue;
        }
        return make_core_failure_result(create_result.result_code,
                                        "writeback_core_transaction_failed");
      }
    }

    const auto append_result = session_store_.append_turn(request.turn);
    if (!append_result.ok) {
      transaction->rollback();
      if (is_retryable(append_result.result_code) &&
          attempt + 1 < kMaxCoreTransactionAttempts) {
        continue;
      }
      return make_core_failure_result(append_result.result_code,
                                      "writeback_core_transaction_failed");
    }

    const auto update_result = session_store_.update_session_active(
        request.session_id, request.turn.created_at.value_or(current_time_millis()));
    if (!update_result.ok) {
      transaction->rollback();
      if (is_retryable(update_result.result_code) &&
          attempt + 1 < kMaxCoreTransactionAttempts) {
        continue;
      }
      return make_core_failure_result(update_result.result_code,
                                      "writeback_core_transaction_failed");
    }

    if (request.summary_candidate.has_value()) {
      const auto summary_result = summary_store_.upsert_summary(*request.summary_candidate);
      if (!summary_result.ok) {
        transaction->rollback();
        if (is_retryable(summary_result.result_code) &&
            attempt + 1 < kMaxCoreTransactionAttempts) {
          continue;
        }
        return make_core_failure_result(summary_result.result_code,
                                        "writeback_core_transaction_failed");
      }
    }

    const auto commit_result = transaction->commit();
    if (commit_result.has_value()) {
      transaction->rollback();
      if (is_retryable(commit_result) && attempt + 1 < kMaxCoreTransactionAttempts) {
        continue;
      }
      return make_core_failure_result(commit_result,
                                      "writeback_core_transaction_failed");
    }

    WritebackResult result;
    result.persisted_turn_id = request.turn.turn_id;
    if (request.summary_candidate.has_value()) {
      result.summary_id = request.summary_candidate->summary_id;
    }
    return result;
  }

  return make_core_failure_result(contracts::ResultCode::RuntimeRetryExhausted,
                                  "writeback_core_transaction_failed");
}

void WritebackCoordinator::persist_derived_data(
    const MemoryWritebackRequest& request,
    WritebackResult& result) {
  if (!conflict_resolver_) {
    append_warning_once(result.warnings, "conflict_resolver_unwired");
  }

  if (request.fact_candidates.empty() && request.experience_candidates.empty()) {
    return;
  }

  auto derived_txn = transaction_store_.begin_immediate();

  for (const auto& candidate : request.fact_candidates) {
    const auto plan = conflict_resolver_
                          ? conflict_resolver_->resolve(candidate, request.session_id)
                          : ConflictResolutionPlan{};

    result.degraded = result.degraded || plan.degraded;
    for (const auto& warning : plan.warnings) {
      append_warning_once(result.warnings, warning);
    }
    if (!plan.conflict_records.empty()) {
      append_warning_once(result.warnings, "conflict_recorded_warning");
      result.conflicts.insert(result.conflicts.end(), plan.conflict_records.begin(),
                              plan.conflict_records.end());
    }

    if (plan.action == ConflictAction::Reject) {
      continue;
    }

    const auto insert_result = fact_store_.insert_fact(candidate.fact);
    if (!insert_result.ok) {
      result.partial = true;
      append_warning_once(result.warnings, "partial_writeback_warning");
      append_warning_once(result.warnings, "fact_write_failed");
      continue;
    }

    result.fact_ids.push_back(
        insert_result.persisted_id.value_or(candidate.fact.fact_id.value_or("")));

    if (plan.action == ConflictAction::Supersede) {
      for (const auto& record : plan.conflict_records) {
        if (record.action != ConflictAction::Supersede ||
            record.existing_fact_id.empty()) {
          continue;
        }

        const auto supersede_result = fact_store_.supersede_fact(
            record.existing_fact_id,
            candidate.fact.fact_id.value_or(insert_result.persisted_id.value_or("")));
        if (!supersede_result.ok) {
          result.partial = true;
          append_warning_once(result.warnings, "partial_writeback_warning");
          append_warning_once(result.warnings, "fact_supersede_failed");
        }
      }
    }
  }

  for (const auto& candidate : request.experience_candidates) {
    const auto insert_result = experience_store_.insert_experience(candidate.experience);
    if (!insert_result.ok) {
      result.partial = true;
      append_warning_once(result.warnings, "partial_writeback_warning");
      append_warning_once(result.warnings, "experience_write_failed");
      continue;
    }

    result.experience_ids.push_back(insert_result.persisted_id.value_or(
        candidate.experience.experience_id.value_or("")));
  }

  if (derived_txn) {
    const auto commit_result = derived_txn->commit();
    if (commit_result.has_value()) {
      result.partial = true;
      append_warning_once(result.warnings, "derived_data_commit_failed");
    }
  }
}

void WritebackCoordinator::persist_vector_sidecar(
    const MemoryWritebackRequest& request,
    WritebackResult& result) {
  if (vector_index_ == nullptr || !vector_index_->is_available()) {
    return;
  }

  const auto turn_upsert = vector_index_->upsert(VectorDocument{
      .doc_id = request.turn.turn_id.value_or("turn"),
      .doc_type = "turn",
      .text = build_turn_vector_text(request.turn),
      .embedding = {},
  });
  if (!turn_upsert.ok) {
    append_warning_once(result.warnings, "vector_sidecar_failed");
  }

  for (const auto& candidate : request.fact_candidates) {
    if (std::find(result.fact_ids.begin(), result.fact_ids.end(),
                  candidate.fact.fact_id.value_or("")) == result.fact_ids.end()) {
      continue;
    }

    const auto fact_upsert = vector_index_->upsert(VectorDocument{
        .doc_id = candidate.fact.fact_id.value_or("fact"),
        .doc_type = "fact",
        .text = candidate.fact.fact_text.value_or(""),
        .embedding = {},
    });
    if (!fact_upsert.ok) {
      append_warning_once(result.warnings, "vector_sidecar_failed");
    }
  }
}

void WritebackCoordinator::update_working_board(
    const MemoryWritebackRequest& request,
    const WritebackResult& result) {
  const auto updated_at = request.turn.created_at.value_or(current_time_millis());

  if (result.persisted_turn_id.has_value()) {
    working_memory_board_.set_slot(
        request.session_id,
        WorkingMemorySlot{
            .key = "latest_turn_id",
            .value = *result.persisted_turn_id,
            .updated_at = updated_at,
            .ttl_ms = 0,
            .source = "writeback",
        });
  }

  if (request.turn.user_input.has_value()) {
    working_memory_board_.set_slot(
        request.session_id,
        WorkingMemorySlot{
            .key = "latest_user_input",
            .value = *request.turn.user_input,
            .updated_at = updated_at,
            .ttl_ms = 0,
            .source = "writeback",
        });
  }

  if (request.turn.agent_response.has_value() && !request.turn.agent_response->empty()) {
    working_memory_board_.set_slot(
        request.session_id,
        WorkingMemorySlot{
            .key = "latest_agent_response",
            .value = *request.turn.agent_response,
            .updated_at = updated_at,
            .ttl_ms = 0,
            .source = "writeback",
        });
  }

  if (result.summary_id.has_value()) {
    working_memory_board_.set_slot(
        request.session_id,
        WorkingMemorySlot{
            .key = "latest_summary_id",
            .value = *result.summary_id,
            .updated_at = updated_at,
            .ttl_ms = 0,
            .source = "writeback",
        });
  }

  if (request.side_effect_report_ref.has_value() &&
      !request.side_effect_report_ref->empty()) {
    working_memory_board_.set_slot(
        request.session_id,
        WorkingMemorySlot{
            .key = "side_effect_report_ref",
            .value = *request.side_effect_report_ref,
            .updated_at = updated_at,
            .ttl_ms = 0,
            .source = "writeback",
        });
  }
}

}  // namespace dasall::memory