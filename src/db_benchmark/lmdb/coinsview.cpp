// Copyright (c) 2026 The Bitcoin Knots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

// Debug flag - uncomment to enable debug output
// #define DEBUG 1

#include <lmdb/coinsview.hpp>

#include <serialize.h>
#include <span.h>
#include <streams.h>
#include <util/fs.h>

#include <lmdb.h>

#include <cassert>
#include <cstring>
#include <sys/stat.h>

// Metrics collector
#include <metrics_collector.hpp>
extern BenchmarkMetricsCollector g_metrics_collector;

// Key prefixes matching LevelDB schema
static constexpr uint8_t DB_COIN = 'C';
static constexpr uint8_t DB_BEST_BLOCK = 'B';
static constexpr uint8_t DB_HEAD_BLOCKS = 'H';

CCoinsViewDB_LMDB::CCoinsViewDB_LMDB(const DBParams& db_params, const CoinsViewOptions& options)
    : m_path(PathToString(db_params.path)), m_map_size(db_params.cache_bytes),
      m_total_put(0), m_total_del(0), m_file_size_last(0) {
    
#ifdef DEBUG
    std::cerr << "DBG: Opening LMDB at " << m_path << "\n";
#endif
    
    int rc = mdb_env_create(&m_env);
    if (rc != MDB_SUCCESS) {
        std::cerr << "LMDB ERROR: mdb_env_create failed, rc=" << rc << " (" << mdb_strerror(rc) << ")\n";
    }
    assert(rc == MDB_SUCCESS);
    
    rc = mdb_env_set_mapsize(m_env, m_map_size);
    if (rc != MDB_SUCCESS) {
        std::cerr << "LMDB ERROR: mdb_env_set_mapsize failed (mapsize=" << m_map_size << "), rc=" << rc << " (" << mdb_strerror(rc) << ")\n";
    }
    assert(rc == MDB_SUCCESS);
    
    // Create directory for LMDB files
    fs::create_directories(m_path);
    rc = mdb_env_open(m_env, m_path.c_str(), MDB_WRITEMAP, 0664);
    if (rc != MDB_SUCCESS) {
        std::cerr << "LMDB ERROR: mdb_env_open failed (path=" << m_path << "), rc=" << rc << " (" << mdb_strerror(rc) << ")\n";
    }
    assert(rc == MDB_SUCCESS);
    
    // Clear any stale reader locks from previous crashed runs
    int dead_readers = 0;
    mdb_reader_check(m_env, &dead_readers);
    if (dead_readers > 0) {
        std::cerr << "LMDB: Cleared " << dead_readers << " stale reader locks\n";
    }
    
    // Test active readers immediately after env_open
#ifdef DEBUG
    TestActiveReaders();
#endif
    
    MDB_txn* txn = nullptr;
    rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) {
        std::cerr << "LMDB ERROR: mdb_txn_begin (init) failed, rc=" << rc << " (" << mdb_strerror(rc) << ")\n";
    }
    assert(rc == MDB_SUCCESS);
    
    rc = mdb_dbi_open(txn, nullptr, MDB_CREATE, &m_dbi);
    if (rc != MDB_SUCCESS) {
        std::cerr << "LMDB ERROR: mdb_dbi_open failed, rc=" << rc << " (" << mdb_strerror(rc) << ")\n";
    }
    assert(rc == MDB_SUCCESS);
    
    mdb_txn_commit(txn);

    // Check if database is empty
#ifdef DEBUG
    uint256 best_block = GetBestBlock();
    std::cerr << "DBG: LMDB opened, GetBestBlock()=" << best_block.ToString() << " isNull=" << best_block.IsNull() << "\n";
#endif
}

CCoinsViewDB_LMDB::~CCoinsViewDB_LMDB() {
    if (m_env) {
        mdb_dbi_close(m_env, m_dbi);
        mdb_env_close(m_env);
    }
}

// Helper to get file size in bytes
static uint64_t GetFileSize(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        return st.st_size;
    }
    return 0;
}

// RAII wrapper for LMDB read transactions
// Ensures transactions are always aborted, even if exceptions occur
class ScopedReadTxn {
    MDB_env* m_env;
    MDB_txn* m_txn;
    const char* m_context;
public:
    explicit ScopedReadTxn(MDB_env* env, const char* context = nullptr) 
        : m_env(env), m_txn(nullptr), m_context(context) {
        int rc = mdb_txn_begin(env, nullptr, MDB_RDONLY, &m_txn);
        if (rc != MDB_SUCCESS) {
            std::cerr << "LMDB ERROR: ScopedReadTxn mdb_txn_begin failed";
            if (context) {
                std::cerr << " (" << context << ")";
            }
            std::cerr << " rc=" << rc << " (" << mdb_strerror(rc) << ")\n";
        }
        // Note: We don't assert here to allow caller to handle the error
    }
    
    ~ScopedReadTxn() {
        if (m_txn) {
            mdb_txn_abort(m_txn);
        }
    }
    
    MDB_txn* get() const { return m_txn; }
    explicit operator bool() const { return m_txn != nullptr; }
};

std::optional<Coin> CCoinsViewDB_LMDB::GetCoin(const COutPoint& outpoint) const {
    ScopedTimer timer(g_metrics_collector.stats_db_get_coin);
    ScopedReadTxn read_txn(const_cast<MDB_env*>(m_env), "GetCoin");
    
    DataStream ssKey;
    ssKey << DB_COIN << outpoint.hash << VARINT(outpoint.n);
    
    MDB_val key;
    key.mv_size = ssKey.size();
    key.mv_data = reinterpret_cast<char*>(ssKey.data());
    
    MDB_val value;
    int rc = mdb_get(read_txn.get(), m_dbi, &key, &value);
    
    if (rc != MDB_SUCCESS) {
        return std::nullopt;
    }
    
    try {
        DataStream ssValue;
        ssValue.write(Span<const std::byte>(reinterpret_cast<const std::byte*>(value.mv_data), value.mv_size));
        Coin coin;
        ssValue >> coin;
        return coin;
    } catch (...) {
        return std::nullopt;
    }
}

bool CCoinsViewDB_LMDB::HaveCoin(const COutPoint& outpoint) const {
    ScopedTimer timer(g_metrics_collector.stats_db_have_coin);
    ScopedReadTxn read_txn(const_cast<MDB_env*>(m_env), "HaveCoin");
    
    DataStream ssKey;
    ssKey << DB_COIN << outpoint.hash << VARINT(outpoint.n);
    
    MDB_val key;
    key.mv_size = ssKey.size();
    key.mv_data = reinterpret_cast<char*>(ssKey.data());
    
    MDB_val value;
    int rc = mdb_get(read_txn.get(), m_dbi, &key, &value);
    
    return rc == MDB_SUCCESS;
}

uint256 CCoinsViewDB_LMDB::GetBestBlock() const {
    ScopedTimer timer(g_metrics_collector.stats_db_get_best_block);
    ScopedReadTxn read_txn(const_cast<MDB_env*>(m_env), "GetBestBlock");
    
    MDB_val key;
    key.mv_size = 1;
    uint8_t best_block_key = DB_BEST_BLOCK;
    key.mv_data = reinterpret_cast<char*>(&best_block_key);
    
    MDB_val value;
    int rc = mdb_get(read_txn.get(), m_dbi, &key, &value);
    
    if (rc != MDB_SUCCESS) {
        return uint256();
    }
    
    try {
        DataStream ssValue;
        ssValue.write(Span<const std::byte>(reinterpret_cast<const std::byte*>(value.mv_data), value.mv_size));
        uint256 hash;
        ssValue >> hash;
        return hash;
    } catch (...) {
        return uint256();
    }
}

std::vector<uint256> CCoinsViewDB_LMDB::GetHeadBlocks() const {
    ScopedTimer timer(g_metrics_collector.stats_db_get_head_blocks);
    ScopedReadTxn read_txn(const_cast<MDB_env*>(m_env), "GetHeadBlocks");
    
    MDB_val key;
    key.mv_size = 1;
    uint8_t head_blocks_key = DB_HEAD_BLOCKS;
    key.mv_data = reinterpret_cast<char*>(&head_blocks_key);
    
    MDB_val value;
    int rc = mdb_get(read_txn.get(), m_dbi, &key, &value);
    
    if (rc != MDB_SUCCESS) {
        return std::vector<uint256>();
    }
    
    try {
        DataStream ssValue;
        ssValue.write(Span<const std::byte>(reinterpret_cast<const std::byte*>(value.mv_data), value.mv_size));
        std::vector<uint256> heads;
        ssValue >> heads;
        return heads;
    } catch (...) {
        return std::vector<uint256>();
    }
}

bool CCoinsViewDB_LMDB::BatchWrite(CoinsViewCacheCursor& cursor, const uint256& hashBlock) {
    ScopedTimer timer(g_metrics_collector.stats_db_batch_write);

    uint256 old_tip;
    std::vector<uint256> old_heads;
    
    // Diagnostic tracking
    size_t local_put = 0;
    size_t local_del = 0;

    // CRITICAL FIX: Read old_tip and old_heads BEFORE starting write transaction.
    // LMDB does not allow simultaneous read/write transactions on the same thread.
    // Reading inside a write txn was creating implicit reader slots that were never
    // released, causing me_numreaders to accumulate and bloat the map file.
    if (!hashBlock.IsNull()) {
        // Read best_block outside of any write transaction
        {
            ScopedReadTxn read_txn(const_cast<MDB_env*>(m_env), "BatchWrite-best_block");
            MDB_val key;
            key.mv_size = 1;
            uint8_t best_block_key = DB_BEST_BLOCK;
            key.mv_data = reinterpret_cast<char*>(&best_block_key);
            
            MDB_val value;
            int rc = mdb_get(read_txn.get(), m_dbi, &key, &value);
            if (rc != MDB_SUCCESS && rc != MDB_NOTFOUND) {
                std::cerr << "LMDB ERROR: BatchWrite mdb_get (best_block) failed, rc=" << rc << " (" << mdb_strerror(rc) << ") block=" << hashBlock.ToString() << "\n";
            }
            if (rc == MDB_SUCCESS) {
                try {
                    DataStream ssValue;
                    ssValue.write(Span<const std::byte>(reinterpret_cast<const std::byte*>(value.mv_data), value.mv_size));
                    ssValue >> old_tip;
                } catch (...) {
                    old_tip = uint256();
                }
            } else {
                old_tip = uint256();
            }
        }

        if (old_tip.IsNull()) {
            // Read head_blocks outside of any write transaction
            ScopedReadTxn read_txn(const_cast<MDB_env*>(m_env), "BatchWrite-head_blocks");
            MDB_val key;
            key.mv_size = 1;
            uint8_t head_blocks_key = DB_HEAD_BLOCKS;
            key.mv_data = reinterpret_cast<char*>(&head_blocks_key);
            
            MDB_val value;
            int rc = mdb_get(read_txn.get(), m_dbi, &key, &value);
            if (rc != MDB_SUCCESS && rc != MDB_NOTFOUND) {
                std::cerr << "LMDB ERROR: BatchWrite mdb_get (head_blocks) failed, rc=" << rc << " (" << mdb_strerror(rc) << ") block=" << hashBlock.ToString() << "\n";
            }
            if (rc == MDB_SUCCESS) {
                try {
                    DataStream ssValue;
                    ssValue.write(Span<const std::byte>(reinterpret_cast<const std::byte*>(value.mv_data), value.mv_size));
                    ssValue >> old_heads;
                } catch (...) {
                    old_heads.clear();
                }
            }
            if (old_heads.size() == 2) {
                assert(old_heads[0] == hashBlock);
                old_tip = old_heads[1];
            }
        }
    }

    // NOW start the write transaction after all reads are complete
    MDB_txn* txn = nullptr;
    int rc = mdb_txn_begin(const_cast<MDB_env*>(m_env), nullptr, 0, &txn);
    if (rc != MDB_SUCCESS) {
        std::cerr << "LMDB ERROR: BatchWrite mdb_txn_begin failed, rc=" << rc << " (" << mdb_strerror(rc) << ") block=" << hashBlock.ToString() << "\n";
    }
    assert(rc == MDB_SUCCESS);

    if (!hashBlock.IsNull()) {
        // Mark transition
        {
            MDB_val key;
            key.mv_size = 1;
            uint8_t best_block_key = DB_BEST_BLOCK;
            key.mv_data = reinterpret_cast<char*>(&best_block_key);
            rc = mdb_del(txn, m_dbi, &key, nullptr);
            if (rc != MDB_SUCCESS && rc != MDB_NOTFOUND) {
                std::cerr << "LMDB ERROR: BatchWrite mdb_del (best_block transition) failed, rc=" << rc << " (" << mdb_strerror(rc) << ") block=" << hashBlock.ToString() << "\n";
            }
            // MDB_NOTFOUND is acceptable if the key doesn't exist yet (first write)
            assert(rc == MDB_SUCCESS || rc == MDB_NOTFOUND);
        }
        
        {
            DataStream ssKey, ssValue;
            ssKey << DB_HEAD_BLOCKS;
            ssValue << std::vector<uint256>{hashBlock, old_tip};
            
            MDB_val key, value;
            key.mv_size = ssKey.size();
            key.mv_data = reinterpret_cast<char*>(ssKey.data());
            value.mv_size = ssValue.size();
            value.mv_data = reinterpret_cast<char*>(ssValue.data());
            
            rc = mdb_put(txn, m_dbi, &key, &value, 0);
            if (rc != MDB_SUCCESS) {
                std::cerr << "LMDB ERROR: BatchWrite mdb_put (head_blocks transition) failed, rc=" << rc << " (" << mdb_strerror(rc) << ") block=" << hashBlock.ToString() << "\n";
            }
            assert(rc == MDB_SUCCESS);
        }

        // Process cursor - sort keys first for B-tree friendly writes
        // Collect dirty entries
        std::vector<std::pair<COutPoint, CCoinsCacheEntry>> dirty_entries;
        for (auto it{cursor.Begin()}; it != cursor.End(); it = cursor.NextAndMaybeErase(*it)) {
            if (it->second.IsDirty()) {
                dirty_entries.emplace_back(it->first, it->second);
            }
        }
        
        // Sort by COutPoint for sequential B-tree inserts
        std::sort(dirty_entries.begin(), dirty_entries.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        
        // Write in sorted order
        for (const auto& [outpoint, entry] : dirty_entries) {
            DataStream ssKey;
            ssKey << DB_COIN << outpoint.hash << VARINT(outpoint.n);
            
            MDB_val key;
            key.mv_size = ssKey.size();
            key.mv_data = reinterpret_cast<char*>(ssKey.data());
            
            if (entry.coin.IsSpent()) {
                local_del++;
                rc = mdb_del(txn, m_dbi, &key, nullptr);
                if (rc != MDB_SUCCESS && rc != MDB_NOTFOUND) {
                    std::cerr << "LMDB ERROR: BatchWrite mdb_del (coin) failed for " << outpoint.hash.ToString() << ":" << outpoint.n 
                              << " rc=" << rc << " (" << mdb_strerror(rc) << ") block=" << hashBlock.ToString() << "\n";
                }
                // MDB_NOTFOUND is acceptable if the coin doesn't exist
                assert(rc == MDB_SUCCESS || rc == MDB_NOTFOUND);
            } else {
                local_put++;
                DataStream ssValue;
                ssValue << entry.coin;
                
                MDB_val value;
                value.mv_size = ssValue.size();
                value.mv_data = reinterpret_cast<char*>(ssValue.data());
                
                rc = mdb_put(txn, m_dbi, &key, &value, 0);
                if (rc != MDB_SUCCESS) {
                    std::cerr << "LMDB ERROR: BatchWrite mdb_put (coin) failed for " << outpoint.hash.ToString() << ":" << outpoint.n 
                              << " value_size=" << value.mv_size 
                              << " rc=" << rc << " (" << mdb_strerror(rc) << ") block=" << hashBlock.ToString() << "\n";
                }
                assert(rc == MDB_SUCCESS);
            }
        }

        // Mark consistent
        {
            MDB_val key;
            key.mv_size = 1;
            uint8_t head_blocks_key = DB_HEAD_BLOCKS;
            key.mv_data = reinterpret_cast<char*>(&head_blocks_key);
            rc = mdb_del(txn, m_dbi, &key, nullptr);
            if (rc != MDB_SUCCESS && rc != MDB_NOTFOUND) {
                std::cerr << "LMDB ERROR: BatchWrite mdb_del (head_blocks consistent) failed, rc=" << rc << " (" << mdb_strerror(rc) << ") block=" << hashBlock.ToString() << "\n";
            }
            // MDB_NOTFOUND is acceptable if the key doesn't exist yet
            assert(rc == MDB_SUCCESS || rc == MDB_NOTFOUND);
        }
        
        {
            DataStream ssKey, ssValue;
            ssKey << DB_BEST_BLOCK;
            ssValue << hashBlock;
            
            MDB_val key, value;
            key.mv_size = ssKey.size();
            key.mv_data = reinterpret_cast<char*>(ssKey.data());
            value.mv_size = ssValue.size();
            value.mv_data = reinterpret_cast<char*>(ssValue.data());
            
            rc = mdb_put(txn, m_dbi, &key, &value, 0);
            if (rc != MDB_SUCCESS) {
                std::cerr << "LMDB ERROR: BatchWrite mdb_put (best_block) failed, rc=" << rc << " (" << mdb_strerror(rc) << ") block=" << hashBlock.ToString() << "\n";
            }
            assert(rc == MDB_SUCCESS);
        }
    }

    rc = mdb_txn_commit(txn);
    if (rc != MDB_SUCCESS) {
        std::cerr << "LMDB ERROR: BatchWrite mdb_txn_commit failed, rc=" << rc << " (" << mdb_strerror(rc) << ") block=" << hashBlock.ToString() << "\n";
    }
    
    // Update diagnostic counters
    m_total_put += local_put;
    m_total_del += local_del;
    
    // Print diagnostics periodically
    #ifdef DEBUG
    if (local_put + local_del > 0) {
        MDB_stat stat;
        MDB_envinfo einfo;
        mdb_env_stat(m_env, &stat);
        mdb_env_info(m_env, &einfo);
        uint64_t used_pages = stat.ms_branch_pages + stat.ms_leaf_pages + stat.ms_overflow_pages;
        uint64_t used_bytes = used_pages * stat.ms_psize;
        uint64_t file_size = GetFileSize(m_path + "/data.mdb");
        
        std::cerr << "LMDB DIAG: puts=" << m_total_put << " dels=" << m_total_del
                  << " net=" << (m_total_put - m_total_del)
                  << " file=" << file_size/1024/1024 << "MB"
                  << " used=" << used_bytes/1024/1024 << "MB"
                  << " overhead=" << (file_size - used_bytes)/1024/1024 << "MB"
                  << " entries=" << stat.ms_entries
                  << " pages_used=" << used_pages
                  << " ACTIVE_READERS=" << einfo.me_numreaders
                  << " block=" << hashBlock.ToString()
                  << "\n";
    }
    #endif
    
    return rc == MDB_SUCCESS;
}

std::unique_ptr<CCoinsViewCursor> CCoinsViewDB_LMDB::Cursor() const {
    ScopedTimer timer(g_metrics_collector.stats_db_cursor);
    return nullptr;  // TODO: Implement cursor for LMDB
}

// Test method to verify reader count behavior
void CCoinsViewDB_LMDB::TestActiveReaders() const {
    MDB_envinfo einfo;
    mdb_env_info(m_env, &einfo);
    std::cerr << "TEST: Initial ACTIVE_READERS=" << einfo.me_numreaders << "\n";

    {
        ScopedReadTxn read_txn(const_cast<MDB_env*>(m_env), "Test");
        mdb_env_info(m_env, &einfo);
        std::cerr << "TEST: During read txn, ACTIVE_READERS=" << einfo.me_numreaders << "\n";
    }

    // Check for stale locks after abort
    int dead_readers = 0;
    mdb_reader_check(m_env, &dead_readers);
    if (dead_readers > 0) {
        std::cerr << "TEST: Cleared " << dead_readers << " stale reader locks\n";
    }

    mdb_env_info(m_env, &einfo);
    std::cerr << "TEST: After read txn closed + reader_check, ACTIVE_READERS=" << einfo.me_numreaders << "\n";
}

size_t CCoinsViewDB_LMDB::EstimateSize() const {
    ScopedTimer timer(g_metrics_collector.stats_db_estimate_size);
    // TODO: Implement proper size estimation
    return 0;
}

std::unique_ptr<CCoinsView> CreateLMDBView(const std::string& db_path, size_t cache_size) {
    DBParams db_params;
    db_params.path = fs::u8path(db_path.c_str());
    // For LMDB, mapsize needs to be large enough for the entire database
    // The chainstate is ~12GB, but we need extra space for growth and overhead
    db_params.cache_bytes = 32ULL * 1024 * 1024 * 1024;
    db_params.memory_only = false;
    db_params.wipe_data = false;
    db_params.obfuscate = false;

    CoinsViewOptions options;
    options.batch_write_bytes = 64 * 1024 * 1024;
    options.simulate_crash_ratio = 0;

    return std::make_unique<CCoinsViewDB_LMDB>(db_params, options);
}

// Factory function for the main benchmark
std::unique_ptr<CCoinsView> CreateDatabaseView(const std::string& db_path, size_t cache_size) {
    return CreateLMDBView(db_path, cache_size);
}
