#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "ISummarizer.h"

namespace dasall::llm {

class ILLMManager;

}  // namespace dasall::llm

namespace dasall::apps::runtime_support {

class LLMBackedSummarizer final : public memory::ISummarizer {
 public:
  struct Options {
    std::string model_route = "cloud.general";
    std::string prompt_release_id = "responder@2026.06.02";
    std::string output_schema_ref = "schema://responder/memory_summary";
    std::string profile_id = "unknown";
    std::uint32_t timeout_ms = 15000U;
    std::uint32_t default_max_output_tokens = 384U;
  };

  explicit LLMBackedSummarizer(std::shared_ptr<llm::ILLMManager> llm_manager);
  LLMBackedSummarizer(std::shared_ptr<llm::ILLMManager> llm_manager,
                      Options options);

  [[nodiscard]] memory::SummaryGenerationResult summarize(
      const memory::SummaryGenerationRequest& request) override;

 private:
  std::shared_ptr<llm::ILLMManager> llm_manager_;
  Options options_;
};

}  // namespace dasall::apps::runtime_support