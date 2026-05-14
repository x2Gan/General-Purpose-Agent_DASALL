#include <exception>
#include <iostream>
#include <string>

#include "AccessSemanticKinds.h"
#include "support/TestAssertions.h"

namespace {

using dasall::access::semantic::AccessEntryKind;
using dasall::access::semantic::AccessProtocolKind;
using dasall::access::semantic::parse_access_entry_kind;
using dasall::access::semantic::parse_access_protocol_kind;
using dasall::access::semantic::to_string;
using dasall::tests::support::assert_equal;
using dasall::tests::support::assert_true;

void access_semantic_kinds_round_trip_without_string_drift() {
  assert_equal(static_cast<int>(AccessEntryKind::Gateway),
               static_cast<int>(parse_access_entry_kind("gateway")),
               "gateway entry string should map to strong enum");
  assert_equal(static_cast<int>(AccessProtocolKind::HttpUnary),
               static_cast<int>(parse_access_protocol_kind("http_unary")),
               "http_unary protocol string should map to strong enum");
  assert_equal(std::string("gateway"),
               std::string(to_string(AccessEntryKind::Gateway)),
               "gateway enum should round-trip to canonical string");
  assert_equal(std::string("http_unary"),
               std::string(to_string(AccessProtocolKind::HttpUnary)),
               "http_unary enum should round-trip to canonical string");
  assert_equal(static_cast<int>(AccessEntryKind::Unknown),
               static_cast<int>(parse_access_entry_kind("gateway-ish")),
               "unknown entry strings should not silently map to a valid enum");
  assert_equal(static_cast<int>(AccessProtocolKind::Unknown),
               static_cast<int>(parse_access_protocol_kind("http")),
               "unknown protocol strings should not silently map to a valid enum");
}

}  // namespace

int main() {
  try {
    access_semantic_kinds_round_trip_without_string_drift();
  } catch (const std::exception& ex) {
    std::cerr << "[AccessStrongEnumContractTest] FAILED: " << ex.what() << '\n';
    return 1;
  }

  return 0;
}