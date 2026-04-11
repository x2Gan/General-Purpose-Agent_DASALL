#include <exception>
#include <iostream>
#include <string>

#include "support/TestAssertions.h"

#include "../../../llm/src/prompt/TemplateRenderer.h"

namespace {

void test_renderer_replaces_simple_variables() {
  using dasall::llm::prompt::TemplateRenderer;
  using dasall::llm::prompt::TemplateRendererConfig;
  using dasall::llm::prompt::TemplateVariables;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  TemplateRenderer renderer;
  assert_true(renderer.init(TemplateRendererConfig{}),
              "TemplateRenderer should initialize with the default simple_var engine");

  const TemplateVariables variables = {
      {"system_instructions", "Plan carefully."},
      {"user_goal", "Summarize the diagnostics."},
  };

  const auto result = renderer.render(
      "system: {{system_instructions}}\nuser: {{user_goal}}", variables);

  assert_equal(std::string("system: Plan carefully.\nuser: Summarize the diagnostics."),
               result.rendered_text,
               "TemplateRenderer should replace matching simple_var placeholders");
  assert_equal(0, static_cast<int>(result.warnings.size()),
               "TemplateRenderer should not emit warnings for clean substitutions");
  assert_true(!result.nested_render_rejected,
              "TemplateRenderer should keep nested_render_rejected false for clean substitutions");
  assert_true(!result.truncated_values,
              "TemplateRenderer should keep truncated_values false for clean substitutions");
}

void test_renderer_keeps_unmatched_variable_and_emits_warning() {
  using dasall::llm::prompt::TemplateRenderer;
  using dasall::llm::prompt::TemplateRendererConfig;
  using dasall::llm::prompt::TemplateVariables;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  TemplateRenderer renderer;
  assert_true(renderer.init(TemplateRendererConfig{}),
              "TemplateRenderer should initialize before unmatched variable testing");

  const TemplateVariables variables = {
      {"user_goal", "Inspect the prompt package."},
  };

  const auto result = renderer.render(
      "task: {{user_goal}} / missing: {{missing_slot}}", variables);

  assert_equal(std::string("task: Inspect the prompt package. / missing: {{missing_slot}}"),
               result.rendered_text,
               "TemplateRenderer should preserve unmatched placeholders as literal text");
  assert_equal(1, static_cast<int>(result.warnings.size()),
               "TemplateRenderer should emit one warning for one unmatched placeholder");
  assert_equal(std::string("unmatched_variable:missing_slot"), result.warnings.front(),
               "TemplateRenderer should identify the missing placeholder by name");
}

void test_renderer_rejects_nested_render_attempts_by_escaping_delimiters() {
  using dasall::llm::prompt::TemplateRenderer;
  using dasall::llm::prompt::TemplateRendererConfig;
  using dasall::llm::prompt::TemplateVariables;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  TemplateRenderer renderer;
  assert_true(renderer.init(TemplateRendererConfig{}),
              "TemplateRenderer should initialize before nested render testing");

  const TemplateVariables variables = {
      {"system_instructions", "literal {{danger}} payload"},
  };

  const auto result = renderer.render("{{system_instructions}}", variables);

  assert_equal(std::string("literal \\{\\{danger\\}\\} payload"), result.rendered_text,
               "TemplateRenderer should escape nested delimiters instead of leaving raw tags");
  assert_equal(1, static_cast<int>(result.warnings.size()),
               "TemplateRenderer should emit one warning for nested render rejection");
  assert_equal(std::string("nested_render_rejected:system_instructions"),
               result.warnings.front(),
               "TemplateRenderer should identify the slot that attempted nested rendering");
  assert_true(result.nested_render_rejected,
              "TemplateRenderer should mark nested render attempts as rejected");
}

void test_renderer_truncates_overlong_values() {
  using dasall::llm::prompt::TemplateRenderer;
  using dasall::llm::prompt::TemplateRendererConfig;
  using dasall::llm::prompt::TemplateVariables;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  TemplateRenderer renderer;
  assert_true(renderer.init(TemplateRendererConfig{
                  .template_engine = "simple_var",
                  .max_variable_length = 8U,
              }),
              "TemplateRenderer should initialize with a tighter per-variable length guard");

  const TemplateVariables variables = {
      {"user_goal", "1234567890"},
  };

  const auto result = renderer.render("goal={{user_goal}}", variables);

  assert_equal(std::string("goal=12345678"), result.rendered_text,
               "TemplateRenderer should truncate values that exceed the configured length cap");
  assert_equal(1, static_cast<int>(result.warnings.size()),
               "TemplateRenderer should emit one warning for a truncated variable");
  assert_equal(std::string("value_truncated:user_goal"), result.warnings.front(),
               "TemplateRenderer should identify the truncated placeholder by name");
  assert_true(result.truncated_values,
              "TemplateRenderer should expose that at least one value was truncated");
}

void test_renderer_escapes_special_delimiters_in_values() {
  using dasall::llm::prompt::TemplateRenderer;
  using dasall::llm::prompt::TemplateRendererConfig;
  using dasall::llm::prompt::TemplateVariables;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  TemplateRenderer renderer;
  assert_true(renderer.init(TemplateRendererConfig{}),
              "TemplateRenderer should initialize before delimiter escaping testing");

  const TemplateVariables variables = {
      {"policy_note", "keep }} and {{ as text"},
  };

  const auto result = renderer.render("note: {{policy_note}}", variables);

  assert_equal(std::string("note: keep \\}\\} and \\{\\{ as text"), result.rendered_text,
               "TemplateRenderer should escape literal template delimiters inside values");
  assert_equal(std::string("nested_render_rejected:policy_note"), result.warnings.front(),
               "TemplateRenderer should report delimiter escaping as a nested render rejection");
}

}  // namespace

int main() {
  try {
    test_renderer_replaces_simple_variables();
    test_renderer_keeps_unmatched_variable_and_emits_warning();
    test_renderer_rejects_nested_render_attempts_by_escaping_delimiters();
    test_renderer_truncates_overlong_values();
    test_renderer_escapes_special_delimiters_in_values();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}