#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

#include "IKnowledgeService.h"
#include "ProfileCatalog.h"
#include "RuntimeDependencySet.h"
#include "RuntimeLiveDependencyComposition.h"
#include "RuntimePolicyProvider.h"
#include "logging/FileLogReader.h"
#include "logging/LogQueryService.h"
#include "logging/LoggingFacade.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

using dasall::tests::support::assert_true;

constexpr char kDefaultProfileId[] = "desktop_full";
constexpr char kCompositionOwner[] = "gateway.http-unary";
constexpr char kSessionId[] = "session-knowledge-production-logging";
constexpr char kQueryArtifactId[] = "knowledge-production-logging";

struct ScopedTempDir {
  fs::path path;

  ~ScopedTempDir() {
    std::error_code error;
    fs::remove_all(path, error);
  }
};

[[nodiscard]] fs::path make_temp_root() {
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  return fs::temp_directory_path() /
         ("dasall-knowledge-production-logging-" + std::to_string(nonce));
}

void copy_memory_assets_only(const fs::path& assets_root) {
  fs::create_directories(assets_root / "sql");
  fs::copy(fs::path(DASALL_SOURCE_ROOT) / "sql" / "memory",
           assets_root / "sql" / "memory",
           fs::copy_options::recursive);
}

void copy_installed_runtime_assets(const fs::path& assets_root) {
  copy_memory_assets_only(assets_root);
  fs::copy(fs::path(DASALL_SOURCE_ROOT) / "profiles",
           assets_root / "profiles",
           fs::copy_options::recursive);
  fs::create_directories(assets_root / "llm");
  fs::copy(fs::path(DASALL_SOURCE_ROOT) / "llm" / "assets" / "providers",
           assets_root / "llm" / "providers",
           fs::copy_options::recursive);
}

[[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>
load_runtime_policy_snapshot(const fs::path& assets_root) {
  const dasall::profiles::ProfileCatalog catalog(assets_root / "profiles");
  const dasall::profiles::RuntimePolicyProvider provider(catalog);
  const auto snapshot_result = provider.load_snapshot(
      dasall::profiles::RuntimePolicyLoadRequest{
          .profile_id = kDefaultProfileId,
      });
  assert_true(snapshot_result.ok() && snapshot_result.snapshot != nullptr,
              "knowledge production logging integration should load the runtime profile snapshot from copied assets");
  return snapshot_result.snapshot;
}

[[nodiscard]] std::string read_text(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] dasall::infra::logging::LogQueryAccessContext make_access_context() {
  return dasall::infra::logging::LogQueryAccessContext{
      .actor_ref = std::string("ops-user"),
      .consumer_module = std::string("diagnostics"),
      .policy_decision_ref = dasall::infra::policy::PolicyDecisionRef{
          .decision = dasall::infra::policy::PolicyDecision::Allow,
          .reason_code = std::string("allow_diag_pull"),
          .matched_rule_ids = {std::string("knowledge-logging-rule")},
          .snapshot_id = std::string("policy-snapshot-knowledge-logging"),
          .generation = 7,
          .evidence_ref = std::string("policy://knowledge/logging/integration"),
          .warnings = {},
      },
      .infra_context = dasall::infra::InfraContext{
          .request_id = std::string("req-knowledge-production-logging-query"),
          .session_id = std::string(kSessionId),
          .trace_id = std::string("trace-knowledge-production-logging"),
          .task_id = std::string("task-knowledge-production-logging"),
          .parent_task_id = std::string("parent-knowledge-production-logging"),
          .lease_id = std::string("lease-knowledge-production-logging"),
      },
  };
}

[[nodiscard]] dasall::knowledge::KnowledgeQuery make_success_query() {
  dasall::knowledge::KnowledgeQuery query;
  query.request_id = "req-knowledge-production-logging-success";
  query.session_id = std::string(kSessionId);
  query.query_text = "DeepSeek Chat";
  query.query_kind = dasall::knowledge::KnowledgeQueryKind::FactLookup;
  query.top_k = 3U;
  query.max_context_projection_items = 3U;
  return query;
}

[[nodiscard]] dasall::knowledge::KnowledgeQuery make_primary_failure_query() {
  auto query = make_success_query();
  query.request_id = "req-knowledge-production-logging-failure";
  query.query_text.clear();
  return query;
}

[[nodiscard]] dasall::knowledge::KnowledgeQuery make_fallback_query() {
  auto query = make_success_query();
  query.request_id.clear();
  query.query_text = "knowledge-secret-fallback-body";
  return query;
}

void knowledge_production_logging_persists_primary_and_fallback_events() {
  using dasall::infra::logging::FileLogReader;
  using dasall::infra::logging::FileLogReaderOptions;
  using dasall::infra::logging::LogFlushDeadline;
  using dasall::infra::logging::LogQueryRequest;
  using dasall::infra::logging::LogQuerySelectorKind;
  using dasall::infra::logging::LogQueryService;
  using dasall::infra::logging::LogQueryServiceOptions;
  using dasall::infra::logging::LoggingFacade;

  ScopedTempDir temp_root{make_temp_root()};
  const auto assets_root = temp_root.path / "assets";
  const auto state_root = temp_root.path / "state";
  const auto runtime_log_path = state_root / "logging" / "runtime.log";
  const auto artifact_root = state_root / "query-artifacts";

  copy_installed_runtime_assets(assets_root);
  fs::create_directories(state_root);

  const auto policy_snapshot = load_runtime_policy_snapshot(assets_root);
  const auto composition = dasall::apps::runtime_support::compose_minimal_live_dependency_set(
      policy_snapshot,
      kCompositionOwner,
      dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions{
          .readonly_assets_root_override = assets_root,
          .runtime_library_root_override = {},
          .state_root_override = state_root,
          .build_dense_snapshot_override = {},
          .create_vector_recall_store_override = {},
          .create_query_encoder_override = {},
          .knowledge_query_encoder_transport_override = nullptr,
          .knowledge_refresh_timer = nullptr,
          .knowledge_refresh_source_provider = nullptr,
      });
  assert_true(composition.ok(),
              "knowledge production logging integration should compose live runtime dependencies: " +
                  composition.error);
  assert_true(composition.dependency_set != nullptr &&
                  composition.dependency_set->knowledge_service != nullptr,
              "knowledge production logging integration should expose the installed knowledge service through runtime composition");

  const auto logger = std::dynamic_pointer_cast<LoggingFacade>(composition.dependency_set->logger);
  assert_true(logger != nullptr,
              "knowledge production logging integration should keep the concrete logger inspectable");

  const auto success_result = composition.dependency_set->knowledge_service->retrieve(
      make_success_query());
  assert_true(success_result.ok && success_result.evidence.has_value() &&
                  !success_result.evidence->slices.empty(),
              "knowledge production logging integration should keep the installed retrieve path successful");

  const auto primary_failure_result = composition.dependency_set->knowledge_service->retrieve(
      make_primary_failure_query());
  assert_true(!primary_failure_result.ok && primary_failure_result.error.has_value(),
              "knowledge production logging integration should emit a primary failed retrieve event for invalid query text");

  const auto fallback_result = composition.dependency_set->knowledge_service->retrieve(
      make_fallback_query());
  assert_true(!fallback_result.ok && fallback_result.error.has_value(),
              "knowledge production logging integration should fail closed when the request_id is missing");

  assert_true(logger->flush(LogFlushDeadline{.timeout_ms = 500}).ok,
              "knowledge production logging integration should flush the live logger before inspecting runtime.log");

  auto reader = std::make_shared<FileLogReader>(FileLogReaderOptions{
      .runtime_log_path = runtime_log_path,
      .include_rotation_family = true,
  });
  LogQueryService service(reader,
                          LogQueryServiceOptions{
                              .enable_diag_pull = true,
                              .artifact_namespace = "diag://infra/logging/query",
                              .artifact_root_dir = artifact_root,
                              .index_file_name = "query-index.jsonl",
                              .retention_policy = {.retention_days = 7, .max_artifact_count = 8},
                          },
                          []() { return static_cast<std::int64_t>(4102444800000); });

  const auto query_result = service.query(LogQueryRequest{
                                              .query_id = std::string(kQueryArtifactId),
                                              .selector_kind = LogQuerySelectorKind::SessionId,
                                              .selector_value = std::string(kSessionId),
                                              .start_ts_ms = 1,
                                              .end_ts_ms = 4102444800000,
                                              .max_records = 16,
                                          },
                                          make_access_context());

  const auto runtime_log_text = read_text(runtime_log_path);
  const auto artifact_path = artifact_root /
                             "knowledge-production-logging-4102444800000.json";
  const auto artifact_text = read_text(artifact_path);
  const auto index_text = read_text(artifact_root / "query-index.jsonl");

    assert_true(runtime_log_text.find("knowledge.retrieve.completed") !=
              std::string::npos &&
            runtime_log_text.find("knowledge.retrieve.failed") !=
              std::string::npos &&
            runtime_log_text.find("knowledge.retrieve.invalid_payload") !=
              std::string::npos,
              "knowledge production logging integration should persist completed, failed, and fallback invalid-payload knowledge events into runtime.log");
  assert_true(runtime_log_text.find("\"module\":\"knowledge\"") != std::string::npos &&
                  runtime_log_text.find("\"session_id\":\"session-knowledge-production-logging\"") !=
                      std::string::npos &&
                  runtime_log_text.find("\"telemetry_path\":\"primary\"") !=
                      std::string::npos &&
                  runtime_log_text.find("\"telemetry_path\":\"fallback\"") !=
                      std::string::npos,
              "knowledge production logging integration should persist structured session-scoped attrs and primary/fallback telemetry paths into runtime.log");
  assert_true(runtime_log_text.find("knowledge-secret-fallback-body") == std::string::npos,
              "knowledge production logging integration should not leak raw retrieve query text into runtime.log");

  assert_true(query_result.ok && query_result.has_success_payload(),
              "knowledge production logging integration should materialize a query artifact from persisted runtime.log by session_id");
  assert_true(query_result.match_count >= 2U,
              "knowledge production logging integration should query at least the completed and failed knowledge events by session_id");
  assert_true(artifact_text.find("knowledge.retrieve.completed") != std::string::npos &&
                  artifact_text.find("knowledge.retrieve.failed") != std::string::npos &&
                  artifact_text.find("session-knowledge-production-logging") !=
                      std::string::npos,
              "knowledge production logging integration should materialize session-scoped knowledge records into the diagnostics artifact");
  assert_true(index_text.find("knowledge-production-logging") != std::string::npos &&
                  index_text.find("session-knowledge-production-logging") !=
                      std::string::npos,
              "knowledge production logging integration should index the session-scoped diagnostics artifact");
}

}  // namespace

int main() {
  try {
    knowledge_production_logging_persists_primary_and_fallback_events();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}