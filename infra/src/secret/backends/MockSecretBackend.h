#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "secret/ISecretBackend.h"

namespace dasall::infra::secret {

struct MockSecretBackendOptions {
  std::string backend_ref = "mock.primary";
  bool available = true;
  bool rate_limited = false;
  bool read_only_fallback_ready = true;
  std::int64_t lease_duration_ms = 60000;
  std::uint64_t rotation_epoch = 1;
};

struct MockSecretRecord {
  SecretBackendRecord record;
  std::string materialized_text;
  std::vector<std::string> allowed_permission_domains;

  [[nodiscard]] bool is_valid() const {
    return record.is_valid() && !materialized_text.empty();
  }
};

class MockSecretBackend final : public ISecretBackend {
 public:
  explicit MockSecretBackend(MockSecretBackendOptions options = {});

  void upsert_secret(MockSecretRecord secret_record);
  void set_available(bool available);
  void set_rate_limited(bool rate_limited);
  void clear();

  [[nodiscard]] SecretBackendFetchResult fetch_record(
      const SecretQuery& query) override;

  [[nodiscard]] SecretMaterializationResult materialize_record(
      const SecretBackendRecord& record,
      const SecretAccessContext& access_context) override;

  [[nodiscard]] RotationResult promote_version(
      const SecretVersionPromotionRequest& request) override;

  [[nodiscard]] SecretLifecycleResult revoke_version(
      std::string_view secret_name,
      std::string_view version) override;

  [[nodiscard]] SecretBackendStatus get_backend_status() const override;

 private:
  using RecordMap = std::map<std::string, MockSecretRecord>;

  [[nodiscard]] RecordMap::iterator find_record(std::string_view secret_name);
  [[nodiscard]] RecordMap::const_iterator find_record(std::string_view secret_name) const;

  MockSecretBackendOptions options_;
  RecordMap records_;
  std::optional<contracts::ResultCode> last_error_code_;
};

}  // namespace dasall::infra::secret