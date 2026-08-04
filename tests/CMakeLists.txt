cmake_minimum_required(VERSION 3.15)

find_package(GTest   REQUIRED)
find_package(Threads REQUIRED)
find_package(nlohmann_json 3.9.0 REQUIRED)
find_package(SQLite3 REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/../agent/include)

set(LIB_SOURCES
    ${CMAKE_SOURCE_DIR}/../agent/src/bt_monitor.cpp
    ${CMAKE_SOURCE_DIR}/../agent/src/kafka_producer.cpp
    ${CMAKE_SOURCE_DIR}/../agent/src/local_buffer.cpp
)

add_executable(agent_tests
    test_bt_event.cpp
    test_bt_monitor.cpp
    test_local_buffer.cpp
    ${LIB_SOURCES}
)

target_link_libraries(agent_tests PRIVATE
    GTest::GTest
    GTest::Main
    Threads::Threads
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
)

add_test(NAME AgentTests COMMAND agent_tests)
