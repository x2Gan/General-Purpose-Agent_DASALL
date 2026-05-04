#include "CliCommandParser.h"

#include <charconv>
#include <optional>
#include <string_view>
#include <vector>

namespace dasall::apps::cli {

namespace {

struct ParsedStableFlags {
  std::optional<std::string> socket_path;
  CliOutputMode output_mode = CliOutputMode::Human;
  std::optional<int> timeout_ms;
  CliAsyncPreference async_preference = CliAsyncPreference::Sync;
  std::optional<std::string> request_id;
  std::optional<std::string> session_hint;
  std::optional<std::string> trace_id;
  bool quiet = false;
  bool no_input = false;
};

[[nodiscard]] bool parse_int_value(std::string_view text, int& value) {
  if (text.empty()) {
    return false;
  }

  int parsed_value = 0;
  const auto* begin = text.data();
  const auto* end = text.data() + text.size();
  const auto result = std::from_chars(begin, end, parsed_value);
  if (result.ec != std::errc() || result.ptr != end || parsed_value <= 0) {
    return false;
  }

  value = parsed_value;
  return true;
}

[[nodiscard]] bool assign_string_flag(std::optional<std::string>& target,
                                      std::string_view value) {
  if (target.has_value() || value.empty()) {
    return false;
  }

  target = std::string(value);
  return true;
}

[[nodiscard]] bool assign_bool_flag(bool& target) {
  if (target) {
    return false;
  }

  target = true;
  return true;
}

[[nodiscard]] bool parse_long_flag_with_value(
    std::string_view arg,
    std::string_view flag_name,
    std::size_t& index,
    const std::vector<std::string>& raw_args,
    std::optional<std::string>& output_value) {
  const auto inline_prefix = std::string(flag_name) + "=";
  if (arg.starts_with(inline_prefix)) {
    return assign_string_flag(output_value, arg.substr(inline_prefix.size()));
  }

  if (arg != flag_name) {
    return false;
  }

  if (index + 1 >= raw_args.size()) {
    return false;
  }

  ++index;
  return assign_string_flag(output_value, raw_args[index]);
}

[[nodiscard]] bool parse_timeout_flag(std::string_view arg,
                                      std::size_t& index,
                                      const std::vector<std::string>& raw_args,
                                      std::optional<int>& timeout_ms) {
  if (timeout_ms.has_value()) {
    return false;
  }

  std::string_view raw_value;
  constexpr std::string_view flag_name = "--timeout-ms";
  const auto inline_prefix = std::string(flag_name) + "=";
  if (arg.starts_with(inline_prefix)) {
    raw_value = arg.substr(inline_prefix.size());
  } else if (arg == flag_name) {
    if (index + 1 >= raw_args.size()) {
      return false;
    }
    raw_value = raw_args[++index];
  } else {
    return false;
  }

  int parsed_timeout = 0;
  if (!parse_int_value(raw_value, parsed_timeout)) {
    return false;
  }

  timeout_ms = parsed_timeout;
  return true;
}

void apply_flags_to_command(const ParsedStableFlags& flags, CliCommand& cmd) {
  cmd.socket_path = flags.socket_path;
  cmd.output_mode = flags.output_mode;
  cmd.timeout_ms = flags.timeout_ms;
  cmd.async_preference = flags.async_preference;
  cmd.request_id = flags.request_id;
  cmd.session_hint = flags.session_hint;
  cmd.trace_id = flags.trace_id;
  cmd.quiet = flags.quiet;
  cmd.no_input = flags.no_input;
}

[[nodiscard]] bool validate_flag_scope(std::string_view command_name,
                                       const ParsedStableFlags& flags) {
  const bool is_run_like = command_name == "run" || command_name == "submit";

  if (flags.async_preference == CliAsyncPreference::Async && !is_run_like) {
    return false;
  }

  if ((flags.request_id.has_value() || flags.session_hint.has_value() ||
       flags.trace_id.has_value() || flags.no_input) &&
      !is_run_like) {
    return false;
  }

  return true;
}

}  // namespace

std::optional<CliCommand> CliCommandParser::parse(int argc,
                                                   char const* const* argv) {
  if (argc < 2 || argv == nullptr) {
    return std::nullopt;
  }

  CliCommand cmd;
  // 收集原始参数（跳过 argv[0] 程序名）
  for (int i = 1; i < argc; ++i) {
    if (argv[i] != nullptr) {
      cmd.raw_args.emplace_back(argv[i]);
    }
  }
  if (cmd.raw_args.empty()) {
    return std::nullopt;
  }

  std::vector<std::string> positional_args;
  positional_args.reserve(cmd.raw_args.size());
  ParsedStableFlags flags;

  for (std::size_t index = 0; index < cmd.raw_args.size(); ++index) {
    const std::string_view arg(cmd.raw_args[index]);

    if (arg == "--json") {
      if (flags.output_mode == CliOutputMode::Json) {
        return std::nullopt;
      }

      flags.output_mode = CliOutputMode::Json;
      continue;
    }

    if (arg == "--async") {
      if (flags.async_preference == CliAsyncPreference::Async) {
        return std::nullopt;
      }

      flags.async_preference = CliAsyncPreference::Async;
      continue;
    }

    if (arg == "--quiet") {
      if (!assign_bool_flag(flags.quiet)) {
        return std::nullopt;
      }

      continue;
    }

    if (arg == "--no-input") {
      if (!assign_bool_flag(flags.no_input)) {
        return std::nullopt;
      }

      continue;
    }

    if (arg.starts_with("--socket-path=") || arg == "--socket-path") {
      if (!parse_long_flag_with_value(arg, "--socket-path", index,
                                      cmd.raw_args, flags.socket_path)) {
        return std::nullopt;
      }
      continue;
    }

    if (arg.starts_with("--request-id=") || arg == "--request-id") {
      if (!parse_long_flag_with_value(arg, "--request-id", index,
                                      cmd.raw_args, flags.request_id)) {
        return std::nullopt;
      }
      continue;
    }

    if (arg.starts_with("--session=") || arg == "--session") {
      if (!parse_long_flag_with_value(arg, "--session", index, cmd.raw_args,
                                      flags.session_hint)) {
        return std::nullopt;
      }
      continue;
    }

    if (arg.starts_with("--trace-id=") || arg == "--trace-id") {
      if (!parse_long_flag_with_value(arg, "--trace-id", index, cmd.raw_args,
                                      flags.trace_id)) {
        return std::nullopt;
      }
      continue;
    }

    if (arg.starts_with("--timeout-ms=") || arg == "--timeout-ms") {
      if (!parse_timeout_flag(arg, index, cmd.raw_args, flags.timeout_ms)) {
        return std::nullopt;
      }
      continue;
    }

    positional_args.push_back(cmd.raw_args[index]);
  }

  if (positional_args.empty()) {
    return std::nullopt;
  }

  cmd.name = positional_args[0];
  if (!validate_flag_scope(cmd.name, flags)) {
    return std::nullopt;
  }

  apply_flags_to_command(flags, cmd);

  if (cmd.name == "ping" || cmd.name == "readiness") {
    return cmd;
  }

  if (cmd.name == "run" || cmd.name == "submit") {
    if (positional_args.size() < 2 || positional_args[1].empty()) {
      return std::nullopt;
    }
    cmd.name = "run";
    cmd.payload = positional_args[1];
    return cmd;
  }

  if (cmd.name == "status" || cmd.name == "cancel") {
    if (positional_args.size() < 3 || positional_args[1].empty() ||
        positional_args[2].empty()) {
      return std::nullopt;
    }

    cmd.receipt_ref = positional_args[1];
    cmd.selector_kind = CliSelectorKind::Receipt;
    cmd.selector_value = positional_args[1];
    cmd.ownership_token = positional_args[2];
    if (positional_args.size() >= 4 && !positional_args[3].empty()) {
      cmd.actor_ref = positional_args[3];
    }
    return cmd;
  }

  if (cmd.name == "diag" || cmd.name == "diagnostics") {
    if (positional_args.size() < 2 || positional_args[1].empty()) {
      return std::nullopt;
    }

    cmd.name = "diag";
    cmd.diag_command = positional_args[1];
    return cmd;
  }

  // 未知命令
  return std::nullopt;
}

std::string CliCommandParser::usage_string() {
  return "Usage: dasall_cli [--socket-path <path>] <command> [args]\n"
         "Options:\n"
         "  --socket-path <path>                      Override daemon unix socket path\n"
         "Commands:\n"
         "  ping                                      Send a health check to dasall daemon\n"
         "  readiness                                 Read daemon readiness summary\n"
         "  run <json_payload>                        Submit a request to dasall daemon\n"
         "  submit <json_payload>                     Backward-compatible alias for run\n"
         "  status <receipt_ref> <ownership_token>    Query async receipt status\n"
         "  cancel <receipt_ref> <ownership_token>    Cancel an async receipt\n";
}

}  // namespace dasall::apps::cli
