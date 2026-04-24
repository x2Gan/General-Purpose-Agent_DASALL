#pragma once

#include <optional>
#include <string>
#include <vector>

namespace dasall::apps::cli {

/// 已解析的 CLI 命令结构
struct CliCommand {
  /// 命令名称（"ping" / "submit" / ...）
  std::string name;

  /// 可选的 payload JSON（submit 命令使用）
  std::optional<std::string> payload;

  /// 原始参数列表（供诊断用）
  std::vector<std::string> raw_args;
};

/// CliCommandParser — 纯函数式 CLI 参数解析器
///
/// 职责：
///   - 将 argc/argv 转换为 CliCommand 结构
///   - 不依赖 IPC 或任何 I/O；仅解析，不执行
///
/// 支持命令：
///   ping                      — 健康检查
///   submit <json_payload>     — 提交 InboundPacket JSON
///
/// 错误处理：
///   - 参数不合法时返回 std::nullopt
class CliCommandParser {
 public:
  /// 解析命令行参数。
  /// @param argc  参数个数（含程序名）
  /// @param argv  参数数组（argv[0] 为程序名）
  /// @return CliCommand on success；nullopt on parse error
  [[nodiscard]] static std::optional<CliCommand> parse(int argc,
                                                        char const* const* argv);

  /// 返回使用说明字符串（供 help / error 提示用）
  [[nodiscard]] static std::string usage_string();
};

}  // namespace dasall::apps::cli
