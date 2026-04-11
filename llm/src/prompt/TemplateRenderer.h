#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dasall::llm::prompt {

using TemplateVariables = std::unordered_map<std::string, std::string>;

struct TemplateRendererConfig {
  std::string template_engine = "simple_var";
  std::size_t max_variable_length = 100U * 1024U;

  [[nodiscard]] bool has_consistent_values() const {
    return template_engine == "simple_var" && max_variable_length > 0U;
  }
};

struct TemplateRenderResult {
  std::string rendered_text;
  std::vector<std::string> warnings;
  bool nested_render_rejected = false;
  bool truncated_values = false;
};

class ITemplateRenderer {
 public:
  virtual ~ITemplateRenderer() = default;

  virtual bool init(const TemplateRendererConfig& config) = 0;
  [[nodiscard]] virtual TemplateRenderResult render(
      std::string_view template_text,
      const TemplateVariables& variables) const = 0;
};

class TemplateRenderer final : public ITemplateRenderer {
 public:
  bool init(const TemplateRendererConfig& config) override;

  [[nodiscard]] TemplateRenderResult render(
      std::string_view template_text,
      const TemplateVariables& variables) const override;

 private:
  TemplateRendererConfig config_;
  bool initialized_ = false;
};

}  // namespace dasall::llm::prompt