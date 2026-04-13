#pragma once

#include "HealthStatus.h"
#include "LLMGenerateRequest.h"
#include "LLMManagerResult.h"

namespace dasall::llm {

struct LLMSubsystemConfig;
class IStreamObserver;

class ILLMManager {
 public:
  virtual ~ILLMManager() = default;

  virtual bool init(const LLMSubsystemConfig& config) = 0;
  virtual LLMManagerResult generate(const LLMGenerateRequest& request) = 0;
  virtual LLMManagerResult stream_generate(const LLMGenerateRequest& request,
                                           IStreamObserver* observer) = 0;
  virtual HealthStatus health_check() const = 0;
};

}  // namespace dasall::llm
