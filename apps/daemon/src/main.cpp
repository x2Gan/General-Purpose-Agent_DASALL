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

#include <csignal>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>
#include <utility>
#include <unistd.h>
#include <vector>

#include "DaemonBootstrap.h"
#include "DaemonConfig.h"
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
  dasall::apps::daemon::DaemonBootstrapConfig config;
  bool validate_only = false;
  bool ok = true;
  std::string error;
};

[[nodiscard]] ParsedDaemonArgs parse_daemon_args(int argc, char* argv[]) {
  ParsedDaemonArgs parsed;

  for (int index = 1; index < argc; ++index) {
    const std::string_view arg(argv[index]);

    if (arg == "--validate-only") {
      parsed.validate_only = true;
      continue;
    }

    if (arg.starts_with("--socket-path=")) {
      parsed.config.socket_path =
          std::string(arg.substr(std::string_view("--socket-path=").size()));
      continue;
    }

    if (arg == "--socket-path") {
      if (index + 1 >= argc) {
        parsed.ok = false;
        parsed.error = "--socket-path requires a value";
        return parsed;
      }

      parsed.config.socket_path = argv[++index];
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

  const dasall::apps::daemon::DaemonConfigValidator config_validator;
  const auto validation = parsed.validate_only
                              ? config_validator.validate_only(parsed.config)
                              : config_validator.validate_config(parsed.config);
  if (!validation.ok()) {
    std::cerr << "[dasall_daemon] config validation failed: "
              << validation.message << "\n";
    return 1;
  }

  if (parsed.validate_only) {
    std::cout << "[dasall_daemon] " << validation.message << "\n";
    return 0;
  }

  // 1. 创建 IIPC provider（Linux UDS 实现）
  auto ipc = std::make_shared<dasall::platform::linux::UnixIpcProvider>();

  // 2. 通过 daemon pipeline factory 构造完整 submit pipeline。
  auto runtime_facade = std::make_shared<dasall::runtime::AgentFacade>();

  dasall::access::DaemonAccessPipelineOptions pipeline_options;
  pipeline_options.bootstrap_config.allowed_protocols = {"ipc_uds"};
  pipeline_options.publish_view.max_payload_bytes =
      static_cast<int>(parsed.config.max_payload_bytes);
  pipeline_options.auth_view.trusted_local_subjects = {
      "local://uid/" + std::to_string(static_cast<unsigned int>(::getuid()))};
    pipeline_options.daemon_diagnostics_enabled = parsed.config.diag_enabled;
    pipeline_options.daemon_profile_id = "daemon.direct_bind.v1";
    pipeline_options.daemon_version = "v1";
    pipeline_options.daemon_listener_ready = true;
    pipeline_options.daemon_gateway_ready = true;
    pipeline_options.daemon_bridge_reachable = true;
  pipeline_options.runtime_dispatch_backend =
      [runtime_facade](const dasall::access::RuntimeDispatchRequest& request)
          -> dasall::access::RuntimeDispatchResult {
        const auto agent_request = project_agent_request(request);
        const auto agent_result = runtime_facade->handle(agent_request);
        return map_agent_result_to_dispatch_result(agent_result);
      };

  auto gateway = dasall::access::create_daemon_access_gateway(
      std::move(pipeline_options));
  if (!gateway->init()) {
    std::cerr << "[dasall_daemon] AccessGateway init failed\n";
    return 1;
  }

  const auto context = dasall::apps::daemon::DaemonBootstrap::build(
      parsed.config,
      dasall::apps::daemon::DaemonBootstrap::BuildDependencies{
          .ipc = ipc,
          .access_gateway = gateway,
          .watchdog_service = nullptr,
          .effective_profile_id = "daemon.direct_bind.v1",
          .config_revision = std::nullopt,
      });
  if (!context.has_value()) {
    std::cerr << "[dasall_daemon] bootstrap build failed\n";
    return 1;
  }

  // 3. 构造 DaemonBootstrap（通过 build(config) 产出的只读 process context 驱动）
  dasall::apps::daemon::DaemonBootstrap bootstrap;
  dasall::apps::daemon::DaemonSignalHandler signal_handler;
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
      bootstrap.stop(std::chrono::milliseconds(parsed.config.shutdown_grace_ms));
      break;
    }

    if (signal_handler.reload_requested()) {
      std::cout << "[dasall_daemon] reload requested by signal "
                << signal_handler.last_signal()
                << " (not applied in v1)\n";
      signal_handler.clear_requests();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  daemon_thread.join();

  std::cout << "[dasall_daemon] stopped (run=" << (run_ok ? "ok" : "failed") << ")\n";
  return run_ok ? 0 : 1;
}


