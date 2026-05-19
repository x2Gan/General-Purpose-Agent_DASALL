#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>

#include "IKnowledgeService.h"
#include "KnowledgeServiceFactory.h"
#include "KnowledgeTypes.h"
#include "support/TestAssertions.h"

#ifndef DASALL_KNOWLEDGE_MODULE_CMAKE
#define DASALL_KNOWLEDGE_MODULE_CMAKE "/home/gangan/DASALL/knowledge/CMakeLists.txt"
#endif

namespace {

namespace fs = std::filesystem;

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

struct NormativeQueryExpectation {
  std::string corpus_id;
  std::string request_id;
  std::string query_text;
  std::string expected_citation_fragment;
  KnowledgeQueryKind query_kind = KnowledgeQueryKind::PolicyEvidence;
};

[[nodiscard]] fs::path repo_root() {
  return fs::path(DASALL_KNOWLEDGE_MODULE_CMAKE).parent_path().parent_path();
}

[[nodiscard]] fs::path make_temp_root() {
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  return fs::temp_directory_path() /
         ("dasall-installed-knowledge-normative-" + std::to_string(nonce));
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

[[nodiscard]] KnowledgeQuery make_query(const NormativeQueryExpectation& expectation) {
  KnowledgeQuery query;
  query.request_id = expectation.request_id;
  query.query_text = expectation.query_text;
  query.query_kind = expectation.query_kind;
  query.allowed_corpora = {expectation.corpus_id};
  query.top_k = 4U;
  query.max_context_projection_items = 4U;
  return query;
}

[[nodiscard]] std::string describe_slices(
    const std::vector<dasall::knowledge::EvidenceSlice>& slices) {
  std::ostringstream stream;
  for (std::size_t index = 0; index < slices.size(); ++index) {
    if (index > 0U) {
      stream << " | ";
    }
    stream << slices[index].evidence_id << " @ " << slices[index].citation_ref;
  }
  return stream.str();
}

void installed_asset_service_routes_normative_queries() {
  ScopedTempDir temp_root{make_temp_root()};
  const auto assets_root = temp_root.path / "assets";
  const auto state_root = temp_root.path / "state";

  copy_fixture_assets(repo_root(), assets_root);
  fs::create_directories(state_root);

  const auto factory_result = dasall::knowledge::create_installed_asset_knowledge_service(
      InstalledAssetKnowledgeServiceOptions{
          .readonly_assets_root = assets_root,
          .state_root = state_root,
          .service_instance_id = "knowledge-installed-normative-corpus",
      });
  assert_true(factory_result.service != nullptr,
              "installed normative knowledge factory failed: " + factory_result.error);

  const auto health = factory_result.service->health_snapshot();
  assert_true(!health.refresh_in_flight,
              "installed normative init prewarm should settle before the factory returns");
  assert_true(health.freshness_state == FreshnessState::Fresh,
              "installed normative init prewarm should produce a fresh snapshot");
  assert_true(health.last_refresh_status.has_value() &&
                  *health.last_refresh_status == RefreshStatus::Completed,
              "installed normative init prewarm should publish a completed terminal status");

  const std::vector<NormativeQueryExpectation> expectations = {
      {"architecture_reference",
       "req-installed-architecture-reference",
       "Build Profile",
        "docs/architecture/DASALL_Engineering_Blueprint.md",
        KnowledgeQueryKind::FactLookup},
      {"adr_normative",
       "req-installed-adr-normative",
       "ContextOrchestrator PromptComposer",
       "docs/adr/ADR-006-context-orchestrator-vs-prompt-composer.md"},
      {"ssot_normative",
       "req-installed-ssot-normative",
       "BusinessChainIntegrationMatrix",
       "docs/ssot/BusinessChainIntegrationMatrix.md"},
      {"profile_policy_normative",
       "req-installed-profile-policy",
       "linux-arm64-embedded",
       "profiles/edge_minimal/runtime_policy.yaml"},
  };

  for (const auto& expectation : expectations) {
    const auto retrieve_result = factory_result.service->retrieve(make_query(expectation));
    assert_true(retrieve_result.ok,
                "installed normative retrieve failed for corpus " + expectation.corpus_id + ": " +
                    format_error(retrieve_result.error));
    assert_true(retrieve_result.evidence.has_value() &&
                    !retrieve_result.evidence->slices.empty(),
                "installed normative retrieve should return evidence for corpus " +
                    expectation.corpus_id);

    const auto matching_slice = std::find_if(
        retrieve_result.evidence->slices.begin(),
        retrieve_result.evidence->slices.end(),
        [&expectation](const auto& slice) {
          return slice.evidence_id.rfind(expectation.corpus_id + "/", 0) == 0U &&
                 slice.citation_ref.find(expectation.expected_citation_fragment) !=
                     std::string::npos;
        });
    assert_true(matching_slice != retrieve_result.evidence->slices.end(),
                "installed normative retrieve should route query to corpus " +
                    expectation.corpus_id + ", actual_slices=" +
                    describe_slices(retrieve_result.evidence->slices));
  }
}

}  // namespace

int main() {
  try {
    installed_asset_service_routes_normative_queries();
  } catch (const std::exception& exception) {
    std::cerr << "[KnowledgeInstalledNormativeCorpusTest] FAILED: "
              << exception.what() << '\n';
    return 1;
  }

  return 0;
}