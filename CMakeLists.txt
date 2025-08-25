cmake_minimum_required(VERSION 3.14)
project(autosar_em LANGUAGES CXX)

# You had C++17; that's fine for this code. Switch to 20 if you prefer.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json REQUIRED)

# ---------- Logging library ----------
option(BUILD_WITH_DLT "Build DLT sink (requires libdlt)" ON)
option(BUILD_LOG_DEMO "Build the logging demo app" ON)

add_library(logging STATIC
  logging/src/sinks_console.cpp
  logging/src/sinks_dlt.cpp
)
target_include_directories(logging PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/logging/include
)

# Try to find DLT; if present, enable HAVE_DLT and link it
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

# Optional demo for quick manual test: ./build/log_demo
if (BUILD_LOG_DEMO)
  add_executable(log_demo logging/src/log_demo.cpp)
  target_link_libraries(log_demo PRIVATE logging)
endif()

# ---------- Our existing execution_manager ----------
add_executable(execution_manager
  em/execution_manager.cpp
)
target_link_libraries(execution_manager
  PRIVATE
    nlohmann_json::nlohmann_json
    logging                # <-- link the logging lib
)

# ---- Testing ----
include(CTest)  # defines BUILD_TESTING (ON by default)

if (BUILD_TESTING)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_executable(logging_tests
    tests/test_logging.cpp
  )

  target_link_libraries(logging_tests
    PRIVATE
      logging
      GTest::gtest_main
  )

  target_include_directories(logging_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/logging/include
  )

  include(GoogleTest)
  gtest_discover_tests(logging_tests)
endif()


