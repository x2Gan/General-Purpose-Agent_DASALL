/// apps/gateway/src/main.cpp
///
/// DASALL gateway HTTP 入口组合根（v1: unary + accepted async receipt）
///
/// 职责：
///   - 使用 cpp-httplib 创建 HTTP/1.1 服务器
///   - POST /v1/submit → HttpProtocolAdapter.decode() → IAccessGateway.submit()
///     → HttpProtocolAdapter.encode() → HTTP response
///   - 所有请求经 AccessGateway 主链（Admission/Policy/Normalizer/RuntimeBridge）
///
/// 架构约束：
///   - v1 仅支持 unary POST + accepted async receipt（无 WebSocket/MQTT）
///   - 健康探针路径 /health/* 在 028 实现后集成
///   - gateway 不持有 runtime 内部控制权（ADR-007/008 边界）
///   - ACC-TODO-028：/health/live、/health/ready、/health/startup 已集成
///   - 所有响应写入固定安全头（X-Content-Type-Options 等）
///   - CORS 默认禁用，通过 SecurityConfig::cors_allowed_origins 白名单启用

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <csignal>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// cpp-httplib：header-only HTTP/1.1 库
#include "httplib.h"

#include "AccessErrors.h"
#include "AccessGatewayFactory.h"
#include "HttpProtocolAdapter.h"
#include "IAccessGateway.h"
#include "HealthProbeHandler.h"
#include "AgentFacade.h"
#include "AccessOwnershipSecretWiring.h"
#include "ProfileError.h"
#include "RuntimeDependencySet.h"
#include "RuntimeLiveDependencyComposition.h"
#include "agent/AgentResult.h"
#include "config/InstallLayout.h"
#include "ProfileCatalog.h"
#include "RuntimePolicyProvider.h"

namespace {

constexpr char kDefaultGatewayProfileId[] = "desktop_full";
constexpr int kDefaultGatewayListenPort = 8080;
constexpr char kGatewayStartupDiagnosticsForceStageEnv[] =
  "DASALL_GATEWAY_STARTUP_DIAGNOSTICS_FORCE_STAGE";
constexpr char kRuntimeStateRootOverrideEnv[] =
  "DASALL_RUNTIME_STATE_ROOT_OVERRIDE";

struct AgentInitRequestBuildResult {
  dasall::runtime::AgentInitRequest request;
  std::string error;

  [[nodiscard]] bool ok() const {
    return error.empty() && request.has_minimum_requirements();
  }
};

struct ParsedGatewayArgs {
  std::optional<std::string> profile_id;
  std::optional<int> port;
  bool ok = true;
  std::string error;
};

struct GatewayStartupFailureContext {
  dasall::infra::config::InstallLayout install_layout;
  std::string requested_profile_id;
  int requested_port = kDefaultGatewayListenPort;
};

void emit_gateway_startup_failure(const GatewayStartupFailureContext& context,
                                  std::string_view stage,
                                  std::string_view error_code,
                                  std::string_view detail,
                                  std::string_view runtime_diagnostics = {}) {
  std::cerr << "[dasall_gateway] startup failure"
            << " stage=" << stage
            << " error_code=" << error_code
            << " trace_id=startup:gateway:" << stage
            << " requested_profile=" << context.requested_profile_id
            << " assets_root=" << context.install_layout.readonly_assets_root.string()
            << " profiles_root=" << context.install_layout.profiles_root.string()
            << " listen_port=" << context.requested_port;
  if (!runtime_diagnostics.empty()) {
    std::cerr << " runtime_diagnostics=" << runtime_diagnostics;
  }
  if (!detail.empty()) {
    std::cerr << " detail=" << detail;
  }
  std::cerr << "\n";
}

[[nodiscard]] bool gateway_startup_stage_forced(std::string_view stage) {
  const char* forced_stage = std::getenv(kGatewayStartupDiagnosticsForceStageEnv);
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

[[nodiscard]] ParsedGatewayArgs parse_gateway_args(int argc, char* argv[]) {
  ParsedGatewayArgs parsed;

  const auto parse_port = [&parsed](std::string_view port_text) -> std::optional<int> {
    int port = 0;
    const auto* begin = port_text.data();
    const auto* end = begin + port_text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, port);
    if (ec != std::errc{} || ptr != end || port <= 0 || port > 65535) {
      parsed.ok = false;
      parsed.error = "--port must be an integer within 1..65535";
      return std::nullopt;
    }

    return port;
  };

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

    if (arg.starts_with("--port=")) {
      const auto parsed_port = parse_port(
          arg.substr(std::string_view("--port=").size()));
      if (!parsed_port.has_value() ||
          !set_unique_option(&parsed.port, *parsed_port, "--port")) {
        return parsed;
      }
      continue;
    }

    if (arg == "--port") {
      if (index + 1 >= argc) {
        parsed.ok = false;
        parsed.error = "--port requires a value";
        return parsed;
      }

      const auto parsed_port = parse_port(argv[++index]);
      if (!parsed_port.has_value() ||
          !set_unique_option(&parsed.port, *parsed_port, "--port")) {
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

  const auto it = request.request_context.find(key);
  if (it == request.request_context.end() || it->second.empty()) {
    return std::nullopt;
  }
  return it->second;
}

[[nodiscard]] dasall::access::RuntimeDispatchResult map_agent_result_to_dispatch_result(
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
  dispatch_result.publish_envelope->agent_result = std::move(agent_result);
  return dispatch_result;
}

[[nodiscard]] AgentInitRequestBuildResult build_gateway_agent_init_request(
    std::shared_ptr<const dasall::profiles::RuntimePolicySnapshot> policy_snapshot) {
  dasall::apps::runtime_support::RuntimeLiveDependencyCompositionOptions
      composition_options;
  if (const auto state_root_override = runtime_state_root_override_from_env();
      state_root_override.has_value()) {
    composition_options.state_root_override = *state_root_override;
  }

  const auto runtime_composition =
      dasall::apps::runtime_support::compose_minimal_live_dependency_set(
          policy_snapshot,
          "gateway.http-unary",
          composition_options);
  if (!runtime_composition.ok()) {
    return AgentInitRequestBuildResult{
        .request = {},
        .error = runtime_composition.error,
    };
  }

  dasall::runtime::AgentInitRequest init_request;
  const std::string effective_profile_id = policy_snapshot->effective_profile_id();
  init_request.runtime_instance_id = "gateway.http-unary:" + effective_profile_id;
  init_request.profile_id = effective_profile_id;
  init_request.policy_snapshot = std::move(policy_snapshot);
  init_request.dependency_set = runtime_composition.dependency_set;
  init_request.boot_reason = "gateway-http-entry";
  init_request.cold_start = true;
  return AgentInitRequestBuildResult{
      .request = std::move(init_request),
      .error = {},
  };
}

/// 全局运行标志，用于信号处理器
std::atomic<bool> g_stop_requested{false};
httplib::Server* g_server = nullptr;

extern "C" void on_shutdown_signal(int /*signal*/) {
  g_stop_requested.store(true);
  if (g_server != nullptr) {
    g_server->stop();
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  const auto install_layout = dasall::infra::config::resolve_install_layout();
  const ParsedGatewayArgs parsed = parse_gateway_args(argc, argv);
  GatewayStartupFailureContext failure_context{
      .install_layout = install_layout,
      .requested_profile_id = parsed.profile_id.has_value()
                                  ? *parsed.profile_id
                                  : std::string(kDefaultGatewayProfileId),
      .requested_port = parsed.port.value_or(kDefaultGatewayListenPort),
  };
  if (!parsed.ok) {
    emit_gateway_startup_failure(failure_context,
                                 "argument-parse",
                                 "GATEWAY_E_ARGUMENT_PARSE_FAILED",
                                 parsed.error);
    return 1;
  }

  const dasall::profiles::ProfileCatalog catalog(install_layout.profiles_root);
  const dasall::profiles::RuntimePolicyProvider runtime_policy_provider(catalog);
  const auto runtime_snapshot = runtime_policy_provider.load_snapshot(
      dasall::profiles::RuntimePolicyLoadRequest{
          .profile_id = failure_context.requested_profile_id,
      });
  if (!runtime_snapshot.ok() || !runtime_snapshot.snapshot) {
    emit_gateway_startup_failure(
        failure_context,
        "runtime-policy-load",
        runtime_snapshot.error_code.has_value()
            ? dasall::profiles::profile_error_code_name(*runtime_snapshot.error_code)
            : std::string_view{"GATEWAY_E_RUNTIME_POLICY_SNAPSHOT_LOAD_FAILED"},
        "runtime policy snapshot load failed for requested profile");
    return 1;
  }

  auto runtime_facade = std::make_shared<dasall::runtime::AgentFacade>();
  if (gateway_startup_stage_forced("runtime-dependency-composition")) {
    emit_gateway_startup_failure(failure_context,
                                 "runtime-dependency-composition",
                                 "GATEWAY_E_RUNTIME_COMPOSITION_FAILED",
                                 "forced startup diagnostics failure");
    return 1;
  }
  const auto runtime_init_request =
      build_gateway_agent_init_request(runtime_snapshot.snapshot);
  if (!runtime_init_request.ok()) {
    emit_gateway_startup_failure(failure_context,
                                 "runtime-dependency-composition",
                                 "GATEWAY_E_RUNTIME_COMPOSITION_FAILED",
                                 runtime_init_request.error);
    return 1;
  }
  if (gateway_startup_stage_forced("runtime-init")) {
    emit_gateway_startup_failure(failure_context,
                                 "runtime-init",
                                 "GATEWAY_E_RUNTIME_INIT_FAILED",
                                 "forced startup diagnostics failure",
                                 "forced-runtime-init-diagnostics");
    return 1;
  }
  const auto runtime_init_result = runtime_facade->init(
      runtime_init_request.request);
  const bool runtime_entry_accepted = runtime_init_result.accepted;
  if (!runtime_entry_accepted) {
    emit_gateway_startup_failure(
        failure_context,
        "runtime-init",
        "GATEWAY_E_RUNTIME_INIT_FAILED",
        runtime_init_result.health_summary.empty()
            ? "runtime facade init rejected"
            : runtime_init_result.health_summary,
        runtime_init_result.diagnostics);
    return 1;
  }
  std::cout << "[dasall_gateway] runtime readiness="
            << runtime_init_result.readiness_label() << "\n";

  dasall::access::gateway::SecurityConfig sec_cfg;

  dasall::access::GatewayAccessPipelineOptions gateway_options;
  gateway_options.bootstrap_config.bootstrap_revision =
      "gateway-bootstrap:" + runtime_snapshot.snapshot->effective_profile_id();
  gateway_options.bootstrap_config.entry_type = "gateway";
  gateway_options.bootstrap_config.allowed_protocols = {"http_unary"};
  gateway_options.bootstrap_config.cors_allowed_origins = sec_cfg.cors_allowed_origins;
  gateway_options.derive_views_from_runtime_policy = true;
  gateway_options.runtime_policy_snapshot = runtime_snapshot.snapshot;
    const std::size_t max_http_request_body_bytes =
      static_cast<std::size_t>(gateway_options.bootstrap_config.max_payload_bytes);
  dasall::apps::runtime_support::wire_runtime_secret_manager_into_access_ownership_seam(
      runtime_init_request.request.dependency_set,
      gateway_options);
  gateway_options.runtime_dispatch_backend =
      [runtime_facade](const dasall::access::RuntimeDispatchRequest& request)
          -> dasall::access::RuntimeDispatchResult {
        const auto agent_result = runtime_facade->handle(request.agent_request);
        return map_agent_result_to_dispatch_result(request, agent_result);
      };

  auto gateway = dasall::access::create_gateway_access_gateway(
      std::move(gateway_options));
  if (gateway_startup_stage_forced("access-gateway-init")) {
    emit_gateway_startup_failure(failure_context,
                                 "access-gateway-init",
                                 "GATEWAY_E_ACCESS_GATEWAY_INIT_FAILED",
                                 "forced startup diagnostics failure");
    return 1;
  }
  if (!gateway->init()) {
    emit_gateway_startup_failure(failure_context,
                                 "access-gateway-init",
                                 "GATEWAY_E_ACCESS_GATEWAY_INIT_FAILED",
                                 "production submit pipeline unavailable");
    return 1;
  }

  // 2. 注册信号处理器
  std::signal(SIGTERM, on_shutdown_signal);
  std::signal(SIGINT, on_shutdown_signal);

  // 3. 创建 httplib 服务器
  httplib::Server srv;
  g_server = &srv;

  // 4. POST /v1/submit — HTTP unary 请求入口
  // 健康探针处理器
  dasall::access::gateway::HealthProbeHandler health;
  health.set_started(true);
  health.set_ready(gateway->is_ready() && runtime_entry_accepted &&
                   !runtime_init_result.stub_ready());
  health.set_readiness_detail(
      "runtime_readiness=" + runtime_init_result.readiness_label());

  /// 辅助：将安全头合并到 httplib::Response
  auto apply_sec = [&](httplib::Response& res,
                       const std::string& origin = "",
                       const std::string& req_id = "") {
    std::map<std::string, std::string> h;
    dasall::access::gateway::apply_security_headers(h, sec_cfg, origin, req_id);
    for (const auto& [k, v] : h) {
      res.set_header(k.c_str(), v.c_str());
    }
  };

  // 4a. GET /health/live
  srv.Get("/health/live", [&health, &apply_sec](const httplib::Request& req,
                                                 httplib::Response& res) {
    const auto r = health.handle_live();
    res.status = r.status_code;
    res.set_content(r.body, "text/plain");
    apply_sec(res, req.get_header_value("Origin"));
  });

  // 4b. GET /health/ready
  srv.Get("/health/ready", [&health, &apply_sec](const httplib::Request& req,
                                                  httplib::Response& res) {
    const auto r = health.handle_ready();
    res.status = r.status_code;
    res.set_content(r.body, "text/plain");
    apply_sec(res, req.get_header_value("Origin"));
  });

  // 4c. GET /health/startup
  srv.Get("/health/startup", [&health, &apply_sec](const httplib::Request& req,
                                                    httplib::Response& res) {
    const auto r = health.handle_startup();
    res.status = r.status_code;
    res.set_content(r.body, "text/plain");
    apply_sec(res, req.get_header_value("Origin"));
  });

  // 4d. OPTIONS preflight — 不经过 Admission，直接回应 204
  srv.Options(".*", [&apply_sec](const httplib::Request& req,
                                  httplib::Response& res) {
    res.status = 204;
    apply_sec(res, req.get_header_value("Origin"));
  });

  // 4e. POST /v1/submit — HTTP unary 请求入口
  srv.Post("/v1/submit", [&gateway, &apply_sec, max_http_request_body_bytes](
                                 const httplib::Request& req,
                                 httplib::Response& res) {
    dasall::access::gateway::HttpRequestContext ctx;
    ctx.method = req.method;
    ctx.path = req.path;
    ctx.body = req.body;
    for (const auto& [k, v] : req.headers) {
      ctx.headers[k] = v;
    }

    const std::string origin = req.get_header_value("Origin");
    const std::string req_id = req.get_header_value("X-Request-Id");

    const auto http_res = dasall::access::gateway::handle_submit_request(
        ctx,
        *gateway,
      max_http_request_body_bytes);
    res.status = http_res.status_code;
    const auto content_type = http_res.headers.find("Content-Type");
    res.set_content(http_res.body,
                    content_type == http_res.headers.end()
                        ? "application/json"
                        : content_type->second.c_str());
    for (const auto& [key, value] : http_res.headers) {
      if (key != "Content-Type") {
        res.set_header(key.c_str(), value.c_str());
      }
    }
    apply_sec(res, origin, req_id);
  });

  // 5. 启动监听
  const int port = parsed.port.value_or(kDefaultGatewayListenPort);
  std::cout << "[dasall_gateway] listening on :" << port
            << " profile=" << runtime_init_result.resolved_profile_id << "\n";
  if (!srv.listen("0.0.0.0", port)) {
    emit_gateway_startup_failure(failure_context,
                                 "listen",
                                 "GATEWAY_E_LISTEN_FAILED",
                                 "listen failed on 0.0.0.0:" + std::to_string(port));
    gateway->shutdown(std::chrono::milliseconds(5000));
    return 1;
  }

  // 6. 优雅关闭
  gateway->shutdown(std::chrono::milliseconds(5000));
  std::cout << "[dasall_gateway] stopped\n";
  return 0;
}
