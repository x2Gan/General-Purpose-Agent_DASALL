#pragma once

#include <string>
#include <vector>

namespace dasall::cognition::perception {

struct EntityCandidate {
  std::string name;
  std::string value;
  float confidence = 0.0F;
  std::vector<std::string> evidence_refs;
};

struct ConstraintDigest {
  std::vector<std::string> hard_constraints;
  std::vector<std::string> soft_constraints;
  std::vector<std::string> policy_refs;
};

struct AmbiguityMarker {
  std::string ambiguity_id;
  std::string description;
  std::vector<std::string> missing_evidence_refs;
  float severity = 0.0F;
};

struct ClarificationCandidate {
  std::string question;
  std::vector<std::string> evidence_refs;
  float priority = 0.0F;
};

struct PerceptionResult {
  std::string intent_summary;
  std::string task_type;
  std::vector<EntityCandidate> entities;
  ConstraintDigest constraints_digest;
  std::vector<AmbiguityMarker> ambiguities;
  std::vector<ClarificationCandidate> clarification_questions;
  float confidence = 0.0F;
  bool requires_clarification = false;
  std::vector<std::string> diagnostics;
};

}  // namespace dasall::cognition::perception
