#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>

#include "IKnowledgeService.h"
#include "KnowledgeServiceFactory.h"
#include "KnowledgeTypes.h"
#include "support/TestAssertions.h"

#ifndef DASALL_KNOWLEDGE_MODULE_CMAKE
#define DASALL_KNOWLEDGE_MODULE_CMAKE "/home/gangan/DASALL/knowledge/CMakeLists.txt"
#endif

namespace {

namespace fs = std::filesystem;

using dasall::knowledge::CorpusChangeSet;
using dasall::knowledge::InstalledAssetKnowledgeServiceOptions;
using dasall::knowledge::KnowledgeQuery;
using dasall::knowledge::KnowledgeQueryKind;
using dasall::knowledge::RefreshStatus;
using dasall::tests::support::assert_equal;
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
         ("dasall-index-startup-recovery-" + std::to_string(nonce));
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
  fs::copy(adr_source, assets_root / "docs" / "adr", fs::copy_options::recursive);
  fs::copy(ssot_source, assets_root / "docs" / "ssot", fs::copy_options::recursive);
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

void wait_for_refresh_completion(
    const std::shared_ptr<dasall::knowledge::IKnowledgeService>& service,
    std::string_view label) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    const auto health = service->health_snapshot();
    if (!health.refresh_in_flight && health.last_refresh_status.has_value()) {
      assert_true(*health.last_refresh_status == RefreshStatus::Completed,
                  std::string(label) +
                      ": refresh should eventually complete successfully");
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  throw std::runtime_error(std::string(label) +
                           ": timed out waiting for async refresh completion");
}

[[nodiscard]] KnowledgeQuery make_probe_query() {
  KnowledgeQuery query;
  query.request_id = "req-startup-recovery-probe";
  query.query_text = "DeepSeek Chat";
  query.query_kind = KnowledgeQueryKind::FactLookup;
  query.top_k = 3U;
  query.max_context_projection_items = 3U;
  return query;
}

[[nodiscard]] std::string slurp_file(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void startup_recovery_restores_last_known_good_snapshot_after_active_corruption() {
  ScopedTempDir temp_root{make_temp_root()};
  const auto assets_root = temp_root.path / "assets";
  const auto state_root = temp_root.path / "state";

  copy_fixture_assets(repo_root(), assets_root);
  fs::create_directories(state_root);

  std::string first_snapshot_id;
  std::string second_snapshot_id;
  {
    const auto factory_result = dasall::knowledge::create_installed_asset_knowledge_service(
        InstalledAssetKnowledgeServiceOptions{
            .readonly_assets_root = assets_root,
            .state_root = state_root,
            .service_instance_id = "knowledge-startup-recovery-fixture",
        });
    assert_true(factory_result.service != nullptr,
                "startup recovery fixture factory failed: " + factory_result.error);

    const auto initial_health = factory_result.service->health_snapshot();
    assert_true(!initial_health.active_snapshot_id.empty(),
                "fixture startup prewarm should publish an initial active snapshot");
    first_snapshot_id = initial_health.active_snapshot_id;

    const auto refresh_result = factory_result.service->request_refresh(CorpusChangeSet{});
    assert_true(refresh_result.status == RefreshStatus::Accepted,
                "fixture refresh should be accepted: " +
                    format_error(refresh_result.error));
    wait_for_refresh_completion(factory_result.service, "startup recovery fixture refresh");

    const auto refreshed_health = factory_result.service->health_snapshot();
    second_snapshot_id = refreshed_health.active_snapshot_id;
    assert_true(!second_snapshot_id.empty(),
                "fixture refresh should publish a second active snapshot");
    assert_true(second_snapshot_id != first_snapshot_id,
                "fixture refresh should rotate the active snapshot to create an LKG candidate");

    const auto retrieve_result = factory_result.service->retrieve(make_probe_query());
    assert_true(retrieve_result.ok,
                "fixture retrieve should succeed before corruption: " +
                    format_error(retrieve_result.error));
    assert_true(retrieve_result.evidence.has_value() &&
                    !retrieve_result.evidence->slices.empty(),
                "fixture retrieve should expose evidence before corruption");
  }

  const auto corrupted_database =
      state_root / "knowledge" / "snapshots" / second_snapshot_id / "lexical.sqlite";
  assert_true(fs::exists(corrupted_database),
              "corrupted snapshot database missing: " + corrupted_database.string());
  fs::remove(corrupted_database);

  const auto restarted_factory_result =
      dasall::knowledge::create_installed_asset_knowledge_service(
          InstalledAssetKnowledgeServiceOptions{
              .readonly_assets_root = assets_root,
              .state_root = state_root,
              .service_instance_id = "knowledge-startup-recovery-restore",
          });
  assert_true(restarted_factory_result.service != nullptr,
              "startup recovery restore factory failed: " +
                  restarted_factory_result.error);

  const auto recovered_health = restarted_factory_result.service->health_snapshot();
  assert_equal(first_snapshot_id, recovered_health.active_snapshot_id,
               "startup recovery should fall back to the persisted last-known-good snapshot");
  assert_true(recovered_health.last_known_good_available,
              "startup recovery should continue advertising last-known-good availability");

  const auto recovered_retrieve_result =
      restarted_factory_result.service->retrieve(make_probe_query());
  assert_true(recovered_retrieve_result.ok,
              "startup recovery retrieve should succeed after active corruption: " +
                  format_error(recovered_retrieve_result.error));
  assert_true(recovered_retrieve_result.evidence.has_value() &&
                  !recovered_retrieve_result.evidence->slices.empty(),
              "startup recovery retrieve should return evidence from the restored LKG snapshot");

  const auto catalog_payload = slurp_file(state_root / "knowledge" / "corpus_catalog.json");
  assert_true(catalog_payload.find(first_snapshot_id) != std::string::npos,
              "catalog persistence should be aligned to the recovered LKG snapshot id");
  assert_true(catalog_payload.find(second_snapshot_id) == std::string::npos,
              "catalog persistence should stop advertising the corrupted active snapshot id");
}

}  // namespace

int main() {
  try {
    startup_recovery_restores_last_known_good_snapshot_after_active_corruption();
  } catch (const std::exception& exception) {
    std::cerr << "[IndexStartupRecoveryTest] FAILED: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}