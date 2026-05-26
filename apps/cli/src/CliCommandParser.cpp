#include "CliCommandParser.h"

#include <charconv>
#include <optional>
#include <string_view>
#include <utility>
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

struct ParsedConfigFlags {
  std::optional<std::string> from_file;
  bool dry_run = false;
};

struct ParsedKnowledgeFlags {
  std::optional<std::string> preferred_mode;
  std::optional<std::string> query_kind;
  std::vector<std::string> allowed_corpora;
  std::vector<std::string> domain_tags;
  std::vector<std::string> required_tags;
  std::optional<std::string> required_language;
  std::vector<std::string> changed_sources;
};

[[nodiscard]] std::string canonicalize_keyword_token(std::string_view value) {
  std::string canonical;
  canonical.reserve(value.size());

  bool previous_was_separator = false;
  for (const unsigned char character : value) {
    if (std::isalnum(character) != 0) {
      canonical.push_back(
          static_cast<char>(std::tolower(character)));
      previous_was_separator = false;
      continue;
    }

    if (character == '-' || character == '_' || std::isspace(character) != 0) {
      if (!canonical.empty() && !previous_was_separator) {
        canonical.push_back('_');
        previous_was_separator = true;
      }
      continue;
    }

    return {};
  }

  while (!canonical.empty() && canonical.back() == '_') {
    canonical.pop_back();
  }

  return canonical;
}

[[nodiscard]] bool is_valid_knowledge_preferred_mode(std::string_view value) {
  return value == "lexical_only" || value == "dense_only" || value == "hybrid";
}

[[nodiscard]] bool is_valid_knowledge_query_kind(std::string_view value) {
  return value == "fact_lookup" || value == "procedure_lookup" ||
         value == "diagnostic_context" || value == "policy_evidence" ||
         value == "multi_hop";
}

[[nodiscard]] bool has_knowledge_only_flags(const ParsedKnowledgeFlags& flags) {
  return flags.preferred_mode.has_value() || flags.query_kind.has_value() ||
         flags.required_language.has_value() || !flags.allowed_corpora.empty() ||
         !flags.domain_tags.empty() || !flags.required_tags.empty() ||
         !flags.changed_sources.empty();
}

[[nodiscard]] bool has_knowledge_retrieve_flags(const ParsedKnowledgeFlags& flags) {
  return flags.preferred_mode.has_value() || flags.query_kind.has_value() ||
         flags.required_language.has_value() || !flags.allowed_corpora.empty() ||
         !flags.domain_tags.empty() || !flags.required_tags.empty();
}

[[nodiscard]] bool has_local_only_flags(const ParsedStableFlags& flags) {
  return flags.socket_path.has_value() || flags.timeout_ms.has_value() ||
         flags.async_preference == CliAsyncPreference::Async ||
         flags.request_id.has_value() || flags.session_hint.has_value() ||
         flags.trace_id.has_value() || flags.no_input;
}

[[nodiscard]] bool validate_help_scope(const ParsedStableFlags& flags) {
  return flags.output_mode == CliOutputMode::Human && !flags.quiet &&
         !has_local_only_flags(flags);
}

[[nodiscard]] bool validate_version_scope(const ParsedStableFlags& flags) {
  return !has_local_only_flags(flags);
}

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

[[nodiscard]] bool parse_repeated_string_flag(
    std::string_view arg,
    std::string_view flag_name,
    std::size_t& index,
    const std::vector<std::string>& raw_args,
    std::vector<std::string>& output_values) {
  std::optional<std::string> parsed_value;
  if (!parse_long_flag_with_value(arg, flag_name, index, raw_args, parsed_value) ||
      !parsed_value.has_value()) {
    return false;
  }

  if (parsed_value->empty()) {
    return false;
  }

  output_values.push_back(*parsed_value);
  return true;
}

[[nodiscard]] bool parse_keyword_flag(
    std::string_view arg,
    std::string_view flag_name,
    std::size_t& index,
    const std::vector<std::string>& raw_args,
    std::optional<std::string>& output_value,
    bool (*validator)(std::string_view)) {
  if (output_value.has_value()) {
    return false;
  }

  std::optional<std::string> parsed_value;
  if (!parse_long_flag_with_value(arg, flag_name, index, raw_args, parsed_value) ||
      !parsed_value.has_value()) {
    return false;
  }

  const auto canonical = canonicalize_keyword_token(*parsed_value);
  if (canonical.empty() || !validator(canonical)) {
    return false;
  }

  output_value = canonical;
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
  if (command_name == "help") {
    return validate_help_scope(flags);
  }

  if (command_name == "version") {
    return validate_version_scope(flags);
  }

  const bool is_run_like = command_name == "run" || command_name == "submit";
  const bool is_daemon_query = command_name == "ping" || command_name == "readiness" ||
                               command_name == "status" || command_name == "cancel" ||
                               command_name == "diag" || command_name == "diagnostics" ||
                               command_name == "knowledge";

  if (flags.async_preference == CliAsyncPreference::Async && !is_run_like) {
    return false;
  }

  if ((flags.request_id.has_value() || flags.session_hint.has_value() ||
       flags.trace_id.has_value() || flags.no_input) &&
      !is_run_like) {
    return false;
  }

  if ((flags.socket_path.has_value() || flags.timeout_ms.has_value()) &&
      !(is_run_like || is_daemon_query)) {
    return false;
  }

  return true;
}

[[nodiscard]] CliConfigCommandKind parse_config_command_kind(
    const std::vector<std::string>& positional_args) {
  if (positional_args.size() == 1) {
    return CliConfigCommandKind::Wizard;
  }

  if (positional_args.size() != 2) {
    return CliConfigCommandKind::None;
  }

  const std::string_view subcommand = positional_args[1];
  if (subcommand == "show") {
    return CliConfigCommandKind::Show;
  }

  if (subcommand == "plan") {
    return CliConfigCommandKind::Plan;
  }

  if (subcommand == "validate") {
    return CliConfigCommandKind::Validate;
  }

  if (subcommand == "apply") {
    return CliConfigCommandKind::Apply;
  }

  return CliConfigCommandKind::None;
}

[[nodiscard]] bool uses_daemon_transport_flags(const ParsedStableFlags& flags) {
  return flags.socket_path.has_value() || flags.timeout_ms.has_value() ||
         flags.async_preference == CliAsyncPreference::Async ||
         flags.request_id.has_value() || flags.session_hint.has_value() ||
         flags.trace_id.has_value() || flags.quiet;
}

[[nodiscard]] bool validate_config_scope(
    const CliConfigCommandKind command_kind,
    const ParsedStableFlags& flags,
    const ParsedConfigFlags& config_flags,
    const std::vector<std::string>& positional_args) {
  if (command_kind == CliConfigCommandKind::None ||
      uses_daemon_transport_flags(flags)) {
    return false;
  }

  switch (command_kind) {
    case CliConfigCommandKind::Wizard:
      return positional_args.size() == 1 && !config_flags.from_file.has_value() &&
             !config_flags.dry_run && !flags.no_input &&
             flags.output_mode == CliOutputMode::Human;
    case CliConfigCommandKind::Show:
    case CliConfigCommandKind::Validate:
      return positional_args.size() == 2 && !config_flags.from_file.has_value() &&
             !config_flags.dry_run && !flags.no_input;
    case CliConfigCommandKind::Plan:
      return positional_args.size() == 2 && !flags.no_input;
    case CliConfigCommandKind::Apply:
      return positional_args.size() == 2 && config_flags.from_file.has_value() &&
             !config_flags.dry_run && flags.no_input;
    case CliConfigCommandKind::None:
      return false;
  }

  return false;
}

void apply_config_flags_to_command(const ParsedConfigFlags& flags,
                                   const CliConfigCommandKind command_kind,
                                   CliCommand& cmd) {
  cmd.config_command = command_kind;
  cmd.config_from_file = flags.from_file;
  cmd.config_dry_run = flags.dry_run;
}

[[nodiscard]] CliCommand make_help_command(std::vector<std::string> help_path) {
  CliCommand cmd;
  cmd.name = "help";
  cmd.help_path = std::move(help_path);
  return cmd;
}

[[nodiscard]] CliKnowledgeCommandKind parse_knowledge_command_kind(
    std::string_view subcommand) {
  if (subcommand == "health") {
    return CliKnowledgeCommandKind::Health;
  }
  if (subcommand == "retrieve") {
    return CliKnowledgeCommandKind::Retrieve;
  }
  if (subcommand == "refresh") {
    return CliKnowledgeCommandKind::Refresh;
  }
  return CliKnowledgeCommandKind::None;
}

[[nodiscard]] std::string join_positional_tail(
    const std::vector<std::string>& positional_args,
    std::size_t start_index) {
  std::string joined;
  for (std::size_t index = start_index; index < positional_args.size(); ++index) {
    if (positional_args[index].empty()) {
      continue;
    }
    if (!joined.empty()) {
      joined.push_back(' ');
    }
    joined += positional_args[index];
  }
  return joined;
}

[[nodiscard]] std::vector<std::string> build_help_path(
    const std::vector<std::string>& positional_args,
    const bool explicit_help_command) {
  if (positional_args.empty()) {
    return {};
  }

  if (explicit_help_command) {
    if (positional_args.size() == 1) {
      return {};
    }
    return std::vector<std::string>(positional_args.begin() + 1,
                                    positional_args.end());
  }

  return positional_args;
}

}  // namespace

std::optional<CliCommand> CliCommandParser::parse(int argc,
                                                   char const* const* argv) {
  if (argc < 1 || argv == nullptr) {
    return std::nullopt;
  }

  CliCommand cmd;
  // 收集原始参数（跳过 argv[0] 程序名）
  for (int i = 1; i < argc; ++i) {
    if (argv[i] != nullptr) {
      cmd.raw_args.emplace_back(argv[i]);
    }
  }

  std::vector<std::string> positional_args;
  positional_args.reserve(cmd.raw_args.size());
  ParsedStableFlags flags;
  ParsedConfigFlags config_flags;
  ParsedKnowledgeFlags knowledge_flags;
  bool help_requested = false;

  for (std::size_t index = 0; index < cmd.raw_args.size(); ++index) {
    const std::string_view arg(cmd.raw_args[index]);

    if (arg == "-h" || arg == "--help") {
      help_requested = true;
      continue;
    }

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

    if (arg == "--dry-run") {
      if (!assign_bool_flag(config_flags.dry_run)) {
        return std::nullopt;
      }
      continue;
    }

    if (arg.starts_with("--from-file=") || arg == "--from-file") {
      if (!parse_long_flag_with_value(arg, "--from-file", index,
                                      cmd.raw_args, config_flags.from_file)) {
        return std::nullopt;
      }
      continue;
    }

    if (arg.starts_with("--preferred-mode=") || arg == "--preferred-mode") {
      if (!parse_keyword_flag(arg, "--preferred-mode", index, cmd.raw_args,
                              knowledge_flags.preferred_mode,
                              is_valid_knowledge_preferred_mode)) {
        return std::nullopt;
      }
      continue;
    }

    if (arg.starts_with("--query-kind=") || arg == "--query-kind") {
      if (!parse_keyword_flag(arg, "--query-kind", index, cmd.raw_args,
                              knowledge_flags.query_kind,
                              is_valid_knowledge_query_kind)) {
        return std::nullopt;
      }
      continue;
    }

    if (arg.starts_with("--allowed-corpus=") || arg == "--allowed-corpus") {
      if (!parse_repeated_string_flag(arg, "--allowed-corpus", index,
                                      cmd.raw_args,
                                      knowledge_flags.allowed_corpora)) {
        return std::nullopt;
      }
      continue;
    }

    if (arg.starts_with("--domain-tag=") || arg == "--domain-tag") {
      if (!parse_repeated_string_flag(arg, "--domain-tag", index,
                                      cmd.raw_args,
                                      knowledge_flags.domain_tags)) {
        return std::nullopt;
      }
      continue;
    }

    if (arg.starts_with("--required-tag=") || arg == "--required-tag") {
      if (!parse_repeated_string_flag(arg, "--required-tag", index,
                                      cmd.raw_args,
                                      knowledge_flags.required_tags)) {
        return std::nullopt;
      }
      continue;
    }

    if (arg.starts_with("--required-language=") || arg == "--required-language") {
      if (!parse_long_flag_with_value(arg, "--required-language", index,
                                      cmd.raw_args,
                                      knowledge_flags.required_language)) {
        return std::nullopt;
      }
      continue;
    }

    if (arg.starts_with("--changed-source=") || arg == "--changed-source") {
      if (!parse_repeated_string_flag(arg, "--changed-source", index,
                                      cmd.raw_args,
                                      knowledge_flags.changed_sources)) {
        return std::nullopt;
      }
      continue;
    }

    positional_args.push_back(cmd.raw_args[index]);
  }

  if (cmd.raw_args.empty()) {
    return make_help_command({});
  }

  if (help_requested) {
    const bool explicit_help_command = !positional_args.empty() && positional_args[0] == "help";
    if (!validate_help_scope(flags) || has_knowledge_only_flags(knowledge_flags)) {
      return std::nullopt;
    }

    auto help_cmd = make_help_command(
        build_help_path(positional_args, explicit_help_command));
    help_cmd.raw_args = cmd.raw_args;
    return help_cmd;
  }

  if (positional_args.empty()) {
    return std::nullopt;
  }

  cmd.name = positional_args[0];

  if (cmd.name == "config") {
    const auto config_command = parse_config_command_kind(positional_args);
    if (!validate_config_scope(config_command, flags, config_flags,
                               positional_args)) {
      return std::nullopt;
    }

    apply_flags_to_command(flags, cmd);
    apply_config_flags_to_command(config_flags, config_command, cmd);
    return cmd;
  }

  if (!validate_flag_scope(cmd.name, flags)) {
    return std::nullopt;
  }

  apply_flags_to_command(flags, cmd);

  if (cmd.name != "knowledge" && has_knowledge_only_flags(knowledge_flags)) {
    return std::nullopt;
  }

  if (cmd.name == "ping" || cmd.name == "readiness") {
    return cmd;
  }

  if (cmd.name == "help") {
    cmd.help_path = build_help_path(positional_args, true);
    return cmd;
  }

  if (cmd.name == "version") {
    if (positional_args.size() != 1) {
      return std::nullopt;
    }
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

  if (cmd.name == "knowledge") {
    if (positional_args.size() < 2 || positional_args[1].empty()) {
      return std::nullopt;
    }

    cmd.knowledge_command = parse_knowledge_command_kind(positional_args[1]);
    switch (cmd.knowledge_command) {
      case CliKnowledgeCommandKind::Health:
        if (positional_args.size() != 2) {
          return std::nullopt;
        }
        if (has_knowledge_only_flags(knowledge_flags)) {
          return std::nullopt;
        }
        return cmd;
      case CliKnowledgeCommandKind::Refresh:
        if (positional_args.size() != 2) {
          return std::nullopt;
        }
        if (has_knowledge_retrieve_flags(knowledge_flags)) {
          return std::nullopt;
        }
        cmd.knowledge_refresh_changed_sources =
            std::move(knowledge_flags.changed_sources);
        return cmd;
      case CliKnowledgeCommandKind::Retrieve:
        if (!knowledge_flags.changed_sources.empty()) {
          return std::nullopt;
        }
        cmd.knowledge_query_text = join_positional_tail(positional_args, 2U);
        if (!cmd.knowledge_query_text.has_value() || cmd.knowledge_query_text->empty()) {
          return std::nullopt;
        }
        cmd.knowledge_preferred_mode = std::move(knowledge_flags.preferred_mode);
        cmd.knowledge_query_kind = std::move(knowledge_flags.query_kind);
        cmd.knowledge_allowed_corpora = std::move(knowledge_flags.allowed_corpora);
        cmd.knowledge_domain_tags = std::move(knowledge_flags.domain_tags);
        cmd.knowledge_required_tags = std::move(knowledge_flags.required_tags);
        cmd.knowledge_required_language = std::move(knowledge_flags.required_language);
        return cmd;
      case CliKnowledgeCommandKind::None:
        return std::nullopt;
    }
  }

  // 未知命令
  return std::nullopt;
}

std::string CliCommandParser::usage_string(std::string_view command_name,
                                           std::string_view subcommand_name) {
  if (command_name == "run") {
    return "Usage: dasall-cli run <request_json_or_-> [--async] [--request-id <id>] "
           "[--session <hint>] [--trace-id <id>] [--timeout-ms <ms>] [--json] "
           "[--socket-path <path>] [--quiet] [--no-input]\n";
  }

  if (command_name == "status") {
    return "Usage: dasall-cli status (--receipt <receipt_ref> --ownership-token <token> | "
           "--request-id <request_id>) [--timeout-ms <ms>] [--json] "
           "[--socket-path <path>] [--quiet]\n";
  }

  if (command_name == "cancel") {
    return "Usage: dasall-cli cancel (--receipt <receipt_ref> --ownership-token <token> | "
           "--request-id <request_id>) [--timeout-ms <ms>] [--json] "
           "[--socket-path <path>] [--quiet]\n";
  }

  if (command_name == "version") {
    return "Usage: dasall-cli version [--json] [--quiet]\n";
  }

  if (command_name == "config") {
    if (subcommand_name == "show") {
      return "Usage: dasall-cli config show [--json]\n";
    }

    if (subcommand_name == "plan") {
      return "Usage: dasall-cli config plan [--from-file <path>] [--dry-run] [--json]\n";
    }

    if (subcommand_name == "validate") {
      return "Usage: dasall-cli config validate [--json]\n";
    }

    if (subcommand_name == "apply") {
      return "Usage: dasall-cli config apply --from-file <path> --no-input [--json]\n";
    }

    return "Usage:\n"
           "  dasall-cli config\n"
           "  dasall-cli config show [--json]\n"
           "  dasall-cli config plan [--from-file <path>] [--dry-run] [--json]\n"
           "  dasall-cli config validate [--json]\n"
           "  dasall-cli config apply --from-file <path> --no-input [--json]\n";
  }

  if (command_name == "ping") {
    return "Usage: dasall-cli ping [--json] [--timeout-ms <ms>] [--socket-path <path>] [--quiet]\n";
  }

  if (command_name == "readiness") {
    return "Usage: dasall-cli readiness [--json] [--timeout-ms <ms>] [--socket-path <path>] [--quiet]\n";
  }

  if (command_name == "diag") {
    if (subcommand_name == "health") {
      return "Usage: dasall-cli diag health [--json] [--timeout-ms <ms>] "
             "[--socket-path <path>] [--quiet]\n";
    }

    if (subcommand_name == "queue") {
      return "Usage: dasall-cli diag queue [--json] [--timeout-ms <ms>] "
             "[--socket-path <path>] [--quiet]\n";
    }

    if (subcommand_name == "threads") {
      return "Usage: dasall-cli diag threads [--json] [--timeout-ms <ms>] "
             "[--socket-path <path>] [--quiet]\n";
    }

    return "Usage: dasall-cli diag <health|queue|threads> [--json] [--timeout-ms <ms>] "
           "[--socket-path <path>] [--quiet]\n";
  }

  if (command_name == "knowledge") {
    if (subcommand_name == "health") {
      return "Usage: dasall-cli knowledge health [--json] [--timeout-ms <ms>] "
             "[--socket-path <path>] [--quiet]\n";
    }

    if (subcommand_name == "refresh") {
          return "Usage: dasall-cli knowledge refresh [--changed-source <path>]... [--json] "
            "[--timeout-ms <ms>] [--socket-path <path>] [--quiet]\n";
    }

    if (subcommand_name == "retrieve") {
          return "Usage: dasall-cli knowledge retrieve <query_text> [--preferred-mode <lexical-only|dense-only|hybrid>] "
            "[--query-kind <fact-lookup|procedure-lookup|diagnostic-context|policy-evidence|multi-hop>] "
            "[--allowed-corpus <id>]... [--domain-tag <tag>]... [--required-tag <tag>]... "
            "[--required-language <lang>] [--json] "
             "[--timeout-ms <ms>] [--socket-path <path>] [--quiet]\n";
    }

    return "Usage: dasall-cli knowledge <health|refresh|retrieve> [query_text] "
          "[--preferred-mode <mode>] [--query-kind <kind>] [--allowed-corpus <id>]... "
          "[--domain-tag <tag>]... [--required-tag <tag>]... [--required-language <lang>] "
          "[--json] [--timeout-ms <ms>] [--socket-path <path>] [--quiet]\n";
  }

  return "Usage:\n"
         "  dasall-cli help [command] [subcommand]\n"
         "  dasall-cli version [--json] [--quiet]\n"
      "  dasall-cli config\n"
      "  dasall-cli config show [--json]\n"
      "  dasall-cli config plan [--from-file <path>] [--dry-run] [--json]\n"
      "  dasall-cli config validate [--json]\n"
      "  dasall-cli config apply --from-file <path> --no-input [--json]\n"
         "  dasall-cli ping [--json] [--timeout-ms <ms>] [--socket-path <path>] [--quiet]\n"
         "  dasall-cli readiness [--json] [--timeout-ms <ms>] [--socket-path <path>] [--quiet]\n"
         "  dasall-cli knowledge <health|refresh|retrieve> [query_text] [--changed-source <path>]... [--preferred-mode <mode>] [--query-kind <kind>] [--allowed-corpus <id>]... [--domain-tag <tag>]... [--required-tag <tag>]... [--required-language <lang>] [--json] [--timeout-ms <ms>] "
         "[--socket-path <path>] [--quiet]\n"
         "  dasall-cli run <request_json_or_-> [--async] [--request-id <id>] [--session <hint>] "
         "[--trace-id <id>] [--timeout-ms <ms>] [--json] [--socket-path <path>] [--quiet] [--no-input]\n"
         "  dasall-cli status (--receipt <receipt_ref> --ownership-token <token> | --request-id <request_id>) "
         "[--timeout-ms <ms>] [--json] [--socket-path <path>] [--quiet]\n"
         "  dasall-cli cancel (--receipt <receipt_ref> --ownership-token <token> | --request-id <request_id>) "
         "[--timeout-ms <ms>] [--json] [--socket-path <path>] [--quiet]\n"
         "  dasall-cli diag <health|queue|threads> [--json] [--timeout-ms <ms>] [--socket-path <path>] [--quiet]\n";
}

}  // namespace dasall::apps::cli
