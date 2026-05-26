#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

#include "ILLMTransport.h"
#include "IKnowledgeService.h"
#include "KnowledgeTypes.h"
#include "ProfileCatalog.h"
#include "RuntimeDependencySet.h"
#include "RuntimeLiveDependencyComposition.h"
#include "RuntimePolicyProvider.h"
#include "audit/AuditService.h"
#include "index/IndexWriter.h"
#include "metrics/MetricsFacade.h"
#include "retrieve/IVectorRecallStore.h"
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

class ScopedEnvironmentVariable {
 public:
  ScopedEnvironmentVariable(std::string name, std::optional<std::string> value)
      : name_(std::move(name)) {
    if (const char* existing_value = std::getenv(name_.c_str()); existing_value != nullptr) {
      previous_value_ = existing_value;
    }

    if (value.has_value()) {
      setenv(name_.c_str(), value->c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

  ~ScopedEnvironmentVariable() {
    if (previous_value_.has_value()) {
      setenv(name_.c_str(), previous_value_->c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

 private:
  std::string name_;
  std::optional<std::string> previous_value_;
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

[[nodiscard]] std::string hex_encode(const std::string& plaintext) {
  constexpr char kHexDigits[] = "0123456789abcdef";

  std::string encoded;
  encoded.reserve(plaintext.size() * 2U);
  for (const unsigned char value : plaintext) {
    encoded.push_back(kHexDigits[(value >> 4U) & 0x0FU]);
    encoded.push_back(kHexDigits[value & 0x0FU]);
  }

  return encoded;
}

void write_secret_fixture(const fs::path& state_root,
                         const std::string& secret_name,
                         const std::string& plaintext) {
  const fs::path secret_path = state_root / "secrets" / (secret_name + ".secret");
  fs::create_directories(secret_path.parent_path());

  std::ofstream stream(secret_path);
  stream << "secret_name=" << secret_name << '\n';
  stream << "classification=credential\n";
  stream << "rotation_policy=rotation/default\n";
  stream << "owner=ops\n";
  stream << "version=v1\n";
  stream << "ciphertext_hex=" << hex_encode(plaintext) << '\n';
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

[[nodiscard]] dasall::knowledge::KnowledgeQuery make_hybrid_canary_query(
    const std::string& request_suffix) {
  dasall::knowledge::KnowledgeQuery query;
  query.request_id = "req-knowledge-production-telemetry-" + request_suffix;
  query.query_text = "ContextOrchestrator PromptComposer";
  query.preferred_mode = dasall::knowledge::RetrievalMode::Hybrid;
  query.query_kind = dasall::knowledge::KnowledgeQueryKind::PolicyEvidence;
  query.allowed_corpora = {"adr_normative"};
  query.top_k = 4U;
  query.max_context_projection_items = 4U;
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

[[nodiscard]] std::size_t count_token_occurrences(const dasall::infra::ExportResult& result,
                                                  const std::string& token) {
  std::size_t count = 0U;
  for (const auto& record : result.records) {
    count += static_cast<std::size_t>(std::count_if(
        record.side_effects.begin(), record.side_effects.end(),
        [&](const std::string& side_effect) {
          return side_effect.find(token) != std::string::npos;
        }));
  }
  return count;
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

[[nodiscard]] std::optional<std::string> header_value(
    const dasall::llm::LLMTransportRequest& request,
    std::string_view header_name) {
  const auto header_it = std::find_if(
      request.headers.begin(), request.headers.end(),
      [&](const dasall::llm::LLMTransportHeader& header) {
        return header.name == header_name;
      });
  if (header_it == request.headers.end()) {
    return std::nullopt;
  }

  return header_it->value;
}

class StubEmbeddingTransport final : public dasall::llm::ILLMTransport {
 public:
  using Handler = std::function<dasall::llm::LLMTransportResponse(
      const dasall::llm::LLMTransportRequest& request)>;

  explicit StubEmbeddingTransport(Handler handler)
      : handler_(std::move(handler)) {}

  [[nodiscard]] dasall::llm::LLMTransportResponse send(
      const dasall::llm::LLMTransportRequest& request) override {
    last_request_ = request;
    ++send_calls_;
    return handler_(request);
  }

  mutable std::optional<dasall::llm::LLMTransportRequest> last_request_;
  mutable int send_calls_ = 0;

 private:
  Handler handler_;
};

class TelemetryEmbeddingRequiredVectorRecallStore final
    : public dasall::knowledge::retrieve::IVectorRecallStore {
 public:
  [[nodiscard]] bool available() const override {
    return true;
  }

  [[nodiscard]] dasall::knowledge::retrieve::DenseQueryInputMode query_input_mode() const override {
    return dasall::knowledge::retrieve::DenseQueryInputMode::EmbeddingRequired;
  }

  [[nodiscard]] std::vector<dasall::knowledge::retrieve::RecallHit> search(
      const dasall::knowledge::retrieve::DenseQueryRequest&) const override {
    dasall::knowledge::retrieve::RecallHit hit;
    hit.corpus_id = "adr_normative";
    hit.document_id = "adr-006";
    hit.chunk_id = "adr-006#telemetry-provider-query-encoder";
    hit.score = 0.94F;
    hit.raw_snippet = "ContextOrchestrator PromptComposer owner boundary evidence.";
    hit.citation_ref = "ADR-006#context-owner";
    hit.updated_at = 1713657600000;
    hit.authority_level = dasall::knowledge::AuthorityLevel::Normative;
    hit.tags = {"normative", "architecture"};
    return {hit};
  }
};

[[nodiscard]] dasall::knowledge::index::DenseSnapshotBuildResult build_fake_dense_snapshot(
    const dasall::knowledge::index::DenseSnapshotBuildRequest&) {
  return dasall::knowledge::index::DenseSnapshotBuildResult{.ok = true, .warnings = {}};
}

[[nodiscard]]
dasall::apps::runtime_support::RuntimeDependencyCompositionResult compose_provider_runtime(
    const std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>& policy_snapshot,
    const fs::path& assets_root,
    const fs::path& state_root,
    const std::shared_ptr<dasall::llm::ILLMTransport>& transport) {
  return dasall::apps::runtime_support::compose_minimal_live_dependency_set(
      policy_snapshot,
      kCompositionOwner,
      dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions{
          .readonly_assets_root_override = assets_root,
          .runtime_library_root_override = {},
          .state_root_override = state_root,
          .build_dense_snapshot_override = build_fake_dense_snapshot,
          .create_vector_recall_store_override =
              [](const dasall::knowledge::DenseStoreFactoryContext&) {
                return std::make_unique<TelemetryEmbeddingRequiredVectorRecallStore>();
              },
          .create_query_encoder_override = {},
          .knowledge_query_encoder_transport_override = transport,
          .knowledge_refresh_timer = nullptr,
          .knowledge_refresh_source_provider = nullptr,
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

void knowledge_production_telemetry_records_provider_query_encoder_reason_codes() {
  const ScopedEnvironmentVariable local_fallback_disabled(
      "DASALL_DETACHED_VECTOR_LOCAL_FALLBACK",
      std::nullopt);

  {
    ScopedTempDir temp_root{make_temp_root()};
    const auto assets_root = temp_root.path / "assets";
    const auto state_root = temp_root.path / "state";

    copy_installed_runtime_assets(assets_root);
    fs::create_directories(state_root);
    write_secret_fixture(state_root,
                         "llm/providers/deepseek-prod",
                         "ready-token");

    const auto transport = std::make_shared<StubEmbeddingTransport>(
        [](const dasall::llm::LLMTransportRequest& request) {
          const auto authorization = header_value(request, "Authorization");
          if (!authorization.has_value() || *authorization != "Bearer ready-token") {
            return dasall::llm::LLMTransportResponse{
                .status_code = 401U,
                .body = "{\"error\":\"unauthorized\"}",
                .error_message = {},
            };
          }

          return dasall::llm::LLMTransportResponse{
              .status_code = 200U,
              .body = "{\"data\":[{\"embedding\":[0.2,0.3,0.5],\"index\":0}],\"model\":\"deepseek-embedding\"}",
              .error_message = {},
          };
        });

    const auto policy_snapshot = load_runtime_policy_snapshot(assets_root);
    const auto composition = compose_provider_runtime(policy_snapshot,
                                                      assets_root,
                                                      state_root,
                                                      transport);
    assert_true(composition.ok(),
                "knowledge production telemetry integration should compose provider-backed runtime dependencies: " +
                    composition.error);

    const auto audit_service = std::dynamic_pointer_cast<dasall::infra::audit::AuditService>(
        composition.dependency_set->audit_logger);
    const auto metrics_facade = std::dynamic_pointer_cast<dasall::infra::metrics::MetricsFacade>(
        composition.dependency_set->metrics_provider);
    const auto tracer_provider = std::dynamic_pointer_cast<dasall::infra::tracing::TracerProviderImpl>(
        composition.dependency_set->tracer_provider);
    assert_true(audit_service != nullptr && metrics_facade != nullptr &&
                    tracer_provider != nullptr,
                "knowledge production telemetry integration should keep provider-backed observability sinks inspectable");

    const auto baseline_audit_export = export_all_audit(audit_service);
    const auto baseline_admitted_tokens =
        count_token_occurrences(baseline_audit_export, "runtime_canary_admitted");
    const auto baseline_metric_attempts = metrics_facade->record_attempt_count();
    const auto baseline_trace_exports = tracer_provider->export_success_total();

    const auto result = composition.dependency_set->knowledge_service->retrieve(
        make_hybrid_canary_query("provider-ready"));
    assert_true(result.ok && result.mode == dasall::knowledge::RetrievalMode::Hybrid,
                "knowledge production telemetry integration should keep the provider-backed canary on Hybrid");

    const auto audit_export = export_all_audit(audit_service);
    assert_true(count_token_occurrences(audit_export, "runtime_canary_admitted") >
                    baseline_admitted_tokens,
                "knowledge production telemetry integration should append runtime_canary_admitted to the shared audit sink for provider-backed Hybrid success");
    assert_true(export_contains_token(audit_export, "retrieval_mode=hybrid") &&
                    export_contains_token(audit_export, "vector_backend_ready=true") &&
                    export_contains_token(audit_export, "dense_hit_count="),
                "knowledge production telemetry integration should persist provider-backed Hybrid explain facts into the shared audit sink");
    assert_true(metrics_facade->record_attempt_count() > baseline_metric_attempts,
                "knowledge production telemetry integration should emit metrics for provider-backed Hybrid success");
    assert_true(tracer_provider->export_success_total() > baseline_trace_exports,
                "knowledge production telemetry integration should export traces for provider-backed Hybrid success");
  }

  {
    ScopedTempDir temp_root{make_temp_root()};
    const auto assets_root = temp_root.path / "assets";
    const auto state_root = temp_root.path / "state";

    copy_installed_runtime_assets(assets_root);
    fs::create_directories(state_root);
    write_secret_fixture(state_root,
                         "llm/providers/deepseek-prod",
                         "bad-token");

    const auto transport = std::make_shared<StubEmbeddingTransport>(
        [](const dasall::llm::LLMTransportRequest& request) {
          const auto authorization = header_value(request, "Authorization");
          if (authorization.has_value() && *authorization == "Bearer ready-token") {
            return dasall::llm::LLMTransportResponse{
                .status_code = 200U,
                .body = "{\"data\":[{\"embedding\":[0.2,0.3,0.5],\"index\":0}],\"model\":\"deepseek-embedding\"}",
                .error_message = {},
            };
          }

          return dasall::llm::LLMTransportResponse{
              .status_code = 401U,
              .body = "{\"error\":\"unauthorized\"}",
              .error_message = {},
          };
        });

    const auto policy_snapshot = load_runtime_policy_snapshot(assets_root);
    const auto composition = compose_provider_runtime(policy_snapshot,
                                                      assets_root,
                                                      state_root,
                                                      transport);
    assert_true(composition.ok(),
                "knowledge production telemetry integration should compose invalid-secret runtime dependencies: " +
                    composition.error);

    const auto audit_service = std::dynamic_pointer_cast<dasall::infra::audit::AuditService>(
        composition.dependency_set->audit_logger);
    assert_true(audit_service != nullptr,
                "knowledge production telemetry integration should keep the invalid-secret audit sink inspectable");

    const auto baseline_audit_export = export_all_audit(audit_service);
    const auto baseline_invalid_secret_tokens =
        count_token_occurrences(baseline_audit_export, "query_encoder_invalid_secret");
    const auto baseline_backend_not_ready_tokens =
        count_token_occurrences(baseline_audit_export, "runtime_canary_backend_not_ready");

    const auto result = composition.dependency_set->knowledge_service->retrieve(
        make_hybrid_canary_query("invalid-secret"));
    assert_true(result.ok && result.mode == dasall::knowledge::RetrievalMode::LexicalOnly,
                "knowledge production telemetry integration should fail closed to lexical-only when the provider secret is invalid");

    const auto audit_export = export_all_audit(audit_service);
    assert_true(count_token_occurrences(audit_export, "query_encoder_invalid_secret") >
                    baseline_invalid_secret_tokens,
                "knowledge production telemetry integration should append query_encoder_invalid_secret to the shared audit sink on invalid-secret fallback");
    assert_true(count_token_occurrences(audit_export, "runtime_canary_backend_not_ready") >
                    baseline_backend_not_ready_tokens,
                "knowledge production telemetry integration should append runtime_canary_backend_not_ready to the shared audit sink on invalid-secret fallback");
    assert_true(export_contains_token(audit_export, "retrieval_mode=lexical_only") &&
                    export_contains_token(audit_export, "vector_backend_ready=false"),
                "knowledge production telemetry integration should persist lexical fail-closed explain facts into the shared audit sink on invalid-secret fallback");
  }
}

}  // namespace

int main() {
  try {
    knowledge_production_telemetry_routes_runtime_live_retrieve_events_to_shared_sinks();
    knowledge_production_telemetry_records_provider_query_encoder_reason_codes();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}