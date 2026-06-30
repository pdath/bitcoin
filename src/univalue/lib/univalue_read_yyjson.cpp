// Copyright 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

/**
 * @file univalue_read_yyjson.cpp
 * @brief yyjson-based implementation of UniValue::read().
 *
 * This file contains the yyjson parser implementation for reading JSON data.
 */

#include <univalue.h>
#include <yyjson/yyjson.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Maximum JSON depth allowed (512 container levels)
static constexpr size_t MAX_JSON_DEPTH = 512;

// Custom deleter for yyjson_doc
static void yyjson_doc_deleter(yyjson_doc* doc) {
    yyjson_doc_free(doc);
}

/**
 * @brief Calculates the maximum nesting depth of a JSON value iteratively.
 *
 * This function uses an iterative depth-first traversal to avoid stack overflow
 * with deeply nested JSON. It only counts container nodes (arrays and objects),
 * matching the original UniValue's stack-based depth counting.
 *
 * @param val The yyjson value to analyze.
 * @return The maximum depth (0-indexed, container nodes only).
 */
static size_t getMaxDepth(yyjson_val* val) {
    if (!val) return 0;
    
    size_t max_depth = 0;
    
    // Use a stack for iterative depth-first traversal
    struct StackEntry {
        yyjson_val* val;
        size_t depth;
        bool children_processed;
    };
    
    std::vector<StackEntry> stack;
    stack.push_back({val, 0, false});
    
    while (!stack.empty()) {
        StackEntry& entry = stack.back();
        
        if (entry.children_processed) {
            // Already processed children, pop from stack
            // Only count container nodes for depth (to match original UniValue behavior)
            stack.pop_back();
            continue;
        }
        
        // Mark as processed
        entry.children_processed = true;
        
        yyjson_type type = yyjson_get_type(entry.val);
        
        // Update max_depth for container nodes only (arrays and objects)
        if (type == YYJSON_TYPE_ARR || type == YYJSON_TYPE_OBJ) {
            if (entry.depth > max_depth) {
                max_depth = entry.depth;
            }
        }
        
        if (type == YYJSON_TYPE_ARR) {
            yyjson_val* item;
            yyjson_arr_iter iter;
            yyjson_arr_iter_init(entry.val, &iter);
            
            size_t child_depth = entry.depth + 1;
            while ((item = yyjson_arr_iter_next(&iter))) {
                stack.push_back({item, child_depth, false});
            }
        } else if (type == YYJSON_TYPE_OBJ) {
            yyjson_val* key, *value;
            yyjson_obj_iter iter;
            yyjson_obj_iter_init(entry.val, &iter);
            
            size_t child_depth = entry.depth + 1;
            while ((key = yyjson_obj_iter_next(&iter))) {
                value = yyjson_obj_iter_get_val(key);
                stack.push_back({value, child_depth, false});
                // Note: We only process values, not keys, since keys are strings and don't have children
            }
        }
    }
    
    return max_depth;
}

/**
 * @brief Parses a JSON string using the yyjson parser.
 *
 * This implementation uses yyjson's SIMD-optimized parser with the following configuration:
 * - YYJSON_READ_STOP_WHEN_DONE: Stop parsing after the first JSON value
 * - YYJSON_READ_NUMBER_AS_RAW: Keep numbers as raw strings for custom validation
 *
 * Custom validations performed:
 * - Leading zeros and hex numbers rejected (via validateNumberString)
 * - Trailing content rejected (explicit check after parsing)
 * - Depth limit enforced at 512 container levels (via getMaxDepth)
 * - UTF-8 validation handled by yyjson natively
 *
 * @param str_in The JSON string to parse.
 * @return true on success, false on parse or validation failure.
 */
bool UniValue::read(std::string_view str_in)
{
    clear();

    if (str_in.empty()) return false;

    // Configure yyjson to:
    // - Stop when done (parse only first JSON value)
    // - Keep numbers as raw strings (to preserve formatting and validate leading zeros)
    // Note: yyjson's default UTF-8 validation matches JSONUTF8StringFilter behavior.
    // yyjson also rejects literal control characters (< 0x20) by default, matching the original parser.
    yyjson_read_flag flg = YYJSON_READ_STOP_WHEN_DONE | YYJSON_READ_NUMBER_AS_RAW;
    
    // Parse with yyjson
    yyjson_doc* doc = yyjson_read(str_in.data(), str_in.size(), flg);
    if (!doc) {
        return false;
    }

    // Check for trailing content
    size_t consumed = yyjson_doc_get_read_size(doc);
    const char* after = str_in.data() + consumed;
    const char* end = str_in.data() + str_in.size();
    
    // Skip whitespace after the JSON value
    while (after < end && (after[0] == ' ' || after[0] == '\t' || after[0] == '\n' || after[0] == '\r')) {
        after++;
    }
    
    // If there's non-whitespace content after the JSON, fail
    if (after < end) {
        yyjson_doc_free(doc);
        return false;
    }

    // Store document with automatic cleanup using shared_ptr
    m_yyjson_doc = std::shared_ptr<yyjson_doc>(doc, yyjson_doc_deleter);
    yyjson_val* root = yyjson_doc_get_root(doc);

    // Check depth limit before building UniValue tree
    // Use a helper function to calculate max depth iteratively to avoid stack overflow
    // Note: getMaxDepth returns 0-indexed depth counting only container nodes (arrays/objects)
    // Original implementation uses stack.size() which equals max_depth + 1
    // So we need to check: (max_depth + 1) > MAX_JSON_DEPTH, which is: max_depth >= MAX_JSON_DEPTH
    if (getMaxDepth(root) >= MAX_JSON_DEPTH) {
        m_yyjson_doc.reset();
        return false;
    }

    // Build UniValue tree from yyjson (depth 0 for root)
    if (!buildFromYyjson(root, 0, m_yyjson_doc)) {
        m_yyjson_doc.reset();
        return false;
    }

    return true;
}
