#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include "ICognitionEngine.h"
#include "IKnowledgeService.h"
#include "IMemoryManager.h"
#include "IResponseBuilder.h"
#include "IToolManager.h"
#include "MockCognitionFixture.h"
#include "ProfileCatalog.h"
#include "RuntimeDependencySet.h"
#include "RuntimeLiveDependencyComposition.h"
#include "RuntimePolicyProvider.h"
#include "logging/FileLogReader.h"
#include "logging/LogQueryService.h"
#include "logging/LoggingFacade.h"
#include "support/TestAssertions.h"
#include "telemetry/RuntimeTelemetryBridge.h"

namespace {

namespace fs = std::filesystem;

using dasall::infra::logging::LogFlushDeadline;
using dasall::infra::logging::LoggingFacade;
using dasall::tests::mocks::MockCognitionFixture;
using dasall::tests::mocks::MockCognitionFixtureOptions;
using dasall::tests::mocks::StructuredExecutionPayloadScenario;
using dasall::tests::mocks::StructuredPlanningPayloadScenario;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

constexpr char kCompositionOwner[] = "gateway.http-unary";
constexpr char kProfileId[] = "desktop_full";
constexpr char kSessionId[] = "session-key-subsystem-production-logging";
constexpr char kTraceId[] = "trace-key-subsystem-production-logging";

struct ScopedTempDir {
  fs::path path;
    bool preserve = false;

  ~ScopedTempDir() {
        if (preserve) {
            return;
        }
    std::error_code error;
    fs::remove_all(path, error);
  }
};

[[nodiscard]] std::optional<fs::path> artifact_root_override() {
    const char* value = std::getenv("DASALL_TEST_ARTIFACT_ROOT");
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }

    return fs::path(value);
}

[[nodiscard]] ScopedTempDir make_temp_root() {
    if (const auto override_path = artifact_root_override(); override_path.has_value()) {
        std::error_code error;
        fs::remove_all(*override_path, error);
        fs::create_directories(*override_path);
        return ScopedTempDir{
                .path = *override_path,
                .preserve = true,
        };
    }

  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = fs::temp_directory_path() /
                                        ("dasall-key-subsystem-production-logging-" + std::to_string(nonce));
    fs::create_directories(path);
    return ScopedTempDir{
            .path = path,
            .preserve = false,
    };
}

[[nodiscard]] fs::path repository_root() {
  return fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

void copy_memory_assets_only(const fs::path& assets_root) {
    fs::create_directories(assets_root / "sql");
    fs::copy(repository_root() / "sql" / "memory",
                     assets_root / "sql" / "memory",
                     fs::copy_options::recursive);
}

void copy_installed_runtime_assets(const fs::path& assets_root) {
    copy_memory_assets_only(assets_root);
    fs::copy(repository_root() / "profiles",
                     assets_root / "profiles",
                     fs::copy_options::recursive);
    fs::copy(repository_root() / "skills",
                     assets_root / "skills",
                     fs::copy_options::recursive);
    fs::create_directories(assets_root / "llm");
    fs::copy(repository_root() / "llm" / "assets" / "providers",
                     assets_root / "llm" / "providers",
                     fs::copy_options::recursive);
}

[[nodiscard]] std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot> load_snapshot(
    const std::string& profile_id) {
  const dasall::profiles::ProfileCatalog catalog(repository_root() / "profiles");
  const dasall::profiles::RuntimePolicyProvider provider(catalog);
  const auto snapshot_result = provider.load_snapshot(
      dasall::profiles::RuntimePolicyLoadRequest{
          .profile_id = profile_id,
      });
  assert_true(snapshot_result.ok() && snapshot_result.snapshot != nullptr,
              "key subsystem production logging e2e should load the runtime profile snapshot");
  return snapshot_result.snapshot;
}

[[nodiscard]] std::string read_text(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

[[nodiscard]] dasall::infra::logging::LogQueryAccessContext make_access_context(
    const std::string& request_id,
    const std::string& trace_id) {
  return dasall::infra::logging::LogQueryAccessContext{
      .actor_ref = std::string("ops-user"),
      .consumer_module = std::string("diagnostics"),
      .policy_decision_ref = dasall::infra::policy::PolicyDecisionRef{
          .decision = dasall::infra::policy::PolicyDecision::Allow,
          .reason_code = std::string("allow_diag_pull"),
          .matched_rule_ids = {std::string("key-subsystem-logging-rule")},
          .snapshot_id = std::string("policy-snapshot-key-subsystem-logging"),
          .generation = 11,
          .evidence_ref = std::string("policy://infra/logging/key-subsystem/e2e"),
          .warnings = {},
      },
      .infra_context = dasall::infra::InfraContext{
          .request_id = request_id,
          .session_id = std::string(kSessionId),
          .trace_id = trace_id,
          .task_id = std::string("task-key-subsystem-production-logging"),
          .parent_task_id = std::string("parent-key-subsystem-production-logging"),
          .lease_id = std::string("lease-key-subsystem-production-logging"),
      },
  };
}

[[nodiscard]] dasall::memory::MemoryWritebackRequest make_memory_writeback_request() {
  dasall::memory::MemoryWritebackRequest request;
  request.session_id = kSessionId;
  request.turn.turn_id = "turn-key-subsystem-memory-001";
  request.turn.session_id = kSessionId;
  request.turn.user_input = "user_input=memory-secret-input";
  request.turn.agent_response =
      "agent_response=memory-secret-output token=memory-secret-token";
  request.turn.created_at = 1713100000100;
  request.summary_candidate = dasall::contracts::SummaryMemory{};
  request.summary_candidate->summary_text = "summary-secret:key-subsystem";
  request.summary_candidate->confirmed_facts = {
      "fact_text=memory-secret-fact",
  };

  dasall::memory::FactCandidate fact_candidate;
  fact_candidate.fact.fact_text = "fact_text=memory-secret-fact";
  fact_candidate.fact.fact_type = "status";
  fact_candidate.fact.confidence_score = 91U;
  fact_candidate.fact.source_turn_ids = {std::string("turn-key-subsystem-memory-001")};
  fact_candidate.extraction_source = "integration-test";
  request.fact_candidates.push_back(std::move(fact_candidate));

  return request;
}

[[nodiscard]] dasall::memory::MemoryContextRequest make_memory_context_request() {
  return dasall::memory::MemoryContextRequest{
      .request_id = "req-key-subsystem-memory-context",
      .session_id = kSessionId,
      .stage = "reasoning",
      .goal_summary = "goal_summary=memory-secret-goal",
      .constraints_summary = "constraints remain owner-local",
      .latest_observation_digest_summary =
          "latest_observation_digest_summary=memory-secret-digest",
      .visible_tools = {"agent.dataset", "agent.terminal"},
      .token_budget_hint = 320U,
      .latency_budget_ms = 200U,
      .external_evidence = {"external_evidence=memory-secret-evidence"},
      .retrieval_evidence_refs = {
          dasall::contracts::RetrievalEvidenceRef{
              .evidence_ref = "retrieval-secret-ref",
              .source_ref = "knowledge://key-subsystem-production-logging",
              .source_kind = "knowledge",
              .summary_text = "retrieval-summary-should-not-log",
              .trust_level = "high",
              .freshness = "fresh",
              .anchor_locator = std::string("turn:key-subsystem-production-logging"),
          },
      },
  };
}

[[nodiscard]] dasall::knowledge::KnowledgeQuery make_success_query() {
  dasall::knowledge::KnowledgeQuery query;
  query.request_id = "req-key-subsystem-knowledge-success";
  query.session_id = kSessionId;
  query.query_text = "DeepSeek Chat";
  query.query_kind = dasall::knowledge::KnowledgeQueryKind::FactLookup;
  query.top_k = 3U;
  query.max_context_projection_items = 3U;
  return query;
}

[[nodiscard]] dasall::knowledge::KnowledgeQuery make_invalid_query() {
  auto query = make_success_query();
  query.request_id = "req-key-subsystem-knowledge-invalid";
  query.query_text.clear();
  return query;
}

[[nodiscard]] dasall::knowledge::KnowledgeQuery make_fallback_query() {
    auto query = make_success_query();
    query.request_id.clear();
    query.query_text = "knowledge-secret-fallback-body";
    return query;
}

[[nodiscard]] dasall::contracts::ToolRequest make_dataset_request() {
  return dasall::contracts::ToolRequest{
      .request_id = std::string("req-key-subsystem-services-query"),
      .tool_call_id = std::string("call-key-subsystem-services-query"),
      .tool_name = std::string("agent.dataset"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::InformationQuery,
      .arguments_payload = std::string("{\"query\":\"services-secret-query\"}"),
      .created_at = 1713100000200,
      .goal_id = std::string("goal-key-subsystem-services-query"),
      .worker_task_id = std::string("worker-key-subsystem-services-query"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-key-subsystem-services-query"),
      .tags = std::vector<std::string>{"integration", "services", "query"},
  };
}

[[nodiscard]] dasall::contracts::ToolRequest make_terminal_request() {
  return dasall::contracts::ToolRequest{
      .request_id = std::string("req-key-subsystem-services-exec"),
      .tool_call_id = std::string("call-key-subsystem-services-exec"),
      .tool_name = std::string("agent.terminal"),
      .invocation_kind = dasall::contracts::ToolInvocationKind::Action,
      .arguments_payload =
          std::string("{\"command\":\"echo services-secret-terminal\"}"),
      .created_at = 1713100000201,
      .goal_id = std::string("goal-key-subsystem-services-exec"),
      .worker_task_id = std::string("worker-key-subsystem-services-exec"),
      .runtime_budget = std::nullopt,
      .timeout_ms = 2500U,
      .idempotency_key = std::string("idem-key-subsystem-services-exec"),
      .tags = std::vector<std::string>{"integration", "services", "execution"},
  };
}

[[nodiscard]] dasall::tools::ToolInvocationContext make_tool_context(
    const dasall::profiles::RuntimePolicySnapshot* snapshot,
    std::optional<std::vector<dasall::tools::ToolConfirmationFact>> confirmation_facts =
        std::nullopt) {
  return dasall::tools::ToolInvocationContext{
      .caller_domain = std::string("runtime.gateway"),
      .session_id = std::string(kSessionId),
      .profile_snapshot = snapshot,
      .trace = {
          .trace_id = std::string(kTraceId),
          .span_id = std::nullopt,
          .parent_span_id = std::nullopt,
      },
      .confirmation_facts = std::move(confirmation_facts),
      .request_timeout_budget_ms = std::nullopt,
  };
}

void test_key_subsystem_production_logging_e2e_persists_redacted_queryable_events() {
  using dasall::cognition::create_cognition_engine;
  using dasall::cognition::create_response_builder;
  using dasall::infra::logging::FileLogReader;
  using dasall::infra::logging::FileLogReaderOptions;
  using dasall::infra::logging::LogQueryRequest;
  using dasall::infra::logging::LogQuerySelectorKind;
  using dasall::infra::logging::LogQueryService;
  using dasall::infra::logging::LogQueryServiceOptions;
  using dasall::runtime::RuntimeState;
  using dasall::runtime::RuntimeTelemetryContext;

    ScopedTempDir temp_root = make_temp_root();
    const auto assets_root = temp_root.path / "assets";
  const auto state_root = temp_root.path / "state";
  const auto runtime_log_path = state_root / "logging" / "runtime.log";
  const auto artifact_root = state_root / "query-artifacts";
    copy_installed_runtime_assets(assets_root);
  fs::create_directories(state_root);

  const auto snapshot = load_snapshot(kProfileId);
  const auto composition = dasall::apps::runtime_support::compose_minimal_live_dependency_set(
      snapshot,
      kCompositionOwner,
      dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions{
          .readonly_assets_root_override = assets_root,
          .runtime_library_root_override = {},
          .state_root_override = state_root,
          .build_dense_snapshot_override = {},
          .create_vector_recall_store_override = {},
          .create_query_encoder_override = {},
          .knowledge_query_encoder_transport_override = nullptr,
          .knowledge_refresh_timer = nullptr,
          .knowledge_refresh_source_provider = nullptr,
      });
  assert_true(composition.ok(),
              "key subsystem production logging e2e should compose live runtime dependencies: " +
                  composition.error);
  assert_true(composition.dependency_set != nullptr &&
                  composition.dependency_set->memory_manager != nullptr &&
                  composition.dependency_set->knowledge_service != nullptr &&
                  composition.dependency_set->tool_manager != nullptr &&
                  composition.dependency_set->runtime_event_bus != nullptr &&
                  composition.dependency_set->runtime_telemetry_bridge != nullptr &&
                  composition.dependency_set->logger != nullptr,
              "key subsystem production logging e2e should expose live ports for memory, knowledge, runtime, tools, and logging: " +
                  composition.dependency_set->describe_readiness().summary());

  const auto logger =
      std::dynamic_pointer_cast<LoggingFacade>(composition.dependency_set->logger);
  assert_true(logger != nullptr,
              "key subsystem production logging e2e should keep the concrete logger inspectable");

  MockCognitionFixture cognition_fixture(MockCognitionFixtureOptions{
      .request_id = "req-key-subsystem-cognition",
      .trace_id = kTraceId,
      .profile_id = kProfileId,
      .goal_id = "goal-key-subsystem-cognition",
      .user_turn = "user_turn=cognition-secret-input",
      .goal_summary = "goal_summary=cognition-secret-goal",
  });
  cognition_fixture.stage_structured_planning_result(StructuredPlanningPayloadScenario::Valid);
  cognition_fixture.stage_structured_execution_result(
      StructuredExecutionPayloadScenario::ValidDirectResponse);

  auto cognition_engine = create_cognition_engine(
      *snapshot,
      dasall::cognition::CognitionRuntimeDependencies{
          .llm_manager = cognition_fixture.llm_manager(),
          .policy_snapshot = snapshot,
          .logger = composition.dependency_set->logger,
          .audit_logger = composition.dependency_set->audit_logger,
          .metrics_provider = composition.dependency_set->metrics_provider,
          .tracer_provider = composition.dependency_set->tracer_provider,
      });
  assert_true(cognition_engine != nullptr,
              "key subsystem production logging e2e should create a cognition engine on the shared live logger");

  const auto decide_result = cognition_engine->decide(
      cognition_fixture.make_decide_request(true));
  assert_true(decide_result.action_decision.has_value() &&
                  !decide_result.error_info.has_value(),
              "key subsystem production logging e2e should keep the valid cognition path successful");

  auto invalid_decide_request = cognition_fixture.make_decide_request(true);
  invalid_decide_request.request_id.clear();
  const auto invalid_decide_result = cognition_engine->decide(invalid_decide_request);
  assert_true(invalid_decide_result.error_info.has_value(),
              "key subsystem production logging e2e should emit a failed cognition event for invalid input");

  auto response_builder = create_response_builder(
      *snapshot,
      dasall::cognition::CognitionRuntimeDependencies{
          .llm_manager = cognition_fixture.llm_manager(),
          .policy_snapshot = snapshot,
          .logger = composition.dependency_set->logger,
          .audit_logger = composition.dependency_set->audit_logger,
          .metrics_provider = composition.dependency_set->metrics_provider,
          .tracer_provider = composition.dependency_set->tracer_provider,
      });
  assert_true(response_builder != nullptr,
              "key subsystem production logging e2e should create a response builder on the shared live logger");

  auto decision = cognition_fixture.make_action_decision(
      dasall::cognition::decision::ActionDecisionKind::DirectResponse);
  decision.response_outline = dasall::cognition::decision::ResponseOutline{
      .summary =
          "raw_prompt=cognition-secret-prompt api_token=secret-token payload=secret-payload",
      .key_points = {std::string("template fallback should redact owner payloads")},
  };
  auto response_request = cognition_fixture.make_response_request(decision);
  response_request.build_hints.prefer_template = true;
  response_request.build_hints.max_summary_chars = 256U;
  const auto response_result = response_builder->build(response_request);
  assert_true(response_result.fallback_used && response_result.agent_result.has_value() &&
                  response_result.agent_result->response_text.has_value(),
              "key subsystem production logging e2e should emit a degraded cognition response through template fallback");

  const auto writeback_result = composition.dependency_set->memory_manager->write_back(
      make_memory_writeback_request());
  assert_true(!writeback_result.result_code.has_value(),
              "key subsystem production logging e2e should complete the live memory writeback path");

  const auto context_result = composition.dependency_set->memory_manager->prepare_context(
      make_memory_context_request());
  assert_true(!context_result.result_code.has_value(),
              "key subsystem production logging e2e should complete the live memory context assembly path");

  const auto knowledge_result = composition.dependency_set->knowledge_service->retrieve(
      make_success_query());
  assert_true(knowledge_result.ok && knowledge_result.evidence.has_value() &&
                  !knowledge_result.evidence->slices.empty(),
              "key subsystem production logging e2e should keep the live knowledge retrieve path successful");

  const auto knowledge_invalid_result = composition.dependency_set->knowledge_service->retrieve(
      make_invalid_query());
  assert_true(!knowledge_invalid_result.ok && knowledge_invalid_result.error.has_value(),
              "key subsystem production logging e2e should emit an invalid-payload knowledge event");

  const auto knowledge_fallback_result = composition.dependency_set->knowledge_service->retrieve(
      make_fallback_query());
  assert_true(!knowledge_fallback_result.ok && knowledge_fallback_result.error.has_value(),
              "key subsystem production logging e2e should fail closed on fallback knowledge requests with missing request_id");

  const auto runtime_transition =
      composition.dependency_set->runtime_telemetry_bridge->emit_transition(
          RuntimeState::Planning,
          RuntimeState::Reasoning,
          RuntimeTelemetryContext{
              .request_id = std::string("req-key-subsystem-runtime"),
              .session_id = std::string(kSessionId),
              .trace_id = std::string(kTraceId),
              .turn_id = std::string("turn-key-subsystem-runtime"),
              .checkpoint_id = std::string("chk-key-subsystem-runtime"),
          },
          "runtime-secret-transition=hidden");
  assert_true(runtime_transition.envelope.event_name == "runtime.transition",
              "key subsystem production logging e2e should emit a runtime control-plane transition event");

  assert_equal(1,
               static_cast<int>(composition.dependency_set->runtime_event_bus->dispatch_pending()),
               "key subsystem production logging e2e should dispatch the runtime control-plane transition through the event bus");

  const auto dataset_envelope = composition.dependency_set->tool_manager->invoke(
      make_dataset_request(),
      make_tool_context(snapshot.get()));
  assert_true(dataset_envelope.tool_result.has_value() &&
                  dataset_envelope.tool_result->success.value_or(false),
              "key subsystem production logging e2e should route agent.dataset through the live services backend");

  const auto terminal_envelope = composition.dependency_set->tool_manager->invoke(
      make_terminal_request(),
      make_tool_context(
          snapshot.get(),
          std::vector<dasall::tools::ToolConfirmationFact>{
              dasall::tools::ToolConfirmationFact{
                  .confirmation_id = std::string("confirm-key-subsystem-services-exec"),
                  .subject_ref = std::string("goal://key-subsystem-services-exec"),
                  .proof_type = std::string("user.approved"),
                  .confirmed_at_ms = 1713100000199,
              },
          }));
  assert_true(terminal_envelope.tool_result.has_value() &&
                  terminal_envelope.tool_result->success.value_or(false),
              "key subsystem production logging e2e should route agent.terminal through the live services backend once confirmation is present");

  assert_true(logger->flush(LogFlushDeadline{.timeout_ms = 500}).ok,
              "key subsystem production logging e2e should flush the shared live logger before inspecting runtime.log");

  auto reader = std::make_shared<FileLogReader>(FileLogReaderOptions{
      .runtime_log_path = runtime_log_path,
      .include_rotation_family = true,
  });
  LogQueryService service(reader,
                          LogQueryServiceOptions{
                              .enable_diag_pull = true,
                              .artifact_namespace = "diag://infra/logging/query",
                              .artifact_root_dir = artifact_root,
                              .index_file_name = "query-index.jsonl",
                              .retention_policy = {.retention_days = 7, .max_artifact_count = 8},
                          },
                          []() { return static_cast<std::int64_t>(4102444800000); });

  const auto trace_query_result = service.query(
      LogQueryRequest{
          .query_id = std::string("key-subsystem-production-logging-trace"),
          .selector_kind = LogQuerySelectorKind::TraceId,
          .selector_value = std::string(kTraceId),
          .start_ts_ms = 1,
          .end_ts_ms = 4102444800000,
          .max_records = 64,
      },
      make_access_context("req-key-subsystem-production-logging-trace-query", kTraceId));
  const auto session_query_result = service.query(
      LogQueryRequest{
          .query_id = std::string("key-subsystem-production-logging-session"),
          .selector_kind = LogQuerySelectorKind::SessionId,
          .selector_value = std::string(kSessionId),
          .start_ts_ms = 1,
          .end_ts_ms = 4102444800000,
          .max_records = 64,
      },
      make_access_context("req-key-subsystem-production-logging-session-query",
                          "trace-key-subsystem-production-logging-session-query"));

  const auto runtime_log_text = read_text(runtime_log_path);
  const auto trace_artifact_text =
      read_text(artifact_root / "key-subsystem-production-logging-trace-4102444800000.json");
  const auto session_artifact_text =
      read_text(artifact_root / "key-subsystem-production-logging-session-4102444800000.json");
  const auto index_text = read_text(artifact_root / "query-index.jsonl");

  assert_true(runtime_log_text.find("cognition stage.completed") != std::string::npos &&
                  runtime_log_text.find("cognition stage.failed") != std::string::npos &&
                  runtime_log_text.find("cognition response.degraded") != std::string::npos &&
                  runtime_log_text.find("memory writeback.completed") != std::string::npos &&
                  runtime_log_text.find("memory context.assembled") != std::string::npos &&
                  runtime_log_text.find("knowledge.retrieve.completed") != std::string::npos &&
                  runtime_log_text.find("knowledge.retrieve.invalid_payload") !=
                      std::string::npos &&
                  runtime_log_text.find("runtime.transition") != std::string::npos &&
                  runtime_log_text.find("service.data.query.route") != std::string::npos &&
                  runtime_log_text.find("service.execution.route") != std::string::npos,
              "key subsystem production logging e2e should persist cognition, memory, knowledge, runtime, and services events into one runtime.log");
  assert_true(runtime_log_text.find("\"module\":\"cognition\"") != std::string::npos &&
                  runtime_log_text.find("\"module\":\"memory\"") != std::string::npos &&
                  runtime_log_text.find("\"module\":\"knowledge\"") != std::string::npos &&
                  runtime_log_text.find("\"module\":\"runtime\"") != std::string::npos &&
                  runtime_log_text.find(std::string("\"session_id\":\"") + kSessionId + "\"") !=
                      std::string::npos &&
                  runtime_log_text.find(std::string("\"trace_id\":\"") + kTraceId + "\"") !=
                      std::string::npos,
              "key subsystem production logging e2e should preserve structured correlation attrs in runtime.log across subsystems");
  assert_true(runtime_log_text.find("cognition-secret-input") == std::string::npos &&
                  runtime_log_text.find("cognition-secret-goal") == std::string::npos &&
                  runtime_log_text.find("cognition-secret-prompt") == std::string::npos &&
                  runtime_log_text.find("secret-token") == std::string::npos &&
                  runtime_log_text.find("secret-payload") == std::string::npos &&
                  runtime_log_text.find("memory-secret-input") == std::string::npos &&
                  runtime_log_text.find("memory-secret-output") == std::string::npos &&
                  runtime_log_text.find("memory-secret-token") == std::string::npos &&
                  runtime_log_text.find("memory-secret-fact") == std::string::npos &&
                  runtime_log_text.find("memory-secret-goal") == std::string::npos &&
                  runtime_log_text.find("memory-secret-digest") == std::string::npos &&
                  runtime_log_text.find("retrieval-secret-ref") == std::string::npos &&
                  runtime_log_text.find("knowledge-secret-fallback-body") == std::string::npos &&
                  runtime_log_text.find("runtime-secret-transition") == std::string::npos &&
                  runtime_log_text.find("services-secret-query") == std::string::npos &&
                  runtime_log_text.find("services-secret-terminal") == std::string::npos,
              "key subsystem production logging e2e should keep the shared runtime.log redacted across subsystem-specific payloads");

  assert_true(trace_query_result.ok && trace_query_result.has_success_payload(),
              "key subsystem production logging e2e should materialize a trace-scoped diagnostics artifact");
  assert_true(trace_query_result.match_count >= 4U,
              "key subsystem production logging e2e should query multiple cognition, runtime, and services records by trace_id");
  assert_true(trace_artifact_text.find("cognition stage.completed") != std::string::npos &&
                  trace_artifact_text.find("runtime.transition") != std::string::npos &&
                  trace_artifact_text.find("service.data.query.route") != std::string::npos &&
                  trace_artifact_text.find("service.execution.route") != std::string::npos,
              "key subsystem production logging e2e should keep cognition, runtime, and services records queryable from the trace artifact");

  assert_true(session_query_result.ok && session_query_result.has_success_payload(),
              "key subsystem production logging e2e should materialize a session-scoped diagnostics artifact");
  assert_true(session_query_result.match_count >= 5U,
              "key subsystem production logging e2e should query memory, knowledge, runtime, and services records by session_id");
  assert_true(session_artifact_text.find("memory writeback.completed") != std::string::npos &&
                  session_artifact_text.find("memory context.assembled") != std::string::npos &&
                  session_artifact_text.find("knowledge.retrieve.completed") != std::string::npos &&
                  session_artifact_text.find("runtime.transition") != std::string::npos &&
                  session_artifact_text.find("service.data.query.route") != std::string::npos,
              "key subsystem production logging e2e should keep memory, knowledge, runtime, and services records queryable from the session artifact");
  assert_true(index_text.find("key-subsystem-production-logging-trace") != std::string::npos &&
                  index_text.find("key-subsystem-production-logging-session") !=
                      std::string::npos &&
                  index_text.find("trace_id") != std::string::npos &&
                  index_text.find("session_id") != std::string::npos,
              "key subsystem production logging e2e should index both trace and session diagnostics artifacts");
}

}  // namespace

int main() {
  try {
    test_key_subsystem_production_logging_e2e_persists_redacted_queryable_events();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}