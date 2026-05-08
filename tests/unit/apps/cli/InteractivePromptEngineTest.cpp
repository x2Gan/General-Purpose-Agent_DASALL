#include <exception>
#include <iostream>
#include <optional>

#include "config/InteractivePromptEngine.h"
#include "support/TestAssertions.h"

namespace {

void test_prompt_text_reuses_default_when_input_is_empty() {
  using dasall::apps::cli::config::InteractivePromptEngine;
  using dasall::apps::cli::config::PromptRequest;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  PromptRequest seen_request;
  InteractivePromptEngine engine(
      [&seen_request](const PromptRequest& request)
          -> std::optional<std::string> {
        seen_request = request;
        return std::string();
      });

  const auto response = engine.prompt_text(
      "profile_id", "Select DASALL profile", "desktop_full");

  assert_equal(std::string("desktop_full"), seen_request.default_value,
               "InteractivePromptEngine should pass default value metadata to prompt handlers");
  assert_true(!seen_request.masked,
              "InteractivePromptEngine should mark plain text prompts as non-masked");
  assert_true(response.accepted && response.used_default,
              "InteractivePromptEngine should reuse the current/default value when prompt input is empty");
  assert_equal(std::string("desktop_full"), response.value,
               "InteractivePromptEngine should return the default value for empty input");
}

void test_prompt_secret_marks_request_as_masked() {
  using dasall::apps::cli::config::InteractivePromptEngine;
  using dasall::apps::cli::config::PromptRequest;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  bool masked_prompt_seen = false;
  InteractivePromptEngine engine(
      [&masked_prompt_seen](const PromptRequest& request)
          -> std::optional<std::string> {
        masked_prompt_seen = request.masked;
        return std::string("secret-token");
      });

  const auto response = engine.prompt_secret("api_key", "Enter provider API key");

  assert_true(masked_prompt_seen,
              "InteractivePromptEngine should mark secret prompts as masked");
  assert_true(response.accepted && response.masked,
              "InteractivePromptEngine should preserve masked prompt metadata in its response");
  assert_equal(std::string("secret-token"), response.value,
               "InteractivePromptEngine should return handler-provided secret input unchanged");
}

void test_prompt_confirm_uses_default_when_no_handler_is_bound() {
  using dasall::apps::cli::config::InteractivePromptEngine;
  using dasall::tests::support::assert_true;

  const InteractivePromptEngine engine;
  assert_true(engine.prompt_confirm("Apply the generated plan now?", true),
              "InteractivePromptEngine should fall back to the configured default confirmation value when no handler is bound");
}

}  // namespace

int main() {
  try {
    test_prompt_text_reuses_default_when_input_is_empty();
    test_prompt_secret_marks_request_as_masked();
    test_prompt_confirm_uses_default_when_no_handler_is_bound();
  } catch (const std::exception& ex) {
    std::cerr << "InteractivePromptEngineTest failed: " << ex.what() << '\n';
    return 1;
  }

  std::cout << "InteractivePromptEngineTest passed\n";
  return 0;
}