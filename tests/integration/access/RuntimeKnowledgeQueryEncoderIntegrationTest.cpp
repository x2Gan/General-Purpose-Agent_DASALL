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
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "IKnowledgeService.h"
#include "ILLMTransport.h"
#include "KnowledgeTypes.h"
#include "index/IndexWriter.h"
#include "ProfileCatalog.h"
#include "retrieve/IVectorRecallStore.h"
#include "RuntimeDependencySet.h"
#include "RuntimeLiveDependencyComposition.h"
#include "RuntimePolicyProvider.h"
#include "config/InstallLayout.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

constexpr char kCompositionOwner[] = "daemon.local-control-plane";

class TempStateRoot {
 public:
  explicit TempStateRoot(const std::string& stem)
      : path_(fs::temp_directory_path() /
              (stem + "-" + std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count()))) {
    fs::create_directories(path_);
  }

  ~TempStateRoot() {
    std::error_code error;
    fs::remove_all(path_, error);
  }

  [[nodiscard]] const fs::path& path() const {
    return path_;
  }

 private:
  fs::path path_;
};

class ScopedEnvironmentVariable {
 public:
  ScopedEnvironmentVariable(std::string name, std::optional<std::string> value)
      : name_(std::move(name)) {
    const char* existing_value = std::getenv(name_.c_str());
    if (existing_value != nullptr) {
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

[[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>
load_runtime_policy_snapshot(const std::string& profile_id) {
  const auto install_layout = dasall::infra::config::resolve_install_layout();
  const dasall::profiles::ProfileCatalog catalog(install_layout.profiles_root);
  const dasall::profiles::RuntimePolicyProvider provider(catalog);
  const auto snapshot_result = provider.load_snapshot(
      dasall::profiles::RuntimePolicyLoadRequest{.profile_id = profile_id});
  assert_true(snapshot_result.ok() && snapshot_result.snapshot != nullptr,
              "runtime query encoder integration should load policy snapshot for " +
                  profile_id);
  return snapshot_result.snapshot;
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
  fs::create_directories(assets_root / "docs");
  fs::copy(fs::path(DASALL_SOURCE_ROOT) / "docs" / "architecture",
           assets_root / "docs" / "architecture",
           fs::copy_options::recursive);
  fs::copy(fs::path(DASALL_SOURCE_ROOT) / "docs" / "adr",
           assets_root / "docs" / "adr",
           fs::copy_options::recursive);
  fs::copy(fs::path(DASALL_SOURCE_ROOT) / "docs" / "ssot",
           assets_root / "docs" / "ssot",
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

[[nodiscard]] std::string read_text_file(const fs::path& file_path) {
  std::ifstream stream(file_path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

void write_text_file(const fs::path& file_path, const std::string& content) {
  std::ofstream stream(file_path, std::ios::binary | std::ios::trunc);
  stream << content;
}

void remove_embedding_feature_note(const fs::path& assets_root) {
  const fs::path models_path = assets_root / "llm" / "providers" / "deepseek" / "models.yaml";
  std::istringstream input(read_text_file(models_path));
  std::ostringstream output;
  std::string line;
  bool removed = false;
  while (std::getline(input, line)) {
    if (line.find("knowledge_query_embedding_default") != std::string::npos) {
      removed = true;
      continue;
    }
    output << line << '\n';
  }

  assert_true(removed,
              "runtime query encoder integration should remove the embedding feature note for provider-missing coverage");
  write_text_file(models_path, output.str());
}

[[nodiscard]] bool contains_reason_code(const std::vector<std::string>& reason_codes,
                                        std::string_view expected_code) {
  return std::find(reason_codes.begin(), reason_codes.end(), expected_code) !=
         reason_codes.end();
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

class RecordingEmbeddingRequiredVectorRecallStore final
    : public dasall::knowledge::retrieve::IVectorRecallStore {
 public:
  [[nodiscard]] bool available() const override {
    return true;
  }

  [[nodiscard]] dasall::knowledge::retrieve::DenseQueryInputMode query_input_mode() const override {
    return dasall::knowledge::retrieve::DenseQueryInputMode::EmbeddingRequired;
  }

  [[nodiscard]] std::vector<dasall::knowledge::retrieve::RecallHit> search(
      const dasall::knowledge::retrieve::DenseQueryRequest& request) const override {
    last_request_ = request;

    dasall::knowledge::retrieve::RecallHit hit;
    hit.corpus_id = "adr_normative";
    hit.document_id = "adr-006";
    hit.chunk_id = "adr-006#chunk-query-encoder";
    hit.score = 0.91F;
    hit.raw_snippet = "ContextOrchestrator PromptComposer owner boundary evidence.";
    hit.citation_ref = "ADR-006#context-owner";
    hit.updated_at = 1713657600000;
    hit.authority_level = dasall::knowledge::AuthorityLevel::Normative;
    hit.tags = {"normative", "architecture"};
    return {hit};
  }

  mutable std::optional<dasall::knowledge::retrieve::DenseQueryRequest> last_request_;
};

[[nodiscard]] dasall::knowledge::index::DenseSnapshotBuildResult build_fake_dense_snapshot(
    const dasall::knowledge::index::DenseSnapshotBuildRequest&) {
  return dasall::knowledge::index::DenseSnapshotBuildResult{.ok = true, .warnings = {}};
}

[[nodiscard]] dasall::knowledge::KnowledgeQuery make_allowlisted_hybrid_query(
    const std::string& request_suffix) {
  dasall::knowledge::KnowledgeQuery query;
  query.request_id = "req-runtime-query-encoder-" + request_suffix;
  query.query_text = "ContextOrchestrator PromptComposer";
  query.preferred_mode = dasall::knowledge::RetrievalMode::Hybrid;
  query.query_kind = dasall::knowledge::KnowledgeQueryKind::PolicyEvidence;
  query.allowed_corpora = {"adr_normative"};
  query.top_k = 4U;
  query.max_context_projection_items = 4U;
  return query;
}

struct ComposedKnowledgeRuntime {
  std::shared_ptr<dasall::knowledge::IKnowledgeService> knowledge_service;
  RecordingEmbeddingRequiredVectorRecallStore* vector_store = nullptr;
};

struct ComposeRuntimeOptions {
  std::shared_ptr<dasall::llm::ILLMTransport> query_encoder_transport;
};

[[nodiscard]] ComposedKnowledgeRuntime compose_runtime(
    const std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>& policy_snapshot,
    const fs::path& readonly_assets_root,
    const fs::path& state_root,
    const ComposeRuntimeOptions& runtime_options = {}) {
  auto vector_store = std::make_unique<RecordingEmbeddingRequiredVectorRecallStore>();
  auto* vector_store_ptr = vector_store.get();

  auto owned_store =
      std::make_shared<std::unique_ptr<RecordingEmbeddingRequiredVectorRecallStore>>(
          std::move(vector_store));
  dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions options{
      .readonly_assets_root_override = readonly_assets_root,
      .runtime_library_root_override = fs::path{},
      .state_root_override = state_root,
      .build_dense_snapshot_override = build_fake_dense_snapshot,
      .create_vector_recall_store_override =
          [owned_store](const dasall::knowledge::DenseStoreFactoryContext&) mutable {
            return std::move(*owned_store);
          },
      .create_query_encoder_override = {},
      .knowledge_query_encoder_transport_override = runtime_options.query_encoder_transport,
      .knowledge_refresh_timer = nullptr,
      .knowledge_refresh_source_provider = nullptr,
  };

  const auto composition = dasall::apps::runtime_support::compose_minimal_live_dependency_set(
      policy_snapshot,
      kCompositionOwner,
      options);
  assert_true(composition.ok(),
              "runtime query encoder integration should compose dependencies: " +
                  composition.error);
  assert_true(composition.dependency_set->knowledge_service != nullptr,
              "runtime query encoder integration should expose a knowledge service");

  return ComposedKnowledgeRuntime{
      .knowledge_service = composition.dependency_set->knowledge_service,
      .vector_store = vector_store_ptr,
  };
}

void runtime_query_encoder_integration_uses_provider_encoder_when_provider_is_ready() {
  const ScopedEnvironmentVariable local_fallback_disabled(
      "DASALL_DETACHED_VECTOR_LOCAL_FALLBACK",
      std::nullopt);
  const auto policy_snapshot = load_runtime_policy_snapshot("desktop_full");
  const TempStateRoot assets_root("dasall-runtime-query-encoder-assets-provider-ready");
  const TempStateRoot state_root("dasall-runtime-query-encoder-state-provider-ready");
  copy_installed_runtime_assets(assets_root.path());
  write_secret_fixture(state_root.path(),
                       "llm/providers/deepseek-prod",
                       "ready-token");

  auto transport = std::make_shared<StubEmbeddingTransport>(
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

  auto composed = compose_runtime(policy_snapshot,
                                  assets_root.path(),
                                  state_root.path(),
                                  ComposeRuntimeOptions{
                                      .query_encoder_transport = transport,
                                  });
  const auto transport_calls_before_retrieve = transport->send_calls_;
  if (composed.vector_store != nullptr) {
    composed.vector_store->last_request_.reset();
  }
  const auto result = composed.knowledge_service->retrieve(
      make_allowlisted_hybrid_query("provider-ready"));

  assert_true(result.ok,
              "runtime query encoder integration should keep allowlisted hybrid query successful when the provider encoder is ready");
  assert_true(result.mode == dasall::knowledge::RetrievalMode::Hybrid,
              "runtime query encoder integration should admit allowlisted hybrid query when the provider encoder is ready");
  assert_true(contains_reason_code(result.reason_codes, "runtime_canary_admitted"),
              "runtime query encoder integration should record admitted canary on provider-ready path");
  assert_true(composed.vector_store != nullptr && composed.vector_store->last_request_.has_value(),
              "runtime query encoder integration should drive the embedding-required vector store when the provider encoder is ready");
  assert_equal(3, static_cast<int>(composed.vector_store->last_request_->query_embedding.size()),
               "runtime query encoder integration should attach the provider embedding to the dense request");
  assert_true(transport_calls_before_retrieve > 0,
              "runtime query encoder integration should warm the provider encoder during the installed hybrid canary probe");
  assert_equal(transport_calls_before_retrieve,
               transport->send_calls_,
               "runtime query encoder integration should reuse the cached provider embedding for the first explicit retrieve after startup probe");
  assert_true(transport->last_request_.has_value() &&
                  header_value(*transport->last_request_, "Authorization") ==
                      std::optional<std::string>("Bearer ready-token"),
              "runtime query encoder integration should materialize the provider auth_ref into the outgoing transport headers");
}

void runtime_query_encoder_integration_falls_back_when_provider_is_missing() {
  const ScopedEnvironmentVariable local_fallback_disabled(
      "DASALL_DETACHED_VECTOR_LOCAL_FALLBACK",
      std::nullopt);
  const auto policy_snapshot = load_runtime_policy_snapshot("desktop_full");
  const TempStateRoot assets_root("dasall-runtime-query-encoder-assets-provider-missing");
  const TempStateRoot state_root("dasall-runtime-query-encoder-state-provider-missing");
  copy_installed_runtime_assets(assets_root.path());
  remove_embedding_feature_note(assets_root.path());

  auto transport = std::make_shared<StubEmbeddingTransport>(
      [](const dasall::llm::LLMTransportRequest&) {
        return dasall::llm::LLMTransportResponse{
            .status_code = 500U,
            .body = "{\"error\":\"should-not-be-called\"}",
            .error_message = {},
        };
      });

  auto composed = compose_runtime(policy_snapshot,
                                  assets_root.path(),
                                  state_root.path(),
                                  ComposeRuntimeOptions{
                                      .query_encoder_transport = transport,
                                  });
  const auto result = composed.knowledge_service->retrieve(
      make_allowlisted_hybrid_query("provider-missing"));

  assert_true(result.ok,
              "runtime query encoder integration should keep lexical fallback successful when the provider asset is missing");
  assert_true(result.mode == dasall::knowledge::RetrievalMode::LexicalOnly,
              "runtime query encoder integration should fall back to lexical-only when the provider asset is missing");
  assert_true(contains_reason_code(result.reason_codes, "runtime_canary_backend_not_ready"),
              "runtime query encoder integration should explain provider-missing fallback as backend-not-ready");
  assert_true(contains_reason_code(result.reason_codes, "query_encoder_provider_missing"),
              "runtime query encoder integration should surface query_encoder_provider_missing when no production embedding provider is selected");
  assert_true(composed.vector_store != nullptr && !composed.vector_store->last_request_.has_value(),
              "runtime query encoder integration should not call the embedding-required vector store when the provider asset is missing");
  assert_equal(0, transport->send_calls_,
               "runtime query encoder integration should not call the transport when no provider embedding model is selected");
}

void runtime_query_encoder_integration_falls_back_when_provider_returns_empty_embedding() {
  const ScopedEnvironmentVariable local_fallback_disabled(
      "DASALL_DETACHED_VECTOR_LOCAL_FALLBACK",
      std::nullopt);
  const auto policy_snapshot = load_runtime_policy_snapshot("desktop_full");
  const TempStateRoot assets_root("dasall-runtime-query-encoder-assets-empty-embedding");
  const TempStateRoot state_root("dasall-runtime-query-encoder-state-empty-embedding");
  copy_installed_runtime_assets(assets_root.path());
  write_secret_fixture(state_root.path(),
                       "llm/providers/deepseek-prod",
                       "ready-token");

  auto transport = std::make_shared<StubEmbeddingTransport>(
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
            .body = "{\"data\":[{\"embedding\":[],\"index\":0}],\"model\":\"deepseek-embedding\"}",
            .error_message = {},
        };
      });

  auto composed = compose_runtime(policy_snapshot,
                                  assets_root.path(),
                                  state_root.path(),
                                  ComposeRuntimeOptions{
                                      .query_encoder_transport = transport,
                                  });
  const auto transport_calls_before_retrieve = transport->send_calls_;
  const auto result = composed.knowledge_service->retrieve(
      make_allowlisted_hybrid_query("empty-embedding"));

  assert_true(result.ok,
              "runtime query encoder integration should keep lexical fallback successful when the provider returns an empty embedding");
  assert_true(result.mode == dasall::knowledge::RetrievalMode::LexicalOnly,
              "runtime query encoder integration should fall back to lexical-only when the provider returns an empty embedding");
  assert_true(contains_reason_code(result.reason_codes, "runtime_canary_backend_not_ready"),
              "runtime query encoder integration should record backend-not-ready when the provider returns an empty embedding");
  assert_true(contains_reason_code(result.reason_codes, "query_encoder_empty_embedding"),
              "runtime query encoder integration should surface query_encoder_empty_embedding on provider empty-embedding fallback");
  assert_true(!result.vector_backend_ready,
              "runtime query encoder integration should mark vector_backend_ready false after an empty provider embedding response");
  assert_true(composed.vector_store != nullptr && !composed.vector_store->last_request_.has_value(),
              "runtime query encoder integration should not drive the dense vector store when the provider returns an empty embedding");
  assert_equal(transport_calls_before_retrieve,
               transport->send_calls_,
               "runtime query encoder integration should reuse the failed startup probe state instead of retrying the same empty-embedding canary immediately");
}

void runtime_query_encoder_integration_falls_back_when_secret_is_invalid() {
  const ScopedEnvironmentVariable local_fallback_disabled(
      "DASALL_DETACHED_VECTOR_LOCAL_FALLBACK",
      std::nullopt);
  const auto policy_snapshot = load_runtime_policy_snapshot("desktop_full");
  const TempStateRoot assets_root("dasall-runtime-query-encoder-assets-invalid-secret");
  const TempStateRoot state_root("dasall-runtime-query-encoder-state-invalid-secret");
  copy_installed_runtime_assets(assets_root.path());
  write_secret_fixture(state_root.path(),
                       "llm/providers/deepseek-prod",
                       "bad-token");

  auto transport = std::make_shared<StubEmbeddingTransport>(
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

  auto composed = compose_runtime(policy_snapshot,
                                  assets_root.path(),
                                  state_root.path(),
                                  ComposeRuntimeOptions{
                                      .query_encoder_transport = transport,
                                  });
  const auto transport_calls_before_retrieve = transport->send_calls_;
  const auto result = composed.knowledge_service->retrieve(
      make_allowlisted_hybrid_query("invalid-secret"));

  assert_true(result.ok,
              "runtime query encoder integration should keep lexical fallback successful when the provider secret is invalid");
  assert_true(result.mode == dasall::knowledge::RetrievalMode::LexicalOnly,
              "runtime query encoder integration should fall back to lexical-only when the provider secret is invalid");
  assert_true(contains_reason_code(result.reason_codes, "runtime_canary_backend_not_ready"),
              "runtime query encoder integration should record backend-not-ready when the provider secret is invalid");
  assert_true(contains_reason_code(result.reason_codes, "query_encoder_invalid_secret"),
              "runtime query encoder integration should surface query_encoder_invalid_secret on invalid-secret fallback");
  assert_true(!result.vector_backend_ready,
              "runtime query encoder integration should mark vector_backend_ready false after an invalid-secret response");
  assert_true(composed.vector_store != nullptr && !composed.vector_store->last_request_.has_value(),
              "runtime query encoder integration should not drive the dense vector store when the provider secret is invalid");
  assert_equal(transport_calls_before_retrieve,
               transport->send_calls_,
               "runtime query encoder integration should reuse the failed startup probe state instead of retrying the same invalid-secret canary immediately");
}

void runtime_query_encoder_integration_uses_local_fallback_encoder_when_enabled() {
  const ScopedEnvironmentVariable local_fallback_enabled(
      "DASALL_DETACHED_VECTOR_LOCAL_FALLBACK",
      std::string("1"));
  const auto policy_snapshot = load_runtime_policy_snapshot("desktop_full");
  const TempStateRoot assets_root("dasall-runtime-query-encoder-assets-local-fallback");
  const TempStateRoot state_root("dasall-runtime-query-encoder-state-local-fallback");
  copy_installed_runtime_assets(assets_root.path());

  auto transport = std::make_shared<StubEmbeddingTransport>(
      [](const dasall::llm::LLMTransportRequest&) {
        return dasall::llm::LLMTransportResponse{
            .status_code = 500U,
            .body = "{\"error\":\"should-not-be-called\"}",
            .error_message = {},
        };
      });

  auto composed = compose_runtime(policy_snapshot,
                                  assets_root.path(),
                                  state_root.path(),
                                  ComposeRuntimeOptions{
                                      .query_encoder_transport = transport,
                                  });
  if (composed.vector_store != nullptr) {
    composed.vector_store->last_request_.reset();
  }
  const auto result = composed.knowledge_service->retrieve(
      make_allowlisted_hybrid_query("local-fallback"));

  assert_true(result.ok,
              "runtime query encoder integration should keep local fallback encoder canary successful");
  assert_true(result.mode == dasall::knowledge::RetrievalMode::Hybrid,
              "runtime query encoder integration should admit hybrid when local fallback encoder is explicitly enabled");
  assert_true(contains_reason_code(result.reason_codes, "runtime_canary_admitted"),
              "runtime query encoder integration should record admitted canary on local fallback encoder path");
  assert_true(composed.vector_store != nullptr && composed.vector_store->last_request_.has_value(),
              "runtime query encoder integration should drive the embedding-required vector store with local fallback encoder");
  assert_true(!composed.vector_store->last_request_->query_embedding.empty(),
              "runtime query encoder integration should attach a non-empty local fallback query embedding");
  assert_equal(0, transport->send_calls_,
               "runtime query encoder integration should not call the provider transport when the explicit local fallback env is enabled");
}

}  // namespace

int main() {
  try {
    runtime_query_encoder_integration_uses_provider_encoder_when_provider_is_ready();
    runtime_query_encoder_integration_falls_back_when_provider_is_missing();
    runtime_query_encoder_integration_falls_back_when_provider_returns_empty_embedding();
    runtime_query_encoder_integration_falls_back_when_secret_is_invalid();
    runtime_query_encoder_integration_uses_local_fallback_encoder_when_enabled();
  } catch (const std::exception& ex) {
    std::cerr << "[RuntimeKnowledgeQueryEncoderIntegrationTest] FAILED: "
              << ex.what() << '\n';
    return 1;
  }

  return 0;
}