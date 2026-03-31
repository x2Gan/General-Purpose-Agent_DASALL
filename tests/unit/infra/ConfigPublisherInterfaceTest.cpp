#include <exception>
#include <iostream>
#include <string>
#include <type_traits>

#include "config/IConfigPublisher.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

dasall::infra::config::ConfigDiff make_valid_diff() {
  return dasall::infra::config::ConfigDiff{
      .from_version = 11,
      .to_version = 12,
      .changes = {dasall::infra::config::ConfigDiffEntry{
          .key_path = std::string("infra.config.validation.strict"),
          .from_serialized_value = std::string("true"),
          .to_serialized_value = std::string("false"),
          .source_kind = dasall::infra::config::ConfigSourceKind::RuntimeOverride,
      }},
  };
}

class NullConfigPublisher final : public dasall::infra::config::IConfigPublisher {
 public:
  dasall::infra::config::ConfigPublishResult publish_config_changed(
      const dasall::infra::config::ConfigDiff& diff) override {
    if (!diff.is_valid()) {
      return dasall::infra::config::ConfigPublishResult::failure(
          dasall::contracts::ResultCode::ValidationFieldMissing,
          "config diff must keep ordered versions and key-granular changes",
          "config.publish_config_changed",
          "NullConfigPublisher");
    }

    return dasall::infra::config::ConfigPublishResult::success(
        std::string("config-event://diff/") + std::to_string(diff.to_version),
        diff.changes.size());
  }
};

void test_config_publisher_interface_accepts_valid_diff_payloads() {
  using dasall::infra::config::ConfigDiff;
  using dasall::infra::config::ConfigPublishResult;
  using dasall::infra::config::IConfigPublisher;
  using dasall::tests::support::assert_true;

  static_assert(std::is_same_v<decltype(std::declval<IConfigPublisher&>().publish_config_changed(
                                   std::declval<const ConfigDiff&>())),
                               ConfigPublishResult>);

  NullConfigPublisher publisher;
  const auto result = publisher.publish_config_changed(make_valid_diff());
  assert_true(result.published && result.event_id == "config-event://diff/12",
              "IConfigPublisher should accept a valid config diff and return a publish receipt");
  assert_true(result.delivered_subscriber_count == 1,
              "publisher receipt should keep the delivered subscriber count in the minimal interface contract");
}

void test_config_publisher_interface_rejects_invalid_diff_payloads() {
  using dasall::tests::support::assert_true;

  NullConfigPublisher publisher;
  const auto result = publisher.publish_config_changed(dasall::infra::config::ConfigDiff{});
  assert_true(!result.published,
              "IConfigPublisher should reject invalid config diffs before attempting delivery");
  assert_true(result.references_only_contract_error_types(),
              "publisher failures should remain inside contracts ResultCode/ErrorInfo types");
}

}  // namespace

int main() {
  try {
    test_config_publisher_interface_accepts_valid_diff_payloads();
    test_config_publisher_interface_rejects_invalid_diff_payloads();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}