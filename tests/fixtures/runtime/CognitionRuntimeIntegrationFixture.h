#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "AgentTypes.h"
#include "../../../cognition/include/ICognitionEngine.h"
#include "../../../memory/include/IMemoryManager.h"
#include "../../../cognition/include/IResponseBuilder.h"
#include "RuntimeDependencySet.h"
#include "RuntimePolicySnapshot.h"
#include "ToolManager.h"
#include "checkpoint/RuntimeBudget.h"
#include "execution/BuiltinExecutorLane.h"
#include "MockLLMManager.h"
#include "registry/ToolRegistry.h"
#include "writeback/MemoryWritebackRequest.h"

namespace dasall::tests::runtime_fixture {

[[nodiscard]] inline std::string make_runtime_structured_planning_payload() {
    return std::string{"{"}
                 + "\"schema_version\":\"cognition.plan.v1\","
                 + "\"plan_id\":\"plan-runtime-integration\","
                 + "\"revision\":1,"
                 + "\"nodes\":[{"
                 + "\"node_id\":\"plan-node:runtime-integration\","
                 + "\"objective\":\"collect governed evidence through the builtin dataset tool\","
                 + "\"success_signal\":\"runtime_evidence_collected\","
                 + "\"action_kind_hint\":\"tool_action\","
                 + "\"depends_on\":[],"
                 + "\"evidence_refs\":[\"runtime:cognition-integration\"]}],"
                 + "\"edges\":[],"
                 + "\"open_questions\":[],"
                 + "\"plan_rationale\":\"runtime integration should project a governed plan graph\","
                 + "\"estimated_complexity\":1}"
                 ;
}

[[nodiscard]] inline std::string make_runtime_structured_execution_payload() {
    return std::string{"{"}
                 + "\"schema_version\":\"cognition.reasoning.v1\","
                 + "\"decision_kind\":\"ExecuteAction\","
                 + "\"confidence\":0.82,"
                 + "\"rationale\":\"runtime integration should preserve governed tool intent\","
                 + "\"selected_node_id\":\"plan-node:runtime-integration\","
                 + "\"tool_intent_hint\":{"
                 + "\"tool_name\":\"agent.dataset\","
                 + "\"intent_summary\":\"query runtime-visible data through tool governance\","
                 + "\"argument_hints\":[\"query=current_state\"],"
                 + "\"evidence_refs\":[\"runtime:cognition-integration\"]},"
                 + "\"clarification_needed\":false,"
                 + "\"clarification_question\":null,"
                 + "\"response_outline\":null,"
                 + "\"candidate_scores\":[{"
                 + "\"candidate_name\":\"execute_action\","
                 + "\"score\":0.82,"
                 + "\"rationale\":\"runtime integration should execute the builtin tool\"}]}"
                 ;
}

inline std::filesystem::path make_temp_database_path(const std::string& stem) {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return std::filesystem::temp_directory_path() /
         (stem + "-" + std::to_string(timestamp) + ".db");
}

inline void cleanup_database_artifacts(const std::filesystem::path& database_path) {
  (void)std::filesystem::remove(database_path);
  (void)std::filesystem::remove(database_path.string() + "-wal");
  (void)std::filesystem::remove(database_path.string() + "-shm");
}

inline memory::MemoryConfig make_sqlite_config(const std::filesystem::path& database_path,
                                               const std::string& migrations_dir) {
  memory::MemoryConfig config;
  config.storage.backend = memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.migrations_dir = migrations_dir;
  config.vector.enabled = false;
  return config;
}

inline std::shared_ptr<runtime::RuntimeDependencySet> make_true_integration_dependency_set(
    const memory::MemoryConfig& config,
    const std::string& session_id,
    const std::string& turn_id,
    const std::string& seed_query) {
  auto dependency_set = std::make_shared<runtime::RuntimeDependencySet>();

  std::shared_ptr<memory::IMemoryManager> memory_manager(memory::create_memory_manager(config));
  const auto init_code = memory_manager->init(config);
  if (static_cast<int>(init_code) != 0) {
    throw std::runtime_error("failed to initialize sqlite-backed memory manager");
  }

  memory::MemoryWritebackRequest writeback_request;
  writeback_request.session_id = session_id;
  writeback_request.turn.turn_id = turn_id;
  writeback_request.turn.session_id = session_id;
  writeback_request.turn.user_input = seed_query;
  writeback_request.turn.agent_response = "seed cognition runtime integration context";
  writeback_request.summary_candidate = contracts::SummaryMemory{};
  writeback_request.summary_candidate->summary_text =
      "cognition runtime integration seeded context";
  writeback_request.summary_candidate->confirmed_facts =
      std::vector<std::string>{"cognition runtime integration context is available"};

  memory::FactCandidate fact_candidate;
  fact_candidate.fact.fact_text = "cognition runtime integration context is available";
  fact_candidate.fact.fact_type = "status";
  fact_candidate.fact.confidence_score = 90U;
  fact_candidate.fact.source_turn_ids = std::vector<std::string>{turn_id};
  fact_candidate.extraction_source = "integration-test";
  writeback_request.fact_candidates.push_back(std::move(fact_candidate));

  const auto writeback_result = memory_manager->write_back(writeback_request);
  if (writeback_result.result_code.has_value()) {
    throw std::runtime_error("failed to seed memory manager for cognition runtime integration");
  }

  auto llm_manager = std::make_shared<dasall::tests::mocks::MockLLMManager>();
  llm_manager->set_default_content("runtime unary integration completed: " + seed_query);
  llm_manager->set_stage_result(
      "planning",
      dasall::tests::mocks::MockLLMManager::make_structured_stage_result(
          "planning", make_runtime_structured_planning_payload()));
  llm_manager->set_stage_result(
      "execution",
      dasall::tests::mocks::MockLLMManager::make_structured_stage_result(
          "execution", make_runtime_structured_execution_payload()));

  dependency_set->memory_manager = std::move(memory_manager);
  dependency_set->llm_manager = llm_manager;

  auto registry = std::make_shared<tools::registry::ToolRegistry>();
  const auto registered = registry->register_builtin(contracts::ToolDescriptor{
      .tool_name = std::string{"agent.dataset"},
      .display_name = std::string{"Agent Dataset"},
      .category = contracts::ToolCategory::Information,
      .capability_tier = contracts::ToolCapabilityTier::Preview,
      .is_read_only = true,
      .supports_compensation = false,
      .default_timeout_ms = 30000U,
      .input_schema_ref = std::string{"schema://tools/agent.dataset/input/v1"},
      .output_schema_ref = std::string{"schema://tools/agent.dataset/output/v1"},
      .required_scopes = std::vector<std::string>{"tools.read"},
      .tags = std::vector<std::string>{"builtin", "query"},
      .version = std::string{"1.0.0"},
  });
  if (!registered) {
    throw std::runtime_error("failed to register agent.dataset descriptor");
  }

  auto builtin_lane = std::make_shared<tools::execution::BuiltinExecutorLane>(
      tools::execution::BuiltinExecutorLaneDependencies{
          .registry = registry,
          .service_bridge = nullptr,
          .execution_service = nullptr,
          .data_service = nullptr,
          .now_ms = {},
      });

  tools::manager::ToolManagerDependencies tool_dependencies;
  tool_dependencies.registry = std::move(registry);
  tool_dependencies.executor = [builtin_lane](const auto& execution_request) {
    return builtin_lane->execute(
        execution_request.tool_ir,
        tools::ToolExecutionContext{
            .invocation_context = execution_request.invocation_context,
            .lane_key = execution_request.route_decision.lane_key,
        });
  };
  dependency_set->tool_manager =
      std::make_shared<tools::ToolManager>(std::move(tool_dependencies));
    dependency_set->local_stub_ports.main_loop_exit =
            runtime::RuntimeStubMainLoopExit::ToolRound;
  dependency_set->visible_tools = {"agent.dataset"};
  dependency_set->available_tool_descriptors = {
      contracts::ToolDescriptor{
          .tool_name = std::string{"agent.dataset"},
          .display_name = std::string{"Agent Dataset"},
          .category = contracts::ToolCategory::Information,
          .capability_tier = contracts::ToolCapabilityTier::Preview,
          .is_read_only = true,
          .supports_compensation = false,
          .default_timeout_ms = 30000U,
          .input_schema_ref = std::string{"schema://tools/agent.dataset/input/v1"},
          .output_schema_ref = std::string{"schema://tools/agent.dataset/output/v1"},
          .required_scopes = std::vector<std::string>{"tools.read"},
          .tags = std::vector<std::string>{"builtin", "query"},
          .version = std::string{"1.0.0"},
      },
  };
  dependency_set->external_evidence = {"runtime:cognition-integration"};

  return dependency_set;
}

inline std::shared_ptr<const profiles::RuntimePolicySnapshot>
make_true_integration_policy_snapshot(const std::string& profile_id,
                                                                            const std::optional<std::string>& omitted_stage = std::nullopt) {
    auto route_name = [&](std::string_view stage_name) {
        return profile_id + "." + std::string(stage_name) + ".primary";
    };
    auto fallback_route_name = [&](std::string_view stage_name) {
        return profile_id + "." + std::string(stage_name) + ".fallback";
    };

  profiles::ModelProfile model_profile;
    const auto emplace_stage_route = [&](std::string_view stage_name, bool streaming_enabled) {
        if (omitted_stage.has_value() && *omitted_stage == stage_name) {
            return;
        }

        model_profile.stage_routes.emplace(
                std::string(stage_name),
                profiles::ModelRoutePolicy{
                        .route = route_name(stage_name),
                        .fallback_route = fallback_route_name(stage_name),
                        .streaming_enabled = streaming_enabled,
                });
    };
    emplace_stage_route("perception", false);
    emplace_stage_route("planning", false);
    emplace_stage_route("execution", false);
    emplace_stage_route("reflection", false);
    emplace_stage_route("response", profile_id != "edge_minimal");

    const auto llm_timeout_ms = profile_id == "edge_minimal"
                                                                    ? 900
                                                                    : profile_id == "edge_balanced"
                                                                                ? 1200
                                                                                : profile_id == "factory_test" ? 1400 : 1800;
    const auto max_output_tokens = profile_id == "edge_minimal"
                                                                         ? 256U
                                                                         : profile_id == "edge_balanced"
                                                                                     ? 384U
                                                                                     : profile_id == "factory_test" ? 320U : 512U;
    const auto max_latency_ms = static_cast<std::uint32_t>(llm_timeout_ms + 400);

  return std::make_shared<const profiles::RuntimePolicySnapshot>(
      1U,
      profile_id,
      contracts::RuntimeBudget{
          .max_tokens = 2048U,
          .max_turns = 6U,
          .max_tool_calls = 2U,
                    .max_latency_ms = max_latency_ms,
          .max_replan_count = 2U,
      },
      std::move(model_profile),
      profiles::TokenBudgetPolicy{
          .max_input_tokens = 2048U,
                    .max_output_tokens = max_output_tokens,
          .max_history_turns = 8U,
          .compression_threshold = 1024U,
      },
      profiles::PromptPolicy{
          .allowed_prompt_releases = {"stable"},
          .trusted_sources = {"runtime"},
          .tool_visibility_rules = {"builtin:agent.dataset"},
      },
      profiles::CapabilityCachePolicy{
          .refresh_interval_ms = 1000,
          .expire_after_ms = 5000,
          .stale_read_allowed = false,
          .failure_backoff_ms = 100,
      },
      profiles::DegradePolicy{
          .fallback_chain = {"safe_mode"},
          .allow_model_failover = true,
          .allow_budget_degrade = true,
      },
      profiles::TimeoutPolicy{
          .llm = profiles::TimeoutBudget{.timeout_ms = llm_timeout_ms, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
          .tool = profiles::TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
          .mcp = profiles::TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
          .workflow = profiles::TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
      },
      profiles::ExecutionPolicy{
          .requires_high_risk_confirmation = true,
          .safe_mode_enabled = true,
          .audit_level = "full",
          .allowed_tool_domains = {"builtin"},
      },
      profiles::OpsPolicy{
          .log_level = "info",
          .metrics_granularity = "detailed",
          .trace_sample_ratio = 1.0,
          .remote_diagnostics_enabled = false,
          .upgrade_strategy = "rolling",
      },
      2U);
}

inline runtime::AgentInitRequest make_true_integration_init_request(
    std::shared_ptr<runtime::RuntimeDependencySet> dependency_set,
    const std::string& runtime_instance_id,
    const std::string& profile_id,
        const std::string& boot_reason,
        const std::optional<std::string>& omitted_stage = std::nullopt) {
  runtime::AgentInitRequest request;
  request.runtime_instance_id = runtime_instance_id;
  request.profile_id = profile_id;
    request.policy_snapshot = make_true_integration_policy_snapshot(profile_id, omitted_stage);
  request.dependency_set = std::move(dependency_set);
  request.boot_reason = boot_reason;
  request.cold_start = true;
  return request;
}

}  // namespace dasall::tests::runtime_fixture
