#include "LogQueryService.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "logging/RedactionFilter.h"

namespace dasall::infra::logging {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kLogQueryServiceSourceRef = "LogQueryService";

[[nodiscard]] bool is_json_whitespace(char ch) {
  return ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t';
}

void skip_json_whitespace(std::string_view input, std::size_t& cursor) {
  while (cursor < input.size() && is_json_whitespace(input[cursor])) {
    ++cursor;
  }
}

void append_json_string(std::string& output, std::string_view value) {
  output.push_back('"');
  for (const unsigned char character : value) {
    switch (character) {
      case '"':
        output += "\\\"";
        break;
      case '\\':
        output += "\\\\";
        break;
      case '\b':
        output += "\\b";
        break;
      case '\f':
        output += "\\f";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        output.push_back(static_cast<char>(character));
        break;
    }
  }
  output.push_back('"');
}

[[nodiscard]] std::optional<std::string> parse_json_string(std::string_view input,
                                                           std::size_t& cursor) {
  skip_json_whitespace(input, cursor);
  if (cursor >= input.size() || input[cursor] != '"') {
    return std::nullopt;
  }

  ++cursor;
  std::string value;
  while (cursor < input.size()) {
    const char ch = input[cursor++];
    if (ch == '"') {
      return value;
    }

    if (ch != '\\') {
      value.push_back(ch);
      continue;
    }

    if (cursor >= input.size()) {
      return std::nullopt;
    }

    const char escaped = input[cursor++];
    switch (escaped) {
      case '"':
        value.push_back('"');
        break;
      case '\\':
        value.push_back('\\');
        break;
      case '/':
        value.push_back('/');
        break;
      case 'b':
        value.push_back('\b');
        break;
      case 'f':
        value.push_back('\f');
        break;
      case 'n':
        value.push_back('\n');
        break;
      case 'r':
        value.push_back('\r');
        break;
      case 't':
        value.push_back('\t');
        break;
      default:
        return std::nullopt;
    }
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> extract_json_string_field(
    std::string_view json,
    std::string_view key) {
  const std::string needle = std::string("\"") + std::string(key) + "\":";
  const auto position = json.find(needle);
  if (position == std::string_view::npos) {
    return std::nullopt;
  }

  std::size_t cursor = position + needle.size();
  return parse_json_string(json, cursor);
}

[[nodiscard]] std::optional<std::int64_t> extract_json_int64_field(std::string_view json,
                                                                   std::string_view key) {
  const std::string needle = std::string("\"") + std::string(key) + "\":";
  const auto position = json.find(needle);
  if (position == std::string_view::npos) {
    return std::nullopt;
  }

  std::size_t cursor = position + needle.size();
  skip_json_whitespace(json, cursor);
  const std::size_t start = cursor;
  while (cursor < json.size() && (json[cursor] == '-' || std::isdigit(static_cast<unsigned char>(json[cursor])) != 0)) {
    ++cursor;
  }

  if (start == cursor) {
    return std::nullopt;
  }

  std::int64_t value = 0;
  const auto result = std::from_chars(json.data() + start, json.data() + cursor, value);
  if (result.ec != std::errc()) {
    return std::nullopt;
  }

  return value;
}

[[nodiscard]] std::optional<bool> extract_json_bool_field(std::string_view json,
                                                          std::string_view key) {
  const std::string needle = std::string("\"") + std::string(key) + "\":";
  const auto position = json.find(needle);
  if (position == std::string_view::npos) {
    return std::nullopt;
  }

  std::size_t cursor = position + needle.size();
  skip_json_whitespace(json, cursor);
  if (json.substr(cursor, 4) == "true") {
    return true;
  }
  if (json.substr(cursor, 5) == "false") {
    return false;
  }

  return std::nullopt;
}

[[nodiscard]] std::string serialize_record_json(const LogEvent& event) {
  std::string output;
  output.reserve(256U);
  output += "{\"level\":";
  append_json_string(output, [level = event.level]() -> std::string_view {
    switch (level) {
      case LogLevel::Trace:
        return "trace";
      case LogLevel::Debug:
        return "debug";
      case LogLevel::Info:
        return "info";
      case LogLevel::Warn:
        return "warn";
      case LogLevel::Error:
        return "error";
      case LogLevel::Fatal:
        return "fatal";
      case LogLevel::Unspecified:
        break;
    }

    return "unspecified";
  }());
  output += ",\"module\":";
  append_json_string(output, event.module);
  output += ",\"message\":";
  append_json_string(output, event.message);
  output += ",\"ts_ms\":";
  output += event.ts.has_value() ? std::to_string(*event.ts) : std::string("0");
  output += ",\"attrs\":{";
  bool first_attr = true;
  for (const auto& [key, value] : event.attrs) {
    if (!first_attr) {
      output.push_back(',');
    }
    first_attr = false;
    append_json_string(output, key);
    output.push_back(':');
    append_json_string(output, value);
  }
  output += "}}";
  return output;
}

[[nodiscard]] std::string serialize_artifact_payload(
    const LogQueryRequest& request,
    const std::string& artifact_ref,
    const std::string& checksum,
    const std::vector<LogEvent>& matches,
    std::uint32_t returned_match_count,
    bool truncated,
    std::int64_t created_at) {
  std::string output;
  output.reserve(1024U + matches.size() * 256U);
  output += "{\"query_id\":";
  append_json_string(output, request.query_id);
  output += ",\"artifact_ref\":";
  append_json_string(output, artifact_ref);
  output += ",\"selector_kind\":";
  append_json_string(output, std::string(log_query_selector_name(request.selector_kind)));
  output += ",\"selector_value\":";
  append_json_string(output, request.selector_value);
  output += ",\"match_count\":";
  output += std::to_string(returned_match_count);
  output += ",\"truncated\":";
  output += truncated ? "true" : "false";
  output += ",\"checksum\":";
  append_json_string(output, checksum);
  output += ",\"created_at\":";
  output += std::to_string(created_at);
  output += ",\"records\":[";
  for (std::size_t index = 0; index < matches.size(); ++index) {
    if (index > 0U) {
      output.push_back(',');
    }
    output += serialize_record_json(matches[index]);
  }
  output += "]}";
  return output;
}

[[nodiscard]] std::string serialize_index_entry_json(const LogQueryArtifactIndexEntry& entry) {
  std::string output;
  output.reserve(256U);
  output += "{\"artifact_ref\":";
  append_json_string(output, entry.artifact_ref);
  output += ",\"artifact_file_name\":";
  append_json_string(output, entry.artifact_file_name);
  output += ",\"query_id\":";
  append_json_string(output, entry.query_id);
  output += ",\"selector_kind\":";
  append_json_string(output, std::string(log_query_selector_name(entry.selector_kind)));
  output += ",\"selector_value\":";
  append_json_string(output, entry.selector_value);
  output += ",\"checksum\":";
  append_json_string(output, entry.checksum);
  output += ",\"match_count\":";
  output += std::to_string(entry.match_count);
  output += ",\"truncated\":";
  output += entry.truncated ? "true" : "false";
  output += ",\"created_at\":";
  output += std::to_string(entry.created_at);
  output += "}";
  return output;
}

[[nodiscard]] std::optional<LogQuerySelectorKind> parse_selector_kind(
    std::string_view selector_name) {
  if (selector_name == "trace_id") {
    return LogQuerySelectorKind::TraceId;
  }
  if (selector_name == "session_id") {
    return LogQuerySelectorKind::SessionId;
  }
  if (selector_name == "request_id") {
    return LogQuerySelectorKind::RequestId;
  }
  return std::nullopt;
}

[[nodiscard]] std::vector<LogQueryArtifactIndexEntry> load_index_entries(
    const fs::path& index_path) {
  std::vector<LogQueryArtifactIndexEntry> entries;
  std::ifstream stream(index_path, std::ios::binary);
  if (!stream.is_open()) {
    return entries;
  }

  std::string line;
  while (std::getline(stream, line)) {
    LogQueryArtifactIndexEntry entry;
    entry.artifact_ref = extract_json_string_field(line, "artifact_ref").value_or("");
    entry.artifact_file_name = extract_json_string_field(line, "artifact_file_name").value_or("");
    entry.query_id = extract_json_string_field(line, "query_id").value_or("");
    entry.selector_value = extract_json_string_field(line, "selector_value").value_or("");
    entry.checksum = extract_json_string_field(line, "checksum").value_or("");
    entry.match_count = static_cast<std::uint32_t>(extract_json_int64_field(line, "match_count").value_or(0));
    entry.truncated = extract_json_bool_field(line, "truncated").value_or(false);
    entry.created_at = extract_json_int64_field(line, "created_at").value_or(0);
    const auto selector_kind = parse_selector_kind(
        extract_json_string_field(line, "selector_kind").value_or(""));
    if (selector_kind.has_value()) {
      entry.selector_kind = *selector_kind;
    }

    if (entry.has_required_fields()) {
      entries.push_back(std::move(entry));
    }
  }

  return entries;
}

[[nodiscard]] bool ensure_directory_exists(const fs::path& path) {
  std::error_code error;
  fs::create_directories(path, error);
  return !error;
}

[[nodiscard]] bool write_text_file(const fs::path& path, std::string_view payload) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream.is_open()) {
    return false;
  }
  stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
  return stream.good();
}

}  // namespace

LogQueryResult LogQueryResult::success(std::string artifact_ref,
                                       std::uint32_t match_count,
                                       bool truncated,
                                       std::string checksum,
                                       std::int64_t created_at) {
  return LogQueryResult{
      .ok = true,
      .artifact_ref = std::move(artifact_ref),
      .match_count = match_count,
      .truncated = truncated,
      .checksum = std::move(checksum),
      .created_at = created_at,
      .result_code = contracts::ResultCode::RuntimeRetryExhausted,
      .error_info = std::nullopt,
  };
}

LogQueryResult LogQueryResult::failure(contracts::ResultCode result_code,
                                       std::string message,
                                       std::string stage,
                                       std::string source_ref) {
  return LogQueryResult{
      .ok = false,
      .artifact_ref = {},
      .match_count = 0,
      .truncated = false,
      .checksum = {},
      .created_at = 0,
      .result_code = result_code,
      .error_info = contracts::ErrorInfo{
          .failure_type = contracts::classify_result_code(result_code),
          .retryable = false,
          .safe_to_replan = false,
          .details = contracts::ErrorDetails{
              .code = static_cast<int>(result_code),
              .message = std::move(message),
              .stage = std::move(stage),
          },
          .source_ref = contracts::ErrorSourceRefMinimal{
              .ref_type = "infra.logging",
              .ref_id = std::move(source_ref),
          },
      },
  };
}

LogQueryService::LogQueryService(std::shared_ptr<ILogQueryRecordReader> record_reader,
                                 LogQueryServiceOptions options,
                                 ClockNowMs clock_now_ms)
    : record_reader_(std::move(record_reader)),
      options_(std::move(options)),
      clock_now_ms_(std::move(clock_now_ms)),
      retention_policy_(options_.retention_policy) {
  if (!clock_now_ms_) {
    clock_now_ms_ = &LogQueryService::default_now_ms;
  }
}

LogRetentionPolicy::LogRetentionPolicy(LogRetentionPolicyOptions options)
    : options_(std::move(options)) {
  if (options_.retention_days == 0U) {
    options_.retention_days = 1U;
  }
  if (options_.max_artifact_count == 0U) {
    options_.max_artifact_count = 1U;
  }
}

std::vector<LogQueryArtifactIndexEntry> LogRetentionPolicy::apply(
    const std::vector<LogQueryArtifactIndexEntry>& entries,
    const std::filesystem::path& artifact_root,
    std::int64_t now_ms) const {
  std::vector<LogQueryArtifactIndexEntry> sorted_entries = entries;
  std::sort(sorted_entries.begin(),
            sorted_entries.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.created_at > rhs.created_at; });

  std::vector<LogQueryArtifactIndexEntry> retained_entries;
  retained_entries.reserve(sorted_entries.size());
  for (const auto& entry : sorted_entries) {
    if (!entry.has_required_fields() || should_expire(entry, now_ms) ||
        retained_entries.size() >= options_.max_artifact_count) {
      remove_artifact_if_present(artifact_root, entry);
      continue;
    }

    retained_entries.push_back(entry);
  }

  std::sort(retained_entries.begin(),
            retained_entries.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.created_at < rhs.created_at; });
  return retained_entries;
}

bool LogRetentionPolicy::should_expire(const LogQueryArtifactIndexEntry& entry,
                                       std::int64_t now_ms) const {
  const std::int64_t retention_window_ms =
      static_cast<std::int64_t>(options_.retention_days) * 24LL * 60LL * 60LL * 1000LL;
  return entry.created_at <= 0 ||
         (now_ms > 0 && entry.created_at < now_ms - retention_window_ms);
}

void LogRetentionPolicy::remove_artifact_if_present(
    const std::filesystem::path& artifact_root,
    const LogQueryArtifactIndexEntry& entry) const {
  if (entry.artifact_file_name.empty()) {
    return;
  }

  std::error_code error;
  fs::remove(artifact_root / entry.artifact_file_name, error);
}

LogQueryResult LogQueryService::query(const LogQueryRequest& request,
                                      const LogQueryAccessContext& access_context) const {
  if (!options_.has_consistent_values()) {
    return LogQueryResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "log query service requires a non-empty local artifact namespace",
        "logging.query.config",
        std::string(kLogQueryServiceSourceRef));
  }

  if (!request.has_required_fields()) {
    return LogQueryResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "log query request must keep query_id, precise selector, ordered time window, and positive max_records",
        "logging.query.request",
        std::string(kLogQueryServiceSourceRef));
  }

  if (!access_context.has_required_fields()) {
    return LogQueryResult::failure(
        contracts::ResultCode::ValidationFieldMissing,
        "log query access context must keep actor_ref, consumer_module, and a complete policy decision reference",
        "logging.query.access",
        std::string(kLogQueryServiceSourceRef));
  }

  if (!options_.enable_diag_pull) {
    return LogQueryResult::failure(
        contracts::ResultCode::PolicyDenied,
        "infra.logging.export.enable_diag_pull must remain enabled before log query export is allowed",
        "logging.query.config_gate",
        std::string(kLogQueryServiceSourceRef));
  }

  if (!access_context.has_allow_proof()) {
    return LogQueryResult::failure(
        contracts::ResultCode::PolicyDenied,
        "log query requires an upstream allow decision proof and does not re-authorize denied or confirmation-required requests",
        "logging.query.policy",
        std::string(kLogQueryServiceSourceRef));
  }

  if (!record_reader_) {
    return LogQueryResult::failure(
        contracts::ResultCode::ToolExecutionFailed,
        "log query requires a local record reader before it can materialize a diagnostics artifact",
        "logging.query.reader",
        std::string(kLogQueryServiceSourceRef));
  }

  const auto records = record_reader_->read_window(request.start_ts_ms, request.end_ts_ms);
  std::vector<LogEvent> matches;
  matches.reserve(records.size());
  std::copy_if(records.begin(),
               records.end(),
               std::back_inserter(matches),
               [&](const LogEvent& record) { return matches_selector(record, request); });

  const bool truncated = matches.size() > request.max_records;
  const auto returned_match_count = static_cast<std::uint32_t>(
      truncated ? request.max_records : matches.size());

  const auto created_at = clock_now_ms_();
  if (created_at <= 0) {
    return LogQueryResult::failure(
        contracts::ResultCode::ToolExecutionFailed,
        "log query artifact generation requires a positive created_at timestamp",
        "logging.query.artifact",
        std::string(kLogQueryServiceSourceRef));
  }

  return materialize_artifact(
      request,
      std::vector<LogEvent>(matches.begin(), matches.begin() + returned_match_count),
      returned_match_count,
      truncated,
      created_at);
}

std::string_view LogQueryService::selector_attr_key(
    LogQuerySelectorKind selector_kind) {
  switch (selector_kind) {
    case LogQuerySelectorKind::TraceId:
      return "trace_id";
    case LogQuerySelectorKind::SessionId:
      return "session_id";
    case LogQuerySelectorKind::RequestId:
      return "request_id";
    case LogQuerySelectorKind::Unspecified:
      break;
  }

  return "unspecified";
}

bool LogQueryService::matches_selector(const LogEvent& event,
                                       const LogQueryRequest& request) {
  if (!event.has_timestamp()) {
    return false;
  }

  if (*event.ts < request.start_ts_ms || *event.ts > request.end_ts_ms) {
    return false;
  }

  const auto iterator = event.attrs.find(std::string(selector_attr_key(request.selector_kind)));
  return iterator != event.attrs.end() && iterator->second == request.selector_value;
}

std::string LogQueryService::make_artifact_ref(const LogQueryRequest& request) const {
  return options_.artifact_namespace + "/" + request.query_id;
}

std::filesystem::path LogQueryService::resolve_artifact_root_path() const {
  if (options_.artifact_root_dir.is_absolute()) {
    return options_.artifact_root_dir;
  }

  std::error_code error;
  const auto cwd = fs::current_path(error);
  if (error) {
    return options_.artifact_root_dir;
  }

  return cwd / options_.artifact_root_dir;
}

std::filesystem::path LogQueryService::resolve_index_path() const {
  return resolve_artifact_root_path() / options_.index_file_name;
}

std::string LogQueryService::make_artifact_file_name(const LogQueryRequest& request,
                                                     std::int64_t created_at) {
  return request.query_id + "-" + std::to_string(created_at) + ".json";
}

std::string LogQueryService::make_checksum(const LogQueryRequest& request,
                                          std::uint32_t match_count,
                                          bool truncated) {
  return std::string("log-query:") + request.query_id + ":" +
         std::string(log_query_selector_name(request.selector_kind)) + ":" +
         request.selector_value + ":" + std::to_string(match_count) + ":" +
         (truncated ? "truncated" : "complete");
}

LogQueryResult LogQueryService::materialize_artifact(
    const LogQueryRequest& request,
    const std::vector<LogEvent>& matches,
    std::uint32_t returned_match_count,
    bool truncated,
    std::int64_t created_at) const {
  const auto artifact_root = resolve_artifact_root_path();
  if (!ensure_directory_exists(artifact_root)) {
    return LogQueryResult::failure(
        contracts::ResultCode::ToolExecutionFailed,
        "log query artifact root must remain writable before local diagnostics artifact materialization",
        "logging.query.artifact_root",
        std::string(kLogQueryServiceSourceRef));
  }

  const auto artifact_ref = make_artifact_ref(request);
  const auto checksum = make_checksum(request, returned_match_count, truncated);
  const auto artifact_file_name = make_artifact_file_name(request, created_at);
  const auto artifact_path = artifact_root / artifact_file_name;

  RedactionFilter redaction_filter;
  std::vector<LogEvent> redacted_matches;
  redacted_matches.reserve(matches.size());
  for (const auto& match : matches) {
    redacted_matches.push_back(redaction_filter.apply(match));
  }

  const auto artifact_payload = serialize_artifact_payload(
      request,
      artifact_ref,
      checksum,
      redacted_matches,
      returned_match_count,
      truncated,
      created_at);
  if (!write_text_file(artifact_path, artifact_payload)) {
    return LogQueryResult::failure(
        contracts::ResultCode::ToolExecutionFailed,
        "log query artifact file must remain writable before local diagnostics artifact materialization",
        "logging.query.artifact_write",
        std::string(kLogQueryServiceSourceRef));
  }

  auto index_entries = load_index_entries(resolve_index_path());
  index_entries.push_back(LogQueryArtifactIndexEntry{
      .artifact_ref = artifact_ref,
      .artifact_file_name = artifact_file_name,
      .query_id = request.query_id,
      .selector_kind = request.selector_kind,
      .selector_value = request.selector_value,
      .checksum = checksum,
      .match_count = returned_match_count,
      .truncated = truncated,
      .created_at = created_at,
  });

  index_entries = retention_policy_.apply(index_entries, artifact_root, created_at);
  std::ostringstream stream;
  for (const auto& entry : index_entries) {
    stream << serialize_index_entry_json(entry) << '\n';
  }

  if (!write_text_file(resolve_index_path(), stream.str())) {
    std::error_code error;
    fs::remove(artifact_path, error);
    return LogQueryResult::failure(
        contracts::ResultCode::ToolExecutionFailed,
        "log query index must remain writable before local diagnostics artifact publication",
        "logging.query.index_write",
        std::string(kLogQueryServiceSourceRef));
  }

  return LogQueryResult::success(
      artifact_ref, returned_match_count, truncated, checksum, created_at);
}

std::int64_t LogQueryService::default_now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace dasall::infra::logging