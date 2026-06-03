#include "MemoryInstalledProofRunner.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <sqlite3.h>

#include "BuildProfileResolver.h"
#include "DaemonEntryConfigLoader.h"
#include "IMemoryManager.h"
#include "ProfileCatalog.h"
#include "config/InstallLayout.h"
#include "config/MemoryConfigProjector.h"

namespace dasall::apps::daemon {
namespace {

namespace fs = std::filesystem;

constexpr int kSqliteBusyTimeoutMs = 5000;
constexpr char kExpectedMarker[] = "mem-fix-006-local-proof";

[[nodiscard]] std::int64_t current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

[[nodiscard]] std::string select_profile_id(
    const MemoryInstalledProofOptions& options) {
  return options.requested_profile_id.empty() ? kDefaultDaemonEntryProfileId
                                              : options.requested_profile_id;
}

[[nodiscard]] fs::path select_state_root(
    const infra::config::InstallLayout& install_layout,
    const MemoryInstalledProofOptions& options) {
  return options.state_root_override.has_value() ? *options.state_root_override
                                                 : install_layout.state_root;
}

struct ScopedSqliteConnection {
  sqlite3* handle = nullptr;

  ~ScopedSqliteConnection() {
    if (handle != nullptr) {
      sqlite3_close(handle);
    }
  }
};

[[nodiscard]] bool open_sqlite_connection(const fs::path& database_path,
                                          ScopedSqliteConnection& connection,
                                          std::string& error) {
  if (sqlite3_open(database_path.string().c_str(), &connection.handle) !=
      SQLITE_OK) {
    error = std::string("failed to open sqlite database: ") +
            sqlite3_errmsg(connection.handle);
    return false;
  }
  sqlite3_busy_timeout(connection.handle, kSqliteBusyTimeoutMs);
  return true;
}

[[nodiscard]] std::optional<int> query_int(sqlite3* connection,
                                           const std::string& sql,
                                           std::string& error) {
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(connection, sql.c_str(), -1, &statement, nullptr) !=
      SQLITE_OK) {
    error = std::string("failed to prepare integer query: ") + sql;
    return std::nullopt;
  }

  const int step_status = sqlite3_step(statement);
  if (step_status != SQLITE_ROW) {
    sqlite3_finalize(statement);
    error = std::string("integer query returned no row: ") + sql;
    return std::nullopt;
  }

  const int value = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  return value;
}

[[nodiscard]] std::optional<int> query_int_with_text_param(
    sqlite3* connection,
    const std::string& sql,
    const std::string& param,
    std::string& error) {
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(connection, sql.c_str(), -1, &statement, nullptr) !=
      SQLITE_OK) {
    error = std::string("failed to prepare integer query with param: ") + sql;
    return std::nullopt;
  }

  sqlite3_bind_text(statement, 1, param.c_str(), -1, SQLITE_TRANSIENT);
  const int step_status = sqlite3_step(statement);
  if (step_status != SQLITE_ROW) {
    sqlite3_finalize(statement);
    error = std::string("integer query with param returned no row: ") + sql;
    return std::nullopt;
  }

  const int value = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  return value;
}

[[nodiscard]] std::optional<std::string> query_text(sqlite3* connection,
                                                    const std::string& sql,
                                                    std::string& error) {
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(connection, sql.c_str(), -1, &statement, nullptr) !=
      SQLITE_OK) {
    error = std::string("failed to prepare text query: ") + sql;
    return std::nullopt;
  }

  const int step_status = sqlite3_step(statement);
  if (step_status != SQLITE_ROW) {
    sqlite3_finalize(statement);
    error = std::string("text query returned no row: ") + sql;
    return std::nullopt;
  }

  const unsigned char* value = sqlite3_column_text(statement, 0);
  const std::string text = value == nullptr
                               ? std::string()
                               : std::string(reinterpret_cast<const char*>(value));
  sqlite3_finalize(statement);
  return text;
}

[[nodiscard]] std::optional<std::string> query_text_with_text_param(
    sqlite3* connection,
    const std::string& sql,
    const std::string& param,
    std::string& error) {
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(connection, sql.c_str(), -1, &statement, nullptr) !=
      SQLITE_OK) {
    error = std::string("failed to prepare text query with param: ") + sql;
    return std::nullopt;
  }

  sqlite3_bind_text(statement, 1, param.c_str(), -1, SQLITE_TRANSIENT);
  const int step_status = sqlite3_step(statement);
  if (step_status != SQLITE_ROW) {
    sqlite3_finalize(statement);
    error = std::string("text query with param returned no row: ") + sql;
    return std::nullopt;
  }

  const unsigned char* value = sqlite3_column_text(statement, 0);
  const std::string text = value == nullptr
                               ? std::string()
                               : std::string(reinterpret_cast<const char*>(value));
  sqlite3_finalize(statement);
  return text;
}

[[nodiscard]] std::optional<profiles::BuildProfileManifest> resolve_build_manifest(
    const fs::path& profiles_root,
    const profiles::RuntimePolicySnapshot& policy_snapshot,
    std::string& error) {
  const profiles::ProfileCatalog catalog(profiles_root);
  const profiles::BuildProfileResolver resolver(catalog);
  const auto result = resolver.resolve_build_manifest(
      profiles::BuildProfileResolveRequest{
          .profile_id = policy_snapshot.effective_profile_id(),
          .expected_target_platform = std::nullopt,
      });
  if (!result.ok()) {
    error = std::string("build manifest resolve failed for profile ") +
            policy_snapshot.effective_profile_id();
    return std::nullopt;
  }
  if (!result.manifest.has_value()) {
    error = std::string("build manifest unavailable for profile ") +
            policy_snapshot.effective_profile_id();
    return std::nullopt;
  }

  return result.manifest;
}

[[nodiscard]] std::optional<memory::MemoryConfig> compose_memory_config(
    const profiles::RuntimePolicySnapshot& policy_snapshot,
    const infra::config::InstallLayout& install_layout,
    const fs::path& state_root,
    std::string& error) {
  const auto build_manifest =
      resolve_build_manifest(install_layout.profiles_root, policy_snapshot, error);
  if (!build_manifest.has_value()) {
    return std::nullopt;
  }

  auto memory_config = memory::config::project_memory_config(
      policy_snapshot,
      *build_manifest);
  if (!memory_config.has_value()) {
    error = std::string("memory config projection failed for profile ") +
            policy_snapshot.effective_profile_id();
    return std::nullopt;
  }

  memory_config->storage.db_path = (state_root / "memory" / "memory.db").string();
  memory_config->storage.migrations_dir =
      (install_layout.readonly_assets_root / "sql" / "memory").string();

#if defined(__APPLE__)
  constexpr const char* kSqliteExtensionSuffix = ".dylib";
#else
  constexpr const char* kSqliteExtensionSuffix = ".so";
#endif

  memory_config->vector.sqlite_vss_vector0_path =
      (install_layout.runtime_library_root / "sqlite-vss" /
       (std::string("vector0") + kSqliteExtensionSuffix))
          .string();
  memory_config->vector.sqlite_vss_vss0_path =
      (install_layout.runtime_library_root / "sqlite-vss" /
       (std::string("vss0") + kSqliteExtensionSuffix))
          .string();
  if (memory_config->vector.enabled &&
      (!fs::exists(memory_config->vector.sqlite_vss_vector0_path) ||
       !fs::exists(memory_config->vector.sqlite_vss_vss0_path))) {
    memory_config->vector.enabled = false;
    memory_config->vector.backend_type = memory::VectorBackend::None;
    memory_config->vector.search_top_k = 0;
  }

  return memory_config;
}

[[nodiscard]] memory::MemoryWritebackRequest make_writeback_request(
    const std::string& request_id,
    const std::string& session_id,
    const std::string& trace_id,
    const std::string& turn_id,
    const std::string& user_input,
    const std::string& agent_response,
    const std::string& summary_text,
    const std::string& fact_text) {
  memory::MemoryWritebackRequest request;
  request.request_id = request_id;
  request.session_id = session_id;
  request.trace_id = trace_id;
  request.turn.turn_id = turn_id;
  request.turn.session_id = session_id;
  request.turn.user_input = user_input;
  request.turn.agent_response = agent_response;
  request.turn.created_at = current_time_millis();
  request.summary_candidate = contracts::SummaryMemory{};
  request.summary_candidate->summary_text = summary_text;
  request.summary_candidate->confirmed_facts = std::vector<std::string>{fact_text};

  memory::FactCandidate fact_candidate;
  fact_candidate.fact.fact_text = fact_text;
  fact_candidate.fact.fact_type = "status";
  fact_candidate.fact.confidence_score = 99U;
  fact_candidate.fact.source_turn_ids = std::vector<std::string>{turn_id};
  fact_candidate.extraction_source = "memory-installed-proof";
  request.fact_candidates.push_back(std::move(fact_candidate));
  return request;
}

[[nodiscard]] memory::MemoryContextRequest make_context_request(
    const std::string& session_id,
    const std::string& expected_marker) {
  memory::MemoryContextRequest request;
  request.request_id = "req-memory-installed-proof-prepare-context";
  request.session_id = session_id;
  request.trace_id = "trace-memory-installed-proof-prepare-context";
  request.stage = "reasoning";
  request.user_turn =
      "在同一个会话里，我刚才让你在答案末尾追加的短标记是什么？";
  request.goal_summary = "验证 memory installed proof 的 prepare_context 召回 marker";
  request.constraints_summary = "必须保留 marker 与 latest summary";
  request.latest_observation_digest_summary =
      std::string("latest summary should keep marker ") + expected_marker;
  request.visible_tools = {"dasall-cli", "memory-installed-proof"};
  request.token_budget_hint = 512;
  request.latency_budget_ms = 100;
  request.external_evidence = {
      std::string("memory-installed-proof marker=") + expected_marker,
  };
  return request;
}

[[nodiscard]] bool contains_marker(const std::string& value,
                                   const std::string& marker) {
  return value.find(marker) != std::string::npos;
}

[[nodiscard]] bool context_contains_marker(
    const memory::ContextAssemblyResult& context_result,
    const std::string& marker) {
  if (context_result.context_packet.summary_memory.has_value() &&
      contains_marker(*context_result.context_packet.summary_memory, marker)) {
    return true;
  }
  if (context_result.context_packet.belief_state_summary.has_value() &&
      contains_marker(*context_result.context_packet.belief_state_summary, marker)) {
    return true;
  }
  if (context_result.context_packet.latest_observation_digest_summary.has_value() &&
      contains_marker(*context_result.context_packet.latest_observation_digest_summary,
                      marker)) {
    return true;
  }
  if (context_result.context_packet.retrieval_evidence.has_value()) {
    return std::any_of(
        context_result.context_packet.retrieval_evidence->begin(),
        context_result.context_packet.retrieval_evidence->end(),
        [&marker](const std::string& value) { return contains_marker(value, marker); });
  }
  return false;
}

[[nodiscard]] std::string truncate_prefix(const std::string& value,
                                          const std::size_t limit) {
  return value.size() <= limit ? value : value.substr(0, limit);
}

}  // namespace

MemoryInstalledProofResult collect_memory_installed_proof(
    const MemoryInstalledProofOptions& options) {
  MemoryInstalledProofResult result;
  result.expected_marker = kExpectedMarker;

  const auto install_layout = infra::config::resolve_install_layout();
  const fs::path state_root = select_state_root(install_layout, options);
  std::error_code fs_error;
  fs::create_directories(state_root / "memory", fs_error);
  if (fs_error) {
    result.error = std::string("failed to create memory state root: ") +
                   fs_error.message();
    return result;
  }

  const DaemonEntryConfigLoader entry_loader;
  const auto entry_result = entry_loader.load(DaemonEntryConfigLoadRequest{
      .profiles_root = install_layout.profiles_root,
      .requested_profile_id = select_profile_id(options),
      .deployment_config_path = options.deployment_config_path,
      .socket_path_override = std::nullopt,
  });
  if (!entry_result.ok() || !entry_result.entry_config.has_value()) {
    result.error = std::string("daemon entry config load failed: ") +
                   entry_result.message;
    return result;
  }

  result.effective_profile_id = entry_result.entry_config->effective_profile_id;

  auto memory_config = compose_memory_config(
      *entry_result.entry_config->runtime_policy_snapshot,
      install_layout,
      state_root,
      result.error);
  if (!memory_config.has_value()) {
    return result;
  }

  result.database_path = memory_config->storage.db_path;

  auto manager = memory::create_memory_manager(*memory_config);
  if (manager == nullptr) {
    result.error = "memory manager factory returned null";
    return result;
  }

  const auto init_code = manager->init(*memory_config);
  if (static_cast<int>(init_code) != 0) {
    result.error = std::string("memory manager init failed with code ") +
                   std::to_string(static_cast<int>(init_code));
    return result;
  }

  const auto shutdown_guard = std::shared_ptr<void>(nullptr, [&](void*) {
    manager->shutdown();
  });
  (void)shutdown_guard;

  const auto seed_epoch = current_time_millis();
  const std::string proof_prefix =
      "memory-installed-proof-" + std::to_string(seed_epoch);
  result.session_id = proof_prefix + "-session";
  result.first_turn_id = proof_prefix + "-turn-001";
  result.second_turn_id = proof_prefix + "-turn-002";

  const auto first_result = manager->write_back(make_writeback_request(
      "req-memory-installed-proof-001",
      result.session_id,
      "trace-memory-installed-proof-001",
      result.first_turn_id,
      "请用LLM回答：1+1等于几？并在同一行末尾追加 mem-fix-006-local-proof。",
      "2 mem-fix-006-local-proof",
      "memory installed proof first summary mem-fix-006-local-proof",
      result.expected_marker));
  if (first_result.result_code.has_value()) {
    result.error = "memory installed proof first write_back failed";
    return result;
  }

  {
    ScopedSqliteConnection connection;
    if (!open_sqlite_connection(result.database_path, connection, result.error)) {
      return result;
    }
    const auto count = query_int_with_text_param(
        connection.handle,
        "SELECT COUNT(*) FROM summaries WHERE session_id = ?1;",
        result.session_id,
        result.error);
    if (!count.has_value()) {
      return result;
    }
    result.session_summary_count_after_first = *count;
  }

  const auto context_result = manager->prepare_context(
      make_context_request(result.session_id, result.expected_marker));
  if (context_result.result_code.has_value()) {
    result.error = "memory installed proof prepare_context failed";
    return result;
  }
  result.prepare_context_marker_visible =
      context_contains_marker(context_result, result.expected_marker);
  if (!result.prepare_context_marker_visible) {
    result.error = "memory installed proof prepare_context did not surface the expected marker";
    return result;
  }

  const auto second_result = manager->write_back(make_writeback_request(
      "req-memory-installed-proof-002",
      result.session_id,
      "trace-memory-installed-proof-002",
      result.second_turn_id,
      "在同一个会话里，我刚才让你在答案末尾追加的短标记是什么？",
      result.expected_marker,
      std::string("memory installed proof second summary ") + result.expected_marker,
      result.expected_marker));
  if (second_result.result_code.has_value()) {
    result.error = "memory installed proof second write_back failed";
    return result;
  }

  ScopedSqliteConnection connection;
  if (!open_sqlite_connection(result.database_path, connection, result.error)) {
    return result;
  }

  const auto journal_mode =
      query_text(connection.handle, "PRAGMA journal_mode;", result.error);
  if (!journal_mode.has_value()) {
    return result;
  }
  result.journal_mode = *journal_mode;

  const auto core_table_count = query_int(
      connection.handle,
      "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN ('sessions','turns','summaries','facts','experiences');",
      result.error);
  if (!core_table_count.has_value()) {
    return result;
  }
  result.core_table_count = *core_table_count;

  const auto vector_table_count = query_int(
      connection.handle,
      "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name = 'memory_vector_documents';",
      result.error);
  if (!vector_table_count.has_value()) {
    return result;
  }
  result.vector_table_count = *vector_table_count;

  const auto session_turn_count_after_second = query_int_with_text_param(
      connection.handle,
      "SELECT COUNT(*) FROM turns WHERE session_id = ?1;",
      result.session_id,
      result.error);
  if (!session_turn_count_after_second.has_value()) {
    return result;
  }
  result.session_turn_count_after_second = *session_turn_count_after_second;

  const auto session_summary_count_after_second = query_int_with_text_param(
      connection.handle,
      "SELECT COUNT(*) FROM summaries WHERE session_id = ?1;",
      result.session_id,
      result.error);
  if (!session_summary_count_after_second.has_value()) {
    return result;
  }
  result.session_summary_count_after_second = *session_summary_count_after_second;

  const auto latest_summary_sources = query_text_with_text_param(
      connection.handle,
      "SELECT source_turn_ids_json FROM summaries WHERE session_id = ?1 ORDER BY created_at DESC LIMIT 1;",
      result.session_id,
      result.error);
  if (!latest_summary_sources.has_value()) {
    return result;
  }
  result.latest_summary_source_turn_ids_json = *latest_summary_sources;
  result.latest_summary_references_second_turn =
      result.latest_summary_source_turn_ids_json.find(result.second_turn_id) !=
      std::string::npos;
  if (!result.latest_summary_references_second_turn) {
    result.error = "latest summary did not reference the second turn";
    return result;
  }

  const auto latest_summary_text = query_text_with_text_param(
      connection.handle,
      "SELECT summary_text FROM summaries WHERE session_id = ?1 ORDER BY created_at DESC LIMIT 1;",
      result.session_id,
      result.error);
  if (!latest_summary_text.has_value()) {
    return result;
  }
  result.latest_summary_text_prefix = truncate_prefix(*latest_summary_text, 160U);

  return result;
}

}  // namespace dasall::apps::daemon