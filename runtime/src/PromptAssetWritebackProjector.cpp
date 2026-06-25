#include "PromptAssetWritebackProjector.h"

#include <utility>

namespace dasall::runtime {

memory::MemoryWritebackRequest PromptAssetWritebackProjector::enrich_with_prompt_asset(
    memory::MemoryWritebackRequest request,
    const contracts::AgentRequest& agent_request,
    const llm::LLMManagerResult& llm_result,
    const std::shared_ptr<llm::ILLMManager>& llm_manager) const {
  if (llm_manager == nullptr || !llm_result.response.has_value() ||
      !llm_result.response->prompt_id.has_value() ||
      llm_result.response->prompt_id->empty() ||
      !llm_result.response->prompt_version.has_value() ||
      llm_result.response->prompt_version->empty() ||
      !request.turn.turn_id.has_value() || request.turn.turn_id->empty()) {
    return request;
  }

  const std::string prompt_release_id = *llm_result.response->prompt_id + "@" +
                                        *llm_result.response->prompt_version;
  const auto prompt_asset_metadata =
      llm_manager->lookup_prompt_asset_metadata(prompt_release_id);
  if (!prompt_asset_metadata.has_value()) {
    return request;
  }

  const auto created_at = agent_request.created_at.value_or(0);
  memory::ProgrammaticMemoryCandidate candidate;
  candidate.record.asset_ref = "prompt:" + prompt_asset_metadata->prompt_release_id;
  candidate.record.session_id = request.session_id;
  candidate.record.source_turn_id = *request.turn.turn_id;
  candidate.record.content_digest = prompt_asset_metadata->content_hash;
  candidate.record.lease_expires_at = created_at + 86400000;
  candidate.record.tags = {
      "programmatic",
      "prompt",
      "source_layer:" + prompt_asset_metadata->source_layer,
      "package_id:" + prompt_asset_metadata->package_id,
  };
  candidate.extraction_source = "runtime.llm.prompt_asset_metadata";
  request.programmatic_candidates.push_back(std::move(candidate));
  return request;
}

}  // namespace dasall::runtime