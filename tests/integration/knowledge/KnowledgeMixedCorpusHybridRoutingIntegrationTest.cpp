#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "IKnowledgeService.h"
#include "KnowledgeServiceFactory.h"
#include "KnowledgeTypes.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

using dasall::knowledge::InstalledAssetKnowledgeServiceOptions;
using dasall::knowledge::KnowledgeQuery;
using dasall::knowledge::KnowledgeQueryKind;
using dasall::knowledge::RefreshStatus;
using dasall::knowledge::RetrievalMode;
using dasall::knowledge::index::DenseSnapshotBuildRequest;
using dasall::knowledge::index::DenseSnapshotBuildResult;
using dasall::knowledge::retrieve::DenseQueryInputMode;
using dasall::knowledge::retrieve::DenseQueryRequest;
using dasall::knowledge::retrieve::IVectorRecallStore;
using dasall::knowledge::retrieve::RecallHit;
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
         ("dasall-mixed-corpus-hybrid-routing-" + std::to_string(nonce));
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
  fs::copy(profiles_source, assets_root / "profiles", fs::copy_options::recursive);
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

[[nodiscard]] bool contains_reason_code(const std::vector<std::string>& reason_codes,
                                        std::string_view expected_code) {
  return std::find(reason_codes.begin(), reason_codes.end(), expected_code) !=
         reason_codes.end();
}

class FakeHybridVectorRecallStore final : public IVectorRecallStore {
 public:
  [[nodiscard]] bool available() const override {
    return true;
  }

  [[nodiscard]] DenseQueryInputMode query_input_mode() const override {
    return DenseQueryInputMode::TextOnly;
  }

  [[nodiscard]] std::vector<RecallHit> search(
      const DenseQueryRequest& request) const override {
    if (request.query_text.find("ContextOrchestrator") == std::string::npos) {
      return {};
    }

    if (std::find(request.allowed_corpus_ids.begin(),
                  request.allowed_corpus_ids.end(),
                  std::string("adr_normative")) == request.allowed_corpus_ids.end()) {
      return {};
    }

    if (std::find(request.allowed_corpus_ids.begin(),
                  request.allowed_corpus_ids.end(),
                  std::string("profile_policy_normative")) != request.allowed_corpus_ids.end()) {
      return {};
    }

    RecallHit hit;
    hit.corpus_id = "adr_normative";
    hit.document_id = "docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md";
    hit.chunk_id = "adr_normative/mixed-hybrid-001";
    hit.score = 0.1F;
    hit.raw_snippet = "ContextOrchestrator 与 PromptComposer 的职责边界由 ADR-006 冻结。";
    hit.citation_ref = "docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md";
    hit.updated_at = 1;
    hit.authority_level = dasall::knowledge::AuthorityLevel::Normative;
    hit.tags = {"adr", "normative"};
    return {hit};
  }
};

[[nodiscard]] DenseSnapshotBuildResult build_fake_dense_snapshot(
    const DenseSnapshotBuildRequest&) {
  return DenseSnapshotBuildResult{.ok = true, .warnings = {}};
}

[[nodiscard]] KnowledgeQuery make_mixed_scope_query() {
  KnowledgeQuery query;
  query.request_id = "req-mixed-corpus-hybrid-scope";
  query.query_text = "ContextOrchestrator PromptComposer";
  query.preferred_mode = RetrievalMode::Hybrid;
  query.query_kind = KnowledgeQueryKind::PolicyEvidence;
  query.allowed_corpora = {"adr_normative", "profile_policy_normative"};
  query.top_k = 4U;
  query.max_context_projection_items = 4U;
  return query;
}

[[nodiscard]] std::shared_ptr<dasall::knowledge::IKnowledgeService> make_service(
    const fs::path& assets_root,
    const fs::path& state_root,
    const std::vector<std::string>& runtime_canary_allowed_corpora) {
  const auto factory_result = dasall::knowledge::create_installed_asset_knowledge_service(
      InstalledAssetKnowledgeServiceOptions{
          .readonly_assets_root = assets_root,
          .state_root = state_root,
          .service_instance_id = "knowledge-mixed-corpus-hybrid-routing",
          .profile_id = "desktop_full",
          .telemetry_sinks = {},
          .build_dense_snapshot = build_fake_dense_snapshot,
          .create_vector_recall_store = [](const dasall::knowledge::DenseStoreFactoryContext&) {
            return std::make_unique<FakeHybridVectorRecallStore>();
          },
          .create_query_encoder = {},
          .runtime_canary_allowed_corpora = runtime_canary_allowed_corpora,
      });
  assert_true(factory_result.ok(),
              "mixed-corpus hybrid routing factory failed: " + factory_result.error);
  return factory_result.service;
}

void installed_asset_service_narrows_mixed_scope_to_allowlisted_hybrid_subset() {
  ScopedTempDir temp_root{make_temp_root()};
  const auto assets_root = temp_root.path / "assets";
  const auto state_root = temp_root.path / "state";

  copy_fixture_assets(repo_root(), assets_root);
  fs::create_directories(state_root);

  const auto service = make_service(assets_root,
                                    state_root,
                                    {"architecture_reference",
                                     "adr_normative",
                                     "ssot_normative"});

  const auto health = service->health_snapshot();
  assert_true(health.last_refresh_status.has_value() &&
                  *health.last_refresh_status == dasall::knowledge::RefreshStatus::Completed,
              "mixed-corpus hybrid routing service should finish startup refresh before retrieve");

  const auto retrieve_result = service->retrieve(make_mixed_scope_query());
  assert_true(retrieve_result.ok,
              "mixed-corpus hybrid routing query failed: " +
                  format_error(retrieve_result.error));
  assert_true(retrieve_result.mode == RetrievalMode::Hybrid,
              "mixed-corpus hybrid routing should keep hybrid after narrowing to allowlisted subset");
  assert_true(contains_reason_code(retrieve_result.reason_codes,
                                   "runtime_canary_admitted"),
              "mixed-corpus hybrid routing should record runtime_canary_admitted");
  assert_true(contains_reason_code(retrieve_result.reason_codes,
                                   "runtime_canary_scope_narrowed"),
              "mixed-corpus hybrid routing should record runtime_canary_scope_narrowed");
  assert_true(!contains_reason_code(retrieve_result.reason_codes,
                                    "runtime_canary_allowlist_miss"),
              "mixed-corpus hybrid routing should not report allowlist miss when an allowlisted subset exists");
  assert_true(retrieve_result.evidence.has_value() &&
                  !retrieve_result.evidence->slices.empty(),
              "mixed-corpus hybrid routing should still return evidence after narrowing");
  assert_true(std::all_of(retrieve_result.evidence->slices.begin(),
                          retrieve_result.evidence->slices.end(),
                          [](const dasall::knowledge::EvidenceSlice& slice) {
                            return slice.citation_ref.find("docs/adr/") != std::string::npos;
                          }),
              "mixed-corpus hybrid routing should keep evidence scoped to the allowlisted dense-capable corpus");
}

}  // namespace

int main() {
  try {
    installed_asset_service_narrows_mixed_scope_to_allowlisted_hybrid_subset();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}