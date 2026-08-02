// Copyright (c) 2026 The Bitcoin Knots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#include <metrics_collector.hpp>

#include <sys/resource.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <cstring>

#include <fstream>
#include <iomanip>
#include <iostream>

BenchmarkMetricsCollector g_metrics_collector;

void BenchmarkMetricsCollector::SampleSystemResources() {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        peak_rss_bytes = static_cast<size_t>(usage.ru_maxrss) * 1024;
        user_cpu_seconds = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1000000.0;
        system_cpu_seconds = usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1000000.0;
    }
}

size_t BenchmarkMetricsCollector::CalculateDirectorySize(const std::string& path) {
    size_t total = 0;
    DIR* dir = opendir(path.c_str());
    if (!dir) return 0;
    
    struct dirent* entry;
    struct stat st;
    while ((entry = readdir(dir))) {
        std::string full = path + "/" + entry->d_name;
        if (stat(full.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
                total += CalculateDirectorySize(full);
            } else {
                total += st.st_size;
            }
        }
    }
    closedir(dir);
    return total;
}

void BenchmarkMetricsCollector::PrintReport(const std::string& engine_name, double total_wall_time_sec) {
    std::cout << "\n" << std::string(80, '=') << "\n";
    std::cout << "BENCHMARK REPORT: " << engine_name << "\n";
    std::cout << std::string(80, '=') << "\n\n";

    std::cout << "Total Wall Clock Time: " << std::fixed << std::setprecision(3)
              << total_wall_time_sec << " seconds\n";
    std::cout << "Peak RSS Memory: " << (peak_rss_bytes / (1024 * 1024)) << " MB\n";
    std::cout << "User CPU Time: " << user_cpu_seconds << " seconds\n";
    std::cout << "System CPU Time: " << system_cpu_seconds << " seconds\n";
    std::cout << "Database Directory Size: " << (db_directory_size_bytes / (1024 * 1024)) << " MB\n";
    std::cout << "\n";

    auto print = [](const std::string& name, const OperationStats& s) {
        if (s.count > 0) {
            std::cout << "  " << std::left << std::setw(30) << name
                      << ": count=" << std::setw(8) << s.count
                      << " mean=" << std::setw(8) << s.MeanTimeUsec() << "us"
                      << " p50=" << std::setw(8) << s.P50_Nsec()/1000.0 << "us"
                      << " p95=" << std::setw(8) << s.P95_Nsec()/1000.0 << "us"
                      << " p99=" << std::setw(8) << s.P99_Nsec()/1000.0 << "us"
                      << " max=" << std::setw(8) << s.max_latency_nsec/1000.0 << "us\n";
        }
    };

    std::cout << "--- Cache Layer ---\n";
    print("AccessCoin", stats_cache_access_coin);
    print("GetCoin", stats_cache_get_coin);
    print("HaveCoin", stats_cache_have_coin);
    print("HaveCoinInCache", stats_cache_have_coin_in_cache);
    print("AddCoin", stats_cache_add_coin);
    print("SpendCoin", stats_cache_spend_coin);
    print("Flush", stats_cache_flush);
    
    std::cout << "\n--- DB Layer ---\n";
    print("DB GetCoin", stats_db_get_coin);
    print("DB HaveCoin", stats_db_have_coin);
    print("DB BatchWrite", stats_db_batch_write);
    print("DB Cursor", stats_db_cursor);
    print("DB GetBestBlock", stats_db_get_best_block);
    print("DB GetHeadBlocks", stats_db_get_head_blocks);
    print("DB EstimateSize", stats_db_estimate_size);
    
    std::cout << "\n" << std::string(80, '=') << "\n";
}

void BenchmarkMetricsCollector::ExportJSON(const std::string& filepath, const std::string& engine_name, double total_wall_time_sec) {
    std::ofstream out(filepath);
    if (!out) {
        std::cerr << "Failed to open: " << filepath << "\n";
        return;
    }
    
    out << "{\n";
    out << "  \"engine\": \"" << engine_name << "\",\n";
    out << "  \"total_wall_time_sec\": " << total_wall_time_sec << ",\n";
    out << "  \"peak_rss_bytes\": " << peak_rss_bytes << ",\n";
    out << "  \"user_cpu_seconds\": " << user_cpu_seconds << ",\n";
    out << "  \"system_cpu_seconds\": " << system_cpu_seconds << ",\n";
    out << "  \"db_directory_size_bytes\": " << db_directory_size_bytes << ",\n";
    
    // Cache layer stats
    out << "  \"cache_layer\": {\n";
    out << "    \"access_coin\": {\"count\": " << stats_cache_access_coin.count << ", \"total_nsec\": " << stats_cache_access_coin.total_nsec << ", \"mean_usec\": " << stats_cache_access_coin.MeanTimeUsec() << "},\n";
    out << "    \"get_coin\": {\"count\": " << stats_cache_get_coin.count << ", \"total_nsec\": " << stats_cache_get_coin.total_nsec << ", \"mean_usec\": " << stats_cache_get_coin.MeanTimeUsec() << "},\n";
    out << "    \"have_coin\": {\"count\": " << stats_cache_have_coin.count << ", \"total_nsec\": " << stats_cache_have_coin.total_nsec << ", \"mean_usec\": " << stats_cache_have_coin.MeanTimeUsec() << "},\n";
    out << "    \"have_coin_in_cache\": {\"count\": " << stats_cache_have_coin_in_cache.count << ", \"total_nsec\": " << stats_cache_have_coin_in_cache.total_nsec << ", \"mean_usec\": " << stats_cache_have_coin_in_cache.MeanTimeUsec() << "},\n";
    out << "    \"add_coin\": {\"count\": " << stats_cache_add_coin.count << ", \"total_nsec\": " << stats_cache_add_coin.total_nsec << ", \"mean_usec\": " << stats_cache_add_coin.MeanTimeUsec() << "},\n";
    out << "    \"spend_coin\": {\"count\": " << stats_cache_spend_coin.count << ", \"total_nsec\": " << stats_cache_spend_coin.total_nsec << ", \"mean_usec\": " << stats_cache_spend_coin.MeanTimeUsec() << "},\n";
    out << "    \"flush\": {\"count\": " << stats_cache_flush.count << ", \"total_nsec\": " << stats_cache_flush.total_nsec << ", \"mean_usec\": " << stats_cache_flush.MeanTimeUsec() << "}\n";
    out << "  },\n";
    
    // DB layer stats
    out << "  \"db_layer\": {\n";
    out << "    \"get_coin\": {\"count\": " << stats_db_get_coin.count << ", \"total_nsec\": " << stats_db_get_coin.total_nsec << ", \"mean_usec\": " << stats_db_get_coin.MeanTimeUsec() << "},\n";
    out << "    \"have_coin\": {\"count\": " << stats_db_have_coin.count << ", \"total_nsec\": " << stats_db_have_coin.total_nsec << ", \"mean_usec\": " << stats_db_have_coin.MeanTimeUsec() << "},\n";
    out << "    \"batch_write\": {\"count\": " << stats_db_batch_write.count << ", \"total_nsec\": " << stats_db_batch_write.total_nsec << ", \"mean_usec\": " << stats_db_batch_write.MeanTimeUsec() << "},\n";
    out << "    \"cursor\": {\"count\": " << stats_db_cursor.count << ", \"total_nsec\": " << stats_db_cursor.total_nsec << ", \"mean_usec\": " << stats_db_cursor.MeanTimeUsec() << "},\n";
    out << "    \"get_best_block\": {\"count\": " << stats_db_get_best_block.count << ", \"total_nsec\": " << stats_db_get_best_block.total_nsec << ", \"mean_usec\": " << stats_db_get_best_block.MeanTimeUsec() << "},\n";
    out << "    \"get_head_blocks\": {\"count\": " << stats_db_get_head_blocks.count << ", \"total_nsec\": " << stats_db_get_head_blocks.total_nsec << ", \"mean_usec\": " << stats_db_get_head_blocks.MeanTimeUsec() << "},\n";
    out << "    \"estimate_size\": {\"count\": " << stats_db_estimate_size.count << ", \"total_nsec\": " << stats_db_estimate_size.total_nsec << ", \"mean_usec\": " << stats_db_estimate_size.MeanTimeUsec() << "}\n";
    out << "  }\n";
    out << "}\n";
}

void BenchmarkMetricsCollector::Reset() {
    *this = BenchmarkMetricsCollector{};
}
