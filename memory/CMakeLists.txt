enable_language(C)

find_package(Threads REQUIRED)

if(NOT TARGET dasall_sqlite3)
  set(DASALL_SQLITE_AUTOCONF_VERSION 3460100)
  set(DASALL_SQLITE_AMALGAMATION_WRAPPER
    ${CMAKE_CURRENT_SOURCE_DIR}/src/store/sqlite/SqliteAmalgamation.c
  )
  set(DASALL_SQLITE_AUTOCONF_URL
    "https://www.sqlite.org/2024/sqlite-autoconf-${DASALL_SQLITE_AUTOCONF_VERSION}.tar.gz"
  )

  FetchContent_Declare(dasall_sqlite_autoconf
    URL ${DASALL_SQLITE_AUTOCONF_URL}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_GetProperties(dasall_sqlite_autoconf)
  if(NOT dasall_sqlite_autoconf_POPULATED)
    FetchContent_Populate(dasall_sqlite_autoconf)
  endif()

  add_library(dasall_sqlite3 STATIC
    ${DASALL_SQLITE_AMALGAMATION_WRAPPER}
  )
  set_source_files_properties(
    ${DASALL_SQLITE_AMALGAMATION_WRAPPER}
    PROPERTIES LANGUAGE C
  )

  target_include_directories(dasall_sqlite3
    PUBLIC
      ${dasall_sqlite_autoconf_SOURCE_DIR}
  )

  target_compile_definitions(dasall_sqlite3
    PUBLIC
      SQLITE_THREADSAFE=1
      SQLITE_ENABLE_FTS5=1
      SQLITE_OMIT_LOAD_EXTENSION=1
  )

  target_link_libraries(dasall_sqlite3
    PUBLIC
      Threads::Threads
      ${CMAKE_DL_LIBS}
      m
  )
endif()

set(DASALL_MEMORY_PUBLIC_INCLUDE_DIR
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(DASALL_MEMORY_PUBLIC_INCLUDE_SUBDIRECTORIES
  config
  context
  error
  store
  vector
  working
  writeback
)

set(DASALL_MEMORY_SKELETON_SOURCES
  src/MemoryBuildSkeleton.cpp
  src/MemoryManager.cpp
  src/MemoryManagerFactory.cpp
  src/config/MemoryConfigProjector.cpp
  src/conflict/MemoryConflictResolver.cpp
  src/context/BudgetAllocator.cpp
  src/context/CandidateCollector.cpp
  src/context/ContextOrchestrator.cpp
  src/maintenance/MemoryMaintenanceWorker.cpp
  src/store/sqlite/RowMappers.cpp
  src/store/sqlite/SqliteMemoryStore.cpp
  src/store/sqlite/SqliteSchemaMigrator.cpp
  src/working/WorkingMemoryBoard.cpp
  src/writeback/CompressionCoordinator.cpp
  src/writeback/WritebackCoordinator.cpp
  src/vector/UnavailableVectorMemoryIndexAdapter.cpp
)

if(NOT IS_DIRECTORY ${DASALL_MEMORY_PUBLIC_INCLUDE_DIR})
  message(FATAL_ERROR "dasall_memory requires a public include root at ${DASALL_MEMORY_PUBLIC_INCLUDE_DIR}")
endif()

foreach(subdirectory IN LISTS DASALL_MEMORY_PUBLIC_INCLUDE_SUBDIRECTORIES)
  if(NOT IS_DIRECTORY ${DASALL_MEMORY_PUBLIC_INCLUDE_DIR}/${subdirectory})
    message(FATAL_ERROR "dasall_memory requires public include subdirectory ${DASALL_MEMORY_PUBLIC_INCLUDE_DIR}/${subdirectory}")
  endif()
endforeach()

foreach(source_file IN LISTS DASALL_MEMORY_SKELETON_SOURCES)
  if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${source_file})
    message(FATAL_ERROR "dasall_memory skeleton source is missing: ${source_file}")
  endif()
endforeach()

add_library(dasall_memory STATIC)

target_sources(dasall_memory
  PRIVATE
    ${DASALL_MEMORY_SKELETON_SOURCES}
)

target_compile_features(dasall_memory PUBLIC cxx_std_20)

target_include_directories(dasall_memory
  PUBLIC
    $<BUILD_INTERFACE:${DASALL_MEMORY_PUBLIC_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_SOURCE_DIR}/profiles/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(dasall_memory
  PUBLIC
    dasall_contracts
  PRIVATE
    dasall_sqlite3
)

dasall_apply_common_options(dasall_memory)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/sql/memory
  DESTINATION ${DASALL_INSTALL_DASALL_DATADIR}/sql
  FILES_MATCHING PATTERN "*.sql"
)
