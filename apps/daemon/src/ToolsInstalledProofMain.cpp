#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "ToolsInstalledProofRunner.h"

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

[[nodiscard]] std::string encode_string_array_json(
    const std::vector<std::string>& values) {
  std::string encoded{"["};
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0U) {
      encoded += ", ";
    }
    encoded += '"';
    encoded += escape_json_string(values[index]);
    encoded += '"';
  }
  encoded += "]";
  return encoded;
}

void print_json(const dasall::apps::daemon::ToolsInstalledProofResult& result) {
  std::cout << "{\n"
            << "  \"ok\": " << (result.ok() ? "true" : "false") << ",\n"
            << "  \"error\": \"" << escape_json_string(result.error) << "\",\n"
            << "  \"effective_profile_id\": \""
            << escape_json_string(result.effective_profile_id) << "\",\n"
            << "  \"route_kind\": \"" << escape_json_string(result.route_kind)
            << "\",\n"
            << "  \"payload\": \"" << escape_json_string(result.payload) << "\",\n"
            << "  \"terminal_route_kind\": \""
            << escape_json_string(result.terminal_route_kind) << "\",\n"
            << "  \"terminal_payload\": \""
            << escape_json_string(result.terminal_payload) << "\",\n"
            << "  \"observation_id\": \""
            << escape_json_string(result.observation_id) << "\",\n"
            << "  \"observation_digest_summary\": \""
            << escape_json_string(result.observation_digest_summary) << "\",\n"
            << "  \"visible_tools\": "
            << encode_string_array_json(result.visible_tools) << ",\n"
            << "  \"external_evidence\": "
            << encode_string_array_json(result.external_evidence) << ",\n"
            << "  \"agent_dataset_visible\": "
            << (result.agent_dataset_visible ? "true" : "false") << ",\n"
            << "  \"agent_terminal_visible\": "
            << (result.agent_terminal_visible ? "true" : "false") << ",\n"
            << "  \"tool_invocation_succeeded\": "
            << (result.tool_invocation_succeeded ? "true" : "false") << ",\n"
            << "  \"terminal_confirmation_denied\": "
            << (result.terminal_confirmation_denied ? "true" : "false") << ",\n"
            << "  \"terminal_invocation_succeeded\": "
            << (result.terminal_invocation_succeeded ? "true" : "false") << ",\n"
            << "  \"projection_present\": "
            << (result.projection_present ? "true" : "false") << ",\n"
            << "  \"terminal_projection_present\": "
            << (result.terminal_projection_present ? "true" : "false") << ",\n"
            << "  \"route_citation_present\": "
            << (result.route_citation_present ? "true" : "false") << ",\n"
            << "  \"tool_call_citation_present\": "
            << (result.tool_call_citation_present ? "true" : "false") << ",\n"
            << "  \"production_bridge_evidence_present\": "
            << (result.production_bridge_evidence_present ? "true" : "false") << ",\n"
            << "  \"production_observability_evidence_present\": "
            << (result.production_observability_evidence_present ? "true" : "false") << ",\n"
            << "  \"failure_reason_code\": \""
            << escape_json_string(result.failure_reason_code) << "\",\n"
            << "  \"terminal_failure_reason_code\": \""
            << escape_json_string(result.terminal_failure_reason_code) << "\"\n"
            << "}\n";
}

[[nodiscard]] bool parse_args(int argc,
                              char* argv[],
                              dasall::apps::daemon::ToolsInstalledProofOptions& options,
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
  dasall::apps::daemon::ToolsInstalledProofOptions options;
  bool json_output = false;
  std::string parse_error;
  if (!parse_args(argc, argv, options, json_output, parse_error)) {
    dasall::apps::daemon::ToolsInstalledProofResult result;
    result.error = parse_error;
    if (json_output) {
      print_json(result);
    } else {
      std::cerr << parse_error << '\n';
    }
    return 1;
  }

  const auto result = dasall::apps::daemon::collect_tools_installed_proof(options);
  if (json_output) {
    print_json(result);
  } else if (result.ok()) {
    std::cout << "tools installed proof completed for profile "
              << result.effective_profile_id << '\n';
  } else {
    std::cerr << result.error << '\n';
  }

  return result.ok() ? 0 : 1;
}