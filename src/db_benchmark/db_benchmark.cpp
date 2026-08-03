// Copyright (c) 2026 The Bitcoin Knots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

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
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
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
 */
static Obfuscation LoadXorKey(const fs::path& blocks_dir) {
    fs::path xor_key_path = blocks_dir / "xor.dat";
    
    std::cerr << "DBG: Loading XOR key from " << fs::PathToString(xor_key_path) << " exists=" << fs::exists(xor_key_path) << "\n";
    
    std::array<std::byte, Obfuscation::KEY_SIZE> xor_key{};
    
    if (fs::exists(xor_key_path)) {
        AutoFile xor_key_file{fsbridge::fopen(xor_key_path, "rb")};
        xor_key_file >> xor_key;
        std::cerr << "DBG: XOR key loaded successfully\n";
    } else {
        std::cerr << "DBG: No XOR key file found\n";
    }
    
    return Obfuscation{xor_key};
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
    uint64_t nTotalBytes = 0;
    int nBlocks = 0;
    nSpendFailures = 0;
    nFlushFailures = 0;
    nCoinsAdded = 0;
    nInputsSpent = 0;
    nBlocksSkipped = 0;
    
    // Get initial chain state from DATABASE, not cache (to avoid stale cache state)
    // This is the key fix: use db_view.GetBestBlock() not cache.GetBestBlock()
    uint256 hashTip = db_view.GetBestBlock();
    std::cerr << "Initial hashTip: " << hashTip.ToString() << " (null=" << hashTip.IsNull() << ")\n";
    std::cerr << "DBG: Starting with nBlocks=0, hashTip=" << hashTip.ToString() << ", hashTip.IsNull()=" << hashTip.IsNull() << "\n";
    
    // If hashTip is not null in a fresh database, that's a problem!
    if (!hashTip.IsNull()) {
        std::cerr << "\n*** ERROR: Fresh database has non-null hashTip! ***\n";
        std::cerr << "hashTip: " << hashTip.ToString() << "\n";
        g_debug_break = 2; // Different value for different breakpoints
    }
    
    int nHeight = -1; // Will be set to 0 when we process genesis
    
    auto nStart = std::chrono::high_resolution_clock::now();
    
    // Mainnet genesis block hash
    const uint256 mainnetGenesisHash = uint256::FromHex("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f").value();
    
    while (true) {
        FlatFilePos pos{nFile, 0};
        fs::path filename = block_file_seq.FileName(pos);
        if (!fs::exists(filename)) {
            break; // No more block files
        }
        
        std::cout << "Processing file: " << fs::PathToString(filename) << "\n";
        std::cerr << "DBG: Starting file " << fs::PathToString(filename) << " at nBlocks=" << nBlocks << " hashTip=" << hashTip.ToString() << "\n";
        
        try {
            FILE* file = block_file_seq.Open(pos, true);
            if (!file) {
                std::cerr << "Failed to open block file: " << fs::PathToString(filename) << "\n";
                nFile++;
                continue;
            }
            
            // Use AutoFile with XOR key for de-obfuscation
            AutoFile file_in{file, xor_key};
            
            // Use BufferedFile for efficient reading with rewind capability
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
                    
                    std::cerr << "DBG: Block " << hash.ToString() << " prev=" << header.hashPrevBlock.ToString() << " hashTip=" << hashTip.ToString() << " nBlocks=" << nBlocks << " hashTip.IsNull()=" << hashTip.IsNull() << " isGenesis=" << (hash == mainnetGenesisHash) << " prevNull=" << header.hashPrevBlock.IsNull() << "\n";
                    
                    // Check chain continuity - block must connect to current tip
                    // For fresh start: if hashTip is null (fresh db) and this is our first block, accept it
                    // For subsequent blocks: must connect to current tip
                    if (nBlocks == 0 && hashTip.IsNull()) {
                        // Fresh database, first block - accept it as the start of our chain
                        // This handles both genesis and starting from a checkpoint
                        std::cerr << "DBG: ACCEPTING first block with null hashTip. hash=" << hash.ToString() << " isGenesis=" << (hash == mainnetGenesisHash) << "\n";
                        hashTip = hash;
                        nHeight = 0; // Genesis block is height 0
                        if (hashTip.ToString() == "00000000eae98bf15e531e004bcaffdf3c5c5cc6444c52a9fd0f82101249ba95") {
                            std::cerr << "\n*** MARKER at line 179: hashTip set to suspicious value! ***\n";
                            std::cerr << "Block: " << hash.ToString() << "\n";
                            std::cerr << "nBlocks: " << nBlocks << "\n";
                        }
                    } else if (nBlocks == 0) {
                        // First block but database has existing data
                        // Only accept if it's genesis or matches the expected chain
                        std::cerr << "DBG: First block with existing db data. hash=" << hash.ToString() << " prev=" << header.hashPrevBlock.ToString() << " hashTip=" << hashTip.ToString() << " isGenesis=" << (hash == mainnetGenesisHash) << " prevNull=" << header.hashPrevBlock.IsNull() << "\n";
                        if (header.hashPrevBlock.IsNull() && hash == mainnetGenesisHash) {
                            std::cerr << "DBG: Accepting genesis block\n";
                            hashTip = hash;
                            nHeight = 0;
                            if (hashTip.ToString() == "00000000eae98bf15e531e004bcaffdf3c5c5cc6444c52a9fd0f82101249ba95") {
                                std::cerr << "\n*** MARKER at line 188: hashTip set to suspicious value! ***\n";
                                std::cerr << "Block: " << hash.ToString() << "\n";
                                std::cerr << "nBlocks: " << nBlocks << "\n";
                            }
                        } else {
                            // Skip - doesn't match our chain
                            std::cerr << "DBG: REJECTING first block because not genesis or doesn't match\n";
                            nBlocksSkipped++;
                            if (nBlocksSkipped <= 10) {
                                std::cerr << "Skipping block " << hash.ToString() << " (prev=" << header.hashPrevBlock.ToString() << ", tip=" << hashTip.ToString() << ") - not genesis\n";
                            } else if (nBlocksSkipped == 11) {
                                std::cerr << "... (additional blocks skipped, total: " << nBlocksSkipped << ")\n";
                            }
                            continue;
                        }
                    } else if (header.hashPrevBlock != hashTip) {
                        // Block doesn't connect to current tip - skip it (reorg/side chain)
                        std::cerr << "DBG: REJECTING block (not connecting to tip). hash=" << hash.ToString() << " prev=" << header.hashPrevBlock.ToString() << " hashTip=" << hashTip.ToString() << "\n";
                        nBlocksSkipped++;
                        if (nBlocksSkipped <= 10) {
                            std::cerr << "Skipping block " << hash.ToString() << " (prev=" << header.hashPrevBlock.ToString() << ", tip=" << hashTip.ToString() << ")\n";
                        } else if (nBlocksSkipped == 11) {
                            std::cerr << "... (additional blocks skipped, total: " << nBlocksSkipped << ")\n";
                        }
                        continue;
                    } else {
                        // Block connects properly
                        uint256 oldHashTip = hashTip;
                        hashTip = hash;
                        std::cerr << "DBG: ACCEPTING block. hash=" << hash.ToString() << " prev=" << header.hashPrevBlock.ToString() << " oldHashTip=" << oldHashTip.ToString() << " newHashTip=" << hashTip.ToString() << " nHeight=" << nHeight << " -> " << nHeight+1 << "\n";
                        nHeight++; // Increment height for each new block
                        
                        // Marker: check if hashTip was just set to the suspicious value
                        if (hashTip.ToString() == "00000000eae98bf15e531e004bcaffdf3c5c5cc6444c52a9fd0f82101249ba95") {
                            std::cerr << "\n*** MARKER at line 214: hashTip set to suspicious value! ***\n";
                            std::cerr << "Block: " << hash.ToString() << "\n";
                            std::cerr << "Prev: " << header.hashPrevBlock.ToString() << "\n";
                            std::cerr << "oldHashTip: " << oldHashTip.ToString() << "\n";
                            std::cerr << "nBlocks: " << nBlocks << "\n";
                            std::cerr << "nHeight: " << nHeight << "\n";
                            g_debug_break = 1; // Set flag for gdb conditional breakpoint
                        }
                    }
                    
                    // Now read the full block
                    blkdat.SetPos(nBlockPos);
                    blkdat.SetLimit(nBlockPos + nSize);
                    CBlock block;
                    blkdat >> TX_WITH_WITNESS(block);
                    
                    // Verify the block hash matches
                    assert(block.GetHash() == hash && "Block hash mismatch after deserialization");
                    
                    // Update rewind position to after this block
                    nRewind = blkdat.GetPos();
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
                                    // In a valid blockchain, SpendCoin should always succeed for non-coinbase tx inputs
                                    // If it fails, it means we're processing blocks out of order or the database is corrupted
                                    if (!is_spent) {
                                        nSpendFailures++;
                                        if (nSpendFailures <= 10) {
                                            std::cerr << "WARNING: SpendCoin failed for " << txin.prevout.hash.ToString() << ":" << txin.prevout.n << " in block " << hash.ToString() << " at height " << nHeight << "\n";
                                        } else if (nSpendFailures == 11) {
                                            std::cerr << "... (additional SpendCoin failures, total: " << nSpendFailures << ")\n";
                                        }
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
                            AddCoins(cache, tx, nHeight, check_for_overwrite);
                            nCoinsAdded += tx.vout.size();
                        }
                    }
                    
                    // Set best block for the cache - this updates the mutable hashBlock
                    cache.SetBestBlock(hash);
                    
                    nBlocks++;
                    nTotalBytes += nSize;
                    
                    // Flush cache periodically to avoid using too much memory
                    if (nBlocks % 100 == 0) {
                        bool flush_ok;
                        {
                            ScopedTimer timer(g_metrics_collector.stats_cache_flush);
                            flush_ok = cache.Flush();
                        }
                        if (!flush_ok) {
                            nFlushFailures++;
                            std::cerr << "CRITICAL: Flush failed at block " << nBlocks << " (height=" << nHeight << ")! Database writes are failing.\n";
                            // Use assert to get core dump on first failure
                            assert(flush_ok && "Flush failed - database write error");
                        }
                        std::cout << "  Processed " << nBlocks << " blocks (" << nTotalBytes / 1024 / 1024 << " MB, height=" << nHeight << ")...\n";
                    }
                } catch (const std::exception& e) {
                    // Block failed to deserialize, try next one
                    // This can happen with historical bugs that added extra data
                    std::cerr << "Block deserialization error at file offset " << (nRewind - 1) << ": " << e.what() << ". Continuing...\n";
                    continue;
                }
            }
            // AutoFile (file_in) and BufferedFile (blkdat) will close the file automatically
        } catch (const std::exception& e) {
            std::cerr << "Error processing file " << fs::PathToString(filename) << ": " << e.what() << "\n";
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
    if (!final_flush_ok) {
        nFlushFailures++;
        std::cerr << "CRITICAL: Final flush failed at block " << nBlocks << " (height=" << nHeight << ")! Database writes are failing.\n";
        assert(final_flush_ok && "Final flush failed - database write error");
    }
    
    auto nEnd = std::chrono::high_resolution_clock::now();
    double nSeconds = std::chrono::duration<double>(nEnd - nStart).count();
    
    std::cout << "\nProcessed " << nBlocks << " blocks (height=" << nHeight << "), " << nTotalBytes / 1024 / 1024 << " MB in " << nSeconds << " seconds\n";
    
    // Report diagnostics
    std::cout << "DIAGNOSTIC: Coins added: " << nCoinsAdded << ", Inputs spent: " << nInputsSpent << ", Net growth: " << (nCoinsAdded - nInputsSpent) << "\n";
    if (nBlocksSkipped > 0) {
        std::cout << "DIAGNOSTIC: Blocks skipped (not in main chain): " << nBlocksSkipped << "\n";
    }
    
    // Use assert() for critical failures as requested by user
    // SpendCoin failures indicate we're not properly processing the chain
    if (nSpendFailures > 0) {
        std::cerr << "DIAGNOSTIC: Total SpendCoin failures: " << nSpendFailures << "\n";
        std::cerr << "DIAGNOSTIC: Each SpendCoin failure causes database bloat by leaving a UTXO unspent\n";
        std::cerr << "DIAGNOSTIC: Expected net growth is much smaller - SpendCoin failures are causing the database to grow too large!\n";
        std::cerr << "DIAGNOSTIC: This means blocks are being processed out of order or the chain state is inconsistent\n";
        assert(nSpendFailures == 0 && "SpendCoin failures detected - database will bloat due to out-of-order processing");
    }
    if (nFlushFailures > 0) {
        std::cerr << "DIAGNOSTIC: Total Flush failures: " << nFlushFailures << "\n";
    }
    if (nSeconds > 0) {
        std::cout << "Throughput: " << (nTotalBytes / 1024.0 / 1024.0 / nSeconds) << " MB/s\n";
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
