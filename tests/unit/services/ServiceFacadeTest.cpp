#include <exception>
#include <iostream>
#include <type_traits>

#include "ServiceFacade.h"
#include "support/TestAssertions.h"

namespace {

dasall::services::ServiceCallContext make_context() {
  dasall::contracts::RuntimeBudget budget;
  budget.max_latency_ms = 5000;

  return dasall::services::ServiceCallContext{
      .request_id = "req-010",
      .session_id = "session-010",
      .trace_id = "trace-010",
      .tool_call_id = "tool-call-010",
      .goal_id = "goal-010",
      .budget_guard = budget,
      .deadline_ms = 9000,
  };
}

dasall::services::ExecutionCommandRequest make_command_request() {
  return dasall::services::ExecutionCommandRequest{
      .context = make_context(),
      .target = {
          .capability_id = "cap.exec",
          .target_id = "target-010",
      },
      .action = "toggle",
      .arguments_json = "{}",
      .idempotency_key = std::string("idem-010"),
  };
}

dasall::services::DataQueryRequest make_data_request() {
  return dasall::services::DataQueryRequest{
      .context = make_context(),
      .dataset = "inventory",
      .filters_json = "{}",
      .projection = "summary",
      .freshness = dasall::services::ServiceDataFreshness::allow_stale,
  };
}

void test_service_facade_implements_public_interfaces_and_delegates() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;
  using Facade = dasall::services::internal::ServiceFacade;

  static_assert(std::is_base_of_v<dasall::services::IExecutionService, Facade>,
                "ServiceFacade should implement IExecutionService");
  static_assert(std::is_base_of_v<dasall::services::IDataService, Facade>,
                "ServiceFacade should implement IDataService");

  dasall::services::internal::ServiceContextBuilder builder;
  bool execute_called = false;
  bool query_called = false;
  dasall::services::ServiceCallContext execute_context;
  dasall::services::ServiceCallContext query_context;

    auto dependencies = dasall::services::internal::ServiceFacadeDependencies{};
    dependencies.context_builder = &builder;
    dependencies.execute_command =
      [&](const dasall::services::ServiceCallContext& context,
        const dasall::services::ExecutionCommandRequest& request) {
        execute_called = true;
        execute_context = context;
        assert_equal(std::string("toggle"), request.action,
                     "execute should forward the action unchanged");
        return dasall::services::ExecutionCommandResult{
          .code = std::nullopt,
            .execution_id = "exec-010",
            .payload_json = "{\"status\":\"ok\"}",
            .side_effects = {"state.changed"},
            .compensation_hints = {"state.restore"},
            .error = std::nullopt,
        };
      };
  dependencies.query_data = [&](const dasall::services::ServiceCallContext& context,
                                const dasall::services::DataQueryRequest& request) {
        query_called = true;
        query_context = context;
        assert_equal(std::string("inventory"), request.dataset,
                     "query should forward dataset unchanged");
        return dasall::services::DataQueryResult{
          .code = std::nullopt,
            .rows_json = "[]",
            .from_cache = true,
            .error = std::nullopt,
        };
          };

        dasall::services::internal::ServiceFacade facade(std::move(dependencies));

  dasall::services::IExecutionService* execution_service = &facade;
  dasall::services::IDataService* data_service = &facade;

  const auto execution_result = execution_service->execute(make_command_request());
  const auto data_result = data_service->query(make_data_request());

  assert_true(execute_called, "execute should delegate to the injected command handler");
  assert_true(query_called, "query should delegate to the injected data handler");
  assert_equal(std::string("exec-010"), execution_result.execution_id,
               "execute should return the delegated execution result");
  assert_true(data_result.from_cache, "query should return the delegated data result");
  assert_equal(std::string("req-010"), execute_context.request_id,
               "execute should receive normalized request_id");
  assert_equal(9000, static_cast<int>(execute_context.deadline_ms),
               "execute should receive normalized deadline_ms");
  assert_equal(std::string("goal-010"), query_context.goal_id,
               "query should receive normalized goal_id");
}

void test_service_facade_rejects_invalid_context_before_delegate() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::services::internal::ServiceContextBuilder builder;
  bool execute_called = false;

    auto dependencies = dasall::services::internal::ServiceFacadeDependencies{};
    dependencies.context_builder = &builder;
    dependencies.execute_command =
      [&](const dasall::services::ServiceCallContext&,
        const dasall::services::ExecutionCommandRequest&) {
        execute_called = true;
        return dasall::services::ExecutionCommandResult{};
      };

    dasall::services::internal::ServiceFacade facade(std::move(dependencies));

  auto invalid_request = make_command_request();
  invalid_request.context.request_id.clear();

  const auto result = facade.execute(invalid_request);

  assert_true(!execute_called,
              "execute should not reach the injected handler when context normalization fails");
  assert_equal(static_cast<int>(dasall::contracts::ResultCode::ValidationFieldMissing),
               static_cast<int>(result.code.value_or(dasall::contracts::ResultCode::ToolExecutionFailed)),
               "invalid context should surface a validation result code");
  assert_true(result.error.has_value(), "invalid context should surface structured error info");
  assert_equal(std::string("request_id is required"), result.error->details.message,
               "invalid context should preserve the builder error message");
}

}  // namespace

int main() {
  try {
    test_service_facade_implements_public_interfaces_and_delegates();
    test_service_facade_rejects_invalid_context_before_delegate();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}