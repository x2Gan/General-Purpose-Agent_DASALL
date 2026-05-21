/// apps/daemon/src/main.cpp
///
/// DASALL daemon 本地控制面组合根
///
/// 职责：
///   - 构造所有依赖（IIPC provider、AccessGateway）
///   - 初始化 AccessGateway（注入最小 SubmitPipeline 和 PublishBackend）
///   - 构造 DaemonBootstrap 并调用 run() 进入事件循环
///
/// 架构约束：
///   - Daemon 是 Access 主链的本地 owner，不持有 runtime 内部控制权
///   - 所有客户端请求经 AccessGateway 主链（Admission/Policy/Normalizer/RuntimeBridge）
///   - 本地控制面优先于 HTTP/gateway；CLI 经 IIPC/UDS 进入本链路

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <unistd.h>
#include <vector>

#include "DaemonBootstrap.h"
#include "DaemonConfig.h"
#include "DaemonEntryConfigLoader.h"
#include "DaemonConfigReloader.h"
#include "DaemonConfigValidator.h"
#include "DaemonSignalHandler.h"
#include "AccessErrors.h"
#include "AccessGatewayFactory.h"
#include "IAccessGateway.h"
#include "AgentFacade.h"
#include "RuntimeDependencySet.h"
#include "RuntimeLiveDependencyComposition.h"
#include "agent/AgentResult.h"
#include "config/InstallLayout.h"
#include "diagnostics/DiagnosticsServiceFacade.h"
#include "linux/UnixIpcProvider.h"

namespace {

constexpr char kDaemonStartupDiagnosticsForceStageEnv[] =
  "DASALL_DAEMON_STARTUP_DIAGNOSTICS_FORCE_STAGE";
constexpr char kRuntimeStateRootOverrideEnv[] =
  "DASALL_RUNTIME_STATE_ROOT_OVERRIDE";

struct AgentInitRequestBuildResult {
  dasall::runtime::AgentInitRequest request;
  std::string error;

  [[nodiscard]] bool ok() const {
    return error.empty() && request.has_minimum_requirements();
  }
};

struct ParsedDaemonArgs {
  bool validate_only = false;
  std::optional<std::string> profile_id;
  std::optional<std::filesystem::path> deployment_config_path;
  std::optional<std::string> socket_path_override;
  bool ok = true;
  std::string error;
};

struct DaemonStartupFailureContext {
  dasall::infra::config::InstallLayout install_layout;
  std::string requested_profile_id;
  std::optional<std::filesystem::path> config_path;
  std::optional<std::string> socket_path;
};

void emit_daemon_startup_failure(const DaemonStartupFailureContext& context,
                                 std::string_view stage,
                                 std::string_view error_code,
                                 std::string_view detail,
                                 std::string_view runtime_diagnostics = {}) {
  std::cerr << "[dasall-daemon] startup failure"
            << " stage=" << stage
            << " error_code=" << error_code
            << " trace_id=startup:daemon:" << stage
            << " requested_profile=" << context.requested_profile_id
            << " assets_root=" << context.install_layout.readonly_assets_root.string()
            << " profiles_root=" << context.install_layout.profiles_root.string()
            << " daemon_config_path=" << context.install_layout.daemon_config_path.string();
  if (context.config_path.has_value()) {
    std::cerr << " config_path=" << context.config_path->string();
  }
  if (context.socket_path.has_value() && !context.socket_path->empty()) {
    std::cerr << " socket_path=" << *context.socket_path;
  }
  if (!runtime_diagnostics.empty()) {
    std::cerr << " runtime_diagnostics=" << runtime_diagnostics;
  }
  if (!detail.empty()) {
    std::cerr << " detail=" << detail;
  }
  std::cerr << "\n";
}

[[nodiscard]] bool daemon_startup_stage_forced(std::string_view stage) {
  const char* forced_stage = std::getenv(kDaemonStartupDiagnosticsForceStageEnv);
  return forced_stage != nullptr && stage == forced_stage;
}

[[nodiscard]] std::optional<std::filesystem::path>
runtime_state_root_override_from_env() {
  const char* raw_value = std::getenv(kRuntimeStateRootOverrideEnv);
  if (raw_value == nullptr || *raw_value == '\0') {
    return std::nullopt;
  }

  return std::filesystem::path(raw_value);
}

[[nodiscard]] dasall::apps::daemon::DaemonSocketPolicy
resolve_validate_only_socket_policy() {
  if (::geteuid() != 0) {
    return dasall::apps::daemon::DaemonSocketPolicy::for_current_process();
  }

  return dasall::apps::daemon::DaemonSocketPolicy::for_daemon_service_account_or_current_process();
}

[[nodiscard]] ParsedDaemonArgs parse_daemon_args(int argc, char* argv[]) {
  ParsedDaemonArgs parsed;

  const auto set_unique_option = [&parsed](auto* slot,
                                           auto value,
                                           std::string_view option_name) {
    if (slot->has_value()) {
      parsed.ok = false;
      parsed.error = std::string(option_name) + " cannot be specified more than once";
      return false;
    }

    *slot = std::move(value);
    return true;
  };

  for (int index = 1; index < argc; ++index) {
    const std::string_view arg(argv[index]);

    if (arg == "--validate-only") {
      parsed.validate_only = true;
      continue;
    }

    if (arg.starts_with("--socket-path=")) {
      if (!set_unique_option(
              &parsed.socket_path_override,
              std::string(arg.substr(std::string_view("--socket-path=").size())),
              "--socket-path")) {
        return parsed;
      }
      continue;
    }

    if (arg == "--socket-path") {
      if (index + 1 >= argc) {
        parsed.ok = false;
        parsed.error = "--socket-path requires a value";
        return parsed;
      }

      if (!set_unique_option(&parsed.socket_path_override,
                             std::string(argv[++index]),
                             "--socket-path")) {
        return parsed;
      }
      continue;
    }

    if (arg.starts_with("--profile-id=")) {
      if (!set_unique_option(
              &parsed.profile_id,
              std::string(arg.substr(std::string_view("--profile-id=").size())),
              "--profile-id")) {
        return parsed;
      }
      continue;
    }

    if (arg == "--profile-id") {
      if (index + 1 >= argc) {
        parsed.ok = false;
        parsed.error = "--profile-id requires a value";
        return parsed;
      }

      if (!set_unique_option(&parsed.profile_id,
                             std::string(argv[++index]),
                             "--profile-id")) {
        return parsed;
      }
      continue;
    }

    if (arg.starts_with("--config-file=")) {
      if (!set_unique_option(
              &parsed.deployment_config_path,
              std::filesystem::path(
                  arg.substr(std::string_view("--config-file=").size())),
              "--config-file")) {
        return parsed;
      }
      continue;
    }

    if (arg == "--config-file") {
      if (index + 1 >= argc) {
        parsed.ok = false;
        parsed.error = "--config-file requires a value";
        return parsed;
      }

      if (!set_unique_option(&parsed.deployment_config_path,
                             std::filesystem::path(argv[++index]),
                             "--config-file")) {
        return parsed;
      }
      continue;
    }

    parsed.ok = false;
    parsed.error = std::string("unsupported argument: ") + std::string(arg);
    return parsed;
  }

  return parsed;
}

[[nodiscard]] std::optional<std::string> dispatch_context_value(
    const dasall::access::RuntimeDispatchRequest& request,
    const std::string& key) {
  if (key == "request_id" && request.agent_request.request_id.has_value() &&
      !request.agent_request.request_id->empty()) {
    return request.agent_request.request_id;
  }
  if (key == "session_id" && request.agent_request.session_id.has_value() &&
      !request.agent_request.session_id->empty()) {
    return request.agent_request.session_id;
  }
  if (key == "trace_id" && request.agent_request.trace_id.has_value() &&
      !request.agent_request.trace_id->empty()) {
    return request.agent_request.trace_id;
  }
  if (key == "idempotency_key" &&
      request.agent_request.idempotency_key.has_value() &&
      !request.agent_request.idempotency_key->empty()) {
    return *request.agent_request.idempotency_key;
  }

  const auto it = request.request_context.find(key);
  if (it == request.request_context.end() || it->second.empty()) {
    return std::nullopt;
  }
  return it->second;
}

[[nodiscard]] dasall::access::RuntimeDispatchResult
map_agent_result_to_dispatch_result(
  const dasall::access::RuntimeDispatchRequest& request,
  dasall::contracts::AgentResult agent_result) {
  dasall::access::RuntimeDispatchResult dispatch_result;
  const auto request_id =
    dispatch_context_value(request, "request_id").value_or(request.packet.packet_id);
  const auto session_id = dispatch_context_value(request, "session_id")
                .value_or(std::string("session:") + request_id);
  const auto trace_id = dispatch_context_value(request, "trace_id")
              .value_or(std::string("trace:") + request_id);
  const auto uninitialized_runtime =
      agent_result.status.has_value() &&
      *agent_result.status == dasall::contracts::AgentResultStatus::Failed &&
      agent_result.response_text.has_value() &&
      agent_result.response_text->find("not initialized") != std::string::npos;

  if (uninitialized_runtime) {
    dispatch_result.disposition = dasall::access::AccessDisposition::Rejected;
    dispatch_result.error_ref = "runtime_bridge_unavailable";
    dispatch_result.response_context["error_code"] = std::to_string(
        static_cast<int>(dasall::access::AccessErrorCode::RuntimeBridgeUnavailable));
    dispatch_result.response_context["error_detail"] = *agent_result.response_text;
    return dispatch_result;
  }

  if (!agent_result.request_id.has_value() || agent_result.request_id->empty()) {
    agent_result.request_id = request_id;
  }
  if (!agent_result.trace_id.has_value() || agent_result.trace_id->empty()) {
    agent_result.trace_id = trace_id;
  }

  dispatch_result.disposition = dasall::access::AccessDisposition::Completed;
  dispatch_result.publish_envelope = dasall::access::PublishEnvelope{};
  dispatch_result.publish_envelope->request_id = request_id;
  dispatch_result.publish_envelope->result_id =
      agent_result.result_id.value_or(std::string("result:") + request_id);
  dispatch_result.publish_envelope->session_id = session_id;
  dispatch_result.publish_envelope->trace_id = trace_id;
  dispatch_result.publish_envelope->channel_ref =
      request.packet.entry_type + "://" + request.packet.protocol_kind;
  dispatch_result.publish_envelope->protocol_kind = request.packet.protocol_kind;
  dispatch_result.publish_envelope->protocol_status_hint = "200";
  dispatch_result.publish_envelope->payload =
      agent_result.response_text.value_or(std::string());
  dispatch_result.publish_envelope->agent_result = agent_result;
  return dispatch_result;
}

[[nodiscard]] AgentInitRequestBuildResult build_daemon_agent_init_request(
    const dasall::apps::daemon::DaemonEntryConfig& entry) {
  dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions
      composition_options;
  if (const auto state_root_override = runtime_state_root_override_from_env();
      state_root_override.has_value()) {
    composition_options.state_root_override = *state_root_override;
  }

  const auto runtime_composition =
      dasall::apps::runtime_support::compose_minimal_live_dependency_set(
          entry.runtime_policy_snapshot,
          "daemon.local-control-plane",
          composition_options);
  if (!runtime_composition.ok()) {
    return AgentInitRequestBuildResult{
        .request = {},
        .error = runtime_composition.error,
    };
  }

  dasall::runtime::AgentInitRequest init_request;
  init_request.runtime_instance_id =
      "daemon.local-control-plane:" + entry.effective_profile_id;
  init_request.profile_id = entry.effective_profile_id;
  init_request.policy_snapshot = entry.runtime_policy_snapshot;
  init_request.dependency_set = runtime_composition.dependency_set;
  init_request.boot_reason = "daemon-local-control-plane";
  init_request.cold_start = true;
  return AgentInitRequestBuildResult{
      .request = std::move(init_request),
      .error = {},
  };
}

}  // namespace

int main(int argc, char* argv[]) {
  const auto install_layout = dasall::infra::config::resolve_install_layout();
  const ParsedDaemonArgs parsed = parse_daemon_args(argc, argv);
  DaemonStartupFailureContext failure_context{
      .install_layout = install_layout,
      .requested_profile_id = parsed.profile_id.value_or(
          dasall::apps::daemon::kDefaultDaemonEntryProfileId),
      .config_path = parsed.deployment_config_path,
      .socket_path = parsed.socket_path_override,
  };
  if (!parsed.ok) {
    emit_daemon_startup_failure(failure_context,
                                "argument-parse",
                                "DAEMON_E_ARGUMENT_PARSE_FAILED",
                                parsed.error);
    return 1;
  }

  const dasall::apps::daemon::DaemonEntryConfigLoader entry_loader;
  const dasall::apps::daemon::DaemonEntryConfigLoadRequest entry_request{
      .profiles_root = install_layout.profiles_root,
      .requested_profile_id = failure_context.requested_profile_id,
      .deployment_config_path = parsed.deployment_config_path,
      .socket_path_override = parsed.socket_path_override,
  };
  const auto entry_config = entry_loader.load(entry_request);
  if (!entry_config.ok() || !entry_config.entry_config.has_value()) {
    emit_daemon_startup_failure(failure_context,
                                "entry-config-load",
                                "DAEMON_E_ENTRY_CONFIG_LOAD_FAILED",
                                entry_config.message);
    return 1;
  }

  const auto& entry = *entry_config.entry_config;
  failure_context.socket_path = entry.bootstrap_config.socket_path;

  const dasall::apps::daemon::DaemonConfigValidator config_validator;
    const auto validate_only_socket_policy = resolve_validate_only_socket_policy();
  const auto validation = parsed.validate_only
                              ? config_validator.validate_only(
                                    entry.bootstrap_config,
              entry.conflicts,
              {},
              validate_only_socket_policy)
                              : config_validator.validate_config(
                                    entry.bootstrap_config);
  if (!validation.ok()) {
    emit_daemon_startup_failure(failure_context,
                                "config-validation",
                                "DAEMON_E_CONFIG_VALIDATION_FAILED",
                                validation.message);
    return 1;
  }

  if (!parsed.validate_only) {
    const auto conflict_validation =
        config_validator.validate_conflicts(entry.conflicts);
    if (!conflict_validation.ok()) {
      emit_daemon_startup_failure(failure_context,
                                  "config-validation",
                                  "DAEMON_E_CONFIG_CONFLICTS_FAILED",
                                  conflict_validation.message);
      return 1;
    }
  }

  if (parsed.validate_only) {
    std::cout << "[dasall-daemon] " << validation.message << "\n";
    return 0;
  }

  // 1. 创建 IIPC provider（Linux UDS 实现）
  auto ipc = std::make_shared<dasall::platform::linux::UnixIpcProvider>();

  // 2. 通过 daemon pipeline factory 构造完整 submit pipeline。
  auto runtime_facade = std::make_shared<dasall::runtime::AgentFacade>();
  if (daemon_startup_stage_forced("runtime-dependency-composition")) {
    emit_daemon_startup_failure(failure_context,
                                "runtime-dependency-composition",
                                "DAEMON_E_RUNTIME_COMPOSITION_FAILED",
                                "forced startup diagnostics failure");
    return 1;
  }
  const auto runtime_init_request = build_daemon_agent_init_request(entry);
  if (!runtime_init_request.ok()) {
    emit_daemon_startup_failure(failure_context,
                                "runtime-dependency-composition",
                                "DAEMON_E_RUNTIME_COMPOSITION_FAILED",
                                runtime_init_request.error);
    return 1;
  }
  if (daemon_startup_stage_forced("runtime-init")) {
    emit_daemon_startup_failure(failure_context,
                                "runtime-init",
                                "DAEMON_E_RUNTIME_INIT_FAILED",
                                "forced startup diagnostics failure",
                                "forced-runtime-init-diagnostics");
    return 1;
  }
  const auto runtime_init_result = runtime_facade->init(
      runtime_init_request.request);
  const bool runtime_entry_accepted = runtime_init_result.accepted;
  if (!runtime_entry_accepted) {
    emit_daemon_startup_failure(
        failure_context,
        "runtime-init",
        "DAEMON_E_RUNTIME_INIT_FAILED",
        runtime_init_result.health_summary.empty()
            ? "runtime facade init rejected"
            : runtime_init_result.health_summary,
        runtime_init_result.diagnostics);
    return 1;
  }
  std::cout << "[dasall-daemon] runtime readiness="
            << runtime_init_result.readiness_label() << "\n";

  const std::string daemon_profile_id = runtime_init_result.resolved_profile_id.empty()
                                            ? entry.effective_profile_id
                                            : runtime_init_result.resolved_profile_id;
  auto diagnostics_enabled_state =
      std::make_shared<std::atomic_bool>(entry.bootstrap_config.diag_enabled);
  auto diagnostics_service =
      std::make_shared<dasall::infra::diagnostics::DiagnosticsServiceFacade>(
          dasall::infra::diagnostics::DiagnosticsServiceFacadeOptions{
              .safe_mode_failure_threshold = 5,
              .snapshot_retention_days = 7,
              .snapshot_max_count = 500,
              .profile_id = daemon_profile_id,
              .metrics_provider = nullptr,
              .audit_logger = nullptr,
          });
  if (!diagnostics_service->start()) {
    emit_daemon_startup_failure(failure_context,
                                "diagnostics-service-start",
                                "DAEMON_E_DIAGNOSTICS_START_FAILED",
                                "DiagnosticsServiceFacade start returned false");
    return 1;
  }

  dasall::access::DaemonAccessPipelineOptions pipeline_options;
    pipeline_options.bootstrap_config.bootstrap_revision =
      entry.config_revision.value_or("daemon-entry:" + daemon_profile_id);
    pipeline_options.bootstrap_config.entry_type = "daemon";
    pipeline_options.bootstrap_config.listen_ref = entry.bootstrap_config.socket_path;
    pipeline_options.bootstrap_config.allowed_protocols = {"ipc_uds"};
    pipeline_options.bootstrap_config.dispatch_deadline_ms =
      static_cast<int>(entry.bootstrap_config.dispatch_timeout_ms);
    pipeline_options.bootstrap_config.result_replay_ttl_ms =
      static_cast<int>(entry.bootstrap_config.receipt_ttl_sec) * 1000;
    pipeline_options.bootstrap_config.max_payload_bytes =
      static_cast<int>(entry.bootstrap_config.max_payload_bytes);
    pipeline_options.bootstrap_config.trusted_local_subjects = {
      "local://uid/" + std::to_string(static_cast<unsigned int>(::getuid())),
      "local://uid/0"};
    pipeline_options.derive_views_from_runtime_policy = true;
    pipeline_options.runtime_policy_snapshot = entry.runtime_policy_snapshot;
  pipeline_options.daemon_diagnostics_enabled = diagnostics_enabled_state->load();
  pipeline_options.daemon_diagnostics_enabled_state = diagnostics_enabled_state;
  pipeline_options.diagnostics_service = diagnostics_service;
  if (runtime_init_request.request.dependency_set != nullptr) {
    pipeline_options.knowledge_service =
        runtime_init_request.request.dependency_set->knowledge_service;
  }
  pipeline_options.daemon_profile_id = daemon_profile_id;
  pipeline_options.daemon_version = "v1";
  pipeline_options.daemon_runtime_readiness_label =
      runtime_init_result.readiness_label();
    pipeline_options.daemon_runtime_degraded_reasons =
      runtime_init_result.degraded_reasons;
  pipeline_options.runtime_dispatch_backend =
      [runtime_facade](const dasall::access::RuntimeDispatchRequest& request)
          -> dasall::access::RuntimeDispatchResult {
        const auto agent_result = runtime_facade->handle(request.agent_request);
        return map_agent_result_to_dispatch_result(request, agent_result);
      };
  pipeline_options.daemon_listener_ready =
      entry.bootstrap_config.has_consistent_values();
  pipeline_options.daemon_gateway_ready = true;
  pipeline_options.daemon_bridge_reachable =
      runtime_entry_accepted && !runtime_init_result.stub_ready();

  auto gateway = dasall::access::create_daemon_access_gateway(
      std::move(pipeline_options));
  if (daemon_startup_stage_forced("access-gateway-init")) {
    emit_daemon_startup_failure(failure_context,
                                "access-gateway-init",
                                "DAEMON_E_ACCESS_GATEWAY_INIT_FAILED",
                                "forced startup diagnostics failure");
    return 1;
  }
  if (!gateway->init()) {
    emit_daemon_startup_failure(failure_context,
                                "access-gateway-init",
                                "DAEMON_E_ACCESS_GATEWAY_INIT_FAILED",
                                "AccessGateway init returned false");
    return 1;
  }

  const auto context = dasall::apps::daemon::DaemonBootstrap::build(
      entry.bootstrap_config,
      dasall::apps::daemon::DaemonBootstrap::BuildDependencies{
          .ipc = ipc,
          .access_gateway = gateway,
          .watchdog_service = nullptr,
          .effective_profile_id = daemon_profile_id,
        .config_revision = entry.config_revision,
      });
  if (!context.has_value()) {
    emit_daemon_startup_failure(failure_context,
                                "bootstrap-build",
                                "DAEMON_E_BOOTSTRAP_BUILD_FAILED",
                                "DaemonBootstrap::build returned null context");
    return 1;
  }

  // 3. 构造 DaemonBootstrap（通过 build(config) 产出的只读 process context 驱动）
  dasall::apps::daemon::DaemonBootstrap bootstrap;
  dasall::apps::daemon::DaemonSignalHandler signal_handler;

  const auto emit_reload_denied =
      [](const std::vector<std::string>& rejected_keys,
         const std::string& reason) {
        const std::vector<std::string> keys = rejected_keys.empty()
                                                  ? std::vector<std::string>{"daemon.config"}
                                                  : rejected_keys;
        for (const auto& key : keys) {
          std::cout << "[dasall-daemon] audit daemon.reload.denied"
                    << " daemon_state=ready"
                    << " rejected_key=" << key
                    << " reason_code=" << reason << "\n";
        }
      };

  dasall::apps::daemon::DaemonConfigReloader config_reloader(
      entry.bootstrap_config,
      emit_reload_denied);

  if (!signal_handler.install_handlers()) {
    emit_daemon_startup_failure(failure_context,
                                "signal-handler-install",
                                "DAEMON_E_SIGNAL_HANDLER_INSTALL_FAILED",
                                "daemon signal handler install failed");
    return 1;
  }

  // 4. 启动 UDS 服务端
  std::cout << "[dasall-daemon] starting on "
            << context->bootstrap_config.socket_path << "\n";

  bool run_ok = false;
  std::atomic<bool> run_finished{false};
  std::thread daemon_thread([&bootstrap, &context, &run_ok, &run_finished]() {
    run_ok = bootstrap.run(*context);
    run_finished.store(true);
  });

  while (!run_finished.load()) {
    if (signal_handler.shutdown_requested()) {
      bootstrap.stop(std::chrono::milliseconds(entry.bootstrap_config.shutdown_grace_ms));
      break;
    }

    if (signal_handler.reload_requested()) {
      dasall::apps::daemon::DaemonReloadResult reload_result;
      const auto reload_entry = entry_loader.load(entry_request);
      if (!reload_entry.ok() || !reload_entry.entry_config.has_value()) {
        emit_reload_denied({}, "reload_candidate_unavailable");
        reload_result.reason = "reload_candidate_unavailable";
      } else {
        const auto conflict_validation =
            config_validator.validate_conflicts(reload_entry.entry_config->conflicts);
        if (!conflict_validation.ok()) {
          std::vector<std::string> rejected_keys;
          rejected_keys.reserve(reload_entry.entry_config->conflicts.size());
          for (const auto& conflict : reload_entry.entry_config->conflicts) {
            rejected_keys.push_back(conflict.key);
          }
          emit_reload_denied(rejected_keys, "reload_rejected_conflict");
          reload_result.rejected_keys = std::move(rejected_keys);
          reload_result.reason = "reload_rejected_conflict";
        } else {
          reload_result = config_reloader.apply_reload_snapshot(
              reload_entry.entry_config->bootstrap_config);
          if (reload_result.ok()) {
            diagnostics_enabled_state->store(
                config_reloader.active_snapshot().diag_enabled);
          }
        }
      }
      std::cout << "[dasall-daemon] reload requested by signal "
                << signal_handler.last_signal() << ": "
                << (reload_result.ok() ? "applied" : "rejected")
                << " reason=" << reload_result.reason << "\n";
      signal_handler.clear_requests();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  daemon_thread.join();

  if (!run_ok) {
    emit_daemon_startup_failure(failure_context,
                                "listener-bind",
                                "DAEMON_E_LISTENER_BIND_FAILED",
                                "daemon listener bind or run failed");
  }

  std::cout << "[dasall-daemon] stopped (run=" << (run_ok ? "ok" : "failed") << ")\n";
  return run_ok ? 0 : 1;
}


