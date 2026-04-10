#pragma once

#include "LLMAdapterConfig.h"
#include "llm/LLMRequest.h"

namespace dasall::llm {

struct AdapterCallResult;
struct HealthStatus;
struct StreamSessionRef;
class IStreamObserver;

class ILLMAdapter {
public:
  virtual ~ILLMAdapter() = default;

  virtual bool init(const LLMAdapterConfig& config) = 0;
  virtual AdapterCallResult generate(const contracts::LLMRequest& request) = 0;
  virtual StreamSessionRef stream_generate(const contracts::LLMRequest& request,
                                           IStreamObserver* observer) = 0;
  virtual HealthStatus health_check() = 0;
};

}  // namespace dasall::llm
