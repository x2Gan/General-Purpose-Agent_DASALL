#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#include "support/TestAssertions.h"

namespace {

using dasall::tests::support::assert_true;

[[nodiscard]] std::filesystem::path repository_root() {
  return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream stream(path);
  assert_true(stream.is_open(),
              "installed security wording guard should open " + path.string());
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

void assert_contains_all(std::string_view text,
                         std::initializer_list<std::string_view> needles,
                         std::string_view message_prefix) {
  for (const auto needle : needles) {
    assert_true(text.find(needle) != std::string_view::npos,
                std::string(message_prefix) + " should contain '" +
                    std::string(needle) + "'");
  }
}

void test_top_level_gap_ledger_keeps_local_installed_security_matrix() {
  const auto text = read_text_file(
      repository_root() / "docs/todos/DASALL_子系统查漏补缺专项记录.md");

  assert_contains_all(
      text,
      {
          "ACC-FIX-005",
          "Local installed security matrix",
          "pkg_smoke_install.sh --explicit-start-check",
          "DaemonSocketModeIntegrationTest",
          "AccessPolicyBackendUnavailableIntegrationTest",
          "不以 qemu / kvm 作为 Access owner 当前验收前置",
      },
      "Top-level Access security matrix ledger");
}

void test_access_todo_keeps_installed_security_acceptance_local() {
  const auto text = read_text_file(
      repository_root() / "docs/todos/access/DASALL_access子系统专项TODO.md");

  assert_contains_all(
      text,
      {
          "access-installed-async-receipt-proof.json",
          "access-installed-gateway-http-proof.json",
          "status_owner_mismatch",
          "cancel_owner_mismatch",
          "diag_disabled",
          "DaemonSocketModeIntegrationTest",
          "AccessPolicyBackendUnavailableIntegrationTest",
          "不以 qemu / kvm 作为 Access owner 当前验收前置",
      },
      "Access TODO installed security matrix");
}

void test_packaging_readme_keeps_access_matrix_authoritative_locally() {
  const auto text = read_text_file(
      repository_root() / "scripts/packaging/README.md");

  assert_contains_all(
      text,
      {
          "Access installed security matrix",
          "access-installed-async-receipt-proof.json",
          "access-installed-gateway-http-proof.json",
          "status_owner_mismatch",
          "cancel_owner_mismatch",
          "negative_listener_exposed=false",
          "DaemonSocketModeIntegrationTest",
          "AccessPolicyBackendUnavailableIntegrationTest",
      },
      "packaging README access matrix");
}

}  // namespace

int main() {
  try {
    test_top_level_gap_ledger_keeps_local_installed_security_matrix();
    test_access_todo_keeps_installed_security_acceptance_local();
    test_packaging_readme_keeps_access_matrix_authoritative_locally();
  } catch (const std::exception& ex) {
    std::cerr << "[AccessInstalledSecurityMatrixWordingGuardIntegrationTest] FAILED: "
              << ex.what() << '\n';
    return 1;
  }

  return 0;
}