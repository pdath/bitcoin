// Copyright (c) 2026 The Bitcoin Knots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BENCHMARK_ROCKSDB_COINSVIEW_HPP
#define BITCOIN_BENCHMARK_ROCKSDB_COINSVIEW_HPP

#include <coins.h>
#include <txdb.h>  // For CCoinsViewDB base class definition

// Forward declaration for RocksDB
namespace rocksdb {
    class DB;
}

#include <rocksdb/options.h>

/**
 * RocksDB implementation of CCoinsView.
 * This is a separate implementation that uses RocksDB instead of LevelDB.
 */
class CCoinsViewDB_RocksDB : public CCoinsView {
private:
    rocksdb::DB* m_db;
    rocksdb::Options m_options;
    std::string m_path;
    size_t m_cache_size;

public:
    CCoinsViewDB_RocksDB(const DBParams& db_params, const CoinsViewOptions& options);
    ~CCoinsViewDB_RocksDB();

    std::optional<Coin> GetCoin(const COutPoint& outpoint) const override;
    bool HaveCoin(const COutPoint& outpoint) const override;
    uint256 GetBestBlock() const override;
    std::vector<uint256> GetHeadBlocks() const override;
    bool BatchWrite(CoinsViewCacheCursor& cursor, const uint256& hashBlock) override;
    std::unique_ptr<CCoinsViewCursor> Cursor() const override;
    size_t EstimateSize() const override;
};

/**
 * Create a RocksDB-based CCoinsView instance.
 */
std::unique_ptr<CCoinsView> CreateRocksDBView(const std::string& db_path, size_t cache_size);

#endif // BITCOIN_BENCHMARK_ROCKSDB_COINSVIEW_HPP
