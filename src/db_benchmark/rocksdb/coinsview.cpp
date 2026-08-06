// Copyright (c) 2026 The Bitcoin Knots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

// Debug flag - uncomment to enable debug output
// #define DEBUG 1

#include <rocksdb/coinsview.hpp>

#include <serialize.h>
#include <span.h>
#include <streams.h>
#include <util/fs.h>

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/write_batch.h>

#include <cassert>

// Metrics collector
#include <metrics_collector.hpp>
extern BenchmarkMetricsCollector g_metrics_collector;

// Key prefixes matching LevelDB schema
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

CCoinsViewDB_RocksDB::CCoinsViewDB_RocksDB(const DBParams& db_params, const CoinsViewOptions& options)
    : m_path(PathToString(db_params.path)), m_cache_size(db_params.cache_bytes) {
    
#ifdef DEBUG
    std::cerr << "DBG: Opening RocksDB at " << m_path << "\n";
#endif
    
    m_options.create_if_missing = true;
    m_options.write_buffer_size = 256 * 1024 * 1024;  // 256MB
    m_options.compression = rocksdb::kNoCompression;
    m_options.target_file_size_base = 64 * 1024 * 1024;  // 64MB - match Bitcoin Knots
    
    rocksdb::Status status = rocksdb::DB::Open(m_options, m_path, &m_db);
    assert(status.ok());
    
    // Check if database is empty
    uint256 best_block = GetBestBlock();
#ifdef DEBUG
    std::cerr << "DBG: RocksDB opened, GetBestBlock()=" << best_block.ToString() << " isNull=" << best_block.IsNull() << "\n";
#endif
}

CCoinsViewDB_RocksDB::~CCoinsViewDB_RocksDB() {
    if (m_db) {
        delete m_db;
    }
}

std::optional<Coin> CCoinsViewDB_RocksDB::GetCoin(const COutPoint& outpoint) const {
    ScopedTimer timer(g_metrics_collector.stats_db_get_coin);
    CoinEntry entry(&outpoint);
    DataStream ssKey;
    ssKey << entry;
    
    std::string value;
    rocksdb::Status status = m_db->Get(rocksdb::ReadOptions(), ssKey.str(), &value);
    
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

bool CCoinsViewDB_RocksDB::HaveCoin(const COutPoint& outpoint) const {
    ScopedTimer timer(g_metrics_collector.stats_db_have_coin);
    CoinEntry entry(&outpoint);
    DataStream ssKey;
    ssKey << entry;
    
    std::string value;
    rocksdb::Status status = m_db->Get(rocksdb::ReadOptions(), ssKey.str(), &value);
    return status.ok();
}

uint256 CCoinsViewDB_RocksDB::GetBestBlock() const {
    ScopedTimer timer(g_metrics_collector.stats_db_get_best_block);
    std::string value;
    rocksdb::Status status = m_db->Get(rocksdb::ReadOptions(), std::string(1, DB_BEST_BLOCK), &value);
    
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

std::vector<uint256> CCoinsViewDB_RocksDB::GetHeadBlocks() const {
    ScopedTimer timer(g_metrics_collector.stats_db_get_head_blocks);
    std::string value;
    rocksdb::Status status = m_db->Get(rocksdb::ReadOptions(), std::string(1, DB_HEAD_BLOCKS), &value);
    
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

bool CCoinsViewDB_RocksDB::BatchWrite(CoinsViewCacheCursor& cursor, const uint256& hashBlock) {
    ScopedTimer timer(g_metrics_collector.stats_db_batch_write);
    rocksdb::WriteBatch batch;

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

    rocksdb::Status status = m_db->Write(rocksdb::WriteOptions(), &batch);
    return status.ok();
}

std::unique_ptr<CCoinsViewCursor> CCoinsViewDB_RocksDB::Cursor() const {
    ScopedTimer timer(g_metrics_collector.stats_db_cursor);
    return nullptr;  // TODO: Implement cursor for RocksDB
}

size_t CCoinsViewDB_RocksDB::EstimateSize() const {
    ScopedTimer timer(g_metrics_collector.stats_db_estimate_size);
    size_t size = 0;
    rocksdb::Range range(std::string(1, DB_COIN), std::string(1, DB_COIN + 1));
    m_db->GetApproximateSizes(&range, 1, &size);
    return size;
}

// Factory function for the main benchmark
std::unique_ptr<CCoinsView> CreateDatabaseView(const std::string& db_path, size_t cache_size) {
    DBParams db_params;
    db_params.path = fs::u8path(db_path.c_str());
    db_params.cache_bytes = cache_size;
    db_params.memory_only = false;
    db_params.wipe_data = false;
    db_params.obfuscate = false;

    CoinsViewOptions options;
    options.batch_write_bytes = 64 * 1024 * 1024;
    options.simulate_crash_ratio = 0;

    return std::make_unique<CCoinsViewDB_RocksDB>(db_params, options);
}
