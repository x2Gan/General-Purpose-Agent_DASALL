/* Keep the SQLite amalgamation behind a local translation unit so package builds
 * don't rely on object paths derived from external cache directories. */
#include "sqlite3.c"