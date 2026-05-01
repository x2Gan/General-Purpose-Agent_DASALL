#pragma once

#include <optional>
#include <string>
#include <vector>

namespace dasall::apps::cli {

/// 已解析的 CLI 命令结构
struct CliCommand {
  /// 命令名称（"ping" / "submit" / ...）
  std::string name;

  /// 可选的 payload JSON（run/submit 命令使用）
  std::optional<std::string> payload;

  /// 异步任务查询/取消所需 receipt_ref。
  std::optional<std::string> receipt_ref;

  /// receipt 所属权校验令牌。
  std::optional<std::string> ownership_token;

  /// 可选主体引用；仅在显式传入时带入请求。
  std::optional<std::string> actor_ref;

  /// diagnostics 子命令名。
  std::optional<std::string> diag_command;

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
///   readiness                 — 读取 daemon readiness 摘要
///   run <json_payload>        — 提交 InboundPacket JSON
///   submit <json_payload>     — run 的兼容别名
///   status <receipt_ref> <ownership_token> [actor_ref]
///   cancel <receipt_ref> <ownership_token> [actor_ref]
///   diag <command_name>       — 隐藏运维命令，默认不出现在 usage 中
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
