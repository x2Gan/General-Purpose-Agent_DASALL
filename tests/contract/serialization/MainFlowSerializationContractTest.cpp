// MainFlowSerializationContractTest.cpp
//
// T001 – 补齐主链路对象序列化兼容测试
//
// 覆盖范围（来自 DASALL_contracts验收整改TODO.md T001）：
//   GoalContract, Observation, ObservationDigest,
//   BeliefState, Checkpoint, AgentResult
//
// 每个对象三类子测试：
//   1. round-trip   ：序列化 -> 反序列化后，required 字段值与原始值一致，
//                    且 guard 验证通过。
//   2. missing field：删除 required 字段后，guard 验证必须失败（检测字段丢失）。
//   3. unknown field：向 wire map 注入未知字段，guard 验证仍通过
//                    （向前兼容容忍性）。
//
// 设计约束：
//   - 使用与 SerializationCompatibilityContractTest.cpp 相同的 WireMap（纯字符串
//     key-value 平坦映射）作为通用序列化载体，无需引入外部序列化库。
//   - guard 函数直接复用 contracts 冻结的 Layer 1 required-field guard，
//     与 WP05-T013 的验证策略一致。
//   - 所有枚举值通过整型 round-trip，使用 fallback_unknown_enum_value 下降到
//     Unspecified 语义，以覆盖 unknown enum 容忍性。

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

// --- contracts headers -------------------------------------------------------
#include "agent/AgentResult.h"
#include "agent/AgentResultGuards.h"
#include "agent/BeliefState.h"
#include "agent/BeliefStateGuards.h"
#include "agent/GoalContract.h"
#include "agent/GoalContractGuards.h"
#include "boundary/CompatibilityGuards.h"
#include "checkpoint/Checkpoint.h"
#include "checkpoint/CheckpointGuards.h"
#include "observation/Observation.h"
#include "observation/ObservationDigest.h"
#include "observation/ObservationDigestGuards.h"
#include "observation/ObservationGuards.h"
#include "observation/ObservationSource.h"

// --- test support ------------------------------------------------------------
#include "dasall/tests/support/TestAssertions.h"

namespace {

// ===========================================================================
// Wire layer utility helpers
// ===========================================================================

// WireMap は文字列 key-value の平坦マップで、プロトコル非依存の
// 軽量 wire 表現として使用する（SerializationCompatibilityContractTest と同一）。
using WireMap = std::map<std::string, std::string>;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

using dasall::contracts::AgentResult;
using dasall::contracts::AgentResultStatus;
using dasall::contracts::BeliefState;
using dasall::contracts::Checkpoint;
using dasall::contracts::CheckpointState;
using dasall::contracts::GoalContract;
using dasall::contracts::GoalStatus;
using dasall::contracts::Observation;
using dasall::contracts::ObservationDigest;
using dasall::contracts::ObservationSource;
using dasall::contracts::ApprovalPolicy;
using dasall::contracts::fallback_unknown_enum_value;

// ---------------------------------------------------------------------------
// find_wire_value
// ---------------------------------------------------------------------------
// wire map から文字列値を読み出す。キーが存在しない場合は std::nullopt を返し、
// "フィールド欠落" と "フィールド空" を区別してガード検証が正確に機能するようにする。
std::optional<std::string> find_wire_value(const WireMap& wire,
                                           const std::string& key) {
  const auto it = wire.find(key);
  if (it == wire.end()) {
    return std::nullopt;
  }
  return it->second;
}

// ---------------------------------------------------------------------------
// parse_wire_int64
// ---------------------------------------------------------------------------
// wire テキストから符号付き 64 ビット整数を解析する。
// 変換失敗時は std::nullopt を返し、ガードが安定した失敗を報告できるようにする。
std::optional<std::int64_t> parse_wire_int64(const WireMap& wire,
                                             const std::string& key) {
  const auto text = find_wire_value(wire, key);
  if (!text.has_value()) {
    return std::nullopt;
  }
  try {
    return std::stoll(*text);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// ---------------------------------------------------------------------------
// parse_wire_uint32
// ---------------------------------------------------------------------------
// wire テキストから符号なし 32 ビット整数を解析する。
// オーバーフローや変換失敗時は std::nullopt を返す。
std::optional<std::uint32_t> parse_wire_uint32(const WireMap& wire,
                                               const std::string& key) {
  const auto text = find_wire_value(wire, key);
  if (!text.has_value()) {
    return std::nullopt;
  }
  try {
    const auto parsed = std::stoull(*text);
    constexpr auto kMax =
        static_cast<unsigned long long>(
            std::numeric_limits<std::uint32_t>::max());
    if (parsed > kMax) {
      return std::nullopt;
    }
    return static_cast<std::uint32_t>(parsed);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// ---------------------------------------------------------------------------
// parse_wire_float
// ---------------------------------------------------------------------------
// wire テキストから float を解析する。変換失敗は std::nullopt として扱う。
std::optional<float> parse_wire_float(const WireMap& wire,
                                      const std::string& key) {
  const auto text = find_wire_value(wire, key);
  if (!text.has_value()) {
    return std::nullopt;
  }
  try {
    return std::stof(*text);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

// ---------------------------------------------------------------------------
// parse_wire_bool
// ---------------------------------------------------------------------------
// wire テキストから bool を解析する。"1" を true、その他を false として扱う。
// キーが存在しない場合は std::nullopt を返す。
std::optional<bool> parse_wire_bool(const WireMap& wire,
                                    const std::string& key) {
  const auto text = find_wire_value(wire, key);
  if (!text.has_value()) {
    return std::nullopt;
  }
  return (*text == "1");
}

// ---------------------------------------------------------------------------
// parse_wire_string_vector
// ---------------------------------------------------------------------------
// wire テキストからカンマ区切り文字列ベクタを解析する。
// キーが存在する場合は空ベクタ（空文字列のエントリ）でも有効とする。
// キーが存在しない場合は std::nullopt を返す（"フィールド欠落" を識別）。
std::optional<std::vector<std::string>> parse_wire_string_vector(
    const WireMap& wire,
    const std::string& key) {
  const auto text = find_wire_value(wire, key);
  if (!text.has_value()) {
    return std::nullopt;
  }
  // 空文字列の場合でも空ベクタとして返す（has_value=true）。
  std::vector<std::string> result;
  if (text->empty()) {
    return result;
  }
  std::string::size_type start = 0;
  while (true) {
    const auto pos = text->find(',', start);
    if (pos == std::string::npos) {
      result.push_back(text->substr(start));
      break;
    }
    result.push_back(text->substr(start, pos - start));
    start = pos + 1;
  }
  return result;
}

// ---------------------------------------------------------------------------
// serialize_string_vector
// ---------------------------------------------------------------------------
// 文字列ベクタをカンマ区切り wire テキストに変換する補助関数。
// 要素内のカンマはエスケープしないため、テスト専用の単純実装として使用する。
std::string serialize_string_vector(const std::vector<std::string>& vec) {
  std::string result;
  for (std::size_t i = 0; i < vec.size(); ++i) {
    if (i > 0) {
      result += ',';
    }
    result += vec[i];
  }
  return result;
}

// ===========================================================================
// GoalContract wire helpers
// ===========================================================================

// make_valid_goal_contract_sample
// 必須フィールドをすべて含む最小有効 GoalContract を構築する。
// T001 の複数シナリオで共通の安定した基準値として再利用する。
GoalContract make_valid_goal_contract_sample() {
  GoalContract goal;
  // Required fields (WP03-T004 – 6 items)
  goal.goal_id = "goal-t001-001";
  goal.request_id = "req-t001-001";
  goal.goal_description = "今日の巡回点検サマリを生成する";
  goal.success_criteria = "{\"summary_length_min\": 100}";
  goal.status = GoalStatus::Active;
  goal.created_at = 1710000100000LL;
  return goal;
}

// serialize_goal_contract_v1
// GoalContract の必須フィールドを wire map に書き出す。
// シリアライズ対象はバージョン V1 の安定フィールドのみとし、
// 将来追加フィールドへの影響を受けない基準アサーションを維持する。
WireMap serialize_goal_contract_v1(const GoalContract& goal) {
  WireMap wire;
  if (goal.goal_id.has_value()) {
    wire["goal_id"] = *goal.goal_id;
  }
  if (goal.request_id.has_value()) {
    wire["request_id"] = *goal.request_id;
  }
  if (goal.goal_description.has_value()) {
    wire["goal_description"] = *goal.goal_description;
  }
  if (goal.success_criteria.has_value()) {
    wire["success_criteria"] = *goal.success_criteria;
  }
  if (goal.status.has_value()) {
    wire["status"] = std::to_string(static_cast<int>(*goal.status));
  }
  if (goal.created_at.has_value()) {
    wire["created_at"] = std::to_string(*goal.created_at);
  }
  return wire;
}

// deserialize_goal_contract_wire
// wire map から GoalContract を復元する。
// unknown enum 値は Unspecified にフォールバックし、
// ガードが一貫した失敗理由を報告できるようにする。
GoalContract deserialize_goal_contract_wire(const WireMap& wire) {
  GoalContract goal;

  goal.goal_id = find_wire_value(wire, "goal_id");
  goal.request_id = find_wire_value(wire, "request_id");
  goal.goal_description = find_wire_value(wire, "goal_description");
  goal.success_criteria = find_wire_value(wire, "success_criteria");
  goal.created_at = parse_wire_int64(wire, "created_at");

  // GoalStatus enum: 既知の値以外は Unspecified にフォールバック。
  constexpr std::array<int, 5> kKnownStatuses{
      static_cast<int>(GoalStatus::Unspecified),
      static_cast<int>(GoalStatus::Active),
      static_cast<int>(GoalStatus::Achieved),
      static_cast<int>(GoalStatus::Failed),
      static_cast<int>(GoalStatus::Cancelled),
  };
  const auto raw_status = parse_wire_int64(wire, "status");
  if (raw_status.has_value()) {
    goal.status = fallback_unknown_enum_value<GoalStatus>(
        static_cast<int>(*raw_status),
        kKnownStatuses.data(),
        kKnownStatuses.size(),
        GoalStatus::Unspecified);
  }

  return goal;
}

// ===========================================================================
// GoalContract tests
// ===========================================================================

// Positive: GoalContract の必須フィールドが round-trip 後も変化しないことを検証。
void test_goal_contract_round_trip_keeps_required_fields() {
  const auto source = make_valid_goal_contract_sample();
  const auto wire = serialize_goal_contract_v1(source);
  const auto restored = deserialize_goal_contract_wire(wire);

  const auto guard =
      dasall::contracts::validate_goal_contract_required_fields(restored);
  assert_true(guard.ok,
              "round-trip GoalContract should remain guard-valid");
  assert_equal(*source.goal_id,
               restored.goal_id.value_or(""),
               "goal_id should remain stable after round-trip");
  assert_equal(*source.goal_description,
               restored.goal_description.value_or(""),
               "goal_description should remain stable after round-trip");
}

// Positive: unknown フィールドを追加しても既存フィールドの guard 検証が通ることを検証。
void test_goal_contract_forward_compatibility_ignores_unknown_fields() {
  auto wire = serialize_goal_contract_v1(make_valid_goal_contract_sample());
  // 未知のフィールドを注入（新バージョンプロデューサーからのメッセージを模擬）。
  wire["future_priority_class"] = "high";
  wire["future_budget_tag"] = "v2-budget";

  const auto restored = deserialize_goal_contract_wire(wire);
  const auto guard =
      dasall::contracts::validate_goal_contract_required_fields(restored);
  assert_true(guard.ok,
              "unknown additive fields should be ignored for GoalContract compatibility");
}

// Negative: goal_id が欠落した場合、guard 検証が失敗することを検証。
void test_goal_contract_missing_required_field_is_rejected() {
  auto wire = serialize_goal_contract_v1(make_valid_goal_contract_sample());
  wire.erase("goal_id");

  const auto restored = deserialize_goal_contract_wire(wire);
  const auto guard =
      dasall::contracts::validate_goal_contract_required_fields(restored);
  assert_true(!guard.ok,
              "missing goal_id should fail GoalContract validation");
}

// ===========================================================================
// Observation wire helpers
// ===========================================================================

// make_valid_observation_sample
// 必須フィールドをすべて含む最小有効 Observation を構築する。
Observation make_valid_observation_sample() {
  Observation obs;
  // Required fields (WP03-T006 – 5 items)
  obs.observation_id = "obs-t001-001";
  obs.source = ObservationSource::ToolExecution;
  obs.success = true;
  obs.payload = "{\"tool_output\": \"ok\"}";
  obs.created_at = 1710000200000LL;
  return obs;
}

// serialize_observation_v1
// Observation の必須フィールドを wire map に書き出す。
WireMap serialize_observation_v1(const Observation& obs) {
  WireMap wire;
  if (obs.observation_id.has_value()) {
    wire["observation_id"] = *obs.observation_id;
  }
  if (obs.source.has_value()) {
    wire["source"] = std::to_string(static_cast<int>(*obs.source));
  }
  if (obs.success.has_value()) {
    wire["success"] = (*obs.success ? "1" : "0");
  }
  if (obs.payload.has_value()) {
    wire["payload"] = *obs.payload;
  }
  if (obs.created_at.has_value()) {
    wire["created_at"] = std::to_string(*obs.created_at);
  }
  return wire;
}

// deserialize_observation_wire
// wire map から Observation を復元する。
// ObservationSource の unknown enum 値は Unspecified にフォールバックする。
Observation deserialize_observation_wire(const WireMap& wire) {
  Observation obs;

  obs.observation_id = find_wire_value(wire, "observation_id");
  obs.success = parse_wire_bool(wire, "success");
  obs.payload = find_wire_value(wire, "payload");
  obs.created_at = parse_wire_int64(wire, "created_at");

  // ObservationSource enum: 既知の値以外は Unspecified にフォールバック。
  constexpr std::array<int, 5> kKnownSources{
      static_cast<int>(ObservationSource::Unspecified),
      static_cast<int>(ObservationSource::ToolExecution),
      static_cast<int>(ObservationSource::WorkerAgent),
      static_cast<int>(ObservationSource::Retrieval),
      static_cast<int>(ObservationSource::HumanFeedback),
  };
  const auto raw_source = parse_wire_int64(wire, "source");
  if (raw_source.has_value()) {
    obs.source = fallback_unknown_enum_value<ObservationSource>(
        static_cast<int>(*raw_source),
        kKnownSources.data(),
        kKnownSources.size(),
        ObservationSource::Unspecified);
  }

  return obs;
}

// ===========================================================================
// Observation tests
// ===========================================================================

// Positive: Observation の必須フィールドが round-trip 後も変化しないことを検証。
void test_observation_round_trip_keeps_required_fields() {
  const auto source = make_valid_observation_sample();
  const auto wire = serialize_observation_v1(source);
  const auto restored = deserialize_observation_wire(wire);

  const auto guard =
      dasall::contracts::validate_observation_required_fields(restored);
  assert_true(guard.ok,
              "round-trip Observation should remain guard-valid");
  assert_equal(*source.observation_id,
               restored.observation_id.value_or(""),
               "observation_id should remain stable after round-trip");
  assert_equal(*source.payload,
               restored.payload.value_or(""),
               "payload should remain stable after round-trip");
}

// Positive: unknown フィールドを追加しても既存フィールドの guard 検証が通ることを検証。
void test_observation_forward_compatibility_ignores_unknown_fields() {
  auto wire = serialize_observation_v1(make_valid_observation_sample());
  wire["future_confidence_score"] = "0.97";
  wire["future_audit_chain"] = "chain-xyz";

  const auto restored = deserialize_observation_wire(wire);
  const auto guard =
      dasall::contracts::validate_observation_required_fields(restored);
  assert_true(guard.ok,
              "unknown additive fields should be ignored for Observation compatibility");
}

// Negative: observation_id が欠落した場合、guard 検証が失敗することを検証。
void test_observation_missing_required_field_is_rejected() {
  auto wire = serialize_observation_v1(make_valid_observation_sample());
  wire.erase("observation_id");

  const auto restored = deserialize_observation_wire(wire);
  const auto guard =
      dasall::contracts::validate_observation_required_fields(restored);
  assert_true(!guard.ok,
              "missing observation_id should fail Observation validation");
}

// ===========================================================================
// ObservationDigest wire helpers
// ===========================================================================

// make_valid_observation_digest_sample
// 必須フィールドをすべて含む最小有効 ObservationDigest を構築する。
ObservationDigest make_valid_observation_digest_sample() {
  ObservationDigest digest;
  // Required fields (WP03-T008 – 5 items)
  digest.observation_id = "obs-t001-001";
  digest.summary = "ツール実行は正常に完了しました。";
  digest.key_facts = {"ツール出力: ok", "実行時間: 120ms"};
  digest.citations = {"obs-t001-001#payload"};
  digest.confidence = 0.95f;
  return digest;
}

// serialize_observation_digest_v1
// ObservationDigest の必須フィールドを wire map に書き出す。
WireMap serialize_observation_digest_v1(const ObservationDigest& digest) {
  WireMap wire;
  if (digest.observation_id.has_value()) {
    wire["observation_id"] = *digest.observation_id;
  }
  if (digest.summary.has_value()) {
    wire["summary"] = *digest.summary;
  }
  if (digest.key_facts.has_value()) {
    wire["key_facts"] = serialize_string_vector(*digest.key_facts);
  }
  if (digest.citations.has_value()) {
    wire["citations"] = serialize_string_vector(*digest.citations);
  }
  if (digest.confidence.has_value()) {
    wire["confidence"] = std::to_string(*digest.confidence);
  }
  return wire;
}

// deserialize_observation_digest_wire
// wire map から ObservationDigest を復元する。
ObservationDigest deserialize_observation_digest_wire(const WireMap& wire) {
  ObservationDigest digest;

  digest.observation_id = find_wire_value(wire, "observation_id");
  digest.summary = find_wire_value(wire, "summary");
  digest.key_facts = parse_wire_string_vector(wire, "key_facts");
  digest.citations = parse_wire_string_vector(wire, "citations");
  digest.confidence = parse_wire_float(wire, "confidence");

  return digest;
}

// ===========================================================================
// ObservationDigest tests
// ===========================================================================

// Positive: ObservationDigest の必須フィールドが round-trip 後も変化しないことを検証。
void test_observation_digest_round_trip_keeps_required_fields() {
  const auto source = make_valid_observation_digest_sample();
  const auto wire = serialize_observation_digest_v1(source);
  const auto restored = deserialize_observation_digest_wire(wire);

  const auto guard =
      dasall::contracts::validate_observation_digest_required_fields(restored);
  assert_true(guard.ok,
              "round-trip ObservationDigest should remain guard-valid");
  assert_equal(*source.observation_id,
               restored.observation_id.value_or(""),
               "observation_id should remain stable after round-trip");
  assert_equal(*source.summary,
               restored.summary.value_or(""),
               "summary should remain stable after round-trip");
  assert_true(restored.key_facts.has_value(),
              "key_facts should be present after round-trip");
}

// Positive: unknown フィールドを追加しても既存フィールドの guard 検証が通ることを検証。
void test_observation_digest_forward_compatibility_ignores_unknown_fields() {
  auto wire =
      serialize_observation_digest_v1(make_valid_observation_digest_sample());
  wire["future_digest_version"] = "2";
  wire["future_model_hint"] = "llm-b";

  const auto restored = deserialize_observation_digest_wire(wire);
  const auto guard =
      dasall::contracts::validate_observation_digest_required_fields(restored);
  assert_true(guard.ok,
              "unknown additive fields should be ignored for ObservationDigest compatibility");
}

// Negative: summary が欠落した場合、guard 検証が失敗することを検証。
void test_observation_digest_missing_required_field_is_rejected() {
  auto wire =
      serialize_observation_digest_v1(make_valid_observation_digest_sample());
  wire.erase("summary");

  const auto restored = deserialize_observation_digest_wire(wire);
  const auto guard =
      dasall::contracts::validate_observation_digest_required_fields(restored);
  assert_true(!guard.ok,
              "missing summary should fail ObservationDigest validation");
}

// ===========================================================================
// BeliefState wire helpers
// ===========================================================================

// make_valid_belief_state_sample
// 必須フィールドをすべて含む最小有効 BeliefState を構築する。
BeliefState make_valid_belief_state_sample() {
  BeliefState state;
  // Required fields (WP03-T009 – 6 items)
  state.request_id = "req-t001-001";
  state.confirmed_facts = {"今日の巡回は完了済み"};
  state.hypotheses = std::vector<std::string>{};
  state.assumptions = {"スケジュールは変更なし"};
  state.evidence_refs = {"obs-t001-001"};
  state.confidence = 0.85f;
  return state;
}

// serialize_belief_state_v1
// BeliefState の必須フィールドを wire map に書き出す。
WireMap serialize_belief_state_v1(const BeliefState& state) {
  WireMap wire;
  if (state.request_id.has_value()) {
    wire["request_id"] = *state.request_id;
  }
  if (state.confirmed_facts.has_value()) {
    wire["confirmed_facts"] = serialize_string_vector(*state.confirmed_facts);
  }
  if (state.hypotheses.has_value()) {
    wire["hypotheses"] = serialize_string_vector(*state.hypotheses);
  }
  if (state.assumptions.has_value()) {
    wire["assumptions"] = serialize_string_vector(*state.assumptions);
  }
  if (state.evidence_refs.has_value()) {
    wire["evidence_refs"] = serialize_string_vector(*state.evidence_refs);
  }
  if (state.confidence.has_value()) {
    wire["confidence"] = std::to_string(*state.confidence);
  }
  return wire;
}

// deserialize_belief_state_wire
// wire map から BeliefState を復元する。
BeliefState deserialize_belief_state_wire(const WireMap& wire) {
  BeliefState state;

  state.request_id = find_wire_value(wire, "request_id");
  state.confirmed_facts = parse_wire_string_vector(wire, "confirmed_facts");
  state.hypotheses = parse_wire_string_vector(wire, "hypotheses");
  state.assumptions = parse_wire_string_vector(wire, "assumptions");
  state.evidence_refs = parse_wire_string_vector(wire, "evidence_refs");
  state.confidence = parse_wire_float(wire, "confidence");

  return state;
}

// ===========================================================================
// BeliefState tests
// ===========================================================================

// Positive: BeliefState の必須フィールドが round-trip 後も変化しないことを検証。
void test_belief_state_round_trip_keeps_required_fields() {
  const auto source = make_valid_belief_state_sample();
  const auto wire = serialize_belief_state_v1(source);
  const auto restored = deserialize_belief_state_wire(wire);

  const auto guard =
      dasall::contracts::validate_belief_state_required_fields(restored);
    assert_true(guard.ok,
          std::string("round-trip BeliefState should remain guard-valid: ") +
            std::string(guard.reason));
  assert_equal(*source.request_id,
               restored.request_id.value_or(""),
               "request_id should remain stable after round-trip");
  assert_true(restored.confirmed_facts.has_value(),
              "confirmed_facts should be present after round-trip");
  assert_true(restored.hypotheses.has_value(),
              "hypotheses should be present after round-trip");
}

// Positive: unknown フィールドを追加しても既存フィールドの guard 検証が通ることを検証。
void test_belief_state_forward_compatibility_ignores_unknown_fields() {
  auto wire = serialize_belief_state_v1(make_valid_belief_state_sample());
  wire["future_reasoning_trace"] = "trace-delta-x";
  wire["future_belief_version"] = "2";

  const auto restored = deserialize_belief_state_wire(wire);
  const auto guard =
      dasall::contracts::validate_belief_state_required_fields(restored);
    assert_true(guard.ok,
          std::string("unknown additive fields should be ignored for BeliefState compatibility: ") +
            std::string(guard.reason));
}

// Negative: request_id が欠落した場合、guard 検証が失敗することを検証。
void test_belief_state_missing_required_field_is_rejected() {
  auto wire = serialize_belief_state_v1(make_valid_belief_state_sample());
  wire.erase("request_id");

  const auto restored = deserialize_belief_state_wire(wire);
  const auto guard =
      dasall::contracts::validate_belief_state_required_fields(restored);
  assert_true(!guard.ok,
              "missing request_id should fail BeliefState validation");
}

// ===========================================================================
// Checkpoint wire helpers
// ===========================================================================

// make_valid_checkpoint_sample
// 必須フィールドをすべて含む最小有効 Checkpoint を構築する。
Checkpoint make_valid_checkpoint_sample() {
  Checkpoint cp;
  // Required fields (WP03-T012 – 5 items)
  cp.checkpoint_id = "cp-t001-001";
  cp.state = CheckpointState::Running;
  cp.step_id = "step-analyze-001";
  cp.working_memory_snapshot = "snapshot-ref-001";
  cp.pending_action = "";  // 空文字列は "保留中アクションなし" を意味する有効値。
  return cp;
}

// serialize_checkpoint_v1
// Checkpoint の必須フィールドを wire map に書き出す。
WireMap serialize_checkpoint_v1(const Checkpoint& cp) {
  WireMap wire;
  if (cp.checkpoint_id.has_value()) {
    wire["checkpoint_id"] = *cp.checkpoint_id;
  }
  if (cp.state.has_value()) {
    wire["state"] = std::to_string(static_cast<int>(*cp.state));
  }
  if (cp.step_id.has_value()) {
    wire["step_id"] = *cp.step_id;
  }
  if (cp.working_memory_snapshot.has_value()) {
    wire["working_memory_snapshot"] = *cp.working_memory_snapshot;
  }
  if (cp.pending_action.has_value()) {
    wire["pending_action"] = *cp.pending_action;
  }
  return wire;
}

// deserialize_checkpoint_wire
// wire map から Checkpoint を復元する。
// CheckpointState の unknown enum 値は Unspecified にフォールバックする。
Checkpoint deserialize_checkpoint_wire(const WireMap& wire) {
  Checkpoint cp;

  cp.checkpoint_id = find_wire_value(wire, "checkpoint_id");
  cp.step_id = find_wire_value(wire, "step_id");
  cp.working_memory_snapshot = find_wire_value(wire, "working_memory_snapshot");
  cp.pending_action = find_wire_value(wire, "pending_action");

  // CheckpointState enum: 既知の値以外は Unspecified にフォールバック。
  constexpr std::array<int, 7> kKnownStates{
      static_cast<int>(CheckpointState::Unspecified),
      static_cast<int>(CheckpointState::Running),
      static_cast<int>(CheckpointState::Paused),
      static_cast<int>(CheckpointState::WaitingConfirm),
      static_cast<int>(CheckpointState::WaitingTool),
      static_cast<int>(CheckpointState::Failed),
      static_cast<int>(CheckpointState::Succeeded),
  };
  const auto raw_state = parse_wire_int64(wire, "state");
  if (raw_state.has_value()) {
    cp.state = fallback_unknown_enum_value<CheckpointState>(
        static_cast<int>(*raw_state),
        kKnownStates.data(),
        kKnownStates.size(),
        CheckpointState::Unspecified);
  }

  return cp;
}

// ===========================================================================
// Checkpoint tests
// ===========================================================================

// Positive: Checkpoint の必須フィールドが round-trip 後も変化しないことを検証。
void test_checkpoint_round_trip_keeps_required_fields() {
  const auto source = make_valid_checkpoint_sample();
  const auto wire = serialize_checkpoint_v1(source);
  const auto restored = deserialize_checkpoint_wire(wire);

  const auto guard =
      dasall::contracts::validate_checkpoint_required_fields(restored);
  assert_true(guard.ok,
              "round-trip Checkpoint should remain guard-valid");
  assert_equal(*source.checkpoint_id,
               restored.checkpoint_id.value_or(""),
               "checkpoint_id should remain stable after round-trip");
  assert_equal(*source.step_id,
               restored.step_id.value_or(""),
               "step_id should remain stable after round-trip");
  assert_equal(*source.working_memory_snapshot,
               restored.working_memory_snapshot.value_or(""),
               "working_memory_snapshot should remain stable after round-trip");
}

// Positive: unknown フィールドを追加しても既存フィールドの guard 検証が通ることを検証。
void test_checkpoint_forward_compatibility_ignores_unknown_fields() {
  auto wire = serialize_checkpoint_v1(make_valid_checkpoint_sample());
  wire["future_shard_id"] = "shard-3";
  wire["future_wal_offset"] = "4096";

  const auto restored = deserialize_checkpoint_wire(wire);
  const auto guard =
      dasall::contracts::validate_checkpoint_required_fields(restored);
  assert_true(guard.ok,
              "unknown additive fields should be ignored for Checkpoint compatibility");
}

// Negative: checkpoint_id が欠落した場合、guard 検証が失敗することを検証。
void test_checkpoint_missing_required_field_is_rejected() {
  auto wire = serialize_checkpoint_v1(make_valid_checkpoint_sample());
  wire.erase("checkpoint_id");

  const auto restored = deserialize_checkpoint_wire(wire);
  const auto guard =
      dasall::contracts::validate_checkpoint_required_fields(restored);
  assert_true(!guard.ok,
              "missing checkpoint_id should fail Checkpoint validation");
}

// ===========================================================================
// AgentResult wire helpers
// ===========================================================================

// make_valid_agent_result_sample
// 必須フィールドをすべて含む最小有効 AgentResult を構築する。
AgentResult make_valid_agent_result_sample() {
  AgentResult result;
  // Required fields (WP03-T014 – 6 items)
  result.result_id = "res-t001-001";
  result.status = AgentResultStatus::Completed;
  result.result_code = 0;
  result.response_text = "巡回点検サマリが生成されました。";
  result.task_completed = true;
  result.created_at = 1710000500000LL;
  return result;
}

// serialize_agent_result_v1
// AgentResult の必須フィールドを wire map に書き出す。
WireMap serialize_agent_result_v1(const AgentResult& result) {
  WireMap wire;
  if (result.result_id.has_value()) {
    wire["result_id"] = *result.result_id;
  }
  if (result.status.has_value()) {
    wire["status"] = std::to_string(static_cast<int>(*result.status));
  }
  if (result.result_code.has_value()) {
    wire["result_code"] = std::to_string(*result.result_code);
  }
  if (result.response_text.has_value()) {
    wire["response_text"] = *result.response_text;
  }
  if (result.task_completed.has_value()) {
    wire["task_completed"] = (*result.task_completed ? "1" : "0");
  }
  if (result.created_at.has_value()) {
    wire["created_at"] = std::to_string(*result.created_at);
  }
  return wire;
}

// deserialize_agent_result_wire
// wire map から AgentResult を復元する。
// AgentResultStatus の unknown enum 値は Unspecified にフォールバックする。
AgentResult deserialize_agent_result_wire(const WireMap& wire) {
  AgentResult result;

  result.result_id = find_wire_value(wire, "result_id");
  result.response_text = find_wire_value(wire, "response_text");
  result.task_completed = parse_wire_bool(wire, "task_completed");
  result.created_at = parse_wire_int64(wire, "created_at");

  // result_code は int32 として直接読み出す。
  const auto raw_code = parse_wire_int64(wire, "result_code");
  if (raw_code.has_value()) {
    result.result_code = static_cast<std::int32_t>(*raw_code);
  }

  // AgentResultStatus enum: 既知の値以外は Unspecified にフォールバック。
  constexpr std::array<int, 6> kKnownStatuses{
      static_cast<int>(AgentResultStatus::Unspecified),
      static_cast<int>(AgentResultStatus::Completed),
      static_cast<int>(AgentResultStatus::Failed),
      static_cast<int>(AgentResultStatus::PartiallyCompleted),
      static_cast<int>(AgentResultStatus::Cancelled),
      static_cast<int>(AgentResultStatus::Timeout),
  };
  const auto raw_status = parse_wire_int64(wire, "status");
  if (raw_status.has_value()) {
    result.status = fallback_unknown_enum_value<AgentResultStatus>(
        static_cast<int>(*raw_status),
        kKnownStatuses.data(),
        kKnownStatuses.size(),
        AgentResultStatus::Unspecified);
  }

  return result;
}

// ===========================================================================
// AgentResult tests
// ===========================================================================

// Positive: AgentResult の必須フィールドが round-trip 後も変化しないことを検証。
void test_agent_result_round_trip_keeps_required_fields() {
  const auto source = make_valid_agent_result_sample();
  const auto wire = serialize_agent_result_v1(source);
  const auto restored = deserialize_agent_result_wire(wire);

  const auto guard =
      dasall::contracts::validate_agent_result_required_fields(restored);
  assert_true(guard.ok,
              "round-trip AgentResult should remain guard-valid");
  assert_equal(*source.result_id,
               restored.result_id.value_or(""),
               "result_id should remain stable after round-trip");
  assert_equal(*source.response_text,
               restored.response_text.value_or(""),
               "response_text should remain stable after round-trip");
  assert_equal(static_cast<int>(AgentResultStatus::Completed),
               static_cast<int>(restored.status.value_or(AgentResultStatus::Unspecified)),
               "status should remain Completed after round-trip");
}

// Positive: unknown フィールドを追加しても既存フィールドの guard 検証が通ることを検証。
void test_agent_result_forward_compatibility_ignores_unknown_fields() {
  auto wire = serialize_agent_result_v1(make_valid_agent_result_sample());
  wire["future_cost_tokens"] = "512";
  wire["future_llm_model_id"] = "model-v3";

  const auto restored = deserialize_agent_result_wire(wire);
  const auto guard =
      dasall::contracts::validate_agent_result_required_fields(restored);
  assert_true(guard.ok,
              "unknown additive fields should be ignored for AgentResult compatibility");
}

// Negative: result_id が欠落した場合、guard 検証が失敗することを検証。
void test_agent_result_missing_required_field_is_rejected() {
  auto wire = serialize_agent_result_v1(make_valid_agent_result_sample());
  wire.erase("result_id");

  const auto restored = deserialize_agent_result_wire(wire);
  const auto guard =
      dasall::contracts::validate_agent_result_required_fields(restored);
  assert_true(!guard.ok,
              "missing result_id should fail AgentResult validation");
}

}  // namespace

// ===========================================================================
// main
// ===========================================================================
int main() {
  int passed = 0;
  int failed = 0;

  // 共通テストランナー：例外捕捉とパス/フェイルカウントを統一する。
  auto run_test = [&](const char* name, void (*fn)()) {
    try {
      fn();
      ++passed;
      std::cout << "  PASS: " << name << "\n";
    } catch (const std::exception& ex) {
      ++failed;
      std::cerr << "  FAIL: " << name << " - " << ex.what() << "\n";
    }
  };

  // バナーテキストで ctest 出力と T001 タスクを紐付ける。
  std::cout << "MainFlowSerializationContractTest - T001\n";

  // --- GoalContract ---
  run_test("test_goal_contract_round_trip_keeps_required_fields",
           test_goal_contract_round_trip_keeps_required_fields);
  run_test("test_goal_contract_forward_compatibility_ignores_unknown_fields",
           test_goal_contract_forward_compatibility_ignores_unknown_fields);
  run_test("test_goal_contract_missing_required_field_is_rejected",
           test_goal_contract_missing_required_field_is_rejected);

  // --- Observation ---
  run_test("test_observation_round_trip_keeps_required_fields",
           test_observation_round_trip_keeps_required_fields);
  run_test("test_observation_forward_compatibility_ignores_unknown_fields",
           test_observation_forward_compatibility_ignores_unknown_fields);
  run_test("test_observation_missing_required_field_is_rejected",
           test_observation_missing_required_field_is_rejected);

  // --- ObservationDigest ---
  run_test("test_observation_digest_round_trip_keeps_required_fields",
           test_observation_digest_round_trip_keeps_required_fields);
  run_test(
      "test_observation_digest_forward_compatibility_ignores_unknown_fields",
      test_observation_digest_forward_compatibility_ignores_unknown_fields);
  run_test("test_observation_digest_missing_required_field_is_rejected",
           test_observation_digest_missing_required_field_is_rejected);

  // --- BeliefState ---
  run_test("test_belief_state_round_trip_keeps_required_fields",
           test_belief_state_round_trip_keeps_required_fields);
  run_test("test_belief_state_forward_compatibility_ignores_unknown_fields",
           test_belief_state_forward_compatibility_ignores_unknown_fields);
  run_test("test_belief_state_missing_required_field_is_rejected",
           test_belief_state_missing_required_field_is_rejected);

  // --- Checkpoint ---
  run_test("test_checkpoint_round_trip_keeps_required_fields",
           test_checkpoint_round_trip_keeps_required_fields);
  run_test("test_checkpoint_forward_compatibility_ignores_unknown_fields",
           test_checkpoint_forward_compatibility_ignores_unknown_fields);
  run_test("test_checkpoint_missing_required_field_is_rejected",
           test_checkpoint_missing_required_field_is_rejected);

  // --- AgentResult ---
  run_test("test_agent_result_round_trip_keeps_required_fields",
           test_agent_result_round_trip_keeps_required_fields);
  run_test("test_agent_result_forward_compatibility_ignores_unknown_fields",
           test_agent_result_forward_compatibility_ignores_unknown_fields);
  run_test("test_agent_result_missing_required_field_is_rejected",
           test_agent_result_missing_required_field_is_rejected);

  // summary 出力は既存 contract-test 規約に準拠する。
  std::cout << "\nResults: " << passed << " passed, " << failed
            << " failed, " << (passed + failed) << " total\n";

  return (failed > 0) ? 1 : 0;
}
