#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "AccessGatewayFactory.h"
#include "IAccessGateway.h"
#include "IKnowledgeService.h"
#include "KnowledgeTypes.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessDisposition;
using dasall::access::DaemonAccessPipelineOptions;
using dasall::access::InboundPacket;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

class FakeKnowledgeService final : public dasall::knowledge::IKnowledgeService {
 public:
  bool init(const dasall::knowledge::KnowledgeConfigSnapshot&) override {
    return true;
  }

  dasall::knowledge::KnowledgeRetrieveResult retrieve(
      const dasall::knowledge::KnowledgeQuery&) override {
    ++retrieve_call_count;
    dasall::knowledge::KnowledgeRetrieveResult result;
    result.ok = true;
    result.mode = dasall::knowledge::RetrievalMode::LexicalOnly;
    dasall::knowledge::EvidenceBundle evidence;
    evidence.slices.push_back(dasall::knowledge::EvidenceSlice{
        .evidence_id = "evidence:knowledge-test",
        .snippet = "DeepSeek Chat installed provider evidence",
        .citation_ref = "dasall_llm_providers#deepseek-chat",
        .confidence = 1.0F,
        .freshness = dasall::knowledge::FreshnessState::Fresh,
        .tags = {"installed"},
    });
    result.evidence = std::move(evidence);
    return result;
  }

  dasall::knowledge::KnowledgeHealthSnapshot health_snapshot() const override {
    ++health_call_count;
    dasall::knowledge::KnowledgeHealthSnapshot snapshot;
    snapshot.state = dasall::knowledge::HealthState::Degraded;
    snapshot.active_snapshot_id = "snapshot:knowledge-test";
    snapshot.freshness_state = dasall::knowledge::FreshnessState::Fresh;
    snapshot.last_known_good_available = true;
    snapshot.reason_codes = {"vector_backend_disabled"};
    return snapshot;
  }

  dasall::knowledge::RefreshResult request_refresh(
      const dasall::knowledge::CorpusChangeSet&) override {
    ++refresh_call_count;
    dasall::knowledge::RefreshResult result;
    result.status = dasall::knowledge::RefreshStatus::Accepted;
    result.refresh_id = "batch:knowledge-test";
    return result;
  }

  int retrieve_call_count = 0;
  mutable int health_call_count = 0;
  int refresh_call_count = 0;
};

std::shared_ptr<dasall::access::IAccessGateway> build_gateway(
    int* runtime_call_count,
    std::shared_ptr<dasall::knowledge::IKnowledgeService> knowledge_service = nullptr,
    int max_payload_bytes = 8) {
  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.publish_view.max_payload_bytes = max_payload_bytes;
  options.knowledge_service = std::move(knowledge_service);
  options.runtime_dispatch_backend =
      [runtime_call_count](const RuntimeDispatchRequest&) {
        ++(*runtime_call_count);
        RuntimeDispatchResult result;
        result.disposition = AccessDisposition::Completed;
        return result;
      };

  auto gateway = dasall::access::create_daemon_access_gateway(std::move(options));
  assert_true(gateway != nullptr, "daemon access gateway factory should return a gateway");
  assert_true(gateway->init(), "daemon access gateway should initialize");
  return gateway;
}

void unknown_command_is_rejected_before_runtime() {
  int runtime_call_count = 0;
  auto gateway = build_gateway(&runtime_call_count);

  InboundPacket packet;
  packet.packet_id = "unknown_command";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";
  packet.payload = "ok";

  const auto result = gateway->submit(packet);
  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(result.disposition),
               "unknown command should be rejected");
  assert_equal(0,
               runtime_call_count,
               "unknown command should not reach runtime backend");
}

void auth_deny_is_rejected_before_runtime() {
  int runtime_call_count = 0;
  auto gateway = build_gateway(&runtime_call_count);

  InboundPacket packet;
  packet.packet_id = "req-auth-deny";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "untrusted";
  packet.payload = "ok";

  const auto result = gateway->submit(packet);
  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(result.disposition),
               "auth deny path should be rejected");
  assert_equal(0,
               runtime_call_count,
               "auth deny path should not reach runtime backend");
}

void payload_too_large_is_rejected_before_runtime() {
  int runtime_call_count = 0;
  auto gateway = build_gateway(&runtime_call_count);

  InboundPacket packet;
  packet.packet_id = "req-large-payload";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";
  packet.payload = "payload-too-large";

  const auto result = gateway->submit(packet);
  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(result.disposition),
               "payload too large should be rejected");
  assert_equal(0,
               runtime_call_count,
               "payload too large should not reach runtime backend");
}

void valid_submit_reaches_runtime_pipeline() {
  int runtime_call_count = 0;
  auto gateway = build_gateway(&runtime_call_count);

  InboundPacket packet;
  packet.packet_id = "req-ok";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";
  packet.payload = "ok";

  const auto result = gateway->submit(packet);
  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(result.disposition),
               "valid submit should reach runtime backend");
  assert_equal(1,
               runtime_call_count,
               "valid submit should call runtime backend exactly once");
}

void knowledge_refresh_completes_without_runtime_pipeline() {
  int runtime_call_count = 0;
  auto knowledge_service = std::make_shared<FakeKnowledgeService>();
  auto gateway = build_gateway(&runtime_call_count, knowledge_service, 256);

  InboundPacket packet;
  packet.packet_id = "knowledge";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";
  packet.payload = "operation=refresh";

  const auto result = gateway->submit(packet);
  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(result.disposition),
               "knowledge refresh should complete through daemon access route");
  assert_equal(0,
               runtime_call_count,
               "knowledge refresh should not be dispatched to runtime submit backend");
  assert_equal(1,
               knowledge_service->refresh_call_count,
               "knowledge refresh should call IKnowledgeService request_refresh once");
  assert_true(result.publish_envelope.has_value() &&
                  result.publish_envelope->agent_result.has_value() &&
                  result.publish_envelope->agent_result->response_text.has_value() &&
                  result.publish_envelope->agent_result->response_text->find(
                      "\"status\":\"accepted\"") != std::string::npos,
              "knowledge refresh should publish accepted JSON payload");
}

void knowledge_retrieve_completes_without_runtime_pipeline() {
  int runtime_call_count = 0;
  auto knowledge_service = std::make_shared<FakeKnowledgeService>();
  auto gateway = build_gateway(&runtime_call_count, knowledge_service, 256);

  InboundPacket packet;
  packet.packet_id = "knowledge";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";
  packet.payload = "operation=retrieve;query_text=DeepSeek Chat";

  const auto result = gateway->submit(packet);
  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(result.disposition),
               "knowledge retrieve should complete through daemon access route");
  assert_equal(0,
               runtime_call_count,
               "knowledge retrieve should not be dispatched to runtime submit backend");
  assert_equal(1,
               knowledge_service->retrieve_call_count,
               "knowledge retrieve should call IKnowledgeService retrieve once");
  assert_true(result.publish_envelope.has_value() &&
                  result.publish_envelope->agent_result.has_value() &&
                  result.publish_envelope->agent_result->response_text.has_value() &&
                  result.publish_envelope->agent_result->response_text->find(
                      "\"slice_count\":1") != std::string::npos,
              "knowledge retrieve should publish result count JSON payload");
}

void knowledge_invalid_payload_is_rejected_before_runtime_pipeline() {
  int runtime_call_count = 0;
  auto knowledge_service = std::make_shared<FakeKnowledgeService>();
  auto gateway = build_gateway(&runtime_call_count, knowledge_service, 256);

  InboundPacket packet;
  packet.packet_id = "knowledge";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";
  packet.payload = "operation=retrieve";

  const auto result = gateway->submit(packet);
  assert_equal(static_cast<int>(AccessDisposition::Rejected),
               static_cast<int>(result.disposition),
               "knowledge retrieve without query should be rejected");
  assert_equal(0,
               runtime_call_count,
               "invalid knowledge payload should not reach runtime backend");
  assert_true(result.error_ref.has_value() && *result.error_ref == "knowledge_payload_invalid",
              "invalid knowledge payload should expose stable error_ref");
}

}  // namespace

int main() {
  try {
    unknown_command_is_rejected_before_runtime();
    auth_deny_is_rejected_before_runtime();
    payload_too_large_is_rejected_before_runtime();
    valid_submit_reaches_runtime_pipeline();
    knowledge_refresh_completes_without_runtime_pipeline();
    knowledge_retrieve_completes_without_runtime_pipeline();
    knowledge_invalid_payload_is_rejected_before_runtime_pipeline();
  } catch (const std::exception& ex) {
    std::cerr << "[DaemonAccessPipelineFactoryTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}
