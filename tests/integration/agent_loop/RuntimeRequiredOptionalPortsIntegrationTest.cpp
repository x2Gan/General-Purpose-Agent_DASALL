#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#ifndef DASALL_SQL_MEMORY_DIR
#error DASALL_SQL_MEMORY_DIR must be defined for runtime required/optional ports integration coverage
#endif

#include "AgentFacade.h"
#include "CognitionRuntimeIntegrationFixture.h"
#include "ICognitionEngine.h"
#include "IResponseBuilder.h"
#include "RuntimeUnaryFixture.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::runtime_fixture::cleanup_database_artifacts;
using dasall::tests::runtime_fixture::make_agent_request;
using dasall::tests::runtime_fixture::make_sqlite_config;
using dasall::tests::runtime_fixture::make_temp_database_path;
using dasall::tests::runtime_fixture::make_true_integration_dependency_set;
using dasall::tests::runtime_fixture::make_true_integration_init_request;
using dasall::tests::support::assert_true;

class OptionalPortGapCognitionEngine final : public dasall::cognition::ICognitionEngine {
 public:
  [[nodiscard]] dasall::cognition::CognitionDecisionResult decide(
      const dasall::cognition::CognitionStepRequest&) override {
    dasall::cognition::CognitionDecisionResult result;

    dasall::cognition::decision::ActionDecision decision;
    decision.decision_kind =
        dasall::cognition::decision::ActionDecisionKind::ExecuteAction;
    decision.selected_node_id = std::string{"runtime-optional-gap-node"};
    decision.rationale = std::string{"optional-port gap integration emits executable action"};
    decision.confidence = 0.88F;
    decision.tool_intent_hint = dasall::cognition::decision::ToolIntentHint{
        .tool_name = std::string{"agent.dataset"},
        .intent_summary = std::string{"exercise degraded unary path while optional ports are absent"},
        .argument_hints = {std::string{"query runtime optional ports integration"}},
        .evidence_refs = {std::string{"tests:runtime-required-optional-ports"}},
    };
    decision.response_outline = dasall::cognition::decision::ResponseOutline{
        .summary = std::string{"runtime degraded readiness should stay explicit"},
        .key_points = {std::string{"optional port gaps must remain auditable"}},
    };
    result.action_decision = std::move(decision);
    result.context_sufficiency = dasall::cognition::ContextSufficiencySignal{
        .context_sufficient = true,
        .context_confidence = 0.86F,
        .missing_evidence_hints = {},
        .recommend_context_reload = false,
    };
    return result;
  }

  [[nodiscard]] dasall::cognition::CognitionReflectionResult reflect(
      const dasall::cognition::ReflectionRequest&) override {
    return {};
  }
};

[[nodiscard]] bool has_tag(const dasall::contracts::AgentResult& result,
                           const std::string& expected_tag) {
  return result.tags.has_value() &&
         std::find(result.tags->begin(), result.tags->end(), expected_tag) !=
             result.tags->end();
}

[[nodiscard]] bool contains_value(const std::vector<std::string>& values,
                                  const std::string& expected_value) {
  return std::find(values.begin(), values.end(), expected_value) != values.end();
}

void test_runtime_required_optional_ports_integration_marks_degraded_runtime() {
  const auto database_path =
      make_temp_database_path("dasall-runtime-required-optional-ports");
  cleanup_database_artifacts(database_path);

  const auto config = make_sqlite_config(database_path, DASALL_SQL_MEMORY_DIR);
  auto dependency_set = make_true_integration_dependency_set(
      config,
      "session-runtime-required-optional-014",
      "turn-runtime-required-optional-014-001",
      "query runtime optional ports integration");
  dependency_set->llm_manager.reset();
  dependency_set->cognition_engine =
      std::make_shared<OptionalPortGapCognitionEngine>();
  dependency_set->response_builder = std::shared_ptr<dasall::cognition::IResponseBuilder>(
      dasall::cognition::create_response_builder().release());

  dasall::runtime::AgentFacade facade;
  const auto init_result = facade.init(make_true_integration_init_request(
      dependency_set,
      "rt-runtime-required-optional-014",
      "desktop_full",
      "runtime-required-optional-ports"));
  assert_true(init_result.accepted,
              "runtime required/optional ports integration should still initialize when only optional ports are missing");
  assert_true(init_result.degraded,
              "runtime required/optional ports integration should mark optional port gaps as degraded");
  assert_true(init_result.degraded_ready(),
              "runtime required/optional ports integration should project degraded-ready through AgentInitResult");
  assert_true(init_result.diagnostics.find("readiness=degraded") != std::string::npos,
              "runtime required/optional ports integration should surface degraded readiness in diagnostics");
  assert_true(init_result.diagnostics.find("missing_optional=knowledge,llm") != std::string::npos,
              "runtime required/optional ports integration should name missing knowledge/llm ports in diagnostics");
  assert_true(contains_value(init_result.degraded_reasons, "runtime_optional_port_gap"),
              "runtime required/optional ports integration should report the generic optional-port degraded reason");
  assert_true(contains_value(init_result.degraded_reasons,
                             "runtime_missing_optional:knowledge"),
              "runtime required/optional ports integration should report knowledge as a degraded reason");
  assert_true(contains_value(init_result.degraded_reasons,
                             "runtime_missing_optional:llm"),
              "runtime required/optional ports integration should report llm as a degraded reason");

  const auto result = facade.handle(make_agent_request(
      "req-runtime-required-optional-014",
      "session-runtime-required-optional-014",
      "trace-runtime-required-optional-014",
      "query runtime optional ports integration"));

  assert_true(result.status.has_value() &&
                  *result.status != dasall::contracts::AgentResultStatus::Failed,
              "runtime required/optional ports integration should keep degraded optional-port runs executable");
  assert_true(has_tag(result, "runtime_readiness:degraded"),
              "runtime required/optional ports integration should tag degraded readiness on AgentResult");
  assert_true(has_tag(result, "runtime_degraded_reason:optional_port_gap"),
              "runtime required/optional ports integration should tag the generic degraded reason on AgentResult");
  assert_true(has_tag(result, "knowledge_unavailable"),
              "runtime required/optional ports integration should audit knowledge gaps on AgentResult");
  assert_true(has_tag(result, "llm_unavailable"),
              "runtime required/optional ports integration should audit llm gaps on AgentResult");

  dependency_set->memory_manager->shutdown();
  cleanup_database_artifacts(database_path);
}

}  // namespace

int main() {
  try {
    test_runtime_required_optional_ports_integration_marks_degraded_runtime();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}