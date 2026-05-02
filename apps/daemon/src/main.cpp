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
#include "agent/AgentResult.h"
#include "linux/UnixIpcProvider.h"

namespace {

struct ParsedDaemonArgs {
  bool validate_only = false;
  std::optional<std::string> profile_id;
  std::optional<std::filesystem::path> deployment_config_path;
  std::optional<std::string> socket_path_override;
  bool ok = true;
  std::string error;
};

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
  const auto it = request.request_context.find(key);
  if (it == request.request_context.end() || it->second.empty()) {
    return std::nullopt;
  }
  return it->second;
}

[[nodiscard]] std::int64_t now_epoch_millis() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

[[nodiscard]] dasall::contracts::AgentRequest project_agent_request(
    const dasall::access::RuntimeDispatchRequest& request) {
  dasall::contracts::AgentRequest agent_request;
  const auto request_id =
      dispatch_context_value(request, "request_id").value_or(request.packet.packet_id);
  agent_request.request_id = request_id;
  agent_request.session_id = dispatch_context_value(request, "session_id")
                                 .value_or(std::string("session:") + request_id);
  agent_request.trace_id = dispatch_context_value(request, "trace_id")
                               .value_or(std::string("trace:") + request_id);
  agent_request.user_input = request.packet.payload;
  agent_request.request_channel = dasall::contracts::RequestChannel::Daemon;
  agent_request.created_at = now_epoch_millis();
  if (const auto idempotency_key =
          dispatch_context_value(request, "idempotency_key");
      idempotency_key.has_value()) {
    agent_request.idempotency_key = *idempotency_key;
  }
  return agent_request;
}

[[nodiscard]] dasall::access::RuntimeDispatchResult
map_agent_result_to_dispatch_result(
    const dasall::contracts::AgentResult& agent_result) {
  dasall::access::RuntimeDispatchResult dispatch_result;
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

  dispatch_result.disposition = dasall::access::AccessDisposition::Completed;
  dispatch_result.publish_envelope = dasall::access::PublishEnvelope{};
  dispatch_result.publish_envelope->agent_result = agent_result;
  return dispatch_result;
}

}  // namespace

int main(int argc, char* argv[]) {
  const ParsedDaemonArgs parsed = parse_daemon_args(argc, argv);
  if (!parsed.ok) {
    std::cerr << "[dasall_daemon] argument parse failed: " << parsed.error << "\n";
    return 1;
  }

  const dasall::apps::daemon::DaemonEntryConfigLoader entry_loader;
  const dasall::apps::daemon::DaemonEntryConfigLoadRequest entry_request{
      .profiles_root = std::filesystem::current_path() / "profiles",
      .requested_profile_id = parsed.profile_id.value_or(
          dasall::apps::daemon::kDefaultDaemonEntryProfileId),
      .deployment_config_path = parsed.deployment_config_path,
      .socket_path_override = parsed.socket_path_override,
  };
  const auto entry_config = entry_loader.load(entry_request);
  if (!entry_config.ok() || !entry_config.entry_config.has_value()) {
    std::cerr << "[dasall_daemon] entry config load failed: "
              << entry_config.message << "\n";
    return 1;
  }

  const auto& entry = *entry_config.entry_config;

  const dasall::apps::daemon::DaemonConfigValidator config_validator;
  const auto validation = parsed.validate_only
                              ? config_validator.validate_only(
                                    entry.bootstrap_config,
                                    entry.conflicts)
                              : config_validator.validate_config(
                                    entry.bootstrap_config);
  if (!validation.ok()) {
    std::cerr << "[dasall_daemon] config validation failed: "
              << validation.message << "\n";
    return 1;
  }

  if (!parsed.validate_only) {
    const auto conflict_validation =
        config_validator.validate_conflicts(entry.conflicts);
    if (!conflict_validation.ok()) {
      std::cerr << "[dasall_daemon] config validation failed: "
                << conflict_validation.message << "\n";
      return 1;
    }
  }

  if (parsed.validate_only) {
    std::cout << "[dasall_daemon] " << validation.message << "\n";
    return 0;
  }

  // 1. 创建 IIPC provider（Linux UDS 实现）
  auto ipc = std::make_shared<dasall::platform::linux::UnixIpcProvider>();

  // 2. 通过 daemon pipeline factory 构造完整 submit pipeline。
  auto runtime_facade = std::make_shared<dasall::runtime::AgentFacade>();
  auto diagnostics_enabled_state =
      std::make_shared<std::atomic_bool>(entry.bootstrap_config.diag_enabled);

  dasall::access::DaemonAccessPipelineOptions pipeline_options;
  pipeline_options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  pipeline_options.publish_view.max_payload_bytes =
      static_cast<int>(entry.bootstrap_config.max_payload_bytes);
  pipeline_options.auth_view.trusted_local_subjects = {
      "local://uid/" + std::to_string(static_cast<unsigned int>(::getuid()))};
  pipeline_options.daemon_diagnostics_enabled = diagnostics_enabled_state->load();
  pipeline_options.daemon_diagnostics_enabled_state = diagnostics_enabled_state;
    pipeline_options.daemon_profile_id = entry.effective_profile_id;
    pipeline_options.daemon_version = "v1";
  pipeline_options.runtime_dispatch_backend =
      [runtime_facade](const dasall::access::RuntimeDispatchRequest& request)
          -> dasall::access::RuntimeDispatchResult {
        const auto agent_request = project_agent_request(request);
        const auto agent_result = runtime_facade->handle(agent_request);
        return map_agent_result_to_dispatch_result(agent_result);
      };
    pipeline_options.daemon_listener_ready =
      entry.bootstrap_config.has_consistent_values();
    pipeline_options.daemon_gateway_ready =
      static_cast<bool>(pipeline_options.runtime_dispatch_backend);
    pipeline_options.daemon_bridge_reachable =
      static_cast<bool>(pipeline_options.runtime_dispatch_backend);

  auto gateway = dasall::access::create_daemon_access_gateway(
      std::move(pipeline_options));
  if (!gateway->init()) {
    std::cerr << "[dasall_daemon] AccessGateway init failed\n";
    return 1;
  }

  const auto context = dasall::apps::daemon::DaemonBootstrap::build(
      entry.bootstrap_config,
      dasall::apps::daemon::DaemonBootstrap::BuildDependencies{
          .ipc = ipc,
          .access_gateway = gateway,
          .watchdog_service = nullptr,
        .effective_profile_id = entry.effective_profile_id,
        .config_revision = entry.config_revision,
      });
  if (!context.has_value()) {
    std::cerr << "[dasall_daemon] bootstrap build failed\n";
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
          std::cout << "[dasall_daemon] audit daemon.reload.denied"
                    << " daemon_state=ready"
                    << " rejected_key=" << key
                    << " reason_code=" << reason << "\n";
        }
      };

  dasall::apps::daemon::DaemonConfigReloader config_reloader(
      entry.bootstrap_config,
      emit_reload_denied);

  if (!signal_handler.install_handlers()) {
    std::cerr << "[dasall_daemon] signal handler install failed\n";
    return 1;
  }

  // 4. 启动 UDS 服务端
  std::cout << "[dasall_daemon] starting on "
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
      std::cout << "[dasall_daemon] reload requested by signal "
                << signal_handler.last_signal() << ": "
                << (reload_result.ok() ? "applied" : "rejected")
                << " reason=" << reload_result.reason << "\n";
      signal_handler.clear_requests();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  daemon_thread.join();

  std::cout << "[dasall_daemon] stopped (run=" << (run_ok ? "ok" : "failed") << ")\n";
  return run_ok ? 0 : 1;
}


