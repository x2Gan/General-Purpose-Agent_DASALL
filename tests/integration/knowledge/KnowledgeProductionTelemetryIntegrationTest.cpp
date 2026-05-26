#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>

#include "IKnowledgeService.h"
#include "KnowledgeTypes.h"
#include "ProfileCatalog.h"
#include "RuntimeDependencySet.h"
#include "RuntimeLiveDependencyComposition.h"
#include "RuntimePolicyProvider.h"
#include "audit/AuditService.h"
#include "metrics/MetricsFacade.h"
#include "support/TestAssertions.h"
#include "tracing/TracerProviderImpl.h"

namespace {

namespace fs = std::filesystem;

using dasall::tests::support::assert_true;

constexpr char kDefaultProfileId[] = "desktop_full";
constexpr char kCompositionOwner[] = "gateway.http-unary";

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
         ("dasall-knowledge-production-telemetry-" + std::to_string(nonce));
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
              "knowledge production telemetry integration should load the runtime profile snapshot from copied assets");
  return snapshot_result.snapshot;
}

[[nodiscard]] dasall::knowledge::KnowledgeQuery make_success_query() {
  dasall::knowledge::KnowledgeQuery query;
  query.request_id = "req-knowledge-production-telemetry-success";
  query.query_text = "DeepSeek Chat";
  query.query_kind = dasall::knowledge::KnowledgeQueryKind::FactLookup;
  query.top_k = 3U;
  query.max_context_projection_items = 3U;
  return query;
}

[[nodiscard]] dasall::knowledge::KnowledgeQuery make_failure_query() {
  auto query = make_success_query();
  query.request_id = "req-knowledge-production-telemetry-failure";
  query.query_text.clear();
  return query;
}

[[nodiscard]] std::size_t count_action(const dasall::infra::ExportResult& result,
                                       const std::string& expected_action) {
  return static_cast<std::size_t>(std::count_if(
      result.records.begin(), result.records.end(),
      [&expected_action](const dasall::infra::AuditEvent& event) {
        return event.action == expected_action;
      }));
}

[[nodiscard]] bool export_contains_token(const dasall::infra::ExportResult& result,
                                         const std::string& token) {
  for (const auto& record : result.records) {
    for (const auto& side_effect : record.side_effects) {
      if (side_effect.find(token) != std::string::npos) {
        return true;
      }
    }
  }

  return false;
}

[[nodiscard]] dasall::infra::ExportResult export_all_audit(
    const std::shared_ptr<dasall::infra::audit::AuditService>& audit_service) {
  return audit_service->export_audit(dasall::infra::ExportQuery{
      .start_ts = 1,
      .end_ts = 4102444800000,
      .actor = std::string(),
      .action = std::string(),
      .target = std::string(),
      .outcome = dasall::infra::AuditOutcome::Unspecified,
      .page_token = std::string(),
  });
}

void knowledge_production_telemetry_routes_runtime_live_retrieve_events_to_shared_sinks() {
  ScopedTempDir temp_root{make_temp_root()};
  const auto assets_root = temp_root.path / "assets";
  const auto state_root = temp_root.path / "state";

  copy_installed_runtime_assets(assets_root);
  fs::create_directories(state_root);

  const auto policy_snapshot = load_runtime_policy_snapshot(assets_root);
  const auto composition = dasall::apps::runtime_support::compose_minimal_live_dependency_set(
      policy_snapshot,
      kCompositionOwner,
      dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions{
          .readonly_assets_root_override = assets_root,
          .state_root_override = state_root,
      });
  assert_true(composition.ok(),
              "knowledge production telemetry integration should compose live runtime dependencies: " +
                  composition.error);
  assert_true(composition.dependency_set != nullptr &&
                  composition.dependency_set->knowledge_service != nullptr,
              "knowledge production telemetry integration should expose the installed knowledge service through runtime composition");

  const auto audit_service = std::dynamic_pointer_cast<dasall::infra::audit::AuditService>(
      composition.dependency_set->audit_logger);
  const auto metrics_facade = std::dynamic_pointer_cast<dasall::infra::metrics::MetricsFacade>(
      composition.dependency_set->metrics_provider);
  const auto tracer_provider = std::dynamic_pointer_cast<dasall::infra::tracing::TracerProviderImpl>(
      composition.dependency_set->tracer_provider);
  assert_true(audit_service != nullptr && metrics_facade != nullptr &&
                  tracer_provider != nullptr,
              "knowledge production telemetry integration should keep concrete audit, metrics, and trace providers inspectable");

  const auto baseline_audit_export = export_all_audit(audit_service);
  const auto baseline_completed =
      count_action(baseline_audit_export, "knowledge.retrieve.completed");
  const auto baseline_failed =
      count_action(baseline_audit_export, "knowledge.retrieve.failed");
  const auto baseline_metric_attempts = metrics_facade->record_attempt_count();
  const auto baseline_trace_exports = tracer_provider->export_success_total();

  const auto success_result = composition.dependency_set->knowledge_service->retrieve(
      make_success_query());
  assert_true(success_result.ok && success_result.evidence.has_value() &&
                  !success_result.evidence->slices.empty(),
              "knowledge production telemetry integration should keep the installed retrieve path successful");
  assert_true(success_result.mode == dasall::knowledge::RetrievalMode::LexicalOnly,
              "knowledge production telemetry integration should keep production installed retrieval lexical-only");

  const auto failure_result = composition.dependency_set->knowledge_service->retrieve(
      make_failure_query());
  assert_true(!failure_result.ok && failure_result.error.has_value(),
              "knowledge production telemetry integration should emit a failure event for invalid retrieve input");

  const auto audit_export = export_all_audit(audit_service);
  assert_true(count_action(audit_export, "knowledge.retrieve.completed") > baseline_completed,
              "knowledge production telemetry integration should append a completed retrieve audit event");
  assert_true(count_action(audit_export, "knowledge.retrieve.failed") > baseline_failed,
              "knowledge production telemetry integration should append a failed retrieve audit event");
  assert_true(export_contains_token(audit_export, "profile_id=desktop_full") &&
                  export_contains_token(audit_export, "retrieval_mode=lexical_only") &&
            export_contains_token(audit_export, "query_kind=fact_lookup") &&
            export_contains_token(audit_export, "vector_backend_ready=") &&
            export_contains_token(audit_export, "selected_corpora=") &&
            export_contains_token(audit_export, "sparse_hit_count=") &&
            export_contains_token(audit_export, "dense_hit_count=") &&
            export_contains_token(audit_export, "warning_count="),
          "knowledge production telemetry integration should persist vector explain facts into the shared audit sink");
  assert_true(metrics_facade->record_attempt_count() > baseline_metric_attempts,
              "knowledge production telemetry integration should emit metrics samples for retrieve telemetry");
  assert_true(tracer_provider->export_success_total() > baseline_trace_exports,
              "knowledge production telemetry integration should export trace spans for retrieve telemetry");
}

}  // namespace

int main() {
  try {
    knowledge_production_telemetry_routes_runtime_live_retrieve_events_to_shared_sinks();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}