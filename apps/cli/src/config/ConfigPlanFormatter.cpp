#include "config/ConfigPlanFormatter.h"

#include <string_view>

namespace dasall::apps::cli::config {

namespace {

[[nodiscard]] std::string bool_text(const bool value) {
  return value ? "true" : "false";
}

[[nodiscard]] std::string escape_json_string(std::string_view input) {
  std::string output;
  output.reserve(input.size());
  for (const unsigned char current : input) {
    switch (current) {
      case '\\':
        output += "\\\\";
        break;
      case '"':
        output += "\\\"";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        output.push_back(static_cast<char>(current));
        break;
    }
  }
  return output;
}

[[nodiscard]] std::string json_string(std::string_view input) {
  return std::string("\"") + escape_json_string(input) + "\"";
}

[[nodiscard]] std::string json_string_array(
    const std::vector<std::string>& values) {
  std::string output = "[";
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) {
      output += ',';
    }
    output += json_string(values[index]);
  }
  output += ']';
  return output;
}

[[nodiscard]] std::vector<std::string> collect_service_actions(
    const ConfigActionPlan& plan) {
  std::vector<std::string> actions;
  if (plan.service_validate_requested) {
    actions.emplace_back("validate-only");
  }
  if (plan.service_reload_required) {
    actions.emplace_back("reload");
  }
  if (plan.service_restart_required) {
    actions.emplace_back("restart");
  }
  if (plan.service_start_requested) {
    actions.emplace_back("start");
  }
  if (plan.service_enable_requested) {
    actions.emplace_back("enable");
  }
  return actions;
}

[[nodiscard]] std::string join_text(const std::vector<std::string>& values) {
  if (values.empty()) {
    return "(none)";
  }

  std::string output;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0) {
      output += ", ";
    }
    output += values[index];
  }
  return output;
}

void append_string_list(std::string& output,
                        std::string_view title,
                        const std::vector<std::string>& values) {
  output += std::string(title);
  output += ":\n";
  if (values.empty()) {
    output += "- (none)\n";
    return;
  }

  for (const auto& value : values) {
    output += "- ";
    output += value;
    output += '\n';
  }
}

}  // namespace

std::string ConfigPlanFormatter::format_human(const ConfigActionPlan& plan) {
  std::string output = "[dasall-config] plan\n";
  output += "state_before: ";
  output += std::string(to_string(plan.state_before));
  output += "\nstate_after_expected: ";
  output += std::string(to_string(plan.state_after_expected));
  output += "\nservice_actions: ";
  output += join_text(collect_service_actions(plan));
  output += '\n';

  output += "file_writes:\n";
  if (plan.file_writes.empty()) {
    output += "- (none)\n";
  } else {
    for (const auto& file_write : plan.file_writes) {
      output += "- ";
      output += file_write.operation;
      output += ' ';
      output += file_write.path;
      output += " [requires_root=";
      output += bool_text(file_write.requires_root);
      output += ", restart_required=";
      output += bool_text(file_write.restart_required);
      output += ", changed_keys=";
      output += join_text(file_write.changed_keys);
      output += "]\n";
    }
  }

  output += "secret_writes:\n";
  if (plan.secret_writes.empty()) {
    output += "- (none)\n";
  } else {
    for (const auto& secret_write : plan.secret_writes) {
      output += "- ";
      output += secret_write.operation;
      output += ' ';
      output += secret_write.ref;
      output += " [runtime_verification=";
      output += secret_write.runtime_verification;
      output += "]\n";
    }
  }

  append_string_list(output, "manual_followups", plan.manual_followups);
  append_string_list(output, "blocked_actions", plan.blocked_actions);
  return output;
}

std::string ConfigPlanFormatter::format_json(const ConfigActionPlan& plan) {
  std::string output = "{";
  output += "\"schema_version\":" + json_string(plan.schema_version);
  output += ",\"state_before\":" +
            json_string(to_string(plan.state_before));
  output += ",\"state_after_expected\":" +
            json_string(to_string(plan.state_after_expected));
  output += ",\"file_writes\":[";
  for (std::size_t index = 0; index < plan.file_writes.size(); ++index) {
    if (index != 0) {
      output += ',';
    }
    const auto& file_write = plan.file_writes[index];
    output += "{";
    output += "\"path\":" + json_string(file_write.path);
    output += ",\"operation\":" + json_string(file_write.operation);
    output += ",\"requires_root\":" + bool_text(file_write.requires_root);
    output += ",\"restart_required\":" +
              bool_text(file_write.restart_required);
    output += ",\"changed_keys\":" +
              json_string_array(file_write.changed_keys);
    output += "}";
  }
  output += ']';
  output += ",\"secret_writes\":[";
  for (std::size_t index = 0; index < plan.secret_writes.size(); ++index) {
    if (index != 0) {
      output += ',';
    }
    const auto& secret_write = plan.secret_writes[index];
    output += "{";
    output += "\"ref\":" + json_string(secret_write.ref);
    output += ",\"operation\":" + json_string(secret_write.operation);
    output += ",\"runtime_verification\":" +
              json_string(secret_write.runtime_verification);
    output += "}";
  }
  output += ']';
  output += ",\"service_validate_requested\":" +
            bool_text(plan.service_validate_requested);
  output += ",\"service_reload_required\":" +
            bool_text(plan.service_reload_required);
  output += ",\"service_restart_required\":" +
            bool_text(plan.service_restart_required);
  output += ",\"service_start_requested\":" +
            bool_text(plan.service_start_requested);
  output += ",\"service_enable_requested\":" +
            bool_text(plan.service_enable_requested);
  output += ",\"manual_followups\":" +
            json_string_array(plan.manual_followups);
  output += ",\"blocked_actions\":" +
            json_string_array(plan.blocked_actions);
  output += '}';
  return output;
}

}  // namespace dasall::apps::cli::config