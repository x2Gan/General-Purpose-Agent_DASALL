#include "context/ContextOrchestrator.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

#include "context/ContextPacketGuards.h"

namespace dasall::memory {
namespace {

struct SlotProjection {
  std::string current_goal_summary;
  std::vector<std::string> recent_history;
  std::string summary_memory;
  std::vector<std::string> retrieval_evidence;
  std::string latest_observation_digest_summary;
  std::vector<std::string> active_tools;
  std::string policy_digest;
  std::string belief_state_summary;
};

std::int64_t current_time_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

int estimate_text_tokens(std::string_view text) {
  if (text.empty()) {
    return 0;
  }

  int ascii_bytes = 0;
  int multibyte_characters = 0;
  for (const unsigned char byte : text) {
    if (byte < 0x80U) {
      ++ascii_bytes;
      continue;
    }
    if ((byte & 0xC0U) != 0x80U) {
      ++multibyte_characters;
    }
  }

  return std::max(1, ((ascii_bytes + 3) / 4) + (multibyte_characters * 2));
}

void append_unique(std::vector<std::string>& destination,
                   const std::string& value) {
  if (std::find(destination.begin(), destination.end(), value) == destination.end()) {
    destination.push_back(value);
  }
}

void append_all_unique(std::vector<std::string>& destination,
                       const std::vector<std::string>& source) {
  for (const auto& value : source) {
    append_unique(destination, value);
  }
}

std::string join_strings(const std::vector<std::string>& values,
                         std::string_view separator) {
  std::ostringstream stream;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0U) {
      stream << separator;
    }
    stream << values[index];
  }
  return stream.str();
}

std::vector<contracts::Turn> make_chronological_turns(
    const std::vector<contracts::Turn>& recent_turns) {
  auto turns = recent_turns;
  std::reverse(turns.begin(), turns.end());
  return turns;
}

std::string trim_text_to_token_limit(const std::string& text, int token_limit) {
  if (text.empty()) {
    return {};
  }

  if (token_limit <= 0) {
    return {};
  }

  const auto estimated_tokens = estimate_text_tokens(text);
  if (estimated_tokens <= token_limit) {
    return text;
  }

  const auto keep_ratio = static_cast<double>(token_limit) /
                          static_cast<double>(std::max(1, estimated_tokens));
  const auto keep_size = std::max<std::size_t>(1U,
                                               static_cast<std::size_t>(text.size() * keep_ratio));
  if (keep_size >= text.size()) {
    return text;
  }

  if (keep_size <= 3U) {
    return text.substr(0, keep_size);
  }

  return text.substr(0, keep_size - 3U) + "...";
}

std::vector<std::string> trim_vector_to_token_limit(
    const std::vector<std::string>& values,
    int token_limit,
    bool keep_tail) {
  if (values.empty()) {
    return {};
  }

  if (token_limit <= 0) {
    return {};
  }

  std::vector<std::string> trimmed;
  int used_tokens = 0;

  if (keep_tail) {
    for (auto it = values.rbegin(); it != values.rend(); ++it) {
      const auto item_tokens = estimate_text_tokens(*it);
      if (used_tokens + item_tokens <= token_limit) {
        trimmed.push_back(*it);
        used_tokens += item_tokens;
        continue;
      }

      const auto remaining_tokens = token_limit - used_tokens;
      const auto truncated = trim_text_to_token_limit(*it, remaining_tokens);
      if (!truncated.empty()) {
        trimmed.push_back(truncated);
      }
      break;
    }

    std::reverse(trimmed.begin(), trimmed.end());
    return trimmed;
  }

  for (const auto& value : values) {
    const auto item_tokens = estimate_text_tokens(value);
    if (used_tokens + item_tokens <= token_limit) {
      trimmed.push_back(value);
      used_tokens += item_tokens;
      continue;
    }

    const auto remaining_tokens = token_limit - used_tokens;
    const auto truncated = trim_text_to_token_limit(value, remaining_tokens);
    if (!truncated.empty()) {
      trimmed.push_back(truncated);
    }
    break;
  }

  return trimmed;
}

std::optional<std::string> find_snapshot_slot(
    const WorkingMemorySnapshot& snapshot,
    std::initializer_list<std::string_view> keys) {
  for (const auto key : keys) {
    const auto it = std::find_if(snapshot.slots.begin(), snapshot.slots.end(),
                                 [key](const WorkingMemorySlot& slot) {
                                   return slot.key == key;
                                 });
    if (it != snapshot.slots.end() && !it->value.empty()) {
      return it->value;
    }
  }
  return std::nullopt;
}

std::optional<std::string> latest_user_turn(
    const CandidateSet& candidates) {
  if (!candidates.session_bundle.recent_turns.empty() &&
      candidates.session_bundle.recent_turns.front().user_input.has_value() &&
      !candidates.session_bundle.recent_turns.front().user_input->empty()) {
    return candidates.session_bundle.recent_turns.front().user_input;
  }

  return find_snapshot_slot(candidates.working_snapshot,
                            {"user_turn", "latest_user_turn"});
}

std::string fallback_user_turn(const CandidateSet& candidates,
                               const MemoryContextRequest& request) {
  const auto candidate = latest_user_turn(candidates);
  if (candidate.has_value() && !candidate->empty()) {
    return *candidate;
  }

  if (!request.goal_summary.empty()) {
    return request.goal_summary;
  }

  return "context unavailable";
}

std::string fallback_goal_summary(const CandidateSet& candidates,
                                  const MemoryContextRequest& request) {
  if (!request.goal_summary.empty()) {
    return request.goal_summary;
  }

  const auto slot_value = find_snapshot_slot(candidates.working_snapshot,
                                             {"current_goal_summary", "goal_summary"});
  if (slot_value.has_value()) {
    return *slot_value;
  }

  return "goal unavailable";
}

std::vector<std::string> serialize_recent_history(const CandidateSet& candidates) {
  std::vector<std::string> entries;
  for (const auto& turn : make_chronological_turns(candidates.session_bundle.recent_turns)) {
    if (turn.user_input.has_value() && !turn.user_input->empty()) {
      entries.push_back("[user] " + *turn.user_input);
    }
    if (turn.agent_response.has_value() && !turn.agent_response->empty()) {
      entries.push_back("[assistant] " + *turn.agent_response);
    }
    if (turn.tool_call_refs.has_value() && !turn.tool_call_refs->empty()) {
      entries.push_back("[tools] " + join_strings(*turn.tool_call_refs, ", "));
    }
    if (turn.observation_refs.has_value() && !turn.observation_refs->empty()) {
      entries.push_back("[observations] " + join_strings(*turn.observation_refs, ", "));
    }
  }
  return entries;
}

std::vector<std::string> build_retrieval_evidence_entries(
    const CandidateSet& candidates) {
  std::vector<std::string> entries;
  auto sorted_hits = candidates.vector_hits;
  std::sort(sorted_hits.begin(), sorted_hits.end(),
            [](const VectorHit& left, const VectorHit& right) {
              if (left.score != right.score) {
                return left.score > right.score;
              }
              return left.doc_id < right.doc_id;
            });

  for (const auto& hit : sorted_hits) {
    entries.push_back("[vector] " + hit.doc_id + " (" + hit.doc_type + ") " +
                      hit.text_snippet);
  }
  append_all_unique(entries, candidates.external_evidence);
  return entries;
}

std::string build_belief_state_summary(const CandidateSet& candidates) {
  if (candidates.relevant_facts.empty()) {
    return {};
  }

  auto facts = candidates.relevant_facts;
  std::sort(facts.begin(), facts.end(),
            [](const contracts::MemoryFact& left,
               const contracts::MemoryFact& right) {
              const auto left_confidence = left.confidence_score.value_or(0U);
              const auto right_confidence = right.confidence_score.value_or(0U);
              if (left_confidence != right_confidence) {
                return left_confidence > right_confidence;
              }
              return left.fact_id.value_or(std::string{}) <
                     right.fact_id.value_or(std::string{});
            });

  std::vector<std::string> parts;
  for (const auto& fact : facts) {
    if (!fact.fact_text.has_value() || fact.fact_text->empty()) {
      continue;
    }
    parts.push_back(*fact.fact_text + " (" +
                    std::to_string(fact.confidence_score.value_or(0U)) + "%)");
  }

  if (parts.empty()) {
    return {};
  }

  return "已确认事实：" + join_strings(parts, "；");
}

const TrimAction* find_trim_action(const BudgetPlan& plan,
                                   const std::string& slot_name) {
  const auto it = std::find_if(plan.trim_actions.begin(), plan.trim_actions.end(),
                               [&slot_name](const TrimAction& action) {
                                 return action.slot_name == slot_name;
                               });
  if (it == plan.trim_actions.end()) {
    return nullptr;
  }
  return &(*it);
}

int effective_slot_limit(const BudgetPlan& plan, const std::string& slot_name) {
  const auto* trim_action = find_trim_action(plan, slot_name);
  if (trim_action != nullptr) {
    return trim_action->target_tokens;
  }

  return std::numeric_limits<int>::max();
}

BudgetPlan make_fallback_budget_plan(int total_token_budget) {
  static constexpr const char* kFallbackSlots[] = {
      "user_turn",
      "current_goal_summary",
      "policy_digest",
      "latest_observation_digest_summary",
      "recent_history",
      "summary_memory",
      "belief_state_summary",
      "retrieval_evidence",
      "active_tools",
  };

  BudgetPlan plan;
  plan.total_token_budget = std::max(0, total_token_budget);
  const auto per_slot = plan.total_token_budget > 0
                            ? std::max(1, plan.total_token_budget /
                                             static_cast<int>(std::size(kFallbackSlots)))
                            : 0;
  for (std::size_t index = 0; index < std::size(kFallbackSlots); ++index) {
    plan.slot_budgets.push_back(SlotBudget{
        .slot_name = kFallbackSlots[index],
        .allocated_tokens = per_slot,
        .estimated_tokens = 0,
        .priority = static_cast<int>(100 - index),
    });
  }
  return plan;
}

std::string build_minimal_budget_report(const MemoryContextRequest& request) {
  return std::string{"token_budget_hint="} + std::to_string(request.token_budget_hint);
}

bool warning_implies_degraded(const std::string& warning) {
  return warning.find("unavailable") != std::string::npos ||
         warning.find("failed") != std::string::npos ||
         warning.find("fallback") != std::string::npos ||
         warning.find("skipped") != std::string::npos ||
         warning == "candidate_collection_missing";
}

std::vector<contracts::Turn> trim_recent_turns_after_compression(
    std::vector<contracts::Turn> recent_turns,
    int compression_trigger_turns) {
  const auto keep_count = std::max<std::size_t>(
      1U, static_cast<std::size_t>(std::max(1, compression_trigger_turns / 4)));
  if (recent_turns.size() <= keep_count) {
    return recent_turns;
  }

  recent_turns.resize(keep_count);
  return recent_turns;
}

contracts::ContextPacket make_minimal_packet(const MemoryContextRequest& request) {
  contracts::ContextPacket packet;
  packet.request_id = request.request_id.empty() ? "context-request" : request.request_id;
  packet.user_turn = !request.goal_summary.empty() ? request.goal_summary
                                                   : std::string{"context unavailable"};
  packet.current_goal_summary = !request.goal_summary.empty()
                                    ? request.goal_summary
                                    : std::string{"goal unavailable"};
  packet.recent_history = std::vector<std::string>{};
  if (!request.latest_observation_digest_summary.empty()) {
    packet.latest_observation_digest_summary = request.latest_observation_digest_summary;
  }
  if (!request.visible_tools.empty()) {
    packet.active_tools = request.visible_tools;
  }
  if (!request.constraints_summary.empty()) {
    packet.policy_digest = request.constraints_summary;
  }
  packet.token_budget_report = build_minimal_budget_report(request);
  packet.created_at = current_time_ms();
  packet.tags = std::vector<std::string>{"memory", "context"};
  return packet;
}

SlotProjection project_slots(const CandidateSet& candidates,
                             const MemoryContextRequest& request) {
  SlotProjection projection;
  projection.current_goal_summary = fallback_goal_summary(candidates, request);
  projection.recent_history = serialize_recent_history(candidates);
  if (candidates.latest_summary.has_value() &&
      candidates.latest_summary->summary_text.has_value()) {
    projection.summary_memory = *candidates.latest_summary->summary_text;
  }
  projection.retrieval_evidence = build_retrieval_evidence_entries(candidates);
  projection.latest_observation_digest_summary =
      request.latest_observation_digest_summary;
  projection.active_tools = request.visible_tools;
  projection.policy_digest = request.constraints_summary;
  projection.belief_state_summary = build_belief_state_summary(candidates);
  return projection;
}

std::string build_budget_report(const BudgetPlan& plan,
                                const contracts::ContextPacket& packet) {
  std::vector<std::string> items;
  const auto add_item = [&items](const std::string& slot_name,
                                 int allocated_tokens,
                                 int used_tokens) {
    items.push_back(slot_name + ": " + std::to_string(allocated_tokens) + "/" +
                    std::to_string(used_tokens));
  };

  for (const auto& slot_budget : plan.slot_budgets) {
    int used_tokens = 0;
    if (slot_budget.slot_name == "user_turn") {
      used_tokens = estimate_text_tokens(packet.user_turn.value_or(std::string{}));
    } else if (slot_budget.slot_name == "current_goal_summary") {
      used_tokens = estimate_text_tokens(
          packet.current_goal_summary.value_or(std::string{}));
    } else if (slot_budget.slot_name == "policy_digest") {
      used_tokens = estimate_text_tokens(packet.policy_digest.value_or(std::string{}));
    } else if (slot_budget.slot_name == "latest_observation_digest_summary") {
      used_tokens = estimate_text_tokens(
          packet.latest_observation_digest_summary.value_or(std::string{}));
    } else if (slot_budget.slot_name == "recent_history") {
      if (packet.recent_history.has_value()) {
        for (const auto& item : *packet.recent_history) {
          used_tokens += estimate_text_tokens(item);
        }
      }
    } else if (slot_budget.slot_name == "summary_memory") {
      used_tokens = estimate_text_tokens(packet.summary_memory.value_or(std::string{}));
    } else if (slot_budget.slot_name == "belief_state_summary") {
      used_tokens = estimate_text_tokens(
          packet.belief_state_summary.value_or(std::string{}));
    } else if (slot_budget.slot_name == "retrieval_evidence") {
      if (packet.retrieval_evidence.has_value()) {
        for (const auto& item : *packet.retrieval_evidence) {
          used_tokens += estimate_text_tokens(item);
        }
      }
    } else if (slot_budget.slot_name == "active_tools") {
      if (packet.active_tools.has_value()) {
        for (const auto& tool : *packet.active_tools) {
          used_tokens += estimate_text_tokens(tool);
        }
      }
    }

    add_item(slot_budget.slot_name, slot_budget.allocated_tokens, used_tokens);
  }

  items.push_back(std::string{"estimated_total="} +
                  std::to_string(plan.estimated_total_tokens));
  items.push_back(std::string{"over_budget="} + (plan.over_budget ? "true" : "false"));
  return join_strings(items, "; ");
}

std::vector<std::string> detect_dropped_sections(
    const SlotProjection& projection,
    const contracts::ContextPacket& packet) {
  std::vector<std::string> dropped_sections;

  if (projection.recent_history.size() >
      packet.recent_history.value_or(std::vector<std::string>{}).size()) {
    append_unique(dropped_sections, "recent_history");
  }

  if (!projection.summary_memory.empty() &&
      packet.summary_memory.value_or(std::string{}) != projection.summary_memory) {
    append_unique(dropped_sections, "summary_memory");
  }

  if (projection.retrieval_evidence.size() >
      packet.retrieval_evidence.value_or(std::vector<std::string>{}).size()) {
    append_unique(dropped_sections, "retrieval_evidence");
  }

  if (!projection.latest_observation_digest_summary.empty() &&
      packet.latest_observation_digest_summary.value_or(std::string{}) !=
          projection.latest_observation_digest_summary) {
    append_unique(dropped_sections, "latest_observation_digest_summary");
  }

  if (projection.active_tools.size() >
      packet.active_tools.value_or(std::vector<std::string>{}).size()) {
    append_unique(dropped_sections, "active_tools");
  }

  if (!projection.policy_digest.empty() &&
      packet.policy_digest.value_or(std::string{}) != projection.policy_digest) {
    append_unique(dropped_sections, "policy_digest");
  }

  if (!projection.belief_state_summary.empty() &&
      packet.belief_state_summary.value_or(std::string{}) !=
          projection.belief_state_summary) {
    append_unique(dropped_sections, "belief_state_summary");
  }

  return dropped_sections;
}

}  // namespace

ContextOrchestrator::ContextOrchestrator(
    std::unique_ptr<CandidateCollector> collector,
    std::unique_ptr<BudgetAllocator> allocator,
    std::unique_ptr<CompressionCoordinator> compressor,
    const MemoryConfig& config)
    : collector_(std::move(collector)),
      allocator_(std::move(allocator)),
      compressor_(std::move(compressor)),
      context_config_(config.context) {}

ContextAssemblyResult ContextOrchestrator::assemble(
    const MemoryContextRequest& request) {
  ContextAssemblyResult result;

  if (!collector_ || !allocator_) {
    result.context_packet = make_minimal_packet(request);
    result.warnings.push_back("candidate_collection_missing");
    result.degraded = true;
    return result;
  }

  CandidateSet candidates;
  try {
    candidates = collector_->collect(CandidateCollectRequest{
        .session_id = request.session_id,
        .stage = request.stage,
        .goal_summary = request.goal_summary,
        .token_budget_hint = request.token_budget_hint,
        .latency_budget_ms = request.latency_budget_ms,
        .external_evidence = request.external_evidence,
    });
  } catch (...) {
    result.context_packet = make_minimal_packet(request);
    result.warnings.push_back("candidate_collection_failed");
    result.degraded = true;
    return result;
  }

  append_all_unique(result.warnings, candidates.warnings);

  if (needs_compression(candidates, request.token_budget_hint)) {
    if (compressor_) {
      try {
        auto compression_output = compressor_->compress(CompressionInput{
            .session_id = request.session_id,
            .source_turns = make_chronological_turns(candidates.session_bundle.recent_turns),
            .existing_summary = candidates.latest_summary,
            .target_token_budget = request.token_budget_hint,
            .materialize_latest_summary = false,
            .strategy_hint = "auto",
        });

        if (compression_output.compression_applied) {
          candidates.latest_summary = compression_output.summary;
          candidates.session_bundle.recent_turns = trim_recent_turns_after_compression(
              std::move(candidates.session_bundle.recent_turns),
              std::max(1, context_config_.compression_trigger_turns));
          append_all_unique(result.compression_notes,
                            compression_output.compression_notes);
        }
      } catch (...) {
        result.warnings.push_back("compression_skipped");
      }
    } else {
      result.warnings.push_back("compression_skipped");
    }
  }

  BudgetPlan plan;
  try {
    plan = allocator_->allocate(
        candidates,
        BudgetPolicy{
            .total_token_budget = std::max(0, request.token_budget_hint),
            .stage = request.stage,
            .risk_level = 0,
            .latency_budget_ms = request.latency_budget_ms,
        });
  } catch (...) {
    result.warnings.push_back("budget_allocator_fallback");
    plan = make_fallback_budget_plan(request.token_budget_hint);
  }

  const auto packet = build_packet(candidates, plan, request);
  const auto slot_projection = project_slots(candidates, request);
  result.context_packet = packet;
  result.context_packet.token_budget_report = build_budget_report(plan, result.context_packet);
  result.context_packet.created_at = current_time_ms();
  result.context_packet.tags = std::vector<std::string>{"memory", "context"};
  result.dropped_sections = detect_dropped_sections(slot_projection, result.context_packet);
  for (const auto& dropped_section : result.dropped_sections) {
    append_unique(result.warnings, dropped_section + "_trimmed");
  }

  if (latest_user_turn(candidates) == std::nullopt) {
    append_unique(result.warnings, "user_turn_fallback_goal_summary");
  }
  if (request.goal_summary.empty() &&
      result.context_packet.current_goal_summary.has_value()) {
    append_unique(result.warnings, "goal_summary_fallback_working_memory");
  }
  if (plan.over_budget) {
    append_unique(result.warnings, "token_budget_still_over_limit");
  }

  const auto guard_result = contracts::validate_context_packet_field_rules(
      result.context_packet);
  if (!guard_result.ok) {
    result.context_packet = make_minimal_packet(request);
    append_unique(result.warnings,
                  std::string{"context_packet_guard_failed:"} +
                      std::string{guard_result.reason});
  }

  result.degraded = plan.over_budget ||
                    std::any_of(result.warnings.begin(), result.warnings.end(),
                                [](const std::string& warning) {
                                  return warning_implies_degraded(warning);
                                });
  return result;
}

contracts::ContextPacket ContextOrchestrator::build_packet(
    const CandidateSet& candidates,
    const BudgetPlan& plan,
    const MemoryContextRequest& request) const {
  const auto projection = project_slots(candidates, request);

  contracts::ContextPacket packet;
  packet.request_id = request.request_id.empty() ? "context-request" : request.request_id;
  packet.user_turn = fallback_user_turn(candidates, request);
  packet.current_goal_summary = fallback_goal_summary(candidates, request);
  packet.recent_history = trim_vector_to_token_limit(
      projection.recent_history,
      effective_slot_limit(plan, "recent_history"),
      true);

  const auto summary_memory = trim_text_to_token_limit(
      projection.summary_memory,
      effective_slot_limit(plan, "summary_memory"));
  if (!summary_memory.empty()) {
    packet.summary_memory = summary_memory;
  }

  const auto retrieval_evidence = trim_vector_to_token_limit(
      projection.retrieval_evidence,
      effective_slot_limit(plan, "retrieval_evidence"),
      false);
  if (!retrieval_evidence.empty()) {
    packet.retrieval_evidence = retrieval_evidence;
  }

  const auto latest_observation = trim_text_to_token_limit(
      projection.latest_observation_digest_summary,
      effective_slot_limit(plan, "latest_observation_digest_summary"));
  if (!latest_observation.empty()) {
    packet.latest_observation_digest_summary = latest_observation;
  }

  const auto active_tools = trim_vector_to_token_limit(
      projection.active_tools,
      effective_slot_limit(plan, "active_tools"),
      false);
  if (!active_tools.empty()) {
    packet.active_tools = active_tools;
  }

  const auto policy_digest = trim_text_to_token_limit(
      projection.policy_digest,
      effective_slot_limit(plan, "policy_digest"));
  if (!policy_digest.empty()) {
    packet.policy_digest = policy_digest;
  }

  const auto belief_state_summary = trim_text_to_token_limit(
      projection.belief_state_summary,
      effective_slot_limit(plan, "belief_state_summary"));
  if (!belief_state_summary.empty()) {
    packet.belief_state_summary = belief_state_summary;
  }

  return packet;
}

bool ContextOrchestrator::needs_compression(const CandidateSet& candidates,
                                            int token_budget_hint) const {
  if (candidates.session_bundle.recent_turns.size() >=
      static_cast<std::size_t>(std::max(1, context_config_.compression_trigger_turns))) {
    return true;
  }

  if (token_budget_hint <= 0) {
    return false;
  }

  const auto threshold = static_cast<double>(token_budget_hint) *
                         context_config_.compression_trigger_ratio;
  return static_cast<double>(candidates.estimated_total_tokens) > threshold;
}

}  // namespace dasall::memory