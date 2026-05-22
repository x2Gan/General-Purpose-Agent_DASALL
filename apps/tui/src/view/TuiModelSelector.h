#pragma once

#include <optional>
#include <string>
#include <vector>

#include "data/TuiProjectionTypes.h"

namespace dasall::tui::view {

struct TuiModelSelectorOption {
  std::string display_label;
  std::string provider_id;
  std::string model_id;
  std::string depth_tier;
  bool selectable = true;
  bool selected = false;
  std::vector<std::string> disabled_reasons;
};

class TuiModelSelector {
 public:
  explicit TuiModelSelector(data::TuiRouteCatalogView route_catalog = {});

  [[nodiscard]] bool is_open() const noexcept;

  [[nodiscard]] data::TuiRoutePreferenceMode open_mode() const noexcept;

  [[nodiscard]] const data::NextTurnPreference& draft() const noexcept;

  void set_route_catalog(data::TuiRouteCatalogView route_catalog);

  [[nodiscard]] std::vector<TuiModelSelectorOption> open_selector(
      std::optional<data::TuiRoutePreferenceMode> mode = std::nullopt);

  [[nodiscard]] bool choose_depth_tier(const std::string& depth_tier);

  [[nodiscard]] bool choose_model(const std::string& provider_id,
                                  const std::string& model_id);

  [[nodiscard]] data::NextTurnPreference apply_preference();

  void cancel_preference();

  [[nodiscard]] std::vector<TuiModelSelectorOption> filtered_options() const;

  [[nodiscard]] std::string render_disabled_reason(
      const std::vector<std::string>& reason_codes) const;

 private:
  [[nodiscard]] data::NextTurnPreference normalize_preference(
      data::NextTurnPreference preference) const;

  [[nodiscard]] std::vector<TuiModelSelectorOption> build_depth_options(
      const std::optional<std::string>& selected_depth_tier) const;

  [[nodiscard]] std::vector<TuiModelSelectorOption> build_pin_options(
      const std::optional<std::string>& selected_provider_id,
      const std::optional<std::string>& selected_model_id) const;

  [[nodiscard]] std::optional<data::TuiRouteCatalogEntry> find_route_entry(
      const std::string& provider_id,
      const std::string& model_id) const;

  data::TuiRouteCatalogView route_catalog_;
  data::NextTurnPreference committed_preference_;
  data::NextTurnPreference pending_preference_;
  bool open_ = false;
};

}  // namespace dasall::tui::view