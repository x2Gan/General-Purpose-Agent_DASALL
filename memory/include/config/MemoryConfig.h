#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace dasall::memory {

// --- L1: Type-safe enums for config fields that were previously raw strings ---

enum class StorageBackend { Sqlite, Memory };

inline constexpr std::string_view to_string_view(StorageBackend value) {
  switch (value) {
    case StorageBackend::Sqlite: return "sqlite";
    case StorageBackend::Memory: return "memory";
  }
  return "sqlite";
}

enum class JournalMode { Delete, Truncate, Persist, Memory, Wal, Off };

inline constexpr std::string_view to_string_view(JournalMode value) {
  switch (value) {
    case JournalMode::Delete:   return "DELETE";
    case JournalMode::Truncate: return "TRUNCATE";
    case JournalMode::Persist:  return "PERSIST";
    case JournalMode::Memory:   return "MEMORY";
    case JournalMode::Wal:      return "WAL";
    case JournalMode::Off:      return "OFF";
  }
  return "WAL";
}

enum class SynchronousMode { Off, Normal, Full, Extra };

inline constexpr std::string_view to_string_view(SynchronousMode value) {
  switch (value) {
    case SynchronousMode::Off:    return "OFF";
    case SynchronousMode::Normal: return "NORMAL";
    case SynchronousMode::Full:   return "FULL";
    case SynchronousMode::Extra:  return "EXTRA";
  }
  return "NORMAL";
}

enum class CheckpointMode { Passive, Full, Restart, Truncate };

inline constexpr std::string_view to_string_view(CheckpointMode value) {
  switch (value) {
    case CheckpointMode::Passive:  return "PASSIVE";
    case CheckpointMode::Full:     return "FULL";
    case CheckpointMode::Restart:  return "RESTART";
    case CheckpointMode::Truncate: return "TRUNCATE";
  }
  return "PASSIVE";
}

enum class VectorBackend { SqliteVss, None };

inline constexpr std::string_view to_string_view(VectorBackend value) {
  switch (value) {
    case VectorBackend::SqliteVss: return "sqlite-vss";
    case VectorBackend::None:      return "none";
  }
  return "none";
}

// --- Config structs ---

struct StorageConfig {
  StorageBackend backend = StorageBackend::Sqlite;
  std::string db_path = "memory.db";
  JournalMode journal_mode = JournalMode::Wal;
  SynchronousMode synchronous = SynchronousMode::Normal;
  int wal_autocheckpoint_pages = 1000;
  int busy_timeout_ms = 50;
  int writer_retry_count = 2;
  CheckpointMode checkpoint_mode = CheckpointMode::Passive;
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
  VectorBackend backend_type = VectorBackend::SqliteVss;
  int search_top_k = 5;
};

struct MaintenanceConfig {
  int retention_turns = 200;
  bool quarantine_enabled = true;
  /// Zero means no TTL (never expire).
  std::int64_t quarantine_ttl_ms = 604800000;
  /// Zero means no TTL (facts never expire by age alone).
  std::int64_t fact_ttl_ms = 0;
  /// Zero means no TTL (experiences never expire by age alone).
  std::int64_t experience_ttl_ms = 0;
  bool auto_schedule = false;
  /// L2: Unified to int64_t for consistency with other *_ms fields.
  std::int64_t schedule_interval_ms = 60000;
};

struct MemoryConfig {
  StorageConfig storage;
  ContextConfig context;
  ExperienceConfig experience;
  VectorConfig vector;
  MaintenanceConfig maintenance;
};

}  // namespace dasall::memory