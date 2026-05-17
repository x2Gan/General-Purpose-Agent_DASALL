CREATE TABLE IF NOT EXISTS memory_vector_documents (
  rowid INTEGER PRIMARY KEY AUTOINCREMENT,
  doc_id TEXT NOT NULL UNIQUE,
  doc_type TEXT NOT NULL,
  text_snippet TEXT NOT NULL,
  embedding_json TEXT NOT NULL,
  updated_at INTEGER NOT NULL
);