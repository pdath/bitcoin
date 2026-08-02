// Copyright (c) 2026 The Bitcoin Knots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BENCHMARK_LEVELDB_COINSVIEW_HPP
#define BITCOIN_BENCHMARK_LEVELDB_COINSVIEW_HPP

#include <coins.h>
#include <txdb.h>  // For DBParams, CoinsViewOptions

// Forward declaration for LevelDB
namespace leveldb {
    class DB;
}

#include <leveldb/options.h>

/**
 * LevelDB implementation of CCoinsView.
 * This is a separate implementation that uses LevelDB directly.
 */
class CCoinsViewDB_LevelDB : public CCoinsView {
private:
    leveldb::DB* m_db;
    leveldb::Options m_options;
    std::string m_path;
    size_t m_cache_size;

public:
    CCoinsViewDB_LevelDB(const DBParams& db_params, const CoinsViewOptions& options);
    ~CCoinsViewDB_LevelDB();

    std::optional<Coin> GetCoin(const COutPoint& outpoint) const override;
    bool HaveCoin(const COutPoint& outpoint) const override;
    uint256 GetBestBlock() const override;
    std::vector<uint256> GetHeadBlocks() const override;
    bool BatchWrite(CoinsViewCacheCursor& cursor, const uint256& hashBlock) override;
    std::unique_ptr<CCoinsViewCursor> Cursor() const override;
    size_t EstimateSize() const override;
};

/**
 * Create a LevelDB-based CCoinsView instance.
 */
std::unique_ptr<CCoinsView> CreateLevelDBView(const std::string& db_path, size_t cache_size);

#endif // BITCOIN_BENCHMARK_LEVELDB_COINSVIEW_HPP
