#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace dasall::contracts {

enum class DomainSubdomain {
  Tool,
  Prompt,
  Memory,
  Task,
  Event,
  Llm,
};

enum class DomainRolloutWave {
  Wave1Tool,
  Wave2PromptMemory,
  Wave3TaskEvent,
  Wave4Llm,
};

enum class DomainRolloutDecision {
  AllowStart,
  RejectAlreadyCompleted,
  RejectAlreadyInProgress,
  RejectMissingPrerequisite,
  RejectParallelBoundaryViolation,
};

struct DomainRolloutSnapshot {
  bool tool_completed = false;
  bool prompt_completed = false;
  bool memory_completed = false;
  bool task_completed = false;
  bool event_completed = false;
  bool llm_completed = false;

  bool tool_in_progress = false;
  bool prompt_in_progress = false;
  bool memory_in_progress = false;
  bool task_in_progress = false;
  bool event_in_progress = false;
  bool llm_in_progress = false;
};

struct DomainRolloutGuardResult {
  bool allowed = false;
  DomainRolloutDecision decision =
      DomainRolloutDecision::RejectMissingPrerequisite;
  DomainRolloutWave required_wave = DomainRolloutWave::Wave1Tool;
  std::string_view missing_prerequisite = "none";
  std::string_view reason =
      "domain rollout blocked until prerequisites and wave boundary are satisfied";
};

inline constexpr std::array<DomainSubdomain, 6> kDomainRolloutOrder = {
    DomainSubdomain::Tool,
    DomainSubdomain::Prompt,
    DomainSubdomain::Memory,
    DomainSubdomain::Task,
    DomainSubdomain::Event,
    DomainSubdomain::Llm,
};

inline constexpr std::array<std::string_view, 6> kDomainRolloutNames = {
    "tool",
    "prompt",
    "memory",
    "task",
    "event",
    "llm",
};

constexpr std::string_view domain_rollout_name(DomainSubdomain domain) {
  switch (domain) {
    case DomainSubdomain::Tool:
      return "tool";
    case DomainSubdomain::Prompt:
      return "prompt";
    case DomainSubdomain::Memory:
      return "memory";
    case DomainSubdomain::Task:
      return "task";
    case DomainSubdomain::Event:
      return "event";
    case DomainSubdomain::Llm:
      return "llm";
  }

  return "unknown";
}

constexpr DomainRolloutWave domain_rollout_wave(DomainSubdomain domain) {
  switch (domain) {
    case DomainSubdomain::Tool:
      return DomainRolloutWave::Wave1Tool;
    case DomainSubdomain::Prompt:
    case DomainSubdomain::Memory:
      return DomainRolloutWave::Wave2PromptMemory;
    case DomainSubdomain::Task:
    case DomainSubdomain::Event:
      return DomainRolloutWave::Wave3TaskEvent;
    case DomainSubdomain::Llm:
      return DomainRolloutWave::Wave4Llm;
  }

  return DomainRolloutWave::Wave4Llm;
}

constexpr bool domain_is_completed(const DomainRolloutSnapshot& snapshot,
                                   DomainSubdomain domain) {
  switch (domain) {
    case DomainSubdomain::Tool:
      return snapshot.tool_completed;
    case DomainSubdomain::Prompt:
      return snapshot.prompt_completed;
    case DomainSubdomain::Memory:
      return snapshot.memory_completed;
    case DomainSubdomain::Task:
      return snapshot.task_completed;
    case DomainSubdomain::Event:
      return snapshot.event_completed;
    case DomainSubdomain::Llm:
      return snapshot.llm_completed;
  }

  return false;
}

constexpr bool domain_is_in_progress(const DomainRolloutSnapshot& snapshot,
                                     DomainSubdomain domain) {
  switch (domain) {
    case DomainSubdomain::Tool:
      return snapshot.tool_in_progress;
    case DomainSubdomain::Prompt:
      return snapshot.prompt_in_progress;
    case DomainSubdomain::Memory:
      return snapshot.memory_in_progress;
    case DomainSubdomain::Task:
      return snapshot.task_in_progress;
    case DomainSubdomain::Event:
      return snapshot.event_in_progress;
    case DomainSubdomain::Llm:
      return snapshot.llm_in_progress;
  }

  return false;
}

constexpr std::string_view first_missing_rollout_prerequisite(
    const DomainRolloutSnapshot& snapshot, DomainSubdomain domain) {
  switch (domain) {
    case DomainSubdomain::Tool:
      return "none";

    case DomainSubdomain::Prompt:
    case DomainSubdomain::Memory:
      return snapshot.tool_completed ? "none" : "tool";

    case DomainSubdomain::Task:
    case DomainSubdomain::Event:
      if (!snapshot.tool_completed) {
        return "tool";
      }
      if (!snapshot.prompt_completed) {
        return "prompt";
      }
      if (!snapshot.memory_completed) {
        return "memory";
      }
      return "none";

    case DomainSubdomain::Llm:
      if (!snapshot.tool_completed) {
        return "tool";
      }
      if (!snapshot.prompt_completed) {
        return "prompt";
      }
      if (!snapshot.memory_completed) {
        return "memory";
      }
      if (!snapshot.task_completed) {
        return "task";
      }
      if (!snapshot.event_completed) {
        return "event";
      }
      return "none";
  }

  return "none";
}

constexpr bool has_parallel_wave_conflict(const DomainRolloutSnapshot& snapshot,
                                          DomainSubdomain domain) {
  switch (domain) {
    case DomainSubdomain::Tool:
      return snapshot.prompt_in_progress || snapshot.memory_in_progress ||
             snapshot.task_in_progress || snapshot.event_in_progress ||
             snapshot.llm_in_progress;

    case DomainSubdomain::Prompt:
    case DomainSubdomain::Memory:
      return snapshot.tool_in_progress || snapshot.task_in_progress ||
             snapshot.event_in_progress || snapshot.llm_in_progress;

    case DomainSubdomain::Task:
    case DomainSubdomain::Event:
      return snapshot.tool_in_progress || snapshot.prompt_in_progress ||
             snapshot.memory_in_progress || snapshot.llm_in_progress;

    case DomainSubdomain::Llm:
      return snapshot.tool_in_progress || snapshot.prompt_in_progress ||
             snapshot.memory_in_progress || snapshot.task_in_progress ||
             snapshot.event_in_progress;
  }

  return true;
}

inline constexpr DomainRolloutGuardResult evaluate_domain_rollout_start(
    const DomainRolloutSnapshot& snapshot, DomainSubdomain domain) {
  if (domain_is_completed(snapshot, domain)) {
    return DomainRolloutGuardResult{
        .allowed = false,
        .decision = DomainRolloutDecision::RejectAlreadyCompleted,
        .required_wave = domain_rollout_wave(domain),
        .missing_prerequisite = "none",
        .reason = "domain rollout cannot restart after completion",
    };
  }

  if (domain_is_in_progress(snapshot, domain)) {
    return DomainRolloutGuardResult{
        .allowed = false,
        .decision = DomainRolloutDecision::RejectAlreadyInProgress,
        .required_wave = domain_rollout_wave(domain),
        .missing_prerequisite = "none",
        .reason = "domain rollout is already in progress",
    };
  }

  const auto missing_prerequisite =
      first_missing_rollout_prerequisite(snapshot, domain);
  if (missing_prerequisite != "none") {
    return DomainRolloutGuardResult{
        .allowed = false,
        .decision = DomainRolloutDecision::RejectMissingPrerequisite,
        .required_wave = domain_rollout_wave(domain),
        .missing_prerequisite = missing_prerequisite,
        .reason = "domain rollout prerequisite is not complete",
    };
  }

  if (has_parallel_wave_conflict(snapshot, domain)) {
    return DomainRolloutGuardResult{
        .allowed = false,
        .decision = DomainRolloutDecision::RejectParallelBoundaryViolation,
        .required_wave = domain_rollout_wave(domain),
        .missing_prerequisite = "none",
        .reason = "domain rollout violates the approved parallel wave boundary",
    };
  }

  return DomainRolloutGuardResult{
      .allowed = true,
      .decision = DomainRolloutDecision::AllowStart,
      .required_wave = domain_rollout_wave(domain),
      .missing_prerequisite = "none",
      .reason = "domain rollout may start in the approved wave",
  };
}

inline constexpr bool can_start_domain_rollout(
    const DomainRolloutSnapshot& snapshot, DomainSubdomain domain) {
  return evaluate_domain_rollout_start(snapshot, domain).allowed;
}

inline constexpr std::size_t count_completed_domain_rollouts(
    const DomainRolloutSnapshot& snapshot) {
  std::size_t count = 0;
  for (const auto domain : kDomainRolloutOrder) {
    if (domain_is_completed(snapshot, domain)) {
      ++count;
    }
  }
  return count;
}

}  // namespace dasall::contracts