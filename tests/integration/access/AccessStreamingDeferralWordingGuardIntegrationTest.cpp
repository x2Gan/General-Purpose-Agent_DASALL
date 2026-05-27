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
              "streaming wording guard should open " + path.string());
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

void test_system_gate_matrix_keeps_gate_int_08_unary_only() {
  const auto text = read_text_file(
      repository_root() / "docs/ssot/SystemIntegrationGateMatrix.md");

  assert_contains_all(
      text,
      {
          "Gate-INT-08",
          "Access v1 unary production ingress gate",
          "ACC-GATE-11",
          "feature flag default-off",
          "StreamGateway / WS / MQTT",
      },
      "SystemIntegrationGateMatrix");
}

void test_access_todo_keeps_streaming_under_acc_gate_11() {
  const auto text = read_text_file(
      repository_root() / "docs/todos/access/DASALL_access子系统专项TODO.md");

  assert_contains_all(
      text,
      {
          "Gate-INT-08",
          "ACC-GATE-11",
          "feature flag default-off",
          "disabled/not ready",
          "StreamGateway / WS / MQTT",
      },
      "Access streaming TODO wording");
}

void test_gateway_sources_keep_streaming_disabled_in_v1() {
  const auto header_text = read_text_file(
      repository_root() / "apps/gateway/src/HttpProtocolAdapter.h");
  assert_contains_all(
      header_text,
      {
          "POST /v1/submit",
          "streaming 路径延后到 A5",
      },
      "HttpProtocolAdapter.h");

  const auto main_text = read_text_file(
      repository_root() / "apps/gateway/src/main.cpp");
  assert_contains_all(
      main_text,
      {
          "unary POST + accepted async receipt",
          "无 WebSocket/MQTT",
      },
      "apps/gateway/src/main.cpp");
}

}  // namespace

int main() {
  try {
    test_system_gate_matrix_keeps_gate_int_08_unary_only();
    test_access_todo_keeps_streaming_under_acc_gate_11();
    test_gateway_sources_keep_streaming_disabled_in_v1();
  } catch (const std::exception& ex) {
    std::cerr << "[AccessStreamingDeferralWordingGuardIntegrationTest] FAILED: "
              << ex.what() << '\n';
    return 1;
  }

  return 0;
}