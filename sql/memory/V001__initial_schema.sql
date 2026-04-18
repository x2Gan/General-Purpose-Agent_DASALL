PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS sessions (
  session_id TEXT PRIMARY KEY,
  user_id TEXT,
  latest_summary_memory_ref TEXT,
  metadata_digest TEXT,
  turn_ids_json TEXT NOT NULL DEFAULT '[]',
  created_at INTEGER NOT NULL,
  last_active_at INTEGER,
  tags_json TEXT NOT NULL DEFAULT '[]'
);

CREATE TABLE IF NOT EXISTS turns (
  turn_id TEXT PRIMARY KEY,
  session_id TEXT NOT NULL,
  user_input TEXT NOT NULL,
  agent_response TEXT,
  tool_call_refs_json TEXT NOT NULL DEFAULT '[]',
  observation_refs_json TEXT NOT NULL DEFAULT '[]',
  summary_memory_ref TEXT,
  created_at INTEGER NOT NULL,
  tags_json TEXT NOT NULL DEFAULT '[]',
  FOREIGN KEY(session_id) REFERENCES sessions(session_id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS summaries (
  summary_id TEXT PRIMARY KEY,
  session_id TEXT NOT NULL,
  summary_text TEXT NOT NULL,
  source_turn_ids_json TEXT NOT NULL DEFAULT '[]',
  decisions_made_json TEXT NOT NULL DEFAULT '[]',
  confirmed_facts_json TEXT NOT NULL DEFAULT '[]',
  tool_outcomes_json TEXT NOT NULL DEFAULT '[]',
  created_at INTEGER NOT NULL,
  tags_json TEXT NOT NULL DEFAULT '[]',
  FOREIGN KEY(session_id) REFERENCES sessions(session_id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS facts (
  fact_id TEXT PRIMARY KEY,
  session_id TEXT,
  user_id TEXT,
  fact_text TEXT NOT NULL,
  source_turn_ids_json TEXT NOT NULL DEFAULT '[]',
  confidence_score INTEGER NOT NULL DEFAULT 0,
  fact_type TEXT NOT NULL DEFAULT '',
  validity_ref TEXT,
  evidence_digest TEXT,
  superseded_by_fact_id TEXT,
  created_at INTEGER NOT NULL,
  tags_json TEXT NOT NULL DEFAULT '[]'
);

CREATE TABLE IF NOT EXISTS experiences (
  experience_id TEXT PRIMARY KEY,
  session_id TEXT,
  user_id TEXT,
  lesson_summary TEXT NOT NULL,
  trigger_condition TEXT NOT NULL,
  recommended_action TEXT NOT NULL,
  source_turn_ids_json TEXT NOT NULL DEFAULT '[]',
  effectiveness_score INTEGER NOT NULL DEFAULT 0,
  applicable_domains_json TEXT NOT NULL DEFAULT '[]',
  risk_notes_json TEXT NOT NULL DEFAULT '[]',
  expires_at INTEGER,
  superseded_by_experience_id TEXT,
  created_at INTEGER NOT NULL,
  tags_json TEXT NOT NULL DEFAULT '[]'
);

CREATE TABLE IF NOT EXISTS quarantined_records (
  quarantine_id INTEGER PRIMARY KEY AUTOINCREMENT,
  object_type TEXT NOT NULL,
  object_id TEXT NOT NULL,
  reason TEXT NOT NULL,
  payload_digest TEXT,
  created_at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_turns_session_created_at
  ON turns(session_id, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_summaries_session_created_at
  ON summaries(session_id, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_facts_session_fact_type_created_at
  ON facts(session_id, fact_type, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_experiences_session_created_at
  ON experiences(session_id, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_quarantined_records_created_at
  ON quarantined_records(created_at DESC);
