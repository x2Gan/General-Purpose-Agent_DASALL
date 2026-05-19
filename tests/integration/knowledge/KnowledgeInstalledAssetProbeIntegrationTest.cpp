#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <sqlite3.h>

#include "IKnowledgeService.h"
#include "KnowledgeServiceFactory.h"
#include "KnowledgeTypes.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

using dasall::knowledge::CorpusChangeSet;
using dasall::knowledge::FreshnessState;
using dasall::knowledge::InstalledAssetKnowledgeServiceOptions;
using dasall::knowledge::KnowledgeQuery;
using dasall::knowledge::KnowledgeQueryKind;
using dasall::knowledge::RefreshStatus;
using dasall::tests::support::assert_true;

struct ScopedTempDir {
  fs::path path;

  ~ScopedTempDir() {
    std::error_code error;
    fs::remove_all(path, error);
  }
};

[[nodiscard]] fs::path repo_root() {
  return fs::path(DASALL_KNOWLEDGE_MODULE_CMAKE).parent_path().parent_path();
}

[[nodiscard]] fs::path make_temp_root() {
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  return fs::temp_directory_path() /
         ("dasall-installed-knowledge-probe-" + std::to_string(nonce));
}

void copy_fixture_assets(const fs::path& repository_root,
                        const fs::path& assets_root) {
  const auto profiles_source = repository_root / "profiles";
  const auto providers_source = repository_root / "llm" / "assets" / "providers";
  const auto architecture_source = repository_root / "docs" / "architecture";
  const auto adr_source = repository_root / "docs" / "adr";
  const auto ssot_source = repository_root / "docs" / "ssot";

  assert_true(fs::exists(profiles_source),
              "profiles asset root missing: " + profiles_source.string());
  assert_true(fs::exists(providers_source),
              "provider asset root missing: " + providers_source.string());
  assert_true(fs::exists(architecture_source),
              "architecture asset root missing: " + architecture_source.string());
  assert_true(fs::exists(adr_source),
              "adr asset root missing: " + adr_source.string());
  assert_true(fs::exists(ssot_source),
              "ssot asset root missing: " + ssot_source.string());

  fs::create_directories(assets_root);
  fs::copy(profiles_source,
           assets_root / "profiles",
           fs::copy_options::recursive);
  fs::create_directories(assets_root / "llm");
  fs::copy(providers_source,
           assets_root / "llm" / "providers",
           fs::copy_options::recursive);
  fs::create_directories(assets_root / "docs");
  fs::copy(architecture_source,
           assets_root / "docs" / "architecture",
           fs::copy_options::recursive);
  fs::copy(adr_source,
           assets_root / "docs" / "adr",
           fs::copy_options::recursive);
  fs::copy(ssot_source,
           assets_root / "docs" / "ssot",
           fs::copy_options::recursive);
}

[[nodiscard]] std::string format_error(
    const std::optional<dasall::contracts::ErrorInfo>& error) {
  if (!error.has_value()) {
    return "none";
  }

  std::ostringstream stream;
  stream << "code=";
  if (error->details.code.has_value()) {
    stream << *error->details.code;
  } else {
    stream << "none";
  }
  stream << ",message=" << error->details.message
         << ",stage=" << error->details.stage
         << ",ref_type=" << error->source_ref.ref_type
         << ",ref_id=" << error->source_ref.ref_id;
  return stream.str();
}

[[nodiscard]] std::string join_reason_codes(
    const std::vector<std::string>& reason_codes) {
  std::ostringstream stream;
  for (std::size_t index = 0; index < reason_codes.size(); ++index) {
    if (index > 0U) {
      stream << ',';
    }
    stream << reason_codes[index];
  }
  return stream.str();
}

void wait_for_refresh_completion(
    const std::shared_ptr<dasall::knowledge::IKnowledgeService>& service,
    std::string_view label) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    const auto health = service->health_snapshot();
    if (!health.refresh_in_flight && health.last_refresh_status.has_value()) {
      assert_true(*health.last_refresh_status == RefreshStatus::Completed,
                  std::string(label) + ": refresh should eventually complete successfully, actual reason_codes=" +
                      join_reason_codes(health.reason_codes));
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  throw std::runtime_error(std::string(label) + ": timed out waiting for async refresh completion");
}

[[nodiscard]] KnowledgeQuery make_probe_query() {
  KnowledgeQuery query;
  query.request_id = "req-installed-asset-probe";
  query.query_text = "DeepSeek Chat";
  query.query_kind = KnowledgeQueryKind::FactLookup;
  query.top_k = 3U;
  query.max_context_projection_items = 3U;
  return query;
}

struct ManualQueryResult {
  bool ok = false;
  std::size_t row_count = 0U;
  std::string error_message;
};

struct ManualCorpusCountResult {
  bool ok = false;
  std::size_t row_count = 0U;
  std::string error_message;
};

[[nodiscard]] ManualQueryResult run_manual_sparse_query(
    const fs::path& database_path) {
  constexpr const char* kSql =
      "SELECT c.corpus_id, c.document_id, c.chunk_id, bm25(chunks_fts) AS rank, c.chunk_text, "
      "c.citation_ref, c.updated_at, c.authority_level, c.language, c.tags "
      "FROM chunks_fts JOIN chunks c ON c.row_id = chunks_fts.rowid "
      "WHERE chunks_fts MATCH ?1 ORDER BY rank ASC LIMIT ?2;";
  constexpr const char* kExpression = "(\"deepseek chat\") OR (\"deepseek\" AND \"chat\")";

  sqlite3* database = nullptr;
  if (sqlite3_open_v2(database_path.c_str(),
                      &database,
                      SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX,
                      nullptr) != SQLITE_OK) {
    ManualQueryResult result;
    result.error_message = database != nullptr ? sqlite3_errmsg(database)
                                               : "sqlite_open_failed";
    if (database != nullptr) {
      sqlite3_close(database);
    }
    return result;
  }

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    ManualQueryResult result;
    result.error_message = sqlite3_errmsg(database);
    sqlite3_close(database);
    return result;
  }

  sqlite3_bind_text(statement, 1, kExpression, -1, SQLITE_STATIC);
  sqlite3_bind_int(statement, 2, 32);

  ManualQueryResult result;
  while (true) {
    const int step_status = sqlite3_step(statement);
    if (step_status == SQLITE_ROW) {
      ++result.row_count;
      continue;
    }
    if (step_status == SQLITE_DONE) {
      result.ok = true;
      break;
    }

    result.error_message = sqlite3_errmsg(database);
    break;
  }

  sqlite3_finalize(statement);
  sqlite3_close(database);
  return result;
}

[[nodiscard]] ManualCorpusCountResult run_manual_corpus_count_query(
    const fs::path& database_path,
    std::string_view corpus_id) {
  constexpr const char* kSql = "SELECT COUNT(*) FROM chunks WHERE corpus_id = ?1;";

  sqlite3* database = nullptr;
  if (sqlite3_open_v2(database_path.c_str(),
                      &database,
                      SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX,
                      nullptr) != SQLITE_OK) {
    ManualCorpusCountResult result;
    result.error_message = database != nullptr ? sqlite3_errmsg(database)
                                               : "sqlite_open_failed";
    if (database != nullptr) {
      sqlite3_close(database);
    }
    return result;
  }

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(database, kSql, -1, &statement, nullptr) != SQLITE_OK) {
    ManualCorpusCountResult result;
    result.error_message = sqlite3_errmsg(database);
    sqlite3_close(database);
    return result;
  }

  sqlite3_bind_text(statement,
                    1,
                    std::string(corpus_id).c_str(),
                    -1,
                    SQLITE_TRANSIENT);

  ManualCorpusCountResult result;
  const int step_status = sqlite3_step(statement);
  if (step_status == SQLITE_ROW) {
    result.ok = true;
    result.row_count = static_cast<std::size_t>(sqlite3_column_int64(statement, 0));
  } else {
    result.error_message = sqlite3_errmsg(database);
  }

  sqlite3_finalize(statement);
  sqlite3_close(database);
  return result;
}

void installed_asset_service_retrieves_deepseek_chat() {
  ScopedTempDir temp_root{make_temp_root()};
  const auto assets_root = temp_root.path / "assets";
  const auto state_root = temp_root.path / "state";

  copy_fixture_assets(repo_root(), assets_root);
  fs::create_directories(state_root);

  const auto factory_result = dasall::knowledge::create_installed_asset_knowledge_service(
      InstalledAssetKnowledgeServiceOptions{
          .readonly_assets_root = assets_root,
          .state_root = state_root,
        .service_instance_id = "knowledge-installed-asset-probe",
      });
  assert_true(factory_result.service != nullptr,
              "installed asset knowledge factory failed: " + factory_result.error);

  const auto initial_health = factory_result.service->health_snapshot();
  assert_true(!initial_health.refresh_in_flight,
              "installed asset init prewarm should settle before the factory returns");
  assert_true(initial_health.last_refresh_status.has_value() &&
                  *initial_health.last_refresh_status == RefreshStatus::Completed,
              "installed asset init prewarm should publish a completed terminal status");
  assert_true(!initial_health.active_snapshot_id.empty(),
              "installed asset init prewarm should publish an active snapshot before the first retrieve");

  const auto repeated_refresh_result = factory_result.service->request_refresh(CorpusChangeSet{});
  assert_true(repeated_refresh_result.status == RefreshStatus::Accepted,
              "installed asset repeated refresh failed: " +
                  format_error(repeated_refresh_result.error));
    wait_for_refresh_completion(factory_result.service,
                  "installed asset repeated refresh");

  const auto health = factory_result.service->health_snapshot();
  assert_true(!health.active_snapshot_id.empty(),
              "installed asset health snapshot should expose active snapshot id");
  assert_true(health.freshness_state == FreshnessState::Fresh,
              "installed asset health snapshot should be fresh, actual reason_codes=" +
                  join_reason_codes(health.reason_codes));
    assert_true(!health.refresh_in_flight,
          "installed asset health snapshot should be idle after the refresh settles");
    assert_true(health.last_refresh_status.has_value() &&
        *health.last_refresh_status == RefreshStatus::Completed,
          "installed asset health snapshot should expose the completed refresh terminal status");

    const auto snapshot_database = state_root / "knowledge" / "snapshots" /
                   health.active_snapshot_id / "lexical.sqlite";
    assert_true(fs::exists(snapshot_database),
          "installed asset snapshot database missing: " +
            snapshot_database.string());

    const auto manual_query = run_manual_sparse_query(snapshot_database);
    assert_true(manual_query.ok,
          "manual snapshot query failed: " + manual_query.error_message +
            ", database=" + snapshot_database.string());
    assert_true(manual_query.row_count > 0U,
          "manual snapshot query should return DeepSeek rows");

        for (const auto* corpus_id : {"architecture_reference",
              "adr_normative",
              "ssot_normative",
              "profile_policy_normative"}) {
          const auto corpus_count = run_manual_corpus_count_query(snapshot_database, corpus_id);
          assert_true(corpus_count.ok,
          std::string("manual corpus count query failed for ") + corpus_id + ": " +
              corpus_count.error_message + ", database=" + snapshot_database.string());
          assert_true(corpus_count.row_count > 0U,
          std::string("installed asset snapshot should index corpus ") + corpus_id);
        }

  const auto retrieve_result = factory_result.service->retrieve(make_probe_query());
  assert_true(retrieve_result.ok,
          "installed asset retrieve failed: " + format_error(retrieve_result.error) +
            ", manual_query_rows=" + std::to_string(manual_query.row_count) +
            ", database=" + snapshot_database.string());
  assert_true(retrieve_result.evidence.has_value(),
              "installed asset retrieve should return evidence bundle");
  assert_true(!retrieve_result.evidence->slices.empty(),
              "installed asset retrieve should return at least one evidence slice");

  const auto matching_slice = std::find_if(
      retrieve_result.evidence->slices.begin(),
      retrieve_result.evidence->slices.end(),
      [](const auto& slice) {
        return slice.snippet.find("DeepSeek Chat") != std::string::npos ||
               slice.citation_ref.find("deepseek") != std::string::npos;
      });
  assert_true(matching_slice != retrieve_result.evidence->slices.end(),
              "installed asset retrieve should surface DeepSeek Chat evidence");
}

}  // namespace

int main() {
  try {
    installed_asset_service_retrieves_deepseek_chat();
  } catch (const std::exception& exception) {
    std::cerr << "[KnowledgeInstalledAssetProbeIntegrationTest] FAILED: "
              << exception.what() << '\n';
    return 1;
  }

  return 0;
}