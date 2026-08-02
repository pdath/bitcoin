# Design Document v2: Storage Engine Benchmarking for Bitcoin Knots

## 1. Introduction & Overview

This document describes the **as-built** implementation of the database benchmarking framework for Bitcoin Knots. The system evaluates candidate embedded key-value storage engines (LMDB, RocksDB) against the baseline LevelDB engine using a minimal, self-contained benchmark executable.

### Key Principles Realized
- **Minimal Dependencies**: Avoid linking against the full `bitcoin_node` library by using self-contained database implementations
- **Compile-Time Selection**: Database engine selected via CMake flags (`WITH_LEVELDB`, `WITH_ROCKSDB`, `WITH_LMDB`)
- **Separate Build Directories**: Each engine builds in its own directory for isolation
- **Use of CCoinsView**: Each database implements the `CCoinsView` interface directly (not `CCoinsViewDB` since that's final)
- **Error Handling**: Uses `assert()` for error conditions with gdb debugging as requested

### Changes from Original Design
- **CCoinsView vs CCoinsViewDB**: Since `CCoinsViewDB` is declared `final` in Bitcoin Knots, we implement `CCoinsView` directly for each database engine
- **Simplified Architecture**: Database implementations are self-contained in `src/db_benchmark/` rather than being wrappers around the existing `CCoinsViewDB`
- **Direct Database Usage**: Each implementation directly uses its respective database library (leveldb, rocksdb, lmdb) without the `CDBWrapper` abstraction

---

## 2. Directory Structure

```
bitcoin/
├── benchmark/
│   ├── requirements_document.md    # Original requirements
│   ├── design_document.md          # Original design
│   ├── design_document_v2.md       # This updated design
│   └── data/                       # Benchmark databases
│       ├── leveldb_chainstate/     # LevelDB database
│       ├── rocksdb_chainstate/     # RocksDB database
│       └── lmdb_chainstate/        # LMDB database
│
└── src/
    └── db_benchmark/               # Renamed from src/benchmark/
        ├── CMakeLists.txt          # CMake build configuration
        ├── db_benchmark.cpp       # Main benchmark entry point
        ├── metrics_collector.hpp  # Metrics data structures
        ├── metrics_collector.cpp  # Metrics implementation
        ├── leveldb/
        │   ├── coinsview.hpp      # LevelDB CCoinsView header
        │   └── coinsview.cpp      # LevelDB CCoinsView implementation
        ├── rocksdb/
        │   ├── coinsview.hpp      # RocksDB CCoinsView header
        │   └── coinsview.cpp      # RocksDB CCoinsView implementation
        └── lmdb/
            ├── coinsview.hpp      # LMDB CCoinsView header
            └── coinsview.cpp      # LMDB CCoinsView implementation
```

---

## 3. CMake Build Configuration

### Build Flags
```cmake
# Select database engine (mutually exclusive)
-DWITH_LEVELDB=ON   # Build with LevelDB
-DWITH_ROCKSDB=ON   # Build with RocksDB  
-DWITH_LMDB=ON      # Build with LMDB
-DRDTS_CONSENT=IMPLICIT  # Required for Bitcoin Knots build
```

### Example Build Commands
```bash
# LevelDB
cmake -B build-leveldb -DWITH_LEVELDB=ON -DWITH_ROCKSDB=OFF -DWITH_LMDB=OFF -DRDTS_CONSENT=IMPLICIT
cmake --build build-leveldb -j4 -t db_benchmark

# RocksDB
cmake -B build-rocksdb -DWITH_ROCKSDB=ON -DWITH_LEVELDB=OFF -DWITH_LMDB=OFF -DRDTS_CONSENT=IMPLICIT
cmake --build build-rocksdb -j4 -t db_benchmark

# LMDB (when fixed)
cmake -B build-lmdb -DWITH_LMDB=ON -DWITH_LEVELDB=OFF -DWITH_ROCKSDB=OFF -DRDTS_CONSENT=IMPLICIT
cmake --build build-lmdb -j4 -t db_benchmark
```

### CMake Target
- **Target name**: `db_benchmark`
- **Output location**: `<build-dir>/bin/db_benchmark`
- **Dependencies**: `core_interface`, `bitcoin_common`, `bitcoin_util`, `leveldb`, `univalue` (+ rocksdb/lmdb as needed)

---

## 4. Database Implementations

### Common Interface: CCoinsView
All database implementations inherit from `CCoinsView` and implement these virtual methods:

```cpp
class CCoinsView {
public:
    virtual std::optional<Coin> GetCoin(const COutPoint& outpoint) const;
    virtual bool HaveCoin(const COutPoint& outpoint) const;
    virtual uint256 GetBestBlock() const;
    virtual std::vector<uint256> GetHeadBlocks() const;
    virtual bool BatchWrite(CoinsViewCacheCursor& cursor, const uint256& hashBlock);
    virtual std::unique_ptr<CCoinsViewCursor> Cursor() const;
    virtual size_t EstimateSize() const;
    virtual ~CCoinsView() = default;
};
```

### Factory Function
Each database implementation provides a factory function:
```cpp
std::unique_ptr<CCoinsView> CreateDatabaseView(const std::string& db_path, size_t cache_size);
```

### 4.1 LevelDB Implementation
- **Class**: `CCoinsViewDB_LevelDB : public CCoinsView`
- **Files**: `src/db_benchmark/leveldb/coinsview.hpp/cpp`
- **Status**: ✅ Working
- **Features**: Direct leveldb::DB usage, proper configuration, working Get/Have/BatchWrite

### 4.2 RocksDB Implementation  
- **Class**: `CCoinsViewDB_RocksDB : public CCoinsView`
- **Files**: `src/db_benchmark/rocksdb/coinsview.hpp/cpp`
- **Status**: ✅ Working
- **Features**: Direct rocksdb::DB usage, proper configuration, working Get/Have/BatchWrite

### 4.3 LMDB Implementation
- **Class**: `CCoinsViewDB_LMDB : public CCoinsView`
- **Files**: `src/db_benchmark/lmdb/coinsview.hpp/cpp`
- **Status**: ⚠️ Needs const_cast fixes
- **Issue**: LMDB API requires `char*` but DataStream uses `std::byte*`. Need proper casting.

---

## 5. Data Flow

```
main()
  → ParseArguments()
  → RunBenchmark(engine, workload)
    → CreateDatabaseView(db_path, cache_size)
    → CCoinsViewCache cache(db_view)
    → RunIBDBenchmark(*cache) or RunSteadyStateBenchmark(*cache)
    → Collect metrics
    → PrintReport()
```

---

## 6. Current Status Summary

### ✅ Working
- LevelDB implementation builds and runs
- RocksDB implementation builds and runs
- CMake configuration with engine selection
- Factory pattern for database creation
- Basic benchmark infrastructure

### ⚠️ Partial / Needs Work
- LMDB implementation (const_cast issues)
- Actual IBD workload (currently minimal test)
- Actual steady-state workload (currently minimal test)
- Full metrics collection
- Results export

---

## 7. Build & Run Examples

### LevelDB
```bash
cmake -B build-leveldb -DWITH_LEVELDB=ON -DWITH_ROCKSDB=OFF -DWITH_LMDB=OFF -DRDTS_CONSENT=IMPLICIT
cmake --build build-leveldb -j4 -t db_benchmark
./build-leveldb/bin/db_benchmark --engine=leveldb --workload=ibd
```

### RocksDB
```bash
cmake -B build-rocksdb -DWITH_ROCKSDB=ON -DWITH_LEVELDB=OFF -DWITH_LMDB=OFF -DRDTS_CONSENT=IMPLICIT
cmake --build build-rocksdb -j4 -t db_benchmark
./build-rocksdb/bin/db_benchmark --engine=rocksdb --workload=ibd
```

### LMDB (when fixed)
```bash
cmake -B build-lmdb -DWITH_LMDB=ON -DWITH_LEVELDB=OFF -DWITH_ROCKSDB=OFF -DRDTS_CONSENT=IMPLICIT
cmake --build build-lmdb -j4 -t db_benchmark
./build-lmdb/bin/db_benchmark --engine=lmdb --workload=ibd
```

---

## 8. Key Implementation Details

### Data Encoding
All implementations use the same key encoding as Bitcoin Knots:
- Coin UTXO: `'C' + txid + VARINT(output_index)` → Serialized Coin
- Best Block: `'B'` → uint256
- Head Blocks: `'H'` → Vector<uint256>

### DataStream Pattern
Since DataStream doesn't have a string constructor:
```cpp
// Old (doesn't work):
DataStream ssValue(value);

// New (works):
DataStream ssValue;
ssValue.write(Span<const std::byte>(reinterpret_cast<const std::byte*>(value.data()), value.size()));
```

### PathToString
Since `fs::path::string()` is deleted:
```cpp
// Old (doesn't work):
m_path = db_params.path.string();

// New (works):
m_path = PathToString(db_params.path);
```

### Null HashBlock Handling
BatchWrite checks for null hash:
```cpp
if (hashBlock.IsNull()) {
    return false;  // Skip if no block hash provided
}
```

---

## 9. Next Steps

1. Fix LMDB const_cast issues
2. Implement IBD block processing
3. Implement mempool replay from getrawmempool.json
4. Add full metrics collection
5. Create results directory and JSON export
