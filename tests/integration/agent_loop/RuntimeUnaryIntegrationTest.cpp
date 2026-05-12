#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for runtime unary integration coverage
#endif

#include "AgentFacade.h"
#include "IDataService.h"
#include "ICognitionEngine.h"
#include "IMemoryManager.h"
#include "IResponseBuilder.h"
#include "RuntimeUnaryFixture.h"
#include "ToolManager.h"
#include "bridge/ToolServiceBridge.h"
#include "execution/BuiltinExecutorLane.h"
#include "registry/ToolRegistry.h"
#include "support/TestAssertions.h"
#include "writeback/MemoryWritebackRequest.h"

namespace {

using dasall::tests::runtime_fixture::make_agent_request;
using dasall::tests::support::assert_true;

class CaptureDataService final : public dasall::services::IDataService {
 public:
  dasall::services::DataQueryResult query(
      const dasall::services::DataQueryRequest& request) override {
    ++query_calls;
    last_query_request = request;
    return dasall::services::DataQueryResult{
        .code = std::nullopt,
        .rows_json = std::string{"{\"dataset\":\""} + request.dataset +
                     "\",\"request_id\":\"" + request.context.request_id +
                     "\",\"trace_id\":\"" + request.context.trace_id + "\"}",
        .from_cache = false,
        .error = std::nullopt,
    };
  }

  dasall::services::DataCatalogResult list_capabilities(
      const dasall::services::DataCatalogRequest& request) override {
    return dasall::services::DataCatalogResult{
        .code = std::nullopt,
        .catalog_json = std::string{"{\"target_class\":\""} + request.target_class +
                        "\"}",
        .error = std::nullopt,
    };
  }

  int query_calls = 0;
  std::optional<dasall::services::DataQueryRequest> last_query_request;
};

struct RuntimeUnaryIntegrationFixture {
  std::shared_ptr<dasall::runtime::RuntimeDependencySet> dependency_set;
  std::shared_ptr<CaptureDataService> data_service;
};

[[nodiscard]] std::filesystem::path make_temp_database_path(const std::string& stem) {
  const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
  return std::filesystem::temp_directory_path() /
         (stem + "-" + std::to_string(timestamp) + ".db");
}

void cleanup_database_artifacts(const std::filesystem::path& database_path) {
  (void)std::filesystem::remove(database_path);
  (void)std::filesystem::remove(database_path.string() + "-wal");
  (void)std::filesystem::remove(database_path.string() + "-shm");
}

[[nodiscard]] dasall::memory::MemoryConfig make_sqlite_config(
    const std::filesystem::path& database_path) {
  dasall::memory::MemoryConfig config;
  config.storage.backend = dasall::memory::StorageBackend::Sqlite;
  config.storage.db_path = database_path.string();
  config.storage.migrations_dir = DASALL_SQL_MEMORY_DIR;
  config.vector.enabled = false;
  return config;
}

[[nodiscard]] RuntimeUnaryIntegrationFixture make_true_integration_dependency_set(
    const dasall::memory::MemoryConfig& config) {
  auto dependency_set = std::make_shared<dasall::runtime::RuntimeDependencySet>();
  auto data_service = std::make_shared<CaptureDataService>();

  std::shared_ptr<dasall::memory::IMemoryManager> memory_manager(
      dasall::memory::create_memory_manager(config));
  const auto init_code = memory_manager->init(config);
  assert_true(static_cast<int>(init_code) == 0,
              "runtime unary integration should initialize the sqlite-backed memory manager");

    dasall::memory::MemoryWritebackRequest writeback_request;
    writeback_request.session_id = "session-027";
    writeback_request.turn.turn_id = "turn-027-001";
    writeback_request.turn.session_id = "session-027";
    writeback_request.turn.user_input = "query runtime unary integration";
    writeback_request.turn.agent_response = "seed runtime true integration context";
    writeback_request.summary_candidate = dasall::contracts::SummaryMemory{};
    writeback_request.summary_candidate->summary_text =
      "runtime unary integration seeded context";
    writeback_request.summary_candidate->confirmed_facts =
      std::vector<std::string>{"runtime unary integration context is available"};

    dasall::memory::FactCandidate fact_candidate;
    fact_candidate.fact.fact_text = "runtime unary integration context is available";
    fact_candidate.fact.fact_type = "status";
    fact_candidate.fact.confidence_score = 90U;
    fact_candidate.fact.source_turn_ids = std::vector<std::string>{"turn-027-001"};
    fact_candidate.extraction_source = "integration-test";
    writeback_request.fact_candidates.push_back(std::move(fact_candidate));

    const auto writeback_result = memory_manager->write_back(writeback_request);
    assert_true(!writeback_result.result_code.has_value(),
          "runtime unary integration should seed the memory manager before context assembly");

  dependency_set->memory_manager = std::move(memory_manager);
  dependency_set->cognition_engine =
      std::shared_ptr<dasall::cognition::ICognitionEngine>(
          dasall::cognition::create_cognition_engine());
  dependency_set->response_builder =
      std::shared_ptr<dasall::cognition::IResponseBuilder>(
          dasall::cognition::create_response_builder());

    auto registry = std::make_shared<dasall::tools::registry::ToolRegistry>();
    const auto registered = registry->register_builtin(
      dasall::contracts::ToolDescriptor{
        .tool_name = std::string{"agent.dataset"},
        .display_name = std::string{"Agent Dataset"},
        .category = dasall::contracts::ToolCategory::Information,
        .capability_tier = dasall::contracts::ToolCapabilityTier::Preview,
        .is_read_only = true,
        .supports_compensation = false,
        .default_timeout_ms = 30000U,
        .input_schema_ref = std::string{"schema://tools/agent.dataset/input/v1"},
        .output_schema_ref = std::string{"schema://tools/agent.dataset/output/v1"},
        .required_scopes = std::vector<std::string>{"tools.read"},
        .tags = std::vector<std::string>{"builtin", "query"},
        .version = std::string{"1.0.0"},
      });
    assert_true(registered,
          "runtime unary integration should register the builtin query descriptor");

      auto builtin_lane = std::make_shared<dasall::tools::execution::BuiltinExecutorLane>(
        dasall::tools::execution::BuiltinExecutorLaneDependencies{
          .registry = registry,
          .service_bridge = std::make_shared<dasall::tools::bridge::ToolServiceBridge>(),
          .execution_service = nullptr,
          .data_service = data_service,
          .now_ms = {},
        });

    dasall::tools::manager::ToolManagerDependencies tool_dependencies;
    tool_dependencies.registry = std::move(registry);
      tool_dependencies.executor = [builtin_lane](const auto& execution_request) {
      return builtin_lane->execute(
        execution_request.tool_ir,
        dasall::tools::ToolExecutionContext{
          .invocation_context = execution_request.invocation_context,
          .lane_key = execution_request.route_decision.lane_key,
        });
      };
    dependency_set->tool_manager =
      std::make_shared<dasall::tools::ToolManager>(std::move(tool_dependencies));
    dependency_set->local_stub_ports.main_loop_exit =
        dasall::runtime::RuntimeStubMainLoopExit::ToolRound;
  dependency_set->visible_tools = {"agent.dataset"};
  dependency_set->external_evidence = {"runtime:true-unary-integration"};
  return RuntimeUnaryIntegrationFixture{
      .dependency_set = std::move(dependency_set),
      .data_service = std::move(data_service),
  };
}

  [[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot>
  make_true_integration_policy_snapshot() {
    dasall::profiles::ModelProfile model_profile;
    model_profile.stage_routes.emplace("main", dasall::profiles::ModelRoutePolicy{
                           .route = "mock-primary",
                           .fallback_route = std::string{"mock-fallback"},
                           .streaming_enabled = false,
                         });

    return std::make_shared<const dasall::profiles::RuntimePolicySnapshot>(
      1U,
      "desktop_full",
      dasall::contracts::RuntimeBudget{
        .max_tokens = 2048U,
        .max_turns = 6U,
        .max_tool_calls = 2U,
        .max_latency_ms = 2000U,
        .max_replan_count = 2U,
      },
      std::move(model_profile),
      dasall::profiles::TokenBudgetPolicy{
        .max_input_tokens = 2048U,
        .max_output_tokens = 512U,
        .max_history_turns = 8U,
        .compression_threshold = 1024U,
      },
      dasall::profiles::PromptPolicy{
        .allowed_prompt_releases = {"stable"},
        .trusted_sources = {"runtime"},
          .tool_visibility_rules = {"builtin:agent.dataset"},
      },
      dasall::profiles::CapabilityCachePolicy{
        .refresh_interval_ms = 1000,
        .expire_after_ms = 5000,
        .stale_read_allowed = false,
        .failure_backoff_ms = 100,
      },
      dasall::profiles::DegradePolicy{
        .fallback_chain = {"safe_mode"},
        .allow_model_failover = true,
        .allow_budget_degrade = true,
      },
      dasall::profiles::TimeoutPolicy{
        .llm = dasall::profiles::TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
        .tool = dasall::profiles::TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
        .mcp = dasall::profiles::TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
        .workflow = dasall::profiles::TimeoutBudget{.timeout_ms = 1000, .retry_budget = 1U, .circuit_breaker_threshold = 2U},
      },
      dasall::profiles::ExecutionPolicy{
        .requires_high_risk_confirmation = true,
        .safe_mode_enabled = true,
        .audit_level = "full",
          .allowed_tool_domains = {"builtin"},
      },
      dasall::profiles::OpsPolicy{
        .log_level = "info",
        .metrics_granularity = "detailed",
        .trace_sample_ratio = 1.0,
        .remote_diagnostics_enabled = false,
        .upgrade_strategy = "rolling",
      },
      2U);
  }

  [[nodiscard]] dasall::runtime::AgentInitRequest make_true_integration_init_request(
    std::shared_ptr<dasall::runtime::RuntimeDependencySet> dependency_set) {
    dasall::runtime::AgentInitRequest request;
    request.runtime_instance_id = "rt-027";
    request.profile_id = "desktop_full";
    request.policy_snapshot = make_true_integration_policy_snapshot();
    request.dependency_set = std::move(dependency_set);
    request.boot_reason = "runtime-unary-integration";
    request.cold_start = true;
    return request;
  }

void test_runtime_unary_integration_traverses_live_ports() {
  const auto database_path = make_temp_database_path("dasall-runtime-unary-integration");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path);
  auto fixture = make_true_integration_dependency_set(config);
  auto dependency_set = fixture.dependency_set;
  const auto readiness = dependency_set->describe_readiness();
  assert_true(readiness.has_required_ports,
              "runtime unary integration should assemble live required ports before init: " +
                  readiness.summary());

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(make_true_integration_init_request(dependency_set));
  assert_true(init_result.accepted,
              "runtime unary integration should initialize AgentFacade with live dependency ports");

  const auto result = facade.handle(
      make_agent_request("req-027", "session-027", "trace-027", "query runtime unary integration"));

  const auto status_name = [&result]() {
    if (!result.status.has_value()) {
      return std::string{"<unset>"};
    }

    switch (*result.status) {
      case dasall::contracts::AgentResultStatus::Unspecified:
        return std::string{"Unspecified"};
      case dasall::contracts::AgentResultStatus::Completed:
        return std::string{"Completed"};
      case dasall::contracts::AgentResultStatus::Failed:
        return std::string{"Failed"};
      case dasall::contracts::AgentResultStatus::PartiallyCompleted:
        return std::string{"PartiallyCompleted"};
      case dasall::contracts::AgentResultStatus::Cancelled:
        return std::string{"Cancelled"};
      case dasall::contracts::AgentResultStatus::Timeout:
        return std::string{"Timeout"};
    }

    return std::string{"<unknown>"};
  }();
  const auto failure_detail = std::string{"status="} + status_name +
                              " result_code=" +
                              (result.result_code.has_value()
                                   ? std::to_string(*result.result_code)
                                   : std::string{"<unset>"}) +
                              " response_text=" +
                              result.response_text.value_or(std::string{"<unset>"}) +
                              " error_stage=" +
                                (result.error_info.has_value() &&
                                     !result.error_info->details.stage.empty()
                                   ? result.error_info->details.stage
                                   : std::string{"<unset>"}) +
                              " error_message=" +
                                (result.error_info.has_value() &&
                                     !result.error_info->details.message.empty()
                                   ? result.error_info->details.message
                                   : std::string{"<unset>"});

  assert_true(result.status == dasall::contracts::AgentResultStatus::Completed,
              "runtime unary integration should complete successfully on the true-port path: " +
                  failure_detail);
  assert_true(result.task_completed.value_or(false),
              "runtime unary integration should mark the completed result as task_completed");
  assert_true(result.checkpoint_ref.has_value() && !result.checkpoint_ref->empty(),
              "runtime unary integration should materialize a completion checkpoint reference");
  assert_true(result.goal_id.has_value() && !result.goal_id->empty(),
              "runtime unary integration should preserve the resolved goal anchor");
  assert_true(result.response_text.has_value() &&
                  result.response_text->find("runtime unary integration completed:") != std::string::npos &&
                  result.response_text->find("\"dataset\":\"agent.dataset\"") != std::string::npos,
          "runtime unary integration should return the projected builtin query payload in AgentResult: " +
            failure_detail);
  assert_true(fixture.data_service->query_calls == 1,
              "runtime unary integration should dispatch exactly one builtin query through IDataService");
  assert_true(fixture.data_service->last_query_request.has_value(),
              "runtime unary integration should preserve the service query request for caller-boundary checks");
  assert_true(fixture.data_service->last_query_request->dataset == "agent.dataset",
              "runtime unary integration should preserve the selected builtin dataset name");
  assert_true(fixture.data_service->last_query_request->context.request_id == "req-027",
              "runtime unary integration should project runtime request_id into ServiceCallContext");
  assert_true(fixture.data_service->last_query_request->context.session_id == "session-027",
              "runtime unary integration should project runtime session_id into ServiceCallContext");
  assert_true(fixture.data_service->last_query_request->context.trace_id == "trace-027",
              "runtime unary integration should project runtime trace_id into ServiceCallContext");
  assert_true(fixture.data_service->last_query_request->context.tool_call_id == "tool-call-req-027",
              "runtime unary integration should derive a stable tool_call_id before entering IDataService");
  assert_true(fixture.data_service->last_query_request->context.goal_id == *result.goal_id,
              "runtime unary integration should preserve the resolved goal id across the tools->services boundary");
  assert_true(fixture.data_service->last_query_request->context.budget_guard.has_value(),
              "runtime unary integration should attach the runtime budget guard to ServiceCallContext");
  assert_true(fixture.data_service->last_query_request->context.deadline_ms == 1000U,
              "runtime unary integration should project the tool timeout policy into ServiceCallContext.deadline_ms");

  dependency_set->memory_manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_runtime_unary_integration_traverses_live_ports();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}