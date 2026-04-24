/// tests/unit/access/SimulatorProtocolAdapterDeterministicTest.cpp
///
/// DASALL simulator protocol adapter 确定性行为测试
#include <iostream>
#include <string>

#include "SimulatorProtocolAdapter.h"
#include "AccessTypes.h"
#include "support/TestAssertions.h"

namespace dasall::access::simulator::test {

void multiple_decode_calls_produce_identical_packets() {
  DeterministicSubjectStub subject{"deterministic_actor_v1", {"action_a", "action_b"}, ""};
  SimulatorProtocolAdapter adapter(subject);

  const std::string request_json = R"({
    "entry_type": "execute",
    "request_id": "req-det-001"
  })";
  adapter.set_active_request(request_json);

  auto packet1 = adapter.decode();
  auto packet2 = adapter.decode();

  dasall::tests::support::assert_equal(
      packet1.packet_id, packet2.packet_id,
      "multiple decode calls should return identical packet_id");
  dasall::tests::support::assert_equal(
      packet1.entry_type, packet2.entry_type,
      "multiple decode calls should return identical entry_type");
  dasall::tests::support::assert_equal(
      packet1.peer_ref, packet2.peer_ref,
      "multiple decode calls should return identical peer_ref");
}

void different_fixtures_produce_different_packets() {
  DeterministicSubjectStub subject1{"actor_alice", {"x", "y"}, ""};
  DeterministicSubjectStub subject2{"actor_bob", {"p", "q"}, ""};
  
  SimulatorProtocolAdapter adapter1(subject1);
  SimulatorProtocolAdapter adapter2(subject2);

  auto packet1 = adapter1.decode();
  auto packet2 = adapter2.decode();

  dasall::tests::support::assert_true(
      packet1.packet_id != packet2.packet_id,
      "different fixtures should produce different packets");
  dasall::tests::support::assert_equal(
      "actor_alice", packet1.packet_id,
      "first fixture should yield actor_alice");
  dasall::tests::support::assert_equal(
      "actor_bob", packet2.packet_id,
      "second fixture should yield actor_bob");
}

void multiple_encode_calls_consistent() {
  DeterministicSubjectStub subject{"test_actor", {}, ""};
  SimulatorProtocolAdapter adapter(subject);

  dasall::access::PublishEnvelope envelope;
  envelope.result_id = "receipt-det-123";
  envelope.session_id = "session-det-123";

  bool result1 = adapter.encode(envelope);
  const auto& response1 = adapter.active_response_body();

  bool result2 = adapter.encode(envelope);
  const auto& response2 = adapter.active_response_body();

  dasall::tests::support::assert_true(
      result1 && result2,
      "encode should consistently return true");
  dasall::tests::support::assert_equal(
      response1, response2,
      "encode should consistently produce same response for same envelope");
}

void fixture_actions_unchanged() {
  std::vector<std::string> actions{"read", "write", "delete"};
  DeterministicSubjectStub subject{"factory_actor", actions, ""};
  SimulatorProtocolAdapter adapter(subject);

  dasall::tests::support::assert_equal(
      size_t(3), subject.granted_actions.size(),
      "fixture actions should remain size 3");
  dasall::tests::support::assert_equal(
      "read", subject.granted_actions[0],
      "fixture action[0] should be 'read'");
  dasall::tests::support::assert_equal(
      "write", subject.granted_actions[1],
      "fixture action[1] should be 'write'");
  dasall::tests::support::assert_equal(
      "delete", subject.granted_actions[2],
      "fixture action[2] should be 'delete'");

  // 多次 decode 不改变 subject 状态
  (void)adapter.decode();
  (void)adapter.decode();

  dasall::tests::support::assert_equal(
      size_t(3), subject.granted_actions.size(),
      "fixture actions should still be size 3 after decodes");
}

void replay_scenario_deterministic() {
  const std::string request_json = R"({
    "entry_type": "replay_test",
    "request_id": "replay-001",
    "payload": {"seed": 42}
  })";

  // 第一次重放
  std::string packet1_id;
  {
    DeterministicSubjectStub subject{"replay_actor", {"action1"}, ""};
    SimulatorProtocolAdapter adapter(subject);
    adapter.set_active_request(request_json);
    auto packet1 = adapter.decode();
    packet1_id = packet1.packet_id;
  }

  // 第二次重放（相同 fixture）
  std::string packet2_id;
  {
    DeterministicSubjectStub subject{"replay_actor", {"action1"}, ""};
    SimulatorProtocolAdapter adapter(subject);
    adapter.set_active_request(request_json);
    auto packet2 = adapter.decode();
    packet2_id = packet2.packet_id;
  }

  dasall::tests::support::assert_equal(
      packet1_id, packet2_id,
      "replay scenario should produce identical packets");
  dasall::tests::support::assert_equal(
      "replay_actor", packet1_id,
      "replay packet should have correct actor");
}

}  // namespace dasall::access::simulator::test

int main() {
  using namespace dasall::access::simulator::test;

  std::cout << "Running SimulatorProtocolAdapterDeterministicTest..." << std::endl;

  multiple_decode_calls_produce_identical_packets();
  different_fixtures_produce_different_packets();
  multiple_encode_calls_consistent();
  fixture_actions_unchanged();
  replay_scenario_deterministic();

  std::cout << "All SimulatorProtocolAdapterDeterministicTest tests passed!" << std::endl;
  return 0;
}
