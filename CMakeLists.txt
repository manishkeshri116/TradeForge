
cmake_minimum_required(VERSION 3.20)

project(TradingEngine
    VERSION 1.0.0
    LANGUAGES CXX
)

find_package(SQLite3 REQUIRED)
find_package(Qt6 6.12 REQUIRED COMPONENTS Widgets Concurrent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# Main Trading Engine
add_executable(trading_engine
    src/main.cpp
)

target_include_directories(trading_engine PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(trading_engine
    PRIVATE
    SQLite3::SQLite3
)

target_compile_options(trading_engine PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# Dark Qt Widgets trading terminal. The CLI remains a separate executable.
add_executable(trading_terminal
    src/trading_terminal.cpp
)

target_include_directories(trading_terminal PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(trading_terminal
    PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
    SQLite3::SQLite3
)

target_compile_options(trading_terminal PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# Unit Tests
add_executable(trading_engine_tests
    tests/OrderBookTests.cpp
    tests/ExchangeTests.cpp
    tests/TradeRepositoryTests.cpp
    tests/TradingAnalyticsTests.cpp
)

target_include_directories(trading_engine_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(trading_engine_tests
    PRIVATE
    GTest::gtest_main
    SQLite3::SQLite3
)

include(GoogleTest)

gtest_discover_tests(trading_engine_tests)
