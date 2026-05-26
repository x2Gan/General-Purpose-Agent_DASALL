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

#include "AgentFacade.h"
#include "ITimer.h"
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

using dasall::tests::support::assert_true;

constexpr char kDefaultProfileId[] = "desktop_full";

class TempStateRoot {
 public:
  explicit TempStateRoot(const std::string& stem)
      : path_(std::filesystem::temp_directory_path() /
              (stem + "-" + std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count()))) {
    std::filesystem::create_directories(path_);
  }

  ~TempStateRoot() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  [[nodiscard]] const std::filesystem::path& path() const {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

struct CompositionOwnerSpec {
  std::string composition_owner;
  std::string runtime_instance_id;
  std::string boot_reason;
  dasall::contracts::RequestChannel request_channel;
};

[[nodiscard]] std::vector<CompositionOwnerSpec> owner_specs() {
  return {
      CompositionOwnerSpec{
          .composition_owner = "daemon.local-control-plane",
          .runtime_instance_id = "daemon.local-control-plane",
          .boot_reason = "daemon-local-control-plane",
          .request_channel = dasall::contracts::RequestChannel::Daemon,
      },
      CompositionOwnerSpec{
          .composition_owner = "gateway.http-unary",
          .runtime_instance_id = "gateway.http-unary",
          .boot_reason = "gateway-http-entry",
          .request_channel = dasall::contracts::RequestChannel::Gateway,
      },
  };
}

[[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>
load_runtime_policy_snapshot() {
  const auto install_layout = dasall::infra::config::resolve_install_layout();
  const dasall::profiles::ProfileCatalog catalog(install_layout.profiles_root);
  const dasall::profiles::RuntimePolicyProvider provider(catalog);
  const auto snapshot_result = provider.load_snapshot(
      dasall::profiles::RuntimePolicyLoadRequest{
          .profile_id = kDefaultProfileId,
      });
  assert_true(snapshot_result.ok() && snapshot_result.snapshot != nullptr,
              "runtime live composition failure matrix should load a runtime policy snapshot");
  return snapshot_result.snapshot;
}

[[nodiscard]] bool contains_port(const std::vector<std::string>& ports,
                                 const std::string& expected_port) {
  return std::find(ports.begin(), ports.end(), expected_port) != ports.end();
}

[[nodiscard]] bool contains_prefix(const std::vector<std::string>& values,
                                   const std::string& expected_prefix) {
  return std::any_of(values.begin(), values.end(),
                     [&expected_prefix](const std::string& value) {
                       return value.rfind(expected_prefix, 0) == 0;
                     });
}

class RecordingTimer final : public dasall::platform::ITimer {
 public:
  dasall::platform::PlatformResult<dasall::platform::TimerHandle> start_once(
      const dasall::platform::TimerSpec& spec,
      dasall::platform::TimerCallback callback) override {
    one_shot_specs.push_back(spec);
    one_shot_callbacks += callback ? 1 : 0;
    return dasall::platform::PlatformResult<dasall::platform::TimerHandle>::success(
        dasall::platform::TimerHandle{.native_id = 91U});
  }

  dasall::platform::PlatformResult<dasall::platform::TimerHandle> start_periodic(
      const dasall::platform::TimerSpec& spec,
      dasall::platform::TimerCallback callback) override {
    periodic_specs.push_back(spec);
    periodic_callbacks += callback ? 1 : 0;
    return dasall::platform::PlatformResult<dasall::platform::TimerHandle>::success(
        dasall::platform::TimerHandle{.native_id =
                                          static_cast<std::uint64_t>(40U + periodic_specs.size())});
  }

  dasall::platform::PlatformResult<dasall::platform::TimerCancelResult> cancel(
      const dasall::platform::TimerHandle& handle) override {
    cancelled_handles.push_back(handle.native_id);
    return dasall::platform::PlatformResult<dasall::platform::TimerCancelResult>::success(
        dasall::platform::TimerCancelResult{
            .cancelled = true,
            .drift_stats = {},
        });
  }

  std::vector<dasall::platform::TimerSpec> one_shot_specs;
  std::vector<dasall::platform::TimerSpec> periodic_specs;
  std::vector<std::uint64_t> cancelled_handles;
  int one_shot_callbacks = 0;
  int periodic_callbacks = 0;
};

[[nodiscard]] dasall::knowledge::retrieve::RecallHit make_runtime_dense_hit() {
  dasall::knowledge::retrieve::RecallHit hit;
  hit.corpus_id = "adr_normative";
  hit.document_id = "adr-006";
  hit.chunk_id = "adr-006#runtime-hybrid-canary";
  hit.score = 0.93F;
  hit.raw_snippet = "ContextOrchestrator PromptComposer owner boundary evidence.";
  hit.citation_ref = "ADR-006#context-owner";
  hit.updated_at = 1713657600000;
  hit.authority_level = dasall::knowledge::AuthorityLevel::Normative;
  hit.tags = {"normative", "architecture"};
  return hit;
}

class DenseHitRuntimeVectorRecallStore final : public dasall::knowledge::retrieve::IVectorRecallStore {
 public:
  [[nodiscard]] bool available() const override {
    return true;
  }

  [[nodiscard]] dasall::knowledge::retrieve::DenseQueryInputMode query_input_mode() const override {
    return dasall::knowledge::retrieve::DenseQueryInputMode::TextOnly;
  }

  [[nodiscard]] std::vector<dasall::knowledge::retrieve::RecallHit> search(
      const dasall::knowledge::retrieve::DenseQueryRequest&) const override {
    return {make_runtime_dense_hit()};
  }
};

class NoHitRuntimeVectorRecallStore final : public dasall::knowledge::retrieve::IVectorRecallStore {
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

class EmbeddingRequiredRuntimeVectorRecallStore final
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
    return {make_runtime_dense_hit()};
  }
};

class StaticQueryEncoder final : public dasall::knowledge::retrieve::IQueryEncoder {
 public:
  [[nodiscard]] std::vector<float> encode(std::string_view) const override {
    return {0.2F, 0.3F, 0.5F};
  }

  [[nodiscard]] bool available() const override {
    return true;
  }
};

[[nodiscard]] dasall::knowledge::index::DenseSnapshotBuildResult build_fake_dense_snapshot(
    const dasall::knowledge::index::DenseSnapshotBuildRequest&) {
  return dasall::knowledge::index::DenseSnapshotBuildResult{.ok = true, .warnings = {}};
}

[[nodiscard]] dasall::knowledge::KnowledgeQuery make_installed_knowledge_query() {
  dasall::knowledge::KnowledgeQuery query;
  query.request_id = "req-runtime-live-composition-knowledge-mode";
  query.query_text = "DeepSeek Chat";
  query.query_kind = dasall::knowledge::KnowledgeQueryKind::FactLookup;
  query.top_k = 3U;
  query.max_context_projection_items = 3U;
  return query;
}

void assert_installed_knowledge_service_stays_lexical_only(
    const std::shared_ptr<dasall::runtime::RuntimeDependencySet>& dependency_set,
  const CompositionOwnerSpec& spec,
  const std::filesystem::path& state_root) {
  assert_true(dependency_set->knowledge_service != nullptr,
              "runtime live composition matrix should expose a knowledge service for " +
                  spec.composition_owner);

  const auto retrieve_result = dependency_set->knowledge_service->retrieve(
      make_installed_knowledge_query());
  assert_true(retrieve_result.ok && retrieve_result.evidence.has_value() &&
                  !retrieve_result.evidence->slices.empty(),
              "runtime live composition matrix should keep installed knowledge retrieval available for " +
                  spec.composition_owner);
  assert_true(retrieve_result.mode == dasall::knowledge::RetrievalMode::LexicalOnly,
              "runtime live composition matrix should keep installed knowledge on lexical-only production mode for " +
                  spec.composition_owner);

  const auto health_snapshot = dependency_set->knowledge_service->health_snapshot();
    assert_true(!health_snapshot.active_snapshot_id.empty(),
          "runtime live composition matrix should expose the active knowledge snapshot id for " +
            spec.composition_owner);
}

void copy_memory_assets_only(const std::filesystem::path& assets_root) {
  std::filesystem::create_directories(assets_root / "sql");
  std::filesystem::copy(std::filesystem::path(DASALL_SOURCE_ROOT) / "sql" / "memory",
                        assets_root / "sql" / "memory",
                        std::filesystem::copy_options::recursive);
}

void copy_installed_runtime_assets(const std::filesystem::path& assets_root) {
  copy_memory_assets_only(assets_root);
  std::filesystem::copy(std::filesystem::path(DASALL_SOURCE_ROOT) / "profiles",
                        assets_root / "profiles",
                        std::filesystem::copy_options::recursive);
  std::filesystem::create_directories(assets_root / "llm");
  std::filesystem::copy(std::filesystem::path(DASALL_SOURCE_ROOT) / "llm" / "assets" /
                            "providers",
                        assets_root / "llm" / "providers",
                        std::filesystem::copy_options::recursive);
  std::filesystem::create_directories(assets_root / "docs");
  std::filesystem::copy(std::filesystem::path(DASALL_SOURCE_ROOT) / "docs" / "architecture",
                        assets_root / "docs" / "architecture",
                        std::filesystem::copy_options::recursive);
  std::filesystem::copy(std::filesystem::path(DASALL_SOURCE_ROOT) / "docs" / "adr",
                        assets_root / "docs" / "adr",
                        std::filesystem::copy_options::recursive);
  std::filesystem::copy(std::filesystem::path(DASALL_SOURCE_ROOT) / "docs" / "ssot",
                        assets_root / "docs" / "ssot",
                        std::filesystem::copy_options::recursive);
}

[[nodiscard]] dasall::runtime::AgentInitRequest make_init_request(
    const CompositionOwnerSpec& spec,
    const std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>& policy_snapshot,
    const std::shared_ptr<dasall::runtime::RuntimeDependencySet>& dependency_set) {
  return dasall::runtime::AgentInitRequest{
      .runtime_instance_id = spec.runtime_instance_id + ":" +
                             policy_snapshot->effective_profile_id(),
      .profile_id = policy_snapshot->effective_profile_id(),
      .policy_snapshot = policy_snapshot,
      .dependency_set = dependency_set,
      .boot_reason = spec.boot_reason,
      .cold_start = true,
  };
}

[[nodiscard]] dasall::apps::runtime_support::RuntimeDependencyCompositionResult
compose_live_dependency_set(
    const std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>& policy_snapshot,
    const CompositionOwnerSpec& spec,
    const std::filesystem::path& readonly_assets_root,
  const std::filesystem::path& state_root,
  std::shared_ptr<dasall::platform::ITimer> knowledge_refresh_timer) {
  return dasall::apps::runtime_support::compose_minimal_live_dependency_set(
      policy_snapshot,
      spec.composition_owner,
      dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions{
          .readonly_assets_root_override = readonly_assets_root,
          .runtime_library_root_override = {},
          .state_root_override = state_root,
          .build_dense_snapshot_override = build_fake_dense_snapshot,
          .create_vector_recall_store_override =
              [](const dasall::knowledge::DenseStoreFactoryContext&) {
                return std::make_unique<DenseHitRuntimeVectorRecallStore>();
              },
          .create_query_encoder_override = {},
          .knowledge_refresh_timer = std::move(knowledge_refresh_timer),
      });
}

void runtime_live_composition_keeps_ready_markers_stratified() {
  const auto policy_snapshot = load_runtime_policy_snapshot();
  for (const auto& spec : owner_specs()) {
    const TempStateRoot assets_root("dasall-runtime-live-matrix-assets-ready");
    const TempStateRoot state_root("dasall-runtime-live-matrix-state-ready");
    copy_installed_runtime_assets(assets_root.path());
    auto timer = std::make_shared<RecordingTimer>();

    const auto composition = compose_live_dependency_set(policy_snapshot,
                                                         spec,
                                                         assets_root.path(),
                                                         state_root.path(),
                                                         timer);
    assert_true(composition.ok(),
                "runtime live composition matrix should compose a ready baseline for " +
                    spec.composition_owner + ": " + composition.error);

    const auto& dependency_set = composition.dependency_set;
    const auto readiness = dependency_set->describe_readiness();
    assert_true(readiness.default_unary_ready(),
                "runtime live composition matrix should keep " +
                    spec.composition_owner + " in default-ready mode: " +
                    readiness.summary());

    const auto ready_marker =
        std::string("runtime:") + spec.composition_owner +
        ":knowledge-installed-assets-ready";
    const auto hybrid_canary_marker =
      std::string("runtime:") + spec.composition_owner +
      ":knowledge-hybrid-canary-ready";
    const auto automation_ready_marker =
      std::string("runtime:") + spec.composition_owner +
      ":knowledge-refresh-automation-ready";
    const auto automation_fallback_prefix =
      std::string("runtime:") + spec.composition_owner +
      ":knowledge-refresh-automation-fallback:";
    const auto degraded_prefix =
        std::string("runtime:") + spec.composition_owner + ":knowledge-degraded:";
    assert_true(contains_port(dependency_set->external_evidence, ready_marker),
                "runtime live composition matrix should keep the ready knowledge marker for " +
                    spec.composition_owner);
    assert_true(contains_port(dependency_set->external_evidence, hybrid_canary_marker),
        "runtime live composition matrix should expose the hybrid canary ready marker for " +
        spec.composition_owner);
    assert_true(contains_port(dependency_set->external_evidence, automation_ready_marker),
          "runtime live composition matrix should expose the automation ready marker for " +
            spec.composition_owner);
    assert_true(!contains_prefix(dependency_set->external_evidence, degraded_prefix),
                "runtime live composition matrix should not mix degraded knowledge markers into the ready baseline for " +
                    spec.composition_owner);
    assert_true(!contains_prefix(dependency_set->external_evidence, automation_fallback_prefix),
          "runtime live composition matrix should not mix automation fallback markers into the timer-ready baseline for " +
            spec.composition_owner);

    dasall::runtime::AgentFacade facade;
    const auto init_result =
        facade.init(make_init_request(spec, policy_snapshot, dependency_set));
    assert_true(init_result.accepted && init_result.default_ready(),
                "runtime live composition matrix should keep " +
                    spec.composition_owner + " default-ready after helper composition: " +
                    init_result.diagnostics);

    assert_installed_knowledge_service_stays_lexical_only(dependency_set,
                                                          spec,
                                                          state_root.path());

  }
}

void runtime_live_composition_emits_hybrid_marker_when_encoder_is_ready() {
  const auto policy_snapshot = load_runtime_policy_snapshot();
  for (const auto& spec : owner_specs()) {
    const TempStateRoot assets_root("dasall-runtime-live-matrix-assets-encoder-ready");
    const TempStateRoot state_root("dasall-runtime-live-matrix-state-encoder-ready");
    copy_installed_runtime_assets(assets_root.path());
    auto timer = std::make_shared<RecordingTimer>();

    const auto composition = dasall::apps::runtime_support::compose_minimal_live_dependency_set(
        policy_snapshot,
        spec.composition_owner,
        dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions{
            .readonly_assets_root_override = assets_root.path(),
            .runtime_library_root_override = {},
            .state_root_override = state_root.path(),
            .build_dense_snapshot_override = build_fake_dense_snapshot,
            .create_vector_recall_store_override =
                [](const dasall::knowledge::DenseStoreFactoryContext&) {
                  return std::make_unique<EmbeddingRequiredRuntimeVectorRecallStore>();
                },
            .create_query_encoder_override = [] {
              return std::make_unique<StaticQueryEncoder>();
            },
            .knowledge_refresh_timer = timer,
        });
    assert_true(composition.ok(),
                "runtime live composition matrix should compose an encoder-ready hybrid baseline for " +
                    spec.composition_owner + ": " + composition.error);

    const auto ready_marker =
        std::string("runtime:") + spec.composition_owner +
        ":knowledge-installed-assets-ready";
    const auto hybrid_canary_marker =
        std::string("runtime:") + spec.composition_owner +
        ":knowledge-hybrid-canary-ready";
    const auto automation_ready_marker =
      std::string("runtime:") + spec.composition_owner +
      ":knowledge-refresh-automation-ready";
    const auto automation_fallback_prefix =
      std::string("runtime:") + spec.composition_owner +
      ":knowledge-refresh-automation-fallback:";
    assert_true(contains_port(composition.dependency_set->external_evidence, ready_marker),
                "runtime live composition matrix should keep installed ready marker on encoder-ready path for " +
                    spec.composition_owner);
    assert_true(contains_port(composition.dependency_set->external_evidence, hybrid_canary_marker),
                "runtime live composition matrix should expose hybrid ready marker only when encoder-required path is ready for " +
                    spec.composition_owner);
    assert_true(contains_port(composition.dependency_set->external_evidence, automation_ready_marker) &&
            !contains_prefix(composition.dependency_set->external_evidence,
                     automation_fallback_prefix),
          "runtime live composition matrix should keep automation ready stratified on the encoder-ready path for " +
            spec.composition_owner);
  }
}

void runtime_live_composition_omits_hybrid_marker_when_dense_lane_has_no_hits() {
  const auto policy_snapshot = load_runtime_policy_snapshot();
  for (const auto& spec : owner_specs()) {
    const TempStateRoot assets_root("dasall-runtime-live-matrix-assets-dense-empty");
    const TempStateRoot state_root("dasall-runtime-live-matrix-state-dense-empty");
    copy_installed_runtime_assets(assets_root.path());
    auto timer = std::make_shared<RecordingTimer>();

    const auto composition = dasall::apps::runtime_support::compose_minimal_live_dependency_set(
        policy_snapshot,
        spec.composition_owner,
        dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions{
            .readonly_assets_root_override = assets_root.path(),
            .runtime_library_root_override = {},
            .state_root_override = state_root.path(),
            .build_dense_snapshot_override = build_fake_dense_snapshot,
            .create_vector_recall_store_override =
                [](const dasall::knowledge::DenseStoreFactoryContext&) {
                  return std::make_unique<NoHitRuntimeVectorRecallStore>();
                },
            .create_query_encoder_override = {},
            .knowledge_refresh_timer = timer,
        });
    assert_true(composition.ok(),
                "runtime live composition matrix should compose when the dense lane returns no canary hits for " +
                    spec.composition_owner + ": " + composition.error);

    const auto ready_marker =
        std::string("runtime:") + spec.composition_owner +
        ":knowledge-installed-assets-ready";
    const auto hybrid_canary_marker =
        std::string("runtime:") + spec.composition_owner +
        ":knowledge-hybrid-canary-ready";
    const auto automation_ready_marker =
      std::string("runtime:") + spec.composition_owner +
      ":knowledge-refresh-automation-ready";
    assert_true(contains_port(composition.dependency_set->external_evidence, ready_marker),
                "runtime live composition matrix should keep installed ready marker when dense lane has no canary hits for " +
                    spec.composition_owner);
    assert_true(!contains_port(composition.dependency_set->external_evidence, hybrid_canary_marker),
                "runtime live composition matrix should omit hybrid ready marker until the dense lane returns a canary hit for " +
                    spec.composition_owner);
    assert_true(contains_port(composition.dependency_set->external_evidence, automation_ready_marker),
                "runtime live composition matrix should keep automation ready marker independent from dense canary hits for " +
                    spec.composition_owner);
  }
}

void runtime_live_composition_fail_closes_when_required_ports_are_missing() {
  const auto policy_snapshot = load_runtime_policy_snapshot();
  for (const auto& spec : owner_specs()) {
    const TempStateRoot assets_root("dasall-runtime-live-matrix-assets-fail-closed");
    const TempStateRoot state_root("dasall-runtime-live-matrix-state-fail-closed");
    copy_installed_runtime_assets(assets_root.path());

    auto composition = compose_live_dependency_set(policy_snapshot,
                                                   spec,
                                                   assets_root.path(),
                                                   state_root.path(),
                                                   std::make_shared<RecordingTimer>());
    assert_true(composition.ok(),
                "runtime live composition matrix should compose the pre-fail-closed baseline for " +
                    spec.composition_owner + ": " + composition.error);

    composition.dependency_set->memory_manager.reset();

    dasall::runtime::AgentFacade facade;
    const auto init_result =
        facade.init(make_init_request(spec, policy_snapshot, composition.dependency_set));
    assert_true(!init_result.accepted,
                "runtime live composition matrix should reject missing required ports for " +
                    spec.composition_owner);
    assert_true(init_result.health_summary.find("missing required dependency ports") !=
                    std::string::npos,
                "runtime live composition matrix should explain the fail-closed reason for " +
                    spec.composition_owner + ": " + init_result.health_summary);
    assert_true(init_result.diagnostics.find("readiness=fail_closed") != std::string::npos &&
                    init_result.diagnostics.find("missing_required=memory") != std::string::npos,
                "runtime live composition matrix should project fail-closed readiness details for " +
                    spec.composition_owner + ": " + init_result.diagnostics);
    assert_true(!init_result.default_ready() && !init_result.degraded_ready() &&
                    !init_result.stub_ready(),
                "runtime live composition matrix should keep fail-closed init results out of all ready states for " +
                    spec.composition_owner);
  }
}

void runtime_live_composition_marks_degraded_runtime_when_knowledge_is_missing() {
  const auto policy_snapshot = load_runtime_policy_snapshot();
  for (const auto& spec : owner_specs()) {
    const TempStateRoot assets_root("dasall-runtime-live-matrix-assets-degraded");
    const TempStateRoot state_root("dasall-runtime-live-matrix-state-degraded");
    copy_memory_assets_only(assets_root.path());

    const auto composition = compose_live_dependency_set(policy_snapshot,
                                                         spec,
                                                         assets_root.path(),
                                                         state_root.path(),
                                                         std::make_shared<RecordingTimer>());
    assert_true(composition.ok(),
                "runtime live composition matrix should keep required ports available when knowledge is missing for " +
                    spec.composition_owner + ": " + composition.error);

    const auto& dependency_set = composition.dependency_set;
    const auto readiness = dependency_set->describe_readiness();
    const auto ready_marker =
        std::string("runtime:") + spec.composition_owner +
        ":knowledge-installed-assets-ready";
    const auto degraded_prefix =
        std::string("runtime:") + spec.composition_owner + ":knowledge-degraded:";
    assert_true(readiness.has_required_ports && readiness.degraded &&
                    contains_port(readiness.missing_optional_ports, "knowledge"),
                "runtime live composition matrix should surface a degraded optional knowledge gap for " +
                    spec.composition_owner + ": " + readiness.summary());
    assert_true(!contains_port(dependency_set->external_evidence, ready_marker) &&
                    contains_prefix(dependency_set->external_evidence, degraded_prefix),
                "runtime live composition matrix should stratify knowledge degraded markers for " +
                    spec.composition_owner);

    dasall::runtime::AgentFacade facade;
    const auto init_result =
        facade.init(make_init_request(spec, policy_snapshot, dependency_set));
    assert_true(init_result.accepted && init_result.degraded_ready(),
                "runtime live composition matrix should allow degraded-ready init when only knowledge is missing for " +
                    spec.composition_owner + ": " + init_result.diagnostics);
    assert_true(init_result.diagnostics.find("missing_optional=knowledge") !=
                    std::string::npos,
                "runtime live composition matrix should name the knowledge gap in diagnostics for " +
                    spec.composition_owner + ": " + init_result.diagnostics);
  }
}

void runtime_live_composition_omits_hybrid_marker_when_encoder_is_missing() {
  const auto policy_snapshot = load_runtime_policy_snapshot();
  for (const auto& spec : owner_specs()) {
    const TempStateRoot assets_root("dasall-runtime-live-matrix-assets-encoder-missing");
    const TempStateRoot state_root("dasall-runtime-live-matrix-state-encoder-missing");
    copy_installed_runtime_assets(assets_root.path());
    auto timer = std::make_shared<RecordingTimer>();

    const auto composition = dasall::apps::runtime_support::compose_minimal_live_dependency_set(
        policy_snapshot,
        spec.composition_owner,
        dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions{
            .readonly_assets_root_override = assets_root.path(),
            .runtime_library_root_override = {},
            .state_root_override = state_root.path(),
            .build_dense_snapshot_override = build_fake_dense_snapshot,
            .create_vector_recall_store_override =
                [](const dasall::knowledge::DenseStoreFactoryContext&) {
                  return std::make_unique<EmbeddingRequiredRuntimeVectorRecallStore>();
                },
            .create_query_encoder_override = {},
            .knowledge_refresh_timer = timer,
        });
    assert_true(composition.ok(),
                "runtime live composition matrix should still compose when query encoder is missing for " +
                    spec.composition_owner + ": " + composition.error);

    const auto ready_marker =
        std::string("runtime:") + spec.composition_owner +
        ":knowledge-installed-assets-ready";
    const auto hybrid_canary_marker =
        std::string("runtime:") + spec.composition_owner +
        ":knowledge-hybrid-canary-ready";
    const auto automation_ready_marker =
      std::string("runtime:") + spec.composition_owner +
      ":knowledge-refresh-automation-ready";
    const auto automation_fallback_prefix =
      std::string("runtime:") + spec.composition_owner +
      ":knowledge-refresh-automation-fallback:";
    assert_true(contains_port(composition.dependency_set->external_evidence, ready_marker),
                "runtime live composition matrix should keep installed ready marker even without query encoder for " +
                    spec.composition_owner);
    assert_true(!contains_port(composition.dependency_set->external_evidence, hybrid_canary_marker),
                "runtime live composition matrix should omit hybrid ready marker when embedding-required store lacks a query encoder for " +
                    spec.composition_owner);
    assert_true(contains_port(composition.dependency_set->external_evidence, automation_ready_marker) &&
            !contains_prefix(composition.dependency_set->external_evidence,
                     automation_fallback_prefix),
          "runtime live composition matrix should keep automation ready evidence even when the encoder-required hybrid marker is absent for " +
            spec.composition_owner);
    }
  }

  void runtime_live_composition_marks_automation_fallback_when_timer_is_missing() {
    const auto policy_snapshot = load_runtime_policy_snapshot();
    for (const auto& spec : owner_specs()) {
    const TempStateRoot assets_root("dasall-runtime-live-matrix-assets-automation-fallback");
    const TempStateRoot state_root("dasall-runtime-live-matrix-state-automation-fallback");
    copy_installed_runtime_assets(assets_root.path());

    const auto composition = compose_live_dependency_set(policy_snapshot,
                               spec,
                               assets_root.path(),
                               state_root.path(),
                               nullptr);
    assert_true(composition.ok(),
          "runtime live composition matrix should still compose when the knowledge automation timer is missing for " +
            spec.composition_owner + ": " + composition.error);

    const auto readiness = composition.dependency_set->describe_readiness();
    const auto ready_marker =
      std::string("runtime:") + spec.composition_owner +
      ":knowledge-installed-assets-ready";
    const auto automation_ready_marker =
      std::string("runtime:") + spec.composition_owner +
      ":knowledge-refresh-automation-ready";
    const auto automation_fallback_prefix =
      std::string("runtime:") + spec.composition_owner +
      ":knowledge-refresh-automation-fallback:";
    const auto degraded_prefix =
      std::string("runtime:") + spec.composition_owner + ":knowledge-degraded:";
    assert_true(readiness.default_unary_ready(),
          "runtime live composition matrix should keep unary readiness even when automation falls back for " +
            spec.composition_owner + ": " + readiness.summary());
    assert_true(contains_port(composition.dependency_set->external_evidence, ready_marker),
          "runtime live composition matrix should keep the installed knowledge ready marker when automation falls back for " +
            spec.composition_owner);
    assert_true(!contains_port(composition.dependency_set->external_evidence, automation_ready_marker) &&
            contains_prefix(composition.dependency_set->external_evidence,
                     automation_fallback_prefix) &&
            !contains_prefix(composition.dependency_set->external_evidence, degraded_prefix),
          "runtime live composition matrix should stratify automation fallback away from knowledge degradation for " +
            spec.composition_owner);
  }
}

}  // namespace

int main() {
  try {
    runtime_live_composition_keeps_ready_markers_stratified();
    runtime_live_composition_emits_hybrid_marker_when_encoder_is_ready();
    runtime_live_composition_omits_hybrid_marker_when_dense_lane_has_no_hits();
    runtime_live_composition_fail_closes_when_required_ports_are_missing();
    runtime_live_composition_marks_degraded_runtime_when_knowledge_is_missing();
    runtime_live_composition_omits_hybrid_marker_when_encoder_is_missing();
    runtime_live_composition_marks_automation_fallback_when_timer_is_missing();
  } catch (const std::exception& ex) {
    std::cerr << "[RuntimeLiveCompositionFailureMatrixTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}