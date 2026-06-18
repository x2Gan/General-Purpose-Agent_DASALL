ALTER TABLE summaries
  ADD COLUMN summary_parent_id TEXT;

CREATE INDEX IF NOT EXISTS idx_summaries_session_parent_created_at
  ON summaries(session_id, summary_parent_id, created_at DESC);

CREATE INDEX IF NOT EXISTS idx_summaries_parent_id
  ON summaries(summary_parent_id);