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

# ---------- ara::com core (header-only) -------------------------------------
add_library(ara_com_core INTERFACE)
# core.hpp lives under include/ so publish that root
target_include_directories(ara_com_core INTERFACE ${CMAKE_SOURCE_DIR}/include)

# ---------- SOME/IP adapter for ara::com ------------------------------------
add_library(ara_com_adapter_someip
  ara/com/someip_adapter.cpp
)
# Public headers are in include/, the adapter privately needs the binding headers in com/
target_include_directories(ara_com_adapter_someip
  PUBLIC  ${CMAKE_SOURCE_DIR}/include
  PRIVATE ${CMAKE_SOURCE_DIR}/com
)
target_link_libraries(ara_com_adapter_someip
  PUBLIC  ara_com_core
  PRIVATE someip_binding         # adapter uses the binding internally
)

# ---------- Logging library --------------------------------------------------
option(BUILD_WITH_DLT "Build DLT sink (requires libdlt)" ON)
option(BUILD_LOG_DEMO "Build the logging demo app" ON)

add_library(logging STATIC
  logging/src/sinks_console.cpp
  #logging/src/log.cpp //removed, no need for this except if a logging demo is needed
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
  # phm/src/phm_supervisor.cpp //Removed to not duplicate with phm_core
)
target_include_directories(ara_phm PUBLIC
  ${CMAKE_SOURCE_DIR}/include
  ${CMAKE_SOURCE_DIR}/phm/include
)
target_link_libraries(ara_phm
  PUBLIC
    someip_binding
    phm_core
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
  #phm/src/phm_supervisor.cpp
)

target_include_directories(execution_manager PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/em
  ${CMAKE_CURRENT_SOURCE_DIR}/phm/include
  ${CMAKE_CURRENT_SOURCE_DIR}/logging/include
  ${CMAKE_CURRENT_SOURCE_DIR}/persistency/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/apps
)

# Common includes used by apps
set(APP_PUBLIC_INCLUDES
  ${CMAKE_SOURCE_DIR}/include          # ara/...
  ${CMAKE_SOURCE_DIR}/logging/include  # log.hpp
  ${CMAKE_SOURCE_DIR}/persistency/include
  ${CMAKE_SOURCE_DIR}/phm/include
  # ${CMAKE_SOURCE_DIR}/com //Removed to not expose binding details to apps
  ${CMAKE_SOURCE_DIR}/services
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
# These are independent of the rest of the project, just using the binding
# Removed: Re-enable with build command to show the use of ara::com with SOME/IP directly
# (not recommended for real use, prefer the adapter instead)
option(BUILD_INTERNAL_DEMOS "Build raw SOME/IP and logging demos" OFF)
if (BUILD_INTERNAL_DEMOS)
  add_executable(someip_provider apps/someip_provider.cpp)
  target_link_libraries(someip_provider PRIVATE 
    someip_binding
    PkgConfig::VSOMEIP
    Threads::Threads
  )
endif()

# ----------- Apps for demos / etc. ------------------------------------------

# --- sensor_provider ---
add_executable(sensor_provider apps/sensor_provider.cpp)
target_include_directories(sensor_provider PRIVATE ${APP_PUBLIC_INCLUDES})
target_link_libraries(sensor_provider PRIVATE
  logging
  persistency
  ara_phm
  ara_com_adapter_someip    # <-- use the adapter
  # removed: someip_binding PkgConfig::VSOMEIP Threads::Threads
)

# --- speed_client ---
add_executable(speed_client apps/speed_client.cpp)
target_include_directories(speed_client PRIVATE ${APP_PUBLIC_INCLUDES})
target_link_libraries(speed_client PRIVATE
  logging
  persistency
  ara_phm
  ara_com_adapter_someip    # <-- use the adapter
  # removed: someip_binding PkgConfig::VSOMEIP Threads::Threads
)

# ============================================================================
#                       Diagnostics / CAN
# ============================================================================

# Diagnostics provider lib
add_library(function_diag_provider
  diagnostics/provider/ifunction_diag_provider.hpp
  diagnostics/provider/generic_provider.hpp
  diagnostics/provider/generic_provider.cpp
  diagnostics/uds/uds.hpp
  diagnostics/uds/uds.cpp
)
target_include_directories(function_diag_provider
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}         # so you can #include "diagnostics/..."
)

# Diagnostics transport executable
# Self-contained facade (only this file)
add_library(ara_diag
  ara/diag/diag_server.cpp
)
target_include_directories(ara_diag PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(ara_diag PUBLIC cxx_std_17)


# CAN bus API (header-only interface)
add_library(function_bus_api INTERFACE)
target_include_directories(function_bus_api
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/can_gateway/include
)

# CAN gateway executable
add_executable(can_gateway
  can_gateway/src/gateway.cpp
  can_gateway/src/main.cpp
)
target_include_directories(can_gateway
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/can_gateway/include
)
target_link_libraries(can_gateway PRIVATE function_bus_api)

# ============================================================================
#                           Stubs / Mocks
# ============================================================================

add_executable(ui_stub stubs/ui_stub.cpp)
add_executable(position_stub stubs/position_stub.cpp)
add_executable(cloud_stub stubs/cloud_stub.cpp)


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
  target_include_directories(logging_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/logging/include)

  add_executable(persistency_tests tests/test_persistency.cpp)
  target_link_libraries(persistency_tests PRIVATE persistency GTest::gtest_main)
  target_include_directories(persistency_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/persistency/include)

  add_executable(phm_supervisor_tests tests/test_phm_supervisor.cpp)
  target_link_libraries(phm_supervisor_tests PRIVATE GTest::gtest_main Threads::Threads phm_core)
  target_include_directories(phm_supervisor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/phm/include)

  include(GoogleTest)
  gtest_discover_tests(logging_tests)
  gtest_discover_tests(persistency_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    PROPERTIES TIMEOUT 20 DISCOVERY_TIMEOUT 60
  )
  gtest_discover_tests(phm_supervisor_tests
    PROPERTIES TIMEOUT 20 DISCOVERY_TIMEOUT 60
  )

  # Speed logic tests
  #add_executable(test_speed_logic tests/test_speed_logic.cpp)
  #target_link_libraries(test_speed_logic PRIVATE gtest gtest_main pthread)
  # Link your persistency objects if they build as a lib; otherwise add sources:
  # target_link_libraries(test_speed_logic PRIVATE persistency_lib)
  # or:
  # target_sources(test_speed_logic PRIVATE
  #   persistency/src/file_storage.cpp
  #   persistency/src/key_value_storage_backend.cpp
  #   persistency/src/key_value_storage_facade.cpp
  #   persistency/src/storage_registry.cpp
  # )

  # Sensor logic tests
  #add_executable(test_sensor_logic tests/test_sensor_logic.cpp)
  #target_link_libraries(test_sensor_logic PRIVATE gtest gtest_main pthread)

  #add_test(NAME speed_logic COMMAND test_speed_logic)
  #add_test(NAME sensor_logic COMMAND test_sensor_logic)

endif()
