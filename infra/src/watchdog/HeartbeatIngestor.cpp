#include "watchdog/HeartbeatIngestor.h"

#include <string>
#include <string_view>

namespace dasall::infra::watchdog {
namespace {

constexpr std::string_view kHeartbeatIngestorSourceRef = "HeartbeatIngestor";

[[nodiscard]] HeartbeatIngestResult make_ingest_failure(
    std::optional<WatchdogErrorCode> watchdog_code,
    contracts::ResultCode result_code,
    std::string message,
    std::string stage) {
  return HeartbeatIngestResult::failure(
      watchdog_code,
      result_code,
      std::move(message),
      std::move(stage),
      std::string(kHeartbeatIngestorSourceRef));
}

[[nodiscard]] HeartbeatSampleQueryResult make_query_failure(
    std::optional<WatchdogErrorCode> watchdog_code,
    contracts::ResultCode result_code,
    std::string message,
    std::string stage) {
  return HeartbeatSampleQueryResult::failure(
      watchdog_code,
      result_code,
      std::move(message),
      std::move(stage),
      std::string(kHeartbeatIngestorSourceRef));
}

}  // namespace

void HeartbeatIngestor::bind_registry(const HeartbeatRegistry* registry) {
  registry_ = registry;
}

void HeartbeatIngestor::set_max_tracked_entities(std::size_t max_tracked_entities) {
  max_tracked_entities_ = max_tracked_entities;
}

HeartbeatIngestResult HeartbeatIngestor::ingest(const HeartbeatSample& sample) {
  if (!sample.has_required_fields()) {
    record_failure(sample.entity_id,
                   std::nullopt,
                   contracts::ResultCode::ValidationFieldMissing,
                   false);
    return make_ingest_failure(
        std::nullopt,
        contracts::ResultCode::ValidationFieldMissing,
        "heartbeat ingestor requires entity_id, timestamps, deadline_ts, and seq_no",
        "watchdog.ingestor.ingest");
  }

  if (registry_ == nullptr) {
    record_failure(sample.entity_id,
                   std::nullopt,
                   contracts::ResultCode::RuntimeRetryExhausted,
                   false);
    return make_ingest_failure(
        std::nullopt,
        contracts::ResultCode::RuntimeRetryExhausted,
        "heartbeat ingestor is not bound to a registry yet",
        "watchdog.ingestor.ingest");
  }

  const auto query = registry_->query_entity(sample.entity_id);
  if (!query.ok) {
    record_failure(sample.entity_id, query.watchdog_code, query.result_code, false);
    return make_ingest_failure(query.watchdog_code,
                               *query.result_code,
                               query.error->details.message,
                               "watchdog.ingestor.ingest");
  }

  const auto current = latest_samples_.find(sample.entity_id);
  if (current != latest_samples_.end() && sample.is_stale_against(current->second)) {
    const auto mapping = map_watchdog_error_code(WatchdogErrorCode::HeartbeatStale);
    record_failure(sample.entity_id,
                   WatchdogErrorCode::HeartbeatStale,
                   mapping.result_code,
                   true);
    return make_ingest_failure(
        WatchdogErrorCode::HeartbeatStale,
        mapping.result_code,
        std::string(watchdog_error_code_name(WatchdogErrorCode::HeartbeatStale)) +
            ": heartbeat ingestor rejected a stale sample",
        "watchdog.ingestor.ingest");
  }

  if (current == latest_samples_.end() && latest_samples_.size() >= max_tracked_entities_) {
    record_failure(sample.entity_id,
                   std::nullopt,
                   contracts::ResultCode::ValidationFieldMissing,
                   false);
    return make_ingest_failure(
        std::nullopt,
        contracts::ResultCode::ValidationFieldMissing,
        "heartbeat ingestor rejected the sample because max_tracked_entities was reached",
        "watchdog.ingestor.ingest");
  }

  latest_samples_[sample.entity_id] = sample;
  record_success(sample.entity_id);
  return HeartbeatIngestResult::success(sample);
}

HeartbeatSampleQueryResult HeartbeatIngestor::latest_sample(
    std::string_view entity_id) const {
  if (entity_id.empty()) {
    return make_query_failure(
        std::nullopt,
        contracts::ResultCode::ValidationFieldMissing,
        "heartbeat ingestor requires a non-empty entity_id for latest_sample",
        "watchdog.ingestor.query");
  }

  const auto sample = latest_samples_.find(std::string(entity_id));
  if (sample == latest_samples_.end()) {
    const auto mapping = map_watchdog_error_code(WatchdogErrorCode::EntityNotFound);
    return make_query_failure(
        WatchdogErrorCode::EntityNotFound,
        mapping.result_code,
        std::string(watchdog_error_code_name(WatchdogErrorCode::EntityNotFound)) +
            ": heartbeat ingestor has no sample for the requested entity_id",
        "watchdog.ingestor.query");
  }

  return HeartbeatSampleQueryResult::success(sample->second);
}

HeartbeatIngestStatus HeartbeatIngestor::status() const {
  return HeartbeatIngestStatus{
      .accepted_total = accepted_total_,
      .stale_drop_total = stale_drop_total_,
      .rejected_total = rejected_total_,
      .last_entity_id = last_entity_id_,
      .last_watchdog_code = last_watchdog_code_,
      .last_result_code = last_result_code_,
  };
}

std::size_t HeartbeatIngestor::tracked_entity_count() const {
  return latest_samples_.size();
}

void HeartbeatIngestor::forget_entity(std::string_view entity_id) {
  if (!entity_id.empty()) {
    latest_samples_.erase(std::string(entity_id));
  }
}

void HeartbeatIngestor::record_success(std::string_view entity_id) {
  ++accepted_total_;
  last_entity_id_ = std::string(entity_id);
  last_watchdog_code_.reset();
  last_result_code_.reset();
}

void HeartbeatIngestor::record_failure(
    std::string_view entity_id,
    std::optional<WatchdogErrorCode> watchdog_code,
    std::optional<contracts::ResultCode> result_code,
    bool stale_drop) {
  ++rejected_total_;
  if (stale_drop) {
    ++stale_drop_total_;
  }

  last_entity_id_ = std::string(entity_id);
  last_watchdog_code_ = watchdog_code;
  last_result_code_ = result_code;
}

}  // namespace dasall::infra::watchdog