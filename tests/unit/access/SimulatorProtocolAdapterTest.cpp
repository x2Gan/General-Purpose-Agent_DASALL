/// tests/unit/access/SimulatorProtocolAdapterTest.cpp
///
/// DASALL simulator protocol adapter 单元测试
#include <iostream>
#include <string_view>

#include "SimulatorProtocolAdapter.h"
#include "AccessTypes.h"
#include "support/TestAssertions.h"

namespace dasall::access::simulator::test {

void simulator_adapter_can_handle_deterministic_test_protocol() {
  DeterministicSubjectStub subject{"test_actor", {"action1", "action2"}, ""};
  SimulatorProtocolAdapter adapter(subject);

  dasall::tests::support::assert_true(
      adapter.can_handle("simulator", "deterministic_test"),
      "SimulatorProtocolAdapter should handle simulator/deterministic_test");
}

void simulator_adapter_rejects_non_simulator_entries() {
  DeterministicSubjectStub subject{"test_actor", {"action1"}, ""};
  SimulatorProtocolAdapter adapter(subject);

  dasall::tests::support::assert_true(
      !adapter.can_handle("http", "unary"),
      "SimulatorProtocolAdapter should not handle http/unary");
  dasall::tests::support::assert_true(
      !adapter.can_handle("cli", "command"),
      "SimulatorProtocolAdapter should not handle cli/command");
  dasall::tests::support::assert_true(
      !adapter.can_handle("daemon", "ipc"),
      "SimulatorProtocolAdapter should not handle daemon/ipc");
}

void simulator_adapter_decode_empty_request() {
  DeterministicSubjectStub subject{"test_actor", {}, ""};
  SimulatorProtocolAdapter adapter(subject);

  auto packet = adapter.decode();
  dasall::tests::support::assert_equal(
      "simulator", packet.entry_type,
      "decode empty request should return default entry_type");
  dasall::tests::support::assert_equal(
      "simulator_local", packet.peer_ref,
      "decode empty request should return simulator_local peer_ref");
}

void simulator_adapter_decode_extracts_entry_type() {
  DeterministicSubjectStub subject{"test_actor", {}, ""};
  SimulatorProtocolAdapter adapter(subject);

  const std::string request_json = R"({
    "entry_type": "task_query",
    "request_id": "req-123",
    "payload": {"op": "query"}
  })";
  adapter.set_active_request(request_json);

  auto packet = adapter.decode();
  dasall::tests::support::assert_equal(
      "task_query", packet.entry_type,
      "decode should extract entry_type from JSON");
}

void simulator_adapter_uses_deterministic_subject() {
  DeterministicSubjectStub subject{"fixture_actor_123", {"action1", "action2"}, ""};
  SimulatorProtocolAdapter adapter(subject);

  auto packet = adapter.decode();
  dasall::tests::support::assert_equal(
      "fixture_actor_123", packet.packet_id,
      "decode should use fixture actor_ref in packet_id");
}

void simulator_adapter_encode_returns_true() {
  DeterministicSubjectStub subject{"test_actor", {}, ""};
  SimulatorProtocolAdapter adapter(subject);

  dasall::access::PublishEnvelope envelope;
  envelope.result_id = "result-abc-123";
  envelope.session_id = "session-det-001";

  bool result = adapter.encode(envelope);
  dasall::tests::support::assert_true(
      result,
      "encode should return true");

  const auto& response = adapter.active_response_body();
  dasall::tests::support::assert_true(
      !response.empty(),
      "encode should populate response body");
  dasall::tests::support::assert_true(
      response.find("accepted") != std::string::npos,
      "encode response should contain 'accepted'");
  dasall::tests::support::assert_true(
      response.find("202") != std::string::npos,
      "encode response should contain status code '202'");
}

}  // namespace dasall::access::simulator::test

int main() {
  using namespace dasall::access::simulator::test;

  std::cout << "Running SimulatorProtocolAdapterTest..." << std::endl;

  simulator_adapter_can_handle_deterministic_test_protocol();
  simulator_adapter_rejects_non_simulator_entries();
  simulator_adapter_decode_empty_request();
  simulator_adapter_decode_extracts_entry_type();
  simulator_adapter_uses_deterministic_subject();
  simulator_adapter_encode_returns_true();

  std::cout << "All SimulatorProtocolAdapterTest tests passed!" << std::endl;
  return 0;
}
