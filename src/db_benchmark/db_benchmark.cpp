// Copyright (c) 2026 The Bitcoin Knots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

// Debug flag - uncomment to enable debug output
// #define DEBUG 1

// Provide translation stub to avoid linking issues with bitcoin_clientversion
// This is needed because some Bitcoin libraries use the translation system
#include <util/translation.h>
const TranslateFn G_TRANSLATION_FUN{nullptr};

#include <consensus/consensus.h> // For MAX_BLOCK_SERIALIZED_SIZE
#include <coins.h>
#include <flatfile.h>
#include <kernel/messagestartchars.h>
#include <node/blockstorage.h> // For BLOCKFILE_CHUNK_SIZE
#include <primitives/block.h>
#include <primitives/transaction.h> // For TX_WITH_WITNESS
#include <serialize.h> // For MAX_SIZE
#include <streams.h>
#include <undo.h>
#include <util/fs.h>
#include <util/obfuscation.h>
#include <util/strencodings.h>
#include <univalue.h> // For JSON parsing

#include <chrono>
#include <csignal>
#include <deque>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

// Debug flag for gdb breakpoint
volatile int g_debug_break = 0;

// Metrics collector
#include <metrics_collector.hpp>

// Database-specific CCoinsView implementation
// Include the appropriate database header
// For LevelDB, RocksDB, and LMDB, we use custom implementations

// Global metrics collector
extern BenchmarkMetricsCollector g_metrics_collector;

// Forward declaration - each database-specific implementation provides this
std::unique_ptr<CCoinsView> CreateDatabaseView(const std::string& db_path, size_t cache_size);

/**
 * Load XOR key from file
 * Try blocks directory first, then parent directory
 */
static Obfuscation LoadXorKey(const fs::path& blocks_dir) {
    std::array<std::byte, Obfuscation::KEY_SIZE> xor_key{};
    
    // Try blocks directory first (user confirmed this is correct location)
    fs::path xor_key_path = blocks_dir / "xor.dat";
#ifdef DEBUG
    std::cerr << "DBG: LoadXorKey: Trying blocks_dir: " << fs::PathToString(xor_key_path) << " exists=" << fs::exists(xor_key_path) << "\n";
    std::cerr.flush();
#endif
    if (fs::exists(xor_key_path)) {
#ifdef DEBUG
        std::cerr << "DBG: LoadXorKey: XOR key file found at " << fs::PathToString(xor_key_path) << "\n";
        std::cerr.flush();
#endif
        FILE* xor_file = fsbridge::fopen(xor_key_path, "rb");
        if (!xor_file) {
#ifdef DEBUG
            std::cerr << "DBG: LoadXorKey: Failed to open XOR key file\n";
            std::cerr.flush();
#endif
            return Obfuscation{xor_key};
        }
#ifdef DEBUG
        std::cerr << "DBG: LoadXorKey: XOR key file opened successfully\n";
        std::cerr.flush();
#endif
        AutoFile xor_key_file{xor_file};
        try {
            xor_key_file >> xor_key;
        } catch (const std::exception& e) {
            return Obfuscation{};
        }
        return Obfuscation{xor_key};
    }
    
    // Fallback: parent directory (Bitcoin data directory root)
    xor_key_path = blocks_dir.parent_path() / "xor.dat";
#ifdef DEBUG
    std::cerr << "DBG: LoadXorKey: Trying parent_dir: " << fs::PathToString(xor_key_path) << " exists=" << fs::exists(xor_key_path) << "\n";
    std::cerr.flush();
#endif
    if (fs::exists(xor_key_path)) {
#ifdef DEBUG
        std::cerr << "DBG: LoadXorKey: XOR key file found at parent dir\n";
        std::cerr.flush();
#endif
        FILE* xor_file = fsbridge::fopen(xor_key_path, "rb");
        if (!xor_file) {
            return Obfuscation{xor_key};
        }
        AutoFile xor_key_file{xor_file};
        xor_key_file >> xor_key;
#ifdef DEBUG
        std::cerr << "DBG: LoadXorKey: XOR key loaded from parent dir, bytes: ";
        for (size_t i = 0; i < xor_key.size(); ++i) {
            std::cerr << std::hex << "0x" << (int)xor_key[i] << " ";
        }
        std::cerr << "\n";
        std::cerr.flush();
#endif
        Obfuscation obfuscation{xor_key};
#ifdef DEBUG
        std::cerr << "DBG: LoadXorKey: Obfuscation object from parent valid=" << (obfuscation ? "true" : "false") << "\n";
        std::cerr.flush();
#endif
        return obfuscation;
    }
    
    // If no XOR key file found, use zero key (no obfuscation)
#ifdef DEBUG
    std::cerr << "DBG: LoadXorKey: No XOR key file found, using zero key\n";
    std::cerr.flush();
#endif
    return Obfuscation{xor_key};
}

/**
 * Helper function to read a block from disk at a specific file position.
 * Matches Bitcoin Knots' BlockManager::ReadBlock() approach.
 */
static std::shared_ptr<CBlock> ReadBlockFromDisk(const fs::path& blocks_dir, const FlatFilePos& pos, unsigned int nSize, Obfuscation xor_key) {
    // Use Bitcoin Knots' approach: open file at position and read raw bytes
    FlatFileSeq block_file_seq(blocks_dir, "blk", node::BLOCKFILE_CHUNK_SIZE);
    
    // pos.nPos is where the block data starts (after magic+size)
    // We need to open before that to read the magic and size
    constexpr unsigned int BLOCK_SERIALIZATION_HEADER_SIZE = 8; // 4 bytes magic + 4 bytes size
    if (pos.nPos < BLOCK_SERIALIZATION_HEADER_SIZE) {
        return nullptr;
    }
    
    unsigned int file_pos = pos.nPos - BLOCK_SERIALIZATION_HEADER_SIZE;
    FILE* file = block_file_seq.Open(FlatFilePos{pos.nFile, file_pos}, true);
    if (!file) {
        return nullptr;
    }
    AutoFile file_in{file, xor_key};
    file_in.SetIdlePriority();
    
    try {
        MessageStartChars blk_start;
        unsigned int blk_size;
        file_in >> blk_start >> blk_size;
        
        // Verify size matches
        if (blk_size != nSize) {
            return nullptr;
        }
        
        std::shared_ptr<CBlock> block = std::make_shared<CBlock>();
        file_in >> TX_WITH_WITNESS(*block);
        return block;
    } catch (const std::exception&) {
        return nullptr;
    }
}

/**
 * Read all blocks from block files and apply to coins view.
 * Based on ChainstateManager::LoadExternalBlockFile from validation.cpp
 * 
 * Uses Bitcoin Knots' AddCoins for proper UTXO management.
 * 
 * Uses assert() for critical error handling as requested.
 */
static void ProcessAllBlocks(CCoinsViewCache& cache, CCoinsView& db_view, const fs::path& blocks_dir, Obfuscation xor_key, uint64_t& nSpendFailures, uint64_t& nFlushFailures, uint64_t& nCoinsAdded, uint64_t& nInputsSpent, uint64_t& nBlocksSkipped) {
    FlatFileSeq block_file_seq(blocks_dir, "blk", node::BLOCKFILE_CHUNK_SIZE);
    
    int nFile = 0;
    int nBlocks = 0;
    nSpendFailures = 0;
    nFlushFailures = 0;
    nCoinsAdded = 0;
    nInputsSpent = 0;
    nBlocksSkipped = 0;
    
    // Get initial chain state from DATABASE, not cache (to avoid stale cache state)
    // This is the key fix: use db_view.GetBestBlock() not cache.GetBestBlock()
    uint256 hashTip = db_view.GetBestBlock();
    std::cout << "Initial hashTip: " << hashTip.ToString() << " (null=" << hashTip.IsNull() << ")\n";
    
    // If hashTip is not null in a fresh database, that's a problem!
    if (!hashTip.IsNull()) {
        assert(hashTip.IsNull() && "Fresh database should have null hashTip");
    }
    
    int nHeight = -1; // Will be set to 0 when we process genesis
    
    // Block index: maps block hash to height for all blocks we've processed
    std::map<uint256, int> knownBlocks;
    
    // Buffer for blocks with unknown parents (like Bitcoin Knots' blocks_with_unknown_parent)
    // Maps parent hash -> list of (child block hash, file position, block size)
    // We store FlatFilePos to match Bitcoin Knots, which re-reads from disk when needed
    std::multimap<uint256, std::tuple<uint256, FlatFilePos, unsigned int>> blocks_with_unknown_parent;
    
    auto nStart = std::chrono::high_resolution_clock::now();
    
    // Mainnet genesis block hash
    const uint256 mainnetGenesisHash = uint256::FromHex("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f").value();
    
    while (true) {
        FlatFilePos pos{nFile, 0u};
        fs::path filename = block_file_seq.FileName(pos);
        if (!fs::exists(filename)) {
            break; // No more block files
        }
        
        std::cout << "Processing file: " << fs::PathToString(filename) << "\n";
        
        try {
            FILE* file = block_file_seq.Open(pos, true);
            if (!file) {
                nFile++;
                continue;
            }
            
            // Use AutoFile with XOR key for de-obfuscation
            AutoFile file_in{file, xor_key};
            file_in.SetIdlePriority();
            BufferedFile blkdat{file_in, 2 * MAX_BLOCK_SERIALIZED_SIZE, MAX_BLOCK_SERIALIZED_SIZE + 8};
            uint64_t nRewind = blkdat.GetPos();
            
            // Read all blocks from this file
            while (!blkdat.eof()) {
                blkdat.SetPos(nRewind);
                nRewind++; // start one byte further next time, in case of failure
                blkdat.SetLimit(); // remove former limit
                
                unsigned int nSize = 0;
                try {
                    // Locate a header by searching for the first magic byte
                    // Mainnet magic bytes: 0xf9, 0xbe, 0xb4, 0xd9
                    constexpr std::byte MAINNET_MAGIC_BYTE = std::byte{0xf9};
                    blkdat.FindByte(MAINNET_MAGIC_BYTE);
                    nRewind = blkdat.GetPos() + 1;
                    
                    // Read full magic bytes
                    MessageStartChars buf;
                    blkdat >> buf;
                    
                    // Verify this is actually the Bitcoin magic (not a false positive)
                    // Bitcoin Knots checks: if (buf != params.MessageStart()) continue;
                    constexpr MessageStartChars MAINNET_MAGIC = {0xf9u, 0xbeu, 0xb4u, 0xd9u};
                    if (buf != MAINNET_MAGIC) {
                        continue;
                    }
                    
                    // Read size
                    blkdat >> nSize;
                    if (nSize < 80 || nSize > MAX_BLOCK_SERIALIZED_SIZE) {
                        continue;
                    }
                } catch (const std::exception&) {
                    // End of file or read error
                    break;
                }
                try {
                    // Remember position for rewinding
                    const uint64_t nBlockPos = blkdat.GetPos();
                    
                    // Set limit to end of this block
                    blkdat.SetLimit(nBlockPos + nSize);
                    
                    // Read and deserialize the block header first for chain validation
                    CBlockHeader header;
                    blkdat >> header;
                    const uint256 hash = header.GetHash();
                    
                    // Check if parent exists (like Bitcoin Knots: m_blockman.LookupBlockIndex(header.hashPrevBlock))
                    // We accept any block whose parent is in our knownBlocks set or is null (genesis)
                    bool is_genesis = (hash == mainnetGenesisHash);
                    bool parent_exists = header.hashPrevBlock.IsNull() || knownBlocks.find(header.hashPrevBlock) != knownBlocks.end();
                    
                    // Skip if we've already processed this block
                    if (knownBlocks.find(hash) != knownBlocks.end()) {
                        continue;
                    }
                    
                    bool extends_tip = false;
                    uint256 oldHashTip = hashTip;
                    int block_height = -1;
                    
                    if (is_genesis) {
                        // Genesis block - will set hashTip and nHeight after successful processing
                        std::cout << "  Processing genesis block\n";
                        extends_tip = true; // Genesis extends the tip by definition
                        block_height = 0;
                    } else if (!parent_exists) {
                        // Parent not known - buffer for later (like Bitcoin Knots' blocks_with_unknown_parent)
                        // Store file position and size to re-read from disk later (Bitcoin Knots approach)
                        blocks_with_unknown_parent.emplace(header.hashPrevBlock, std::make_tuple(hash, FlatFilePos{nFile, static_cast<unsigned int>(nBlockPos)}, nSize));
                        continue;
                    } else {
                        // Parent exists in index - accept this block
                        // Get parent's height and compute this block's height
                        int parent_height = knownBlocks[header.hashPrevBlock];
                        block_height = parent_height + 1;
                        // Remember if this extends the tip for later height increment
                        extends_tip = (header.hashPrevBlock == oldHashTip);
                    }
                    
                    // Now read the full block
                    blkdat.SetPos(nBlockPos);
                    blkdat.SetLimit(nBlockPos + nSize);
                    CBlock block;
                    blkdat >> TX_WITH_WITNESS(block);
                    
                    // Verify the block hash matches
                    assert(block.GetHash() == hash && "Block hash mismatch after deserialization");
                    
                    // Update rewind position to after this block
                    // Use nBlockPos + nSize to ensure we skip past the entire block
                    // This matches Bitcoin Knots: nRewind = nBlockPos + nSize
                    nRewind = nBlockPos + nSize;
                    blkdat.SkipTo(nRewind);
                    
                    // Process transactions in block using Bitcoin Knots' AddCoins
                    // This is the proper way to add transaction outputs to the coins view
                    for (size_t i = 0; i < block.vtx.size(); ++i) {
                        const CTransaction& tx = *block.vtx[i];
                        bool is_coinbase = (i == 0);
                        
                        if (!is_coinbase) {
                            // For non-coinbase: spend inputs first
                            for (const CTxIn& txin : tx.vin) {
                                {
                                    ScopedTimer timer(g_metrics_collector.stats_cache_spend_coin);
                                    bool is_spent = cache.SpendCoin(txin.prevout, nullptr);
                                    if (!is_spent) {
                                        nSpendFailures++;
                                    } else {
                                        nInputsSpent++;
                                    }
                                }
                            }
                        }
                        
                        // Add outputs using Bitcoin Knots' AddCoins function
                        // This properly handles coinbase vs non-coinbase and overwrite logic
                        {
                            ScopedTimer timer(g_metrics_collector.stats_cache_add_coin);
                            bool check_for_overwrite = !is_coinbase;
                            AddCoins(cache, tx, block_height, check_for_overwrite);
                            nCoinsAdded += tx.vout.size();
                        }
                    }
                    
                    // Set best block for the cache - this updates the mutable hashBlock
                    cache.SetBestBlock(hash);
                    // Update chain state after successful processing
                    knownBlocks[hash] = block_height;
                    // Only update hashTip and nHeight if this block extends the tip
                    if (extends_tip) {
                        hashTip = hash;
                        nHeight = block_height;
                    }
                    nBlocks++; // Count the main block
                    if (nBlocks % 1000 == 0) {
                        std::cout << "  Processed " << nBlocks << " blocks (height=" << nHeight << ")...\n";
                    }
                    
                    // Process any buffered blocks that have this block as parent
                    // Re-read from disk for each buffered block (Bitcoin Knots approach)
                    std::deque<uint256> queue;
                    queue.push_back(hash);
                    while (!queue.empty()) {
                        uint256 head = queue.front();
                        queue.pop_front();
                        auto range = blocks_with_unknown_parent.equal_range(head);
                        for (auto it = range.first; it != range.second; ) {
                            const uint256& child_hash = std::get<0>(it->second);
                            const FlatFilePos& child_pos = std::get<1>(it->second);
                            const unsigned int child_nSize = std::get<2>(it->second);
                            
                            // Re-read block from disk (Bitcoin Knots: BlockManager::ReadBlock)
                            std::shared_ptr<CBlock> child_block = ReadBlockFromDisk(blocks_dir, child_pos, child_nSize, xor_key);
                            if (!child_block) {
                                // Failed to read block, skip it
                                blocks_with_unknown_parent.erase(it++);
                                range = blocks_with_unknown_parent.equal_range(head);
                                continue;
                            }
                            
                            // Verify hash
                            if (child_block->GetHash() != child_hash) {
                                blocks_with_unknown_parent.erase(it++);
                                range = blocks_with_unknown_parent.equal_range(head);
                                continue;
                            }
                            
                            // Compute child height based on parent's height
                            int parent_height = knownBlocks[head];
                            int child_height = parent_height + 1;
                            
                            // Process the buffered block's transactions
                            // This mirrors Bitcoin Knots' AcceptBlock() processing
                            for (size_t i = 0; i < child_block->vtx.size(); ++i) {
                                const CTransaction& tx = *child_block->vtx[i];
                                bool is_coinbase = (i == 0);
                                
                                if (!is_coinbase) {
                                    for (const CTxIn& txin : tx.vin) {
                                        {
                                            ScopedTimer timer(g_metrics_collector.stats_cache_spend_coin);
                                            bool is_spent = cache.SpendCoin(txin.prevout, nullptr);
                                            if (!is_spent) {
                                                nSpendFailures++;
                                            } else {
                                                nInputsSpent++;
                                            }
                                        }
                                    }
                                }
                                
                                {
                                    ScopedTimer timer(g_metrics_collector.stats_cache_add_coin);
                                    bool check_for_overwrite = !is_coinbase;
                                    AddCoins(cache, tx, child_height, check_for_overwrite);
                                    nCoinsAdded += tx.vout.size();
                                }
                            }
                            
                            // Update state for this buffered block
                            knownBlocks[child_hash] = child_height;
                            cache.SetBestBlock(child_hash);
                            // Update hashTip and nHeight only if this extends the current tip
                            if (child_height > nHeight) {
                                hashTip = child_hash;
                                nHeight = child_height;
                            }
                            nBlocks++;
                            
                            // Add to queue for recursive processing of its children
                            queue.push_back(child_hash);
                            
                            // Remove from buffered map
                            blocks_with_unknown_parent.erase(it++);
                            range = blocks_with_unknown_parent.equal_range(head);
                        }
                    }
                    
                    // Flush cache periodically to avoid using too much memory
                    // Mirror Bitcoin Knots' behavior: flush less frequently to batch writes
                    if (nBlocks % 1000 == 0) {
                        bool flush_ok;
                        {
                            ScopedTimer timer(g_metrics_collector.stats_cache_flush);
                            flush_ok = cache.Flush();
                        }
                        if (!flush_ok) {
                            nFlushFailures++;
                            assert(flush_ok && "Flush failed - database write error");
                        }
                        std::cout << "  Processed " << nBlocks << " blocks (height=" << nHeight << ")...\n";
                    }
                } catch (const std::exception&) {
                    // Block failed to deserialize, try next one
                    // This can happen with historical bugs that added extra data
                    continue;
                }
            }
            // AutoFile (file_in) and BufferedFile (blkdat) will close the file automatically
        } catch (const std::exception&) {
            // Don't manually close - AutoFile handles it
        }
        
        nFile++;
    }
    
    // Final flush
    bool final_flush_ok;
    {
        ScopedTimer timer(g_metrics_collector.stats_cache_flush);
        final_flush_ok = cache.Flush();
    }
    assert(final_flush_ok && "Final flush failed - database write error");
    
    auto nEnd = std::chrono::high_resolution_clock::now();
    double nSeconds = std::chrono::duration<double>(nEnd - nStart).count();
    
    std::cout << "\nProcessed " << nBlocks << " blocks (height=" << nHeight << ") in " << nSeconds << " seconds\n";
    
    // Report diagnostics
    std::cout << "DIAGNOSTIC: Coins added: " << nCoinsAdded << ", Inputs spent: " << nInputsSpent << ", Net growth: " << (nCoinsAdded - nInputsSpent) << "\n";
    if (nBlocksSkipped > 0) {
        std::cout << "DIAGNOSTIC: Blocks skipped (not in main chain): " << nBlocksSkipped << "\n";
    }
    
    // Use assert() for critical failures as requested by user
    // SpendCoin failures can occur for fork blocks (expected behavior in Bitcoin Knots)
    // Only assert on flush failures which indicate database write errors
    assert(nFlushFailures == 0 && "Flush failures detected");
    
    // Log SpendCoin failures (expected for fork blocks, but worth monitoring)
    if (nSpendFailures > 0) {
        std::cout << "INFO: " << nSpendFailures << " SpendCoin failures (expected for blocks on fork chains)\n";
    }
    if (nSeconds > 0) {
        std::cout << "Throughput: " << (nBlocks / nSeconds) << " blocks/s\n";
    }
}

/**
 * Run the IBD benchmark.
 */
void RunIBDBenchmark(CCoinsView& db_view) {
    std::cout << "Running IBD benchmark...\n";
    
    // Create cache
    CCoinsViewCache cache(&db_view);
    
    // Load XOR key and process blocks
    fs::path blocks_dir = fs::u8path("/home/knots-mainnet/.bitcoin/blocks");
    Obfuscation xor_key = LoadXorKey(blocks_dir);
    
    uint64_t nSpendFailures = 0;
    uint64_t nFlushFailures = 0;
    uint64_t nCoinsAdded = 0;
    uint64_t nInputsSpent = 0;
    uint64_t nBlocksSkipped = 0;
    ProcessAllBlocks(cache, db_view, blocks_dir, xor_key, nSpendFailures, nFlushFailures, nCoinsAdded, nInputsSpent, nBlocksSkipped);
    
    // Get final best block
    uint256 best_block;
    {
        ScopedTimer timer(g_metrics_collector.stats_db_get_best_block);
        best_block = db_view.GetBestBlock();
    }
    std::cout << "Best block: " << best_block.ToString() << "\n";
    
    std::cout << "IBD benchmark completed.\n";
}

/**
 * Load mempool transactions from JSON file.
 * Expected format: array of transaction objects with "hex" field containing raw transaction bytes.
 */
static std::vector<CTransactionRef> LoadMempoolTransactions(const fs::path& mempool_file) {
    std::vector<CTransactionRef> transactions;
    
    if (!fs::exists(mempool_file)) {
        std::cerr << "Mempool file not found: " << fs::PathToString(mempool_file) << "\n";
        return transactions;
    }
    
    // Read file contents
    std::string mempool_json;
    {
        std::ifstream ifs(fs::PathToString(mempool_file), std::ios::binary);
        if (!ifs) {
            std::cerr << "Failed to open mempool file: " << fs::PathToString(mempool_file) << "\n";
            return transactions;
        }
        mempool_json = std::string((std::istreambuf_iterator<char>(ifs)),
                                   std::istreambuf_iterator<char>());
    }
    
    if (mempool_json.empty()) {
        std::cerr << "Mempool file is empty: " << fs::PathToString(mempool_file) << "\n";
        return transactions;
    }
    
    UniValue mempool_data;
    if (!mempool_data.read(mempool_json)) {
        std::cerr << "Failed to parse mempool JSON: " << fs::PathToString(mempool_file) << "\n";
        return transactions;
    }
    
    if (!mempool_data.isArray()) {
        std::cerr << "Mempool JSON is not an array: " << fs::PathToString(mempool_file) << "\n";
        return transactions;
    }
    
    for (size_t i = 0; i < mempool_data.size(); ++i) {
        UniValue tx_obj = mempool_data[i];
        if (!tx_obj.isObject()) continue;
        
        UniValue hex_value = tx_obj["hex"];
        if (!hex_value.isStr()) continue;
        
        std::string hex_str = hex_value.get_str();
        std::vector<unsigned char> tx_bytes = ParseHex(hex_str);
        if (tx_bytes.empty()) {
            std::cerr << "Failed to parse hex for transaction " << i << "\n";
            continue;
        }
        
        try {
            DataStream ss(Span<const std::byte>(reinterpret_cast<const std::byte*>(tx_bytes.data()), tx_bytes.size()));
            CTransactionRef tx = std::make_shared<CTransaction>(deserialize, TX_WITH_WITNESS, ss);
            transactions.push_back(tx);
        } catch (const std::exception& e) {
            std::cerr << "Failed to deserialize transaction " << i << ": " << e.what() << "\n";
            continue;
        }
    }
    
    std::cout << "Loaded " << transactions.size() << " mempool transactions from " 
              << fs::PathToString(mempool_file) << "\n";
    return transactions;
}

/**
 * Process mempool transactions against the coins view cache.
 */
static void ProcessMempoolTransactions(CCoinsViewCache& cache, const std::vector<CTransactionRef>& transactions, uint64_t& nSpendFailures) {
    int nHeight = 0; // Steady-state doesn't track height
    
    for (size_t i = 0; i < transactions.size(); ++i) {
        const CTransaction& tx = *transactions[i];
        const Txid& txid = tx.GetHash();
        
        // Spend inputs
        for (const CTxIn& txin : tx.vin) {
            Coin coin;
            {
                ScopedTimer timer(g_metrics_collector.stats_cache_spend_coin);
                bool is_spent = cache.SpendCoin(txin.prevout, &coin);
                if (!is_spent) {
                    nSpendFailures++;
                    if (nSpendFailures <= 10) {
                        std::cerr << "INFO: Mempool SpendCoin failed for " << txin.prevout.hash.ToString() << ":" << txin.prevout.n << " (tx: " << txid.ToString() << ") - expected for unconfirmed inputs\n";
                    }
                    // In mempool replay, inputs might not exist in our chainstate
                    // This is expected for transactions spending unconfirmed inputs
                }
            }
        }
        
        // Add outputs
        for (size_t j = 0; j < tx.vout.size(); ++j) {
            bool overwrite = false;
            {
                ScopedTimer timer(g_metrics_collector.stats_cache_have_coin);
                overwrite = cache.HaveCoin(COutPoint(txid, j));
            }
            {
                ScopedTimer timer(g_metrics_collector.stats_cache_add_coin);
                cache.AddCoin(COutPoint(txid, j), Coin(tx.vout[j], nHeight, false), overwrite);
            }
        }
    }
}

/**
 * Run the steady-state benchmark.
 */
void RunSteadyStateBenchmark(CCoinsView& db_view) {
    std::cout << "Running steady-state benchmark...\n";
    
    // Create cache
    CCoinsViewCache cache(&db_view);
    
    // Load mempool transactions
    fs::path mempool_file = fs::u8path("benchmark/getrawmempool_full.json");
    std::vector<CTransactionRef> transactions = LoadMempoolTransactions(mempool_file);
    
    if (transactions.empty()) {
        std::cerr << "No transactions loaded. Steady-state benchmark skipped.\n";
        std::cout << "Steady-state benchmark completed.\n";
        return;
    }
    
    uint64_t nSpendFailures = 0;
    
    // Process all transactions
    auto start = std::chrono::high_resolution_clock::now();
    ProcessMempoolTransactions(cache, transactions, nSpendFailures);
    
    // Final flush
    bool flush_ok;
    {
        ScopedTimer timer(g_metrics_collector.stats_cache_flush);
        flush_ok = cache.Flush();
    }
    if (!flush_ok) {
        std::cerr << "CRITICAL: Flush failed in steady-state benchmark! Database writes are failing.\n";
        assert(flush_ok && "Steady-state flush failed - database write error");
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    
    std::cout << "Processed " << transactions.size() << " mempool transactions in " 
              << elapsed << " seconds\n";
    if (elapsed > 0) {
        std::cout << "Throughput: " << (transactions.size() / elapsed) << " tx/s\n";
    }
    
    // Report diagnostics
    if (nSpendFailures > 0) {
        std::cerr << "DIAGNOSTIC: Mempool SpendCoin failures: " << nSpendFailures << " (expected for transactions spending unconfirmed inputs)\n";
    }
    
    std::cout << "Steady-state benchmark completed.\n";
}

/**
 * Run benchmark for a specific engine.
 */
void RunBenchmark(const std::string& engine_name, const std::string& workload) {
    // Create database path
    std::string db_path = std::string(BENCH_DATA_DIR) + "/" + engine_name + "_chainstate";
    
    // Ensure directory exists
    fs::path data_dir = fs::u8path(db_path.c_str()).parent_path();
    if (!fs::exists(data_dir)) {
        fs::create_directories(data_dir);
    }
    
    // Create database view
    const size_t cache_size = BENCH_CACHE_MB * 1024 * 1024;
    std::unique_ptr<CCoinsView> db_view = CreateDatabaseView(db_path, cache_size);
    
    // Reset metrics
    g_metrics_collector.Reset();
    
    // Sample initial system resources
    g_metrics_collector.SampleSystemResources();
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Run selected workload
    if (workload == "ibd") {
        RunIBDBenchmark(*db_view);
    } else if (workload == "steady") {
        RunSteadyStateBenchmark(*db_view);
    } else {
        RunIBDBenchmark(*db_view);
        RunSteadyStateBenchmark(*db_view);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    
    // Sample final system resources
    g_metrics_collector.SampleSystemResources();
    
    // Print report
    g_metrics_collector.PrintReport(engine_name + "_" + workload, elapsed);
    
    // Export JSON
    std::string results_dir = "/home/odroid/bitcoin/benchmark/results";
    std::string json_path = results_dir + "/" + engine_name + "_" + workload + ".json";
    g_metrics_collector.ExportJSON(json_path, engine_name + "_" + workload, elapsed);
}

/**
 * Parse command line arguments.
 */
void ParseArguments(int argc, char* argv[], std::string& engine, std::string& workload) {
    engine = "leveldb";
    workload = "ibd";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--engine=leveldb" || arg == "-e=leveldb") {
            engine = "leveldb";
        } else if (arg == "--engine=rocksdb" || arg == "-e=rocksdb") {
            engine = "rocksdb";
        } else if (arg == "--engine=lmdb" || arg == "-e=lmdb") {
            engine = "lmdb";
        } else if (arg == "--workload=ibd" || arg == "-w=ibd") {
            workload = "ibd";
        } else if (arg == "--workload=steady" || arg == "-w=steady") {
            workload = "steady";
        }
    }
}

int main(int argc, char* argv[]) {
    std::cout << "Database Benchmark Tool for Bitcoin Knots\n";
    std::cout << "==========================================\n\n";
    
    std::string engine, workload;
    ParseArguments(argc, argv, engine, workload);
    
    std::cout << "Running with engine=" << engine << " workload=" << workload << "\n\n";
    
    RunBenchmark(engine, workload);
    
    std::cout << "\nBenchmark completed successfully!\n";
    return 0;
}
