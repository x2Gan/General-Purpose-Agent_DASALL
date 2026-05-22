#include "view/TuiModelSelector.h"

#include <algorithm>
#include <utility>

namespace dasall::tui::view {
namespace {

constexpr char kSelectorSource[] = "tui_model_selector";

void append_unique_reason(std::vector<std::string>& reasons, const std::string& reason) {
  if (reason.empty()) {
    return;
  }

  if (std::find(reasons.begin(), reasons.end(), reason) == reasons.end()) {
    reasons.push_back(reason);
  }
}

[[nodiscard]] std::string build_depth_summary(
    const std::optional<std::string>& depth_tier) {
  if (!depth_tier.has_value()) {
    return "prefer depth";
  }

  return "prefer depth: " + *depth_tier;
}

[[nodiscard]] std::string build_pin_summary(
    const std::optional<std::string>& provider_id,
    const std::optional<std::string>& model_id) {
  if (!provider_id.has_value() || !model_id.has_value()) {
    return "pin model";
  }

  return "pin model: " + *provider_id + "/" + *model_id;
}

[[nodiscard]] std::string build_pin_label(const data::TuiRouteCatalogEntry& entry) {
  return entry.provider_id + "/" + entry.model_id + " (" + entry.depth_tier + ")";
}

[[nodiscard]] data::TuiRouteCatalogEntry make_current_route_entry(
    const data::TuiRouteCatalogView& route_catalog) {
  data::TuiRouteCatalogEntry entry;
  entry.provider_id = route_catalog.current_route.current_provider_id;
  entry.model_id = route_catalog.current_route.current_model_id;
  entry.depth_tier = route_catalog.current_route.current_depth_tier;
  entry.disabled_reasons = route_catalog.current_route.disabled_reasons;
  entry.selectable = entry.disabled_reasons.empty();
  return entry;
}

}  // namespace

TuiModelSelector::TuiModelSelector(data::TuiRouteCatalogView route_catalog) {
  set_route_catalog(std::move(route_catalog));
}

bool TuiModelSelector::is_open() const noexcept { return open_; }

data::TuiRoutePreferenceMode TuiModelSelector::open_mode() const noexcept {
  return pending_preference_.mode;
}

const data::NextTurnPreference& TuiModelSelector::draft() const noexcept {
  return committed_preference_;
}

void TuiModelSelector::set_route_catalog(data::TuiRouteCatalogView route_catalog) {
  route_catalog_ = std::move(route_catalog);
  committed_preference_ = route_catalog_.current_route.next_preference;
  pending_preference_ = committed_preference_;
  open_ = false;
}

std::vector<TuiModelSelectorOption> TuiModelSelector::open_selector(
    const std::optional<data::TuiRoutePreferenceMode> mode) {
  if (mode.has_value()) {
    data::NextTurnPreference next = committed_preference_;
    next.mode = *mode;
    pending_preference_ = normalize_preference(std::move(next));
  } else {
    pending_preference_ = committed_preference_;
  }

  open_ = true;
  return filtered_options();
}

bool TuiModelSelector::choose_depth_tier(const std::string& depth_tier) {
  for (const TuiModelSelectorOption& option : build_depth_options(std::nullopt)) {
    if (option.depth_tier != depth_tier) {
      continue;
    }

    if (!option.selectable) {
      return false;
    }

    pending_preference_.mode = data::TuiRoutePreferenceMode::PreferDepth;
    pending_preference_.preferred_depth_tier = depth_tier;
    pending_preference_.pinned_provider_id.reset();
    pending_preference_.pinned_model_id.reset();
    pending_preference_ = normalize_preference(std::move(pending_preference_));
    return true;
  }

  return false;
}

bool TuiModelSelector::choose_model(const std::string& provider_id,
                                    const std::string& model_id) {
  const auto entry = find_route_entry(provider_id, model_id);
  if (!entry.has_value() || !entry->selectable) {
    return false;
  }

  pending_preference_.mode = data::TuiRoutePreferenceMode::PinModel;
  pending_preference_.preferred_depth_tier.reset();
  pending_preference_.pinned_provider_id = provider_id;
  pending_preference_.pinned_model_id = model_id;
  pending_preference_ = normalize_preference(std::move(pending_preference_));
  return true;
}

data::NextTurnPreference TuiModelSelector::apply_preference() {
  committed_preference_ = normalize_preference(pending_preference_);
  pending_preference_ = committed_preference_;
  open_ = false;
  return committed_preference_;
}

void TuiModelSelector::cancel_preference() {
  pending_preference_ = committed_preference_;
  open_ = false;
}

std::vector<TuiModelSelectorOption> TuiModelSelector::filtered_options() const {
  switch (pending_preference_.mode) {
    case data::TuiRoutePreferenceMode::Auto:
      return {};

    case data::TuiRoutePreferenceMode::PreferDepth:
      return build_depth_options(pending_preference_.preferred_depth_tier);

    case data::TuiRoutePreferenceMode::PinModel:
      return build_pin_options(pending_preference_.pinned_provider_id,
                               pending_preference_.pinned_model_id);
  }

  return {};
}

std::string TuiModelSelector::render_disabled_reason(
    const std::vector<std::string>& reason_codes) const {
  std::vector<std::string> normalized_reasons;
  normalized_reasons.reserve(reason_codes.size());

  for (std::string reason_code : reason_codes) {
    std::replace(reason_code.begin(), reason_code.end(), '_', ' ');
    append_unique_reason(normalized_reasons, reason_code);
  }

  std::string summary;
  for (std::size_t index = 0; index < normalized_reasons.size(); ++index) {
    if (index > 0) {
      summary += ", ";
    }
    summary += normalized_reasons[index];
  }

  return summary;
}

data::NextTurnPreference TuiModelSelector::normalize_preference(
    data::NextTurnPreference preference) const {
  preference.source = kSelectorSource;
  preference.applies_to_next_turn_only = true;

  switch (preference.mode) {
    case data::TuiRoutePreferenceMode::Auto:
      preference.preferred_depth_tier.reset();
      preference.pinned_provider_id.reset();
      preference.pinned_model_id.reset();
      preference.user_visible_summary = "auto";
      return preference;

    case data::TuiRoutePreferenceMode::PreferDepth: {
      preference.pinned_provider_id.reset();
      preference.pinned_model_id.reset();

      if (!preference.preferred_depth_tier.has_value() &&
          !route_catalog_.current_route.current_depth_tier.empty()) {
        preference.preferred_depth_tier = route_catalog_.current_route.current_depth_tier;
      }

      preference.user_visible_summary =
          build_depth_summary(preference.preferred_depth_tier);
      return preference;
    }

    case data::TuiRoutePreferenceMode::PinModel: {
      preference.preferred_depth_tier.reset();

      if (!preference.pinned_provider_id.has_value() ||
          !preference.pinned_model_id.has_value()) {
        if (!route_catalog_.current_route.current_provider_id.empty() &&
            !route_catalog_.current_route.current_model_id.empty()) {
          preference.pinned_provider_id = route_catalog_.current_route.current_provider_id;
          preference.pinned_model_id = route_catalog_.current_route.current_model_id;
        }
      }

      preference.user_visible_summary =
          build_pin_summary(preference.pinned_provider_id, preference.pinned_model_id);
      return preference;
    }
  }

  return preference;
}

std::vector<TuiModelSelectorOption> TuiModelSelector::build_depth_options(
    const std::optional<std::string>& selected_depth_tier) const {
  std::vector<TuiModelSelectorOption> options;

  if (!route_catalog_.current_route.current_depth_tier.empty()) {
    TuiModelSelectorOption option;
    option.display_label = route_catalog_.current_route.current_depth_tier;
    option.depth_tier = route_catalog_.current_route.current_depth_tier;
    option.selectable = route_catalog_.current_route.disabled_reasons.empty();
    option.selected = selected_depth_tier.has_value() &&
                      *selected_depth_tier == option.depth_tier;
    if (!option.selectable) {
      option.disabled_reasons = route_catalog_.current_route.disabled_reasons;
    }
    options.push_back(std::move(option));
  }

  for (const data::TuiRouteCatalogEntry& entry : route_catalog_.candidate_routes) {
    auto existing = std::find_if(options.begin(), options.end(), [&](const auto& option) {
      return option.depth_tier == entry.depth_tier;
    });

    if (existing == options.end()) {
      TuiModelSelectorOption option;
      option.display_label = entry.depth_tier;
      option.depth_tier = entry.depth_tier;
      option.selectable = entry.selectable;
      option.selected = selected_depth_tier.has_value() &&
                        *selected_depth_tier == entry.depth_tier;
      if (!entry.selectable) {
        option.disabled_reasons = entry.disabled_reasons;
      }
      options.push_back(std::move(option));
      continue;
    }

    existing->selected = selected_depth_tier.has_value() &&
                         *selected_depth_tier == existing->depth_tier;

    if (entry.selectable) {
      existing->selectable = true;
      existing->disabled_reasons.clear();
      continue;
    }

    if (!existing->selectable) {
      for (const std::string& reason : entry.disabled_reasons) {
        append_unique_reason(existing->disabled_reasons, reason);
      }
    }
  }

  return options;
}

std::vector<TuiModelSelectorOption> TuiModelSelector::build_pin_options(
    const std::optional<std::string>& selected_provider_id,
    const std::optional<std::string>& selected_model_id) const {
  std::vector<TuiModelSelectorOption> options;

  auto append_option = [&](const data::TuiRouteCatalogEntry& entry) {
    const auto existing = std::find_if(options.begin(), options.end(), [&](const auto& option) {
      return option.provider_id == entry.provider_id && option.model_id == entry.model_id;
    });
    if (existing != options.end()) {
      return;
    }

    TuiModelSelectorOption option;
    option.display_label = build_pin_label(entry);
    option.provider_id = entry.provider_id;
    option.model_id = entry.model_id;
    option.depth_tier = entry.depth_tier;
    option.selectable = entry.selectable;
    option.selected = selected_provider_id.has_value() && selected_model_id.has_value() &&
                      *selected_provider_id == entry.provider_id &&
                      *selected_model_id == entry.model_id;
    option.disabled_reasons = entry.disabled_reasons;
    options.push_back(std::move(option));
  };

  const auto current_route = make_current_route_entry(route_catalog_);
  if (!current_route.provider_id.empty() && !current_route.model_id.empty()) {
    append_option(current_route);
  }

  for (const data::TuiRouteCatalogEntry& entry : route_catalog_.candidate_routes) {
    append_option(entry);
  }

  return options;
}

std::optional<data::TuiRouteCatalogEntry> TuiModelSelector::find_route_entry(
    const std::string& provider_id,
    const std::string& model_id) const {
  const auto existing = std::find_if(route_catalog_.candidate_routes.begin(),
                                     route_catalog_.candidate_routes.end(),
                                     [&](const auto& entry) {
                                       return entry.provider_id == provider_id &&
                                              entry.model_id == model_id;
                                     });
  if (existing != route_catalog_.candidate_routes.end()) {
    return *existing;
  }

  if (route_catalog_.current_route.current_provider_id == provider_id &&
      route_catalog_.current_route.current_model_id == model_id) {
    return make_current_route_entry(route_catalog_);
  }

  return std::nullopt;
}

}  // namespace dasall::tui::view