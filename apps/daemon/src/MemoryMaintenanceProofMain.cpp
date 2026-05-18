#include <iostream>
#include <optional>
#include <string>

#include "MemoryMaintenanceProofRunner.h"

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

void print_json(const dasall::apps::daemon::MemoryMaintenanceProofResult& result) {
  std::cout << "{\n"
            << "  \"ok\": " << (result.ok() ? "true" : "false") << ",\n"
            << "  \"error\": \"" << escape_json_string(result.error) << "\",\n"
            << "  \"effective_profile_id\": \""
            << escape_json_string(result.effective_profile_id) << "\",\n"
            << "  \"database_path\": \""
            << escape_json_string(result.database_path.string()) << "\",\n"
            << "  \"session_id\": \"" << escape_json_string(result.session_id)
            << "\",\n"
            << "  \"protected_turn_id\": \""
            << escape_json_string(result.protected_turn_id) << "\",\n"
            << "  \"purged_turn_id\": \""
            << escape_json_string(result.purged_turn_id) << "\",\n"
            << "  \"newest_turn_id\": \""
            << escape_json_string(result.newest_turn_id) << "\",\n"
            << "  \"quarantine_object_id\": \""
            << escape_json_string(result.quarantine_object_id) << "\",\n"
            << "  \"journal_mode\": \""
            << escape_json_string(result.journal_mode) << "\",\n"
            << "  \"retention_turns\": " << result.retention_turns << ",\n"
            << "  \"turns_before\": " << result.turns_before << ",\n"
            << "  \"turns_after\": " << result.turns_after << ",\n"
            << "  \"quarantine_rows_before\": " << result.quarantine_rows_before << ",\n"
            << "  \"quarantine_rows_after\": " << result.quarantine_rows_after << ",\n"
            << "  \"protected_turn_retained\": "
            << (result.protected_turn_retained ? "true" : "false") << ",\n"
            << "  \"purged_turn_removed\": "
            << (result.purged_turn_removed ? "true" : "false") << ",\n"
            << "  \"newest_turn_retained\": "
            << (result.newest_turn_retained ? "true" : "false") << ",\n"
            << "  \"wal_bytes_before\": " << result.wal_bytes_before << ",\n"
            << "  \"maintenance_report\": {\n"
            << "    \"checkpoint_executed\": "
            << (result.maintenance_report.checkpoint_executed ? "true" : "false") << ",\n"
            << "    \"checkpoint_wal_pages_remaining\": "
            << result.maintenance_report.checkpoint_wal_pages_remaining << ",\n"
            << "    \"turns_purged\": " << result.maintenance_report.turns_purged << ",\n"
            << "    \"facts_purged\": " << result.maintenance_report.facts_purged << ",\n"
            << "    \"experiences_purged\": "
            << result.maintenance_report.experiences_purged << ",\n"
            << "    \"quarantine_cleaned\": "
            << result.maintenance_report.quarantine_cleaned << ",\n"
            << "    \"vector_rebuild_executed\": "
            << (result.maintenance_report.vector_rebuild_executed ? "true" : "false")
            << ",\n"
            << "    \"duration_ms\": " << result.maintenance_report.duration_ms << ",\n"
            << "    \"warnings\": [";

  for (std::size_t index = 0; index < result.maintenance_report.warnings.size(); ++index) {
    if (index != 0U) {
      std::cout << ", ";
    }
    std::cout << '"' << escape_json_string(result.maintenance_report.warnings[index]) << '"';
  }

  std::cout << "]\n"
            << "  }\n"
            << "}\n";
}

[[nodiscard]] bool parse_args(int argc,
                              char* argv[],
                              dasall::apps::daemon::MemoryMaintenanceProofOptions& options,
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
  dasall::apps::daemon::MemoryMaintenanceProofOptions options;
  bool json_output = false;
  std::string parse_error;
  if (!parse_args(argc, argv, options, json_output, parse_error)) {
    const dasall::apps::daemon::MemoryMaintenanceProofResult result{
        .error = parse_error,
    };
    if (json_output) {
      print_json(result);
    } else {
      std::cerr << parse_error << '\n';
    }
    return 1;
  }

  const auto result = dasall::apps::daemon::collect_memory_maintenance_proof(options);
  if (json_output) {
    print_json(result);
  } else if (result.ok()) {
    std::cout << "maintenance proof completed for session " << result.session_id << "\n";
  } else {
    std::cerr << result.error << '\n';
  }

  return result.ok() ? 0 : 1;
}