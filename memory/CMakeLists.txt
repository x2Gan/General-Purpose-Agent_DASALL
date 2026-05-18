enable_language(C)

find_package(Threads REQUIRED)

option(DASALL_PACKAGE_SQLITE_VSS "Install sqlite-vss loadable extension assets when supported" ON)

function(dasall_validate_sqlite_autoconf_source_dir source_dir expected_version_number autoconf_pin)
  set(sqlite_header_path "${source_dir}/sqlite3.h")
  if(NOT EXISTS "${sqlite_header_path}")
    message(FATAL_ERROR
      "SQLite source dir ${source_dir} is missing sqlite3.h for autoconf ${autoconf_pin}"
    )
  endif()

  file(READ "${sqlite_header_path}" sqlite_header_contents)
  string(REGEX MATCH "#define[ 	]+SQLITE_VERSION_NUMBER[ 	]+([0-9]+)" _sqlite_version_match
    "${sqlite_header_contents}"
  )
  if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR
      "SQLite source dir ${source_dir} does not expose SQLITE_VERSION_NUMBER in sqlite3.h"
    )
  endif()

  if(NOT CMAKE_MATCH_1 STREQUAL "${expected_version_number}")
    message(FATAL_ERROR
      "SQLite source dir ${source_dir} is stale: expected SQLITE_VERSION_NUMBER=${expected_version_number} for autoconf ${autoconf_pin}, got ${CMAKE_MATCH_1}. Clear or update FETCHCONTENT_SOURCE_DIR_DASALL_SQLITE_AUTOCONF."
    )
  endif()
endfunction()

if(DASALL_PACKAGE_SQLITE_VSS)
  set(DASALL_SQLITE_VSS_VERSION "0.1.2")
  set(DASALL_SQLITE_VSS_INSTALL_DIR "lib/dasall/sqlite-vss")

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
    set(DASALL_SQLITE_VSS_CACHE_DIR
      "${DASALL_THIRD_PARTY_CACHE_DIR}/sqlite-vss/v${DASALL_SQLITE_VSS_VERSION}/linux-x86_64"
    )
    set(DASALL_SQLITE_VSS_VECTOR0_ASSET
      "${DASALL_SQLITE_VSS_CACHE_DIR}/vector0.so"
    )
    set(DASALL_SQLITE_VSS_VSS0_ASSET
      "${DASALL_SQLITE_VSS_CACHE_DIR}/vss0.so"
    )

    file(MAKE_DIRECTORY "${DASALL_SQLITE_VSS_CACHE_DIR}")

    if(NOT EXISTS "${DASALL_SQLITE_VSS_VECTOR0_ASSET}")
      file(DOWNLOAD
        "https://github.com/asg017/sqlite-vss/releases/download/v${DASALL_SQLITE_VSS_VERSION}/sqlite-vss-v${DASALL_SQLITE_VSS_VERSION}-deno-linux-x86_64.vector0.so"
        "${DASALL_SQLITE_VSS_VECTOR0_ASSET}"
        STATUS DASALL_SQLITE_VSS_VECTOR0_STATUS
        SHOW_PROGRESS
      )
      list(GET DASALL_SQLITE_VSS_VECTOR0_STATUS 0 DASALL_SQLITE_VSS_VECTOR0_STATUS_CODE)
      if(NOT DASALL_SQLITE_VSS_VECTOR0_STATUS_CODE EQUAL 0)
        list(GET DASALL_SQLITE_VSS_VECTOR0_STATUS 1 DASALL_SQLITE_VSS_VECTOR0_STATUS_TEXT)
        message(FATAL_ERROR
          "Failed to download sqlite-vss vector0 asset: ${DASALL_SQLITE_VSS_VECTOR0_STATUS_TEXT}"
        )
      endif()
    endif()

    if(NOT EXISTS "${DASALL_SQLITE_VSS_VSS0_ASSET}")
      file(DOWNLOAD
        "https://github.com/asg017/sqlite-vss/releases/download/v${DASALL_SQLITE_VSS_VERSION}/sqlite-vss-v${DASALL_SQLITE_VSS_VERSION}-deno-linux-x86_64.vss0.so"
        "${DASALL_SQLITE_VSS_VSS0_ASSET}"
        STATUS DASALL_SQLITE_VSS_VSS0_STATUS
        SHOW_PROGRESS
      )
      list(GET DASALL_SQLITE_VSS_VSS0_STATUS 0 DASALL_SQLITE_VSS_VSS0_STATUS_CODE)
      if(NOT DASALL_SQLITE_VSS_VSS0_STATUS_CODE EQUAL 0)
        list(GET DASALL_SQLITE_VSS_VSS0_STATUS 1 DASALL_SQLITE_VSS_VSS0_STATUS_TEXT)
        message(FATAL_ERROR
          "Failed to download sqlite-vss vss0 asset: ${DASALL_SQLITE_VSS_VSS0_STATUS_TEXT}"
        )
      endif()
    endif()

    install(FILES
      "${DASALL_SQLITE_VSS_VECTOR0_ASSET}"
      "${DASALL_SQLITE_VSS_VSS0_ASSET}"
      DESTINATION "${DASALL_SQLITE_VSS_INSTALL_DIR}"
    )
  else()
    message(STATUS "sqlite-vss package assets are unsupported on this platform; runtime will fail closed")
  endif()
endif()

if(NOT TARGET dasall_sqlite3)
  set(DASALL_SQLITE_AUTOCONF_VERSION 3510300)
  math(EXPR DASALL_SQLITE_VERSION_MAJOR "${DASALL_SQLITE_AUTOCONF_VERSION} / 1000000")
  math(EXPR DASALL_SQLITE_VERSION_MINOR "(${DASALL_SQLITE_AUTOCONF_VERSION} / 10000) % 100")
  math(EXPR DASALL_SQLITE_VERSION_PATCH "(${DASALL_SQLITE_AUTOCONF_VERSION} / 100) % 100")
  math(EXPR DASALL_SQLITE_EXPECTED_VERSION_NUMBER
    "${DASALL_SQLITE_VERSION_MAJOR} * 1000000 + ${DASALL_SQLITE_VERSION_MINOR} * 1000 + ${DASALL_SQLITE_VERSION_PATCH}"
  )
  set(DASALL_SQLITE_AMALGAMATION_WRAPPER
    ${CMAKE_CURRENT_SOURCE_DIR}/src/store/sqlite/SqliteAmalgamation.c
  )
  set(DASALL_SQLITE_AUTOCONF_URL
    "https://www.sqlite.org/2026/sqlite-autoconf-${DASALL_SQLITE_AUTOCONF_VERSION}.tar.gz"
  )

  FetchContent_Declare(dasall_sqlite_autoconf
    URL ${DASALL_SQLITE_AUTOCONF_URL}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_GetProperties(dasall_sqlite_autoconf)
  if(NOT dasall_sqlite_autoconf_POPULATED)
    FetchContent_Populate(dasall_sqlite_autoconf)
  endif()

  dasall_validate_sqlite_autoconf_source_dir(
    "${dasall_sqlite_autoconf_SOURCE_DIR}"
    "${DASALL_SQLITE_EXPECTED_VERSION_NUMBER}"
    "${DASALL_SQLITE_AUTOCONF_VERSION}"
  )

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
      DASALL_SQLITE_AUTOCONF_PIN=${DASALL_SQLITE_AUTOCONF_VERSION}
      SQLITE_THREADSAFE=1
      SQLITE_ENABLE_FTS5=1
      SQLITE_ENABLE_LOAD_EXTENSION=1
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
  src/observability/MemoryObservability.cpp
  src/store/sqlite/RowMappers.cpp
  src/store/sqlite/SqliteMemoryStore.cpp
  src/store/sqlite/SqliteSchemaMigrator.cpp
  src/working/WorkingMemoryBoard.cpp
  src/writeback/CompressionCoordinator.cpp
  src/writeback/WritebackCoordinator.cpp
  src/vector/SimpleLocalEmbeddingAdapter.cpp
  src/vector/SqliteVssVectorBackend.cpp
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
    dasall_infra
    dasall_sqlite3
)

dasall_apply_common_options(dasall_memory)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/sql/memory
  DESTINATION ${DASALL_INSTALL_DASALL_DATADIR}/sql
  FILES_MATCHING PATTERN "*.sql"
)
