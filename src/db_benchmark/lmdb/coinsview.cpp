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

// Metrics collector
#include <metrics_collector.hpp>
extern BenchmarkMetricsCollector g_metrics_collector;

// Key prefixes matching LevelDB schema
static constexpr uint8_t DB_COIN = 'C';
static constexpr uint8_t DB_BEST_BLOCK = 'B';
static constexpr uint8_t DB_HEAD_BLOCKS = 'H';

CCoinsViewDB_LMDB::CCoinsViewDB_LMDB(const DBParams& db_params, const CoinsViewOptions& options)
    : m_path(PathToString(db_params.path)), m_map_size(db_params.cache_bytes) {
    
#ifdef DEBUG
    std::cerr << "DBG: Opening LMDB at " << m_path << "\n";
#endif
    
    int rc = mdb_env_create(&m_env);
    assert(rc == MDB_SUCCESS);
    
    rc = mdb_env_set_mapsize(m_env, m_map_size);
    assert(rc == MDB_SUCCESS);
    
    rc = mdb_env_open(m_env, m_path.c_str(), MDB_NOSUBDIR, 0664);
    assert(rc == MDB_SUCCESS);
    
    MDB_txn* txn = nullptr;
    rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    assert(rc == MDB_SUCCESS);
    
    rc = mdb_dbi_open(txn, nullptr, MDB_CREATE, &m_dbi);
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

std::optional<Coin> CCoinsViewDB_LMDB::GetCoin(const COutPoint& outpoint) const {
    ScopedTimer timer(g_metrics_collector.stats_db_get_coin);
    MDB_txn* txn = nullptr;
    int rc = mdb_txn_begin(const_cast<MDB_env*>(m_env), nullptr, MDB_RDONLY, &txn);
    assert(rc == MDB_SUCCESS);
    
    DataStream ssKey;
    ssKey << DB_COIN << outpoint.hash << VARINT(outpoint.n);
    
    MDB_val key;
    key.mv_size = ssKey.size();
    key.mv_data = reinterpret_cast<char*>(ssKey.data());
    
    MDB_val value;
    rc = mdb_get(txn, m_dbi, &key, &value);
    mdb_txn_abort(txn);
    
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
    MDB_txn* txn = nullptr;
    int rc = mdb_txn_begin(const_cast<MDB_env*>(m_env), nullptr, MDB_RDONLY, &txn);
    assert(rc == MDB_SUCCESS);
    
    DataStream ssKey;
    ssKey << DB_COIN << outpoint.hash << VARINT(outpoint.n);
    
    MDB_val key;
    key.mv_size = ssKey.size();
    key.mv_data = reinterpret_cast<char*>(ssKey.data());
    
    MDB_val value;
    rc = mdb_get(txn, m_dbi, &key, &value);
    mdb_txn_abort(txn);
    
    return rc == MDB_SUCCESS;
}

uint256 CCoinsViewDB_LMDB::GetBestBlock() const {
    ScopedTimer timer(g_metrics_collector.stats_db_get_best_block);
    MDB_txn* txn = nullptr;
    int rc = mdb_txn_begin(const_cast<MDB_env*>(m_env), nullptr, MDB_RDONLY, &txn);
    assert(rc == MDB_SUCCESS);
    
    MDB_val key;
    key.mv_size = 1;
    uint8_t best_block_key = DB_BEST_BLOCK;
    key.mv_data = reinterpret_cast<char*>(&best_block_key);
    
    MDB_val value;
    rc = mdb_get(txn, m_dbi, &key, &value);
    mdb_txn_abort(txn);
    
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
    MDB_txn* txn = nullptr;
    int rc = mdb_txn_begin(const_cast<MDB_env*>(m_env), nullptr, MDB_RDONLY, &txn);
    assert(rc == MDB_SUCCESS);
    
    MDB_val key;
    key.mv_size = 1;
    uint8_t head_blocks_key = DB_HEAD_BLOCKS;
    key.mv_data = reinterpret_cast<char*>(&head_blocks_key);
    
    MDB_val value;
    rc = mdb_get(txn, m_dbi, &key, &value);
    mdb_txn_abort(txn);
    
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
    MDB_txn* txn = nullptr;
    int rc = mdb_txn_begin(const_cast<MDB_env*>(m_env), nullptr, 0, &txn);
    assert(rc == MDB_SUCCESS);

    uint256 old_tip;
    std::vector<uint256> old_heads;

    if (!hashBlock.IsNull()) {
        // Full block write with best block tracking
        // Read old_tip and old_heads within the write transaction to avoid
        // conflicting read/write transactions in LMDB
        {
            MDB_val key;
            key.mv_size = 1;
            uint8_t best_block_key = DB_BEST_BLOCK;
            key.mv_data = reinterpret_cast<char*>(&best_block_key);
            
            MDB_val value;
            rc = mdb_get(txn, m_dbi, &key, &value);
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
            MDB_val key;
            key.mv_size = 1;
            uint8_t head_blocks_key = DB_HEAD_BLOCKS;
            key.mv_data = reinterpret_cast<char*>(&head_blocks_key);
            
            MDB_val value;
            rc = mdb_get(txn, m_dbi, &key, &value);
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

        // Mark transition
        {
            MDB_val key;
            key.mv_size = 1;
            uint8_t best_block_key = DB_BEST_BLOCK;
            key.mv_data = reinterpret_cast<char*>(&best_block_key);
            rc = mdb_del(txn, m_dbi, &key, nullptr);
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
            assert(rc == MDB_SUCCESS);
        }
    }

    // Process cursor
    for (auto it{cursor.Begin()}; it != cursor.End();) {
        if (it->second.IsDirty()) {
            DataStream ssKey;
            ssKey << DB_COIN << it->first.hash << VARINT(it->first.n);
            
            MDB_val key;
            key.mv_size = ssKey.size();
            key.mv_data = reinterpret_cast<char*>(ssKey.data());
            
            if (it->second.coin.IsSpent()) {
                rc = mdb_del(txn, m_dbi, &key, nullptr);
                // MDB_NOTFOUND is acceptable if the coin doesn't exist
                assert(rc == MDB_SUCCESS || rc == MDB_NOTFOUND);
            } else {
                DataStream ssValue;
                ssValue << it->second.coin;
                
                MDB_val value;
                value.mv_size = ssValue.size();
                value.mv_data = reinterpret_cast<char*>(ssValue.data());
                
                rc = mdb_put(txn, m_dbi, &key, &value, 0);
                assert(rc == MDB_SUCCESS);
            }
        }
        it = cursor.NextAndMaybeErase(*it);
    }

    if (!hashBlock.IsNull()) {
        // Mark consistent
        {
            MDB_val key;
            key.mv_size = 1;
            uint8_t head_blocks_key = DB_HEAD_BLOCKS;
            key.mv_data = reinterpret_cast<char*>(&head_blocks_key);
            rc = mdb_del(txn, m_dbi, &key, nullptr);
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
            assert(rc == MDB_SUCCESS);
        }
    }

    rc = mdb_txn_commit(txn);
    return rc == MDB_SUCCESS;
}

std::unique_ptr<CCoinsViewCursor> CCoinsViewDB_LMDB::Cursor() const {
    ScopedTimer timer(g_metrics_collector.stats_db_cursor);
    return nullptr;  // TODO: Implement cursor for LMDB
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
    // The chainstate is ~12GB, so hard code to 16GB
    db_params.cache_bytes = 16ULL * 1024 * 1024 * 1024;
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
