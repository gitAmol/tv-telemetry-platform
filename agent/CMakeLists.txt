cmake_minimum_required(VERSION 3.15)
project(tv-bt-agent VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_KAFKA "Build with librdkafka" ON)
option(BUILD_TESTS  "Build unit tests"      ON)

find_package(Threads      REQUIRED)
find_package(nlohmann_json 3.9.0 REQUIRED)
find_package(SQLite3      REQUIRED)

include_directories(include)

set(AGENT_SOURCES
    src/main.cpp
    src/bt_monitor.cpp
    src/kafka_producer.cpp
    src/local_buffer.cpp
)

add_executable(tv-bt-agent ${AGENT_SOURCES})

target_link_libraries(tv-bt-agent PRIVATE
    Threads::Threads
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
)

if(ENABLE_KAFKA)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RDKAFKA REQUIRED rdkafka++)
    target_compile_definitions(tv-bt-agent PRIVATE ENABLE_KAFKA)
    target_include_directories(tv-bt-agent PRIVATE ${RDKAFKA_INCLUDE_DIRS})
    target_link_libraries(tv-bt-agent PRIVATE ${RDKAFKA_LIBRARIES})
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(../tests tests_build)
endif()

install(TARGETS tv-bt-agent RUNTIME DESTINATION bin)

message(STATUS "")
message(STATUS "tv-bt-agent build config:")
message(STATUS "  Kafka:  ${ENABLE_KAFKA}")
message(STATUS "  Tests:  ${BUILD_TESTS}")
message(STATUS "")
