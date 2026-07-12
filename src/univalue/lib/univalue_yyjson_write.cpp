// Copyright 2026 The Bitcoin Knots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit-license.php.

#include <univalue.h>
#include <univalue_common.h>
#include <yyjson/yyjson.h>

#include <string>
#include <vector>

/**
 * @brief Post-process yyjson output to match UniValue escaping behaviour
 *
 * Handles two differences:
 * 1. yyjson doesn't escape DEL (0x7f) by default, but UniValue does
 * 2. yyjson uses uppercase hex in \uXXXX escapes, but UniValue uses lowercase
 *
 * Uses optimized single-pass processing to avoid multiple string scans.
 *
 * @param result The JSON string from yyjson_mut_write
 * @return Post-processed string matching UniValue behaviour
 */
static std::string postProcessYyjsonOutput(std::string result) {
    // Fast path: check if any processing is needed
    size_t del_pos = result.find(0x7f);
    size_t u_pos = result.find("\\u");

    if (del_pos == std::string::npos && u_pos == std::string::npos) {
        return result; // No processing needed at all
    }

    // Single-pass processing: handle both DEL and \uXXXX in one iteration
    std::string final_result;
    final_result.reserve(result.size() + 10); // Extra space for potential expansions

    const size_t UNICODE_ESCAPE_LENGTH = 6; // Length of "\uXXXX" sequence
    const size_t HEX_START = 2; // Position of first hex digit in "\uXXXX"

    for (size_t i = 0; i < result.size(); ) {
        unsigned char c = result[i];

        if (c == 0x7f) {
            // Replace DEL with \u007f
            final_result += "\\u007f";
            ++i;
        } else if (c == '\\' && i + 1 < result.size()) {
            // Check for escaped backslash (\\\\) first
            if (result[i+1] == '\\') {
                // Literal escaped backslash - copy both characters as-is
                final_result += '\\';
                final_result += '\\';
                i += 2;
            } else if (result[i+1] == 'u') {
                // Found start of \uXXXX sequence

                // Check if we have a complete \uXXXX sequence
                if (i + UNICODE_ESCAPE_LENGTH <= result.size()) {
                    // Process complete \uXXXX sequence, converting uppercase hex to lowercase
                    final_result += '\\';
                    final_result += 'u';

                    // Process all 4 hex digits, converting uppercase to lowercase
                    for (size_t j = HEX_START; j < UNICODE_ESCAPE_LENGTH; ++j) {
                        char hex_char = result[i + j];
                        if (hex_char >= 'A' && hex_char <= 'F') {
                            final_result += (hex_char - 'A' + 'a');
                        } else {
                            // For lowercase hex, digits, or invalid chars: copy as-is
                            // (lowercase hex and digits don't need conversion)
                            final_result += hex_char;
                        }
                    }
                    i += UNICODE_ESCAPE_LENGTH; // Skip the entire \uXXXX sequence
                } else {
                    // Incomplete \u sequence at end of string - copy characters as-is
                    // Don't interpret as escape sequence
                    final_result += c;
                    ++i;
                }
            } else {
                // Other escape sequences (like \n, \t, etc.) - copy as-is
                final_result += c;
                ++i;
            }
        } else {
            final_result += c;
            ++i;
        }
    }

    return final_result;
}

/**
 * @brief Recursively serialize a UniValue to JSON string (unsafe version)
 *
 * This function handles the materialized representation (val, keys, values vectors)
 * for serialization. It assumes the document mutex is already held by the caller.
 * It's used as a fallback when we need to serialize from the materialized cache rather
 * than directly from the yyjson tree.
 *
 * Unsafe version: assumes the caller holds the document mutex. Only accesses member
 * variables directly or calls other unsafe functions.
 *
 * @param prettyIndent Indentation level for pretty printing (0 for compact)
 * @param indentLevel Current nesting level for indentation
 * @return JSON string representation of the value
 */
std::string UniValue::writeYyjsonValueInternal_unsafe(unsigned int prettyIndent, unsigned int indentLevel) const {
    const bool pretty = prettyIndent > 0;
    std::string indentStr;
    if (pretty) {
        ::indentStr(prettyIndent, indentLevel, indentStr);
    }

    // Access members directly to avoid deadlock (we already hold the document mutex)
    switch (typ) {
        case VNULL:
            return "null";
        case VBOOL:
            return val == "1" ? "true" : "false";
        case VNUM:
            return val; // Preserve exact number formatting
        case VSTR:
            return '"' + json_escape(val) + '"';
        case VARR: {
            if (values.empty()) {
                if (!pretty) return "[]";
                std::string s = "[";
                s += "\n";
                if (indentLevel > 0) {
                    std::string closeIndentStr;
                    ::indentStr(prettyIndent, indentLevel - 1, closeIndentStr);
                    s += closeIndentStr;
                }
                s += "]";
                return s;
            }
            std::string s = "[";
            if (pretty) s += "\n";
            for (size_t i = 0; i < values.size(); ++i) {
                if (pretty) s += indentStr;
                // For child values, use their unsafe writeYyjson_unsafe() since we already hold the parent's document lock
                // If child shares the same document, we're safe; if child has its own document, writeYyjson_unsafe
                // will not lock it (which is fine as it's const and we're not modifying)
                s += values[i].writeYyjson_unsafe(prettyIndent, indentLevel + 1);
                if (i < values.size() - 1) {
                    s += ",";
                }
                if (pretty) s += "\n";
            }
            if (pretty) {
                if (indentLevel > 0) {
                    std::string closeIndentStr;
                    ::indentStr(prettyIndent, indentLevel - 1, closeIndentStr);
                    s += closeIndentStr;
                }
            }
            s += "]";
            return s;
        }
        case VOBJ: {
            if (keys.empty()) {
                if (!pretty) return "{}";
                std::string s = "{";
                s += "\n";
                if (indentLevel > 0) {
                    std::string closeIndentStr;
                    ::indentStr(prettyIndent, indentLevel - 1, closeIndentStr);
                    s += closeIndentStr;
                }
                s += "}";
                return s;
            }
            std::string s = "{";
            if (pretty) s += "\n";
            for (size_t i = 0; i < keys.size(); ++i) {
                if (pretty) s += indentStr;
                s += '"' + json_escape(keys[i]) + std::string("\":");
                if (pretty) s += " ";
                // For child values, use their unsafe writeYyjson_unsafe() since we already hold the parent's document lock
                // If child shares the same document, we're safe; if child has its own document, writeYyjson_unsafe
                // will not lock it (which is fine as it's const and we're not modifying)
                s += values[i].writeYyjson_unsafe(prettyIndent, indentLevel + 1);
                if (i < keys.size() - 1) {
                    s += ",";
                }
                if (pretty) s += "\n";
            }
            if (pretty) {
                if (indentLevel > 0) {
                    std::string closeIndentStr;
                    ::indentStr(prettyIndent, indentLevel - 1, closeIndentStr);
                    s += closeIndentStr;
                }
            }
            s += "}";
            return s;
        }
    }
    return "";
}

/**
 * @brief Write a VSTR without yyjson document using a temporary document
 *
 * Unsafe version: assumes the caller holds the document mutex.
 * For manually constructed string primitives (VSTR without m_yyjson_doc),
 * create a temporary document for serialization using yyjson_mut_write.
 *
 * @param uv The UniValue to serialize (must be VSTR without document)
 * @param prettyIndent Indentation level for pretty printing (0 for compact)
 * @return JSON string representation
 */
std::string writeYyjsonStrPrimitive(const UniValue& uv, unsigned int prettyIndent) {
    // Unsafe: assumes caller holds the document mutex
    // Use getValStr_unsafe() to avoid re-acquiring the lock
    yyjson_mut_doc* temp_doc = yyjson_mut_doc_new(nullptr);
    const std::string& str = uv.getValStr_unsafe();
    yyjson_mut_val* temp_node = (yyjson_mut_val*)yyjson_mut_strncpy(temp_doc, str.data(), str.size());
    yyjson_mut_doc_set_root(temp_doc, temp_node);

    yyjson_write_flag flags = prettyIndent ? YYJSON_WRITE_PRETTY_TWO_SPACES : YYJSON_WRITE_NOFLAG;
    size_t len = 0;
    char* output = yyjson_mut_write_opts(temp_doc, flags, nullptr, &len, nullptr);
    std::string result;
    if (output) {
        result = std::string(output, len);
        free(output);
    }
    yyjson_mut_doc_free(temp_doc);

    // Use the shared post-processing function to handle DEL and \uXXXX
    return postProcessYyjsonOutput(std::move(result));
}

/**
 * @brief Serializes the UniValue to a JSON string (unsafe version)
 *
 * Optimized implementation using yyjson_mut_write for maximum performance:
 * - For VNUM, VNULL, VBOOL: Returns pre-formatted strings directly
 * - For VSTR with document: Uses yyjson_mut_write
 * - For VSTR without document: Creates temporary document and uses yyjson_mut_write
 * - For VARR, VOBJ: Uses yyjson_mut_write directly on the document
 * - For custom indentation (prettyIndent != 0 && prettyIndent != 2): Falls back to writeYyjsonValueInternal_unsafe
 *
 * Unsafe version: assumes the caller holds the document mutex. Only accesses member
 * variables directly or calls other unsafe functions.
 *
 * Post-processing handles DEL (0x7f) character escaping to match UniValue behaviour.
 *
 * @param prettyIndent Indentation level for pretty printing (0 for compact, 2 for 2-space pretty)
 * @param indentLevel Current nesting level for indentation
 * @return JSON string representation
 */
std::string UniValue::writeYyjson_unsafe(unsigned int prettyIndent, unsigned int indentLevel) const {
    // Access members directly since caller holds the lock
    VType my_typ = typ;
    
    // Fast path for VNUM: return val directly (already properly formatted)
    if (my_typ == VNUM) {
        return val;
    }

    // Handle VNULL: return "null"
    if (my_typ == VNULL) {
        return "null";
    }

    // Handle VBOOL: convert "1"/"" to "true"/"false"
    if (my_typ == VBOOL) {
        return val == "1" ? "true" : "false";
    }

    // For VSTR, VOBJ, VARR: use yyjson_mut_write directly for maximum performance
    // This is the fast path for the common case

    // Determine if we can use yyjson_mut_write directly
    // We can use it for: VSTR with document, VARR, VOBJ (all have documents)
    // We need to fall back for: VSTR without document (needs temp doc), or custom indentation
    bool can_use_yyjson_direct = false;
    yyjson_mut_doc* doc_to_use = nullptr;

    if (my_typ == VSTR && m_yyjson_doc) {
        can_use_yyjson_direct = true;
        doc_to_use = m_yyjson_doc->m_doc;
    } else if ((my_typ == VARR || my_typ == VOBJ) && m_yyjson_doc) {
        can_use_yyjson_direct = true;
        doc_to_use = m_yyjson_doc->m_doc;
    }

    // Use yyjson_mut_write for standard indentation (0 or 2) and when indentLevel is 1 (root level)
    // For non-standard indentation or non-root levels, fall back to writeYyjsonValueInternal_unsafe
    // Additionally, require m_yyjson_node to be the document root to avoid serializing
    // the entire document when writing a materialized child node
    bool use_fast_path = can_use_yyjson_direct && doc_to_use && (prettyIndent == 0 || prettyIndent == 2) && indentLevel == 1 &&
                         m_yyjson_node && m_yyjson_node == yyjson_mut_doc_get_root(doc_to_use);

    if (use_fast_path) {
        // For empty containers, yyjson's pretty-printing indentation doesn't match the legacy behaviour
        // so fall back to writeYyjsonValueInternal_unsafe for consistent formatting
        // Check for emptiness without calling empty() to avoid forcing materialization
        bool is_empty_container = false;
        if (my_typ == VARR || my_typ == VOBJ) {
            // Check the yyjson tree directly to avoid materialization
            is_empty_container = (yyjson_mut_get_type(m_yyjson_node) == YYJSON_TYPE_ARR)
                ? (yyjson_mut_arr_size(m_yyjson_node) == 0)
                : (yyjson_mut_obj_size(m_yyjson_node) == 0);
        }
        if (is_empty_container) {
            return writeYyjsonValueInternal_unsafe(prettyIndent, indentLevel);
        }

        yyjson_write_flag flags = prettyIndent ? YYJSON_WRITE_PRETTY_TWO_SPACES : YYJSON_WRITE_NOFLAG;
        size_t len = 0;
        char* output = yyjson_mut_write_opts(doc_to_use, flags, nullptr, &len, nullptr);
        if (!output) {
            // Handle write failure by falling through to alternative path
            return writeYyjsonValueInternal_unsafe(prettyIndent, indentLevel);
        }
        std::string result(output, len);
        free(output);
        return postProcessYyjsonOutput(std::move(result));
    }

    // If we have a yyjson node but it's not the root, serialize just that node
    // Only use yyjson's write for indentation levels it supports (0 or 2 spaces)
    // For pretty output, check if this is an empty container - yyjson's pretty printer
    // keeps empty containers compact ([]/{}), but UniValue uses indented formatting
    if (m_yyjson_node && !use_fast_path && (prettyIndent == 0 || prettyIndent == 2)) {
        // Check if this node is an empty container (array or object with no children)
        // If so, fall through to writeYyjsonValueInternal_unsafe for correct formatting
        yyjson_type node_type = yyjson_mut_get_type(m_yyjson_node);
        bool is_empty_container = false;
        if (prettyIndent > 0 && (node_type == YYJSON_TYPE_ARR || node_type == YYJSON_TYPE_OBJ)) {
            is_empty_container = (node_type == YYJSON_TYPE_ARR)
                ? (yyjson_mut_arr_size(m_yyjson_node) == 0)
                : (yyjson_mut_obj_size(m_yyjson_node) == 0);
        }
        if (!is_empty_container) {
            yyjson_write_flag flags = prettyIndent ? YYJSON_WRITE_PRETTY_TWO_SPACES : YYJSON_WRITE_NOFLAG;
            size_t len = 0;
            char* output = yyjson_mut_val_write_opts(m_yyjson_node, flags, nullptr, &len, nullptr);
            if (output) {
                std::string result(output, len);
                free(output);
                return postProcessYyjsonOutput(std::move(result));
            }
        }
        // Fall through to writeYyjsonValueInternal_unsafe on failure or for empty containers
        return writeYyjsonValueInternal_unsafe(prettyIndent, indentLevel);
    }

    // For VSTR without document, use temporary document
    if (my_typ == VSTR && !m_yyjson_doc && !m_yyjson_node) {
        return writeYyjsonStrPrimitive(*this, prettyIndent);
    }

    // Fallback for custom indentation levels, non-root levels, or other cases
    // Use writeYyjsonValueInternal_unsafe which handles all formatting correctly with indentLevel
    return writeYyjsonValueInternal_unsafe(prettyIndent, indentLevel);
}

/**
 * @brief Serializes the UniValue to a JSON string
 *
 * Safe version: Acquires the document mutex before calling the unsafe version.
 * Optimized implementation using yyjson_mut_write for maximum performance:
 * - For VNUM, VNULL, VBOOL: Returns pre-formatted strings directly
 * - For VSTR with document: Uses yyjson_mut_write
 * - For VSTR without document: Creates temporary document and uses yyjson_mut_write
 * - For VARR, VOBJ: Uses yyjson_mut_write directly on the document
 * - For custom indentation (prettyIndent != 0 && prettyIndent != 2): Falls back to writeYyjsonValueInternal_unsafe
 *
 * Post-processing handles DEL (0x7f) character escaping to match UniValue behaviour.
 *
 * Thread-safety: Holds document-level mutex during yyjson tree access to prevent
 * concurrent mutations from corrupting the tree during serialization.
 *
 * @param prettyIndent Indentation level for pretty printing (0 for compact, 2 for 2-space pretty)
 * @param indentLevel Current nesting level for indentation
 * @return JSON string representation
 */
std::string UniValue::writeYyjson(unsigned int prettyIndent, unsigned int indentLevel) const {
    // Snapshot document pointer before acquiring lock
    auto doc_holder = m_yyjson_doc;
    
    if (doc_holder) {
        std::lock_guard<std::mutex> lock(doc_holder->m_mutex);
        // Re-check under the lock: ensure doc_holder is still current and node is valid
        if (m_yyjson_doc == doc_holder && m_yyjson_node) {
            return writeYyjson_unsafe(prettyIndent, indentLevel);
        }
        // Document changed or no node - re-acquire the CURRENT document's lock
        // First, we need to release the old lock (done when lock_guard goes out of scope)
        // Then acquire the new lock
    }
    
    // No document or document changed, use fallback path
    // Need to acquire the current document's lock (if it exists) before calling unsafe function
    doc_holder = m_yyjson_doc;
    if (doc_holder) {
        std::lock_guard<std::mutex> lock(doc_holder->m_mutex);
        return writeYyjsonValueInternal_unsafe(prettyIndent, indentLevel);
    }
    
    // No document at all, safe to call unsafe function without lock
    return writeYyjsonValueInternal_unsafe(prettyIndent, indentLevel);
}

/**
 * @brief Serializes the UniValue to a JSON string
 *
 * Delegates to writeYyjson() which uses the optimized yyjson_mut_write path
 * for maximum performance when possible.
 *
 * @param prettyIndent Indentation level for pretty printing (0 for compact)
 * @param indentLevel Current nesting level
 * @return JSON string representation
 */
std::string UniValue::write(unsigned int prettyIndent, unsigned int indentLevel) const {
    // Handle indentLevel the same way as the non-yyjson backend:
    // if indentLevel is 0, treat it as 1 for the first level when pretty printing
    unsigned int modIndent = indentLevel;
    if (modIndent == 0) {
        modIndent = 1;
    }
    return writeYyjson(prettyIndent, modIndent);
}
