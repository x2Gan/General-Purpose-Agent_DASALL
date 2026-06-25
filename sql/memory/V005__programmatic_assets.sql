CREATE TABLE IF NOT EXISTS programmatic_assets (
  asset_ref TEXT PRIMARY KEY,
  session_id TEXT NOT NULL,
  source_turn_id TEXT NOT NULL,
  content_digest TEXT NOT NULL,
  lease_expires_at INTEGER NOT NULL,
  tags_json TEXT NOT NULL DEFAULT '[]',
  created_at INTEGER NOT NULL,
  updated_at INTEGER NOT NULL,
  FOREIGN KEY(session_id) REFERENCES sessions(session_id) ON DELETE CASCADE,
  FOREIGN KEY(source_turn_id) REFERENCES turns(turn_id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_programmatic_assets_session_id
  ON programmatic_assets(session_id, updated_at DESC);

CREATE INDEX IF NOT EXISTS idx_programmatic_assets_lease_expires_at
  ON programmatic_assets(lease_expires_at);