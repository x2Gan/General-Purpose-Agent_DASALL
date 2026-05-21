#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for runtime cancellation propagation coverage
#endif

#include "AgentFacade.h"
#include "IDataService.h"
#include "MockLLMManager.h"
#include "RuntimeErrorCode.h"
#include "RuntimeUnaryFixture.h"
#include "ToolManager.h"
#include "bridge/ToolServiceBridge.h"
#include "execution/BuiltinExecutorLane.h"
#include "registry/ToolRegistry.h"
#include "support/TestAssertions.h"
#include "tests/fixtures/runtime/CognitionRuntimeIntegrationFixture.h"

namespace {

using dasall::tests::runtime_fixture::cleanup_database_artifacts;
using dasall::tests::runtime_fixture::make_agent_request;
using dasall::tests::runtime_fixture::make_sqlite_config;
using dasall::tests::runtime_fixture::make_temp_database_path;
using dasall::tests::runtime_fixture::make_true_integration_dependency_set;
using dasall::tests::runtime_fixture::make_true_integration_init_request;
using dasall::tests::support::assert_true;

[[nodiscard]] std::int64_t current_time_ms() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

class SlowDataService final : public dasall::services::IDataService {
 public:
  explicit SlowDataService(const std::chrono::milliseconds delay) : delay_(delay) {}

  dasall::services::DataQueryResult query(
      const dasall::services::DataQueryRequest& request) override {
    ++query_calls;
    last_query_request = request;
    std::this_thread::sleep_for(delay_);
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

 private:
  std::chrono::milliseconds delay_;
};

[[nodiscard]] std::shared_ptr<dasall::tools::IToolManager>
make_slow_tool_manager(std::shared_ptr<SlowDataService> data_service) {
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
  if (!registered) {
    throw std::runtime_error("failed to register agent.dataset descriptor");
  }

  auto builtin_lane = std::make_shared<dasall::tools::execution::BuiltinExecutorLane>(
      dasall::tools::execution::BuiltinExecutorLaneDependencies{
          .registry = registry,
          .service_bridge = std::make_shared<dasall::tools::bridge::ToolServiceBridge>(),
          .execution_service = nullptr,
          .data_service = std::move(data_service),
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

  return std::make_shared<dasall::tools::ToolManager>(std::move(tool_dependencies));
}

void test_runtime_deadline_cancels_late_direct_llm_result() {
  const auto database_path = make_temp_database_path("dasall-runtime-cancel-direct-llm");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
  auto dependency_set = make_true_integration_dependency_set(
      config,
      "session-runtime-cancel-direct-llm",
      "turn-runtime-cancel-direct-llm-001",
      "query runtime deadline propagation direct llm");
  dependency_set->external_evidence = {
      "runtime:daemon.local-control-plane:required-live-baseline"};

  const auto llm_manager =
      std::dynamic_pointer_cast<dasall::tests::mocks::MockLLMManager>(
          dependency_set->llm_manager);
  if (!llm_manager) {
    throw std::runtime_error("direct llm cancellation test requires MockLLMManager");
  }

  llm_manager->set_generate_handler(
      [](const dasall::llm::LLMGenerateRequest& request) {
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        return dasall::tests::mocks::MockLLMManager::make_success_result(
            "late direct llm response",
            request.request.model_route.value_or(std::string{"mock-primary"}),
            request.request.request_id);
      });

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(make_true_integration_init_request(
      dependency_set,
      "rt-runtime-cancel-direct-llm",
      "desktop_full",
      "runtime-cancellation-direct-llm"));
  assert_true(init_result.accepted,
              "runtime direct llm cancellation integration should initialize live dependencies");

  auto request = make_agent_request(
      "req-runtime-cancel-direct-llm",
      "session-runtime-cancel-direct-llm",
      "trace-runtime-cancel-direct-llm",
      "query runtime deadline propagation direct llm");
  request.created_at = current_time_ms();
  request.timeout_ms = 60U;

  const auto result = facade.handle(request);

  assert_true(llm_manager->generate_call_count() == 1,
              "runtime direct llm cancellation integration should still enter the LLM path before deadline expiry");
  assert_true(llm_manager->last_request().has_value() &&
                  llm_manager->last_request()->request.timeout_ms.has_value() &&
                  *llm_manager->last_request()->request.timeout_ms < 100U,
              "runtime direct llm cancellation integration should clamp the LLM request timeout to the live request deadline");
  assert_true(result.status.has_value() &&
                  *result.status == dasall::contracts::AgentResultStatus::Timeout,
              "runtime direct llm cancellation integration should reject late direct LLM success as timeout");
  assert_true(result.error_info.has_value() && result.error_info->details.code.has_value() &&
                  *result.error_info->details.code ==
                      static_cast<int>(dasall::runtime::RuntimeErrorCode::RT_E_600_LLM_TIMEOUT),
              "runtime direct llm cancellation integration should surface RT_E_600 for late LLM results");

  dependency_set->memory_manager->shutdown();
  cleanup_database_artifacts(database_path);
}

void test_runtime_deadline_cancels_late_tool_result() {
  const auto database_path = make_temp_database_path("dasall-runtime-cancel-tool");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
  auto dependency_set = make_true_integration_dependency_set(
      config,
      "session-runtime-cancel-tool",
      "turn-runtime-cancel-tool-001",
      "query runtime deadline propagation tool round");
  auto slow_data_service = std::make_shared<SlowDataService>(std::chrono::milliseconds(120));
  dependency_set->tool_manager = make_slow_tool_manager(slow_data_service);
  dependency_set->external_evidence = {"runtime:cognition-integration"};

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(make_true_integration_init_request(
      dependency_set,
      "rt-runtime-cancel-tool",
      "desktop_full",
      "runtime-cancellation-tool-round"));
  assert_true(init_result.accepted,
              "runtime tool cancellation integration should initialize live dependencies");

  auto request = make_agent_request(
      "req-runtime-cancel-tool",
      "session-runtime-cancel-tool",
      "trace-runtime-cancel-tool",
      "query runtime deadline propagation tool round");
  request.created_at = current_time_ms();
  request.timeout_ms = 60U;

  const auto result = facade.handle(request);

  assert_true(slow_data_service->query_calls == 1,
              "runtime tool cancellation integration should enter the builtin query lane before the deadline expires");
  assert_true(slow_data_service->last_query_request.has_value() &&
                  slow_data_service->last_query_request->context.deadline_ms < 100U,
              "runtime tool cancellation integration should clamp ServiceCallContext.deadline_ms to the live request deadline");
  assert_true(result.status.has_value() &&
                  *result.status == dasall::contracts::AgentResultStatus::Timeout,
              "runtime tool cancellation integration should reject late tool observations as timeout");
  assert_true(result.error_info.has_value() && result.error_info->details.code.has_value() &&
                  *result.error_info->details.code ==
                      static_cast<int>(dasall::runtime::RuntimeErrorCode::RT_E_601_TOOL_TIMEOUT),
              "runtime tool cancellation integration should surface RT_E_601 for late tool results");

  dependency_set->memory_manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_runtime_deadline_cancels_late_direct_llm_result();
    test_runtime_deadline_cancels_late_tool_result();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}