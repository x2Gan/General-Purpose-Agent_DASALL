#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef DASALL_PROJECT_SOURCE_DIR
#error DASALL_PROJECT_SOURCE_DIR must be defined for cognition review regression coverage
#endif

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for cognition review regression coverage
#endif

#include "AgentFacade.h"
#include "CognitionRuntimeIntegrationFixture.h"
#include "RuntimeUnaryFixture.h"
#include "MockLLMManager.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::mocks::MockLLMManager;
using dasall::tests::runtime_fixture::cleanup_database_artifacts;
using dasall::tests::runtime_fixture::make_agent_request;
using dasall::tests::runtime_fixture::make_sqlite_config;
using dasall::tests::runtime_fixture::make_temp_database_path;
using dasall::tests::runtime_fixture::make_true_integration_dependency_set;
using dasall::tests::runtime_fixture::make_true_integration_init_request;
using dasall::tests::support::assert_true;

[[nodiscard]] const dasall::llm::LLMGenerateRequest* find_generate_request(
    const std::vector<dasall::llm::LLMGenerateRequest>& requests,
    std::string_view stage,
    std::string_view task_type) {
  for (const auto& request : requests) {
    if (request.stage == stage && request.task_type == task_type) {
      return &request;
    }
  }

  return nullptr;
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream stream(path);
  if (!stream.is_open()) {
    throw std::runtime_error("failed to open file: " + path.string());
  }

  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

void test_bridge_backed_flow_records_all_canonical_stage_requests() {
  const auto database_path =
      make_temp_database_path("dasall-cognition-review-regression-bridge");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
  auto dependency_set = make_true_integration_dependency_set(
      config,
      "session-037-review-bridge",
      "turn-037-review-bridge-001",
      "query cognition review regression bridge");

  auto mock_llm_manager = std::dynamic_pointer_cast<MockLLMManager>(dependency_set->llm_manager);
  assert_true(mock_llm_manager != nullptr,
              "review regression bridge case should expose MockLLMManager for request inspection");
  mock_llm_manager->clear_recorded_requests();

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(make_true_integration_init_request(
      dependency_set,
      "rt-037-review-bridge",
      "desktop_full",
      "cognition-review-regression-bridge"));
  assert_true(init_result.accepted,
              "review regression bridge case should initialize AgentFacade");

  const auto result = facade.handle(make_agent_request(
      "req-037-review-bridge",
      "session-037-review-bridge",
      "trace-037-review-bridge",
      "query cognition review regression bridge"));
  assert_true(result.status == dasall::contracts::AgentResultStatus::Completed,
              "review regression bridge case should complete through the live cognition path");

  const auto& requests = mock_llm_manager->generate_requests();
  assert_true(find_generate_request(requests, "planning", "plan") != nullptr,
              "review regression should fail if planning no longer invokes the cognition bridge");
  assert_true(find_generate_request(requests, "execution", "action_decision") != nullptr,
              "review regression should fail if execution no longer invokes the cognition bridge");
  assert_true(find_generate_request(requests, "reflection", "failure_analysis") != nullptr,
              "review regression should fail if reflection no longer invokes the cognition bridge");
  if (dependency_set->memory_manager != nullptr) {
    dependency_set->memory_manager->shutdown();
  }
  cleanup_database_artifacts(database_path);
}

void test_integration_registration_has_no_placeholder_alias() {
  const auto cmake_lists_path =
      std::filesystem::path(DASALL_PROJECT_SOURCE_DIR) / "tests/integration/cognition/CMakeLists.txt";
  const auto contents = read_text_file(cmake_lists_path);

  assert_true(contents.find("CognitionProfileCompatibilityIntegrationTest") == std::string::npos,
              "review regression should fail if the historical profile compatibility alias returns");
  assert_true(contents.find("dasall_add_cognition_integration_placeholder") == std::string::npos,
              "review regression should fail if the placeholder registration helper returns");
  assert_true(contents.find("cmake -E true") == std::string::npos,
              "review regression should fail if cognition integration registration falls back to empty commands");
}

void test_missing_canonical_route_is_rejected_at_runtime_init() {
  const auto database_path =
      make_temp_database_path("dasall-cognition-review-regression-missing-route");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
  auto dependency_set = make_true_integration_dependency_set(
      config,
      "session-037-review-missing-route",
      "turn-037-review-missing-route-001",
      "query cognition review regression missing route");

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(make_true_integration_init_request(
      dependency_set,
      "rt-037-review-missing-route",
      "desktop_full",
      "cognition-review-regression-missing-route",
      std::string{"response"}));
  assert_true(!init_result.accepted,
              "review regression should fail closed when the canonical response route is missing");
  assert_true(init_result.error_code ==
                  static_cast<std::int32_t>(dasall::contracts::ResultCode::PolicyDenied),
              "review regression should keep missing canonical route mapped to PolicyDenied");

  if (dependency_set->memory_manager != nullptr) {
    dependency_set->memory_manager->shutdown();
  }
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_bridge_backed_flow_records_all_canonical_stage_requests();
    test_integration_registration_has_no_placeholder_alias();
    test_missing_canonical_route_is_rejected_at_runtime_init();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}