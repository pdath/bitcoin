// Copyright (c) 2026 The Bitcoin Knots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BENCHMARK_LMDB_COINSVIEW_HPP
#define BITCOIN_BENCHMARK_LMDB_COINSVIEW_HPP

#include <coins.h>
#include <txdb.h>  // For DBParams, CoinsViewOptions

// Forward declaration for LMDB
typedef struct MDB_env MDB_env;
typedef unsigned int MDB_dbi;

/**
 * LMDB implementation of CCoinsView.
 */
class CCoinsViewDB_LMDB : public CCoinsView {
private:
    MDB_env* m_env;
    MDB_dbi m_dbi;
    std::string m_path;
    size_t m_map_size;

public:
    CCoinsViewDB_LMDB(const DBParams& db_params, const CoinsViewOptions& options);
    ~CCoinsViewDB_LMDB();

    std::optional<Coin> GetCoin(const COutPoint& outpoint) const override;
    bool HaveCoin(const COutPoint& outpoint) const override;
    uint256 GetBestBlock() const override;
    std::vector<uint256> GetHeadBlocks() const override;
    bool BatchWrite(CoinsViewCacheCursor& cursor, const uint256& hashBlock) override;
    std::unique_ptr<CCoinsViewCursor> Cursor() const override;
    size_t EstimateSize() const override;
};

/**
 * Create an LMDB-based CCoinsView instance.
 */
std::unique_ptr<CCoinsView> CreateLMDBView(const std::string& db_path, size_t cache_size);

#endif // BITCOIN_BENCHMARK_LMDB_COINSVIEW_HPP
