#include <algorithm>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "config/ConfigPublisher.h"
#include "support/TestAssertions.h"

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

void test_config_publisher_delivers_only_namespace_matched_subscribers() {
  using dasall::infra::config::ConfigPublisher;
  using dasall::tests::support::assert_equal;
  using dasall::tests::support::assert_true;

  ConfigPublisher publisher;
  std::vector<std::string> delivered_subscribers;
  std::vector<std::uint64_t> observed_versions;

  const auto exact_match = publisher.subscribe(dasall::infra::config::ConfigSubscriptionRequest{
      .namespace_filter = std::string("infra.config.validation."),
      .subscriber_id = std::string("validation-subscriber"),
      .callback = [&](const dasall::infra::config::ConfigDiff& diff) {
        delivered_subscribers.push_back("validation-subscriber");
        observed_versions.push_back(diff.to_version);
      },
  });
  const auto broad_match = publisher.subscribe(dasall::infra::config::ConfigSubscriptionRequest{
      .namespace_filter = std::string("infra.config."),
      .subscriber_id = std::string("broad-subscriber"),
      .callback = [&](const dasall::infra::config::ConfigDiff& diff) {
        delivered_subscribers.push_back("broad-subscriber");
        observed_versions.push_back(diff.to_version);
      },
  });
  const auto non_match = publisher.subscribe(dasall::infra::config::ConfigSubscriptionRequest{
      .namespace_filter = std::string("runtime_budget."),
      .subscriber_id = std::string("non-match-subscriber"),
      .callback = [&](const dasall::infra::config::ConfigDiff&) {
        delivered_subscribers.push_back("non-match-subscriber");
      },
  });
  const auto throwing_match = publisher.subscribe(dasall::infra::config::ConfigSubscriptionRequest{
      .namespace_filter = std::string("infra.config."),
      .subscriber_id = std::string("throwing-subscriber"),
      .callback = [](const dasall::infra::config::ConfigDiff&) {
        throw std::runtime_error("publisher should isolate subscriber failures");
      },
  });

  assert_true(exact_match.has_value() && exact_match->active,
              "ConfigPublisher should issue an active handle for exact namespace subscribers");
  assert_true(broad_match.has_value() && broad_match->active,
              "ConfigPublisher should issue an active handle for broad namespace subscribers");
  assert_true(non_match.has_value() && non_match->active,
              "ConfigPublisher should keep non-matching subscriptions registered for future diffs");
  assert_true(throwing_match.has_value() && throwing_match->active,
              "ConfigPublisher should register callbacks even when future delivery may fail");

  const auto result = publisher.publish_config_changed(make_valid_diff());
  assert_true(result.published,
              "ConfigPublisher should accept a valid config diff and publish it to matching subscribers");
  assert_equal(std::string("config-event://diff/12"),
               result.event_id,
               "ConfigPublisher should freeze the event_id format for runtime patch publishes");
  assert_equal(2,
               static_cast<int>(result.delivered_subscriber_count),
               "ConfigPublisher should count only successfully delivered matching subscribers");
  assert_equal(2,
               static_cast<int>(delivered_subscribers.size()),
               "ConfigPublisher should deliver the diff only to matching successful subscribers");
  assert_true(std::find(delivered_subscribers.begin(),
                        delivered_subscribers.end(),
                        "validation-subscriber") != delivered_subscribers.end(),
              "ConfigPublisher should deliver to the exact namespace subscriber");
  assert_true(std::find(delivered_subscribers.begin(),
                        delivered_subscribers.end(),
                        "broad-subscriber") != delivered_subscribers.end(),
              "ConfigPublisher should deliver to the broader namespace subscriber");
  assert_true(std::find(delivered_subscribers.begin(),
                        delivered_subscribers.end(),
                        "non-match-subscriber") == delivered_subscribers.end(),
              "ConfigPublisher should not deliver to non-matching namespace subscribers");
  assert_equal(2,
               static_cast<int>(observed_versions.size()),
               "ConfigPublisher should pass the published diff to every successful callback");
  assert_true(std::all_of(observed_versions.begin(), observed_versions.end(), [](std::uint64_t version) {
                return version == 12;
              }),
              "ConfigPublisher should preserve the diff to_version across matching deliveries");
}

void test_config_publisher_rejects_invalid_contracts() {
  using dasall::infra::config::ConfigPublisher;
  using dasall::tests::support::assert_true;

  ConfigPublisher publisher;

  const auto invalid_subscription = publisher.subscribe(dasall::infra::config::ConfigSubscriptionRequest{});
  assert_true(!invalid_subscription.has_value(),
              "ConfigPublisher should reject invalid subscription requests before registration");

  const auto invalid_publish = publisher.publish_config_changed(dasall::infra::config::ConfigDiff{});
  assert_true(!invalid_publish.published,
              "ConfigPublisher should reject invalid config diffs before delivery");
  assert_true(invalid_publish.references_only_contract_error_types(),
              "ConfigPublisher failures should remain inside contracts ResultCode/ErrorInfo types");
}

}  // namespace

int main() {
  try {
    test_config_publisher_delivers_only_namespace_matched_subscribers();
    test_config_publisher_rejects_invalid_contracts();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}