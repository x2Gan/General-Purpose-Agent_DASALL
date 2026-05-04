#pragma once

#include <optional>
#include <string>
#include <vector>

namespace dasall::apps::cli {

enum class CliOutputMode {
  Human,
  Json,
};

enum class CliAsyncPreference {
  Sync,
  Async,
};

enum class CliSelectorKind {
  None,
  Receipt,
  RequestId,
};

/// 已解析的 CLI 命令结构
struct CliCommand {
  /// 命令名称（"ping" / "submit" / ...）
  std::string name;

  /// 可选 daemon socket path 覆盖；缺省时由 CLI 使用共享默认值。
  std::optional<std::string> socket_path;

  /// 可选的 payload JSON（run/submit 命令使用）
  std::optional<std::string> payload;

  /// 异步任务查询/取消所需 receipt_ref。
  std::optional<std::string> receipt_ref;

  /// status/cancel 所使用的稳定 selector 类型。
  CliSelectorKind selector_kind = CliSelectorKind::None;

  /// selector 的稳定值。当前 receipt 路径会与 receipt_ref 同步；后续
  /// request-id 路径会在同一字段中承载目标 request_id。
  std::optional<std::string> selector_value;

  /// receipt 所属权校验令牌。
  std::optional<std::string> ownership_token;

  /// 客户端输出模式；v1 缺省为人类可读。
  CliOutputMode output_mode = CliOutputMode::Human;

  /// 客户端等待上限；缺省时由调用方/部署默认值决定。
  std::optional<int> timeout_ms;

  /// 是否显式要求 accepted_async。
  CliAsyncPreference async_preference = CliAsyncPreference::Sync;

  /// 显式指定的请求 ID。
  std::optional<std::string> request_id;

  /// 提供给 daemon 的 session hint。
  std::optional<std::string> session_hint;

  /// 显式指定的 trace ID。
  std::optional<std::string> trace_id;

  /// 是否抑制非必要 stderr 信息。
  bool quiet = false;

  /// 是否禁止交互式提示。
  bool no_input = false;

  /// 可选主体引用；仅在显式传入时带入请求。
  std::optional<std::string> actor_ref;

  /// diagnostics 子命令名。
  std::optional<std::string> diag_command;

  /// 本地 help 命令所请求的命令路径，如 {"diag", "health"}。
  std::vector<std::string> help_path;

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
  [[nodiscard]] static std::string usage_string(
      std::string_view command_name = {},
      std::string_view subcommand_name = {});
};

}  // namespace dasall::apps::cli
