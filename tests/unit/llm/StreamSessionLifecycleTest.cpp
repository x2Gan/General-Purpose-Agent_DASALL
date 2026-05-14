#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "support/TestAssertions.h"

#include "../../../llm/src/LLMManager.h"
#include "../../../llm/src/adapters/LocalLLMAdapter.h"
#include "../../../llm/src/adapters/OllamaAdapter.h"
#include "../../../llm/src/adapters/OpenAICompatibleAdapter.h"

namespace {

using dasall::contracts::LLMRequest;
using dasall::contracts::LLMRequestMode;
using dasall::contracts::ResultCode;
using dasall::llm::LLMFailureCategory;
using dasall::llm::LLMGenerateRequest;
using dasall::llm::LLMManager;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

LLMRequest make_stream_request() {
  LLMRequest request;
  request.request_id = "req-036-stream";
  request.llm_call_id = "call-036-stream";
  request.model_route = "cloud.default";
  request.request_mode = LLMRequestMode::Streaming;
  request.messages = std::vector<std::string>{"user: stream this response"};
  request.prompt_id = "prompt.stream.guard";
  request.prompt_version = "2026-05-14.1";
  request.max_output_tokens = 128U;
  return request;
}

LLMGenerateRequest make_generate_request() {
  return LLMGenerateRequest{
      .stage = "planning",
      .task_type = "plan",
      .request = make_stream_request(),
      .prompt_release_id_override = std::nullopt,
      .selection_hint = nullptr,
  };
}

void test_manager_stream_generate_remains_fail_closed_until_lifecycle_owner_exists() {
  LLMManager manager;

  const auto result = manager.stream_generate(make_generate_request(), nullptr);

  assert_true(result.has_consistent_values(),
              "stream_generate() failure should preserve LLMManagerResult invariants");
  assert_true(!result.response.has_value(),
              "stream_generate() should not fabricate a streaming response while lifecycle is deferred");
  assert_true(result.error.has_value(),
              "stream_generate() should return explicit error information while lifecycle is deferred");
  assert_true(result.code.has_value(),
              "stream_generate() should carry a shared ResultCode for runtime fail-closed handling");
  assert_equal(static_cast<int>(ResultCode::RuntimeRetryExhausted),
               static_cast<int>(*result.code),
               "stream_generate() should keep the current fail-closed result code");
  assert_true(result.failure_category.has_value(),
              "stream_generate() should classify the deferred path for observability");
  assert_equal(static_cast<int>(LLMFailureCategory::AdapterTransport),
               static_cast<int>(*result.failure_category),
               "stream_generate() should not report PromptGovernance or Routing when no session was opened");
  assert_equal("llm.manager.stream_generate", result.error->details.stage,
               "stream_generate() should keep a dedicated manager stage anchor");
  assert_equal("stream_unavailable", result.error->source_ref.ref_id,
               "stream_generate() should expose a stable deferred-streaming source ref");
  assert_true(!result.error->safe_to_replan.value_or(true),
              "stream_generate() deferral is not a ContextOrchestrator recompose signal");
  assert_true(result.attempted_routes.empty(),
              "stream_generate() should not touch route execution before StreamSessionRegistry exists");
}

void test_adapter_stream_generate_returns_stable_placeholder_session_refs() {
  const auto request = make_stream_request();

  dasall::llm::OpenAICompatibleAdapter openai_adapter(nullptr);
  dasall::llm::OllamaAdapter ollama_adapter(nullptr);
  dasall::llm::LocalLLMAdapter local_adapter(nullptr);

  assert_equal("openai-compatible-streaming-not-implemented",
               openai_adapter.stream_generate(request, nullptr).session_id,
               "OpenAI-compatible streaming should remain a placeholder until lifecycle is implemented");
  assert_equal("ollama-native-streaming-not-implemented",
               ollama_adapter.stream_generate(request, nullptr).session_id,
               "Ollama streaming should remain a placeholder until lifecycle is implemented");
  assert_equal("local-runtime-streaming-not-implemented",
               local_adapter.stream_generate(request, nullptr).session_id,
               "Local runtime streaming should remain a placeholder until lifecycle is implemented");
}

}  // namespace

int main() {
  try {
    test_manager_stream_generate_remains_fail_closed_until_lifecycle_owner_exists();
    test_adapter_stream_generate_returns_stable_placeholder_session_refs();
  } catch (const std::exception& error) {
    std::cerr << "StreamSessionLifecycleTest failed: " << error.what() << '\n';
    return 1;
  }

  return 0;
}
