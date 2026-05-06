#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "RuntimeDependencySet.h"
#include "support/TestAssertions.h"

namespace {

template <typename T>
std::shared_ptr<T> make_live_port() {
  auto holder = std::make_shared<int>(1);
  return std::shared_ptr<T>(holder, reinterpret_cast<T*>(holder.get()));
}

bool contains_value(const std::vector<std::string>& values,
                    const std::string& expected) {
  return std::find(values.begin(), values.end(), expected) != values.end();
}

void test_runtime_dependency_set_fail_closes_when_required_ports_are_missing() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::runtime::RuntimeDependencySet dependencies;
  const auto readiness = dependencies.describe_readiness();

  assert_true(!dependencies.has_live_unary_ports(),
              "runtime dependency set should fail the live unary check when required ports are missing");
  assert_true(!readiness.has_required_ports,
              "runtime dependency set should mark required ports as unavailable when any core port is missing");
  assert_true(!readiness.default_unary_ready(),
              "runtime dependency set should not claim default unary ready without required ports");
  assert_true(!readiness.degraded,
              "runtime dependency set should not report degraded readiness when it cannot even satisfy required ports");
  assert_equal("fail_closed",
               readiness.readiness_state(),
               "runtime dependency set should name the missing-required state as fail_closed");
  assert_true(contains_value(readiness.missing_required_ports, "memory"),
              "runtime dependency set should report memory as a missing required port");
  assert_true(contains_value(readiness.missing_required_ports, "cognition"),
              "runtime dependency set should report cognition as a missing required port");
  assert_true(contains_value(readiness.missing_required_ports, "response_builder"),
              "runtime dependency set should report response_builder as a missing required port");
  assert_true(contains_value(readiness.missing_required_ports, "tools"),
              "runtime dependency set should report tools as a missing required port");
  assert_true(readiness.summary().find("missing_optional=knowledge,llm") != std::string::npos,
              "runtime dependency set should surface optional port gaps in the readiness summary");
}

void test_runtime_dependency_set_reports_degraded_when_only_optional_ports_are_missing() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::runtime::RuntimeDependencySet dependencies;
  dependencies.memory_manager = make_live_port<dasall::memory::IMemoryManager>();
  dependencies.cognition_engine = make_live_port<dasall::cognition::ICognitionEngine>();
  dependencies.response_builder = make_live_port<dasall::cognition::IResponseBuilder>();
  dependencies.tool_manager = make_live_port<dasall::tools::IToolManager>();

  const auto readiness = dependencies.describe_readiness();

  assert_true(dependencies.has_live_unary_ports(),
              "runtime dependency set should keep has_live_unary_ports focused on required core ports");
  assert_true(readiness.has_required_ports,
              "runtime dependency set should mark required ports as available once the core seams are live");
  assert_true(!readiness.has_optional_ports,
              "runtime dependency set should report optional ports missing when knowledge and llm are absent");
  assert_true(readiness.degraded,
              "runtime dependency set should degrade rather than fail-close when only optional ports are missing");
  assert_true(!readiness.default_unary_ready(),
              "runtime dependency set should not claim default ready while optional ports are still missing");
  assert_equal("degraded",
               readiness.readiness_state(),
               "runtime dependency set should name the required-only state as degraded");
  assert_true(readiness.summary().find("missing_optional=knowledge,llm") != std::string::npos,
              "runtime dependency set should surface the degraded optional-port gap in the readiness summary");
}

void test_runtime_dependency_set_reports_ready_when_all_ports_are_live() {
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  dasall::runtime::RuntimeDependencySet dependencies;
  dependencies.memory_manager = make_live_port<dasall::memory::IMemoryManager>();
  dependencies.cognition_engine = make_live_port<dasall::cognition::ICognitionEngine>();
  dependencies.response_builder = make_live_port<dasall::cognition::IResponseBuilder>();
  dependencies.tool_manager = make_live_port<dasall::tools::IToolManager>();
  dependencies.knowledge_service = make_live_port<dasall::knowledge::IKnowledgeService>();
  dependencies.llm_manager = make_live_port<dasall::llm::ILLMManager>();

  const auto readiness = dependencies.describe_readiness();

  assert_true(dependencies.has_live_unary_ports(),
              "runtime dependency set should keep core-port liveness once all ports are connected");
  assert_true(readiness.has_required_ports,
              "runtime dependency set should keep required ports marked live when all seams are present");
  assert_true(readiness.has_optional_ports,
              "runtime dependency set should mark optional ports live when knowledge and llm are present");
  assert_true(!readiness.degraded,
              "runtime dependency set should clear degraded once optional ports are restored");
  assert_true(readiness.default_unary_ready(),
              "runtime dependency set should report default unary ready once both required and optional ports are live");
  assert_equal("ready",
               readiness.readiness_state(),
               "runtime dependency set should name the fully live state as ready");
  assert_equal("ready",
               readiness.summary(),
               "runtime dependency set should emit a compact readiness summary when no port gaps remain");
}

}  // namespace

int main() {
  try {
    test_runtime_dependency_set_fail_closes_when_required_ports_are_missing();
    test_runtime_dependency_set_reports_degraded_when_only_optional_ports_are_missing();
    test_runtime_dependency_set_reports_ready_when_all_ports_are_live();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}