#include <iostream>
#include <string>
#include <vector>

#include "RuntimeInstalledProofRunner.h"

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

void print_json(const dasall::apps::daemon::RuntimeInstalledProofResult& result) {
  std::cout << "{\n"
            << "  \"ok\": " << (result.ok() ? "true" : "false") << ",\n"
            << "  \"error\": \"" << escape_json_string(result.error) << "\",\n"
            << "  \"effective_profile_id\": \""
            << escape_json_string(result.effective_profile_id) << "\",\n"
            << "  \"visible_tools\": "
            << encode_string_array_json(result.visible_tools) << ",\n"
            << "  \"external_evidence\": "
            << encode_string_array_json(result.external_evidence) << ",\n"
            << "  \"agent_dataset_visible\": "
            << (result.agent_dataset_visible ? "true" : "false") << ",\n"
            << "  \"agent_terminal_visible\": "
            << (result.agent_terminal_visible ? "true" : "false") << ",\n"
            << "  \"tool_init_readiness\": \""
            << escape_json_string(result.tool_init_readiness) << "\",\n"
            << "  \"tool_status\": \""
            << escape_json_string(result.tool_status) << "\",\n"
            << "  \"tool_task_completed\": "
            << (result.tool_task_completed ? "true" : "false") << ",\n"
            << "  \"tool_runtime_path\": \""
            << escape_json_string(result.tool_runtime_path) << "\",\n"
            << "  \"tool_checkpoint_ref\": \""
            << escape_json_string(result.tool_checkpoint_ref) << "\",\n"
            << "  \"tool_response_text\": \""
            << escape_json_string(result.tool_response_text) << "\",\n"
            << "  \"recovery_init_readiness\": \""
            << escape_json_string(result.recovery_init_readiness) << "\",\n"
            << "  \"waiting_status\": \""
            << escape_json_string(result.waiting_status) << "\",\n"
            << "  \"waiting_checkpoint_ref\": \""
            << escape_json_string(result.waiting_checkpoint_ref) << "\",\n"
            << "  \"recovery_positive_status\": \""
            << escape_json_string(result.recovery_positive_status) << "\",\n"
            << "  \"recovery_positive_task_completed\": "
            << (result.recovery_positive_task_completed ? "true" : "false") << ",\n"
            << "  \"recovery_positive_runtime_path\": \""
            << escape_json_string(result.recovery_positive_runtime_path) << "\",\n"
            << "  \"recovery_positive_checkpoint_ref\": \""
            << escape_json_string(result.recovery_positive_checkpoint_ref) << "\",\n"
            << "  \"recovery_positive_checkpoint_persisted\": "
            << (result.recovery_positive_checkpoint_persisted ? "true" : "false") << ",\n"
            << "  \"recovery_positive_response_text\": \""
            << escape_json_string(result.recovery_positive_response_text) << "\",\n"
            << "  \"recovery_negative_init_readiness\": \""
            << escape_json_string(result.recovery_negative_init_readiness) << "\",\n"
            << "  \"recovery_negative_status\": \""
            << escape_json_string(result.recovery_negative_status) << "\",\n"
            << "  \"recovery_negative_task_completed\": "
            << (result.recovery_negative_task_completed ? "true" : "false") << ",\n"
            << "  \"recovery_negative_binding_rejected\": "
            << (result.recovery_negative_binding_rejected ? "true" : "false") << ",\n"
            << "  \"recovery_negative_response_text\": \""
            << escape_json_string(result.recovery_negative_response_text) << "\"\n"
            << "}\n";
}

[[nodiscard]] bool parse_args(int argc,
                              char* argv[],
                              dasall::apps::daemon::RuntimeInstalledProofOptions& options,
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
  dasall::apps::daemon::RuntimeInstalledProofOptions options;
  bool json_output = false;
  std::string parse_error;
  if (!parse_args(argc, argv, options, json_output, parse_error)) {
    dasall::apps::daemon::RuntimeInstalledProofResult result;
    result.error = parse_error;
    if (json_output) {
      print_json(result);
    } else {
      std::cerr << parse_error << '\n';
    }
    return 1;
  }

  const auto result = dasall::apps::daemon::collect_runtime_installed_proof(options);
  if (json_output) {
    print_json(result);
  } else if (result.ok()) {
    std::cout << "runtime installed proof completed for profile "
              << result.effective_profile_id << '\n';
  } else {
    std::cerr << result.error << '\n';
  }

  return result.ok() ? 0 : 1;
}