#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "LLMBackedSummarizer.h"
#include "MockLLMManager.h"
#include "memory/Turn.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::mocks::MockLLMManager;

[[nodiscard]] bool contains_prefixed_tag(
    const std::optional<std::vector<std::string>>& tags,
    std::string_view prefix,
    std::string_view fragment) {
  if (!tags.has_value()) {
    return false;
  }

  for (const auto& tag : *tags) {
    if (tag.rfind(prefix, 0U) == 0U && tag.find(fragment) != std::string::npos) {
      return true;
    }
  }

  return false;
}

[[nodiscard]] dasall::contracts::Turn make_turn(
    const std::string& turn_id,
    const std::string& session_id,
    const std::string& user_input,
    const std::string& agent_response) {
  dasall::contracts::Turn turn;
  turn.turn_id = turn_id;
  turn.session_id = session_id;
  turn.user_input = user_input;
  turn.agent_response = agent_response;
  turn.created_at = 1717000000000LL;
  return turn;
}

void test_llm_backed_summarizer_projects_structured_response() {
  using dasall::tests::support::assert_true;

  auto llm_manager = std::make_shared<MockLLMManager>();
  llm_manager->set_generate_handler(
      [](const dasall::llm::LLMGenerateRequest& request) {
        return MockLLMManager::make_structured_stage_result(
            request.stage,
            R"({"schema_version":"memory_summary.v1","request_id":"req-memory-summary","summary_text":"LLM 结构化摘要","decisions_made":["冻结 summarizer seam"],"confirmed_facts":["生产注入路径已接线"],"tool_outcomes":["build:passed"]})",
            request.request.request_id);
      });

  dasall::apps::runtime_support::LLMBackedSummarizer summarizer(llm_manager);
  const auto result = summarizer.summarize(dasall::memory::SummaryGenerationRequest{
      .session_id = "session-llm-backed-summarizer",
      .source_turns = {
          make_turn("turn-001", "session-llm-backed-summarizer",
                    "请总结本轮 memory 接缝改动。",
                    "已经完成 summarizer seam 与 runtime composition 路由。"),
      },
      .existing_summary = std::nullopt,
      .target_token_budget = 128,
      .strategy_hint = "llm",
  });

  assert_true(!result.fallback_used && !result.degraded,
              "LLM-backed summarizer compile test should stay on the structured LLM path");
  assert_true(result.projection.summary_text == "LLM 结构化摘要",
              "LLM-backed summarizer compile test should project summary_text from JSON");
  assert_true(result.projection.decisions_made.size() == 1U &&
                  result.projection.decisions_made.front() == "冻结 summarizer seam",
              "LLM-backed summarizer compile test should project decisions_made from JSON");
  assert_true(result.projection.confirmed_facts.size() == 1U &&
                  result.projection.confirmed_facts.front() == "生产注入路径已接线",
              "LLM-backed summarizer compile test should project confirmed_facts from JSON");
  assert_true(result.projection.tool_outcomes.size() == 1U &&
                  result.projection.tool_outcomes.front() == "build:passed",
              "LLM-backed summarizer compile test should project tool_outcomes from JSON");
  assert_true(result.projection.source_turn_ids == std::vector<std::string>{"turn-001"},
              "LLM-backed summarizer compile test should preserve source turn ids from the request");
  assert_true(result.warnings == std::vector<std::string>{"llm_summary_used"},
              "LLM-backed summarizer compile test should mark successful LLM usage");

  assert_true(llm_manager->generate_requests().size() == 1U,
              "LLM-backed summarizer compile test should issue exactly one LLM request");
  const auto& generate_request = llm_manager->generate_requests().front();
  assert_true(generate_request.stage == "response" &&
                  generate_request.task_type == "summary",
              "LLM-backed summarizer compile test should use the response/summary selector pair");
  assert_true(generate_request.prompt_release_id_override ==
                  std::optional<std::string>{"responder@2026.06.02"},
              "LLM-backed summarizer compile test should pin the memory summary prompt release");
  assert_true(generate_request.request.response_format ==
                  std::optional<std::string>{"json_object"},
              "LLM-backed summarizer compile test should request structured JSON output");
  assert_true(generate_request.request.output_schema_ref ==
                  std::optional<std::string>{"schema://responder/memory_summary"},
              "LLM-backed summarizer compile test should project the memory summary schema ref");
  assert_true(generate_request.request.max_output_tokens ==
                  std::optional<std::uint32_t>{128U},
              "LLM-backed summarizer compile test should honor the target token budget as output cap");
  assert_true(generate_request.request.messages.has_value() &&
                  !generate_request.request.messages->empty(),
              "LLM-backed summarizer compile test should satisfy the LLM manager message precondition");
  assert_true(contains_prefixed_tag(generate_request.request.tags, "user_goal=",
                                    "结构化摘要") &&
                  contains_prefixed_tag(generate_request.request.tags, "constraints=",
                                        "target_token_budget=128") &&
                  contains_prefixed_tag(generate_request.request.tags, "session_summary=",
                                        "turn-001"),
              "LLM-backed summarizer compile test should project prompt slot variables through tags");
}

}  // namespace

int main() {
  try {
    test_llm_backed_summarizer_projects_structured_response();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}