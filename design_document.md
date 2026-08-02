# Design Document: Storage Engine Benchmarking & Evaluation Suite for Bitcoin Knots

## 1. Introduction & Executive Summary

This document specifies the technical design for a standalone benchmarking suite created to evaluate candidate embedded key-value storage engines (specifically **LMDB** and **RocksDB**) against the baseline **LevelDB** engine in **Bitcoin Knots**.

### Key Principles
- **Benchmarking-First Approach**: The sole goal is generating reproducible, empirical performance comparison metrics across candidate database engines.
- **Disposable Prototype Architecture**: Database wrappers for candidate engines (`CCoinsViewLMDB`, `CCoinsViewRocksDB`) are minimal, throwaway wrappers implementing happy-path execution without edge-case handling or error recovery.
- **Realistic Workload Simulation**: Evaluates IBD block ingestion reading live mainnet block files directly from `/home/knots-mainnet/.bitcoin/blocks/` and steady-state mempool processing on a representative ~12GB UTXO chainstate dataset.

---

## 2. System Architecture & Component Diagram

The benchmarking system consists of three main operational tiers:

```
┌────────────────────────────────────────────────────────────────────────┐
│                      Benchmarking Driver & CLI                         │
│       (Configures ultra-minimal parameters: --engine, --workload)      │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                   Workload Generators & Simulators                     │
│  ┌──────────────────────────────┐    ┌──────────────────────────────┐  │
│  │   IBD Simulator              │    │ Steady-State Mempool Sim     │  │
│  │   Reads live mainnet blocks  │    │ (warm-up + mempool trace)    │  │
│  │   /home/knots-mainnet/       │    │                              │  │
│  │   .bitcoin/blocks/           │    │                              │  │
│  │   (Reuses Knots block reader │    │                              │  │
│  │    for xor.dat de-obfuscation)│   │                              │  │
│  └──────────────┬───────────────┘    └──────────────┬───────────────┘  │
└─────────────────┼───────────────────────────────────┼──────────────────┘
                  │                                   │
                  ▼                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│             Bitcoin Knots Abstract View Layer (Coins View)             │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │ CCoinsViewCache (RAM cache layer, #define BENCH_CACHE_MB 256)    │  │
│  └────────────────────────────────┬─────────────────────────────────┘  │
│                                   │                                    │
│  ┌────────────────────────────────▼─────────────────────────────────┐  │
│  │ CCoinsViewBacked / CCoinsViewErrorCatcher (Pass-through link)    │  │
│  └────────────────────────────────┬─────────────────────────────────┘  │
└───────────────────────────────────┼────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                   Prototype Database Storage Adapters                   │
│  ┌──────────────────┐    ┌──────────────────┐    ┌──────────────────┐  │
│  │ CCoinsViewDB     │    │ CCoinsViewLMDB   │    │ CCoinsViewRocksDB│  │
│  │ (LevelDB)        │    │ (LMDB Prototype) │    │ (RocksDB Prototyp)│ │
│  └────────┬─────────┘    └────────┬─────────┘    └────────┬─────────┘  │
└───────────┼───────────────────────┼───────────────────────┼────────────┘
            │                       │                       │
            ▼                       ▼                       ▼
  ┌──────────────────┐    ┌──────────────────┐    ┌──────────────────┐
  │ LevelDB          │    │ LMDB             │    │ RocksDB          │
  │ benchmark/data/  │    │ benchmark/data/  │    │ benchmark/data/  │
  │ leveldb_chain/   │    │ lmdb_chain/      │    │ rocksdb_chain/   │
  └──────────────────┘    └──────────────────┘    └──────────────────┘
```

---

## 3. Class & Component Design

### 3.1 Abstract View Hierarchy Preservation
The prototype storage adapters inherit directly from `CCoinsView` (or subclass `CCoinsViewDB`) to preserve compatibility with upper caching layers:

```cpp
// Base interface (existing in src/coins.h)
class CCoinsView {
public:
    virtual std::optional<Coin> GetCoin(const COutPoint& outpoint) const;
    virtual bool HaveCoin(const COutPoint& outpoint) const;
    virtual uint256 GetBestBlock() const;
    virtual std::vector<uint256> GetHeadBlocks() const;
    virtual bool BatchWrite(CoinsViewCacheCursor& cursor, const uint256& hashBlock);
    virtual std::unique_ptr<CCoinsViewCursor> Cursor() const;
    virtual ~CCoinsView() {}
};
```

### 3.2 Candidate Engine Prototype Implementations

#### 1. LevelDB Baseline (`CCoinsViewDB`)
- Uses existing Bitcoin Knots implementation (`src/txdb.cpp`, `src/txdb.h`, `src/dbwrapper.cpp`).
- Wraps `CDBWrapper` around `leveldb::DB`.

#### 2. LMDB Prototype Adapter (`CCoinsViewLMDB`)
- **Header Structure**:
  ```cpp
  class CCoinsViewLMDB final : public CCoinsView {
  private:
      MDB_env* m_env{nullptr};
      MDB_dbi m_dbi{0};
      std::string m_path;

  public:
      CCoinsViewLMDB(const std::string& db_path, size_t map_size_bytes);
      ~CCoinsViewLMDB();

      std::optional<Coin> GetCoin(const COutPoint& outpoint) const override;
      bool HaveCoin(const COutPoint& outpoint) const override;
      uint256 GetBestBlock() const override;
      std::vector<uint256> GetHeadBlocks() const override;
      bool BatchWrite(CoinsViewCacheCursor& cursor, const uint256& hashBlock) override;
      std::unique_ptr<CCoinsViewCursor> Cursor() const override;
  };
  ```
- **LMDB Transaction Mechanics**:
  - `GetCoin()` / `HaveCoin()`: Opened in `MDB_RDONLY` read transactions (`mdb_txn_begin(m_env, NULL, MDB_RDONLY, &txn)` -> `mdb_get()` -> `mdb_txn_abort()`).
  - `BatchWrite()`: Opened in a single read-write transaction (`mdb_txn_begin(m_env, NULL, 0, &txn)`). Iterates over `cursor`, performing `mdb_put()` for updated/added coins and `mdb_del()` for spent coins, writes `hashBlock` best block record, then calls `mdb_txn_commit(txn)`.

#### 3. RocksDB Prototype Adapter (`CCoinsViewRocksDB`) & LevelDB Tuning Parity
- **Header Structure**:
  ```cpp
  class CCoinsViewRocksDB final : public CCoinsView {
  private:
      rocksdb::DB* m_db{nullptr};
      rocksdb::Options m_options;
      rocksdb::BlockBasedTableOptions m_table_options;

  public:
      CCoinsViewRocksDB(const std::string& db_path, size_t nCacheSize);
      ~CCoinsViewRocksDB();

      std::optional<Coin> GetCoin(const COutPoint& outpoint) const override;
      bool HaveCoin(const COutPoint& outpoint) const override;
      uint256 GetBestBlock() const override;
      std::vector<uint256> GetHeadBlocks() const override;
      bool BatchWrite(CoinsViewCacheCursor& cursor, const uint256& hashBlock) override;
      std::unique_ptr<CCoinsViewCursor> Cursor() const override;
  };
  ```
- **LevelDB Parameter Parity & RocksDB Configuration Tuning**:
  To ensure measurement parity with LevelDB tuning in Bitcoin Knots (`src/dbwrapper.cpp`) while preventing write stalls under the constrained 256MB benchmark `dbcache`, `CCoinsViewRocksDB` adopts the following parameters:
  - **Memtable Write Buffer (Anti-Write Stall Tuning)**: `m_options.write_buffer_size = 256 * 1024 * 1024` (256MB explicit memtable size to buffer large `BatchWrite()` spikes and prevent write stalls under low `dbcache` conditions).
  - **Block Cache Allocation**: `m_table_options.block_cache = rocksdb::NewLRUCache(nCacheSize / 2)` (allocates 50% of db cache to block cache, matching LevelDB).
  - **Bloom Filter Policy**: `m_table_options.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10))` (10 bits per key, matching LevelDB).
  - **Compression**: `m_options.compression = rocksdb::kNoCompression` (disabled compression for maximum raw throughput, matching LevelDB).
  - **Max File Size**: `m_options.target_file_size_base = 32 * 1024 * 1024` (2MB target file size, matching `DBWRAPPER_MAX_FILE_SIZE`).
  - **Auto Creation**: `m_options.create_if_missing = true`.
- **RocksDB Write Mechanics**:
  - `GetCoin()` / `HaveCoin()`: Calls `m_db->Get(read_options, key, &value_str)`.
  - `BatchWrite()`: Instantiates `rocksdb::WriteBatch batch`. Iterates over `cursor`, invoking `batch.Put()` or `batch.Delete()`, adds best block key, and executes `m_db->Write(write_options, &batch)`.

---

## 4. Key/Value Data Encoding Schema

To ensure parity across all candidate database engines, prototype wrappers use a unified, binary key-value encoding layout:

| Key Type | Key Prefix | Key Payload | Value Format |
|---|---|---|---|
| **Coin UTXO** | `'c'` (`0x63`) | Serialized `COutPoint` (TxID + Output Index vuint) | Serialized `Coin` (Amount, ScriptPubKey, Height, IsCoinbase) |
| **Best Block** | `'B'` (`0x42`) | None | 32-byte Block Hash (`uint256`) |
| **Head Blocks** | `'H'` (`0x48`) | None | Vector of 32-byte Block Hashes |

---

## 5. Workload Simulation Drivers

### 5.1 Initial Block Download (IBD) Workload Driver & Block De-obfuscation
- **Objective**: Simulates bulk write and update operations during full chain indexing using live block data.
- **Hardcoded Block Path**: Hardcoded to `/home/knots-mainnet/.bitcoin/blocks/` for direct reading of `blk*.dat` raw block files.
- **XOR De-obfuscation & Block Ingestion**:
  - Raw block data files (`blk*.dat`) are obfuscated on disk using an 8-byte key stored in `/home/knots-mainnet/.bitcoin/blocks/xor.dat`.
  - The IBD driver reuses or adapts Bitcoin Knots' existing block reader components (`src/node/blockstorage.cpp`, `FlatFileSeq`, `CBufferedFile`) to automatically load `xor.dat`, apply byte-wise XOR de-obfuscation, and deserialize raw stream data into `CBlock` structures.
- **Execution Flow**:
  1. Loads `xor.dat` key and opens block files directly from `/home/knots-mainnet/.bitcoin/blocks/blk*.dat`.
  2. Reads and de-obfuscates raw bytes, deserializing each block into `CBlock`.
  3. For each block batch:
     - Populates a `CCoinsMap` containing newly created UTXOs and marks spent UTXOs.
     - Calls `view.BatchWrite(cursor, block_hash)`.
  4. Execution runs until block dataset is completely processed.
  5. Measures total elapsed wall-clock time, `BatchWrite()` execution duration per call ($\text{ms/call}$), and disk space occupied.

### 5.2 Steady-State Mempool Workload Driver
- **Objective**: Simulates real-time transaction processing under steady-state operating conditions across all key `CCoinsViewCache` and backing `CCoinsView` public methods.
- **Execution Flow**:
  1. **Cache Warm-Up Phase**:
     - Pre-loads `CCoinsViewCache` (configured via `#define BENCH_CACHE_MB 256`) by querying random UTXOs using `AccessCoin()` / `GetCoin()` to reach target cache fill ratio.
  2. **Execution Phase**:
     - Replays a trace of real mempool transactions (e.g. captured via `bitcoin-cli getrawmempool`), stored in a hard coded file called `getrawmempool.json`
     - **Read & Lookup Operations**:
       - `AccessCoin()`: Direct reference lookup in cache or disk fallback.
       - `GetCoin()`: Retrieves `std::optional<Coin>`.
       - `HaveCoin()`: Unspent status check.
       - `HaveCoinInCache()`: Pure memory lookup check without disk IO.
     - **Cache State Mutation & Persistence**:
       - `AddCoin()`: Inserting newly observed transaction outputs.
       - `SpendCoin()`: Marking outputs spent by mempool transactions.
       - `Flush()`: Triggering cache flush when memory bounds are reached.
       - `BatchWrite()`: Persisting dirty coin entries down to the backend database.
     - **Header & Iteration Queries**:
       - `GetHeadBlocks()` & `GetBestBlock()`: Header consistency checks.
       - `Cursor()`: Iteration over backing view records.
  3. Execution runs until workload trace completes naturally.
  4. Measures average execution time per call ($\text{ns/call}$ or $\text{ms/call}$), latency percentiles (p50, p95, p99), and maximum latency spikes for all monitored methods.

---

## 6. Measurement Instrumentation & Metrics Collector

The `BenchmarkMetricsCollector` class instruments metrics from three complementary sources:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    Benchmark Metrics Collector                          │
└───────────────┬─────────────────────┬─────────────────────┬─────────────┘
                │                     │                     │
                ▼                     ▼                     ▼
┌───────────────────────────────┐ ┌────────────────────────┐ ┌─────────────┐
│ 1. CCoinsViewCache Layer      │ │ 2. Backend DB Adapter  │ │ 3. OS Kernel│
│ - AccessCoin(), HaveCoin()    │ │ - Direct DB GetCoin()  │ │ - getrusage │
│ - AddCoin(), SpendCoin()      │ │ - Direct DB HaveCoin() │ │ - RSS RAM   │
│ - Flush()                     │ │ - BatchWrite()         │ │ - Disk Size │
│                               │ │ - Cursor()             │ │ - Write Amp │
└───────────────────────────────┘ └────────────────────────┘ └─────────────┘
```

```cpp
#define BENCH_CACHE_MB 256

struct OperationStats {
    uint64_t count{0};
    uint64_t total_nsec{0};
    uint32_t max_latency_nsec{0};
    std::vector<uint32_t> latency_samples_nsec; // Sampled for percentile calculation

    double MeanTimeNsec() const {
        return count > 0 ? static_cast<double>(total_nsec) / count : 0.0;
    }
};

class BenchmarkMetricsCollector {
public:
    // Source 1: Upper CCoinsViewCache RAM Layer API Calls
    OperationStats stats_cache_access_coin;
    OperationStats stats_cache_get_coin;
    OperationStats stats_cache_have_coin;
    OperationStats stats_cache_have_coin_in_cache;
    OperationStats stats_cache_add_coin;
    OperationStats stats_cache_spend_coin;
    OperationStats stats_cache_flush;

    // Source 2: Underlying CCoinsView Storage Engine Adapter Direct Calls
    OperationStats stats_db_get_coin;      // Direct disk read on cache miss
    OperationStats stats_db_have_coin;     // Direct disk lookup check
    OperationStats stats_db_batch_write;   // Direct atomic database transaction commit
    OperationStats stats_db_cursor;        // Direct database iterator creation/step
    OperationStats stats_db_get_best_block;
    OperationStats stats_db_get_head_blocks;

    // Source 3: OS Kernel System Profiler Metrics
    size_t peak_rss_bytes{0};              // Maximum Resident Set Size (via getrusage)
    double user_cpu_seconds{0.0};          // User space CPU time
    double system_cpu_seconds{0.0};        // Kernel space CPU time
    size_t db_directory_size_bytes{0};     // On-disk database folder footprint
    double write_amplification_ratio{0.0}; // Ratio of disk bytes written vs UTXO bytes payload

    void Record(OperationStats& stats, uint64_t duration_nsec);
    void SampleSystemResources();
    void PrintReport(const std::string& engine_name, double total_wall_time_sec);
    void ExportJSON(const std::string& filepath);
};
```

---

## 7. Directory & File Organization & Storage Location

All benchmark source code, configuration files, and temporary benchmark datasets reside within the top-level `benchmark/` folder in the workspace:

```
bitcoin/
└── benchmark/
    ├── requirements_document.md    # Requirements specification
    ├── design_document.md          # Technical design specification (this document)
    ├── CMakeLists.txt              # CMake build configuration for benchmark runner
    ├── getrawmempool.json          # Output of "bitcoin-cli getrawmempool"
    ├── data/                       # Hardcoded storage directory for temporary benchmark databases
    │   ├── leveldb_chainstate/     # LevelDB benchmark database (~12GB)
    │   ├── lmdb_chainstate/        # LMDB benchmark database (~12GB)
    │   └── rocksdb_chainstate/     # RocksDB benchmark database (~12GB)
    ├── src/
    │   ├── main.cpp                # Benchmark CLI harness entry point
    │   ├── metrics_collector.hpp   # High-resolution metrics collector
    │   ├── drivers/
    │   │   ├── ibd_driver.cpp      # IBD driver (reads /home/knots-mainnet/.bitcoin/blocks/ & xor.dat)
    │   │   └── mempool_driver.cpp  # Steady-state mempool driver
    │   └── adapters/
    │       ├── coinsview_leveldb.cpp # Baseline LevelDB adapter wrapper
    │       ├── coinsview_lmdb.cpp    # Prototype LMDB adapter wrapper
    │       └── coinsview_rocksdb.cpp # Prototype RocksDB adapter wrapper
    └── results/                    # Hardcoded output directory for JSON report logs
```

### Hardcoded Configuration Constants:
- **Cache Size**: `#define BENCH_CACHE_MB 256`
- **Database Paths**: Hardcoded to `benchmark/data/<engine>_chainstate/`
- **Output JSON Path**: Hardcoded to `benchmark/results/<engine>_<workload>.json`

---

## 8. Minimal CLI Arguments & Execution Workflow

The benchmark harness is an ultra-minimal executable (`bitcoin-db-bench`) featuring `"all"` options for batch testing:

### Command Line Arguments
- `--engine=<leveldb|lmdb|rocksdb|all>` (Selects target engine or runs all engines sequentially)
- `--workload=<ibd|steady|all>` (Selects target workload or runs all workloads sequentially)

### Usage Examples
```bash
# Run a single engine against a single workload
./bitcoin-db-bench --engine=rocksdb --workload=steady

# Run ALL database engines against ALL workloads sequentially
./bitcoin-db-bench --engine=all --workload=all
```

*Note: Runs proceed until the target workload dataset finishes completely. Output JSON metrics are automatically exported to `benchmark/results/<engine>_<workload>.json`.*

---

## 9. Key Source Code & Reference Material for Implementers

To assist future developers and AI coding agents implementing this benchmark suite, the following repository source files and external database engine repositories serve as primary technical references:

### Key Bitcoin Knots Source Files
- [txdb.h](../src/txdb.h): Definition of `CCoinsViewDB` and `CDBWrapper` LevelDB bindings.
- [txdb.cpp](../src/txdb.cpp): Implementation of `CCoinsViewDB` read/write operations and LevelDB initialization.
- [dbwrapper.cpp](../src/dbwrapper.cpp): LevelDB configuration parameters (`block_cache`, `write_buffer_size`, `filter_policy`, `compression`).
- [validation.h](../src/validation.h): `CoinsViews` cache hierarchy manager (`m_dbview`, `m_cacheview`).
- [coins.h](../src/coins.h): `CCoinsView`, `CCoinsViewBacked`, and `CCoinsViewCache` base class interfaces and memory cache management logic.

### Candidate Embedded Database Engine Repositories
- **LMDB**: [LMDB GitHub Repository](https://github.com/LMDB/lmdb/tree/mdb.master3/libraries/liblmdb), installed on the system via liblmdb-dev package.
- **RocksDB**: [RocksDB GitHub Repository](https://github.com/facebook/rocksdb/), installed on the system via the librocksdb-dev package.
