#include "config/ConfigSummaryFormatter.h"

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

bool ConfigSecretSummaryEntry::is_well_formed() const {
  return !ref.empty() && !status.empty();
}

bool ConfigSummaryView::is_well_formed() const {
  if (schema_version.empty() || profile_id.empty() || socket_path.empty()) {
    return false;
  }

  for (const auto& secret_ref : secret_refs) {
    if (!secret_ref.is_well_formed()) {
      return false;
    }
  }

  return true;
}

std::string ConfigSummaryFormatter::format_human(const ConfigSummaryView& summary) {
  std::string output = "[dasall-config] summary\n";
  output += "profile: ";
  output += summary.profile_id;
  output += "\ndaemon.socket_path: ";
  output += summary.socket_path;
  output += "\ndaemon.log_format: ";
  output += summary.log_format;
  output += "\nservice: installed=";
  output += bool_text(summary.service_installed);
  output += ", running=";
  output += bool_text(summary.service_running);
  output += ", enabled=";
  output += bool_text(summary.service_enabled);
  output += "\nping: ";
  output += summary.ping_status.empty() ? "unknown" : summary.ping_status;
  output += "\nreadiness: ";
  output +=
      summary.readiness_status.empty() ? "unknown" : summary.readiness_status;
  output += "\nsecret_refs:\n";
  if (summary.secret_refs.empty()) {
    output += "- (none)\n";
  } else {
    for (const auto& secret_ref : summary.secret_refs) {
      output += "- ";
      output += secret_ref.ref;
      output += " (";
      output += secret_ref.status;
      output += ")\n";
    }
  }
  output += "apply_outcome: ";
  output += summary.apply_result.outcome;
  output += "\ncompleted_actions:\n";
  if (summary.apply_result.completed_actions.empty()) {
    output += "- (none)\n";
  } else {
    for (const auto& action : summary.apply_result.completed_actions) {
      output += "- ";
      output += action;
      output += "\n";
    }
  }
  output += "\noperator_access: ";
  output += summary.operator_access_hint.empty() ? "(none)"
                                                 : summary.operator_access_hint;
  output += '\n';
  append_string_list(output, "incomplete_items", summary.incomplete_items);
  append_string_list(output, "next_steps", summary.next_steps);
  append_string_list(output, "manual_followups",
                     summary.apply_result.manual_followups);
  append_string_list(output, "blocked_actions",
                     summary.apply_result.blocked_actions);
  return output;
}

std::string ConfigSummaryFormatter::format_json(const ConfigSummaryView& summary) {
  std::string output = "{";
  output += "\"schema_version\":" + json_string(summary.schema_version);
  output += ",\"profile_id\":" + json_string(summary.profile_id);
  output += ",\"daemon\":{";
  output += "\"socket_path\":" + json_string(summary.socket_path);
  output += ",\"log_format\":" + json_string(summary.log_format);
  output += "}";
  output += ",\"service\":{";
  output += "\"installed\":" + bool_text(summary.service_installed);
  output += ",\"running\":" + bool_text(summary.service_running);
  output += ",\"enabled\":" + bool_text(summary.service_enabled);
  output += "}";
  output += ",\"connectivity\":{";
  output += "\"ping_status\":" + json_string(summary.ping_status);
  output += ",\"readiness_status\":" +
            json_string(summary.readiness_status);
  output += "}";
  output += ",\"secret_refs\":[";
  for (std::size_t index = 0; index < summary.secret_refs.size(); ++index) {
    if (index != 0) {
      output += ',';
    }
    const auto& secret_ref = summary.secret_refs[index];
    output += "{";
    output += "\"ref\":" + json_string(secret_ref.ref);
    output += ",\"status\":" + json_string(secret_ref.status);
    output += "}";
  }
  output += ']';
  output += ",\"operator_access_hint\":" +
            json_string(summary.operator_access_hint);
  output += ",\"incomplete_items\":" +
            json_string_array(summary.incomplete_items);
  output += ",\"next_steps\":" + json_string_array(summary.next_steps);
  output += ",\"apply_result\":{";
  output += "\"outcome\":" + json_string(summary.apply_result.outcome);
  output += ",\"state_before\":" +
            json_string(to_string(summary.apply_result.state_before));
  output += ",\"state_after\":" +
            json_string(to_string(summary.apply_result.state_after));
  output += ",\"applied\":" + bool_text(summary.apply_result.applied);
  output += ",\"rollback_performed\":" +
            bool_text(summary.apply_result.rollback_performed);
  output += ",\"written_files\":" +
            json_string_array(summary.apply_result.written_files);
  output += ",\"written_secret_refs\":" +
            json_string_array(summary.apply_result.written_secret_refs);
  output += ",\"completed_actions\":" +
            json_string_array(summary.apply_result.completed_actions);
  output += ",\"manual_followups\":" +
            json_string_array(summary.apply_result.manual_followups);
  output += ",\"blocked_actions\":" +
            json_string_array(summary.apply_result.blocked_actions);
  output += "}";
  output += '}';
  return output;
}

}  // namespace dasall::apps::cli::config