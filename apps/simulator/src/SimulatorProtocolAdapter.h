/// apps/simulator/src/SimulatorProtocolAdapter.h
///
/// DASALL simulator protocol adapter（v1）
///
/// 职责：
///   - 实现 IProtocolAdapter 对应的 decode/encode 接口
///   - 提供确定性测试输入与受控 subject stub
///   - 仅在测试/工厂 profile 下启用（profile allowlist 约束）
///
/// 设计约束（Access 详设 6.14.9、6.15.3）：
///   - simulator 入口不越权，subject stub 行为可重复
///   - decode 从测试 fixture JSON body 提取 entry_type/payload，注入确定性 subject
///   - encode 填充响应，不经过真实 runtime（仅 simulator context 处理）
///   - session_id/idempotency 由 fixture 装配注入，保证测试可重放
#pragma once

#include <string>
#include <vector>

#include "IProtocolAdapter.h"

namespace dasall::access::simulator {

/// 确定性测试主体存根
struct DeterministicSubjectStub {
  std::string actor_ref;      ///< fixture 注入的确定性 actor_ref（用于测试）
  std::vector<std::string> granted_actions;  ///< 允许的 action 白名单（deny-by-default）
  std::string override_source;  ///< 若非空，表示使用 override 路径（仅 factory profile）
};

/// SimulatorProtocolAdapter — 确定性测试刺激适配器
///
/// 无 IPC 依赖的纯测试桩，fixture 直接装配注入主体和请求上下文。
class SimulatorProtocolAdapter final : public dasall::access::IProtocolAdapter {
 public:
  explicit SimulatorProtocolAdapter(const DeterministicSubjectStub& subject = {})
      : subject_(subject) {}

  /// IProtocolAdapter 接口实现
  /// 覆盖 entry_type="simulator"、protocol_kind="deterministic_test"
  bool can_handle(std::string_view entry_type,
                  std::string_view protocol_kind) const override;

  /// decode：从 fixture JSON body 提取请求，注入确定性 subject
  /// fixture 格式：{ "entry_type": "...", "request_id": "...", "payload": {...} }
  [[nodiscard]] dasall::access::InboundPacket decode() override;

  /// encode：填充响应（v1 返回 pending，simulator 不执行实际 runtime）
  bool encode(const dasall::access::PublishEnvelope& envelope) override;

  /// 设置活跃请求上下文（来自 fixture）
  void set_active_request(const std::string& request_body);

  /// 获取活跃响应上下文
  const std::string& active_response_body() const { return response_body_; }

 private:
  DeterministicSubjectStub subject_;
  std::string request_body_;
  std::string response_body_;
};

}  // namespace dasall::access::simulator
