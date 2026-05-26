#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "IKnowledgeService.h"
#include "KnowledgeTypes.h"
#include "index/IndexWriter.h"
#include "ProfileCatalog.h"
#include "retrieve/IQueryEncoder.h"
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

[[nodiscard]] bool contains_reason_code(const std::vector<std::string>& reason_codes,
                                        std::string_view expected_code) {
  return std::find(reason_codes.begin(), reason_codes.end(), expected_code) !=
         reason_codes.end();
}

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

class StaticQueryEncoder final : public dasall::knowledge::retrieve::IQueryEncoder {
 public:
  explicit StaticQueryEncoder(std::vector<float> embedding)
      : embedding_(std::move(embedding)) {}

  [[nodiscard]] std::vector<float> encode(std::string_view query_text) const override {
    last_query_text_ = std::string(query_text);
    ++encode_calls_;
    return embedding_;
  }

  [[nodiscard]] bool available() const override {
    return true;
  }

  mutable std::string last_query_text_;
  mutable int encode_calls_ = 0;

 private:
  std::vector<float> embedding_;
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
  StaticQueryEncoder* query_encoder = nullptr;
};

[[nodiscard]] ComposedKnowledgeRuntime compose_runtime(
    const std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>& policy_snapshot,
    const fs::path& readonly_assets_root,
    const fs::path& state_root,
    bool with_query_encoder) {
  auto vector_store = std::make_unique<RecordingEmbeddingRequiredVectorRecallStore>();
  auto* vector_store_ptr = vector_store.get();

  std::unique_ptr<StaticQueryEncoder> query_encoder;
  StaticQueryEncoder* query_encoder_ptr = nullptr;
  if (with_query_encoder) {
    query_encoder = std::make_unique<StaticQueryEncoder>(std::vector<float>{0.2F, 0.3F, 0.5F});
    query_encoder_ptr = query_encoder.get();
  }

  auto owned_store =
      std::make_shared<std::unique_ptr<RecordingEmbeddingRequiredVectorRecallStore>>(
          std::move(vector_store));
  auto owned_encoder =
      std::make_shared<std::unique_ptr<StaticQueryEncoder>>(std::move(query_encoder));

  const auto composition = dasall::apps::runtime_support::compose_minimal_live_dependency_set(
      policy_snapshot,
      kCompositionOwner,
      dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions{
          .readonly_assets_root_override = readonly_assets_root,
          .runtime_library_root_override = {},
          .state_root_override = state_root,
          .build_dense_snapshot_override = build_fake_dense_snapshot,
          .create_vector_recall_store_override =
              [owned_store](const dasall::knowledge::DenseStoreFactoryContext&) mutable {
                return std::move(*owned_store);
              },
          .create_query_encoder_override =
              [owned_encoder]() mutable -> std::unique_ptr<dasall::knowledge::retrieve::IQueryEncoder> {
                return std::move(*owned_encoder);
              },
      });
  assert_true(composition.ok(),
              "runtime query encoder integration should compose dependencies: " +
                  composition.error);
  assert_true(composition.dependency_set->knowledge_service != nullptr,
              "runtime query encoder integration should expose a knowledge service");

  return ComposedKnowledgeRuntime{
      .knowledge_service = composition.dependency_set->knowledge_service,
      .vector_store = vector_store_ptr,
      .query_encoder = query_encoder_ptr,
  };
}

void runtime_query_encoder_integration_allows_hybrid_when_encoder_is_ready() {
  const auto policy_snapshot = load_runtime_policy_snapshot("desktop_full");
  const TempStateRoot assets_root("dasall-runtime-query-encoder-assets-ready");
  const TempStateRoot state_root("dasall-runtime-query-encoder-state-ready");
  copy_installed_runtime_assets(assets_root.path());

  auto composed = compose_runtime(policy_snapshot,
                                  assets_root.path(),
                                  state_root.path(),
                                  true);
  const auto encode_calls_before_retrieve =
      composed.query_encoder == nullptr ? 0 : composed.query_encoder->encode_calls_;
  if (composed.vector_store != nullptr) {
    composed.vector_store->last_request_.reset();
  }
  const auto result = composed.knowledge_service->retrieve(
      make_allowlisted_hybrid_query("ready"));

  assert_true(result.ok,
              "runtime query encoder integration should keep allowlisted hybrid query successful when encoder is ready");
  assert_true(result.mode == dasall::knowledge::RetrievalMode::Hybrid,
              "runtime query encoder integration should admit allowlisted hybrid query when encoder is ready");
  assert_true(contains_reason_code(result.reason_codes, "runtime_canary_admitted"),
              "runtime query encoder integration should record admitted canary on encoder-ready path");
  assert_true(composed.vector_store != nullptr && composed.vector_store->last_request_.has_value(),
              "runtime query encoder integration should drive the embedding-required vector store when encoder is ready");
  assert_equal(3, static_cast<int>(composed.vector_store->last_request_->query_embedding.size()),
               "runtime query encoder integration should attach the encoded embedding to the dense request");
  assert_true(composed.query_encoder != nullptr &&
                  composed.query_encoder->encode_calls_ == encode_calls_before_retrieve + 1,
              "runtime query encoder integration should call the encoder once for the explicit positive retrieve after composition probe");
  assert_equal(std::string("contextorchestrator promptcomposer"),
               composed.query_encoder->last_query_text_,
               "runtime query encoder integration should pass normalized query text into the encoder");
}

void runtime_query_encoder_integration_falls_back_when_encoder_is_missing() {
  const auto policy_snapshot = load_runtime_policy_snapshot("desktop_full");
  const TempStateRoot assets_root("dasall-runtime-query-encoder-assets-fallback");
  const TempStateRoot state_root("dasall-runtime-query-encoder-state-fallback");
  copy_installed_runtime_assets(assets_root.path());

  auto composed = compose_runtime(policy_snapshot,
                                  assets_root.path(),
                                  state_root.path(),
                                  false);
  const auto result = composed.knowledge_service->retrieve(
      make_allowlisted_hybrid_query("fallback"));

  assert_true(result.ok,
              "runtime query encoder integration should keep lexical fallback successful when encoder is missing");
  assert_true(result.mode == dasall::knowledge::RetrievalMode::LexicalOnly,
              "runtime query encoder integration should fall back to lexical-only when encoder is missing");
  assert_true(contains_reason_code(result.reason_codes, "runtime_canary_backend_not_ready"),
              "runtime query encoder integration should explain encoder-missing fallback as backend-not-ready");
  assert_true(composed.vector_store != nullptr && !composed.vector_store->last_request_.has_value(),
              "runtime query encoder integration should not call the embedding-required vector store when encoder is missing");
}

}  // namespace

int main() {
  try {
    runtime_query_encoder_integration_allows_hybrid_when_encoder_is_ready();
    runtime_query_encoder_integration_falls_back_when_encoder_is_missing();
  } catch (const std::exception& ex) {
    std::cerr << "[RuntimeKnowledgeQueryEncoderIntegrationTest] FAILED: "
              << ex.what() << '\n';
    return 1;
  }

  return 0;
}