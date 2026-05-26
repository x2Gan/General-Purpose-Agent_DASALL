#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "AccessGatewayFactory.h"
#include "CliCommandParser.h"
#include "CliIpcClient.h"
#include "CliRequestBuilder.h"
#include "DaemonIntegrationHarness.h"
#include "IKnowledgeService.h"
#include "KnowledgeTypes.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::AccessDisposition;
using dasall::access::DaemonAccessPipelineOptions;
using dasall::access::RuntimeDispatchRequest;
using dasall::access::RuntimeDispatchResult;
using dasall::apps::cli::CliCommandParser;
using dasall::apps::cli::CliRequestBuilder;
using dasall::tests::integration::access_support::DaemonIntegrationHarness;
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

    dasall::knowledge::EvidenceBundle evidence;
    evidence.degraded = true;
    evidence.slices.push_back(dasall::knowledge::EvidenceSlice{
        .evidence_id = "evidence:runtime-query-surface",
        .snippet = "Hybrid canary fell back to lexical-only for the current runtime gate.",
        .citation_ref = "adr_normative#hybrid-canary",
        .confidence = 1.0F,
        .freshness = dasall::knowledge::FreshnessState::Fresh,
        .tags = {"runtime", "canary"},
    });
    result.evidence = std::move(evidence);
    result.reason_codes = {"profile_forced_lexical_only",
                           "allowed_corpora_filter_applied"};
    result.warning_count = 1U;
    result.corpus_summary = query.allowed_corpora;
    return result;
  }

  dasall::knowledge::KnowledgeHealthSnapshot health_snapshot() const override {
    return {};
  }

  dasall::knowledge::RefreshResult request_refresh(
      const dasall::knowledge::CorpusChangeSet&) override {
    dasall::knowledge::RefreshResult result;
    result.status = dasall::knowledge::RefreshStatus::Accepted;
    result.refresh_id = "refresh:query-surface-unused";
    return result;
  }

  int retrieve_call_count = 0;
  std::optional<dasall::knowledge::KnowledgeQuery> last_query;
};

void knowledge_retrieve_request_scoped_surface_roundtrip_reaches_knowledge_query() {
  int runtime_call_count = 0;
  auto knowledge_service = std::make_shared<FakeKnowledgeService>();

  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.publish_view.max_payload_bytes = 4096;
  options.daemon_profile_id = "daemon.knowledge.query-surface";
  options.knowledge_service = knowledge_service;
  options.runtime_dispatch_backend = [&runtime_call_count](const RuntimeDispatchRequest&) {
    ++runtime_call_count;
    RuntimeDispatchResult result;
    result.disposition = AccessDisposition::Completed;
    return result;
  };

  DaemonIntegrationHarness harness(std::move(options));

  const char* argv[] = {
      "dasall-cli",
      "knowledge",
      "retrieve",
      "--preferred-mode",
      "hybrid",
      "--query-kind",
      "policy-evidence",
      "--allowed-corpus=adr_normative",
      "--allowed-corpus",
      "ssot_normative",
      "--domain-tag",
      "runtime",
      "--required-tag",
      "runtime-owner",
      "--required-language",
      "zh-CN",
      "--json",
      "Hybrid",
      "canary",
  };
  const auto command = CliCommandParser::parse(19, argv);
  assert_true(command.has_value(),
              "knowledge query surface integration should parse request-scoped retrieve flags");

  const auto frame = CliRequestBuilder::build(*command);
  assert_true(frame.has_value(),
              "knowledge query surface integration should build a daemon frame");

  const auto response = harness.send_frame(*frame);
  assert_true(response.ok(),
              "knowledge query surface integration should receive a parsed daemon response");
  assert_true(response.is_completed(),
              "knowledge query surface integration should complete over daemon unary path");
  assert_equal(0,
               runtime_call_count,
               "knowledge query surface integration should stay on the knowledge path");
  assert_equal(1,
               knowledge_service->retrieve_call_count,
               "knowledge query surface integration should invoke retrieve exactly once");
  assert_true(knowledge_service->last_query.has_value(),
              "knowledge query surface integration should capture the mapped KnowledgeQuery");
  assert_true(knowledge_service->last_query->preferred_mode.has_value() &&
                  *knowledge_service->last_query->preferred_mode ==
                      dasall::knowledge::RetrievalMode::Hybrid,
              "knowledge query surface integration should forward preferred_mode to KnowledgeQuery");
  assert_true(knowledge_service->last_query->query_kind ==
                  dasall::knowledge::KnowledgeQueryKind::PolicyEvidence,
              "knowledge query surface integration should forward query_kind to KnowledgeQuery");
  assert_equal(2,
               static_cast<int>(knowledge_service->last_query->allowed_corpora.size()),
               "knowledge query surface integration should preserve repeated allowed_corpus values");
  assert_equal(1,
               static_cast<int>(knowledge_service->last_query->domain_tags.size()),
               "knowledge query surface integration should preserve domain_tags");
  assert_equal(1,
               static_cast<int>(knowledge_service->last_query->required_tags.size()),
               "knowledge query surface integration should preserve required_tags");
  assert_true(knowledge_service->last_query->required_language ==
                  std::optional<std::string>{"zh-CN"},
              "knowledge query surface integration should preserve required_language");
  assert_true(response.response_text.has_value() &&
                  response.response_text->find("\"mode\":\"lexical_only\"") !=
                      std::string::npos &&
                  response.response_text->find("\"degraded\":true") !=
                      std::string::npos &&
                  response.response_text->find(
                      "\"reason_codes\":[\"profile_forced_lexical_only\",\"allowed_corpora_filter_applied\"]") !=
                      std::string::npos &&
                  response.response_text->find("\"warning_count\":1") !=
                      std::string::npos &&
                  response.response_text->find(
                      "\"corpus_summary\":[\"adr_normative\",\"ssot_normative\"]") !=
                      std::string::npos,
              "knowledge query surface integration should surface additive explain payload fields");

  harness.stop();
  assert_true(harness.daemon_stopped_cleanly(),
              "knowledge query surface integration should stop the daemon cleanly");
}

}  // namespace

int main() {
  try {
    knowledge_retrieve_request_scoped_surface_roundtrip_reaches_knowledge_query();
  } catch (const std::exception& ex) {
    std::cerr << "[KnowledgeRuntimeQuerySurfaceIntegrationTest] FAILED: " << ex.what()
              << '\n';
    return 1;
  }

  return 0;
}