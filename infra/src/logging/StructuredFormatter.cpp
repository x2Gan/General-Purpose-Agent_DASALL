#include "logging/StructuredFormatter.h"

#include <string>
#include <string_view>

namespace dasall::infra::logging {

namespace {

[[nodiscard]] std::string level_name(LogLevel level) {
  switch (level) {
    case LogLevel::Unspecified:
      return "unspecified";
    case LogLevel::Trace:
      return "trace";
    case LogLevel::Debug:
      return "debug";
    case LogLevel::Info:
      return "info";
    case LogLevel::Warn:
      return "warn";
    case LogLevel::Error:
      return "error";
    case LogLevel::Fatal:
      return "fatal";
  }

  return "unknown";
}

[[nodiscard]] std::string escape_json(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());

  for (const auto ch : value) {
    switch (ch) {
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
        escaped.push_back(ch);
        break;
    }
  }

  return escaped;
}

[[nodiscard]] std::string attr_or_unknown(const LogEvent& event,
                                          std::string_view key) {
  const auto it = event.attrs.find(std::string(key));
  if (it == event.attrs.end() || it->second.empty()) {
    return std::string(LogContext::kUnknownIdentifier);
  }

  return it->second;
}

[[nodiscard]] std::string resolve_correlation_id(const LogEvent& event) {
  for (const auto key : {std::string_view("trace_id"),
                         std::string_view("request_id"),
                         std::string_view("session_id"),
                         std::string_view("task_id")}) {
    const auto value = attr_or_unknown(event, key);
    if (value != LogContext::kUnknownIdentifier) {
      return value;
    }
  }

  return std::string(LogContext::kUnknownIdentifier);
}

[[nodiscard]] std::string build_idempotency_key(const LogEvent& event,
                                                std::string_view correlation_id) {
  return std::string(correlation_id) + "|" + attr_or_unknown(event, "task_id") +
         "|" + event.module + "|" +
         (event.ts.has_value() ? std::to_string(*event.ts)
                               : std::string(LogContext::kUnknownIdentifier));
}

[[nodiscard]] std::string build_attrs_json(const LogEvent::AttributeMap& attrs) {
  std::string json = "{";
  bool first = true;

  for (const auto& [key, value] : attrs) {
    if (!first) {
      json += ",";
    }
    first = false;
    json += "\"" + escape_json(key) + "\":\"" + escape_json(value) + "\"";
  }

  json += "}";
  return json;
}

[[nodiscard]] std::string build_structured_message(const LogEvent& event,
                                                   std::string_view rendered_message) {
  std::string json = "{";
  json += "\"schema_version\":\"" +
          escape_json(std::string(StructuredFormatter::kSchemaVersion)) + "\",";
  json += "\"level\":\"" + escape_json(level_name(event.level)) + "\",";
  json += "\"module\":\"" + escape_json(event.module) + "\",";
  json += "\"message\":\"" + escape_json(rendered_message) + "\",";
  json += "\"ts_ms\":";
  if (event.ts.has_value()) {
    json += std::to_string(*event.ts);
  } else {
    json += "null";
  }
  json += ",\"attrs\":" + build_attrs_json(event.attrs);
  json += "}";
  return json;
}

}  // namespace

LogEvent StructuredFormatter::format(const LogEvent& event) const {
  auto formatted = event;
  const auto correlation_id = resolve_correlation_id(formatted);

  formatted.attrs.insert_or_assign("schema_version",
                                   std::string(StructuredFormatter::kSchemaVersion));
  formatted.attrs.insert_or_assign("correlation_id", correlation_id);
  formatted.attrs.insert_or_assign("idempotency_key",
                                   build_idempotency_key(formatted, correlation_id));

  const auto rendered_message = formatted.message;
  formatted.message = build_structured_message(formatted, rendered_message);
  return formatted;
}

}  // namespace dasall::infra::logging