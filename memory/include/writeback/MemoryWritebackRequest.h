#pragma once

#include <optional>
#include <string>
#include <vector>

#include "checkpoint/ReflectionLessonProjection.h"
#include "IProgrammaticMemoryStore.h"
#include "memory/ExperienceMemory.h"
#include "memory/MemoryFact.h"
#include "memory/SummaryMemory.h"
#include "memory/Turn.h"

namespace dasall::memory {

struct FactCandidate {
  contracts::MemoryFact fact;
  std::string extraction_source;
};

struct ExperienceCandidate {
  contracts::ExperienceMemory experience;
  std::string extraction_source;
};

struct ProgrammaticMemoryCandidate {
  ProgrammaticMemoryRecord record;
  std::string extraction_source;
};

struct MemoryWritebackRequest {
  std::string request_id;
  std::string session_id;
  std::string trace_id;
  contracts::Turn turn;
  std::optional<contracts::SummaryMemory> summary_candidate;
  std::vector<FactCandidate> fact_candidates;
  std::vector<ExperienceCandidate> experience_candidates;
  std::vector<ProgrammaticMemoryCandidate> programmatic_candidates;
  std::optional<contracts::ReflectionLessonProjection> reflection_lesson;
  std::optional<std::string> side_effect_report_ref;
};

}  // namespace dasall::memory