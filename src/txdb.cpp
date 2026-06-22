// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <txdb.h>

#include <coins.h>
#include <dbwrapper.h>
#include <logging.h>
#include <primitives/transaction.h>
#include <random.h>
#include <serialize.h>
#include <uint256.h>
#include <util/vector.h>

#include <cassert>
#include <cstdlib>
#include <iterator>
#include <utility>

#ifdef ENABLE_INMEMORYCS
#include <util/fs_helpers.h>
#include <leveldb/db.h>
#include <leveldb/iterator.h>
#endif

static constexpr uint8_t DB_COIN{'C'};
static constexpr uint8_t DB_BEST_BLOCK{'B'};
static constexpr uint8_t DB_HEAD_BLOCKS{'H'};
// Keys used in previous version that might still be found in the DB:
static constexpr uint8_t DB_COINS{'c'};

bool CCoinsViewDB::NeedsUpgrade()
{
    std::unique_ptr<CDBIterator> cursor{m_db->NewIterator()};
    // DB_COINS was deprecated in v0.15.0, commit
    // 1088b02f0ccd7358d2b7076bb9e122d59d502d02
    cursor->Seek(std::make_pair(DB_COINS, uint256{}));
    return cursor->Valid();
}

namespace {

struct CoinEntry {
    COutPoint* outpoint;
    uint8_t key;
    explicit CoinEntry(const COutPoint* ptr) : outpoint(const_cast<COutPoint*>(ptr)), key(DB_COIN)  {}

    SERIALIZE_METHODS(CoinEntry, obj) { READWRITE(obj.key, obj.outpoint->hash, VARINT(obj.outpoint->n)); }
};

} // namespace

CCoinsViewDB::CCoinsViewDB(DBParams db_params, CoinsViewOptions options) :
    m_db_params{std::move(db_params)},
    m_options{std::move(options)},
    m_db{std::make_unique<CDBWrapper>(m_db_params)} { }

void CCoinsViewDB::ResizeCache(size_t new_cache_size)
{
    // We can't do this operation with an in-memory DB since we'll lose all the coins upon
    // reset.
    if (!m_db_params.memory_only) {
        // Have to do a reset first to get the original `m_db` state to release its
        // filesystem lock.
        m_db.reset();
        m_db_params.cache_bytes = new_cache_size;
        m_db_params.wipe_data = false;
        m_db = std::make_unique<CDBWrapper>(m_db_params);
    }
}

std::optional<Coin> CCoinsViewDB::GetCoin(const COutPoint& outpoint) const
{
    if (Coin coin; m_db->Read(CoinEntry(&outpoint), coin)) return coin;
    return std::nullopt;
}

bool CCoinsViewDB::HaveCoin(const COutPoint &outpoint) const {
    return m_db->Exists(CoinEntry(&outpoint));
}

uint256 CCoinsViewDB::GetBestBlock() const {
    uint256 hashBestChain;
    if (!m_db->Read(DB_BEST_BLOCK, hashBestChain))
        return uint256();
    return hashBestChain;
}

std::vector<uint256> CCoinsViewDB::GetHeadBlocks() const {
    std::vector<uint256> vhashHeadBlocks;
    if (!m_db->Read(DB_HEAD_BLOCKS, vhashHeadBlocks)) {
        return std::vector<uint256>();
    }
    return vhashHeadBlocks;
}

bool CCoinsViewDB::BatchWrite(CoinsViewCacheCursor& cursor, const uint256 &hashBlock) {
    CDBBatch batch(*m_db);
    size_t count = 0;
    size_t changed = 0;
    assert(!hashBlock.IsNull());

    uint256 old_tip = GetBestBlock();
    if (old_tip.IsNull()) {
        // We may be in the middle of replaying.
        std::vector<uint256> old_heads = GetHeadBlocks();
        if (old_heads.size() == 2) {
            if (old_heads[0] != hashBlock) {
                LogPrintLevel(BCLog::COINDB, BCLog::Level::Error, "The coins database detected an inconsistent state, likely due to a previous crash or shutdown. You will need to restart bitcoind with the -reindex-chainstate or -reindex configuration option.\n");
            }
            assert(old_heads[0] == hashBlock);
            old_tip = old_heads[1];
        }
    }

    // In the first batch, mark the database as being in the middle of a
    // transition from old_tip to hashBlock.
    // A vector is used for future extensibility, as we may want to support
    // interrupting after partial writes from multiple independent reorgs.
    batch.Erase(DB_BEST_BLOCK);
    batch.Write(DB_HEAD_BLOCKS, Vector(hashBlock, old_tip));

    for (auto it{cursor.Begin()}; it != cursor.End();) {
        if (it->second.IsDirty()) {
            CoinEntry entry(&it->first);
            if (it->second.coin.IsSpent())
                batch.Erase(entry);
            else
                batch.Write(entry, it->second.coin);
            changed++;
        }
        count++;
        it = cursor.NextAndMaybeErase(*it);
        if (batch.SizeEstimate() > m_options.batch_write_bytes) {
            LogDebug(BCLog::COINDB, "Writing partial batch of %.2f MiB\n", batch.SizeEstimate() * (1.0 / 1048576.0));
            m_db->WriteBatch(batch);
            batch.Clear();
            if (m_options.simulate_crash_ratio) {
                static FastRandomContext rng;
                if (rng.randrange(m_options.simulate_crash_ratio) == 0) {
                    LogError("Simulating a crash. Goodbye.");
                    _Exit(0);
                }
            }
        }
    }

    // In the last batch, mark the database as consistent with hashBlock again.
    batch.Erase(DB_HEAD_BLOCKS);
    batch.Write(DB_BEST_BLOCK, hashBlock);

    LogDebug(BCLog::COINDB, "Writing final batch of %.2f MiB\n", batch.SizeEstimate() * (1.0 / 1048576.0));
    bool ret = m_db->WriteBatch(batch);
    LogDebug(BCLog::COINDB, "Committed %u changed transaction outputs (out of %u) to coin database...\n", (unsigned int)changed, (unsigned int)count);
    return ret;
}

size_t CCoinsViewDB::EstimateSize() const
{
    return m_db->EstimateSize(DB_COIN, uint8_t(DB_COIN + 1));
}

/** Specialization of CCoinsViewCursor to iterate over a CCoinsViewDB */
class CCoinsViewDBCursor: public CCoinsViewCursor
{
public:
    // Prefer using CCoinsViewDB::Cursor() since we want to perform some
    // cache warmup on instantiation.
    CCoinsViewDBCursor(CDBIterator* pcursorIn, const uint256&hashBlockIn):
        CCoinsViewCursor(hashBlockIn), pcursor(pcursorIn) {}
    ~CCoinsViewDBCursor() = default;

    bool GetKey(COutPoint &key) const override;
    bool GetValue(Coin &coin) const override;

    bool Valid() const override;
    void Next() override;

private:
    std::unique_ptr<CDBIterator> pcursor;
    std::pair<char, COutPoint> keyTmp;

    friend class CCoinsViewDB;
};

std::unique_ptr<CCoinsViewCursor> CCoinsViewDB::Cursor() const
{
    auto i = std::make_unique<CCoinsViewDBCursor>(
        const_cast<CDBWrapper&>(*m_db).NewIterator(), GetBestBlock());
    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    i->pcursor->Seek(DB_COIN);
    // Cache key of first record
    if (i->pcursor->Valid()) {
        CoinEntry entry(&i->keyTmp.second);
        i->pcursor->GetKey(entry);
        i->keyTmp.first = entry.key;
    } else {
        i->keyTmp.first = 0; // Make sure Valid() and GetKey() return false
    }
    return i;
}

bool CCoinsViewDBCursor::GetKey(COutPoint &key) const
{
    // Return cached key
    if (keyTmp.first == DB_COIN) {
        key = keyTmp.second;
        return true;
    }
    return false;
}

bool CCoinsViewDBCursor::GetValue(Coin &coin) const
{
    return pcursor->GetValue(coin);
}

bool CCoinsViewDBCursor::Valid() const
{
    return keyTmp.first == DB_COIN;
}

void CCoinsViewDBCursor::Next()
{
    pcursor->Next();
    CoinEntry entry(&keyTmp.second);
    if (!pcursor->Valid() || !pcursor->GetKey(entry)) {
        keyTmp.first = 0; // Invalidate cached key after last record so that Valid() and GetKey() return false
    } else {
        keyTmp.first = entry.key;
    }
}

#ifdef ENABLE_INMEMORYCS
static constexpr size_t BATCH_SIZE = 64 * 1024 * 1024; // 64MB

void LoadChainstateIntoMemory(CCoinsViewDB& coins_db, const fs::path& src_path) {
    // Create temporary disk DB to read from (memory_only=false for disk access)
    DBParams src_params;
    src_params.path = src_path;
    src_params.memory_only = false;  // Must be disk-based to read existing data
    src_params.cache_bytes = coins_db.GetDBParams().cache_bytes;  // Use same cache settings
    src_params.obfuscate = coins_db.GetDBParams().obfuscate;
    CDBWrapper src_db(src_params);

    // Get total size in bytes for progress reporting (only for COIN entries)
    size_t total_bytes = src_db.EstimateSize(DB_COIN, uint8_t(DB_COIN + 1));
    size_t count = 0;
    size_t bytes_processed = 0;
    size_t report_interval = total_bytes / 10;
    if (report_interval == 0) report_interval = 1;

    // Handle empty database case
    if (total_bytes == 0) {
        // But still check if there's metadata (best block, etc.)
        uint256 best_block;
        if (src_db.Read(DB_BEST_BLOCK, best_block)) {
            // Copy metadata even if no coins
            coins_db.GetDB()->Write(DB_BEST_BLOCK, best_block);
        }
        std::vector<uint256> head_blocks;
        if (src_db.Read(DB_HEAD_BLOCKS, head_blocks)) {
            coins_db.GetDB()->Write(DB_HEAD_BLOCKS, head_blocks);
        }
        LogInfo("Chainstate database is empty, nothing to load\n");
        return;
    }

    // First, copy metadata keys (DB_BEST_BLOCK, DB_HEAD_BLOCKS)
    uint256 best_block;
    if (src_db.Read(DB_BEST_BLOCK, best_block)) {
        coins_db.GetDB()->Write(DB_BEST_BLOCK, best_block);
    }
    std::vector<uint256> head_blocks;
    if (src_db.Read(DB_HEAD_BLOCKS, head_blocks)) {
        coins_db.GetDB()->Write(DB_HEAD_BLOCKS, head_blocks);
    }

    // Process coin entries in batches
    CDBBatch batch(*coins_db.GetDB());
    std::unique_ptr<CDBIterator> it(src_db.NewIterator());
    it->Seek(DB_COIN);
    
    size_t last_reported_10 = 0;  // Track last reported 10% boundary (0-10)
    
    for (; it->Valid(); it->Next()) {
        COutPoint outpoint;
        CoinEntry entry(&outpoint);
        Coin coin;
        if (it->GetKey(entry) && it->GetValue(coin)) {
            batch.Write(entry, coin);
            count++;
        }

        if (batch.SizeEstimate() >= BATCH_SIZE) {
            coins_db.GetDB()->WriteBatch(batch);
            bytes_processed += batch.SizeEstimate();
            batch.Clear();
            
            // Report progress at 10%, 20%, ..., 90%, 100%
            unsigned int percent = static_cast<unsigned int>((bytes_processed * 100) / total_bytes);
            unsigned int ten_percent = percent / 10;
            if (ten_percent > last_reported_10 && ten_percent <= 10) {
                last_reported_10 = ten_percent;
                LogInfo("Loading chainstate: %u%%\n", ten_percent * 10);
            }
        }
    }

    // Write remaining batch
    if (batch.SizeEstimate() > 0) {
        bytes_processed += batch.SizeEstimate();
        coins_db.GetDB()->WriteBatch(batch);
    }
    
    // Report 100% if we haven't already
    unsigned int percent = static_cast<unsigned int>((bytes_processed * 100) / total_bytes);
    if (percent >= 100 && last_reported_10 < 10) {
        LogInfo("Loading chainstate: 100%%\n");
    }

    LogInfo("Chainstate loaded into memory: %u entries\n", static_cast<unsigned int>(count));
}

void SaveChainstateToDisk(CCoinsViewDB& coins_db, const fs::path& dest_path) {
    fs::path tmp_path = dest_path;
    tmp_path += ".new";
    fs::remove_all(tmp_path);  // Clean up from previous failed save

    // Variables that need to persist after dest_db scope
    size_t total_bytes = 0;
    size_t count = 0;
    size_t bytes_processed = 0;
    size_t last_reported_10 = 0;
    const leveldb::Snapshot* snapshot = nullptr;

    // Scope block for dest_db - will be closed before rename operations
    {
        // Create temporary disk DB for writing (memory_only=false)
        DBParams dest_params;
        dest_params.path = tmp_path;
        dest_params.memory_only = false;  // Must be disk-based
        dest_params.cache_bytes = coins_db.GetDBParams().cache_bytes;
        dest_params.obfuscate = coins_db.GetDBParams().obfuscate;
        CDBWrapper dest_db(dest_params);

        // Get total size in bytes for progress reporting (while we have the lock)
        total_bytes = coins_db.GetDB()->EstimateSize(DB_COIN, uint8_t(DB_COIN + 1));
        size_t report_interval = total_bytes / 10;
        if (report_interval == 0) report_interval = 1;

        // Use LevelDB snapshot: brief lock to capture, then zero-lock save
        snapshot = coins_db.GetDB()->GetSnapshot();

        // Process in batches - no lock needed after snapshot is taken
        CDBBatch batch(dest_db);
        leveldb::ReadOptions ro;
        ro.snapshot = snapshot;
        std::unique_ptr<CDBIterator> it(coins_db.GetDB()->NewIterator(ro));
        it->Seek(DB_COIN);
        
        for (; it->Valid(); it->Next()) {
            COutPoint outpoint;
            CoinEntry entry(&outpoint);
            Coin coin;
            if (it->GetKey(entry) && it->GetValue(coin)) {
                batch.Write(entry, coin);
                count++;
            }

            if (batch.SizeEstimate() >= BATCH_SIZE) {
                dest_db.WriteBatch(batch);
                bytes_processed += batch.SizeEstimate();
                batch.Clear();
                
                // Report progress at 10%, 20%, ..., 90%, 100% (only if total_bytes > 0 to avoid division by zero)
                if (total_bytes > 0) {
                    unsigned int percent = static_cast<unsigned int>((bytes_processed * 100) / total_bytes);
                    unsigned int ten_percent = percent / 10;
                    if (ten_percent > last_reported_10 && ten_percent <= 10) {
                        last_reported_10 = ten_percent;
                        LogInfo("Saving chainstate: %u%%\n", ten_percent * 10);
                    }
                }
            }
        }

        // Write remaining batch
        if (batch.SizeEstimate() > 0) {
            bytes_processed += batch.SizeEstimate();
            dest_db.WriteBatch(batch);
        }

        // Copy metadata keys (DB_BEST_BLOCK, DB_HEAD_BLOCKS) for all database sizes
        // This ensures the saved chainstate has the correct tip information
        uint256 best_block;
        if (coins_db.GetDB()->Read(DB_BEST_BLOCK, best_block)) {
            dest_db.Write(DB_BEST_BLOCK, best_block);
        }
        std::vector<uint256> head_blocks;
        if (coins_db.GetDB()->Read(DB_HEAD_BLOCKS, head_blocks)) {
            dest_db.Write(DB_HEAD_BLOCKS, head_blocks);
        }
        
        // Force LevelDB to flush all memtables to SST files
        // This ensures the directory contains all expected files (not just one .ldb file)
        CDBBatch final_flush(dest_db);
        dest_db.WriteBatch(final_flush, true); // fSync=true ensures all data is on disk

        // dest_db destructor runs here at end of scope, closing the LevelDB
        // Now tmp_path directory is no longer locked by LevelDB
    } // End of scope - dest_db is now closed

    // Release snapshot after dest_db is closed
    if (snapshot) {
        coins_db.GetDB()->ReleaseSnapshot(snapshot);
    }

    // Atomic directory rename with backup for crash recovery
    fs::path backup_path = dest_path;
    backup_path += ".old";

    // Step 0: If destination doesn't exist but backup does, restore from backup
    // This handles the case where we crashed after creating backup in a previous run
    if (!fs::exists(dest_path) && fs::exists(backup_path)) {
        if (!RenameOver(backup_path, dest_path)) {
            throw std::runtime_error("Save failed: could not restore chainstate from backup");
        }
    }

    // Step 1: Remove any existing backup from previous failed save
    // (If we get here, either dest exists, or both dest and backup don't exist)
    fs::remove_all(backup_path);

    // Step 2: If destination exists, rename it to backup
    if (fs::exists(dest_path)) {
        if (!RenameOver(dest_path, backup_path)) {
            throw std::runtime_error("Save failed: could not create backup");
        }
    }

    // Step 3: Rename new chainstate to destination
    if (!RenameOver(tmp_path, dest_path)) {
        // If rename fails, try to restore from backup if it exists
        if (fs::exists(backup_path)) {
            if (!RenameOver(backup_path, dest_path)) {
                throw std::runtime_error("Save failed: rename failed and could not restore from backup");
            }
        }
        fs::remove_all(tmp_path);
        throw std::runtime_error("Save failed: rename failed (restored from backup if available)");
    }

    // Step 4: Remove backup
    fs::remove_all(backup_path);

    // Report 100% if we haven't already
    if (total_bytes > 0) {
        unsigned int percent = static_cast<unsigned int>((bytes_processed * 100) / total_bytes);
        if (percent >= 100 && last_reported_10 < 10) {
            LogInfo("Saving chainstate: 100%%\n");
        }
    }

    LogInfo("Chainstate saved to disk: %u entries\n", static_cast<unsigned int>(count));
}

#endif // ENABLE_INMEMORYCS
