cmake_minimum_required(VERSION 3.14)
project(autosar_em LANGUAGES CXX)

# ---- Toolchain / language ---------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---- Dependencies -----------------------------------------------------------
find_package(PkgConfig REQUIRED)
# Use IMPORTED_TARGET so we can link the target directly
pkg_check_modules(VSOMEIP REQUIRED IMPORTED_TARGET vsomeip3)

find_package(Threads REQUIRED)
find_package(nlohmann_json REQUIRED)

# ---- Include roots for project headers -------------------------------------
include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/include          # e.g., include/ara/...
  ${CMAKE_CURRENT_SOURCE_DIR}                  # if some headers live next to sources
)

# ============================================================================
#                               LIBRARIES
# ============================================================================

# ---------- SOME/IP shim -----------------------------------------------------
add_library(someip_binding
  com/someip_binding.cpp
  com/someip_binding.hpp
)
target_include_directories(someip_binding PUBLIC com)
target_link_libraries(someip_binding
  PUBLIC
    PkgConfig::VSOMEIP
    Threads::Threads
)

# ---------- Logging library --------------------------------------------------
option(BUILD_WITH_DLT "Build DLT sink (requires libdlt)" ON)
option(BUILD_LOG_DEMO "Build the logging demo app" ON)

add_library(logging STATIC
  logging/src/sinks_console.cpp
  logging/src/sinks_dlt.cpp
)
target_include_directories(logging PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/logging/include
)

if (BUILD_WITH_DLT)
  find_path(DLT_INCLUDE_DIR NAMES dlt/dlt_user.h)
  find_library(DLT_LIBRARY NAMES dlt)
  if (DLT_INCLUDE_DIR AND DLT_LIBRARY)
    target_compile_definitions(logging PUBLIC HAVE_DLT)
    target_include_directories(logging PUBLIC ${DLT_INCLUDE_DIR})
    target_link_libraries(logging PUBLIC ${DLT_LIBRARY})
  else()
    message(WARNING "DLT not found; building logging without DLT support")
  endif()
endif()

if (BUILD_LOG_DEMO)
  add_executable(log_demo logging/src/log_demo.cpp)
  target_link_libraries(log_demo PRIVATE logging)
endif()

# ---------- Persistency library ---------------------------------------------
add_library(persistency STATIC
  persistency/src/storage_registry.cpp
  persistency/src/key_value_storage_backend.cpp
  persistency/src/key_value_storage_facade.cpp
  persistency/src/file_storage.cpp
)

target_link_libraries(persistency PUBLIC nlohmann_json::nlohmann_json)

target_include_directories(persistency PUBLIC
  ${CMAKE_SOURCE_DIR}/include                 # gives <ara/...> if you keep common headers here
  ${CMAKE_SOURCE_DIR}/persistency/include     # gives <persistency/...>
)

# ---------- ara_phm client (apps call ReportAlive/Checkpoint) ---------------
add_library(ara_phm STATIC
  phm/src/supervision_client.cpp
  #phm/src/phm_supervisor.cpp
)
target_include_directories(ara_phm PUBLIC
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/phm/include
)
target_link_libraries(ara_phm
  PUBLIC
    someip_binding
    PkgConfig::VSOMEIP
    Threads::Threads
)

# ---------- PHM core (supervisor logic) ----------
add_library(phm_core STATIC
  phm/src/phm_supervisor.cpp
)
target_include_directories(phm_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/phm/include
)


# ============================================================================
#                               EXECUTABLES
# ============================================================================

# ---------- Execution Manager (PHM server inside) ---------------------------
add_executable(execution_manager
  em/execution_manager.cpp
  phm/src/phm_supervisor.cpp
)

target_include_directories(execution_manager PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/em
  ${CMAKE_CURRENT_SOURCE_DIR}/phm/include
  ${CMAKE_CURRENT_SOURCE_DIR}/logging/include
  ${CMAKE_CURRENT_SOURCE_DIR}/persistency/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(execution_manager
  PRIVATE
    persistency
    logging
    nlohmann_json::nlohmann_json
    someip_binding           # offers PHM service via vsomeip
    PkgConfig::VSOMEIP
    Threads::Threads
    phm_core
)

# ---------- Example SOME/IP apps (keep your current samples) ----------------
add_executable(someip_provider apps/someip_provider.cpp)
target_link_libraries(someip_provider PRIVATE 
  someip_binding 
  PkgConfig::VSOMEIP 
  Threads::Threads
)

add_executable(sensor_consumer apps/sensor_consumer.cpp)
target_link_libraries(sensor_consumer PRIVATE someip_binding PkgConfig::VSOMEIP Threads::Threads)

# ============================================================================
#                                 TESTS
# ============================================================================

include(CTest)

if (BUILD_TESTING)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_executable(logging_tests tests/test_logging.cpp)
  target_link_libraries(logging_tests PRIVATE logging GTest::gtest_main)

  add_executable(persistency_tests tests/test_persistency.cpp)
  target_link_libraries(persistency_tests PRIVATE persistency GTest::gtest_main)

  add_executable(phm_supervisor_tests tests/test_phm_supervisor.cpp)
  target_link_libraries(phm_supervisor_tests
  PRIVATE
    GTest::gtest_main
    Threads::Threads
    phm_core
  )

  target_include_directories(phm_supervisor_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/phm/include)

  target_include_directories(persistency_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/persistency/include)
  target_include_directories(logging_tests     PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/logging/include)

  # Provide manifest path to tests
  set(PERSIST_MANIFEST "${CMAKE_SOURCE_DIR}/manifests/persistency.json")
  target_compile_definitions(persistency_tests PRIVATE K_MANIFEST_PATH="${PERSIST_MANIFEST}")

  include(GoogleTest)
  gtest_discover_tests(logging_tests)
  gtest_discover_tests(persistency_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    PROPERTIES TIMEOUT 20
    DISCOVERY_TIMEOUT 60
  )
  gtest_discover_tests(phm_supervisor_tests
  PROPERTIES TIMEOUT 20
  DISCOVERY_TIMEOUT 60
)
endif()
