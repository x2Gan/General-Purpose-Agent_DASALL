#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "IStoreTransaction.h"
#include "config/MemoryConfig.h"
#include "memory/ExperienceMemory.h"
#include "memory/MemoryFact.h"
#include "memory/Session.h"
#include "memory/SummaryMemory.h"
#include "memory/Turn.h"

namespace dasall::memory {

struct SessionLoadBundle {
  contracts::Session session;
  std::vector<contracts::Turn> recent_turns;
  int total_turn_count = 0;
};

struct SessionLoadRequest {
  std::string session_id;
  int recent_turn_limit = 10;
};

struct StoreResult {
  bool ok = false;
  std::optional<contracts::ResultCode> result_code;
  std::optional<std::string> persisted_id;
  std::optional<std::string> error_message;

  [[nodiscard]] static StoreResult success(
      std::optional<std::string> persisted_id = std::nullopt) {
    return StoreResult{
        .ok = true,
        .result_code = std::nullopt,
        .persisted_id = std::move(persisted_id),
        .error_message = std::nullopt,
    };
  }

  [[nodiscard]] static StoreResult failure(contracts::ResultCode result_code,
                                           std::optional<std::string> error_message =
                                               std::nullopt) {
    return StoreResult{
        .ok = false,
        .result_code = result_code,
        .persisted_id = std::nullopt,
        .error_message = std::move(error_message),
    };
  }
};

struct FactQuery {
  std::optional<std::string> session_id;
  std::optional<std::string> user_id;
  std::optional<std::string> fact_type;
  std::optional<int> min_confidence;
  bool exclude_superseded = true;
  int limit = 50;
};

struct FactQueryResult {
  std::vector<contracts::MemoryFact> facts;
  int total_count = 0;
};

struct ExperienceQuery {
  std::optional<std::string> session_id;
  std::optional<std::string> user_id;
  std::optional<std::string> stage;
  std::optional<std::vector<std::string>> applicable_domains;
  bool exclude_expired = true;
  int limit = 20;
};

struct ExperienceQueryResult {
  std::vector<contracts::ExperienceMemory> experiences;
  int total_count = 0;
};

class IMemoryStore {
 public:
  virtual ~IMemoryStore() = default;

  [[nodiscard]] virtual std::optional<contracts::ResultCode> open(
      const MemoryConfig& config) = 0;
  virtual void close() noexcept = 0;

  [[nodiscard]] virtual std::unique_ptr<IStoreTransaction> begin_immediate() = 0;

  [[nodiscard]] virtual SessionLoadBundle load_session_bundle(
      const SessionLoadRequest& request) const = 0;
  [[nodiscard]] virtual StoreResult create_session(
      const contracts::Session& session) = 0;
  [[nodiscard]] virtual StoreResult append_turn(
      const contracts::Turn& turn) = 0;
  [[nodiscard]] virtual StoreResult update_session_active(
      const std::string& session_id, std::int64_t last_active_at) = 0;

  [[nodiscard]] virtual StoreResult upsert_summary(
      const contracts::SummaryMemory& summary) = 0;
  [[nodiscard]] virtual std::optional<contracts::SummaryMemory> load_latest_summary(
      const std::string& session_id) const = 0;

  [[nodiscard]] virtual FactQueryResult query_facts(const FactQuery& query) const = 0;
  [[nodiscard]] virtual StoreResult insert_fact(
      const contracts::MemoryFact& fact) = 0;
  [[nodiscard]] virtual StoreResult supersede_fact(
      const std::string& old_fact_id, const std::string& new_fact_id) = 0;

  [[nodiscard]] virtual ExperienceQueryResult query_experiences(
      const ExperienceQuery& query) const = 0;
  [[nodiscard]] virtual StoreResult insert_experience(
      const contracts::ExperienceMemory& experience) = 0;

  [[nodiscard]] virtual std::int64_t count_turns(const std::string& session_id) const = 0;
  [[nodiscard]] virtual StoreResult quarantine_record(
      const std::string& object_type,
      const std::string& object_id,
      const std::string& reason) = 0;
};

}  // namespace dasall::memory