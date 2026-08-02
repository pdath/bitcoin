// Copyright (c) 2026 The Bitcoin Knots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

// Provide translation stub to avoid linking issues with bitcoin_clientversion
// This is needed because some Bitcoin libraries use the translation system
#include <util/translation.h>
const TranslateFn G_TRANSLATION_FUN{nullptr};

#include <coins.h>
#include <txdb.h>
#include <primitives/block.h>
#include <util/fs.h>
#include <util/strencodings.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>

// Metrics collector
#include <metrics_collector.hpp>

// Database-specific CCoinsView implementation
// Include the appropriate database header
// For LevelDB, RocksDB, and LMDB, we use custom implementations

// Global metrics collector
extern BenchmarkMetricsCollector g_metrics_collector;

// Forward declaration - each database-specific implementation provides this
std::unique_ptr<CCoinsView> CreateDatabaseView(const std::string& db_path, size_t cache_size);

/**
 * Run the IBD benchmark.
 */
void RunIBDBenchmark(CCoinsView& db_view) {
    std::cout << "Running IBD benchmark...\n";
    
    // Create cache
    CCoinsViewCache cache(&db_view);
    
    // TODO: Implement actual block processing
    // For now, just do a simple test
    uint256 best_block = db_view.GetBestBlock();
    std::cout << "Best block: " << best_block.ToString() << "\n";
    
    // Note: Skipping BatchWrite/Flush tests for now as they require proper setup
    // The basic GetCoin/GetBestBlock interface works
    
    std::cout << "IBD benchmark completed.\n";
}

/**
 * Run the steady-state benchmark.
 */
void RunSteadyStateBenchmark(CCoinsView& db_view) {
    std::cout << "Running steady-state benchmark...\n";
    
    // Create cache
    CCoinsViewCache cache(&db_view);
    
    // TODO: Implement mempool replay
    
    std::cout << "Steady-state benchmark completed.\n";
}

/**
 * Run benchmark for a specific engine.
 */
void RunBenchmark(const std::string& engine_name, const std::string& workload) {
    // Create database path
    std::string db_path = std::string(BENCH_DATA_DIR) + "/" + engine_name + "_chainstate";
    
    // Ensure directory exists
    fs::path data_dir = fs::u8path(db_path.c_str()).parent_path();
    if (!fs::exists(data_dir)) {
        fs::create_directories(data_dir);
    }
    
    // Create database view
    const size_t cache_size = BENCH_CACHE_MB * 1024 * 1024;
    std::unique_ptr<CCoinsView> db_view = CreateDatabaseView(db_path, cache_size);
    
    // Reset metrics
    g_metrics_collector.Reset();
    
    // Sample initial system resources
    g_metrics_collector.SampleSystemResources();
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Run selected workload
    if (workload == "ibd") {
        RunIBDBenchmark(*db_view);
    } else if (workload == "steady") {
        RunSteadyStateBenchmark(*db_view);
    } else {
        RunIBDBenchmark(*db_view);
        RunSteadyStateBenchmark(*db_view);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    
    // Sample final system resources
    g_metrics_collector.SampleSystemResources();
    
    // Print report
    g_metrics_collector.PrintReport(engine_name + "_" + workload, elapsed);
    
    // Export JSON
    std::string results_dir = "/home/odroid/bitcoin/benchmark/results";
    std::string json_path = results_dir + "/" + engine_name + "_" + workload + ".json";
    g_metrics_collector.ExportJSON(json_path, engine_name + "_" + workload, elapsed);
}

/**
 * Parse command line arguments.
 */
void ParseArguments(int argc, char* argv[], std::string& engine, std::string& workload) {
    engine = "leveldb";
    workload = "ibd";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--engine=leveldb" || arg == "-e=leveldb") {
            engine = "leveldb";
        } else if (arg == "--engine=rocksdb" || arg == "-e=rocksdb") {
            engine = "rocksdb";
        } else if (arg == "--engine=lmdb" || arg == "-e=lmdb") {
            engine = "lmdb";
        } else if (arg == "--workload=ibd" || arg == "-w=ibd") {
            workload = "ibd";
        } else if (arg == "--workload=steady" || arg == "-w=steady") {
            workload = "steady";
        }
    }
}

int main(int argc, char* argv[]) {
    std::cout << "Database Benchmark Tool for Bitcoin Knots\n";
    std::cout << "==========================================\n\n";
    
    std::string engine, workload;
    ParseArguments(argc, argv, engine, workload);
    
    std::cout << "Running with engine=" << engine << " workload=" << workload << "\n\n";
    
    RunBenchmark(engine, workload);
    
    std::cout << "\nBenchmark completed successfully!\n";
    return 0;
}
