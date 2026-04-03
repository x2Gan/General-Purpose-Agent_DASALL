#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include "secret/SecretLeaseRegistry.h"
#include "dasall/tests/support/TestAssertions.h"

namespace {

[[nodiscard]] dasall::infra::secret::SecretHandle make_handle(std::string version = "v3",
                                                              std::int64_t expires_at_ms = 4102444800000) {
  return dasall::infra::secret::SecretHandle{
      .handle_id = std::string("secret-handle://db/root/") + version,
      .secret_name = std::string("db/root"),
      .version = std::move(version),
      .backend_ref = std::string("mock.primary"),
      .issued_at_ms = 1700000000000,
      .expires_at_ms = expires_at_ms,
      .redaction_hint = std::string("redacted://secret/db/root"),
  };
}

void test_secret_lease_registry_creates_a_valid_active_lease() {
  using dasall::infra::secret::SecretLeaseRegistry;
  using dasall::infra::secret::SecretLeaseState;
  using dasall::tests::support::assert_true;

  SecretLeaseRegistry registry;
  const auto created = registry.create_lease(make_handle(), "runtime", 3, std::nullopt);

  assert_true(created.ok && created.is_valid() && created.lease.state == SecretLeaseState::Active &&
                  registry.active_lease_count() == 1U,
              "SecretLeaseRegistry should create a valid active lease with a unique lease_id for a valid handle and consumer");
}

void test_secret_lease_registry_marks_expired_leases_and_rejects_validation() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::SecretLeaseRegistry;
  using dasall::infra::secret::SecretLeaseState;
  using dasall::tests::support::assert_true;

  SecretLeaseRegistry registry;
  const auto created = registry.create_lease(make_handle(), "runtime", 1, std::nullopt);
  const auto expired = registry.expire_lease(created.lease, SecretLeaseState::Expired);
  const auto validated = registry.validate_lease(created.lease, make_handle(), created.lease.rotation_epoch);

  assert_true(expired.ok && expired.is_valid() && !validated.ok && validated.is_valid() &&
                  validated.result_code == ResultCode::RuntimeRetryExhausted &&
                  registry.active_lease_count() == 0U,
              "SecretLeaseRegistry should transition a lease to expired and reject later validation with a runtime lifecycle error");
}

void test_secret_lease_registry_releases_active_leases() {
  using dasall::infra::secret::SecretLeaseRegistry;
  using dasall::tests::support::assert_true;

  SecretLeaseRegistry registry;
  const auto created = registry.create_lease(make_handle(), "runtime", 1, std::nullopt);
  const auto released = registry.release_lease(created.lease);

  assert_true(released.ok && released.is_valid() && registry.active_lease_count() == 0U,
              "SecretLeaseRegistry should release an active lease and remove it from the active lease count");
}

void test_secret_lease_registry_detects_stale_rotation_epoch() {
  using dasall::contracts::ResultCode;
  using dasall::infra::secret::SecretLeaseRegistry;
  using dasall::tests::support::assert_true;

  SecretLeaseRegistry registry;
  const auto handle = make_handle();
  const auto created = registry.create_lease(handle, "runtime", 2, std::nullopt);
  const auto validated = registry.validate_lease(created.lease, handle, 3);

  assert_true(!validated.ok && validated.is_valid() &&
                  validated.result_code == ResultCode::RuntimeRetryExhausted &&
                  registry.active_lease_count() == 0U,
              "SecretLeaseRegistry should treat rotation epoch drift as a stale handle signal and stop counting the lease as active");
}

}  // namespace

int main() {
  try {
    test_secret_lease_registry_creates_a_valid_active_lease();
    test_secret_lease_registry_marks_expired_leases_and_rejects_validation();
    test_secret_lease_registry_releases_active_leases();
    test_secret_lease_registry_detects_stale_rotation_epoch();
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << std::endl;
    return 1;
  }

  return 0;
}