#include <exception>
#include <iostream>
#include <memory>
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
      const dasall::knowledge::KnowledgeQuery&) override {
    return {};
  }

  dasall::knowledge::KnowledgeHealthSnapshot health_snapshot() const override {
    return {};
  }

  dasall::knowledge::RefreshResult request_refresh(
      const dasall::knowledge::CorpusChangeSet& changes) override {
    ++refresh_call_count;
    last_refresh_changes = changes;

    dasall::knowledge::RefreshResult result;
    result.status = dasall::knowledge::RefreshStatus::Accepted;
    result.refresh_id = "refresh:knowledge-trigger";
    return result;
  }

  int refresh_call_count = 0;
  dasall::knowledge::CorpusChangeSet last_refresh_changes;
};

void knowledge_refresh_changed_sources_roundtrip_reaches_request_refresh() {
  int runtime_call_count = 0;
  auto knowledge_service = std::make_shared<FakeKnowledgeService>();

  DaemonAccessPipelineOptions options;
  options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  options.auth_view.trusted_local_subjects = {"local://uid/1000"};
  options.publish_view.max_payload_bytes = 4096;
  options.daemon_profile_id = "daemon.knowledge.refresh-trigger";
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
      "refresh",
      "--changed-source",
      "profiles/desktop_full/runtime_policy.yaml",
      "--changed-source=profiles/linux-arm64-embedded/runtime_policy.yaml",
      "--json",
  };
  const auto command = CliCommandParser::parse(7, argv);
  assert_true(command.has_value(),
              "knowledge refresh trigger integration should parse changed-source CLI arguments");

  const auto frame = CliRequestBuilder::build(*command);
  assert_true(frame.has_value(),
              "knowledge refresh trigger integration should build a daemon frame");

  const auto response = harness.send_frame(*frame);
  assert_true(response.ok(),
              "knowledge refresh trigger integration should receive a parsed daemon response");
  assert_true(response.is_completed(),
              "knowledge refresh trigger integration should complete over daemon unary path");
  assert_equal(0,
               runtime_call_count,
               "knowledge refresh trigger integration should stay on the knowledge path");
  assert_equal(1,
               knowledge_service->refresh_call_count,
               "knowledge refresh trigger integration should invoke request_refresh exactly once");
  assert_equal(2,
               static_cast<int>(knowledge_service->last_refresh_changes.updated_sources.size()),
               "knowledge refresh trigger integration should forward both changed sources");
  assert_equal(std::string("profiles/desktop_full/runtime_policy.yaml"),
               knowledge_service->last_refresh_changes.updated_sources.front(),
               "knowledge refresh trigger integration should preserve changed-source ordering");
  assert_true(response.response_text.has_value() &&
                  response.response_text->find("\"status\":\"accepted\"") !=
                      std::string::npos,
              "knowledge refresh trigger integration should surface accepted refresh status");

  harness.stop();
  assert_true(harness.daemon_stopped_cleanly(),
              "knowledge refresh trigger integration should stop the daemon cleanly");
}

}  // namespace

int main() {
  try {
    knowledge_refresh_changed_sources_roundtrip_reaches_request_refresh();
  } catch (const std::exception& ex) {
    std::cerr << "[KnowledgeRuntimeRefreshTriggerIntegrationTest] FAILED: " << ex.what()
              << '\n';
    return 1;
  }

  return 0;
}