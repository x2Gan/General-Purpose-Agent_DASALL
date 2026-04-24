#include "RequestValidator.h"

#include <cctype>
#include <string>
#include <utility>

namespace dasall::access {

namespace {

[[nodiscard]] bool contains_newline_or_cr(const std::string& value) {
  return value.find('\n') != std::string::npos || value.find('\r') != std::string::npos;
}

[[nodiscard]] bool is_header_key_char(const char ch) {
  const unsigned char raw = static_cast<unsigned char>(ch);
  return std::isalnum(raw) != 0 || ch == '-' || ch == '_' || ch == '.';
}

}  // namespace

RequestValidator::RequestValidator(AccessPublishView publish_view,
                                   std::vector<std::string> allowed_protocols)
    : publish_view_(std::move(publish_view)),
      allowed_protocols_(std::move(allowed_protocols)) {}

RequestValidationResult RequestValidator::validate_packet(
    const RuntimeDispatchRequest& request) const {
  RequestValidationResult result;

  if (request.packet.packet_id.empty() ||
      request.packet.entry_type.empty() ||
      request.packet.protocol_kind.empty()) {
    result.error = make_access_error(
        AccessErrorCode::ValidationRejected,
        "packet_id/entry_type/protocol_kind must be non-empty",
        std::nullopt,
        "missing_required_packet_metadata");
    return result;
  }

  if (request.subject_identity.actor_ref.empty()) {
    result.error = make_access_error(
        AccessErrorCode::ValidationRejected,
        "subject actor_ref must be non-empty",
        std::nullopt,
        "missing_subject_actor_ref");
    return result;
  }

  if (!is_allowed_protocol(request.packet.protocol_kind)) {
    result.error = make_access_error(
        AccessErrorCode::UnsupportedProtocol,
        "protocol_kind is not in access allowed protocol set",
        std::nullopt,
        "unsupported_protocol_kind");
    return result;
  }

  if (!validate_payload_limits(request, &result)) {
    return result;
  }

  if (!validate_headers(request, &result)) {
    return result;
  }

  result.accepted = true;
  return result;
}

bool RequestValidator::validate_payload_limits(
    const RuntimeDispatchRequest& request,
    RequestValidationResult* result) const {
  if (publish_view_.max_payload_bytes > 0 &&
      request.packet.payload.size() > static_cast<std::size_t>(publish_view_.max_payload_bytes)) {
    result->error = make_access_error(
        AccessErrorCode::PayloadTooLarge,
        "payload exceeds max_payload_bytes",
        std::nullopt,
        "payload_size_limit_exceeded");
    return false;
  }

  const auto user_input_it = request.request_context.find("user_input");
  if (publish_view_.max_user_input_bytes > 0 && user_input_it != request.request_context.end() &&
      user_input_it->second.size() >
          static_cast<std::size_t>(publish_view_.max_user_input_bytes)) {
    result->error = make_access_error(
        AccessErrorCode::PayloadTooLarge,
        "user_input exceeds max_user_input_bytes",
        std::nullopt,
        "user_input_size_limit_exceeded");
    return false;
  }

  return true;
}

bool RequestValidator::validate_headers(const RuntimeDispatchRequest& request,
                                        RequestValidationResult* result) const {
  for (const auto& [key, value] : request.request_context) {
    if (key.empty()) {
      result->error = make_access_error(
          AccessErrorCode::MalformedInput,
          "request_context key must be non-empty",
          std::nullopt,
          "malformed_request_context_key");
      return false;
    }

    if (key.size() > 128 || value.size() > 4096) {
      result->error = make_access_error(
          AccessErrorCode::MalformedInput,
          "request_context key/value exceeds max length",
          std::nullopt,
          "request_context_too_large");
      return false;
    }

    if (contains_newline_or_cr(key) || contains_newline_or_cr(value)) {
      result->error = make_access_error(
          AccessErrorCode::MalformedInput,
          "request_context key/value contains CRLF characters",
          std::nullopt,
          "request_context_header_injection_detected");
      return false;
    }

    for (const char ch : key) {
      if (!is_header_key_char(ch)) {
        result->error = make_access_error(
            AccessErrorCode::MalformedInput,
            "request_context key contains unsupported characters",
            std::nullopt,
            "request_context_key_charset_invalid");
        return false;
      }
    }
  }

  return true;
}

bool RequestValidator::is_allowed_protocol(const std::string_view protocol_kind) const {
  if (protocol_kind.empty()) {
    return false;
  }

  if (allowed_protocols_.empty()) {
    return true;
  }

  for (const auto& allowed : allowed_protocols_) {
    if (allowed == protocol_kind) {
      return true;
    }
  }

  return false;
}

}  // namespace dasall::access
