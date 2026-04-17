#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "skills/ExternalSkillImporter.h"
#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

[[nodiscard]] std::filesystem::path project_root() {
  auto root = std::filesystem::path(__FILE__);
  for (int level = 0; level < 4; ++level) {
    root = root.parent_path();
  }

  return root;
}

struct TempDirectory {
  std::filesystem::path path;

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

[[nodiscard]] TempDirectory make_temp_directory(const std::string& prefix) {
  const auto unique_suffix = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto path = std::filesystem::temp_directory_path() /
                    (prefix + "-" + unique_suffix);
  std::filesystem::create_directories(path);
  return TempDirectory{.path = path};
}

void write_text_file(const std::filesystem::path& path, const std::string& content) {
  std::ofstream stream(path);
  stream << content;
}

[[nodiscard]] bool has_reason_code(
    const std::vector<dasall::tools::skills::SkillImportDiagnostic>& diagnostics,
    const std::string& reason_code) {
  return std::any_of(diagnostics.begin(), diagnostics.end(),
                     [&reason_code](const auto& diagnostic) {
                       return diagnostic.reason_code == reason_code;
                     });
}

[[nodiscard]] dasall::tools::skills::ExternalSkillImporter make_importer(bool enabled) {
  return dasall::tools::skills::ExternalSkillImporter(
      dasall::tools::skills::SkillImporterOptions{
          .external_skill_import_enabled = enabled,
          .project_root = project_root(),
      });
}

void test_feature_flag_disabled_rejects_external_import() {
  const auto importer = make_importer(false);
  const auto result = importer.import_directory(
      "external:github",
      "skills/external_dialects/github",
      std::string("github.skills"));

  assert_equal(0, static_cast<int>(result.imported_assets.size()),
               "feature-disabled importer should not normalize any external skill assets");
  assert_true(has_reason_code(result.diagnostics, "skill.importer.feature_disabled"),
              "feature-disabled importer should emit the feature_disabled diagnostic");
}

void test_github_sample_is_normalized_to_skill_asset() {
  const auto importer = make_importer(true);
  const auto result = importer.import_directory(
      "external:github",
      "skills/external_dialects/github",
      std::string("github.skills"));

  assert_equal(2, static_cast<int>(result.imported_assets.size()),
               "GitHub-style sample fixture should normalize to two skill assets");
  const auto it = std::find_if(result.imported_assets.begin(), result.imported_assets.end(),
      [](const auto& a) { return a.name == "runtime-incident"; });
  assert_true(it != result.imported_assets.end(),
              "GitHub-style sample should contain the runtime-incident skill asset");
  const auto& asset = *it;
  assert_equal(std::string("external:github"), asset.source_key,
               "normalized asset should preserve the caller-provided source key");
  assert_equal(std::string("runtime-incident"), asset.name,
               "normalized asset should preserve the frontmatter skill name");
  assert_equal(3, static_cast<int>(asset.allowed_tools.size()),
               "supporting workflow fixture should provide the normalized tool allowlist");
  assert_equal(std::string("skills/external_dialects/github/runtime-incident/workflow.yaml"),
               asset.workflow_template_ref,
               "normalized workflow ref should be project-root relative");
  assert_equal(std::string("builtin-summary"), asset.fallback_mode,
               "supporting workflow fixture should preserve the fallback mode");
}

void test_claude_sample_uses_supporting_workflow_allowlist() {
  const auto importer = make_importer(true);
  const auto result = importer.import_directory(
      "external:claude",
      "skills/external_dialects/claude",
      std::string("claude.skills"));

  assert_equal(2, static_cast<int>(result.imported_assets.size()),
               "Claude-style sample fixture should normalize to two skill assets");
  const auto it = std::find_if(result.imported_assets.begin(), result.imported_assets.end(),
      [](const auto& a) { return a.name == "runtime-incident"; });
  assert_true(it != result.imported_assets.end(),
              "Claude-style sample should contain the runtime-incident skill asset");
  const auto& asset = *it;
  assert_equal(std::string("external:claude"), asset.source_key,
               "normalized Claude asset should preserve the caller-provided source key");
  assert_equal(std::string("reject"), asset.fallback_mode,
               "Claude fixture should take fallback_mode from the supporting workflow file");
  assert_equal(std::string("runtime.inspect_status"), asset.allowed_tools.front(),
               "Claude fixture should normalize tool names from workflow.yaml instead of generic Read/Grep labels");
  assert_equal(2, static_cast<int>(asset.intent_patterns.size()),
               "Claude fixture should import intent keywords from workflow.yaml");
}

void test_malformed_frontmatter_is_quarantined() {
  const auto importer = make_importer(true);
  auto temp_dir = make_temp_directory("dasall-external-skill-malformed");
  write_text_file(
      temp_dir.path / "SKILL.md",
      "---\n"
      "name runtime-incident\n"
      "workflow-template: workflow.yaml\n"
      "eval-suite: eval.yaml\n"
      "---\n"
      "Broken fixture\n");

  const auto result = importer.import_directory(
      "external:temp",
      temp_dir.path,
      std::string("github.skills"));

  assert_equal(0, static_cast<int>(result.imported_assets.size()),
               "malformed frontmatter should quarantine the skill instead of normalizing it");
  assert_true(has_reason_code(result.diagnostics, "skill.importer.frontmatter_invalid"),
              "malformed frontmatter should emit the frontmatter_invalid diagnostic");
}

void test_missing_workflow_is_quarantined() {
  const auto importer = make_importer(true);
  auto temp_dir = make_temp_directory("dasall-external-skill-missing-workflow");
  write_text_file(
      temp_dir.path / "SKILL.md",
      "---\n"
      "name: runtime-incident\n"
      "description: temp external skill\n"
      "tool-allowlist:\n"
      "  - runtime.inspect_status\n"
      "workflow-template: workflow.yaml\n"
      "eval-suite: eval.yaml\n"
      "---\n"
      "Temporary fixture\n");
  write_text_file(temp_dir.path / "eval.yaml", "suite: placeholder\n");

  const auto result = importer.import_directory(
      "external:temp",
      temp_dir.path,
      std::string("github.skills"));

  assert_equal(0, static_cast<int>(result.imported_assets.size()),
               "missing workflow file should quarantine the external skill asset");
  assert_true(has_reason_code(result.diagnostics, "skill.importer.workflow_missing"),
              "missing workflow file should emit the workflow_missing diagnostic");
}

}  // namespace

int main() {
  try {
    test_feature_flag_disabled_rejects_external_import();
    test_github_sample_is_normalized_to_skill_asset();
    test_claude_sample_uses_supporting_workflow_allowlist();
    test_malformed_frontmatter_is_quarantined();
    test_missing_workflow_is_quarantined();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}