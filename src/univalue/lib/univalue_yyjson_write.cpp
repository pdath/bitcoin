// Copyright 2026 The Bitcoin Knots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit-license.php.

#include <univalue.h>
#include <univalue_common.h>
#include <yyjson/yyjson.h>

#include <string>
#include <vector>

/**
 * @brief Forward declaration for recursive value serialization
 *
 * Handles primitive values (VSTR, VNUM, VBOOL, VNULL) that don't have their own
 * yyjson documents by creating temporary documents for serialization.
 * This fallback path is used when we need to serialize from the materialized
 * representation (val, keys, values vectors) rather than directly from the yyjson tree.
 *
 * @param uv The UniValue to serialize
 * @param prettyIndent Indentation level for pretty printing (0 for compact)
 * @param indentLevel Current nesting level for indentation
 * @return JSON string representation of the value
 */
static std::string writeYyjsonValueInternal(const UniValue& uv, unsigned int prettyIndent, unsigned int indentLevel);

/**
 * @brief Post-process yyjson output to match UniValue escaping behavior
 *
 * Handles two differences:
 * 1. yyjson doesn't escape DEL (0x7f) by default, but UniValue does
 * 2. yyjson uses uppercase hex in \uXXXX escapes, but UniValue uses lowercase
 *
 * Uses optimized fast-path checks to avoid unnecessary processing.
 *
 * @param result The JSON string from yyjson_mut_write
 * @return Post-processed string matching UniValue behavior
 */
static std::string postProcessYyjsonOutput(std::string result) {
    // Fast path: check if any processing is needed
    size_t del_pos = result.find(0x7f);
    size_t u_pos = result.find("\\u");

    if (del_pos == std::string::npos && u_pos == std::string::npos) {
        return result; // No processing needed at all
    }

    // Check if uppercase hex conversion is actually needed
    bool needs_uppercase_conversion = false;
    if (u_pos != std::string::npos && del_pos == std::string::npos) {
        // Only need to check for uppercase if there are escape sequences but no DEL
        for (size_t i = u_pos; i < result.size(); ) {
            if (result[i] == '\\' && i + 5 < result.size() && result[i+1] == 'u') {
                for (int j = 2; j < 6; j++) {
                    if (result[i+j] >= 'A' && result[i+j] <= 'F') {
                        needs_uppercase_conversion = true;
                        goto processing_needed;
                    }
                }
                i += 6;
            } else {
                i++;
            }
        }
        if (!needs_uppercase_conversion) {
            return result; // No uppercase hex to convert
        }
    }
    processing_needed:

    std::string final_result;
    final_result.reserve(result.size() + (del_pos != std::string::npos ? 6 : 0));

    for (size_t i = 0; i < result.size(); i++) {
        unsigned char c = result[i];
        if (c == 0x7f) {
            // Replace DEL with \u007f
            final_result += "\\u007f";
        } else if (i + 1 < result.size() && c == '\\' && result[i+1] == 'u') {
            // Found \uXXXX, copy it and convert hex digits to lowercase
            final_result += '\\';
            final_result += 'u';
            if (i + 5 < result.size()) {
                for (int j = 2; j < 6; j++) {
                    char hex_char = result[i + j];
                    if (hex_char >= 'A' && hex_char <= 'F') {
                        final_result += (hex_char - 'A' + 'a');
                    } else {
                        final_result += hex_char;
                    }
                }
                i += 5; // Skip the next 5 characters
            }
        } else {
            final_result += c;
        }
    }

    return final_result;
}

/**
 * @brief Recursively serialize a UniValue to JSON string
 *
 * This function handles the materialized representation (val, keys, values vectors)
 * for serialization. It's used as a fallback when we need to serialize from the
 * materialized cache rather than directly from the yyjson tree.
 *
 * @param uv The UniValue to serialize
 * @param prettyIndent Indentation level for pretty printing (0 for compact)
 * @param indentLevel Current nesting level for indentation
 * @return JSON string representation of the value
 */
static std::string writeYyjsonValueInternal(const UniValue& uv, unsigned int prettyIndent, unsigned int indentLevel) {
    const bool pretty = prettyIndent > 0;
    std::string indentStr = pretty ? std::string(indentLevel * prettyIndent, ' ') : "";
    std::string nextIndentStr = pretty ? std::string((indentLevel + 1) * prettyIndent, ' ') : "";
    
    switch (uv.getType()) {
        case UniValue::VNULL:
            return "null";
        case UniValue::VBOOL:
            return uv.isTrue() ? "true" : "false";
        case UniValue::VNUM:
            return uv.getValStr(); // Preserve exact number formatting
        case UniValue::VSTR:
            return '"' + json_escape(uv.getValStr()) + '"';
        case UniValue::VARR: {
            if (uv.empty()) {
                if (!pretty) return "[]";
                return std::string("[\n") + indentStr + "]";
            }
            std::string s = "[";
            if (pretty) s += "\n";
            const auto& values = uv.getValues();
            for (size_t i = 0; i < values.size(); ++i) {
                if (pretty) s += nextIndentStr;
                s += writeYyjsonValueInternal(values[i], prettyIndent, indentLevel + 1);
                if (i < values.size() - 1) {
                    s += ",";
                }
                if (pretty) s += "\n";
            }
            if (pretty) s += indentStr;
            s += "]";
            return s;
        }
        case UniValue::VOBJ: {
            if (uv.empty()) {
                if (!pretty) return "{}";
                return std::string("{\n") + indentStr + "}";
            }
            std::string s = "{";
            if (pretty) s += "\n";
            const auto& keys = uv.getKeys();
            const auto& values = uv.getValues();
            for (size_t i = 0; i < keys.size(); ++i) {
                if (pretty) s += nextIndentStr;
                s += '"' + json_escape(keys[i]) + std::string("\":");
                if (pretty) s += " ";
                s += writeYyjsonValueInternal(values[i], prettyIndent, indentLevel + 1);
                if (i < keys.size() - 1) {
                    s += ",";
                }
                if (pretty) s += "\n";
            }
            if (pretty) s += indentStr;
            s += "}";
            return s;
        }
    }
    return "";
}

/**
 * @brief Write a VSTR without yyjson document using a temporary document
 *
 * For manually constructed string primitives (VSTR without m_yyjson_doc),
 * create a temporary document for serialization using yyjson_mut_write.
 *
 * @param prettyIndent Indentation level for pretty printing (0 for compact)
 * @return JSON string representation
 */
static std::string writeYyjsonStrPrimitive(const UniValue& uv, unsigned int prettyIndent) {
    // Create a temporary document and node for this primitive string
    yyjson_mut_doc* temp_doc = yyjson_mut_doc_new(nullptr);
    const std::string& str = uv.getValStr();
    yyjson_mut_val* temp_node = (yyjson_mut_val*)yyjson_mut_strncpy(temp_doc, str.data(), str.size());
    yyjson_mut_doc_set_root(temp_doc, temp_node);

    yyjson_write_flag flags = prettyIndent ? YYJSON_WRITE_PRETTY_TWO_SPACES : YYJSON_WRITE_NOFLAG;
    size_t len = 0;
    char* output = yyjson_mut_write_opts(temp_doc, flags, nullptr, &len, nullptr);
    std::string result(output, len);
    free(output);
    yyjson_mut_doc_free(temp_doc);

    // Post-process to match UniValue's escaping behavior for DEL (0x7f)
    // Fast-path: check if any processing is needed
    if (result.find(0x7f) == std::string::npos) {
        return result; // No processing needed
    }

    // Replace DEL characters with \u007f
    std::string final_result;
    final_result.reserve(result.size() + 10);
    for (size_t i = 0; i < result.size(); i++) {
        unsigned char c = result[i];
        if (c == 0x7f) {
            final_result += "\\u007f";
        } else {
            final_result += c;
        }
    }
    return final_result;
}

/**
 * @brief Serializes the UniValue to a JSON string
 *
 * Optimized implementation using yyjson_mut_write for maximum performance:
 * - For VNUM, VNULL, VBOOL: Returns pre-formatted strings directly
 * - For VSTR with document: Uses yyjson_mut_write
 * - For VSTR without document: Creates temporary document and uses yyjson_mut_write
 * - For VARR, VOBJ: Uses yyjson_mut_write directly on the document
 * - For custom indentation (prettyIndent != 0 && prettyIndent != 2): Falls back to writeYyjsonValueInternal
 *
 * Post-processing handles DEL (0x7f) character escaping to match UniValue behavior.
 *
 * @param prettyIndent Indentation level for pretty printing (0 for compact, 2 for 2-space pretty)
 * @return JSON string representation
 */
std::string UniValue::writeYyjson(unsigned int prettyIndent) const {
    // Fast path for VNUM: return val directly (already properly formatted)
    if (typ == VNUM) {
        return val;
    }

    // Handle VNULL: return "null"
    if (typ == VNULL) {
        return "null";
    }

    // Handle VBOOL: convert "1"/"" to "true"/"false"
    if (typ == VBOOL) {
        return val == "1" ? "true" : "false";
    }

    // For VSTR, VOBJ, VARR: use yyjson_mut_write directly for maximum performance
    // This is the fast path for the common case

    // Determine if we can use yyjson_mut_write directly
    // We can use it for: VSTR with document, VARR, VOBJ (all have documents)
    // We need to fall back for: VSTR without document (needs temp doc), or custom indentation
    bool can_use_yyjson_direct = false;
    yyjson_mut_doc* doc_to_use = nullptr;

    if (typ == VSTR && m_yyjson_doc) {
        can_use_yyjson_direct = true;
        doc_to_use = m_yyjson_doc.get();
    } else if ((typ == VARR || typ == VOBJ) && m_yyjson_doc) {
        can_use_yyjson_direct = true;
        doc_to_use = m_yyjson_doc.get();
    }

    // Use yyjson_mut_write for standard indentation (0 or 2)
    if (can_use_yyjson_direct && doc_to_use && (prettyIndent == 0 || prettyIndent == 2)) {
        yyjson_write_flag flags = prettyIndent ? YYJSON_WRITE_PRETTY_TWO_SPACES : YYJSON_WRITE_NOFLAG;
        size_t len = 0;
        char* output = yyjson_mut_write_opts(doc_to_use, flags, nullptr, &len, nullptr);
        std::string result(output, len);
        free(output);
        return postProcessYyjsonOutput(std::move(result));
    }

    // For VSTR without document, use temporary document
    if (typ == VSTR && !m_yyjson_doc && !m_yyjson_node) {
        return writeYyjsonStrPrimitive(*this, prettyIndent);
    }

    // Fallback for custom indentation levels or other cases
    // Use writeYyjsonValueInternal which handles all formatting correctly
    return writeYyjsonValueInternal(*this, prettyIndent, 0);
}

/**
 * @brief Serializes the UniValue to a JSON string
 *
 * Delegates to writeYyjson() which uses the optimized yyjson_mut_write path
 * for maximum performance when possible.
 *
 * @param prettyIndent Indentation level for pretty printing (0 for compact)
 * @param indentLevel Current nesting level (unused in this implementation)
 * @return JSON string representation
 */
std::string UniValue::write(unsigned int prettyIndent, unsigned int /* indentLevel */) const {
    // Delegate to writeYyjson which handles the optimized path
    return writeYyjson(prettyIndent);
}
