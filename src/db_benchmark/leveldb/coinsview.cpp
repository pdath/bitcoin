// Copyright (c) 2026 The Bitcoin Knots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#include <leveldb/coinsview.hpp>

#include <serialize.h>
#include <streams.h>
#include <util/fs.h>

#include <leveldb/db.h>
#include <leveldb/options.h>
#include <leveldb/write_batch.h>
#include <leveldb/cache.h>
#include <leveldb/filter_policy.h>

#include <cassert>
#include <cstddef>

// Metrics collector
#include <metrics_collector.hpp>
extern BenchmarkMetricsCollector g_metrics_collector;

// Key prefixes matching Bitcoin Knots schema
static constexpr uint8_t DB_COIN = 'C';
static constexpr uint8_t DB_BEST_BLOCK = 'B';
static constexpr uint8_t DB_HEAD_BLOCKS = 'H';

struct CoinEntry {
    const COutPoint* outpoint;
    uint8_t key;
    explicit CoinEntry(const COutPoint* ptr) : outpoint(ptr), key(DB_COIN) {}

    template<typename Stream>
    void Serialize(Stream& s) const {
        s << key << outpoint->hash << VARINT(outpoint->n);
    }
};

CCoinsViewDB_LevelDB::CCoinsViewDB_LevelDB(const DBParams& db_params, const CoinsViewOptions& options)
    : m_path(PathToString(db_params.path)), m_cache_size(db_params.cache_bytes) {
    
    m_options.create_if_missing = true;
    m_options.write_buffer_size = 256 * 1024 * 1024;  // 256MB
    m_options.block_cache = leveldb::NewLRUCache(m_cache_size / 2);  // Use half for block cache
    m_options.filter_policy = leveldb::NewBloomFilterPolicy(10);
    m_options.compression = leveldb::kNoCompression;
    m_options.max_file_size = 64 * 1024 * 1024;  // 64MB - match Bitcoin Knots

    leveldb::Status status = leveldb::DB::Open(m_options, m_path, &m_db);
    assert(status.ok());
}

CCoinsViewDB_LevelDB::~CCoinsViewDB_LevelDB() {
    if (m_db) {
        delete m_db;
    }
}

std::optional<Coin> CCoinsViewDB_LevelDB::GetCoin(const COutPoint& outpoint) const {
    ScopedTimer timer(g_metrics_collector.stats_db_get_coin);
    CoinEntry entry(&outpoint);
    DataStream ssKey;
    ssKey << entry;
    
    std::string value;
    leveldb::Status status = m_db->Get(leveldb::ReadOptions(), ssKey.str(), &value);
    
    if (!status.ok() || status.IsNotFound()) {
        return std::nullopt;
    }
    
    try {
        DataStream ssValue;
        ssValue.write(Span<const std::byte>(reinterpret_cast<const std::byte*>(value.data()), value.size()));
        Coin coin;
        ssValue >> coin;
        return coin;
    } catch (...) {
        return std::nullopt;
    }
}

bool CCoinsViewDB_LevelDB::HaveCoin(const COutPoint& outpoint) const {
    ScopedTimer timer(g_metrics_collector.stats_db_have_coin);
    CoinEntry entry(&outpoint);
    DataStream ssKey;
    ssKey << entry;
    
    std::string value;
    leveldb::Status status = m_db->Get(leveldb::ReadOptions(), ssKey.str(), &value);
    return status.ok();
}

uint256 CCoinsViewDB_LevelDB::GetBestBlock() const {
    ScopedTimer timer(g_metrics_collector.stats_db_get_best_block);
    std::string value;
    leveldb::Status status = m_db->Get(leveldb::ReadOptions(), std::string(1, DB_BEST_BLOCK), &value);
    
    if (!status.ok() || status.IsNotFound()) {
        return uint256();
    }
    
    try {
        DataStream ssValue;
        ssValue.write(Span<const std::byte>(reinterpret_cast<const std::byte*>(value.data()), value.size()));
        uint256 hash;
        ssValue >> hash;
        return hash;
    } catch (...) {
        return uint256();
    }
}

std::vector<uint256> CCoinsViewDB_LevelDB::GetHeadBlocks() const {
    ScopedTimer timer(g_metrics_collector.stats_db_get_head_blocks);
    std::string value;
    leveldb::Status status = m_db->Get(leveldb::ReadOptions(), std::string(1, DB_HEAD_BLOCKS), &value);
    
    if (!status.ok() || status.IsNotFound()) {
        return std::vector<uint256>();
    }
    
    try {
        DataStream ssValue;
        ssValue.write(Span<const std::byte>(reinterpret_cast<const std::byte*>(value.data()), value.size()));
        std::vector<uint256> heads;
        ssValue >> heads;
        return heads;
    } catch (...) {
        return std::vector<uint256>();
    }
}

bool CCoinsViewDB_LevelDB::BatchWrite(CoinsViewCacheCursor& cursor, const uint256& hashBlock) {
    ScopedTimer timer(g_metrics_collector.stats_db_batch_write);
    leveldb::WriteBatch batch;

    if (!hashBlock.IsNull()) {
        // Full block write with best block tracking
        uint256 old_tip = GetBestBlock();
        if (old_tip.IsNull()) {
            std::vector<uint256> old_heads = GetHeadBlocks();
            if (old_heads.size() == 2) {
                assert(old_heads[0] == hashBlock);
                old_tip = old_heads[1];
            }
        }

        // Mark transition
        batch.Delete(std::string(1, DB_BEST_BLOCK));
        {
            DataStream ssKey, ssValue;
            ssKey << DB_HEAD_BLOCKS;
            ssValue << std::vector<uint256>{hashBlock, old_tip};
            batch.Put(ssKey.str(), ssValue.str());
        }
    }

    // Process cursor
    for (auto it{cursor.Begin()}; it != cursor.End();) {
        if (it->second.IsDirty()) {
            CoinEntry entry(&it->first);
            DataStream ssKey;
            ssKey << entry;
            
            if (it->second.coin.IsSpent()) {
                batch.Delete(ssKey.str());
            } else {
                DataStream ssValue;
                ssValue << it->second.coin;
                batch.Put(ssKey.str(), ssValue.str());
            }
        }
        it = cursor.NextAndMaybeErase(*it);
    }

    if (!hashBlock.IsNull()) {
        // Mark consistent
        batch.Delete(std::string(1, DB_HEAD_BLOCKS));
        {
            DataStream ssKey, ssValue;
            ssKey << DB_BEST_BLOCK;
            ssValue << hashBlock;
            batch.Put(ssKey.str(), ssValue.str());
        }
    }

    leveldb::Status status = m_db->Write(leveldb::WriteOptions(), &batch);
    return status.ok();
}

std::unique_ptr<CCoinsViewCursor> CCoinsViewDB_LevelDB::Cursor() const {
    ScopedTimer timer(g_metrics_collector.stats_db_cursor);
    return nullptr;  // TODO: Implement cursor for LevelDB
}

size_t CCoinsViewDB_LevelDB::EstimateSize() const {
    ScopedTimer timer(g_metrics_collector.stats_db_estimate_size);
    uint64_t size_val;
    leveldb::Range range(std::string(1, DB_COIN), std::string(1, DB_COIN + 1));
    m_db->GetApproximateSizes(&range, 1, &size_val);
    return static_cast<size_t>(size_val);
}

// Factory function for the main benchmark
std::unique_ptr<CCoinsView> CreateDatabaseView(const std::string& db_path, size_t cache_size) {
    DBParams db_params;
    db_params.path = fs::u8path(db_path.c_str());
    db_params.cache_bytes = cache_size;
    db_params.memory_only = false;
    db_params.wipe_data = true;
    db_params.obfuscate = false;

    CoinsViewOptions options;
    options.batch_write_bytes = 64 * 1024 * 1024;
    options.simulate_crash_ratio = 0;

    return std::make_unique<CCoinsViewDB_LevelDB>(db_params, options);
}
