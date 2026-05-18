#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "BuildProfileManifest.h"
#include "RuntimePolicySnapshot.h"
#include "config/KnowledgeConfigProjector.h"
#include "support/TestAssertions.h"

namespace {

using dasall::knowledge::RetrievalMode;
using dasall::knowledge::config::KnowledgeConfigProjector;
using dasall::knowledge::config::KnowledgeConfigProjectorOverlay;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] dasall::profiles::RuntimePolicySnapshot make_runtime_policy_snapshot(
    std::string profile_id = "desktop_full",
    std::uint32_t max_latency_ms = 5000U,
    std::uint32_t worker_threads = 4U,
    bool allow_budget_degrade = true,
    bool allow_stale_read = false) {
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

  return RuntimePolicySnapshot{
      19U,
      std::move(profile_id),
      dasall::contracts::RuntimeBudget{
          .max_tokens = 8192U,
          .max_turns = 16U,
          .max_tool_calls = 8U,
          .max_latency_ms = max_latency_ms,
          .max_replan_count = 2U,
      },
      ModelProfile{
          .stage_routes = {
              {"planning", ModelRoutePolicy{.route = "cloud.reasoning",
                                             .fallback_route = "lan.general",
                                             .streaming_enabled = true}},
          },
      },
      TokenBudgetPolicy{.max_input_tokens = 4096U,
                        .max_output_tokens = 1024U,
                        .max_history_turns = 8U,
                        .compression_threshold = 3000U},
      PromptPolicy{.allowed_prompt_releases = {"stable"},
                   .trusted_sources = {"profiles"},
                   .tool_visibility_rules = {"builtin:all"}},
      CapabilityCachePolicy{.refresh_interval_ms = 30000,
                            .expire_after_ms = 120000,
                            .stale_read_allowed = allow_stale_read,
                            .failure_backoff_ms = 5000},
      DegradePolicy{.fallback_chain = {"lexical_only"},
                    .allow_model_failover = true,
                    .allow_budget_degrade = allow_budget_degrade},
      TimeoutPolicy{.llm = TimeoutBudget{.timeout_ms = 4500,
                                         .retry_budget = 2U,
                                         .circuit_breaker_threshold = 4U},
                    .tool = TimeoutBudget{.timeout_ms = 1500,
                                          .retry_budget = 1U,
                                          .circuit_breaker_threshold = 3U},
                    .mcp = TimeoutBudget{.timeout_ms = 1500,
                                         .retry_budget = 1U,
                                         .circuit_breaker_threshold = 3U},
                    .workflow = TimeoutBudget{.timeout_ms = 3000,
                                              .retry_budget = 1U,
                                              .circuit_breaker_threshold = 3U}},
      ExecutionPolicy{.requires_high_risk_confirmation = true,
                      .safe_mode_enabled = true,
                      .audit_level = "full",
                      .allowed_tool_domains = {"builtin", "mcp"}},
      OpsPolicy{.log_level = "info",
                .metrics_granularity = "core",
                .trace_sample_ratio = 0.25,
                .remote_diagnostics_enabled = true,
                .upgrade_strategy = "rolling"},
      worker_threads,
  };
}

[[nodiscard]] dasall::profiles::BuildProfileManifest make_manifest(
    std::initializer_list<std::string> enabled_modules) {
  return dasall::profiles::BuildProfileManifest{
      .enabled_modules = std::vector<std::string>(enabled_modules),
      .enabled_adapters = {},
      .observability_level = "standard",
      .build_tags = {"unit"},
      .toolchain_hint = std::string("gcc"),
  };
}

void test_projector_derives_default_knowledge_config_from_snapshot_and_manifest() {
  const auto config = KnowledgeConfigProjector::project(
      make_runtime_policy_snapshot(),
      make_manifest({"runtime", "knowledge", "memory_vector"}));

  assert_true(config.has_value(),
              "knowledge config projector should accept a consistent snapshot plus build manifest");
  assert_true(config->has_consistent_values(),
              "projected KnowledgeConfigSnapshot should satisfy the frozen 006 surface invariants");
  assert_true(config->knowledge_enabled,
              "knowledge module enablement should come directly from the build manifest");
  assert_true(config->vector_enabled,
              "memory_vector capability should come directly from the build manifest");
  assert_true(config->retrieval_mode_default == RetrievalMode::LexicalOnly,
              "vector-capable production profiles should still default to lexical-only retrieval");
  assert_equal(1024, static_cast<int>(config->evidence_budget_tokens),
               "evidence budget should derive from min(max_input_tokens/4, compression_threshold/2)");
  assert_equal(6, static_cast<int>(config->max_context_projection_items),
               "desktop-class worker threads should project the mid-tier context item budget");
  assert_equal(30000, static_cast<int>(config->catalog_refresh_interval_ms),
               "catalog refresh interval should project directly from capability cache policy");
  assert_equal(120000, static_cast<int>(config->catalog_expire_after_ms),
               "catalog expire interval should project directly from capability cache policy");
  assert_equal(1500, static_cast<int>(config->request_deadline_ms),
               "request deadline should clamp runtime_budget.max_latency_ms / 3 into the knowledge range");
  assert_equal(2, static_cast<int>(config->max_parallel_recall),
               "parallel recall should clamp to two lanes on a four-thread profile");
  assert_equal(525, static_cast<int>(config->sparse_recall_timeout_ms),
               "sparse lane timeout should derive from 35 percent of request deadline");
  assert_equal(525, static_cast<int>(config->dense_recall_timeout_ms),
               "dense lane timeout should derive from 35 percent of request deadline");
  assert_equal(30000, static_cast<int>(config->ingest_timeout_ms),
               "desktop_full should project the longer ingest timeout budget");
}

void test_projector_applies_module_local_overrides_without_rebuilding_parallel_config() {
  KnowledgeConfigProjectorOverlay overlay;
  overlay.retrieval_mode_default = RetrievalMode::LexicalOnly;
  overlay.evidence_budget_tokens = 512U;
  overlay.max_context_projection_items = 5U;
  overlay.catalog_refresh_interval_ms = 45000;
  overlay.catalog_expire_after_ms = 180000;
  overlay.allow_stale_read = true;
  overlay.failure_backoff_ms = 2500;
  overlay.request_deadline_ms = 900;
  overlay.allow_budget_degrade = false;
  overlay.max_parallel_recall = 1U;
  overlay.ingest_timeout_ms = 15000;

  const auto config = KnowledgeConfigProjector::project(
      make_runtime_policy_snapshot("cloud_full", 4200U, 8U, true, false),
      make_manifest({"runtime", "knowledge", "memory_vector"}),
      overlay);

  assert_true(config.has_value(),
              "knowledge projector should accept module-local deployment overrides on top of profile defaults");
  assert_true(config->retrieval_mode_default == RetrievalMode::LexicalOnly,
              "retrieval mode override should let deployment pin lexical-only even when vector is available");
  assert_equal(512, static_cast<int>(config->evidence_budget_tokens),
               "deployment override should be able to tighten the evidence token budget");
  assert_equal(5, static_cast<int>(config->max_context_projection_items),
               "deployment override should be able to tighten the context projection fanout");
  assert_true(config->allow_stale_read,
              "deployment override should be able to widen stale-read behavior within the knowledge module");
  assert_equal(2500, static_cast<int>(config->failure_backoff_ms),
               "deployment override should be able to tighten failure backoff");
  assert_equal(900, static_cast<int>(config->request_deadline_ms),
               "deployment override should replace the derived request deadline");
  assert_true(!config->allow_budget_degrade,
              "deployment override should be able to close budget degrade even if profile allows it");
  assert_equal(1, static_cast<int>(config->max_parallel_recall),
               "deployment override should be able to constrain parallel recall fanout");
  assert_equal(315, static_cast<int>(config->sparse_recall_timeout_ms),
               "lane timeout should be recomputed from the overridden request deadline when no explicit lane override exists");
  assert_equal(315, static_cast<int>(config->dense_recall_timeout_ms),
               "dense timeout should be recomputed from the overridden request deadline when no explicit lane override exists");
  assert_equal(15000, static_cast<int>(config->ingest_timeout_ms),
               "deployment override should be able to override ingest timeout independently of profile class");
}

void test_projector_keeps_knowledge_enabled_vector_disabled_as_legal_lexical_only_shape() {
  const auto config = KnowledgeConfigProjector::project(
      make_runtime_policy_snapshot("edge_balanced", 1800U, 2U),
      make_manifest({"runtime", "knowledge"}));

  assert_true(config.has_value(),
              "knowledge=true and memory_vector=false should remain a legal projector combination");
  assert_true(config->knowledge_enabled,
              "knowledge projector should preserve the enabled knowledge module state");
  assert_true(!config->vector_enabled,
              "missing memory_vector module should disable vector retrieval without rejecting the config");
  assert_true(config->retrieval_mode_default == RetrievalMode::LexicalOnly,
              "vector-disabled profile should automatically fall back to lexical-only");
  assert_equal(10000, static_cast<int>(config->ingest_timeout_ms),
               "edge-like profiles should keep the shorter ingest timeout budget");
}

void test_projector_rejects_invalid_overrides_instead_of_inventing_a_parallel_config_system() {
  KnowledgeConfigProjectorOverlay invalid_overlay;
  invalid_overlay.retrieval_mode_default = RetrievalMode::DenseOnly;

  const auto config = KnowledgeConfigProjector::project(
      make_runtime_policy_snapshot("edge_balanced", 1800U, 2U),
      make_manifest({"runtime", "knowledge"}),
      invalid_overlay);

  assert_true(!config.has_value(),
              "projector should reject vector-dependent overrides when memory_vector is disabled");
}

}  // namespace

int main() {
  try {
    test_projector_derives_default_knowledge_config_from_snapshot_and_manifest();
    test_projector_applies_module_local_overrides_without_rebuilding_parallel_config();
    test_projector_keeps_knowledge_enabled_vector_disabled_as_legal_lexical_only_shape();
    test_projector_rejects_invalid_overrides_instead_of_inventing_a_parallel_config_system();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}