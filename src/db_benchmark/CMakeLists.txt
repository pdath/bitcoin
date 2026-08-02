# Copyright (c) 2026 The Bitcoin Knots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/licenses/mit-license.php.

# Database benchmark target configuration

# Find required packages for RocksDB and LMDB
if(WITH_ROCKSDB)
    find_package(RocksDB REQUIRED)
endif()
if(WITH_LMDB)
    find_package(LMDB QUIET)
    # If find_package fails, try to use system LMDB
    if(NOT LMDB_FOUND)
        message(WARNING "LMDB not found via find_package, trying system paths")
        set(LMDB_INCLUDE_DIRS "/usr/include")
        set(LMDB_LIBRARIES "lmdb")
        set(LMDB_FOUND TRUE)
    endif()
endif()

# Define hardcoded constants
add_compile_definitions(
    BENCH_CACHE_MB=256
    BENCH_DATA_DIR="/home/odroid/bitcoin/benchmark/data"
    BENCH_RESULTS_DIR="/home/odroid/bitcoin/benchmark/results"
)

# Source files - main benchmark program
set(BENCHMARK_SOURCES
    db_benchmark.cpp
    metrics_collector.cpp
)

# Add database-specific CCoinsViewDB implementation
if(WITH_LEVELDB)
    list(APPEND BENCHMARK_SOURCES leveldb/coinsview.cpp)
    add_compile_definitions(BENCH_DATABASE_LEVELDB=1)
    message(STATUS "Benchmark: Building with LevelDB")
elseif(WITH_ROCKSDB)
    list(APPEND BENCHMARK_SOURCES rocksdb/coinsview.cpp)
    add_compile_definitions(BENCH_DATABASE_ROCKSDB=1)
    message(STATUS "Benchmark: Building with RocksDB")
elseif(WITH_LMDB)
    list(APPEND BENCHMARK_SOURCES lmdb/coinsview.cpp)
    add_compile_definitions(BENCH_DATABASE_LMDB=1)
    message(STATUS "Benchmark: Building with LMDB")
else()
    list(APPEND BENCHMARK_SOURCES leveldb/coinsview.cpp)
    add_compile_definitions(BENCH_DATABASE_LEVELDB=1)
    message(STATUS "Benchmark: Defaulting to LevelDB")
endif()

# Create the db_benchmark executable
add_executable(db_benchmark ${BENCHMARK_SOURCES})

# Include directories
target_include_directories(db_benchmark
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/src
)

# Link with Bitcoin libraries
target_link_libraries(db_benchmark
    PRIVATE
    core_interface
    bitcoin_common
    bitcoin_util
    leveldb
    univalue
)

# Link with RocksDB if needed
if(WITH_ROCKSDB)
    target_link_libraries(db_benchmark PRIVATE RocksDB::rocksdb)
    target_include_directories(db_benchmark PRIVATE ${RocksDB_INCLUDE_DIRS})
endif()

# Link with LMDB if needed
if(WITH_LMDB)
    target_link_libraries(db_benchmark PRIVATE ${LMDB_LIBRARIES})
    target_include_directories(db_benchmark PRIVATE ${LMDB_INCLUDE_DIRS})
endif()

# Set output directory
set_target_properties(db_benchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin
)

message(STATUS "db_benchmark target configured")
