#include <iostream>
#include <optional>
#include <string>

#include "MemoryInstalledProofRunner.h"

namespace {

[[nodiscard]] std::string escape_json_string(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char current : value) {
    switch (current) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(current);
        break;
    }
  }
  return escaped;
}

void print_json(const dasall::apps::daemon::MemoryInstalledProofResult& result) {
  std::cout << "{\n"
            << "  \"ok\": " << (result.ok() ? "true" : "false") << ",\n"
            << "  \"error\": \"" << escape_json_string(result.error) << "\",\n"
            << "  \"effective_profile_id\": \""
            << escape_json_string(result.effective_profile_id) << "\",\n"
            << "  \"database_path\": \""
            << escape_json_string(result.database_path.string()) << "\",\n"
            << "  \"session_id\": \"" << escape_json_string(result.session_id)
            << "\",\n"
            << "  \"expected_marker\": \""
            << escape_json_string(result.expected_marker) << "\",\n"
            << "  \"first_turn_id\": \""
            << escape_json_string(result.first_turn_id) << "\",\n"
            << "  \"second_turn_id\": \""
            << escape_json_string(result.second_turn_id) << "\",\n"
            << "  \"journal_mode\": \""
            << escape_json_string(result.journal_mode) << "\",\n"
            << "  \"core_table_count\": " << result.core_table_count << ",\n"
            << "  \"vector_table_count\": " << result.vector_table_count << ",\n"
            << "  \"session_summary_count_after_first\": "
            << result.session_summary_count_after_first << ",\n"
            << "  \"session_turn_count_after_second\": "
            << result.session_turn_count_after_second << ",\n"
            << "  \"session_summary_count_after_second\": "
            << result.session_summary_count_after_second << ",\n"
            << "  \"latest_summary_source_turn_ids_json\": \""
            << escape_json_string(result.latest_summary_source_turn_ids_json) << "\",\n"
            << "  \"latest_summary_text_prefix\": \""
            << escape_json_string(result.latest_summary_text_prefix) << "\",\n"
            << "  \"prepare_context_marker_visible\": "
            << (result.prepare_context_marker_visible ? "true" : "false") << ",\n"
            << "  \"latest_summary_references_second_turn\": "
            << (result.latest_summary_references_second_turn ? "true" : "false") << "\n"
            << "}\n";
}

[[nodiscard]] bool parse_args(int argc,
                              char* argv[],
                              dasall::apps::daemon::MemoryInstalledProofOptions& options,
                              bool& json_output,
                              std::string& error) {
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--json") {
      json_output = true;
      continue;
    }

    if (argument == "--profile-id") {
      if (index + 1 >= argc) {
        error = "--profile-id requires a value";
        return false;
      }
      options.requested_profile_id = argv[++index];
      continue;
    }

    if (argument == "--config-file") {
      if (index + 1 >= argc) {
        error = "--config-file requires a value";
        return false;
      }
      options.deployment_config_path = argv[++index];
      continue;
    }

    if (argument == "--state-root") {
      if (index + 1 >= argc) {
        error = "--state-root requires a value";
        return false;
      }
      options.state_root_override = argv[++index];
      continue;
    }

    error = std::string("unknown argument: ") + argument;
    return false;
  }

  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  dasall::apps::daemon::MemoryInstalledProofOptions options;
  bool json_output = false;
  std::string parse_error;
  if (!parse_args(argc, argv, options, json_output, parse_error)) {
    const dasall::apps::daemon::MemoryInstalledProofResult result{
        .error = parse_error,
    };
    if (json_output) {
      print_json(result);
    } else {
      std::cerr << parse_error << '\n';
    }
    return 1;
  }

  const auto result = dasall::apps::daemon::collect_memory_installed_proof(options);
  if (json_output) {
    print_json(result);
  } else if (result.ok()) {
    std::cout << "memory installed proof completed for session "
              << result.session_id << "\n";
  } else {
    std::cerr << result.error << '\n';
  }

  return result.ok() ? 0 : 1;
}