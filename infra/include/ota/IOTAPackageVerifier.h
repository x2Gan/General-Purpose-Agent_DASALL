#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ota/OTATypes.h"

namespace dasall::infra::ota {

struct PackageVerificationResult {
  bool verified = false;
  VerifiedPackageManifest manifest;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static PackageVerificationResult success(
      VerifiedPackageManifest manifest) {
    return PackageVerificationResult{
        .verified = true,
        .manifest = std::move(manifest),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static PackageVerificationResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return PackageVerificationResult{
        .verified = false,
        .manifest = {},
        .result_code = result_code,
        .error = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.ota",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error.has_value()) {
      return verified && manifest.is_valid();
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

struct ArtifactVerificationResult {
  bool verified = false;
  ArtifactDescriptor artifact;
  std::vector<std::string> verified_hashes;
  contracts::ResultCode result_code = contracts::ResultCode::RuntimeRetryExhausted;
  std::optional<contracts::ErrorInfo> error;

  [[nodiscard]] static ArtifactVerificationResult success(
      ArtifactDescriptor artifact,
      std::vector<std::string> verified_hashes) {
    return ArtifactVerificationResult{
        .verified = true,
        .artifact = std::move(artifact),
        .verified_hashes = std::move(verified_hashes),
        .result_code = contracts::ResultCode::RuntimeRetryExhausted,
        .error = std::nullopt,
    };
  }

  [[nodiscard]] static ArtifactVerificationResult failure(
      contracts::ResultCode result_code,
      std::string message,
      std::string stage,
      std::string source_ref) {
    return ArtifactVerificationResult{
        .verified = false,
        .artifact = {},
        .verified_hashes = {},
        .result_code = result_code,
        .error = contracts::ErrorInfo{
            .failure_type = contracts::classify_result_code(result_code),
            .retryable = false,
            .safe_to_replan = false,
            .details = contracts::ErrorDetails{
                .code = static_cast<int>(result_code),
                .message = std::move(message),
                .stage = std::move(stage),
            },
            .source_ref = contracts::ErrorSourceRefMinimal{
                .ref_type = "infra.ota",
                .ref_id = std::move(source_ref),
            },
        },
    };
  }

  [[nodiscard]] bool references_only_contract_error_types() const {
    if (!error.has_value()) {
      return verified && artifact.is_valid() && !verified_hashes.empty() &&
             has_unique_non_empty_values(verified_hashes);
    }

    return error->failure_type.has_value() &&
           *error->failure_type == contracts::classify_result_code(result_code);
  }
};

class IOTAPackageVerifier {
 public:
  virtual ~IOTAPackageVerifier() = default;

  [[nodiscard]] virtual PackageVerificationResult verify_package(
      const PackageDescriptor& package_descriptor) const = 0;
  [[nodiscard]] virtual ArtifactVerificationResult verify_artifact(
      const ArtifactDescriptor& artifact_descriptor) const = 0;
};

}  // namespace dasall::infra::ota