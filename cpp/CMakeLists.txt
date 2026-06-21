cmake_minimum_required(VERSION 3.16)
project(orderbook CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Source-existence checks. Fail loud at configure time instead of producing
# confusing "no sources given to target" errors later.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/OrderBook.cpp")
    message(FATAL_ERROR "Missing src/OrderBook.cpp")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/EventGenerator.cpp")
    message(FATAL_ERROR "Missing src/EventGenerator.cpp")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
    message(FATAL_ERROR "Missing src/main.cpp")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/benchmark.cpp")
    message(FATAL_ERROR "Missing src/benchmark.cpp")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_orderbook.cpp")
    message(FATAL_ERROR "Missing tests/test_orderbook.cpp")
endif()

include_directories(include)

# Core library: shared compiled code used by all three executables.
add_library(orderbook_core STATIC
    src/OrderBook.cpp
    src/EventGenerator.cpp
)

# Main simulation executable — writes midprices.csv and trades.csv.
add_executable(orderbook src/main.cpp)
target_link_libraries(orderbook PRIVATE orderbook_core)

# Benchmark executable — measures throughput and per-event latency.
add_executable(benchmark src/benchmark.cpp)
target_link_libraries(benchmark PRIVATE orderbook_core)

# Test executable — 15 sanity + matching tests.
add_executable(test_orderbook tests/test_orderbook.cpp)
target_link_libraries(test_orderbook PRIVATE orderbook_core)

enable_testing()
add_test(NAME test_orderbook COMMAND test_orderbook)