// Copyright 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit-license.php.

#include <univalue.h>
#include <yyjson/yyjson.h>

#include <string>
#include <string_view>
#include <vector>

// Maximum JSON depth allowed (512 container levels)
static constexpr size_t MAX_JSON_DEPTH = 512;

// Helper function to deep copy a yyjson value into a target document
// Note: This function is duplicated in univalue_yyjson.cpp to avoid header dependencies
static yyjson_mut_val* copyYyjsonValue(yyjson_val* src_val, yyjson_mut_doc* target_doc) {
    if (!src_val) return nullptr;
    
    yyjson_type type = yyjson_get_type(src_val);
    
    switch (type) {
        case YYJSON_TYPE_NULL:
            return yyjson_mut_null(target_doc);
        case YYJSON_TYPE_BOOL:
            return yyjson_mut_bool(target_doc, yyjson_get_bool(src_val));
        case YYJSON_TYPE_NUM:
        case YYJSON_TYPE_RAW: {
            const char* raw = yyjson_get_raw(src_val);
            size_t len = yyjson_get_len(src_val);
            if (raw && len > 0) {
                return yyjson_mut_rawncpy(target_doc, raw, len);
            } else {
                return yyjson_mut_null(target_doc);
            }
        }
        case YYJSON_TYPE_STR: {
            const char* str = yyjson_get_str(src_val);
            size_t len = yyjson_get_len(src_val);
            return yyjson_mut_strncpy(target_doc, str, len);
        }
        case YYJSON_TYPE_ARR: {
            yyjson_mut_val* arr = yyjson_mut_arr(target_doc);
            size_t idx, max;
            yyjson_val *item;
            yyjson_arr_foreach(src_val, idx, max, item) {
                yyjson_mut_arr_append(arr, copyYyjsonValue(item, target_doc));
            }
            return arr;
        }
        case YYJSON_TYPE_OBJ: {
            yyjson_mut_val* obj = yyjson_mut_obj(target_doc);
            yyjson_val *key, *val;
            yyjson_obj_iter iter;
            yyjson_obj_iter_init(src_val, &iter);
            while ((key = yyjson_obj_iter_next(&iter))) {
                val = yyjson_obj_iter_get_val(key);
                const char* kstr = yyjson_get_str(key);
                size_t klen = yyjson_get_len(key);
                yyjson_mut_val* new_key = yyjson_mut_strncpy(target_doc, kstr, klen);
                yyjson_mut_val* new_val = copyYyjsonValue(val, target_doc);
                yyjson_mut_obj_add(obj, new_key, new_val);
            }
            return obj;
        }
        default:
            return nullptr;
    }
}

// Helper function to calculate maximum nesting depth
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
            }
        }
    }
    
    return max_depth;
}

bool UniValue::read(std::string_view str_in) {
    clear();

    if (str_in.empty()) return false;

    // yyjson_read requires non-const char*, so we need to make a mutable copy
    std::string str_copy(str_in);
    
    // Use yyjson to parse the JSON
    yyjson_read_flag flags = YYJSON_READ_NUMBER_AS_RAW | YYJSON_READ_STOP_WHEN_DONE;
    yyjson_doc* doc = yyjson_read(str_copy.data(), str_copy.size(), flags);
    if (!doc) {
        return false;
    }

    // Check for trailing content
    size_t consumed = yyjson_doc_get_read_size(doc);
    const char* after = str_copy.data() + consumed;
    const char* end = str_copy.data() + str_copy.size();

    // yyjson with YYJSON_READ_STOP_WHEN_DONE stops at the first non-JSON token
    // For "7\n", it stops after "7", with after pointing to "\n"
    // The original UniValue parser accepts trailing whitespace, so we should too
    // Skip whitespace after the JSON value
    while (after < end && (after[0] == ' ' || after[0] == '\t' || after[0] == '\n' || after[0] == '\r')) {
        after++;
    }

    // If there's non-whitespace content after the JSON, fail
    // But if we reached the end or only have whitespace, it's OK
    if (after < end) {
        // There's content after the JSON - this is only OK if it's all whitespace
        // Actually, we already skipped whitespace above, so if after < end, it means
        // there's non-whitespace content, which should fail
        yyjson_doc_free(doc);
        return false;
    }

    // Check depth limit before building UniValue tree
    // Note: getMaxDepth returns 0-indexed depth counting only container nodes (arrays/objects)
    // Original implementation uses stack.size() which equals max_depth + 1
    // So we need to check: (max_depth + 1) > MAX_JSON_DEPTH, which is: max_depth >= MAX_JSON_DEPTH
    yyjson_val* root = yyjson_doc_get_root(doc);
    if (getMaxDepth(root) >= MAX_JSON_DEPTH) {
        yyjson_doc_free(doc);
        return false;
    }
    
    // yyjson_read returns an immutable doc, but we need a mutable doc for consistency
    // Create a new mutable document and copy the tree
    yyjson_mut_doc* mut_doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* mut_root = copyYyjsonValue(root, mut_doc);
    yyjson_mut_doc_set_root(mut_doc, mut_root);
    
    // Store document with automatic cleanup using shared_ptr
    m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(mut_doc, yyjson_doc_deleter);
    m_yyjson_node = (yyjson_val*)mut_root;
    
    // Free the immutable document from yyjson_read
    yyjson_doc_free(doc);
    
    // Set the type based on the root value
    switch (yyjson_get_type(m_yyjson_node)) {
        case YYJSON_TYPE_NULL:
            typ = VNULL;
            break;
        case YYJSON_TYPE_BOOL:
            typ = VBOOL;
            val = yyjson_get_bool(m_yyjson_node) ? "1" : "0";
            break;
        case YYJSON_TYPE_RAW:
        case YYJSON_TYPE_NUM: {
            typ = VNUM;
            const char* raw = yyjson_get_raw(m_yyjson_node);
            size_t len = yyjson_get_len(m_yyjson_node);
            if (raw && len > 0) {
                val.assign(raw, len);
            } else {
                // Fallback: try to get the string representation
                const char* str = yyjson_get_str(m_yyjson_node);
                if (str && len > 0) {
                    val.assign(str, len);
                } else {
                    val = "0"; // Fallback for invalid numbers
                }
            }
            break;
        }
        case YYJSON_TYPE_STR: {
            typ = VSTR;
            const char* str = yyjson_get_str(m_yyjson_node);
            size_t len = yyjson_get_len(m_yyjson_node);
            if (str && len > 0) {
                val.assign(str, len);
            } else {
                val = ""; // Fallback for invalid strings
            }
            break;
        }
        case YYJSON_TYPE_ARR:
            typ = VARR;
            break;
        case YYJSON_TYPE_OBJ:
            typ = VOBJ;
            break;
    }
    
    m_materialized = (typ != VARR && typ != VOBJ);

    return true;
}