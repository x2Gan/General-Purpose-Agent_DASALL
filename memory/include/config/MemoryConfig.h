#pragma once

#include <cstdint>
#include <string>

namespace dasall::memory {

struct StorageConfig {
  std::string backend = "sqlite";
  std::string db_path = "memory.db";
  std::string journal_mode = "WAL";
  std::string synchronous = "NORMAL";
  int wal_autocheckpoint_pages = 1000;
  int busy_timeout_ms = 50;
  int writer_retry_count = 2;
  std::string checkpoint_mode = "PASSIVE";
  int reader_pool_size = 2;
  std::string migrations_dir = "sql/memory/";
};

struct ContextConfig {
  int recent_turn_limit = 8;
  int compression_trigger_turns = 12;
  int max_summary_candidates = 3;
  int fact_confidence_floor = 80;
  double compression_trigger_ratio = 0.85;
};

struct ExperienceConfig {
  int effectiveness_floor = 60;
};

struct VectorConfig {
  bool enabled = false;
  std::string backend_type = "sqlite-vss";
  int search_top_k = 5;
};

struct MaintenanceConfig {
  int retention_turns = 200;
  bool quarantine_enabled = true;
  std::int64_t quarantine_ttl_ms = 604800000;
  std::int64_t fact_ttl_ms = 0;
  std::int64_t experience_ttl_ms = 0;
  bool auto_schedule = false;
  int schedule_interval_ms = 60000;
};

struct MemoryConfig {
  StorageConfig storage;
  ContextConfig context;
  ExperienceConfig experience;
  VectorConfig vector;
  MaintenanceConfig maintenance;
};

}  // namespace dasall::memory