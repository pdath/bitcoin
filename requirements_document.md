# Requirements Document: Storage Engine Evaluation & Benchmarking for Bitcoin Knots

## 1. Executive Summary & Primary Objective

The **primary objective** of this requirements document is to define the evaluation and benchmarking framework needed to select a superior embedded database engine (such as LMDB or RocksDB) to replace LevelDB in **Bitcoin Knots**. 

At this stage, the sole focus is on conducting realistic, empirical performance benchmarks against a representative ~12GB+ UTXO chainstate dataset. All prototype code written during this phase is strictly disposable. Production-grade codebase refactoring and full database migrations will only be undertaken after a winning database engine has been conclusively selected based on benchmark data.

### 1.1 In Scope
- Evaluating battle-hardened, high-performance embedded key-value engines (LMDB, RocksDB, etc.) against baseline LevelDB.
- Creating "quick and dirty", throwaway prototype bindings to plug candidate database engines into `CCoinsViewDB`.
- Simulating Initial Block Download (IBD) workloads similar to `-reindex-chainstate` replay.
- Simulating steady-state operations via cache warm-up and real mempool transaction replay (`getrawmempool`).
- Collecting, reporting, and comparing throughput, latency, and resource metrics to select the winning storage engine.

### 1.2 Non-Goals
- Writing production-quality, robust, or maintainable code during the prototype phase.
- Implementing edge-case handling, complex error recovery, or clean shutdown logic in prototype wrappers.
- Full production refactoring or clean architectural overhaul prior to candidate selection.
- Modifying consensus rules, UTXO validation logic, or upper caching layers (`CCoinsViewCache`, `CCoinsViewBacked`).
- Migrating secondary indexes (`txindex`, `blockfilterindex`, `coinstatsindex`) during the initial evaluation phase.
- Evaluating non-embedded or client-server database architectures.

---

## 2. Requirements Notation (EARS)

Acceptance criteria in this document are expressed using **EARS (Easy Approach to Requirements Syntax)** patterns:

- **Ubiquitous**: The `<system>` shall `<action>`.
- **Event-Driven**: WHEN `<trigger>`, the `<system>` shall `<action>`.
- **State-Driven**: WHILE `<in state>`, the `<system>` shall `<action>`.
- **Unwanted Behavior**: IF `<error/invalid condition>`, THEN the `<system>` shall `<action>`.
- **Optional Feature**: WHERE `<feature is enabled>`, the `<system>` shall `<action>`.
- **Complex Combination**: WHILE `<state>`, WHEN `<trigger>`, IF `<error condition>`, THEN the `<system>` shall `<action>`.

---

## 3. Detailed Requirements & User Stories

### Requirement 1 (PRIMARY GOAL): Empirical Database Engine Selection via Benchmarking

#### User Story
> **As a** Bitcoin Knots core developer,  
> **I want** to evaluate candidate embedded storage engines against LevelDB using realistic workloads,  
> **So that** I can empirically select the best-performing database engine before committing to full production implementation.

#### Acceptance Criteria (EARS)
1. **[UBIQUITOUS]** The benchmarking process shall serve as the mandatory decision gate for selecting a new storage engine or retaining LevelDB.
2. **[STATE-DRIVEN]** WHILE conducting the evaluation phase, candidate engines shall be judged strictly on empirical performance benchmarks without requiring production-grade code completeness.
3. **[EVENT-DRIVEN]** WHEN all candidate benchmark runs are complete, the system shall produce a comparative evaluation matrix to identify the winning database engine.
4. **[UNWANTED BEHAVIOR]** IF no candidate engine demonstrates statistically significant performance improvements over LevelDB across key metrics, THEN LevelDB shall remain the default engine and production refactoring shall be aborted.

---

### Requirement 2: Candidate Engine Eligibility & Baseline Comparison

#### User Story
> **As a** system architect,  
> **I want** to compare LevelDB against battle-hardened embedded key-value engines,  
> **So that** the candidate pool is restricted to reliable, production-tested storage solutions.

#### Acceptance Criteria (EARS)
1. **[UBIQUITOUS]** The benchmark suite shall evaluate baseline LevelDB alongside candidate engines LMDB and RocksDB.
2. **[OPTIONAL FEATURE]** WHERE additional embedded database engines are considered, the candidate engine shall be restricted to well-adopted, battle-hardened embedded key-value stores.
3. **[UNWANTED BEHAVIOR]** IF a database candidate requires a separate daemon, network protocol, or client-server architecture, THEN it shall be disqualified from evaluation.

---

### Requirement 3: Disposable Prototype ("Quick & Dirty") Engine Bindings

#### User Story
> **As a** benchmark developer,  
> **I want** minimal, throwaway prototype implementations of `CCoinsViewDB` for each candidate engine,  
> **So that** I can execute benchmarks with zero engineering time wasted on production robustness, edge-case handling, or code polish.

#### Acceptance Criteria (EARS)
1. **[UBIQUITOUS]** All prototype code created during this evaluation phase shall be treated as strictly disposable throwaway code to be discarded upon benchmark completion.
2. **[UBIQUITOUS]** Prototype implementations shall implement only the bare-minimum happy-path `CCoinsViewDB` interface methods required to run the benchmark suite.
3. **[STATE-DRIVEN]** WHILE executing prototype benchmark runs, edge-case validation, comprehensive error recovery, and production hardening shall be omitted.
4. **[UNWANTED BEHAVIOR]** IF an exceptional condition or disk/runtime error occurs during a prototype benchmark run, THEN uncaught exceptions, process crashes, or core dumps are explicitly acceptable.
5. **[STATE-DRIVEN]** WHILE executing prototype `BatchWrite()` operations, candidate wrappers (such as LMDB or RocksDB) shall execute mutations within engine-native atomic transactions (e.g. `MDB_txn` with `mdb_put` / `mdb_del`).
6. **[UBIQUITOUS]** Prototype engine implementations shall store data files in dedicated subdirectories or with distinct file prefixes to avoid corrupting standard LevelDB datasets.

---

### Requirement 4: Realistic Workload Simulation Framework

#### User Story
> **As a** performance engineer,  
> **I want** the benchmark suite to simulate both Initial Block Download (IBD) and steady-state UTXO processing on a realistic ~12GB+ dataset,  
> **So that** benchmark results reflect real-world Bitcoin Knots operating conditions.

#### Acceptance Criteria (EARS)
1. **[UBIQUITOUS]** The benchmarking framework shall instantiate tests against a realistic ~12GB+ UTXO chainstate dataset.
2. **[EVENT-DRIVEN]** WHEN running an IBD simulation, the benchmark framework shall replay raw block processing similar to `-reindex-chainstate` replay to measure bulk UTXO write/update throughput.
3. **[EVENT-DRIVEN]** WHEN running a steady-state simulation, the benchmark framework shall:
   - Perform a configurable cache warm-up phase.
   - Execute real transaction vectors (e.g., extracted via `bitcoin-cli getrawmempool` or pre-recorded mempool traces) against `CCoinsViewCache` backed by the prototype `CCoinsViewDB`.
4. **[STATE-DRIVEN]** WHILE executing benchmark driver runs, the framework shall allow configurable database parameters (e.g., path, cache size such as 256MB default, memory-only mode).

---

### Requirement 5: Performance Metrics & Selection Criteria

#### User Story
> **As a** performance reviewer,  
> **I want** precise execution time per API call (in nanoseconds/milliseconds) and latency distribution metrics reported for every benchmark run,  
> **So that** I can accurately analyze both high-frequency reads and infrequent coarse operations (like `Flush()`) without metric distortion.

#### Acceptance Criteria (EARS)
1. **[UBIQUITOUS]** The benchmark framework shall measure and report the **mean execution time per API call (in ns/call, µs/call, or ms/call)** for all monitored `CCoinsViewCache` and backing `CCoinsView` public methods, including:
   - Read & Cache-miss queries: `AccessCoin`, `GetCoin`, `HaveCoin`, `HaveCoinInCache`
   - Cache mutation & persistence: `AddCoin`, `SpendCoin`, `Flush`, `BatchWrite`
   - Header & Cursor operations: `GetHeadBlocks`, `GetBestBlock`, `Cursor`
2. **[UBIQUITOUS]** The benchmark framework shall measure and report full latency distributions per API call, including median (p50), 95th percentile (p95), 99th percentile (p99), and **maximum observed call latency**.
3. **[EVENT-DRIVEN]** WHEN a benchmark workload completes, the framework shall report total execution wall-clock time, peak RAM memory consumption, and resulting database disk footprint size.
4. **[STATE-DRIVEN]** WHILE executing comparative benchmark runs, identical workload vectors and hardware constraints shall be applied to LevelDB, LMDB, RocksDB, and any other candidate engines to ensure strict measurement parity.

---

## 4. Summary Matrix of Requirements

| ID | Category | Requirement Title | Primary EARS Pattern |
|---|---|---|---|
| **REQ-1** | **Primary Objective** | Empirical Engine Selection via Benchmarking | Ubiquitous / State-Driven / Event-Driven |
| **REQ-2** | Scope & Eligibility | Candidate Engines & LevelDB Baseline | Ubiquitous / Optional / Unwanted |
| **REQ-3** | **Prototyping** | **Disposable Prototype ("Quick & Dirty") Engine Bindings** | **Ubiquitous / State-Driven / Unwanted** |
| **REQ-4** | Workload Simulation | IBD & Steady-State Replay (~12GB UTXO) | Event-Driven / State-Driven |
| **REQ-5** | Analytics | Per-Call Execution Time (ns/call) & Latency Distributions | Ubiquitous / Event-Driven / State-Driven |

---

## 5. Key Source Code & Reference Material for Implementers

To assist future developers and AI coding agents implementing this benchmark suite, the following repository source files and external database engine repositories serve as primary technical references:

### Key Bitcoin Knots Source Files
- [txdb.h](file:///home/odroid/bitcoin/src/txdb.h): Definition of `CCoinsViewDB` and `CDBWrapper` LevelDB bindings.
- [txdb.cpp](file:///home/odroid/bitcoin/src/txdb.cpp): Implementation of `CCoinsViewDB` read/write operations and LevelDB initialization.
- [validation.h](file:///home/odroid/bitcoin/src/validation.h): `CoinsViews` cache hierarchy manager (`m_dbview`, `m_cacheview`).
- [coins.h](file:///home/odroid/bitcoin/src/coins.h): `CCoinsView`, `CCoinsViewBacked`, and `CCoinsViewCache` base class interfaces and memory cache management logic.

### Candidate Embedded Database Engine Repositories
- **LMDB**: [LMDB GitHub Repository](https://github.com/LMDB/lmdb/tree/mdb.master3/libraries/liblmdb)
- **RocksDB**: [RocksDB GitHub Repository](https://github.com/facebook/rocksdb/)
