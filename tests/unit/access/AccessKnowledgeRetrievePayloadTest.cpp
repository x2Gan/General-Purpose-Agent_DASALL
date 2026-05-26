#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

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
      const dasall::knowledge::KnowledgeQuery& query) override {
    ++retrieve_call_count;
    last_query = query;

    dasall::knowledge::KnowledgeRetrieveResult result;
    result.ok = true;
    result.mode = dasall::knowledge::RetrievalMode::LexicalOnly;
    result.evidence = dasall::knowledge::EvidenceBundle{
        .slices = {dasall::knowledge::EvidenceSlice{
            .evidence_id = "evidence:access-payload-001",
            .snippet = "DeepSeek Chat installed provider evidence",
            .citation_ref = "dasall_llm_providers#deepseek-chat",
            .confidence = 1.0F,
            .freshness = dasall::knowledge::FreshnessState::Fresh,
            .tags = {"installed"},
        }},
        .context_projection = {"[provider] DeepSeek Chat installed provider evidence"},
        .omitted_sources = {},
        .degraded = false,
        .evidence_insufficient = false,
        .coverage_notes = "single provider hit",
    };
    result.reason_codes = {"mode_lexical_only"};
    result.warning_count = 2U;
    result.warning_summary = {"query_text_trimmed",
                              "dense_lane_skipped_default_lexical"};
    result.corpus_summary = {"adr_normative", "ssot_normative"};
    result.vector_backend_ready = true;
    result.sparse_hit_count = 1U;
    result.dense_hit_count = 0U;
    return result;
  }

  dasall::knowledge::KnowledgeHealthSnapshot health_snapshot() const override {
    return {};
  }

  dasall::knowledge::RefreshResult request_refresh(
      const dasall::knowledge::CorpusChangeSet&) override {
    return {};
  }

  int retrieve_call_count = 0;
  std::optional<dasall::knowledge::KnowledgeQuery> last_query;
};

std::shared_ptr<dasall::access::IAccessGateway> build_gateway(
    int* runtime_call_count,
    std::shared_ptr<dasall::knowledge::IKnowledgeService> knowledge_service,
    int max_payload_bytes = 512) {
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
  assert_true(gateway != nullptr,
              "access knowledge retrieve payload test should build a daemon access gateway");
  assert_true(gateway->init(),
              "access knowledge retrieve payload test should initialize the gateway");
  return gateway;
}

void knowledge_retrieve_payload_surfaces_vector_explain_fields() {
  int runtime_call_count = 0;
  auto knowledge_service = std::make_shared<FakeKnowledgeService>();
  auto gateway = build_gateway(&runtime_call_count, knowledge_service);

  InboundPacket packet;
  packet.packet_id = "knowledge";
  packet.entry_type = "daemon";
  packet.protocol_kind = "ipc_uds";
  packet.peer_ref = "local_trusted:1000";
  packet.payload = "operation=retrieve;query_text=DeepSeek%20Chat";

  const auto result = gateway->submit(packet);
  assert_equal(static_cast<int>(AccessDisposition::Completed),
               static_cast<int>(result.disposition),
               "access knowledge retrieve payload test should complete through the daemon access route");
  assert_equal(0,
               runtime_call_count,
               "access knowledge retrieve payload test should not dispatch to the runtime backend");
  assert_equal(1,
               knowledge_service->retrieve_call_count,
               "access knowledge retrieve payload test should call knowledge retrieve exactly once");
  assert_true(result.publish_envelope.has_value() &&
                  result.publish_envelope->agent_result.has_value() &&
                  result.publish_envelope->agent_result->response_text.has_value(),
              "access knowledge retrieve payload test should publish a retrieve JSON payload");

  const auto& payload = *result.publish_envelope->agent_result->response_text;
  assert_true(payload.find("\"warning_summary\":[\"query_text_trimmed\",\"dense_lane_skipped_default_lexical\"]") !=
                      std::string::npos &&
                  payload.find("\"selected_corpora\":[\"adr_normative\",\"ssot_normative\"]") !=
                      std::string::npos &&
                  payload.find("\"vector_backend_ready\":true") != std::string::npos &&
                  payload.find("\"sparse_hit_count\":1") != std::string::npos &&
                  payload.find("\"dense_hit_count\":0") != std::string::npos &&
                  payload.find("\"corpus_summary\":[\"adr_normative\",\"ssot_normative\"]") !=
                      std::string::npos,
              "access knowledge retrieve payload test should surface additive vector explain fields without dropping legacy corpus summary");
}

}  // namespace

int main() {
  try {
    knowledge_retrieve_payload_surfaces_vector_explain_fields();
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}