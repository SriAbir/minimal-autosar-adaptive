cmake_minimum_required(VERSION 3.14)
project(autosar_em)

set(CMAKE_CXX_STANDARD 17)

find_package(nlohmann_json REQUIRED)

add_executable(execution_manager em/execution_manager.cpp)

target_link_libraries(execution_manager PRIVATE nlohmann_json::nlohmann_json)
