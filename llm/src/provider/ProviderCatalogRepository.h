#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "LLMSubsystemConfig.h"
#include "provider/ModelCatalogEntry.h"
#include "provider/ProviderDescriptor.h"

namespace dasall::llm::provider {

struct ProviderRuntimeSettings {
  std::string display_name;
  std::string auth_mode;
  std::string trusted_source;
  std::string source_layer;
  std::string base_url_alias;
  std::vector<std::string> tags;
  std::vector<std::string> mutable_overlay_fields;
  bool activation_flag = true;

  [[nodiscard]] bool has_consistent_values() const;
  [[nodiscard]] bool overlay_field_is_mutable(std::string_view field) const;
};

struct ProviderCatalogProvider {
  ProviderDescriptor descriptor;
  ProviderRuntimeSettings runtime;

  [[nodiscard]] bool has_consistent_values() const;
};

struct ProviderModelMetadata {
  ModelCatalogEntry summary;
  std::string display_name;
  std::string reasoning_mode;
  std::string source_layer;
  std::string pricing_ref;
  std::vector<std::string> aliases;
  std::vector<std::string> input_modalities;
  std::vector<std::string> feature_notes;
  std::vector<std::string> response_private_fields;
  std::unordered_map<std::string, std::string> verification_states;
  bool supports_streaming = false;
  bool supports_json_object = false;
  bool supports_json_schema = false;
  bool supports_native_stream_usage = false;

  [[nodiscard]] bool has_consistent_values() const;
  [[nodiscard]] std::string verification_state_for(std::string_view capability) const;
};

struct ProviderCatalogSnapshot {
  std::string default_source_version;
  std::vector<ProviderCatalogProvider> providers;
  std::vector<ProviderModelMetadata> models;

  [[nodiscard]] bool has_consistent_values() const;
  [[nodiscard]] const ProviderCatalogProvider* find_provider(std::string_view provider_id) const;
  [[nodiscard]] const ProviderModelMetadata* find_model(std::string_view provider_id,
                                                        std::string_view model_id) const;
};

class ProviderCatalogRepository {
 public:
  bool init(const ProviderCatalogSourceConfig& config);
  bool reload();

  [[nodiscard]] std::shared_ptr<const ProviderCatalogSnapshot> snapshot() const;
  [[nodiscard]] std::string last_error_message() const;

 private:
  ProviderCatalogSourceConfig config_;
  std::shared_ptr<const ProviderCatalogSnapshot> catalog_snapshot_;
  std::string last_error_message_;
  bool initialized_ = false;
};

}  // namespace dasall::llm::provider