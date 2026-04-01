#pragma once

#include <string_view>

namespace dasall::infra::secret {

struct SecretQuery;
struct SecretAccessContext;
struct SecretDescriptor;
struct SecretHandle;
struct SecretLease;
struct SecureBuffer;
struct RotationRequest;
struct RotationResult;

struct SecretHandleResult;
struct SecretMaterializationResult;
struct SecretInspectionResult;
struct SecretLifecycleResult;

class ISecretManager {
 public:
  virtual ~ISecretManager() = default;

  [[nodiscard]] virtual SecretHandleResult get_secret(
      const SecretQuery& query,
      const SecretAccessContext& access_context) = 0;

  [[nodiscard]] virtual SecretMaterializationResult materialize(
      const SecretHandle& handle,
      const SecretAccessContext& access_context) = 0;

  [[nodiscard]] virtual SecretLifecycleResult release(
      const SecretLease& lease) = 0;

  [[nodiscard]] virtual RotationResult rotate(
      const RotationRequest& request) = 0;

  [[nodiscard]] virtual SecretLifecycleResult revoke(
      std::string_view secret_name,
      std::string_view reason_code) = 0;

  [[nodiscard]] virtual SecretInspectionResult inspect(
      std::string_view secret_name) const = 0;
};

}  // namespace dasall::infra::secret