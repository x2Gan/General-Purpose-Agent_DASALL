#include "CliOutputFormatter.h"

#include <string>

namespace dasall::apps::cli {

std::string CliOutputFormatter::format_ping_success(
    std::string_view raw_response) {
  std::string out = "[dasall_cli] daemon ping: ok";
  if (!raw_response.empty()) {
    out += " (";
    out += raw_response;
    out += ')';
  }
  return out;
}

std::string CliOutputFormatter::format_ping_failure() {
  return "[dasall_cli] daemon ping: FAILED — daemon unavailable or timeout";
}

std::string CliOutputFormatter::format_submit_success(
    std::string_view raw_response) {
  std::string out = "[dasall_cli] submit accepted";
  if (!raw_response.empty()) {
    out += ": ";
    out += raw_response;
  }
  return out;
}

std::string CliOutputFormatter::format_error(std::string_view reason) {
  std::string out = "[dasall_cli] error";
  if (!reason.empty()) {
    out += ": ";
    out += reason;
  }
  return out;
}

}  // namespace dasall::apps::cli
