#pragma once

#include <string>
#include <string_view>

namespace dasall::apps::cli {

/// CliOutputFormatter — CLI 输出格式化工具
///
/// 职责：
///   - 将原始 JSON 响应字符串格式化为人类可读文本
///   - 提供 success / failure / error 标准输出格式
///   - 纯函数，不依赖任何 I/O
///
/// 边界约束：
///   - 不直接写 stdout/stderr，由调用方负责打印
///   - 仅做字符串变换，不解析 JSON
class CliOutputFormatter {
 public:
  /// 格式化 ping 成功响应。
  /// @param raw_response 守护进程返回的原始 JSON 字符串
  /// @return 人类可读的成功消息
  [[nodiscard]] static std::string format_ping_success(
      std::string_view raw_response);

  /// 格式化 ping 失败消息（守护进程不可达）。
  /// @return 人类可读的失败消息
  [[nodiscard]] static std::string format_ping_failure();

  /// 格式化 submit 成功响应。
  /// @param raw_response 守护进程返回的原始 JSON 字符串
  /// @return 人类可读的成功消息
  [[nodiscard]] static std::string format_submit_success(
      std::string_view raw_response);

  /// 格式化错误消息。
  /// @param reason 错误原因描述
  /// @return 人类可读的错误消息
  [[nodiscard]] static std::string format_error(std::string_view reason);
};

}  // namespace dasall::apps::cli
