#include "CheckpointManager.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "CheckpointStateMapper.h"

namespace dasall::runtime {
namespace {

struct ParsedCheckpointRecord {
  contracts::Checkpoint checkpoint;
  std::optional<contracts::BudgetSnapshot> runtime_budget_snapshot;
};

enum class DurableCheckpointReadStatus : std::uint8_t {
  NotFound = 0,
  Loaded,
  Corrupt,
};

struct DurableCheckpointReadResult {
  DurableCheckpointReadStatus status = DurableCheckpointReadStatus::NotFound;
  std::optional<ParsedCheckpointRecord> record;
  std::string detail;
};

[[nodiscard]] std::string hex_encode(std::string_view value) {
  static constexpr char kHexDigits[] = "0123456789abcdef";

  std::string encoded;
  encoded.reserve(value.size() * 2U);
  for (const unsigned char ch : value) {
    encoded.push_back(kHexDigits[(ch >> 4U) & 0x0FU]);
    encoded.push_back(kHexDigits[ch & 0x0FU]);
  }
  return encoded;
}

[[nodiscard]] int hex_value(const char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return 10 + (ch - 'a');
  }
  if (ch >= 'A' && ch <= 'F') {
    return 10 + (ch - 'A');
  }
  return -1;
}

[[nodiscard]] bool hex_decode(std::string_view value, std::string& decoded) {
  if ((value.size() % 2U) != 0U) {
    return false;
  }

  decoded.clear();
  decoded.reserve(value.size() / 2U);
  for (std::size_t index = 0; index < value.size(); index += 2U) {
    const auto high = hex_value(value[index]);
    const auto low = hex_value(value[index + 1U]);
    if (high < 0 || low < 0) {
      return false;
    }
    decoded.push_back(static_cast<char>((high << 4U) | low));
  }
  return true;
}

void write_field(std::ostream& output,
                 const std::string_view key,
                 const std::string_view value) {
  output << key << '=' << value << '\n';
}

template <typename Integer>
void write_integer_field(std::ostream& output,
                         const std::string_view key,
                         const Integer value) {
  output << key << '=' << value << '\n';
}

void write_encoded_field(std::ostream& output,
                         const std::string_view key,
                         const std::string& value) {
  write_field(output, key, hex_encode(value));
}

void write_optional_encoded_field(std::ostream& output,
                                  const std::string_view key,
                                  const std::optional<std::string>& value) {
  if (value.has_value()) {
    write_encoded_field(output, key, *value);
  }
}

template <typename Integer>
void write_optional_integer_field(std::ostream& output,
                                  const std::string_view key,
                                  const std::optional<Integer>& value) {
  if (value.has_value()) {
    write_integer_field(output, key, *value);
  }
}

[[nodiscard]] std::filesystem::path checkpoint_record_path(
    const std::string_view state_root,
    const std::string_view checkpoint_ref) {
  return std::filesystem::path(std::string(state_root)) /
         "checkpoints" /
         (hex_encode(checkpoint_ref) + ".kv");
}

[[nodiscard]] bool parse_key_value_file(
    std::istream& input,
    std::unordered_map<std::string, std::string>& fields,
    std::string& detail) {
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty()) {
      continue;
    }

    const auto separator = line.find('=');
    if (separator == std::string::npos) {
      detail = "durable checkpoint record contains an invalid key-value line";
      return false;
    }

    fields.emplace(line.substr(0, separator), line.substr(separator + 1U));
  }

  detail.clear();
  return true;
}

[[nodiscard]] bool decode_required_string_field(
    const std::unordered_map<std::string, std::string>& fields,
    const std::string_view key,
    std::optional<std::string>& target,
    std::string& detail) {
  const auto iterator = fields.find(std::string(key));
  if (iterator == fields.end()) {
    detail = std::string("durable checkpoint record is missing ") + std::string(key);
    return false;
  }

  std::string decoded;
  if (!hex_decode(iterator->second, decoded)) {
    detail = std::string("durable checkpoint record could not decode ") + std::string(key);
    return false;
  }

  target = std::move(decoded);
  return true;
}

[[nodiscard]] bool decode_optional_string_field(
    const std::unordered_map<std::string, std::string>& fields,
    const std::string_view key,
    std::optional<std::string>& target,
    std::string& detail) {
  const auto iterator = fields.find(std::string(key));
  if (iterator == fields.end()) {
    target = std::nullopt;
    return true;
  }

  std::string decoded;
  if (!hex_decode(iterator->second, decoded)) {
    detail = std::string("durable checkpoint record could not decode ") + std::string(key);
    return false;
  }

  target = std::move(decoded);
  return true;
}

template <typename Integer>
[[nodiscard]] bool decode_optional_integer_field(
    const std::unordered_map<std::string, std::string>& fields,
    const std::string_view key,
    std::optional<Integer>& target,
    std::string& detail) {
  const auto iterator = fields.find(std::string(key));
  if (iterator == fields.end()) {
    target = std::nullopt;
    return true;
  }

  try {
    if constexpr (std::is_same_v<Integer, std::uint32_t>) {
      target = static_cast<std::uint32_t>(std::stoul(iterator->second));
    } else if constexpr (std::is_same_v<Integer, std::uint64_t>) {
      target = static_cast<std::uint64_t>(std::stoull(iterator->second));
    } else if constexpr (std::is_same_v<Integer, std::int64_t>) {
      target = static_cast<std::int64_t>(std::stoll(iterator->second));
    } else {
      static_assert(sizeof(Integer) == 0U, "unsupported integer type");
    }
  } catch (const std::exception&) {
    detail = std::string("durable checkpoint record has an invalid integer for ") +
             std::string(key);
    return false;
  }

  return true;
}

template <typename Integer>
[[nodiscard]] bool decode_required_integer_field(
    const std::unordered_map<std::string, std::string>& fields,
    const std::string_view key,
    Integer& target,
    std::string& detail) {
  std::optional<Integer> decoded;
  if (!decode_optional_integer_field(fields, key, decoded, detail)) {
    return false;
  }
  if (!decoded.has_value()) {
    detail = std::string("durable checkpoint record is missing ") + std::string(key);
    return false;
  }

  target = *decoded;
  return true;
}

[[nodiscard]] std::optional<contracts::BudgetType> parse_budget_type(
    const std::uint32_t raw_value) {
  switch (raw_value) {
    case 0:
      return contracts::BudgetType::Token;
    case 1:
      return contracts::BudgetType::Turn;
    case 2:
      return contracts::BudgetType::ToolCall;
    case 3:
      return contracts::BudgetType::Latency;
    case 4:
      return contracts::BudgetType::Replan;
    default:
      return std::nullopt;
  }
}

[[nodiscard]] DurableCheckpointReadResult read_durable_checkpoint_record(
    const std::string_view state_root,
    const std::string_view checkpoint_ref) {
  const auto record_path = checkpoint_record_path(state_root, checkpoint_ref);
  if (!std::filesystem::exists(record_path)) {
    return DurableCheckpointReadResult{
        .status = DurableCheckpointReadStatus::NotFound,
        .record = std::nullopt,
        .detail = "checkpoint not found in durable store",
    };
  }

  std::ifstream input(record_path, std::ios::binary);
  if (!input.is_open()) {
    return DurableCheckpointReadResult{
        .status = DurableCheckpointReadStatus::Corrupt,
        .record = std::nullopt,
        .detail = "checkpoint durable store could not open the checkpoint record",
    };
  }

  std::unordered_map<std::string, std::string> fields;
  std::string detail;
  if (!parse_key_value_file(input, fields, detail)) {
    return DurableCheckpointReadResult{
        .status = DurableCheckpointReadStatus::Corrupt,
        .record = std::nullopt,
        .detail = std::move(detail),
    };
  }

  ParsedCheckpointRecord record;
  if (!decode_required_string_field(fields, "checkpoint_id", record.checkpoint.checkpoint_id, detail) ||
      !decode_required_string_field(fields, "step_id", record.checkpoint.step_id, detail) ||
      !decode_required_string_field(fields,
                                    "working_memory_snapshot",
                                    record.checkpoint.working_memory_snapshot,
                                    detail) ||
      !decode_required_string_field(fields,
                                    "pending_action",
                                    record.checkpoint.pending_action,
                                    detail) ||
      !decode_optional_string_field(fields, "request_id", record.checkpoint.request_id, detail) ||
      !decode_optional_string_field(fields, "goal_id", record.checkpoint.goal_id, detail) ||
      !decode_optional_string_field(fields,
                                    "belief_state_ref",
                                    record.checkpoint.belief_state_ref,
                                    detail) ||
      !decode_optional_integer_field(fields,
                                     "retry_count",
                                     record.checkpoint.retry_count,
                                     detail) ||
      !decode_optional_integer_field(fields,
                                     "created_at",
                                     record.checkpoint.created_at,
                                     detail)) {
    return DurableCheckpointReadResult{
        .status = DurableCheckpointReadStatus::Corrupt,
        .record = std::nullopt,
        .detail = std::move(detail),
    };
  }

  std::uint32_t raw_state = 0;
  if (!decode_required_integer_field(fields, "state", raw_state, detail)) {
    return DurableCheckpointReadResult{
        .status = DurableCheckpointReadStatus::Corrupt,
        .record = std::nullopt,
        .detail = std::move(detail),
    };
  }
  record.checkpoint.state = static_cast<contracts::CheckpointState>(raw_state);

  std::uint32_t tag_count = 0;
  if (!decode_required_integer_field(fields, "tag_count", tag_count, detail)) {
    return DurableCheckpointReadResult{
        .status = DurableCheckpointReadStatus::Corrupt,
        .record = std::nullopt,
        .detail = std::move(detail),
    };
  }

  if (tag_count > 0U) {
    std::vector<std::string> tags;
    tags.reserve(tag_count);
    for (std::uint32_t index = 0; index < tag_count; ++index) {
      std::optional<std::string> tag_value;
      if (!decode_required_string_field(
              fields,
              std::string("tag.") + std::to_string(index),
              tag_value,
              detail)) {
        return DurableCheckpointReadResult{
            .status = DurableCheckpointReadStatus::Corrupt,
            .record = std::nullopt,
            .detail = std::move(detail),
        };
      }
      tags.push_back(*tag_value);
    }
    record.checkpoint.tags = std::move(tags);
  }

  std::uint32_t budget_snapshot_present = 0;
  if (!decode_required_integer_field(
          fields,
          "budget_snapshot_present",
          budget_snapshot_present,
          detail)) {
    return DurableCheckpointReadResult{
        .status = DurableCheckpointReadStatus::Corrupt,
        .record = std::nullopt,
        .detail = std::move(detail),
    };
  }

  if (budget_snapshot_present == 1U) {
    contracts::BudgetSnapshot snapshot;
    if (!decode_optional_integer_field(fields,
                                       "budget_snapshot.snapshot_at_ms",
                                       snapshot.snapshot_at_ms,
                                       detail) ||
        !decode_optional_string_field(fields,
                                      "budget_snapshot.overall_reject_reason",
                                      snapshot.overall_reject_reason,
                                      detail)) {
      return DurableCheckpointReadResult{
          .status = DurableCheckpointReadStatus::Corrupt,
          .record = std::nullopt,
          .detail = std::move(detail),
      };
    }

    std::uint32_t entry_count = 0;
    if (!decode_required_integer_field(fields, "budget_snapshot.entry_count", entry_count, detail)) {
      return DurableCheckpointReadResult{
          .status = DurableCheckpointReadStatus::Corrupt,
          .record = std::nullopt,
          .detail = std::move(detail),
      };
    }

    snapshot.entries.reserve(entry_count);
    for (std::uint32_t index = 0; index < entry_count; ++index) {
      const auto prefix = std::string("budget_entry.") + std::to_string(index) + ".";
      std::uint32_t raw_budget_type = 0;
      contracts::BudgetSnapshotEntry entry;
      if (!decode_required_integer_field(fields, prefix + "type", raw_budget_type, detail) ||
          !decode_required_integer_field(fields, prefix + "current", entry.current, detail) ||
          !decode_required_integer_field(fields, prefix + "max", entry.max, detail) ||
          !decode_required_integer_field(fields, prefix + "remaining", entry.remaining, detail) ||
          !decode_optional_string_field(fields,
                                        prefix + "reject_reason",
                                        entry.reject_reason,
                                        detail)) {
        return DurableCheckpointReadResult{
            .status = DurableCheckpointReadStatus::Corrupt,
            .record = std::nullopt,
            .detail = std::move(detail),
        };
      }

      const auto budget_type = parse_budget_type(raw_budget_type);
      if (!budget_type.has_value()) {
        return DurableCheckpointReadResult{
            .status = DurableCheckpointReadStatus::Corrupt,
            .record = std::nullopt,
            .detail = "durable checkpoint record has an invalid budget type",
        };
      }

      entry.budget_type = *budget_type;
      snapshot.entries.push_back(std::move(entry));
    }

    record.runtime_budget_snapshot = std::move(snapshot);
  }

  if (record.checkpoint.checkpoint_id != std::optional<std::string>(std::string(checkpoint_ref))) {
    return DurableCheckpointReadResult{
        .status = DurableCheckpointReadStatus::Corrupt,
        .record = std::nullopt,
        .detail = "durable checkpoint record checkpoint_id does not match file anchor",
    };
  }

  return DurableCheckpointReadResult{
      .status = DurableCheckpointReadStatus::Loaded,
      .record = std::move(record),
      .detail = "checkpoint loaded from durable store",
  };
}

[[nodiscard]] bool write_durable_checkpoint_record(
    const std::string_view state_root,
    const contracts::Checkpoint& checkpoint,
    const std::optional<contracts::BudgetSnapshot>& runtime_budget_snapshot,
    std::string& detail) {
  if (!checkpoint.checkpoint_id.has_value() || checkpoint.checkpoint_id->empty()) {
    detail = "checkpoint durable store requires checkpoint_id";
    return false;
  }

  const auto record_path = checkpoint_record_path(state_root, *checkpoint.checkpoint_id);
  const auto temporary_path = record_path.string() + ".tmp";
  std::error_code error_code;
  std::filesystem::create_directories(record_path.parent_path(), error_code);
  if (error_code) {
    detail = "checkpoint durable store could not create state directories";
    return false;
  }

  {
    std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      detail = "checkpoint durable store could not open the checkpoint record for writing";
      return false;
    }

    write_encoded_field(output, "checkpoint_id", *checkpoint.checkpoint_id);
    write_integer_field(output,
                        "state",
                        static_cast<int>(checkpoint.state.value_or(
                            contracts::CheckpointState::Unspecified)));
    write_encoded_field(output, "step_id", checkpoint.step_id.value_or(std::string{}));
    write_encoded_field(output,
                        "working_memory_snapshot",
                        checkpoint.working_memory_snapshot.value_or(std::string{}));
    write_encoded_field(output,
                        "pending_action",
                        checkpoint.pending_action.value_or(std::string{}));
    write_optional_encoded_field(output, "request_id", checkpoint.request_id);
    write_optional_encoded_field(output, "goal_id", checkpoint.goal_id);
    write_optional_encoded_field(output, "belief_state_ref", checkpoint.belief_state_ref);
    write_optional_integer_field(output, "retry_count", checkpoint.retry_count);
    write_optional_integer_field(output, "created_at", checkpoint.created_at);

    const auto& tags = checkpoint.tags.value_or(std::vector<std::string>{});
    write_integer_field(output, "tag_count", tags.size());
    for (std::size_t index = 0; index < tags.size(); ++index) {
      write_encoded_field(output, std::string("tag.") + std::to_string(index), tags[index]);
    }

    write_integer_field(output,
                        "budget_snapshot_present",
                        runtime_budget_snapshot.has_value() ? 1 : 0);
    if (runtime_budget_snapshot.has_value()) {
      write_optional_integer_field(output,
                                   "budget_snapshot.snapshot_at_ms",
                                   runtime_budget_snapshot->snapshot_at_ms);
      write_optional_encoded_field(output,
                                   "budget_snapshot.overall_reject_reason",
                                   runtime_budget_snapshot->overall_reject_reason);
      write_integer_field(output,
                          "budget_snapshot.entry_count",
                          runtime_budget_snapshot->entries.size());
      for (std::size_t index = 0; index < runtime_budget_snapshot->entries.size(); ++index) {
        const auto& entry = runtime_budget_snapshot->entries[index];
        const auto prefix = std::string("budget_entry.") + std::to_string(index) + ".";
        write_integer_field(output, prefix + "type", static_cast<int>(entry.budget_type));
        write_integer_field(output, prefix + "current", entry.current);
        write_integer_field(output, prefix + "max", entry.max);
        write_integer_field(output, prefix + "remaining", entry.remaining);
        write_optional_encoded_field(output, prefix + "reject_reason", entry.reject_reason);
      }
    }
  }

  std::filesystem::rename(temporary_path, record_path, error_code);
  if (error_code) {
    detail = "checkpoint durable store could not atomically publish the checkpoint record";
    (void)std::filesystem::remove(temporary_path);
    return false;
  }

  detail = "checkpoint persisted in durable store";
  return true;
}

[[nodiscard]] bool is_waiting_checkpoint_state(const contracts::CheckpointState state) {
  return state == contracts::CheckpointState::Paused ||
         state == contracts::CheckpointState::WaitingConfirm ||
         state == contracts::CheckpointState::WaitingTool;
}

[[nodiscard]] CheckpointConsistencyReport reject_checkpoint(
    const CheckpointConsistencyIssue issue,
    const std::string& detail) {
  return make_checkpoint_rejected_report(issue, detail);
}

[[nodiscard]] std::optional<contracts::CheckpointState> resolve_checkpoint_state(
    const StateTransitionOutcome& outcome) {
  const auto mapped_state = CheckpointStateMapper::to_checkpoint_state(outcome.resolved_state);
  if (!mapped_state.has_value() || *mapped_state == contracts::CheckpointState::Unspecified) {
    return std::nullopt;
  }

  if (outcome.checkpoint_hint.checkpoint_state.has_value() &&
      outcome.checkpoint_hint.checkpoint_state != mapped_state) {
    return std::nullopt;
  }

  return mapped_state;
}

[[nodiscard]] CheckpointConsistencyReport validate_version_tag(
    const contracts::Checkpoint& checkpoint,
    const std::string_view key,
    const std::string_view expected_value,
    const CheckpointConsistencyIssue unsupported_issue) {
  const auto value = find_checkpoint_tag_value(checkpoint, key);
  if (!value.has_value()) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::MissingVersionTag,
        std::string("checkpoint is missing ") + std::string(key));
  }

  if (value.value() != expected_value) {
    return reject_checkpoint(
        unsupported_issue,
        std::string("checkpoint ") + std::string(key) +
            " is not compatible with runtime");
  }

  return make_checkpoint_consistent_report();
}

}  // namespace

void CheckpointManager::set_durable_state_root(
    const std::optional<std::string>& state_root) {
  const std::lock_guard<std::mutex> lock(ckpt_mutex_);
  if (state_root.has_value() && !state_root->empty()) {
    durable_state_root_ = state_root;
    return;
  }

  durable_state_root_ = std::nullopt;
}

CheckpointBuildResult CheckpointManager::build_checkpoint(
    const CheckpointBuildRequest& request) const {
  if (!request.transition_outcome.accepted) {
    return CheckpointBuildResult{
        .checkpoint = std::nullopt,
        .report = reject_checkpoint(
            CheckpointConsistencyIssue::TransitionRejected,
            "checkpoint build requires an accepted transition outcome"),
    };
  }

  const auto checkpoint_state = resolve_checkpoint_state(request.transition_outcome);
  if (!checkpoint_state.has_value()) {
    return CheckpointBuildResult{
        .checkpoint = std::nullopt,
        .report = reject_checkpoint(
            CheckpointConsistencyIssue::InvalidCheckpointState,
            "resolved runtime state cannot be folded into a compatible checkpoint state"),
    };
  }

  const contracts::Checkpoint checkpoint{
      .checkpoint_id = request.checkpoint_id,
      .state = checkpoint_state,
      .step_id = request.step_id,
      .working_memory_snapshot = request.working_memory_snapshot,
      .pending_action = request.pending_action.has_value()
                            ? request.pending_action
                            : std::optional<std::string>(std::string()),
      .request_id = request.request_id,
      .goal_id = request.goal_id,
      .belief_state_ref = request.belief_state_ref,
      .retry_count = request.retry_count,
      .created_at = request.created_at_ms,
      .tags = with_runtime_checkpoint_version_tags(request.tags),
  };

  const auto report = validate(checkpoint);
  if (report.consistent && checkpoint.checkpoint_id.has_value()) {
    const std::lock_guard<std::mutex> lock(ckpt_mutex_);
    pending_runtime_budget_snapshots_[*checkpoint.checkpoint_id] =
        request.runtime_budget_snapshot;
  }

  return CheckpointBuildResult{
      .checkpoint = report.consistent ? std::optional<contracts::Checkpoint>(checkpoint)
                                      : std::nullopt,
      .report = report,
      .runtime_budget_snapshot = request.runtime_budget_snapshot,
  };
}

CheckpointPersistResult CheckpointManager::save(const contracts::Checkpoint& checkpoint) {
  std::optional<contracts::BudgetSnapshot> runtime_budget_snapshot;
  if (checkpoint.checkpoint_id.has_value()) {
    const std::lock_guard<std::mutex> lock(ckpt_mutex_);
    const auto iterator = pending_runtime_budget_snapshots_.find(*checkpoint.checkpoint_id);
    if (iterator != pending_runtime_budget_snapshots_.end()) {
      runtime_budget_snapshot = iterator->second;
      pending_runtime_budget_snapshots_.erase(iterator);
    }
  }

  return save(checkpoint, runtime_budget_snapshot);
}

CheckpointPersistResult CheckpointManager::save(
    const contracts::Checkpoint& checkpoint,
    const std::optional<contracts::BudgetSnapshot>& runtime_budget_snapshot) {
  const auto report = validate(checkpoint);
  if (!report.consistent) {
    return CheckpointPersistResult{
        .persisted = false,
        .checkpoint_ref = std::nullopt,
        .error_code = RuntimeErrorCode::RT_E_411_CHECKPOINT_SAVE_FAILED,
        .detail = report.detail,
    };
  }

  const std::lock_guard<std::mutex> lock(ckpt_mutex_);
  stored_checkpoints_[checkpoint.checkpoint_id.value()] = StoredCheckpointRecord{
      .checkpoint = checkpoint,
      .runtime_budget_snapshot = runtime_budget_snapshot,
  };
  pending_runtime_budget_snapshots_.erase(checkpoint.checkpoint_id.value());

  std::string persist_detail = "checkpoint persisted in memory store";
  if (durable_state_root_.has_value() &&
      !write_durable_checkpoint_record(
          *durable_state_root_, checkpoint, runtime_budget_snapshot, persist_detail)) {
    return CheckpointPersistResult{
        .persisted = false,
        .checkpoint_ref = std::nullopt,
        .error_code = RuntimeErrorCode::RT_E_411_CHECKPOINT_SAVE_FAILED,
        .detail = persist_detail,
    };
  }

  return CheckpointPersistResult{
      .persisted = true,
      .checkpoint_ref = checkpoint.checkpoint_id,
      .error_code = std::nullopt,
      .detail = std::move(persist_detail),
  };
}

CheckpointLoadResult CheckpointManager::load(const std::string& checkpoint_ref) const {
  std::optional<contracts::Checkpoint> checkpoint;
  std::optional<contracts::BudgetSnapshot> runtime_budget_snapshot;
  bool loaded_from_durable_store = false;
  std::string load_detail = "checkpoint loaded from memory store";
  {
    const std::lock_guard<std::mutex> lock(ckpt_mutex_);
    const auto iterator = stored_checkpoints_.find(checkpoint_ref);
    if (iterator == stored_checkpoints_.end()) {
      if (!durable_state_root_.has_value()) {
        return CheckpointLoadResult{
            .checkpoint = std::nullopt,
            .report = reject_checkpoint(
                CheckpointConsistencyIssue::MissingCheckpointId,
                "checkpoint not found in memory store"),
            .error_code = RuntimeErrorCode::RT_E_410_CHECKPOINT_CORRUPT,
            .detail = "checkpoint not found in memory store",
        };
      }

      const auto durable_read = read_durable_checkpoint_record(
          *durable_state_root_, checkpoint_ref);
      if (durable_read.status == DurableCheckpointReadStatus::NotFound) {
        return CheckpointLoadResult{
            .checkpoint = std::nullopt,
            .report = reject_checkpoint(
                CheckpointConsistencyIssue::MissingCheckpointId,
                durable_read.detail),
            .error_code = RuntimeErrorCode::RT_E_410_CHECKPOINT_CORRUPT,
            .detail = durable_read.detail,
        };
      }
      if (durable_read.status == DurableCheckpointReadStatus::Corrupt ||
          !durable_read.record.has_value()) {
        return CheckpointLoadResult{
            .checkpoint = std::nullopt,
            .report = reject_checkpoint(
                CheckpointConsistencyIssue::InvalidCheckpointState,
                durable_read.detail),
            .error_code = RuntimeErrorCode::RT_E_410_CHECKPOINT_CORRUPT,
            .detail = durable_read.detail,
        };
      }

      stored_checkpoints_[checkpoint_ref] = StoredCheckpointRecord{
          .checkpoint = durable_read.record->checkpoint,
          .runtime_budget_snapshot = durable_read.record->runtime_budget_snapshot,
      };
      checkpoint = durable_read.record->checkpoint;
      runtime_budget_snapshot = durable_read.record->runtime_budget_snapshot;
      loaded_from_durable_store = true;
      load_detail = durable_read.detail;
    } else {
      checkpoint = iterator->second.checkpoint;
      runtime_budget_snapshot = iterator->second.runtime_budget_snapshot;
    }
  }

  const auto report = validate(*checkpoint);
  return CheckpointLoadResult{
      .checkpoint = report.consistent ? checkpoint : std::nullopt,
      .report = report,
      .error_code = report.consistent ? std::nullopt : report.error_code,
      .detail = report.consistent
                    ? (loaded_from_durable_store ? load_detail
                                                 : std::string("checkpoint loaded from memory store"))
                    : report.detail,
      .runtime_budget_snapshot = report.consistent ? runtime_budget_snapshot : std::nullopt,
  };
}

CheckpointConsistencyReport CheckpointManager::validate(
    const contracts::Checkpoint& checkpoint) const {
  if (!checkpoint.checkpoint_id.has_value() || checkpoint.checkpoint_id->empty()) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::MissingCheckpointId,
        "checkpoint_id must be populated");
  }

  if (!checkpoint.step_id.has_value() || checkpoint.step_id->empty()) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::MissingStepId,
        "step_id must be populated");
  }

  if (!checkpoint.working_memory_snapshot.has_value() ||
      checkpoint.working_memory_snapshot->empty()) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::MissingWorkingMemorySnapshot,
        "working_memory_snapshot must be populated");
  }

  if (!checkpoint.state.has_value() ||
      checkpoint.state == contracts::CheckpointState::Unspecified) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::InvalidCheckpointState,
        "checkpoint state must be concrete");
  }

  if (!checkpoint.pending_action.has_value()) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::MissingPendingAction,
        "pending_action must be present (use empty string for none)");
  }

  if (checkpoint.request_id.has_value() && checkpoint.request_id->empty()) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::InvalidCheckpointState,
        "request_id must be non-empty when present");
  }

  if (checkpoint.goal_id.has_value() && checkpoint.goal_id->empty()) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::InvalidCheckpointState,
        "goal_id must be non-empty when present");
  }

  if (checkpoint.belief_state_ref.has_value() && checkpoint.belief_state_ref->empty()) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::InvalidCheckpointState,
        "belief_state_ref must be non-empty when present");
  }

  if (checkpoint.created_at.has_value() && *checkpoint.created_at <= 0) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::InvalidCheckpointState,
        "created_at must be a positive timestamp when present");
  }

  if (checkpoint.tags.has_value()) {
    if (checkpoint.tags->empty()) {
      return reject_checkpoint(
          CheckpointConsistencyIssue::InvalidCheckpointState,
          "tags must contain at least one item when present");
    }

    for (const auto& tag : *checkpoint.tags) {
      if (tag.empty()) {
        return reject_checkpoint(
            CheckpointConsistencyIssue::InvalidCheckpointState,
            "tags must not contain empty strings");
      }
    }
  }

  if (is_waiting_checkpoint_state(*checkpoint.state) && checkpoint.pending_action->empty()) {
    return reject_checkpoint(
        CheckpointConsistencyIssue::MissingPendingAction,
        "waiting checkpoints must persist non-empty pending_action");
  }

  auto report = validate_version_tag(
      checkpoint,
      kRuntimeCheckpointSchemaVersionTag,
      kRuntimeCheckpointSchemaVersion,
      CheckpointConsistencyIssue::UnsupportedSchemaVersion);
  if (report.rejected()) {
    return report;
  }

  report = validate_version_tag(
      checkpoint,
      kRuntimeCheckpointFsmStateEnumVersionTag,
      kRuntimeCheckpointFsmStateEnumVersion,
      CheckpointConsistencyIssue::UnsupportedFsmStateVersion);
  if (report.rejected()) {
    return report;
  }

  report = validate_version_tag(
      checkpoint,
      kRuntimeCheckpointBudgetSchemaVersionTag,
      kRuntimeCheckpointBudgetSchemaVersion,
      CheckpointConsistencyIssue::UnsupportedBudgetSchemaVersion);
  if (report.rejected()) {
    return report;
  }

  return make_checkpoint_consistent_report("checkpoint is structurally resumable");
}

ResumePlanDecision CheckpointManager::make_resume_plan(
    const contracts::Checkpoint& checkpoint) const {
  const auto report = validate(checkpoint);
  if (!report.consistent) {
    return make_rejected_resume_plan(
        ResumePlanViolation::CheckpointInvalid,
        report.detail,
        report.error_code);
  }

  if (!checkpoint.checkpoint_id.has_value() || checkpoint.checkpoint_id->empty()) {
    return make_rejected_resume_plan(
        ResumePlanViolation::MissingCheckpointReference,
        "checkpoint_id is required to build a resume plan");
  }

  const auto target_state = resume_target_state(*checkpoint.state);
  if (!target_state.has_value()) {
    return make_rejected_resume_plan(
        ResumePlanViolation::UnsupportedCheckpointState,
        "terminal or unspecified checkpoints cannot be resumed");
  }

  if (is_waiting_checkpoint_state(*checkpoint.state) &&
      (!checkpoint.pending_action.has_value() || checkpoint.pending_action->empty())) {
    return make_rejected_resume_plan(
        ResumePlanViolation::MissingPendingAction,
        "resume plan requires pending_action for waiting checkpoints");
  }

  return make_resume_plan_decision(
      ResumePlan{
          .checkpoint_ref = *checkpoint.checkpoint_id,
          .target_state = *target_state,
          .checkpoint_state = *checkpoint.state,
          .resume_token = std::string(),
          .resume_reason = "resume plan synthesized from checkpoint",
          .pending_action = checkpoint.pending_action,
          .policy_snapshot_ref = std::nullopt,
          .requires_operator_intervention =
              checkpoint.state == contracts::CheckpointState::WaitingConfirm,
      },
      "checkpoint is resumable");
}

ResumePlanDecision CheckpointManager::make_resume_plan(
    const contracts::Checkpoint& checkpoint,
    const ResumeSeed& resume_seed) const {
  if (!resume_seed.has_minimum_requirements()) {
    return make_rejected_resume_plan(
        ResumePlanViolation::CheckpointInvalid,
        "resume seed must include checkpoint_ref, resume_token and resume_reason");
  }

  const auto base_decision = make_resume_plan(checkpoint);
  if (base_decision.rejected() || !base_decision.plan.has_value()) {
    return base_decision;
  }

  if (base_decision.plan->checkpoint_ref != resume_seed.checkpoint_ref) {
    return make_rejected_resume_plan(
        ResumePlanViolation::CheckpointInvalid,
        "resume seed checkpoint_ref does not match checkpoint anchor");
  }

  if (checkpoint.request_id.has_value() && checkpoint.request_id != resume_seed.request_id) {
    return make_rejected_resume_plan(
        ResumePlanViolation::CheckpointInvalid,
        "resume seed request_id does not match checkpoint request_id");
  }

  auto plan = *base_decision.plan;
  plan.resume_token = resume_seed.resume_token;
  plan.resume_reason = resume_seed.resume_reason;
  plan.policy_snapshot_ref = resume_seed.policy_snapshot_ref;
  return make_resume_plan_decision(plan, "checkpoint is resumable with session resume seed");
}

void CheckpointManager::seed_for_test(
    const contracts::Checkpoint& checkpoint,
    std::optional<contracts::BudgetSnapshot> runtime_budget_snapshot) {
  const std::lock_guard<std::mutex> lock(ckpt_mutex_);
  if (!checkpoint.checkpoint_id.has_value() || checkpoint.checkpoint_id->empty()) {
    return;
  }

  stored_checkpoints_[*checkpoint.checkpoint_id] = StoredCheckpointRecord{
      .checkpoint = checkpoint,
      .runtime_budget_snapshot = std::move(runtime_budget_snapshot),
  };

  if (durable_state_root_.has_value()) {
    std::string detail;
    (void)write_durable_checkpoint_record(
        *durable_state_root_,
        checkpoint,
        stored_checkpoints_[*checkpoint.checkpoint_id].runtime_budget_snapshot,
        detail);
  }
}

}  // namespace dasall::runtime