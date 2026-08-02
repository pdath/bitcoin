// Copyright (c) 2026 The Bitcoin Knots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#include <lmdb/coinsview.hpp>

#include <serialize.h>
#include <streams.h>
#include <util/fs.h>

#include <lmdb.h>

#include <cassert>
#include <cstring>

// Key prefixes matching LevelDB schema
static constexpr uint8_t DB_COIN = 'C';
static constexpr uint8_t DB_BEST_BLOCK = 'B';
static constexpr uint8_t DB_HEAD_BLOCKS = 'H';

CCoinsViewDB_LMDB::CCoinsViewDB_LMDB(const DBParams& db_params, const CoinsViewOptions& options)
    : m_path(db_params.path.string()), m_map_size(db_params.cache_bytes) {
    
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
}

CCoinsViewDB_LMDB::~CCoinsViewDB_LMDB() {
    if (m_env) {
        mdb_dbi_close(m_env, m_dbi);
        mdb_env_close(m_env);
    }
}

std::optional<Coin> CCoinsViewDB_LMDB::GetCoin(const COutPoint& outpoint) const {
    MDB_txn* txn = nullptr;
    int rc = mdb_txn_begin(const_cast<MDB_env*>(m_env), nullptr, MDB_RDONLY, &txn);
    assert(rc == MDB_SUCCESS);
    
    DataStream ssKey;
    ssKey << DB_COIN << outpoint.hash << VARINT(outpoint.n);
    
    MDB_val key;
    key.mv_size = ssKey.size();
    key.mv_data = const_cast<char*>(ssKey.data());
    
    MDB_val value;
    rc = mdb_get(txn, m_dbi, &key, &value);
    mdb_txn_abort(txn);
    
    if (rc != MDB_SUCCESS) {
        return std::nullopt;
    }
    
    try {
        DataStream ssValue(std::string(static_cast<char*>(value.mv_data), value.mv_size));
        Coin coin;
        ssValue >> coin;
        return coin;
    } catch (...) {
        return std::nullopt;
    }
}

bool CCoinsViewDB_LMDB::HaveCoin(const COutPoint& outpoint) const {
    MDB_txn* txn = nullptr;
    int rc = mdb_txn_begin(const_cast<MDB_env*>(m_env), nullptr, MDB_RDONLY, &txn);
    assert(rc == MDB_SUCCESS);
    
    DataStream ssKey;
    ssKey << DB_COIN << outpoint.hash << VARINT(outpoint.n);
    
    MDB_val key;
    key.mv_size = ssKey.size();
    key.mv_data = const_cast<char*>(ssKey.data());
    
    MDB_val value;
    rc = mdb_get(txn, m_dbi, &key, &value);
    mdb_txn_abort(txn);
    
    return rc == MDB_SUCCESS;
}

uint256 CCoinsViewDB_LMDB::GetBestBlock() const {
    MDB_txn* txn = nullptr;
    int rc = mdb_txn_begin(const_cast<MDB_env*>(m_env), nullptr, MDB_RDONLY, &txn);
    assert(rc == MDB_SUCCESS);
    
    MDB_val key;
    key.mv_size = 1;
    key.mv_data = const_cast<char*>(&DB_BEST_BLOCK);
    
    MDB_val value;
    rc = mdb_get(txn, m_dbi, &key, &value);
    mdb_txn_abort(txn);
    
    if (rc != MDB_SUCCESS) {
        return uint256();
    }
    
    try {
        DataStream ssValue(std::string(static_cast<char*>(value.mv_data), value.mv_size));
        uint256 hash;
        ssValue >> hash;
        return hash;
    } catch (...) {
        return uint256();
    }
}

std::vector<uint256> CCoinsViewDB_LMDB::GetHeadBlocks() const {
    MDB_txn* txn = nullptr;
    int rc = mdb_txn_begin(const_cast<MDB_env*>(m_env), nullptr, MDB_RDONLY, &txn);
    assert(rc == MDB_SUCCESS);
    
    MDB_val key;
    key.mv_size = 1;
    key.mv_data = const_cast<char*>(&DB_HEAD_BLOCKS);
    
    MDB_val value;
    rc = mdb_get(txn, m_dbi, &key, &value);
    mdb_txn_abort(txn);
    
    if (rc != MDB_SUCCESS) {
        return std::vector<uint256>();
    }
    
    try {
        DataStream ssValue(std::string(static_cast<char*>(value.mv_data), value.mv_size));
        std::vector<uint256> heads;
        ssValue >> heads;
        return heads;
    } catch (...) {
        return std::vector<uint256>();
    }
}

bool CCoinsViewDB_LMDB::BatchWrite(CoinsViewCacheCursor& cursor, const uint256& hashBlock) {
    MDB_txn* txn = nullptr;
    int rc = mdb_txn_begin(const_cast<MDB_env*>(m_env), nullptr, 0, &txn);
    assert(rc == MDB_SUCCESS);
    
    assert(!hashBlock.IsNull());

    uint256 old_tip = GetBestBlock();
    if (old_tip.IsNull()) {
        std::vector<uint256> old_heads = GetHeadBlocks();
        if (old_heads.size() == 2) {
            assert(old_heads[0] == hashBlock);
            old_tip = old_heads[1];
        }
    }

    // Mark transition
    {
        MDB_val key;
        key.mv_size = 1;
        key.mv_data = const_cast<char*>(&DB_BEST_BLOCK);
        rc = mdb_del(txn, m_dbi, &key, nullptr);
        assert(rc == MDB_SUCCESS);
    }
    
    {
        DataStream ssKey, ssValue;
        ssKey << DB_HEAD_BLOCKS;
        ssValue << std::vector<uint256>{hashBlock, old_tip};
        
        MDB_val key, value;
        key.mv_size = ssKey.size();
        key.mv_data = const_cast<char*>(ssKey.data());
        value.mv_size = ssValue.size();
        value.mv_data = const_cast<char*>(ssValue.data());
        
        rc = mdb_put(txn, m_dbi, &key, &value, 0);
        assert(rc == MDB_SUCCESS);
    }

    // Process cursor
    for (auto it{cursor.Begin()}; it != cursor.End();) {
        if (it->second.IsDirty()) {
            DataStream ssKey;
            ssKey << DB_COIN << it->first.hash << VARINT(it->first.n);
            
            MDB_val key;
            key.mv_size = ssKey.size();
            key.mv_data = const_cast<char*>(ssKey.data());
            
            if (it->second.coin.IsSpent()) {
                rc = mdb_del(txn, m_dbi, &key, nullptr);
                assert(rc == MDB_SUCCESS);
            } else {
                DataStream ssValue;
                ssValue << it->second.coin;
                
                MDB_val value;
                value.mv_size = ssValue.size();
                value.mv_data = const_cast<char*>(ssValue.data());
                
                rc = mdb_put(txn, m_dbi, &key, &value, 0);
                assert(rc == MDB_SUCCESS);
            }
        }
        it = cursor.NextAndMaybeErase(*it);
    }

    // Mark consistent
    {
        MDB_val key;
        key.mv_size = 1;
        key.mv_data = const_cast<char*>(&DB_HEAD_BLOCKS);
        rc = mdb_del(txn, m_dbi, &key, nullptr);
        assert(rc == MDB_SUCCESS);
    }
    
    {
        DataStream ssKey, ssValue;
        ssKey << DB_BEST_BLOCK;
        ssValue << hashBlock;
        
        MDB_val key, value;
        key.mv_size = ssKey.size();
        key.mv_data = const_cast<char*>(ssKey.data());
        value.mv_size = ssValue.size();
        value.mv_data = const_cast<char*>(ssValue.data());
        
        rc = mdb_put(txn, m_dbi, &key, &value, 0);
        assert(rc == MDB_SUCCESS);
    }

    rc = mdb_txn_commit(txn);
    return rc == MDB_SUCCESS;
}

std::unique_ptr<CCoinsViewCursor> CCoinsViewDB_LMDB::Cursor() const {
    return nullptr;  // TODO: Implement cursor for LMDB
}

size_t CCoinsViewDB_LMDB::EstimateSize() const {
    // TODO: Implement proper size estimation
    return 0;
}

std::unique_ptr<CCoinsView> CreateLMDBView(const std::string& db_path, size_t cache_size) {
    DBParams db_params;
    db_params.path = fs::u8path(db_path.c_str());
    db_params.cache_bytes = cache_size;
    db_params.memory_only = false;
    db_params.wipe_data = true;
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
