#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef DASALL_PROJECT_SOURCE_DIR
#error DASALL_PROJECT_SOURCE_DIR must be defined for cognition llm judge regression coverage
#endif

#include "support/TestAssertions.h"

#include "../../../llm/include/route/ModelSelectionHint.h"
#include "../../../llm/src/LLMManager.h"
#include "../../../llm/src/UsageAggregator.h"
#include "../../../llm/src/execution/ResponseNormalizer.h"
#include "../../../llm/src/prompt/PromptPipeline.h"
#include "../../../llm/src/route/AdapterRegistry.h"
#include "../../../llm/src/route/ModelRouter.h"

#include "../../mocks/include/MockLLMAdapter.h"
#include "../../unit/llm/ModelRouterTestSupport.h"

namespace {

namespace fs = std::filesystem;

using dasall::contracts::LLMRequest;
using dasall::contracts::LLMRequestMode;
using dasall::contracts::LLMResponse;
using dasall::contracts::LLMResponseKind;
using dasall::llm::AdapterCallResult;
using dasall::llm::AdapterUsageFragment;
using dasall::llm::LLMGenerateRequest;
using dasall::llm::LLMManager;
using dasall::llm::LLMSubsystemConfig;
using dasall::llm::ModelSelectionHint;
using dasall::llm::execution::ResponseNormalizer;
using dasall::llm::prompt::PromptPipeline;
using dasall::llm::route::AdapterRegistration;
using dasall::tests::mocks::MockLLMAdapter;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

constexpr std::string_view kPromptAssetRoot =
    DASALL_PROJECT_SOURCE_DIR "/llm/assets/prompts";
constexpr std::string_view kReplayRoot =
    DASALL_PROJECT_SOURCE_DIR "/tests/data/cognition/replay";
constexpr std::string_view kJudgeTaskType = "judge_main_chain";
constexpr std::string_view kJudgePromptRelease = "responder@2026.06.01";
constexpr std::string_view kJudgeOutputSchema =
    "schema://responder/judge_main_chain";

struct JudgeCase {
  std::string request_id;
  std::string split;
  std::string expected_verdict;
  std::string summary;
  std::string rubric;
  std::string trace_bundle;
  std::vector<fs::path> trace_files;
};

struct JudgeCaseOutcome {
  std::string request_id;
  std::string split;
  std::string expected_verdict;
  std::string actual_verdict;
  double score = 0.0;
  double confidence = 0.0;
  std::string summary;
  std::string resolved_route;
  std::string prompt_id;
  std::string prompt_version;
  std::string raw_payload;
};

struct JudgeRunSummary {
  fs::path artifact_dir;
  std::vector<JudgeCaseOutcome> outcomes;
  std::vector<LLMRequest> observed_requests;
  std::size_t curated_case_count = 0U;
  std::size_t supplemental_case_count = 0U;
  std::size_t pass_count = 0U;
  std::size_t fail_count = 0U;
  bool expectations_met = false;
};

struct SyntheticFailureFixture {
  fs::path root;

  ~SyntheticFailureFixture() {
    if (root.empty()) {
      return;
    }

    std::error_code error;
    fs::remove_all(root, error);
  }
};

struct JudgeRunnerOptions {
  fs::path replay_root;
  std::optional<fs::path> failure_samples_root;
  fs::path artifact_dir;
  std::optional<std::string> prompt_release_override;
  std::optional<std::size_t> max_curated_cases;
};

[[nodiscard]] std::optional<std::string> read_env(std::string_view name) {
  const char* value = std::getenv(std::string(name).c_str());
  if (value == nullptr || *value == '\0') {
    return std::nullopt;
  }

  return std::string(value);
}

[[nodiscard]] std::string trim_copy(std::string value) {
  while (!value.empty() &&
         (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' ||
          value.back() == '\t')) {
    value.pop_back();
  }

  std::size_t start = 0U;
  while (start < value.size() &&
         (value[start] == '\n' || value[start] == '\r' || value[start] == ' ' ||
          value[start] == '\t')) {
    ++start;
  }

  return value.substr(start);
}

[[nodiscard]] std::string format_double(const double value) {
  std::ostringstream buffer;
  buffer << std::fixed << std::setprecision(2) << value;
  return buffer.str();
}

[[nodiscard]] std::string escape_json_string(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size() + 16U);

  for (const char ch : value) {
    switch (ch) {
      case '\\':
        escaped.append("\\\\");
        break;
      case '"':
        escaped.append("\\\"");
        break;
      case '\n':
        escaped.append("\\n");
        break;
      case '\r':
        escaped.append("\\r");
        break;
      case '\t':
        escaped.append("\\t");
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }

  return escaped;
}

[[nodiscard]] std::string json_string_array(
    const std::vector<std::string>& values) {
  std::ostringstream buffer;
  buffer << '[';
  for (std::size_t index = 0U; index < values.size(); ++index) {
    if (index > 0U) {
      buffer << ',';
    }
    buffer << '"' << escape_json_string(values[index]) << '"';
  }
  buffer << ']';
  return buffer.str();
}

[[nodiscard]] std::string read_text_file(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    throw std::runtime_error("failed to open file: " + path.string());
  }

  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

void write_text_file(const fs::path& path, const std::string& content) {
  std::error_code error;
  fs::create_directories(path.parent_path(), error);

  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream.is_open()) {
    throw std::runtime_error("failed to open output file: " + path.string());
  }

  stream << content;
}

[[nodiscard]] fs::path make_temp_dir(std::string_view label) {
  const auto suffix = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const fs::path root = fs::temp_directory_path() /
                        (std::string{"dasall-"} + std::string(label) + "-" + suffix);
  std::error_code error;
  fs::remove_all(root, error);
  fs::create_directories(root, error);
  return root;
}

[[nodiscard]] bool is_trace_file(const fs::directory_entry& entry) {
  return entry.is_regular_file() && entry.path().extension() == ".trace";
}

[[nodiscard]] std::string request_id_from_trace_name(std::string_view file_name) {
  const std::size_t separator = file_name.find("__");
  if (separator == std::string_view::npos || separator == 0U) {
    throw std::runtime_error("unexpected replay trace name: " + std::string(file_name));
  }

  return std::string(file_name.substr(0U, separator));
}

[[nodiscard]] std::string build_trace_bundle(
    std::vector<fs::path> trace_files) {
  std::sort(trace_files.begin(), trace_files.end());

  std::ostringstream bundle;
  for (std::size_t index = 0U; index < trace_files.size(); ++index) {
    if (index > 0U) {
      bundle << "\n\n";
    }
    bundle << "FILE " << trace_files[index].filename().string() << "\n";
    bundle << trim_copy(read_text_file(trace_files[index]));
  }

  return bundle.str();
}

[[nodiscard]] std::string describe_curated_case(std::string_view request_id) {
  if (request_id.find("decide_direct") != std::string_view::npos) {
    return "执行主链 direct decision 应保留 perception/planning bridge 证据，并给出有界行动决策。";
  }

  if (request_id.find("decide_planning_fallback") != std::string_view::npos) {
    return "planning fallback 分支应保留降级证据，但仍需输出受控决定而不是主链失真。";
  }

  if (request_id.find("reflect_continue") != std::string_view::npos) {
    return "reflection 主链应维持 Continue 结论，不应漂移到 AbortSafe 或空结果。";
  }

  if (request_id.find("build_projection") != std::string_view::npos) {
    return "response build projection 主链应保留 build request/result 的投影闭环，不应退化到模板 fallback。";
  }

  return "curated replay case 应满足 DASALL cognition 主链回归基线。";
}

[[nodiscard]] std::string rubric_for_curated_case(std::string_view request_id) {
  if (request_id.find("decide_planning_fallback") != std::string_view::npos) {
    return "trace_complete;fallback_accounted_but_bounded;decision_path_consistent";
  }

  if (request_id.find("reflect_continue") != std::string_view::npos) {
    return "trace_complete;reflection_continue_preserved;no_abort_safe_signal";
  }

  if (request_id.find("build_projection") != std::string_view::npos) {
    return "trace_complete;response_projection_complete;no_response_fallback";
  }

  return "trace_complete;decision_path_consistent;no_unbounded_failure";
}

[[nodiscard]] std::string describe_failure_case(std::string_view category,
                                                std::string_view request_id) {
  return "supplemental failure sample " + std::string(category) +
         " 应在 request/result trace 暴露失败信号时被判为 fail；当前 request 为 " +
         std::string(request_id) + "。";
}

[[nodiscard]] std::string rubric_for_failure_case(std::string_view category) {
  if (category == "response.fallback_used") {
    return "failure_signal_accounted;mark_fail_when_fallback_used;cite_trace_evidence";
  }

  if (category == "reflection.abort_safe") {
    return "failure_signal_accounted;mark_fail_when_abort_safe;cite_trace_evidence";
  }

  if (category == "cognition.schema_violation") {
    return "failure_signal_accounted;mark_fail_when_schema_violation;cite_trace_evidence";
  }

  return "failure_signal_accounted;mark_fail_on_degradation;cite_trace_evidence";
}

[[nodiscard]] std::vector<JudgeCase> load_curated_cases(
    const fs::path& replay_root,
    const std::optional<std::size_t>& max_cases = std::nullopt) {
  std::map<std::string, std::vector<fs::path>> grouped;

  for (const auto& entry : fs::directory_iterator(replay_root)) {
    if (!is_trace_file(entry)) {
      continue;
    }

    const std::string request_id =
        request_id_from_trace_name(entry.path().filename().string());
    grouped[request_id].push_back(entry.path());
  }

  std::vector<JudgeCase> cases;
  cases.reserve(grouped.size());
  for (const auto& [request_id, trace_files] : grouped) {
    cases.push_back(JudgeCase{
        .request_id = request_id,
        .split = "curated.success_chain",
        .expected_verdict = "pass",
        .summary = describe_curated_case(request_id),
        .rubric = rubric_for_curated_case(request_id),
        .trace_bundle = build_trace_bundle(trace_files),
        .trace_files = trace_files,
    });

    if (max_cases.has_value() && cases.size() >= *max_cases) {
      break;
    }
  }

  return cases;
}

[[nodiscard]] std::vector<JudgeCase> load_failure_sample_cases(
    const fs::path& failure_root) {
  if (failure_root.empty() || !fs::exists(failure_root)) {
    return {};
  }

  std::vector<JudgeCase> cases;

  for (const auto& category_entry : fs::directory_iterator(failure_root)) {
    if (!category_entry.is_directory()) {
      continue;
    }

    const std::string category = category_entry.path().filename().string();
    std::map<std::string, std::vector<fs::path>> grouped;

    for (const auto& trace_entry : fs::directory_iterator(category_entry.path())) {
      if (!is_trace_file(trace_entry)) {
        continue;
      }

      const std::string request_id =
          request_id_from_trace_name(trace_entry.path().filename().string());
      grouped[request_id].push_back(trace_entry.path());
    }

    for (const auto& [request_id, trace_files] : grouped) {
      cases.push_back(JudgeCase{
          .request_id = request_id,
          .split = "failure_samples." + category,
          .expected_verdict = "fail",
          .summary = describe_failure_case(category, request_id),
          .rubric = rubric_for_failure_case(category),
          .trace_bundle = build_trace_bundle(trace_files),
          .trace_files = trace_files,
      });
    }
  }

  std::sort(cases.begin(), cases.end(), [](const JudgeCase& left, const JudgeCase& right) {
    return std::tie(left.split, left.request_id) < std::tie(right.split, right.request_id);
  });

  return cases;
}

[[nodiscard]] SyntheticFailureFixture make_synthetic_failure_fixture() {
  SyntheticFailureFixture fixture;
  fixture.root = make_temp_dir("cognition-judge-failure-samples");

  const fs::path category_dir = fixture.root / "response.fallback_used";
  write_text_file(category_dir / "req_failure_response_fallback__response__build_request.trace",
                  "request_id=req_failure_response_fallback\nstage=response\nevent=build.request\n");
  write_text_file(category_dir / "req_failure_response_fallback__response__build_result.trace",
                  "request_id=req_failure_response_fallback\nstage=response\nevent=build.result\nfallback_used=true\nagent_result_status=degraded\n");
  return fixture;
}

AdapterRegistration make_registration(std::shared_ptr<MockLLMAdapter> adapter) {
  return AdapterRegistration{
      .provider_id = "deepseek-prod",
      .model_id = "deepseek-chat",
      .adapter_id = "deepseek-cloud",
      .deployment_type = "cloud",
      .capability_tags = {"cloud", "external", "unary"},
      .supports_streaming = false,
      .adapter = std::move(adapter),
  };
}

[[nodiscard]] LLMSubsystemConfig make_config() {
  auto config = dasall::llm::test_support::make_config(
      "response", "cloud.default", std::nullopt, {"local.small"}, false, false);
  config.profile_id = "desktop_full";
  config.prompt_asset_sources.baseline_root = std::string(kPromptAssetRoot);
  config.prompt_selector_overlay.active_scene = "general";
  config.prompt_selector_overlay.active_persona = "responder";
  return config;
}

[[nodiscard]] std::shared_ptr<const ModelSelectionHint> make_selection_hint() {
  return std::make_shared<const ModelSelectionHint>(ModelSelectionHint{
      .stage = "response",
      .task_type = std::string(kJudgeTaskType),
      .complexity_tier = "standard",
      .latency_sla_tier = "offline",
      .budget_tier = "hard_cap",
      .requires_tools = false,
      .requires_reasoning = false,
      .prefers_visible_reasoning = false,
      .estimated_input_tokens = 4096U,
      .target_output_tokens = 1024U,
      .previous_route_failures = 0U,
  });
}

[[nodiscard]] LLMGenerateRequest make_request(
    const JudgeCase& judge_case,
    const std::optional<std::string>& prompt_release_override) {
  LLMRequest request;
  request.request_id = judge_case.request_id;
  request.llm_call_id = "judge-" + judge_case.request_id;
  request.model_route = "cloud.default";
  request.request_mode = LLMRequestMode::Unary;
  request.messages = std::vector<std::string>{"judge regression placeholder"};
  request.output_schema_ref = std::string(kJudgeOutputSchema);
  request.response_format = "json_object";
  request.max_output_tokens = 1024U;
  request.runtime_budget = dasall::contracts::RuntimeBudget{
      .max_tokens = 4096U,
      .max_turns = std::nullopt,
      .max_tool_calls = std::nullopt,
      .max_latency_ms = std::nullopt,
      .max_replan_count = std::nullopt,
  };
  request.tags = std::vector<std::string>{
      "integration",
      "cognition-judge",
      "judge_split=" + judge_case.split,
      "user_goal=" + judge_case.summary,
      "constraints=" + judge_case.rubric,
      "session_summary=" + judge_case.trace_bundle,
  };

  return LLMGenerateRequest{
      .stage = "response",
      .task_type = std::string(kJudgeTaskType),
      .request = std::move(request),
      .prompt_release_id_override = prompt_release_override,
      .selection_hint = make_selection_hint(),
  };
}

[[nodiscard]] std::string parse_json_string_field(std::string_view json,
                                                  std::string_view field) {
  const std::string needle = "\"" + std::string(field) + "\":\"";
  const std::size_t start = json.find(needle);
  if (start == std::string_view::npos) {
    throw std::runtime_error("missing string field in json payload: " + std::string(field));
  }

  std::string value;
  for (std::size_t index = start + needle.size(); index < json.size(); ++index) {
    const char ch = json[index];
    if (ch == '\\') {
      if (index + 1U >= json.size()) {
        throw std::runtime_error("malformed escape sequence in json payload");
      }
      const char escaped = json[++index];
      switch (escaped) {
        case '\\':
          value.push_back('\\');
          break;
        case '"':
          value.push_back('"');
          break;
        case 'n':
          value.push_back('\n');
          break;
        case 'r':
          value.push_back('\r');
          break;
        case 't':
          value.push_back('\t');
          break;
        default:
          value.push_back(escaped);
          break;
      }
      continue;
    }

    if (ch == '"') {
      return value;
    }

    value.push_back(ch);
  }

  throw std::runtime_error("unterminated json string field: " + std::string(field));
}

[[nodiscard]] double parse_json_number_field(std::string_view json,
                                             std::string_view field) {
  const std::string needle = "\"" + std::string(field) + "\":";
  const std::size_t start = json.find(needle);
  if (start == std::string_view::npos) {
    throw std::runtime_error("missing numeric field in json payload: " + std::string(field));
  }

  std::size_t end = start + needle.size();
  while (end < json.size() && json[end] != ',' && json[end] != '}') {
    ++end;
  }

  return std::stod(trim_copy(std::string(json.substr(start + needle.size(),
                                                     end - start - needle.size()))));
}

[[nodiscard]] std::string build_mock_response(const LLMRequest& request,
                                              std::string_view verdict) {
  const std::string request_id = request.request_id.value_or(std::string{"unknown"});
  const std::string prompt_identity =
      request.prompt_id.value_or(std::string{"unknown"}) + "@" +
      request.prompt_version.value_or(std::string{"unknown"});
  const double score = verdict == "pass" ? 0.95 : 0.18;
  const double confidence = verdict == "pass" ? 0.89 : 0.94;
  const std::vector<std::string> rubric_hits =
      verdict == "pass"
          ? std::vector<std::string>{"trace_complete", "result_consistent"}
          : std::vector<std::string>{"failure_signal_accounted", "mark_fail_on_degradation"};
  const std::vector<std::string> evidence =
      verdict == "pass"
          ? std::vector<std::string>{
                "request_id=" + request_id,
                "trace bundle preserved a bounded main-chain outcome",
                "prompt=" + prompt_identity,
            }
          : std::vector<std::string>{
                "request_id=" + request_id,
                "trace bundle exposed degraded or fallback semantics",
                "prompt=" + prompt_identity,
            };
  const std::string summary =
      verdict == "pass" ? "主链回归满足基线。" : "failure sample 暴露失败信号，应判定为 fail。";

  std::ostringstream payload;
  payload << '{'
          << "\"schema_version\":\"judge_main_chain.v1\","
          << "\"request_id\":\"" << escape_json_string(request_id) << "\","
          << "\"verdict\":\"" << verdict << "\","
          << "\"score\":" << format_double(score) << ','
          << "\"confidence\":" << format_double(confidence) << ','
          << "\"rubric_hits\":" << json_string_array(rubric_hits) << ','
          << "\"summary\":\"" << escape_json_string(summary) << "\","
          << "\"evidence\":" << json_string_array(evidence)
          << '}';
  return payload.str();
}

void write_artifacts(const JudgeRunnerOptions& options,
                     const std::vector<JudgeCase>& cases,
                     const JudgeRunSummary& summary) {
  std::ostringstream cases_jsonl;
  for (const auto& judge_case : cases) {
    cases_jsonl << '{'
                << "\"request_id\":\"" << escape_json_string(judge_case.request_id) << "\","
                << "\"split\":\"" << escape_json_string(judge_case.split) << "\","
                << "\"expected_verdict\":\"" << judge_case.expected_verdict << "\","
                << "\"trace_file_count\":" << judge_case.trace_files.size() << ','
                << "\"summary\":\"" << escape_json_string(judge_case.summary) << "\","
                << "\"rubric\":\"" << escape_json_string(judge_case.rubric) << "\""
                << "}\n";
  }

  std::ostringstream results_jsonl;
  for (const auto& outcome : summary.outcomes) {
    results_jsonl << '{'
                  << "\"request_id\":\"" << escape_json_string(outcome.request_id) << "\","
                  << "\"split\":\"" << escape_json_string(outcome.split) << "\","
                  << "\"expected_verdict\":\"" << outcome.expected_verdict << "\","
                  << "\"actual_verdict\":\"" << outcome.actual_verdict << "\","
                  << "\"score\":" << format_double(outcome.score) << ','
                  << "\"confidence\":" << format_double(outcome.confidence) << ','
                  << "\"resolved_route\":\"" << escape_json_string(outcome.resolved_route) << "\","
                  << "\"prompt_id\":\"" << escape_json_string(outcome.prompt_id) << "\","
                  << "\"prompt_version\":\"" << escape_json_string(outcome.prompt_version) << "\""
                  << "}\n";
  }

  std::ostringstream report_json;
  report_json << '{'
              << "\"schema_version\":\"cognition_judge_report.v1\","
              << "\"artifact_dir\":\"" << escape_json_string(summary.artifact_dir.string()) << "\","
              << "\"replay_root\":\"" << escape_json_string(options.replay_root.string()) << "\","
              << "\"failure_samples_root\":\""
              << escape_json_string(options.failure_samples_root.has_value()
                                        ? options.failure_samples_root->string()
                                        : std::string())
              << "\","
              << "\"prompt_release_override\":\""
              << escape_json_string(options.prompt_release_override.value_or(std::string()))
              << "\","
              << "\"curated_case_count\":" << summary.curated_case_count << ','
              << "\"supplemental_case_count\":" << summary.supplemental_case_count << ','
              << "\"pass_count\":" << summary.pass_count << ','
              << "\"fail_count\":" << summary.fail_count << ','
              << "\"expectations_met\":" << (summary.expectations_met ? "true" : "false")
              << '}';

  std::ostringstream report_md;
  report_md << "# Cognition LLM Judge Regression\n\n"
            << "- curated success-chain cases: " << summary.curated_case_count << "\n"
            << "- supplemental failure-sample cases: " << summary.supplemental_case_count << "\n"
            << "- pass verdicts: " << summary.pass_count << "\n"
            << "- fail verdicts: " << summary.fail_count << "\n"
            << "- expectations met: " << (summary.expectations_met ? "true" : "false") << "\n";

  write_text_file(summary.artifact_dir / "judge-cases.jsonl", cases_jsonl.str());
  write_text_file(summary.artifact_dir / "judge-results.jsonl", results_jsonl.str());
  write_text_file(summary.artifact_dir / "judge-report.json", report_json.str());
  write_text_file(summary.artifact_dir / "judge-report.md", report_md.str());
  write_text_file(summary.artifact_dir / "status.txt",
                  summary.expectations_met ? "PASS\n" : "FAIL\n");
}

[[nodiscard]] JudgeRunSummary run_judge_regression(
    const JudgeRunnerOptions& options) {
  auto prompt_pipeline = std::make_shared<PromptPipeline>();
  auto router = std::make_shared<dasall::llm::route::ModelRouter>();
  auto registry = std::make_shared<dasall::llm::route::AdapterRegistry>();
  auto executor = std::make_shared<dasall::llm::LLMCallExecutor>();
  auto normalizer = std::make_shared<ResponseNormalizer>();
  auto aggregator = std::make_shared<dasall::llm::UsageAggregator>();
  auto catalog_snapshot =
      std::make_shared<const dasall::llm::provider::ProviderCatalogSnapshot>(
          dasall::llm::test_support::make_default_catalog());

  assert_true(registry->init(dasall::llm::route::AdapterRegistryConfig{
                  .blocked_failure_threshold = 6U,
              }),
              "judge regression should initialize AdapterRegistry before wiring the manager");

  std::vector<JudgeCase> cases =
      load_curated_cases(options.replay_root, options.max_curated_cases);
  std::vector<JudgeCase> failure_cases;
  if (options.failure_samples_root.has_value()) {
    failure_cases = load_failure_sample_cases(*options.failure_samples_root);
    cases.insert(cases.end(), failure_cases.begin(), failure_cases.end());
  }

  assert_true(!cases.empty(),
              "judge regression should load at least one replay or failure-sample case");

  std::map<std::string, std::string> expected_verdicts;
  for (const auto& judge_case : cases) {
    expected_verdicts.emplace(judge_case.request_id, judge_case.expected_verdict);
  }

  JudgeRunSummary summary;
  summary.artifact_dir = options.artifact_dir;
  summary.curated_case_count = cases.size() - failure_cases.size();
  summary.supplemental_case_count = failure_cases.size();

  auto adapter = std::make_shared<MockLLMAdapter>();
  adapter->set_generate_handler([&](const LLMRequest& request) {
    summary.observed_requests.push_back(request);

    assert_true(request.prompt_id == std::optional<std::string>{"responder"},
                "judge regression should select the responder prompt family");
    assert_true(request.prompt_version == std::optional<std::string>{"2026.06.01"},
                "judge regression should select the dedicated judge prompt release");
    assert_true(request.output_schema_ref == std::optional<std::string>{std::string(kJudgeOutputSchema)},
                "judge regression should preserve the judge output schema on the provider request");
    assert_true(request.messages.has_value() && request.messages->size() == 2U,
                "judge regression should dispatch composed system/user messages");

    const std::string request_id = request.request_id.value_or(std::string());
    const auto expected = expected_verdicts.find(request_id);
    if (expected == expected_verdicts.end()) {
      throw std::runtime_error("missing expected verdict for request: " + request_id);
    }

    LLMResponse response;
    response.request_id = request.request_id;
    response.llm_call_id = request.llm_call_id;
    response.response_kind = LLMResponseKind::DirectResponse;
    response.content_payload = build_mock_response(request, expected->second);
    response.finish_reason = "stop";

    AdapterCallResult result;
    result.response = std::move(response);
    result.usage = AdapterUsageFragment{
        .prompt_tokens = 256U,
        .completion_tokens = 64U,
        .total_tokens = 320U,
        .prompt_cache_hit_tokens = 0U,
        .prompt_cache_miss_tokens = 256U,
    };
    return result;
  });

  assert_true(registry->register_adapter(make_registration(adapter)),
              "judge regression should register a MockLLMAdapter for the response route");

  LLMManager manager(prompt_pipeline,
                     router,
                     registry,
                     executor,
                     normalizer,
                     aggregator,
                     catalog_snapshot,
                     nullptr,
                     nullptr,
                     nullptr);
  assert_true(manager.init(make_config()),
              "judge regression should initialize LLMManager with real PromptPipeline assets");

  for (const auto& judge_case : cases) {
    const auto result =
        manager.generate(make_request(judge_case, options.prompt_release_override));

    assert_true(result.has_consistent_values() && result.response.has_value(),
                "judge regression should return a consistent manager result for each case");
    assert_true(result.response->content_payload.has_value(),
                "judge regression should produce a JSON judge payload for each case");
    assert_true(result.response->prompt_id == std::optional<std::string>{"responder"} &&
                    result.response->prompt_version == std::optional<std::string>{"2026.06.01"},
                "judge regression should stamp the selected judge prompt identity onto each response");

    const std::string payload = *result.response->content_payload;
    assert_true(payload.find("\"schema_version\":\"judge_main_chain.v1\"") !=
                    std::string::npos,
                "judge regression should require the dedicated schema_version in the judge payload");

    const std::string actual_verdict = parse_json_string_field(payload, "verdict");
    const double score = parse_json_number_field(payload, "score");
    const double confidence = parse_json_number_field(payload, "confidence");
    const std::string summary_text = parse_json_string_field(payload, "summary");

    summary.outcomes.push_back(JudgeCaseOutcome{
        .request_id = judge_case.request_id,
        .split = judge_case.split,
        .expected_verdict = judge_case.expected_verdict,
        .actual_verdict = actual_verdict,
        .score = score,
        .confidence = confidence,
        .summary = summary_text,
        .resolved_route = result.resolved_route,
        .prompt_id = result.response->prompt_id.value_or(std::string()),
        .prompt_version = result.response->prompt_version.value_or(std::string()),
        .raw_payload = payload,
    });

    if (actual_verdict == "pass") {
      ++summary.pass_count;
    } else {
      ++summary.fail_count;
    }
  }

  summary.expectations_met = std::all_of(
      summary.outcomes.begin(), summary.outcomes.end(), [](const JudgeCaseOutcome& outcome) {
        if (outcome.expected_verdict != outcome.actual_verdict) {
          return false;
        }

        if (outcome.actual_verdict == "pass") {
          return outcome.score >= 0.80;
        }

        return outcome.score <= 0.50;
      });

  write_artifacts(options, cases, summary);
  return summary;
}

void assert_artifacts_exist(const fs::path& artifact_dir) {
  assert_true(fs::exists(artifact_dir / "judge-cases.jsonl"),
              "judge regression should write judge-cases.jsonl");
  assert_true(fs::exists(artifact_dir / "judge-results.jsonl"),
              "judge regression should write judge-results.jsonl");
  assert_true(fs::exists(artifact_dir / "judge-report.json"),
              "judge regression should write judge-report.json");
  assert_true(fs::exists(artifact_dir / "judge-report.md"),
              "judge regression should write judge-report.md");
  assert_true(fs::exists(artifact_dir / "status.txt"),
              "judge regression should write status.txt");
}

void test_judge_regression_selects_task_type_specific_prompt_and_writes_artifacts() {
  const SyntheticFailureFixture synthetic_failure = make_synthetic_failure_fixture();
  const fs::path artifact_dir = make_temp_dir("cognition-judge-artifacts");
  const JudgeRunSummary summary = run_judge_regression(JudgeRunnerOptions{
      .replay_root = fs::path{kReplayRoot},
      .failure_samples_root = synthetic_failure.root,
      .artifact_dir = artifact_dir,
      .prompt_release_override = std::nullopt,
      .max_curated_cases = std::nullopt,
  });

  assert_true(summary.curated_case_count >= 4U,
              "judge regression should consume the curated replay success-chain corpus");
  assert_equal(1, static_cast<int>(summary.supplemental_case_count),
               "judge regression should load one synthetic supplemental failure-sample case in focused validation");
  assert_true(summary.pass_count == summary.curated_case_count,
              "judge regression should map curated replay cases to pass verdicts in the mock-backed focused validation");
  assert_equal(1, static_cast<int>(summary.fail_count),
               "judge regression should keep one fail verdict for the supplemental failure sample");
  assert_true(summary.expectations_met,
              "judge regression should satisfy verdict and score expectations across all focused cases");
  assert_true(summary.observed_requests.size() == summary.curated_case_count +
                                               summary.supplemental_case_count,
              "judge regression should dispatch one provider request per replay case");

  for (const auto& request : summary.observed_requests) {
    assert_true(request.messages.has_value() && request.messages->size() == 2U,
                "judge regression should keep composed system/user messages on the provider request");
    assert_true(request.messages->at(1).find("Case Summary：") != std::string::npos &&
                    request.messages->at(1).find("Trace Bundle：") != std::string::npos,
                "judge regression should compose the judge task template with case summary and trace bundle slots");
  }

  assert_artifacts_exist(artifact_dir);

  const std::string report_json = read_text_file(artifact_dir / "judge-report.json");
  const std::string cases_jsonl = read_text_file(artifact_dir / "judge-cases.jsonl");
  const std::string results_jsonl = read_text_file(artifact_dir / "judge-results.jsonl");
  const std::string status = read_text_file(artifact_dir / "status.txt");

  assert_true(report_json.find("\"expectations_met\":true") != std::string::npos,
              "judge regression report should record expectations_met=true");
  assert_true(cases_jsonl.find("req_replay_build_projection") != std::string::npos &&
                  cases_jsonl.find("req_replay_decide_direct") != std::string::npos &&
                  cases_jsonl.find("req_replay_decide_planning_fallback") != std::string::npos &&
                  cases_jsonl.find("req_replay_reflect_continue") != std::string::npos,
              "judge regression cases artifact should enumerate the curated replay request ids");
  assert_true(results_jsonl.find("\"prompt_version\":\"2026.06.01\"") != std::string::npos,
              "judge regression results artifact should persist the selected prompt release identity");
  assert_true(trim_copy(status) == "PASS",
              "judge regression status artifact should mark the focused validation as PASS");
}

void test_judge_regression_forwards_explicit_prompt_release_override() {
  const fs::path artifact_dir = make_temp_dir("cognition-judge-override");
  const JudgeRunSummary summary = run_judge_regression(JudgeRunnerOptions{
      .replay_root = fs::path{kReplayRoot},
      .failure_samples_root = std::nullopt,
      .artifact_dir = artifact_dir,
      .prompt_release_override = std::string(kJudgePromptRelease),
      .max_curated_cases = 1U,
  });

  assert_equal(1, static_cast<int>(summary.curated_case_count),
               "judge regression override case should run a single curated replay item");
  assert_equal(0, static_cast<int>(summary.supplemental_case_count),
               "judge regression override case should not require supplemental failure samples");
  assert_true(summary.expectations_met,
              "judge regression override case should still satisfy verdict expectations");
  assert_equal(1, static_cast<int>(summary.observed_requests.size()),
               "judge regression override case should dispatch exactly one provider request");
  assert_true(summary.observed_requests.front().messages.has_value() &&
                  summary.observed_requests.front().messages->at(1).find(
                      "Prompt Release：responder@2026.06.01") != std::string::npos,
              "judge regression override case should surface the explicit prompt release id inside the composed task message");
}

void run_batch_mode_from_environment() {
  const auto artifact_dir = read_env("DASALL_COGNITION_JUDGE_ARTIFACT_DIR");
  assert_true(artifact_dir.has_value(),
              "judge regression batch mode requires DASALL_COGNITION_JUDGE_ARTIFACT_DIR");

  const auto replay_root = read_env("DASALL_COGNITION_JUDGE_REPLAY_DIR")
                               .value_or(std::string(kReplayRoot));
  const auto failure_root = read_env("DASALL_COGNITION_JUDGE_FAILURE_SAMPLES_DIR");
  const auto prompt_release_override =
      read_env("DASALL_COGNITION_JUDGE_PROMPT_RELEASE_ID");

  const JudgeRunSummary summary = run_judge_regression(JudgeRunnerOptions{
      .replay_root = replay_root,
      .failure_samples_root = failure_root.has_value()
                                  ? std::optional<fs::path>{*failure_root}
                                  : std::nullopt,
      .artifact_dir = *artifact_dir,
      .prompt_release_override = prompt_release_override,
      .max_curated_cases = std::nullopt,
  });

  assert_true(summary.expectations_met,
              "judge regression batch mode should fail closed when any case verdict drifts from expectations");
  std::cout << "artifact_dir=" << summary.artifact_dir << '\n';
  std::cout << "curated_case_count=" << summary.curated_case_count << '\n';
  std::cout << "supplemental_case_count=" << summary.supplemental_case_count << '\n';
  std::cout << "pass_count=" << summary.pass_count << '\n';
  std::cout << "fail_count=" << summary.fail_count << '\n';
}

}  // namespace

int main() {
  try {
    if (read_env("DASALL_COGNITION_JUDGE_ARTIFACT_DIR").has_value()) {
      run_batch_mode_from_environment();
    } else {
      test_judge_regression_selects_task_type_specific_prompt_and_writes_artifacts();
      test_judge_regression_forwards_explicit_prompt_release_override();
    }
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }

  return 0;
}