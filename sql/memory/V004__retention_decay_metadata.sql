ALTER TABLE facts ADD COLUMN last_accessed_at INTEGER NOT NULL DEFAULT 0;
ALTER TABLE facts ADD COLUMN hit_count INTEGER NOT NULL DEFAULT 1;

UPDATE facts
SET last_accessed_at = COALESCE(NULLIF(created_at, 0), last_accessed_at)
WHERE last_accessed_at = 0;

UPDATE facts
SET hit_count = 1
WHERE hit_count <= 0;

ALTER TABLE experiences ADD COLUMN last_accessed_at INTEGER NOT NULL DEFAULT 0;
ALTER TABLE experiences ADD COLUMN hit_count INTEGER NOT NULL DEFAULT 1;

UPDATE experiences
SET last_accessed_at = COALESCE(NULLIF(created_at, 0), last_accessed_at)
WHERE last_accessed_at = 0;

UPDATE experiences
SET hit_count = 1
WHERE hit_count <= 0;