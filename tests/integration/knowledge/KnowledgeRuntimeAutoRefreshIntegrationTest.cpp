#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ITimer.h"
#include "IKnowledgeService.h"
#include "KnowledgeTypes.h"
#include "ProfileCatalog.h"
#include "RuntimeDependencySet.h"
#include "RuntimeLiveDependencyComposition.h"
#include "RuntimePolicyProvider.h"
#include "support/TestAssertions.h"

namespace {

namespace fs = std::filesystem;

using dasall::tests::support::assert_true;

constexpr char kDefaultProfileId[] = "desktop_full";
constexpr char kCompositionOwner[] = "gateway.http-unary";
constexpr char kBaselineToken[] = "runtimeautorefreshbaselineanchor";
constexpr char kUpdatedToken[] = "runtimeautorefreshupdatedanchor";
constexpr char kArchitectureBaselineToken[] =
  "runtimeautorefresharchitecturebaselineanchor";
constexpr char kArchitectureUpdatedToken[] =
  "runtimeautorefresharchitectureupdatedanchor";
constexpr std::int64_t kRefreshIntervalMs = 25;

const fs::path kAutoRefreshDocument =
    fs::path("docs") / "adr" / "ADR-RUNTIME-AUTO-REFRESH.md";
const fs::path kArchitectureDocument =
  fs::path("docs") / "ssot" / "RuntimeAutoRefresh.md";

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
         ("dasall-knowledge-runtime-auto-refresh-" + std::to_string(nonce));
}

void write_file(const fs::path& path, std::string_view content) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << content;
}

[[nodiscard]] std::string absolute_source_uri(const fs::path& assets_root,
                                              const fs::path& relative_path) {
  return (assets_root / relative_path).lexically_normal().generic_string();
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

[[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>
load_runtime_policy_snapshot(const fs::path& assets_root) {
  const dasall::profiles::ProfileCatalog catalog(assets_root / "profiles");
  const dasall::profiles::RuntimePolicyProvider provider(catalog);
  const auto snapshot_result = provider.load_snapshot(
      dasall::profiles::RuntimePolicyLoadRequest{
          .profile_id = kDefaultProfileId,
      });
  assert_true(snapshot_result.ok() && snapshot_result.snapshot != nullptr,
              "runtime auto-refresh integration should load the copied runtime profile snapshot");
  return snapshot_result.snapshot;
}

[[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>
with_refresh_interval(const std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>& base,
                      const std::int64_t refresh_interval_ms) {
  auto capability_cache_policy = base->capability_cache_policy();
  capability_cache_policy.refresh_interval_ms = refresh_interval_ms;
  if (capability_cache_policy.expire_after_ms < refresh_interval_ms) {
    capability_cache_policy.expire_after_ms = refresh_interval_ms;
  }

  return std::make_shared<dasall::profiles::RuntimePolicySnapshot>(
      base->generation(),
      base->effective_profile_id(),
      base->runtime_budget(),
      base->model_profile(),
      base->token_budget_policy(),
      base->prompt_policy(),
      capability_cache_policy,
      base->degrade_policy(),
      base->timeout_policy(),
      base->execution_policy(),
      base->ops_policy(),
      base->worker_threads(),
      base->multi_agent_enabled());
}

[[nodiscard]] dasall::knowledge::KnowledgeQuery make_query(std::string request_id,
                                                           std::string query_text) {
  dasall::knowledge::KnowledgeQuery query;
  query.request_id = std::move(request_id);
  query.query_text = std::move(query_text);
  query.query_kind = dasall::knowledge::KnowledgeQueryKind::PolicyEvidence;
  query.top_k = 4U;
  query.max_context_projection_items = 3U;
  return query;
}

[[nodiscard]] bool evidence_contains_token(const dasall::knowledge::EvidenceBundle& evidence,
                                           std::string_view token) {
  for (const auto& slice : evidence.slices) {
    if (slice.snippet.find(token) != std::string::npos ||
        slice.citation_ref.find(token) != std::string::npos) {
      return true;
    }
  }

  for (const auto& projection : evidence.context_projection) {
    if (projection.find(token) != std::string::npos) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] bool contains_port(const std::vector<std::string>& values,
                                 const std::string& expected_value) {
  return std::find(values.begin(), values.end(), expected_value) != values.end();
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
    std::lock_guard<std::mutex> lock(mutex_);
    one_shot_specs.push_back(spec);
    if (callback) {
      callbacks_.emplace(next_id_, std::move(callback));
    }
    return dasall::platform::PlatformResult<dasall::platform::TimerHandle>::success(
        dasall::platform::TimerHandle{.native_id = next_id_++});
  }

  dasall::platform::PlatformResult<dasall::platform::TimerHandle> start_periodic(
      const dasall::platform::TimerSpec& spec,
      dasall::platform::TimerCallback callback) override {
    std::lock_guard<std::mutex> lock(mutex_);
    periodic_specs.push_back(spec);
    if (callback) {
      callbacks_.emplace(next_id_, std::move(callback));
    }
    return dasall::platform::PlatformResult<dasall::platform::TimerHandle>::success(
        dasall::platform::TimerHandle{.native_id = next_id_++});
  }

  dasall::platform::PlatformResult<dasall::platform::TimerCancelResult> cancel(
      const dasall::platform::TimerHandle& handle) override {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool cancelled = callbacks_.erase(handle.native_id) > 0U;
    cancelled_handles.push_back(handle.native_id);
    return dasall::platform::PlatformResult<dasall::platform::TimerCancelResult>::success(
        dasall::platform::TimerCancelResult{
            .cancelled = cancelled,
            .drift_stats = {},
        });
  }

  void fire_all_periodic() {
    std::vector<dasall::platform::TimerCallback> callbacks;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (const auto& entry : callbacks_) {
        callbacks.push_back(entry.second);
      }
    }

    for (const auto& callback : callbacks) {
      callback(dasall::platform::TimerDriftStats{
          .expiration_count = 1U,
          .last_drift_ms = 0U,
          .max_drift_ms = 0U,
      });
    }
  }

  std::vector<dasall::platform::TimerSpec> one_shot_specs;
  std::vector<dasall::platform::TimerSpec> periodic_specs;
  std::vector<std::uint64_t> cancelled_handles;

 private:
  std::mutex mutex_;
  std::uint64_t next_id_ = 1U;
  std::unordered_map<std::uint64_t, dasall::platform::TimerCallback> callbacks_;
};

class RecordingRefreshSourceProvider final
    : public dasall::apps::runtime_support::IRuntimeKnowledgeRefreshSourceProvider {
 public:
  [[nodiscard]] dasall::apps::runtime_support::RuntimeKnowledgeRefreshPlan next_plan() override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++call_count_;
    if (next_index_ < queued_plans_.size()) {
      last_plan_ = queued_plans_[next_index_++];
      return last_plan_;
    }

    last_plan_ = make_full_scan_fallback_plan();
    return last_plan_;
  }

  void enqueue_plan(dasall::apps::runtime_support::RuntimeKnowledgeRefreshPlan plan) {
    std::lock_guard<std::mutex> lock(mutex_);
    queued_plans_.push_back(std::move(plan));
  }

  [[nodiscard]] std::size_t call_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return call_count_;
  }

  [[nodiscard]] dasall::apps::runtime_support::RuntimeKnowledgeRefreshPlan last_plan() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_plan_;
  }

 private:
  static dasall::apps::runtime_support::RuntimeKnowledgeRefreshPlan
  make_full_scan_fallback_plan() {
    dasall::apps::runtime_support::RuntimeKnowledgeRefreshPlan plan;
    plan.kind =
        dasall::apps::runtime_support::RuntimeKnowledgeRefreshPlanKind::FullScanFallback;
    return plan;
  }

  mutable std::mutex mutex_;
  std::vector<dasall::apps::runtime_support::RuntimeKnowledgeRefreshPlan> queued_plans_;
  std::size_t next_index_ = 0U;
  std::size_t call_count_ = 0U;
  dasall::apps::runtime_support::RuntimeKnowledgeRefreshPlan last_plan_ =
      make_full_scan_fallback_plan();
};

[[nodiscard]] dasall::apps::runtime_support::RuntimeKnowledgeRefreshPlan
make_selective_refresh_plan(std::vector<std::string> updated_sources) {
  dasall::apps::runtime_support::RuntimeKnowledgeRefreshPlan plan;
  plan.kind = dasall::apps::runtime_support::RuntimeKnowledgeRefreshPlanKind::Selective;
  plan.updated_sources = std::move(updated_sources);
  return plan;
}

[[nodiscard]] dasall::apps::runtime_support::RuntimeKnowledgeRefreshPlan
make_full_scan_fallback_plan() {
  dasall::apps::runtime_support::RuntimeKnowledgeRefreshPlan plan;
  plan.kind =
      dasall::apps::runtime_support::RuntimeKnowledgeRefreshPlanKind::FullScanFallback;
  return plan;
}

void wait_for_refresh_completion(
    const std::shared_ptr<dasall::knowledge::IKnowledgeService>& service,
    std::string_view label) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    const auto health = service->health_snapshot();
    if (!health.refresh_in_flight && health.last_refresh_status.has_value() &&
        *health.last_refresh_status == dasall::knowledge::RefreshStatus::Completed) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  throw std::runtime_error(std::string(label) +
                           ": timed out waiting for async refresh completion");
}

[[nodiscard]] bool wait_for_refresh_in_flight(
    const std::shared_ptr<dasall::knowledge::IKnowledgeService>& service) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    if (service->health_snapshot().refresh_in_flight) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  return false;
}

void knowledge_runtime_auto_refresh_default_provider_detects_changed_asset() {
  ScopedTempDir temp_root{make_temp_root()};
  const auto assets_root = temp_root.path / "assets";
  const auto state_root = temp_root.path / "state";

  copy_installed_runtime_assets(assets_root);
  fs::create_directories(state_root);
  write_file(assets_root / kAutoRefreshDocument,
             "# Runtime Auto Refresh\n\nruntimeautorefreshbaselineanchor baseline evidence should be visible before automation.\n");

  const auto policy_snapshot =
      with_refresh_interval(load_runtime_policy_snapshot(assets_root), kRefreshIntervalMs);
  auto timer = std::make_shared<RecordingTimer>();
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
          .knowledge_refresh_timer = timer,
          .knowledge_refresh_source_provider = nullptr,
      });
  assert_true(composition.ok(),
              "runtime auto-refresh integration should compose live runtime dependencies: " +
                  composition.error);
  assert_true(composition.dependency_set != nullptr &&
                  composition.dependency_set->knowledge_service != nullptr,
              "runtime auto-refresh integration should expose a composed knowledge service");

  const auto ready_marker =
      std::string("runtime:") + kCompositionOwner + ":knowledge-refresh-automation-ready";
  const auto fallback_prefix =
      std::string("runtime:") + kCompositionOwner + ":knowledge-refresh-automation-fallback:";
  assert_true(timer->periodic_specs.size() == 1U,
              "runtime auto-refresh integration should arm exactly one periodic knowledge refresh timer");
  assert_true(timer->periodic_specs.front().mode == dasall::platform::TimerMode::Periodic &&
                  timer->periodic_specs.front().interval_ms == kRefreshIntervalMs &&
                  timer->periodic_specs.front().initial_delay_ms == kRefreshIntervalMs &&
                  timer->periodic_specs.front().clock_kind ==
                      dasall::platform::TimerClockKind::Monotonic,
              "runtime auto-refresh integration should arm the periodic knowledge timer on the monotonic ITimer seam");
  assert_true(contains_port(composition.dependency_set->external_evidence, ready_marker),
              "runtime auto-refresh integration should record the automation ready evidence when the timer arms successfully");
  assert_true(!contains_prefix(composition.dependency_set->external_evidence, fallback_prefix),
              "runtime auto-refresh integration should not mix automation fallback evidence into the timer-ready path");

  const auto baseline_snapshot_id =
      composition.dependency_set->knowledge_service->health_snapshot().active_snapshot_id;
  assert_true(!baseline_snapshot_id.empty(),
              "runtime auto-refresh integration should expose the baseline active snapshot id");

  const auto baseline_retrieve = composition.dependency_set->knowledge_service->retrieve(
      make_query("req-runtime-auto-refresh-baseline", kBaselineToken));
  assert_true(baseline_retrieve.ok && baseline_retrieve.evidence.has_value() &&
                  evidence_contains_token(*baseline_retrieve.evidence, kBaselineToken),
              "runtime auto-refresh integration should retrieve the baseline token before the timer fires");

  write_file(assets_root / kAutoRefreshDocument,
             "# Runtime Auto Refresh\n\nruntimeautorefreshupdatedanchor timer-driven refresh should surface this updated evidence.\n");
  timer->fire_all_periodic();
    wait_for_refresh_completion(composition.dependency_set->knowledge_service,
                  "runtime auto-refresh default provider");

    const auto refreshed_snapshot_id =
      composition.dependency_set->knowledge_service->health_snapshot().active_snapshot_id;
    assert_true(refreshed_snapshot_id != baseline_snapshot_id,
          "runtime auto-refresh integration should update the active snapshot after a timer-triggered refresh");

    const auto retrieve_result = composition.dependency_set->knowledge_service->retrieve(
      make_query("req-runtime-auto-refresh-updated", kUpdatedToken));
    assert_true(retrieve_result.ok && retrieve_result.evidence.has_value() &&
            evidence_contains_token(*retrieve_result.evidence, kUpdatedToken),
          "runtime auto-refresh integration should surface updated evidence after the default selective provider detects a changed asset");
  }

  void knowledge_runtime_auto_refresh_respects_selective_provider_scope() {
    ScopedTempDir temp_root{make_temp_root()};
    const auto assets_root = temp_root.path / "assets";
    const auto state_root = temp_root.path / "state";

    copy_installed_runtime_assets(assets_root);
    fs::create_directories(state_root);
    write_file(assets_root / kAutoRefreshDocument,
         "# Runtime Auto Refresh\n\nruntimeautorefreshbaselineanchor baseline evidence should be visible before automation.\n");
    write_file(assets_root / kArchitectureDocument,
         "# Runtime Auto Refresh Design\n\nruntimeautorefresharchitecturebaselineanchor architecture baseline evidence should remain active until that corpus is refreshed.\n");

    const auto policy_snapshot =
      with_refresh_interval(load_runtime_policy_snapshot(assets_root), kRefreshIntervalMs);
    auto timer = std::make_shared<RecordingTimer>();
    auto provider = std::make_shared<RecordingRefreshSourceProvider>();
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
        .knowledge_refresh_timer = timer,
        .knowledge_refresh_source_provider = provider,
      });
    assert_true(composition.ok() && composition.dependency_set != nullptr &&
            composition.dependency_set->knowledge_service != nullptr,
          "runtime auto-refresh integration should compose with an injected refresh provider");

    write_file(assets_root / kAutoRefreshDocument,
         "# Runtime Auto Refresh\n\nruntimeautorefreshupdatedanchor selective refresh should surface this updated ADR evidence.\n");
    write_file(assets_root / kArchitectureDocument,
         "# Runtime Auto Refresh Design\n\nruntimeautorefresharchitectureupdatedanchor architecture evidence should stay stale when the selective plan excludes this source.\n");
      provider->enqueue_plan(make_selective_refresh_plan(
        {absolute_source_uri(assets_root, kAutoRefreshDocument)}));

    timer->fire_all_periodic();
    wait_for_refresh_completion(composition.dependency_set->knowledge_service,
                  "runtime auto-refresh selective provider scope");

    const auto selective_retrieve = composition.dependency_set->knowledge_service->retrieve(
      make_query("req-runtime-auto-refresh-selective", kUpdatedToken));
    assert_true(selective_retrieve.ok && selective_retrieve.evidence.has_value() &&
            evidence_contains_token(*selective_retrieve.evidence, kUpdatedToken),
          "runtime auto-refresh integration should surface the updated ADR token when the provider emits a selective source plan");

    const auto architecture_updated = composition.dependency_set->knowledge_service->retrieve(
      make_query("req-runtime-auto-refresh-architecture-updated",
           kArchitectureUpdatedToken));
    assert_true(!architecture_updated.ok || !architecture_updated.evidence.has_value() ||
            !evidence_contains_token(*architecture_updated.evidence,
                        kArchitectureUpdatedToken),
          "runtime auto-refresh integration should not refresh non-target corpora when the selective plan excludes their changed source");

    const auto last_plan = provider->last_plan();
    assert_true(provider->call_count() == 1U &&
            last_plan.kind ==
              dasall::apps::runtime_support::RuntimeKnowledgeRefreshPlanKind::Selective &&
            last_plan.updated_sources.size() == 1U &&
              last_plan.updated_sources.front() ==
                absolute_source_uri(assets_root, kAutoRefreshDocument),
          "runtime auto-refresh integration should forward the injected selective source delta unchanged into the timer callback path");
  }

  void knowledge_runtime_auto_refresh_falls_back_to_full_scan_when_provider_requests_it() {
    ScopedTempDir temp_root{make_temp_root()};
    const auto assets_root = temp_root.path / "assets";
    const auto state_root = temp_root.path / "state";

    copy_installed_runtime_assets(assets_root);
    fs::create_directories(state_root);
    write_file(assets_root / kArchitectureDocument,
         "# Runtime Auto Refresh Design\n\nruntimeautorefresharchitecturebaselineanchor architecture baseline evidence should be visible before fallback refresh.\n");

    const auto policy_snapshot =
      with_refresh_interval(load_runtime_policy_snapshot(assets_root), kRefreshIntervalMs);
    auto timer = std::make_shared<RecordingTimer>();
    auto provider = std::make_shared<RecordingRefreshSourceProvider>();
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
        .knowledge_refresh_timer = timer,
        .knowledge_refresh_source_provider = provider,
      });
    assert_true(composition.ok() && composition.dependency_set != nullptr &&
            composition.dependency_set->knowledge_service != nullptr,
          "runtime auto-refresh integration should compose with an injected fallback provider");

    write_file(assets_root / kArchitectureDocument,
         "# Runtime Auto Refresh Design\n\nruntimeautorefresharchitectureupdatedanchor full-scan fallback should surface this architecture update.\n");
    provider->enqueue_plan(make_full_scan_fallback_plan());

    timer->fire_all_periodic();
    wait_for_refresh_completion(composition.dependency_set->knowledge_service,
                  "runtime auto-refresh full-scan fallback");

    const auto retrieve_result = composition.dependency_set->knowledge_service->retrieve(
      make_query("req-runtime-auto-refresh-architecture-fallback",
           kArchitectureUpdatedToken));
    assert_true(retrieve_result.ok && retrieve_result.evidence.has_value() &&
            evidence_contains_token(*retrieve_result.evidence,
                       kArchitectureUpdatedToken),
          "runtime auto-refresh integration should retain the 041 full-scan fallback path when the provider explicitly requests it");
  }

  void knowledge_runtime_auto_refresh_skips_timer_tick_while_refresh_is_in_flight() {
    ScopedTempDir temp_root{make_temp_root()};
    const auto assets_root = temp_root.path / "assets";
    const auto state_root = temp_root.path / "state";

    copy_installed_runtime_assets(assets_root);
    fs::create_directories(state_root);
    write_file(assets_root / kAutoRefreshDocument,
         "# Runtime Auto Refresh\n\nruntimeautorefreshbaselineanchor baseline evidence should be visible before busy-skip verification.\n");

    const auto policy_snapshot =
      with_refresh_interval(load_runtime_policy_snapshot(assets_root), kRefreshIntervalMs);
    auto timer = std::make_shared<RecordingTimer>();
    auto provider = std::make_shared<RecordingRefreshSourceProvider>();
    provider->enqueue_plan(
        make_selective_refresh_plan({absolute_source_uri(assets_root, kAutoRefreshDocument)}));
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
        .knowledge_refresh_timer = timer,
        .knowledge_refresh_source_provider = provider,
      });
    assert_true(composition.ok() && composition.dependency_set != nullptr &&
            composition.dependency_set->knowledge_service != nullptr,
          "runtime auto-refresh integration should compose with an injected provider for busy-skip verification");

    const std::string busy_payload(65536U, 'b');
    for (int index = 0; index < 24; ++index) {
      write_file(assets_root / "docs" / "adr" /
                     ("ADR-BUSY-" + std::to_string(index) + ".md"),
                 "# ADR Busy\n\n" + busy_payload + "\n");
    }

    const auto manual_refresh =
      composition.dependency_set->knowledge_service->request_refresh(
        dasall::knowledge::CorpusChangeSet{});
    assert_true(manual_refresh.has_consistent_values() &&
            manual_refresh.status == dasall::knowledge::RefreshStatus::Accepted,
          "runtime auto-refresh integration should accept the manual refresh used to create an in-flight busy window");
    assert_true(wait_for_refresh_in_flight(composition.dependency_set->knowledge_service),
          "runtime auto-refresh integration should observe the manual refresh as in-flight before firing the timer");

    timer->fire_all_periodic();
    assert_true(provider->call_count() == 0U,
          "runtime auto-refresh integration should not consult the selective provider while a refresh is already in flight");

    wait_for_refresh_completion(composition.dependency_set->knowledge_service,
                  "runtime auto-refresh busy skip");
}

}  // namespace

int main() {
  try {
    knowledge_runtime_auto_refresh_default_provider_detects_changed_asset();
    knowledge_runtime_auto_refresh_respects_selective_provider_scope();
    knowledge_runtime_auto_refresh_falls_back_to_full_scan_when_provider_requests_it();
    knowledge_runtime_auto_refresh_skips_timer_tick_while_refresh_is_in_flight();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}