#pragma once

#include <string>
#include <string_view>

#include "CliExitDecision.h"
#include "CliIpcClient.h"

namespace dasall::apps::cli {

/// CliOutputFormatter — CLI 输出格式化工具
///
/// 职责：
///   - 将结构化 daemon 响应格式化为人类可读文本
///   - 提供 success / failure / error 标准输出格式
///   - 纯函数，不依赖任何 I/O
///
/// 边界约束：
///   - 不直接写 stdout/stderr，由调用方负责打印
///   - 仅做字符串变换，不执行 IPC
class CliOutputFormatter {
 public:
  [[nodiscard]] static std::string format_command_human_output(
      std::string_view command,
      const DaemonClientResponse& response);

  [[nodiscard]] static std::string format_json_output(
      std::string_view command,
      const DaemonClientResponse& response,
      const CliExitDecision& decision);

  /// 格式化 ping 成功响应。
  /// @param response 守护进程返回的结构化响应
  /// @return 人类可读的成功消息
  [[nodiscard]] static std::string format_ping_success(
      const DaemonClientResponse& response);

  /// 格式化 readiness 成功响应。
  [[nodiscard]] static std::string format_readiness_success(
      const DaemonClientResponse& response);

  /// 格式化 ping/readiness 失败消息（守护进程不可达或返回非法响应）。
  /// @return 人类可读的失败消息
  [[nodiscard]] static std::string format_ping_failure(std::string_view reason);

  /// 格式化 run/submit 成功响应。
  /// @param response 守护进程返回的结构化响应
  /// @return 人类可读的成功消息
  [[nodiscard]] static std::string format_submit_success(
      const DaemonClientResponse& response);

  [[nodiscard]] static std::string format_status_success(
      const DaemonClientResponse& response);

  [[nodiscard]] static std::string format_cancel_success(
      const DaemonClientResponse& response);

  [[nodiscard]] static std::string format_diag_success(
      const DaemonClientResponse& response);

  /// 格式化错误消息。
  /// @param reason 错误原因描述
  /// @return 人类可读的错误消息
  [[nodiscard]] static std::string format_error(std::string_view reason);
};

}  // namespace dasall::apps::cli
