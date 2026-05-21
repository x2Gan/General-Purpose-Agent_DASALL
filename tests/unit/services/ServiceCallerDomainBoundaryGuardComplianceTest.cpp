#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "support/TestAssertions.h"

namespace {

[[nodiscard]] std::filesystem::path repo_root() {
#ifdef DASALL_REPO_ROOT
  return std::filesystem::path(DASALL_REPO_ROOT);
#else
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
#endif
}

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
  std::ifstream stream(path);
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

[[nodiscard]] std::string relative_path(const std::filesystem::path& path) {
  return path.lexically_relative(repo_root()).string();
}

void assert_file_excludes_tokens(const std::filesystem::path& path,
                                 const std::vector<std::string_view>& tokens) {
  using dasall::tests::support::assert_true;

  const std::string text = read_text(path);
  for (const auto token : tokens) {
    assert_true(text.find(token) == std::string::npos,
                relative_path(path) + " must not define or consume " + std::string(token));
  }
}

void assert_file_contains_tokens(const std::filesystem::path& path,
                                 const std::vector<std::string_view>& tokens) {
  using dasall::tests::support::assert_true;

  const std::string text = read_text(path);
  for (const auto token : tokens) {
    assert_true(text.find(token) != std::string::npos,
                relative_path(path) + " must retain caller-domain owner token " +
                    std::string(token));
  }
}

void test_services_do_not_define_caller_domain_admission_owner() {
  const auto root = repo_root();

  assert_file_excludes_tokens(root / "services" / "include" / "ServiceTypes.h",
                              {"caller_domain"});
  assert_file_excludes_tokens(root / "services" / "src" / "adapters" / "AdapterRouter.h",
                              {"caller_domain", "caller_domain_allowlist", "allowed_tool_domains"});
  assert_file_excludes_tokens(root / "services" / "src" / "adapters" / "AdapterRouter.cpp",
                              {"caller_domain", "caller_domain_allowlist", "allowed_tool_domains"});
  assert_file_excludes_tokens(root / "services" / "src" / "ops" / "ServiceConfigAdapter.cpp",
                              {"caller_domain", "caller_domain_allowlist", "allowed_tool_domains"});
}

void test_tool_bridge_does_not_smuggle_caller_domain_into_services_context() {
  const auto root = repo_root();

  assert_file_excludes_tokens(root / "tools" / "src" / "bridge" / "ToolServiceBridge.cpp",
                              {"caller_domain", "allowed_tool_domains", "caller_domain_allowlist"});
}

void test_tools_and_access_retain_upstream_policy_gate_owner_markers() {
  const auto root = repo_root();

  assert_file_contains_tokens(root / "tools" / "src" / "policy" / "ToolPolicyGate.cpp",
                              {"check_allowed_domain", "allowed_tool_domains", "caller_domain",
                               "policy.domain_denied"});
  assert_file_contains_tokens(root / "tools" / "src" / "ToolManager.cpp",
                              {"derive_requested_domain", ".caller_domain = requested_domain",
                               "policy_gate->evaluate"});
  assert_file_contains_tokens(root / "access" / "src" / "AccessPolicyGate.cpp",
                              {"AccessPolicyGate::evaluate_with_evaluator",
                               "authentication_required", "evaluator.evaluate"});
}

}  // namespace

int main() {
  try {
    test_services_do_not_define_caller_domain_admission_owner();
    test_tool_bridge_does_not_smuggle_caller_domain_into_services_context();
    test_tools_and_access_retain_upstream_policy_gate_owner_markers();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}