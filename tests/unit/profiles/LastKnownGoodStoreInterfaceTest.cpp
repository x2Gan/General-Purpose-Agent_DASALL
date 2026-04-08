#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <unordered_map>

#include "ILastKnownGoodStore.h"
#include "support/TestAssertions.h"

namespace {

class FakeLastKnownGoodStore final : public dasall::profiles::ILastKnownGoodStore {
 public:
  [[nodiscard]] dasall::profiles::LastKnownGoodSaveResult save(
      dasall::profiles::RuntimePolicySnapshotRef snapshot_ref) override {
    if (!snapshot_ref || !snapshot_ref->has_consistent_values()) {
      return dasall::profiles::LastKnownGoodSaveResult{
          .saved = false,
          .error_code = dasall::profiles::ProfileErrorCode::LastKnownGoodUnavailable,
      };
    }

    snapshots_[snapshot_ref->effective_profile_id()] = std::move(snapshot_ref);
    return dasall::profiles::LastKnownGoodSaveResult{
        .saved = true,
        .error_code = std::nullopt,
    };
  }

  [[nodiscard]] dasall::profiles::LastKnownGoodLoadResult load(
      std::string_view profile_id) const override {
    const auto it = snapshots_.find(std::string(profile_id));
    if (it == snapshots_.end()) {
      return dasall::profiles::LastKnownGoodLoadResult{
          .snapshot = nullptr,
          .error_code = dasall::profiles::ProfileErrorCode::LastKnownGoodUnavailable,
      };
    }

    return dasall::profiles::LastKnownGoodLoadResult{
        .snapshot = it->second,
        .error_code = std::nullopt,
    };
  }

 private:
  std::unordered_map<std::string, dasall::profiles::RuntimePolicySnapshotRef> snapshots_;
};

dasall::profiles::RuntimePolicySnapshotRef make_snapshot_ref() {
  using dasall::profiles::CapabilityCachePolicy;
  using dasall::profiles::DegradePolicy;
  using dasall::profiles::ExecutionPolicy;
  using dasall::profiles::ModelProfile;
  using dasall::profiles::ModelRoutePolicy;
  using dasall::profiles::OpsPolicy;
  using dasall::profiles::PromptPolicy;
  using dasall::profiles::RuntimePolicySnapshot;
  using dasall::profiles::TimeoutBudget;
  using dasall::profiles::TimeoutPolicy;
  using dasall::profiles::TokenBudgetPolicy;

  return std::make_shared<const RuntimePolicySnapshot>(
      9U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{
          .max_tokens = 4096U,
          .max_turns = 16U,
          .max_tool_calls = 8U,
          .max_latency_ms = 4000U,
          .max_replan_count = 2U,
      },
      ModelProfile{
          .stage_routes = {{"default", ModelRoutePolicy{.route = "primary", .fallback_route = std::string("backup"), .streaming_enabled = true}}},
      },
      TokenBudgetPolicy{
          .max_input_tokens = 2048U,
          .max_output_tokens = 1024U,
          .max_history_turns = 8U,
          .compression_threshold = 512U,
      },
      PromptPolicy{
          .allowed_prompt_releases = {"stable"},
          .trusted_sources = {"profiles"},
          .tool_visibility_rules = {"default"},
      },
      CapabilityCachePolicy{
          .refresh_interval_ms = 1000,
          .expire_after_ms = 3000,
          .stale_read_allowed = true,
          .failure_backoff_ms = 100,
      },
      DegradePolicy{
          .fallback_chain = {"safe_mode"},
          .allow_model_failover = true,
          .allow_budget_degrade = true,
      },
      TimeoutPolicy{
          .llm = TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
          .tool = TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
          .mcp = TimeoutBudget{.timeout_ms = 1200, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
          .workflow = TimeoutBudget{.timeout_ms = 1600, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
      },
      ExecutionPolicy{
          .requires_high_risk_confirmation = true,
          .safe_mode_enabled = true,
          .audit_level = "strict",
          .allowed_tool_domains = {"default"},
      },
      OpsPolicy{
          .log_level = "warn",
          .metrics_granularity = "minimal",
          .trace_sample_ratio = 0.25,
          .remote_diagnostics_enabled = false,
          .upgrade_strategy = "manual",
      });
}

void test_last_known_good_store_supports_read_write_placeholder_flow() {
  using dasall::tests::support::assert_true;

  FakeLastKnownGoodStore store;
  const auto snapshot_ref = make_snapshot_ref();

  const auto save_result = store.save(snapshot_ref);
  const auto load_result = store.load("desktop_full");

  assert_true(save_result.ok(),
              "last known good store should accept validated snapshot references");
  assert_true(save_result.has_consistent_values(),
              "save result should stay internally consistent on success");
  assert_true(load_result.ok(),
              "last known good store should load previously saved snapshot references");
  assert_true(load_result.has_consistent_values(),
              "load result should stay internally consistent on success");
  assert_true(load_result.snapshot->effective_profile_id() == "desktop_full",
              "last known good store should preserve effective profile id across load");
}

void test_last_known_good_store_rejects_missing_snapshot_refs() {
  using dasall::tests::support::assert_true;

  FakeLastKnownGoodStore store;
  const auto failed_save = store.save(nullptr);
  const auto missing_load = store.load("edge_balanced");

  assert_true(!failed_save.ok(),
              "last known good store should reject null snapshot references");
  assert_true(failed_save.has_consistent_values(),
              "failed save result should still keep an explicit error code");
  assert_true(!missing_load.ok(),
              "last known good store should reject unknown profile ids");
  assert_true(missing_load.has_consistent_values(),
              "failed load result should still keep an explicit error code");
}

void test_ilast_known_good_store_interface_surface_stays_stable() {
  using dasall::profiles::ILastKnownGoodStore;
  using dasall::profiles::LastKnownGoodLoadResult;
  using dasall::profiles::LastKnownGoodSaveResult;
  using dasall::profiles::RuntimePolicySnapshotRef;
  using dasall::tests::support::assert_true;

  using SaveSignature = LastKnownGoodSaveResult (ILastKnownGoodStore::*)(RuntimePolicySnapshotRef);
  using LoadSignature = LastKnownGoodLoadResult (ILastKnownGoodStore::*)(std::string_view) const;

  static_assert(std::is_same_v<decltype(&ILastKnownGoodStore::save), SaveSignature>,
                "ILastKnownGoodStore::save signature should remain stable");
  static_assert(std::is_same_v<decltype(&ILastKnownGoodStore::load), LoadSignature>,
                "ILastKnownGoodStore::load signature should remain stable");

  assert_true(std::is_abstract_v<ILastKnownGoodStore>,
              "ILastKnownGoodStore should remain an abstract interface");
}

}  // namespace

int main() {
  try {
    test_last_known_good_store_supports_read_write_placeholder_flow();
    test_last_known_good_store_rejects_missing_snapshot_refs();
    test_ilast_known_good_store_interface_surface_stays_stable();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}