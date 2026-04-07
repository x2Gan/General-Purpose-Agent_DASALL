#include "diagnostics/CommandRegistry.h"

#include <algorithm>
#include <charconv>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

namespace dasall::infra::diagnostics {
namespace {

constexpr std::string_view kRuntimeScope = "runtime";
constexpr std::string_view kCatalogSchemaVersion = "1";
constexpr std::string_view kHealthSnapshotName = "health.snapshot";
constexpr std::string_view kQueueStatsName = "queue.stats";
constexpr std::string_view kThreadDumpName = "thread.dump";

constexpr std::string_view kHealthSnapshotSchemaRef = "schema://diagnostics/health.snapshot/v1";
constexpr std::string_view kQueueStatsSchemaRef = "schema://diagnostics/queue.stats/v1";
constexpr std::string_view kThreadDumpSchemaRef = "schema://diagnostics/thread.dump/v1";

constexpr std::string_view kHealthSnapshotSummary =
    "type=array;minItems=0;maxItems=1;token=--summary;default=--summary";
constexpr std::string_view kQueueStatsSummary =
    "type=array;minItems=0;maxItems=1;token=--queue=<queue_id>;pattern=[a-z0-9._-]{1,32};default=--queue=main";
constexpr std::string_view kThreadDumpSummary =
    "type=array;minItems=0;maxItems=1;token=--limit=<1..32>;default=--limit=5";

[[nodiscard]] ValidationResult make_failure(std::string catalog_ref,
                                            std::string matched_command_ref,
                                            std::string schema_ref,
                                            std::string blocking_error,
                                            std::string field_path) {
  return ValidationResult::failure({std::move(blocking_error)},
                                   {std::move(field_path)},
                                   std::move(catalog_ref),
                                   std::move(matched_command_ref),
                                   std::move(schema_ref));
}

[[nodiscard]] ValidationResult make_required_field_failure(const DiagnosticsCommand& command,
                                                           std::string catalog_ref) {
  std::vector<std::string> blocking_errors;
  std::vector<std::string> field_paths;

  if (command.command_id.empty()) {
    blocking_errors.emplace_back("command_id is required");
    field_paths.emplace_back("command_id");
  }

  if (command.command_name.empty()) {
    blocking_errors.emplace_back("command_name is required");
    field_paths.emplace_back("command_name");
  }

  if (command.request_scope.empty()) {
    blocking_errors.emplace_back("request_scope is required");
    field_paths.emplace_back("request_scope");
  }

  if (command.timeout_ms == 0) {
    blocking_errors.emplace_back("timeout_ms must be greater than zero");
    field_paths.emplace_back("timeout_ms");
  }

  if (command.actor_ref.empty()) {
    blocking_errors.emplace_back("actor_ref is required");
    field_paths.emplace_back("actor_ref");
  }

  return ValidationResult::failure(std::move(blocking_errors),
                                   std::move(field_paths),
                                   std::move(catalog_ref));
}

[[nodiscard]] bool starts_with(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] bool is_valid_queue_id(std::string_view queue_id) {
  if (queue_id.empty() || queue_id.size() > 32) {
    return false;
  }

  return std::all_of(queue_id.begin(), queue_id.end(), [](char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' ||
           ch == '-';
  });
}

[[nodiscard]] std::optional<std::uint32_t> parse_thread_limit(std::string_view raw_value) {
  if (raw_value.empty()) {
    return std::nullopt;
  }

  if (raw_value.size() > 1 && raw_value.front() == '0') {
    return std::nullopt;
  }

  std::uint32_t parsed = 0;
  const auto [end, error] = std::from_chars(raw_value.data(),
                                            raw_value.data() + raw_value.size(),
                                            parsed);
  if (error != std::errc{} || end != raw_value.data() + raw_value.size()) {
    return std::nullopt;
  }

  if (parsed < 1 || parsed > 32) {
    return std::nullopt;
  }

  return parsed;
}

[[nodiscard]] ValidationResult validate_health_snapshot(std::string catalog_ref,
                                                        std::string matched_command_ref,
                                                        DiagnosticsCommand command) {
  if (command.args.empty()) {
    command.args = {std::string("--summary")};
    return ValidationResult::success(std::move(catalog_ref),
                                     std::move(matched_command_ref),
                                     std::string(kHealthSnapshotSchemaRef),
                                     std::move(command),
                                     {std::string("args normalized to --summary")});
  }

  if (command.args.size() > 1) {
    return make_failure(std::move(catalog_ref),
                        std::move(matched_command_ref),
                        std::string(kHealthSnapshotSchemaRef),
                        "health.snapshot accepts at most one token in v1",
                        "args[1]");
  }

  if (command.args.front() != "--summary") {
    return make_failure(std::move(catalog_ref),
                        std::move(matched_command_ref),
                        std::string(kHealthSnapshotSchemaRef),
                        "health.snapshot only accepts --summary in v1",
                        "args[0]");
  }

  return ValidationResult::success(std::move(catalog_ref),
                                   std::move(matched_command_ref),
                                   std::string(kHealthSnapshotSchemaRef),
                                   std::move(command));
}

[[nodiscard]] ValidationResult validate_queue_stats(std::string catalog_ref,
                                                    std::string matched_command_ref,
                                                    DiagnosticsCommand command) {
  if (command.args.empty()) {
    command.args = {std::string("--queue=main")};
    return ValidationResult::success(std::move(catalog_ref),
                                     std::move(matched_command_ref),
                                     std::string(kQueueStatsSchemaRef),
                                     std::move(command),
                                     {std::string("args normalized to --queue=main")});
  }

  if (command.args.size() > 1) {
    return make_failure(std::move(catalog_ref),
                        std::move(matched_command_ref),
                        std::string(kQueueStatsSchemaRef),
                        "queue.stats accepts at most one token in v1",
                        "args[1]");
  }

  const std::string_view token = command.args.front();
  constexpr std::string_view kPrefix = "--queue=";
  if (!starts_with(token, kPrefix)) {
    return make_failure(std::move(catalog_ref),
                        std::move(matched_command_ref),
                        std::string(kQueueStatsSchemaRef),
                        "queue.stats requires --queue=<queue_id> when args are present",
                        "args[0]");
  }

  const auto queue_id = token.substr(kPrefix.size());
  if (!is_valid_queue_id(queue_id)) {
    return make_failure(std::move(catalog_ref),
                        std::move(matched_command_ref),
                        std::string(kQueueStatsSchemaRef),
                        "queue.stats queue_id must match [a-z0-9._-]{1,32}",
                        "args[0]");
  }

  return ValidationResult::success(std::move(catalog_ref),
                                   std::move(matched_command_ref),
                                   std::string(kQueueStatsSchemaRef),
                                   std::move(command));
}

[[nodiscard]] ValidationResult validate_thread_dump(std::string catalog_ref,
                                                    std::string matched_command_ref,
                                                    DiagnosticsCommand command) {
  if (command.args.empty()) {
    command.args = {std::string("--limit=5")};
    return ValidationResult::success(std::move(catalog_ref),
                                     std::move(matched_command_ref),
                                     std::string(kThreadDumpSchemaRef),
                                     std::move(command),
                                     {std::string("args normalized to --limit=5")});
  }

  if (command.args.size() > 1) {
    return make_failure(std::move(catalog_ref),
                        std::move(matched_command_ref),
                        std::string(kThreadDumpSchemaRef),
                        "thread.dump accepts at most one token in v1",
                        "args[1]");
  }

  const std::string_view token = command.args.front();
  constexpr std::string_view kPrefix = "--limit=";
  if (!starts_with(token, kPrefix)) {
    return make_failure(std::move(catalog_ref),
                        std::move(matched_command_ref),
                        std::string(kThreadDumpSchemaRef),
                        "thread.dump requires --limit=<n> when args are present",
                        "args[0]");
  }

  const auto limit = parse_thread_limit(token.substr(kPrefix.size()));
  if (!limit.has_value()) {
    return make_failure(std::move(catalog_ref),
                        std::move(matched_command_ref),
                        std::string(kThreadDumpSchemaRef),
                        "thread.dump limit must be a decimal integer between 1 and 32",
                        "args[0]");
  }

  return ValidationResult::success(std::move(catalog_ref),
                                   std::move(matched_command_ref),
                                   std::string(kThreadDumpSchemaRef),
                                   std::move(command));
}

}  // namespace

CommandRegistry::CommandRegistry(CommandRegistryOptions options) : options_(std::move(options)) {
  if (options_.timeout_cap_ms == 0) {
    options_.timeout_cap_ms = 3000;
  }

  if (options_.allowed_commands.empty()) {
    options_.allowed_commands = {
        std::string(kHealthSnapshotName),
        std::string(kQueueStatsName),
        std::string(kThreadDumpName),
    };
  }

  for (const auto& command_name : options_.allowed_commands) {
    if (is_read_only_command_whitelisted(command_name)) {
      allowed_commands_.insert(command_name);
    }
  }
}

CommandCatalog CommandRegistry::list_commands() {
  CommandCatalog catalog{
      .catalog_id = options_.catalog_id,
      .profile_id = options_.profile_id,
      .schema_version = std::string(kCatalogSchemaVersion),
      .entries = {},
      .generated_at = options_.generated_at,
  };

  for (const auto command_name : kReadOnlyCommandWhitelist) {
    if (!is_command_enabled(command_name)) {
      continue;
    }

    catalog.entries.push_back(build_catalog_entry(command_name));
  }

  return catalog;
}

ValidationResult CommandRegistry::validate(const DiagnosticsCommand& command) {
  if (!command.has_required_fields()) {
    return make_required_field_failure(command, options_.catalog_id);
  }

  if (!command.has_whitelisted_command_name() || !is_command_enabled(command.command_name)) {
    return make_failure(options_.catalog_id,
                        {},
                        {},
                        "command_name is outside the frozen diagnostics v1 whitelist",
                        "command_name");
  }

  const auto matched_command_ref = command_ref_for(command.command_name);
  const auto schema_ref = schema_ref_for(command.command_name);

  if (command.request_scope != kRuntimeScope) {
    return make_failure(options_.catalog_id,
                        matched_command_ref,
                        schema_ref,
                        "diagnostics v1 request_scope must remain runtime",
                        "request_scope");
  }

  if (command.timeout_ms > options_.timeout_cap_ms) {
    return make_failure(options_.catalog_id,
                        matched_command_ref,
                        schema_ref,
                        "timeout_ms exceeds the diagnostics v1 cap",
                        "timeout_ms");
  }

  if (command.command_name == kHealthSnapshotName) {
    return validate_health_snapshot(options_.catalog_id, matched_command_ref, command);
  }

  if (command.command_name == kQueueStatsName) {
    return validate_queue_stats(options_.catalog_id, matched_command_ref, command);
  }

  return validate_thread_dump(options_.catalog_id, matched_command_ref, command);
}

bool CommandRegistry::is_command_enabled(std::string_view command_name) const {
  return allowed_commands_.contains(std::string(command_name));
}

std::string CommandRegistry::schema_ref_for(std::string_view command_name) const {
  if (command_name == kHealthSnapshotName) {
    return std::string(kHealthSnapshotSchemaRef);
  }

  if (command_name == kQueueStatsName) {
    return std::string(kQueueStatsSchemaRef);
  }

  return std::string(kThreadDumpSchemaRef);
}

std::string CommandRegistry::command_ref_for(std::string_view command_name) const {
  return std::string("command://diagnostics/") + std::string(command_name) + "/v1";
}

CommandCatalogEntry CommandRegistry::build_catalog_entry(std::string_view command_name) const {
  if (command_name == kHealthSnapshotName) {
    return CommandCatalogEntry{
        .command_name = std::string(command_name),
        .request_scope = std::string(kRuntimeScope),
        .arg_schema_ref = std::string(kHealthSnapshotSchemaRef),
        .arg_schema_summary = std::string(kHealthSnapshotSummary),
        .read_only = true,
    };
  }

  if (command_name == kQueueStatsName) {
    return CommandCatalogEntry{
        .command_name = std::string(command_name),
        .request_scope = std::string(kRuntimeScope),
        .arg_schema_ref = std::string(kQueueStatsSchemaRef),
        .arg_schema_summary = std::string(kQueueStatsSummary),
        .read_only = true,
    };
  }

  return CommandCatalogEntry{
      .command_name = std::string(command_name),
      .request_scope = std::string(kRuntimeScope),
      .arg_schema_ref = std::string(kThreadDumpSchemaRef),
      .arg_schema_summary = std::string(kThreadDumpSummary),
      .read_only = true,
  };
}

}  // namespace dasall::infra::diagnostics