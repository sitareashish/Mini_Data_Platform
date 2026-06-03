cmake_minimum_required(VERSION 3.14)
project(MiniDataPlatform VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
if(MSVC)
    add_compile_options(/W4 /WX- /O2)
else()
    add_compile_options(-Wall -Wextra -O2 -pthread)
endif()

# Include directories
include_directories(include)

# Source files (shared library)
set(MINIDB_SOURCES
    src/btree.cpp
    src/memory_pool.cpp
    src/schema.cpp
    src/storage.cpp
    src/parser.cpp
    src/executor.cpp
    src/llm_copilot.cpp
)

# ─── Main executable ────────────────────────────────────────────────────────
add_executable(minidb
    src/main.cpp
    ${MINIDB_SOURCES}
)

# ─── Test executable ────────────────────────────────────────────────────────
add_executable(minidb_tests
    tests/test_main.cpp
    ${MINIDB_SOURCES}
)

# Link with dl/pthread on Linux
if(UNIX AND NOT APPLE)
    target_link_libraries(minidb dl pthread)
    target_link_libraries(minidb_tests dl pthread)
endif()

# Enable testing
enable_testing()
add_test(NAME MiniDBTests COMMAND minidb_tests)

# Print config summary
message(STATUS "")
message(STATUS "─── MiniDB Build Configuration ──────────────")
message(STATUS "  C++ Standard : C++17")
message(STATUS "  Build Type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler     : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "───────────────────────────────────────────")
message(STATUS "")
