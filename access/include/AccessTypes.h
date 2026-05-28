#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "agent/AgentRequest.h"
#include "agent/AgentResult.h"

namespace dasall::access {

// AccessGateway 生命周期状态枚举
enum class AccessGatewayState {
  Uninitialized = 0,    // 未初始化：init() 前的初始状态
  Initializing = 1,     // 正在初始化：注册 adapter、pipeline 阶段
  Ready = 2,            // 就绪：唯一可接受新请求的状态
  Draining = 3,         // 排空中：shutdown() 已触发，不再接受新请求
  ShutDown = 4,         // 已终止：所有资源已释放，网关已停止
};

enum class AccessDisposition {
  Rejected = 0,
  Completed = 1,
  AcceptedAsync = 2,
  StreamAttached = 3,
};

// SubjectIdentity - 认证主体事实
struct SubjectIdentity {
  std::string actor_ref;        // 调用者唯一标识：scheme://source/identifier
  std::string subject_type;     // 主体类型：user/service/operator/diagnostic_endpoint
  std::string auth_method;      // 认证方法：mTLS/JWT/token/local_trusted
  std::string trust_level;      // 信任等级：public/authenticated/trusted/ops_privileged
  std::string tenant_ref;       // 租户引用（多租户隔离）
  std::optional<std::string> auth_metadata;  // 补充数据
};

// LocalPeerUidFact - daemon 本地控制面 peer 身份事实
struct LocalPeerUidFact {
  std::string actor_ref;
  std::uint32_t peer_uid = 0;
  std::uint32_t peer_gid = 0;
  std::uint32_t peer_pid = 0;
  bool is_local_socket_peer = false;
  bool eligible_for_local_trusted = false;
};

// AccessDecisionProof - 授权裁定证据
struct AccessDecisionProof {
  std::string decision;                      // Allow / Deny / RequireConfirmation
  std::string policy_decision_ref;           // 策略决策引用
  std::string reason_code;                   // 拒绝原因码
  std::optional<std::string> reason_description;  // 拒绝原因描述
  std::optional<std::string> evaluated_at;   // 评估时间戳
};

struct InboundPacket {
  std::string packet_id;
  std::string entry_type;
  std::string protocol_kind;
  std::string peer_ref;
  std::string payload;
  std::map<std::string, std::string> headers;
  std::optional<std::string> trace_id;
  std::optional<std::string> session_hint;
  std::optional<int> deadline_ms;
  bool async_preferred = false;
  bool stream_requested = false;
};

struct RuntimeDispatchRequest {
  InboundPacket packet;
  // === 新增：认证与授权 sidecar ===
  SubjectIdentity subject_identity;          // 认证主体事实
  AccessDecisionProof decision_proof;        // 授权裁定证据

  // public handoff：shared AgentRequest 必须在 Access -> Runtime 边界显式承载，
  // 不能只靠 request_context sidecar 重投影。
  dasall::contracts::AgentRequest agent_request;
  
  // 客户端能力视图（stream 能力判定）
  std::string client_capability_view;
  
  // 发布模式标志
  bool async_allowed = false;
  bool stream_requested = false;
  
  // 请求头扩展
  std::map<std::string, std::string> request_context;
  
  // 访问层 deadline（内部使用）
  std::optional<std::string> access_deadline;
};

// AsyncTaskReceipt - 异步受理凭证（必须在 PublishEnvelope 之前定义）
struct AsyncTaskReceipt {
  std::string receipt_id;                                    // 凭证唯一 ID
  std::string request_id;                                    // 原始请求 ID
  std::string session_id;                                    // 原始会话 ID
  std::string actor_ref;                                     // 原始调用者（所有权验证）
  std::string task_ref;                                      // Runtime 任务引用
  std::chrono::steady_clock::time_point expires_at;          // TTL 过期时间
  std::string ownership_token;                               // HMAC 所有权验证令牌
  std::optional<std::string> initial_status;                 // 初始任务状态快照
};

struct PublishEnvelope {
  std::string request_id;
  std::string result_id;
  std::string session_id;                    // 会话 ID
  std::string trace_id;                      // 链路追踪 ID
  std::string channel_ref;                   // 内部通道引用
  std::string protocol_kind;
  std::optional<dasall::contracts::AgentResult> agent_result;  // shared facts
  std::string protocol_status_hint;          // HTTP 状态码 hint
  std::string protocol_metadata;             // Protocol 特定元数据
  bool is_final = true;                      // 是否为最终响应
  std::string payload;                       // 原 payload 字段保持兼容
  std::optional<AsyncTaskReceipt> receipt;   // accepted_async 响应时包含 receipt
};

struct RuntimeDispatchResult {
  AccessDisposition disposition = AccessDisposition::Rejected;
  std::optional<PublishEnvelope> publish_envelope;
  std::optional<std::string> receipt_ref;           // 异步凭证 ID
  std::optional<std::string> subscription_ref;      // 流式订阅 ID
  std::optional<std::string> error_ref;
  std::map<std::string, std::string> response_context;  // 响应上下文
};

// AccessAdmissionResult - Admission 统一准入结果
struct AccessAdmissionResult {
  bool admitted = false;                             // 是否准入
  bool replay_hit = false;                           // 是否命中幂等重放
  bool conflict_hit = false;                         // 是否发生幂等冲突
  std::optional<std::string> ticket_ref;             // inflight 票据引用
  std::optional<std::string> replay_receipt_ref;     // replay 命中时的回执引用
  std::optional<std::string> reject_reason;          // 拒绝原因
  std::optional<std::string> challenge_hint;         // challenge 提示
};

// AccessBootstrapConfig - 启动事实配置
struct AccessBootstrapConfig {
  std::string bootstrap_revision;
  std::string entry_type;

  std::string listen_ref;
  std::vector<std::string> allowed_protocols;
  std::string peer_auth_mode = "strict";
  std::optional<std::string> auth_provider_ref;

  int idempotency_window_ms = 300000;
  int max_inflight_requests = 256;
  int dispatch_deadline_ms = 30000;

  int result_replay_ttl_ms = 600000;
  int stream_heartbeat_ms = 15000;
  int slow_consumer_max_buffer = 32;
  int drain_timeout_ms = 10000;

  int max_payload_bytes = 1048576;
  int max_user_input_bytes = 65536;
  std::vector<std::string> cors_allowed_origins;
  std::string session_id_mode = "auto";

  std::vector<std::string> trusted_local_subjects;
  std::optional<std::string> ownership_token_hmac_secret_ref;
};

// SnapshotVersionFingerprint - 配置快照版本锚点
struct SnapshotVersionFingerprint {
  std::string bootstrap_revision;
  std::string effective_profile_id;
  std::int64_t runtime_policy_generation = 0;
};

// AccessAuthView - 认证治理视图
struct AccessAuthView {
  std::string peer_auth_mode = "strict";
  std::optional<std::string> auth_provider_ref;
  std::vector<std::string> trusted_local_subjects;
  bool strict_auth_required = true;
};

// AccessAdmissionView - 准入治理视图
struct AccessAdmissionView {
  int idempotency_window_ms = 300000;
  int max_inflight_requests = 256;
  int dispatch_deadline_ms = 30000;
  bool default_deny = true;
};

// AccessPublishView - 发布治理视图
struct AccessPublishView {
  int result_replay_ttl_ms = 600000;
  int stream_heartbeat_ms = 15000;
  int slow_consumer_max_buffer = 32;
  int drain_timeout_ms = 10000;
  int max_payload_bytes = 1048576;
  int max_user_input_bytes = 65536;
  std::vector<std::string> cors_allowed_origins;
};

// AccessRuntimeGovernanceView - runtime/profile/infra 投影视图
struct AccessRuntimeGovernanceView {
  std::string runtime_budget_profile;
  std::string timeout_policy_profile;
  std::string ops_log_level;
  std::string ops_trace_sample_ratio;
  bool remote_diagnostics_enabled = false;
  std::string security_default_effect = "deny";
  bool diag_pull_enabled = false;
};

}  // namespace dasall::access