#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef DASALL_SQL_MEMORY_DIR
#define DASALL_SQL_MEMORY_DIR ""
#endif

#include "AccessTypes.h"
#include "AgentTypes.h"
#include "AsyncTaskRegistry.h"
#include "CancellationToken.h"
#include "ResultReplayCache.h"
#include "RuntimeBridge.h"
#include "checkpoint/BudgetSnapshot.h"
#include "checkpoint/Checkpoint.h"
#include "checkpoint/CheckpointBuildTypes.h"
#include "checkpoint/RecoveryRequest.h"
#include "error/ErrorInfo.h"
#include "IMemoryManager.h"
#include "observation/Observation.h"
#include "recovery/RecoveryManager.h"
#include "store/sqlite/SqliteMemoryStore.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

struct ChainIds {
  std::string request_id = "req-fullint-011";
  std::string session_id = "session-fullint-011";
  std::string trace_id = "trace-fullint-011";
  std::string actor_ref = "local://uid/1000";
  std::string goal_id = "goal-fullint-011";
  std::string receipt_id = "receipt:req-fullint-011";
  std::string checkpoint_id = "chk-fullint-011";
  std::string turn_id = "turn-fullint-011";
};

void assert_optional_string(const std::string& expected,
                            const std::optional<std::string>& actual,
                            const std::string& message) {
  assert_true(actual.has_value(), message + " should be present");
  assert_equal(expected, *actual, message);
}

void assert_context_string(const std::string& expected,
                           const std::map<std::string, std::string>& context,
                           const std::string& key,
                           const std::string& message) {
  const auto iterator = context.find(key);
  assert_true(iterator != context.end(), message + " should be present");
  assert_equal(expected, iterator->second, message);
}

std::int64_t current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::filesystem::path make_temp_database_path() {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return std::filesystem::temp_directory_path() /
         ("dasall-fullint-011-" + std::to_string(timestamp) + ".db");
}

void cleanup_database_artifacts(const std::filesystem::path& database_path) {
  (void)std::filesystem::remove(database_path);
  (void)std::filesystem::remove(database_path.string() + "-wal");
  (void)std::filesystem::remove(database_path.string() + "-shm");
}

dasall::contracts::AgentRequest make_agent_request(const ChainIds& ids) {
  return dasall::contracts::AgentRequest{
      .request_id = ids.request_id,
      .session_id = ids.session_id,
      .trace_id = ids.trace_id,
      .user_input = std::string("FULLINT-011 async recovery causality probe"),
      .request_channel = dasall::contracts::RequestChannel::Daemon,
      .created_at = current_time_millis(),
      .goal_hint = std::string("verify async recovery causality"),
      .domain_context = std::nullopt,
      .constraint_set = std::string("fullint-011"),
      .approval_policy_hint = std::nullopt,
      .runtime_budget = std::nullopt,
      .timeout_ms = 120000U,
      .deadline_at = std::nullopt,
      .priority = 1U,
      .idempotency_key = std::string("idem-fullint-011"),
      .locale = std::string("zh-CN"),
      .client_capabilities = std::string("async,status,cancel,json"),
      .tags = std::vector<std::string>{"fullint-011", "async-recovery"},
  };
}

dasall::access::RuntimeDispatchRequest make_dispatch_request(const ChainIds& ids) {
  dasall::access::RuntimeDispatchRequest request;
  request.packet.packet_id = ids.request_id;
  request.packet.entry_type = "daemon";
  request.packet.protocol_kind = "ipc_uds";
  request.packet.peer_ref = ids.actor_ref;
  request.packet.payload = "{\"prompt\":\"FULLINT-011 async recovery causality probe\"}";
  request.packet.trace_id = ids.trace_id;
  request.packet.session_hint = ids.session_id;
  request.packet.async_preferred = true;
  request.subject_identity.actor_ref = ids.actor_ref;
  request.subject_identity.subject_type = "user";
  request.subject_identity.auth_method = "local_trusted";
  request.subject_identity.trust_level = "trusted";
  request.subject_identity.tenant_ref = "tenant:local";
  request.decision_proof.decision = "Allow";
  request.decision_proof.policy_decision_ref = "policy:fullint-011";
  request.decision_proof.reason_code = "allowed";
  request.agent_request = make_agent_request(ids);
  request.client_capability_view = "cli-json-async";
  request.async_allowed = true;
  request.request_context["normalizer_ready"] = "true";
  request.request_context["request_id"] = ids.request_id;
  request.request_context["session_id"] = ids.session_id;
  request.request_context["trace_id"] = ids.trace_id;
  return request;
}

dasall::access::PublishEnvelope make_replay_envelope(
    const ChainIds& ids,
    const dasall::access::AsyncTaskReceipt& receipt) {
  dasall::access::PublishEnvelope envelope;
  envelope.request_id = ids.request_id;
  envelope.result_id = "result:" + ids.request_id;
  envelope.session_id = ids.session_id;
  envelope.trace_id = ids.trace_id;
  envelope.channel_ref = "daemon:ipc";
  envelope.protocol_kind = "ipc_uds";
  envelope.protocol_status_hint = "202";
  envelope.protocol_metadata = "fullint-011 replay metadata";
  envelope.is_final = false;
  envelope.payload = "{\"status\":\"accepted_async\"}";
  envelope.receipt = receipt;
  return envelope;
}

dasall::contracts::BudgetSnapshot make_budget_snapshot(const bool exhausted) {
  using dasall::contracts::BudgetSnapshot;
  using dasall::contracts::BudgetSnapshotEntry;
  using dasall::contracts::BudgetType;

  return BudgetSnapshot{
      .snapshot_at_ms = static_cast<std::uint64_t>(current_time_millis()),
      .entries = {
          BudgetSnapshotEntry{.budget_type = BudgetType::Token,
                              .current = 64,
                              .max = 2048,
                              .remaining = 1984,
                              .reject_reason = std::nullopt},
          BudgetSnapshotEntry{.budget_type = BudgetType::Turn,
                              .current = 1,
                              .max = 6,
                              .remaining = 5,
                              .reject_reason = std::nullopt},
          BudgetSnapshotEntry{.budget_type = BudgetType::ToolCall,
                              .current = exhausted ? 2ULL : 1ULL,
                              .max = 1,
                              .remaining = exhausted ? -1 : 0,
                              .reject_reason = exhausted
                                                   ? std::optional<std::string>(
                                                         "fullint-011 tool budget exhausted")
                                                   : std::nullopt},
          BudgetSnapshotEntry{.budget_type = BudgetType::Latency,
                              .current = 120,
                              .max = 4000,
                              .remaining = 3880,
                              .reject_reason = std::nullopt},
          BudgetSnapshotEntry{.budget_type = BudgetType::Replan,
                              .current = 0,
                              .max = 2,
                              .remaining = 2,
                              .reject_reason = std::nullopt},
      },
      .overall_reject_reason = exhausted
                                   ? std::optional<std::string>(
                                         "fullint-011 tool budget exhausted")
                                   : std::nullopt,
  };
}

dasall::contracts::ErrorInfo make_error_info(const ChainIds& ids) {
  return dasall::contracts::ErrorInfo{
      .failure_type = dasall::contracts::ResultCodeCategory::Runtime,
      .retryable = true,
      .safe_to_replan = false,
      .details = dasall::contracts::ErrorDetails{
          .code = 5011,
          .message = "fullint-011 async task failed before replay",
          .stage = "async_recovery",
      },
      .source_ref = dasall::contracts::ErrorSourceRefMinimal{
          .ref_type = "tool_call",
          .ref_id = "tool-call:" + ids.request_id,
      },
  };
}

dasall::contracts::Checkpoint make_checkpoint(const ChainIds& ids) {
  return dasall::contracts::Checkpoint{
      .checkpoint_id = ids.checkpoint_id,
      .state = dasall::contracts::CheckpointState::Running,
      .step_id = std::string("async-recovery-step"),
      .working_memory_snapshot = std::string("wm:" + ids.session_id),
      .pending_action = std::string(),
      .request_id = ids.request_id,
      .goal_id = ids.goal_id,
      .belief_state_ref = std::string("belief:" + ids.request_id),
      .retry_count = 1U,
      .created_at = current_time_millis(),
      .tags = std::vector<std::string>{
          "rt.schema_version=1",
          "rt.fsm_state_enum_version=1",
          "rt.budget_schema_version=1",
          "fullint-011"},
  };
}

dasall::contracts::RecoveryRequest make_recovery_request(const ChainIds& ids,
                                                         const bool exhausted_budget) {
  const auto error_info = make_error_info(ids);
  const auto retry_token = dasall::runtime::make_resume_binding_token(
      ids.session_id,
      ids.checkpoint_id);

  return dasall::contracts::RecoveryRequest{
      .reflection_decision = dasall::contracts::ReflectionDecision{
          .request_id = ids.request_id,
          .decision_kind = dasall::contracts::ReflectionDecisionKind::RetryStep,
          .rationale = std::string("retry the async step with preserved idempotency evidence"),
          .goal_id = ids.goal_id,
          .confidence = 0.91F,
          .relevant_observation_refs = std::vector<std::string>{"obs:" + ids.request_id},
          .hint_ref = std::string("hint:fullint-011"),
          .created_at = current_time_millis(),
          .tags = std::vector<std::string>{"fullint-011", "runtime-recovery"},
      },
      .error_info = error_info,
      .latest_observation = dasall::contracts::Observation{
          .observation_id = std::string("obs:" + ids.request_id),
          .source = dasall::contracts::ObservationSource::ToolExecution,
          .success = false,
          .payload = std::string("{\"receipt_id\":\"" + ids.receipt_id + "\"}"),
          .created_at = current_time_millis(),
          .error = error_info,
          .side_effects = std::nullopt,
          .tool_call_id = std::string("tool-call:" + ids.request_id),
          .worker_task_id = std::nullopt,
          .request_id = ids.request_id,
          .goal_id = ids.goal_id,
          .duration_ms = 25,
          .tags = std::vector<std::string>{"fullint-011", "async-failure"},
      },
      .checkpoint = make_checkpoint(ids),
      .idempotency_and_side_effect_report =
          dasall::contracts::IdempotencyAndSideEffectReport{
              .replay_safe = true,
              .idempotency_key = retry_token,
              .side_effects_present = true,
              .non_replayable_reason = std::nullopt,
          },
      .retry_count = 1U,
      .runtime_budget_snapshot = make_budget_snapshot(exhausted_budget),
  };
}

dasall::memory::MemoryConfig make_memory_config(const std::filesystem::path& database_path) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.vector.enabled = false;
  return config;
}

dasall::memory::MemoryWritebackRequest make_writeback_request(const ChainIds& ids) {
  dasall::memory::MemoryWritebackRequest request;
  request.session_id = ids.session_id;
  request.turn.turn_id = ids.turn_id;
  request.turn.session_id = ids.session_id;
  request.turn.user_input = "FULLINT-011 async recovery user turn";
  request.turn.agent_response = "FULLINT-011 recovery path completed";
  request.turn.created_at = current_time_millis();
  request.side_effect_report_ref = "side-effect:" + ids.request_id;
  request.summary_candidate = dasall::contracts::SummaryMemory{};
  request.summary_candidate->summary_text = "FULLINT-011 async recovery summary";
  request.summary_candidate->source_turn_ids = std::vector<std::string>{ids.turn_id};
  request.summary_candidate->confirmed_facts = std::vector<std::string>{ids.trace_id};

  dasall::memory::FactCandidate fact_candidate;
  fact_candidate.fact.fact_text = "FULLINT-011 trace preserved through recovery";
  fact_candidate.fact.session_id = ids.session_id;
  fact_candidate.fact.source_turn_ids = std::vector<std::string>{ids.turn_id};
  fact_candidate.fact.confidence_score = 95;
  fact_candidate.fact.created_at = current_time_millis();
  fact_candidate.fact.fact_type = "integration_evidence";
  fact_candidate.extraction_source = "fullint-011";
  request.fact_candidates.push_back(std::move(fact_candidate));
  return request;
}

bool snapshot_has_slot(const dasall::memory::WorkingMemorySnapshot& snapshot,
                       const std::string& key,
                       const std::string& expected_value) {
  return std::any_of(snapshot.slots.begin(),
                     snapshot.slots.end(),
                     [&key, &expected_value](const dasall::memory::WorkingMemorySlot& slot) {
                       return slot.key == key && slot.value == expected_value;
                     });
}

void verify_access_async_receipt_replay_and_cancel_causality(const ChainIds& ids) {
  auto registry = std::make_shared<dasall::access::AsyncTaskRegistry>(
      "fullint-011-secret",
      std::chrono::seconds(60));

  dasall::access::RuntimeBridge bridge(
      [ids](const dasall::access::RuntimeDispatchRequest&) {
        dasall::access::RuntimeDispatchResult result;
        result.disposition = dasall::access::AccessDisposition::AcceptedAsync;
        result.receipt_ref = ids.receipt_id;
        result.publish_envelope = dasall::access::PublishEnvelope{
            .request_id = ids.request_id,
            .result_id = "result:" + ids.request_id,
            .session_id = ids.session_id,
            .trace_id = ids.trace_id,
            .channel_ref = "daemon:ipc",
            .protocol_kind = "ipc_uds",
            .agent_result = std::nullopt,
            .protocol_status_hint = "202",
            .protocol_metadata = "accepted_async",
            .is_final = false,
            .payload = "{\"status\":\"accepted_async\"}",
            .receipt = std::nullopt,
        };
        return result;
      },
      {});

  const auto dispatch_request = make_dispatch_request(ids);
  const auto dispatch_result = bridge.dispatch(dispatch_request);
  assert_true(dispatch_result.disposition == dasall::access::AccessDisposition::AcceptedAsync,
              "runtime bridge should preserve accepted_async disposition");
  assert_context_string(ids.request_id,
                        dispatch_result.response_context,
                        "request_id",
                        "runtime bridge response request_id");
  assert_context_string(ids.session_id,
                        dispatch_result.response_context,
                        "session_id",
                        "runtime bridge response session_id");
  assert_context_string(ids.trace_id,
                        dispatch_result.response_context,
                        "trace_id",
                        "runtime bridge response trace_id");

  const auto receipt = registry->register_async_accept(dispatch_request, dispatch_result);
  assert_true(receipt.has_value(), "accepted async dispatch should register a receipt");
  assert_equal(ids.receipt_id, receipt->receipt_id, "async receipt id should preserve runtime ref");
  assert_equal(ids.request_id, receipt->request_id, "async receipt should preserve request id");
  assert_equal(ids.session_id, receipt->session_id, "async receipt should preserve session id");
  assert_equal(ids.actor_ref, receipt->actor_ref, "async receipt should preserve owner actor");
  assert_true(!receipt->ownership_token.empty(), "async receipt should carry an ownership token");

  dasall::access::ResultReplayCache replay_cache(4, std::chrono::seconds(60));
  replay_cache.put(receipt->receipt_id, make_replay_envelope(ids, *receipt));
  const auto replay = replay_cache.lookup(receipt->receipt_id);
  assert_true(replay.has_value(), "replay cache should return the accepted async envelope");
  assert_equal(ids.request_id, replay->request_id, "replay envelope should preserve request id");
  assert_equal(ids.session_id, replay->session_id, "replay envelope should preserve session id");
  assert_equal(ids.trace_id, replay->trace_id, "replay envelope should preserve trace id");
  assert_true(replay->receipt.has_value(), "replay envelope should preserve async receipt");
  assert_equal(receipt->ownership_token,
               replay->receipt->ownership_token,
               "replay envelope should preserve ownership token");

  assert_true(registry->validate_ownership(receipt->receipt_id,
                                           ids.actor_ref,
                                           receipt->ownership_token),
              "owner should pass receipt ownership validation");
  assert_true(!registry->validate_ownership(receipt->receipt_id,
                                            "local://uid/2000",
                                            receipt->ownership_token),
              "owner mismatch should fail closed before cancel forwarding");

  int cancel_calls = 0;
  std::string cancel_request_id;
  std::string cancel_actor_ref;
  dasall::access::RuntimeBridge cancel_bridge(
      {},
      [&cancel_calls, &cancel_request_id, &cancel_actor_ref](
          std::string_view request_id,
          std::string_view actor_ref) {
        ++cancel_calls;
        cancel_request_id = std::string(request_id);
        cancel_actor_ref = std::string(actor_ref);
        return true;
      });

  assert_true(cancel_bridge.cancel(receipt->request_id, receipt->actor_ref),
              "owner-matched cancel should be forwarded to runtime bridge");
  assert_equal(1, cancel_calls, "cancel should be forwarded exactly once");
  assert_equal(ids.request_id, cancel_request_id, "cancel should forward receipt-bound request id");
  assert_equal(ids.actor_ref, cancel_actor_ref, "cancel should forward authenticated actor ref");
  assert_true(registry->mark_completed(receipt->receipt_id, "cancelled"),
              "cancel forwarding should keep receipt state observable");

  const auto cancelled = registry->query_receipt(receipt->receipt_id);
  assert_true(cancelled.receipt.has_value(), "cancelled receipt should remain queryable");
  assert_optional_string("cancelled",
                         cancelled.receipt->initial_status,
                         "cancelled receipt status");
}

void verify_runtime_recovery_admission_uses_checkpoint_and_replay_evidence(const ChainIds& ids) {
  dasall::runtime::CancellationToken token;
  const auto worker_copy = token;
  assert_true(!worker_copy.is_cancelled(), "copied cancellation token should start active");
  token.cancel();
  assert_true(worker_copy.is_cancelled(),
              "copied cancellation token should observe cooperative cancellation");

  dasall::runtime::RecoveryManager manager;
  const auto retry_token = dasall::runtime::make_resume_binding_token(
      ids.session_id,
      ids.checkpoint_id);
  const auto plan = manager.evaluate(make_recovery_request(ids, false));
  assert_true(plan.admission == dasall::runtime::RecoveryAdmission::Admit && plan.executable(),
              "replay-safe recovery request should be admitted");
  assert_true(plan.resume_plan.has_value(),
              "admitted retry recovery should materialize a resume plan");
  assert_equal(ids.checkpoint_id,
               plan.resume_plan->checkpoint_ref,
               "resume plan should preserve checkpoint causality");
  assert_true(plan.detail.find(retry_token) != std::string::npos,
              "recovery admission should reuse retry idempotency token evidence");

  const auto outcome = manager.execute(plan);
  assert_optional_string("retry_step", outcome.executed_action, "recovery outcome action");
  assert_optional_string("Reasoning", outcome.final_runtime_state, "recovery outcome state");
  assert_optional_string(ids.checkpoint_id, outcome.checkpoint_ref, "recovery outcome checkpoint ref");
  const auto apply_result = manager.apply(outcome);
  assert_true(apply_result.applied && !apply_result.error_code.has_value(),
              "admitted retry outcome should apply cleanly");

  const auto degraded_plan = manager.evaluate(make_recovery_request(ids, true));
  assert_true(degraded_plan.escalated() && degraded_plan.safe_failure_hint.has_value() &&
                  degraded_plan.safe_failure_hint->enter_degraded_mode,
              "exhausted recovery budget should escalate into degraded mode");
  const auto degraded_outcome = manager.execute(degraded_plan);
  assert_optional_string("degrade", degraded_outcome.executed_action, "degrade outcome action");
  assert_optional_string("Degraded", degraded_outcome.final_runtime_state, "degrade outcome state");
}

void verify_memory_writeback_preserves_session_and_turn_continuity(const ChainIds& ids) {
  const auto database_path = make_temp_database_path();
  cleanup_database_artifacts(database_path);
  const auto config = make_memory_config(database_path);
  if (config.storage.migrations_dir.empty()) {
    throw std::runtime_error("DASALL_SQL_MEMORY_DIR must be defined for FULLINT-011 memory coverage");
  }

  auto manager = dasall::memory::create_memory_manager(config);
  assert_true(static_cast<int>(manager->init(config)) == 0,
              "memory manager should initialize for FULLINT-011 writeback coverage");

  const auto writeback = manager->write_back(make_writeback_request(ids));
  assert_true(!writeback.result_code.has_value(),
              "writeback should persist the recovery turn without result_code failure");
  assert_optional_string(ids.turn_id,
                         writeback.persisted_turn_id,
                         "writeback result persisted turn id");
  assert_true(writeback.summary_id.has_value(), "writeback should persist summary evidence");
  assert_equal(1, static_cast<int>(writeback.fact_ids.size()),
               "writeback should persist one trace continuity fact");
  assert_true(!writeback.retryable_storage_failure,
              "writeback should not request runtime retry on the healthy path");

  const auto export_result = manager->export_working_memory_snapshot(
      dasall::memory::WorkingMemoryExportRequest{
          .session_id = ids.session_id,
          .export_reason = "fullint-011-verify",
          .include_ephemeral_facts = true,
      });
  assert_true(!export_result.result_code.has_value(),
              "working memory snapshot should export after writeback");
  assert_true(snapshot_has_slot(export_result.snapshot, "latest_turn_id", ids.turn_id),
              "working memory should expose latest turn continuity");

  auto store = dasall::memory::store::sqlite::create_sqlite_memory_store();
  const auto open_result = store->open(config);
  if (open_result.has_value()) {
    throw std::runtime_error("failed to reopen sqlite memory store for FULLINT-011 verification");
  }

  const auto bundle = store->load_session_bundle(
      dasall::memory::SessionLoadRequest{.session_id = ids.session_id,
                                         .recent_turn_limit = 4});
  assert_equal(1, bundle.total_turn_count,
               "stored session should contain the recovery writeback turn");
  assert_equal(ids.turn_id,
               bundle.recent_turns.front().turn_id.value_or(std::string()),
               "stored turn should preserve the fullint causal turn id");
  store->close();
  manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    const ChainIds ids;
    verify_access_async_receipt_replay_and_cancel_causality(ids);
    verify_runtime_recovery_admission_uses_checkpoint_and_replay_evidence(ids);
    verify_memory_writeback_preserves_session_and_turn_continuity(ids);
  } catch (const std::exception& exception) {
    std::cerr << "[FullIntAsyncRecoveryCausalityTest] FAILED: "
              << exception.what() << '\n';
    return 1;
  }

  return 0;
}