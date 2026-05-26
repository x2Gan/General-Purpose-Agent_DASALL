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
#include "retrieve/IVectorRecallStore.h"
#include "RuntimeDependencySet.h"
#include "RuntimeLiveDependencyComposition.h"
#include "RuntimePolicyProvider.h"
#include "config/InstallLayout.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

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
              "runtime hybrid canary integration should load policy snapshot for " +
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

class FakeRuntimeVectorRecallStore final : public dasall::knowledge::retrieve::IVectorRecallStore {
 public:
  [[nodiscard]] bool available() const override {
    return true;
  }

  [[nodiscard]] dasall::knowledge::retrieve::DenseQueryInputMode query_input_mode() const override {
    return dasall::knowledge::retrieve::DenseQueryInputMode::TextOnly;
  }

  [[nodiscard]] std::vector<dasall::knowledge::retrieve::RecallHit> search(
      const dasall::knowledge::retrieve::DenseQueryRequest&) const override {
    return {};
  }
};

[[nodiscard]] dasall::knowledge::index::DenseSnapshotBuildResult build_fake_dense_snapshot(
    const dasall::knowledge::index::DenseSnapshotBuildRequest&) {
  return dasall::knowledge::index::DenseSnapshotBuildResult{.ok = true, .warnings = {}};
}

[[nodiscard]] dasall::knowledge::KnowledgeQuery make_allowlisted_hybrid_query(
    const std::string& profile_id) {
  dasall::knowledge::KnowledgeQuery query;
  query.request_id = "req-runtime-hybrid-positive-" + profile_id;
  query.query_text = "ContextOrchestrator PromptComposer";
  query.preferred_mode = dasall::knowledge::RetrievalMode::Hybrid;
  query.query_kind = dasall::knowledge::KnowledgeQueryKind::PolicyEvidence;
  query.allowed_corpora = {"adr_normative"};
  query.top_k = 4U;
  query.max_context_projection_items = 4U;
  return query;
}

[[nodiscard]] dasall::knowledge::KnowledgeQuery make_non_allowlisted_hybrid_query(
    const std::string& profile_id) {
  dasall::knowledge::KnowledgeQuery query;
  query.request_id = "req-runtime-hybrid-fallback-" + profile_id;
  query.query_text = "linux-arm64-embedded";
  query.preferred_mode = dasall::knowledge::RetrievalMode::Hybrid;
  query.query_kind = dasall::knowledge::KnowledgeQueryKind::PolicyEvidence;
  query.allowed_corpora = {"profile_policy_normative"};
  query.top_k = 4U;
  query.max_context_projection_items = 4U;
  return query;
}

[[nodiscard]] dasall::apps::runtime_support::RuntimeDependencyCompositionResult
compose_live_dependency_set(
    const std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>& policy_snapshot,
    const fs::path& readonly_assets_root,
    const fs::path& state_root) {
  return dasall::apps::runtime_support::compose_minimal_live_dependency_set(
      policy_snapshot,
      kCompositionOwner,
      dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions{
          .readonly_assets_root_override = readonly_assets_root,
          .runtime_library_root_override = {},
          .state_root_override = state_root,
          .build_dense_snapshot_override = build_fake_dense_snapshot,
          .create_vector_recall_store_override =
              [](const dasall::knowledge::DenseStoreFactoryContext&) {
                return std::make_unique<FakeRuntimeVectorRecallStore>();
              },
      });
}

void runtime_hybrid_canary_admits_allowlisted_queries_only() {
  for (const std::string profile_id : {"desktop_full", "cloud_full", "edge_balanced"}) {
    const auto policy_snapshot = load_runtime_policy_snapshot(profile_id);
    const TempStateRoot assets_root("dasall-runtime-hybrid-canary-assets");
    const TempStateRoot state_root("dasall-runtime-hybrid-canary-state");
    copy_installed_runtime_assets(assets_root.path());

    const auto composition = compose_live_dependency_set(policy_snapshot,
                                                         assets_root.path(),
                                                         state_root.path());
    assert_true(composition.ok(),
                "runtime hybrid canary integration should compose dependencies for " +
                    profile_id + ": " + composition.error);
    assert_true(composition.dependency_set->knowledge_service != nullptr,
                "runtime hybrid canary integration should expose a knowledge service for " +
                    profile_id);

    const auto positive_result = composition.dependency_set->knowledge_service->retrieve(
        make_allowlisted_hybrid_query(profile_id));
    assert_true(positive_result.ok,
                "runtime hybrid canary integration positive path failed for " + profile_id);
    assert_true(positive_result.mode == dasall::knowledge::RetrievalMode::Hybrid,
                "runtime hybrid canary integration should admit allowlisted hybrid query for " +
                    profile_id);
    assert_true(contains_reason_code(positive_result.reason_codes,
                                     "runtime_canary_admitted"),
                "runtime hybrid canary integration should record runtime_canary_admitted for " +
                    profile_id);
    assert_true(positive_result.evidence.has_value() &&
                    !positive_result.evidence->slices.empty(),
                "runtime hybrid canary integration should return evidence for allowlisted hybrid query on " +
                    profile_id);

    const auto fallback_result = composition.dependency_set->knowledge_service->retrieve(
        make_non_allowlisted_hybrid_query(profile_id));
    assert_true(fallback_result.ok,
                "runtime hybrid canary integration fallback path failed for " + profile_id);
    assert_true(fallback_result.mode == dasall::knowledge::RetrievalMode::LexicalOnly,
                "runtime hybrid canary integration should keep non-allowlisted corpus on lexical fallback for " +
                    profile_id);
    assert_true(contains_reason_code(fallback_result.reason_codes,
                                     "runtime_canary_allowlist_miss"),
                "runtime hybrid canary integration should record allowlist miss for non-allowlisted corpus on " +
                    profile_id);
    assert_true(fallback_result.evidence.has_value() &&
                    !fallback_result.evidence->slices.empty(),
                "runtime hybrid canary integration should still return lexical evidence on fallback path for " +
                    profile_id);
  }
}

}  // namespace

int main() {
  try {
    runtime_hybrid_canary_admits_allowlisted_queries_only();
  } catch (const std::exception& ex) {
    std::cerr << "[RuntimeKnowledgeHybridCanaryIntegrationTest] FAILED: "
              << ex.what() << '\n';
    return 1;
  }

  return 0;
}