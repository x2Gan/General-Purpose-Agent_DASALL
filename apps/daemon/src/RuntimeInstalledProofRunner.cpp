#include "RuntimeInstalledProofRunner.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "AgentFacade.h"
#include "AgentTypes.h"
#include "ICognitionEngine.h"
#include "IMemoryManager.h"
#include "IResponseBuilder.h"
#include "DaemonEntryConfigLoader.h"
#include "RuntimeDependencySet.h"
#include "RuntimeLiveDependencyComposition.h"
#include "agent/AgentRequest.h"
#include "agent/AgentResult.h"
#include "config/InstallLayout.h"

namespace dasall::apps::daemon {
namespace {

namespace fs = std::filesystem;

constexpr char kDatasetToolName[] = "agent.dataset";
constexpr char kTerminalToolName[] = "agent.terminal";
constexpr char kRuntimePathTagPrefix[] = "runtime_path:";
constexpr char kToolPositiveTag[] = "runtime_path:tool_positive";
constexpr char kRecoveryPositiveTag[] = "runtime_path:recovery_positive";
constexpr char kCompositionOwner[] = "daemon.runtime-installed-proof";
constexpr char kWaitingResponseText[] =
    "runtime installed proof waiting for clarification";
constexpr char kResumeRejectFragment[] =
    "token does not match waiting checkpoint binding";

[[nodiscard]] std::int64_t current_time_millis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
       std::chrono::system_clock::now().time_since_epoch())
    .count();
}

[[nodiscard]] std::string select_profile_id(
    const RuntimeInstalledProofOptions& options) {
  return options.requested_profile_id.empty() ? kDefaultDaemonEntryProfileId
                                              : options.requested_profile_id;
}

void apply_assets_override(infra::config::InstallLayout& install_layout,
                           const RuntimeInstalledProofOptions& options) {
  if (!options.readonly_assets_root_override.has_value()) {
    return;
  }

  const auto& assets_root = *options.readonly_assets_root_override;
  install_layout.readonly_assets_root = assets_root;
  install_layout.profiles_root = assets_root / "profiles";
  install_layout.llm_prompts_root = assets_root / "llm" / "prompts";
  install_layout.llm_providers_root = assets_root / "llm" / "providers";
}

[[nodiscard]] bool contains_value(const std::vector<std::string>& values,
                                  std::string_view expected_value) {
  return std::find(values.begin(), values.end(), expected_value) != values.end();
}

[[nodiscard]] std::string agent_result_status_name(
    const std::optional<contracts::AgentResultStatus>& status) {
  if (!status.has_value()) {
    return std::string{"<unset>"};
  }

  switch (*status) {
    case contracts::AgentResultStatus::Unspecified:
      return std::string{"Unspecified"};
    case contracts::AgentResultStatus::Completed:
      return std::string{"Completed"};
    case contracts::AgentResultStatus::Failed:
      return std::string{"Failed"};
    case contracts::AgentResultStatus::PartiallyCompleted:
      return std::string{"PartiallyCompleted"};
    case contracts::AgentResultStatus::Cancelled:
      return std::string{"Cancelled"};
    case contracts::AgentResultStatus::Timeout:
      return std::string{"Timeout"};
  }

  return std::string{"<unknown>"};
}

[[nodiscard]] std::optional<std::string> extract_runtime_path_tag(
    const contracts::AgentResult& result) {
  if (!result.tags.has_value()) {
    return std::nullopt;
  }

  const auto& tags = *result.tags;
  const auto iterator = std::find_if(
      tags.begin(), tags.end(), [](const std::string& tag) {
        return tag.rfind(kRuntimePathTagPrefix, 0U) == 0U;
      });
  if (iterator == tags.end()) {
    return std::nullopt;
  }
  return *iterator;
}

[[nodiscard]] runtime::AgentInitRequest make_init_request(
    const std::string& runtime_instance_id,
    const std::string& profile_id,
    std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot,
    std::shared_ptr<runtime::RuntimeDependencySet> dependency_set,
    const std::string& boot_reason) {
  runtime::AgentInitRequest request;
  request.runtime_instance_id = runtime_instance_id;
  request.profile_id = profile_id;
  request.policy_snapshot = std::move(policy_snapshot);
  request.dependency_set = std::move(dependency_set);
  request.boot_reason = boot_reason;
  request.cold_start = true;
  return request;
}

[[nodiscard]] contracts::AgentRequest make_agent_request(
    const std::string& request_id,
    const std::string& session_id,
    const std::string& trace_id,
    const std::string& user_input) {
  contracts::AgentRequest request;
  request.request_id = request_id;
  request.session_id = session_id;
  request.trace_id = trace_id;
  request.user_input = user_input;
  request.request_channel = contracts::RequestChannel::Cli;
  request.created_at = current_time_millis();
  return request;
}

[[nodiscard]] runtime::ResumeHandleRequest make_resume_request(
    const std::string& session_id,
    const std::string& checkpoint_ref,
    const std::string& request_id,
    const std::string& resume_reason,
    const std::string& resume_token,
    const std::string& trace_context) {
  runtime::ResumeHandleRequest request;
  request.request_id = request_id;
  request.session_id = session_id;
  request.checkpoint_ref = checkpoint_ref;
  request.resume_reason = resume_reason;
  request.resume_token = resume_token.empty()
                             ? runtime::make_resume_binding_token(session_id,
                                                                 checkpoint_ref)
                             : resume_token;
  request.trace_context = trace_context;
  return request;
}

[[nodiscard]] fs::path proof_state_root(
  const fs::path& base_state_root,
    const std::string_view scenario_name) {
  return base_state_root / std::string(scenario_name);
}

[[nodiscard]] apps::runtime_support::RuntimeDependencyCompositionResult
compose_runtime_dependency_set(
    std::shared_ptr<const profiles::RuntimePolicySnapshot> policy_snapshot,
    const infra::config::InstallLayout& install_layout,
  const fs::path& base_state_root,
    const std::string_view scenario_name) {
  return apps::runtime_support::compose_minimal_live_dependency_set(
      std::move(policy_snapshot),
      kCompositionOwner,
      apps::runtime_support::RuntimeLiveDependencyCompositionOptions{
          .readonly_assets_root_override = install_layout.readonly_assets_root,
          .runtime_library_root_override = install_layout.runtime_library_root,
          .state_root_override =
        proof_state_root(base_state_root, scenario_name),
      });
}

void disable_live_llm_bridge(
  std::shared_ptr<runtime::RuntimeDependencySet>& dependency_set) {
  dependency_set->llm_manager.reset();
  dependency_set->cognition_engine =
    std::shared_ptr<dasall::cognition::ICognitionEngine>(
      cognition::create_cognition_engine().release());
  dependency_set->response_builder =
    std::shared_ptr<dasall::cognition::IResponseBuilder>(
      cognition::create_response_builder().release());
  auto& evidence = dependency_set->external_evidence;
  evidence.erase(
    std::remove_if(
      evidence.begin(),
      evidence.end(),
      [](const std::string& value) {
      return value.find("required-live-baseline") != std::string::npos ||
           value.find("cognition-first-forced") != std::string::npos;
      }),
    evidence.end());
}

[[nodiscard]] std::shared_ptr<runtime::RuntimeDependencySet>
make_runtime_local_stub_dependency_set(
    const std::shared_ptr<runtime::RuntimeDependencySet>& source_dependency_set) {
  auto dependency_set = std::make_shared<runtime::RuntimeDependencySet>();
  if (source_dependency_set != nullptr) {
    dependency_set->visible_tools = source_dependency_set->visible_tools;
    dependency_set->external_evidence = source_dependency_set->external_evidence;
  }
  return dependency_set;
}

[[nodiscard]] bool seed_runtime_context(
    const std::shared_ptr<runtime::RuntimeDependencySet>& dependency_set,
    const std::string& session_id,
    const std::string& turn_id,
    const std::string& user_input,
    const std::string& fact_text,
    std::string* error) {
  if (dependency_set == nullptr || dependency_set->memory_manager == nullptr) {
    return true;
  }

  memory::MemoryWritebackRequest writeback_request;
  writeback_request.session_id = session_id;
  writeback_request.turn.turn_id = turn_id;
  writeback_request.turn.session_id = session_id;
  writeback_request.turn.user_input = user_input;
  writeback_request.turn.agent_response =
      "seed runtime installed proof context";
  writeback_request.summary_candidate = contracts::SummaryMemory{};
  writeback_request.summary_candidate->summary_text = fact_text;
  writeback_request.summary_candidate->confirmed_facts =
      std::vector<std::string>{fact_text};

  memory::FactCandidate fact_candidate;
  fact_candidate.fact.fact_text = fact_text;
  fact_candidate.fact.fact_type = "status";
  fact_candidate.fact.confidence_score = 90U;
  fact_candidate.fact.source_turn_ids = std::vector<std::string>{turn_id};
  fact_candidate.extraction_source = "runtime-installed-proof";
  writeback_request.fact_candidates.push_back(std::move(fact_candidate));

  const auto writeback_result = dependency_set->memory_manager->write_back(writeback_request);
  if (writeback_result.result_code.has_value()) {
    if (error != nullptr) {
      *error = "runtime installed proof failed to seed memory context";
    }
    return false;
  }

  return true;
}

}  // namespace

RuntimeInstalledProofResult collect_runtime_installed_proof(
    const RuntimeInstalledProofOptions& options) {
  RuntimeInstalledProofResult result;

  auto install_layout = infra::config::resolve_install_layout();
  apply_assets_override(install_layout, options);

  const DaemonEntryConfigLoader loader;
  const auto load_result = loader.load(
      DaemonEntryConfigLoadRequest{
          .profiles_root = install_layout.profiles_root,
          .requested_profile_id = select_profile_id(options),
          .deployment_config_path = options.deployment_config_path,
          .socket_path_override = std::nullopt,
      });
  if (!load_result.ok()) {
    result.error = std::string("failed to load daemon entry config: ") +
                   load_result.message;
    return result;
  }

  result.effective_profile_id = load_result.entry_config->effective_profile_id;
  const auto base_state_root = options.state_root_override.value_or(
      fs::temp_directory_path() /
      ("dasall-runtime-installed-proof-" +
       std::to_string(current_time_millis())));

  {
    const auto composition = compose_runtime_dependency_set(
        load_result.entry_config->runtime_policy_snapshot,
        install_layout,
        base_state_root,
        "tool-positive");
    if (!composition.ok()) {
      result.error =
          std::string("failed to compose tool-positive runtime dependency set: ") +
          composition.error;
      return result;
    }

    auto dependency_set = composition.dependency_set;
    disable_live_llm_bridge(dependency_set);
    result.visible_tools = dependency_set->visible_tools;
    result.external_evidence = dependency_set->external_evidence;
    result.agent_dataset_visible =
        contains_value(result.visible_tools, kDatasetToolName);
    result.agent_terminal_visible =
        contains_value(result.visible_tools, kTerminalToolName);
    dependency_set->local_stub_ports.main_loop_exit =
        runtime::RuntimeStubMainLoopExit::ToolRound;
    if (!seed_runtime_context(dependency_set,
                  "session-runtime-installed-proof-tool",
                  "turn-runtime-installed-proof-tool-seed",
                  "Search dataset for runtime installed proof evidence",
                  "runtime installed proof context is available",
                  &result.error)) {
      return result;
    }

    runtime::AgentFacade facade;
    const auto init_result = facade.init(make_init_request(
        "runtime-installed-proof-tool",
        result.effective_profile_id,
        load_result.entry_config->runtime_policy_snapshot,
        dependency_set,
        "runtime-installed-proof-tool"));
    result.tool_init_readiness = init_result.readiness_label();
    if (!init_result.accepted) {
      result.error = "runtime installed proof tool-positive init was rejected";
      return result;
    }

    const auto tool_result = facade.handle(make_agent_request(
        "req-runtime-installed-proof-tool",
        "session-runtime-installed-proof-tool",
        "trace-runtime-installed-proof-tool",
      "Search dataset for runtime installed proof evidence"));
    result.tool_status = agent_result_status_name(tool_result.status);
    result.tool_task_completed = tool_result.task_completed.value_or(false);
    result.tool_runtime_path =
        extract_runtime_path_tag(tool_result).value_or(std::string{});
    result.tool_checkpoint_ref = tool_result.checkpoint_ref.value_or(std::string{});
    result.tool_response_text = tool_result.response_text.value_or(std::string{});
    if (!result.agent_dataset_visible || !result.agent_terminal_visible) {
      result.error =
          "runtime installed proof composition did not expose both dataset and terminal tools";
      return result;
    }
    if (result.tool_status != "Completed" || !result.tool_task_completed ||
        result.tool_runtime_path != kToolPositiveTag ||
        result.tool_checkpoint_ref.empty()) {
      result.error =
          "runtime installed proof tool-positive probe did not converge to a completed tool_positive result";
      return result;
    }
  }

  {
    const auto composition = compose_runtime_dependency_set(
        load_result.entry_config->runtime_policy_snapshot,
        install_layout,
      base_state_root,
        "recovery-positive");
    if (!composition.ok()) {
      result.error = std::string(
                         "failed to compose recovery-positive runtime dependency set: ") +
                     composition.error;
      return result;
    }

      auto dependency_set =
        make_runtime_local_stub_dependency_set(composition.dependency_set);
      dependency_set->local_stub_ports.main_loop_exit =
        runtime::RuntimeStubMainLoopExit::ToolRound;

    runtime::AgentFacade facade;
    const auto init_result = facade.init(make_init_request(
        "runtime-installed-proof-recovery-positive",
        result.effective_profile_id,
        load_result.entry_config->runtime_policy_snapshot,
        dependency_set,
        "runtime-installed-proof-recovery-positive"));
    result.recovery_init_readiness = init_result.readiness_label();
    if (!init_result.accepted) {
      result.error =
          "runtime installed proof recovery-positive init was rejected";
      return result;
    }

    const auto recovery_result = facade.handle(make_agent_request(
      "req-runtime-installed-proof-recovery-positive",
      "session-runtime-installed-proof-recovery-positive",
      "trace-runtime-installed-proof-recovery-positive",
      "route recovery-positive runtime proof"));
    result.recovery_positive_status =
      agent_result_status_name(recovery_result.status);
    result.recovery_positive_task_completed =
      recovery_result.task_completed.value_or(false);
    result.recovery_positive_runtime_path =
      extract_runtime_path_tag(recovery_result).value_or(std::string{});
    result.recovery_positive_checkpoint_ref =
      recovery_result.checkpoint_ref.value_or(std::string{});
    result.recovery_positive_checkpoint_persisted =
      !result.recovery_positive_checkpoint_ref.empty();
    result.recovery_positive_response_text =
      recovery_result.response_text.value_or(std::string{});
    if (result.recovery_positive_status != "Completed" ||
        !result.recovery_positive_task_completed ||
        result.recovery_positive_runtime_path != kRecoveryPositiveTag ||
      !result.recovery_positive_checkpoint_persisted) {
      result.error =
          "runtime installed proof recovery-positive probe did not converge to a recovery_positive result";
      return result;
    }
  }

  {
    const auto composition = compose_runtime_dependency_set(
        load_result.entry_config->runtime_policy_snapshot,
        install_layout,
      base_state_root,
        "recovery-negative");
    if (!composition.ok()) {
      result.error = std::string(
                         "failed to compose recovery-negative runtime dependency set: ") +
                     composition.error;
      return result;
    }

    auto dependency_set =
        make_runtime_local_stub_dependency_set(composition.dependency_set);
    dependency_set->local_stub_ports.main_loop_exit =
        runtime::RuntimeStubMainLoopExit::WaitingClarify;
    dependency_set->local_stub_ports.waiting_response_text = kWaitingResponseText;

    runtime::AgentFacade facade;
    const auto init_result = facade.init(make_init_request(
        "runtime-installed-proof-recovery-negative",
        result.effective_profile_id,
        load_result.entry_config->runtime_policy_snapshot,
        dependency_set,
        "runtime-installed-proof-recovery-negative"));
    result.recovery_negative_init_readiness = init_result.readiness_label();
    if (!init_result.accepted) {
      result.error =
          "runtime installed proof recovery-negative init was rejected";
      return result;
    }

    const auto waiting_result = facade.handle(make_agent_request(
        "req-runtime-installed-proof-wait-negative",
        "session-runtime-installed-proof-recovery-negative",
        "trace-runtime-installed-proof-wait-negative",
      "need clarification"));
    result.waiting_status = agent_result_status_name(waiting_result.status);
    result.waiting_checkpoint_ref =
      waiting_result.checkpoint_ref.value_or(std::string{});
    const auto negative_checkpoint_ref =
        waiting_result.checkpoint_ref.value_or(std::string{});
    if (result.waiting_status != "PartiallyCompleted" ||
        waiting_result.task_completed.value_or(true) ||
        negative_checkpoint_ref.empty()) {
      result.error =
          "runtime installed proof recovery-negative probe did not produce a resumable waiting checkpoint";
      return result;
    }

    const auto rejected_result = facade.resume(make_resume_request(
        "session-runtime-installed-proof-recovery-negative",
        negative_checkpoint_ref,
        "resume-runtime-installed-proof-recovery-negative",
        "user clarification received",
        "resume-token-mismatch",
        "trace-runtime-installed-proof-recovery-negative"));
    result.recovery_negative_status =
        agent_result_status_name(rejected_result.status);
    result.recovery_negative_task_completed =
        rejected_result.task_completed.value_or(false);
    result.recovery_negative_response_text =
        rejected_result.response_text.value_or(std::string{});
    result.recovery_negative_binding_rejected =
        result.recovery_negative_response_text.find(kResumeRejectFragment) !=
        std::string::npos;
    if (result.recovery_negative_status != "Failed" ||
        result.recovery_negative_task_completed ||
        !result.recovery_negative_binding_rejected) {
      result.error =
          "runtime installed proof recovery-negative probe did not reject a mismatched resume token";
      return result;
    }
  }

  return result;
}

}  // namespace dasall::apps::daemon