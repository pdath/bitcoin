// Copyright (c) 2026 The Bitcoin Knots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BENCHMARK_METRICS_COLLECTOR_HPP
#define BITCOIN_BENCHMARK_METRICS_COLLECTOR_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>

// Hardcoded cache size
#define BENCH_CACHE_MB 256

/**
 * Operation statistics tracker.
 */
struct OperationStats {
    uint64_t count{0};
    uint64_t total_nsec{0};
    uint32_t max_latency_nsec{0};
    std::vector<uint32_t> latency_samples_nsec;

    void Record(uint64_t duration_nsec) {
        count++;
        total_nsec += duration_nsec;
        if (duration_nsec > max_latency_nsec) {
            max_latency_nsec = static_cast<uint32_t>(duration_nsec);
        }
        if (latency_samples_nsec.size() < 10000) {
            latency_samples_nsec.push_back(static_cast<uint32_t>(duration_nsec));
        }
    }

    double MeanTimeNsec() const {
        return count > 0 ? static_cast<double>(total_nsec) / count : 0.0;
    }
    double MeanTimeUsec() const { return MeanTimeNsec() / 1000.0; }
    double MeanTimeMsec() const { return MeanTimeNsec() / 1000000.0; }

    double Percentile(double p) const {
        if (latency_samples_nsec.empty()) return 0.0;
        std::vector<uint32_t> sorted = latency_samples_nsec;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = static_cast<size_t>(p / 100.0 * sorted.size());
        if (idx >= sorted.size()) idx = sorted.size() - 1;
        return static_cast<double>(sorted[idx]);
    }
    double P50_Nsec() const { return Percentile(50.0); }
    double P95_Nsec() const { return Percentile(95.0); }
    double P99_Nsec() const { return Percentile(99.0); }
};

/**
 * RAII-based scoped timer for automatic operation timing.
 * Usage: ScopedTimer timer(stats, "operation_name");
 */
class ScopedTimer {
public:
    ScopedTimer(OperationStats& stats, const std::string& op_name = "") 
        : m_stats(stats), m_start(std::chrono::high_resolution_clock::now()) {}
    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - m_start).count();
        m_stats.Record(static_cast<uint64_t>(duration));
    }
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
private:
    OperationStats& m_stats;
    std::chrono::high_resolution_clock::time_point m_start;
};

/**
 * Metrics collector for benchmark.
 */
class BenchmarkMetricsCollector {
public:
    // Cache layer stats
    OperationStats stats_cache_access_coin;
    OperationStats stats_cache_get_coin;
    OperationStats stats_cache_have_coin;
    OperationStats stats_cache_have_coin_in_cache;
    OperationStats stats_cache_add_coin;
    OperationStats stats_cache_spend_coin;
    OperationStats stats_cache_flush;

    // DB layer stats
    OperationStats stats_db_get_coin;
    OperationStats stats_db_have_coin;
    OperationStats stats_db_batch_write;
    OperationStats stats_db_cursor;
    OperationStats stats_db_get_best_block;
    OperationStats stats_db_get_head_blocks;
    OperationStats stats_db_estimate_size;

    // System metrics
    size_t peak_rss_bytes{0};
    double user_cpu_seconds{0.0};
    double system_cpu_seconds{0.0};
    size_t db_directory_size_bytes{0};
    double write_amplification_ratio{0.0};

    void Record(OperationStats& stats, uint64_t duration_nsec) {
        stats.Record(duration_nsec);
    }

    void SampleSystemResources();
    static size_t CalculateDirectorySize(const std::string& path);
    void PrintReport(const std::string& engine_name, double total_wall_time_sec);
    void ExportJSON(const std::string& filepath, const std::string& engine_name, double total_wall_time_sec);
    void Reset();
};

extern BenchmarkMetricsCollector g_metrics_collector;

#endif // BITCOIN_BENCHMARK_METRICS_COLLECTOR_HPP
