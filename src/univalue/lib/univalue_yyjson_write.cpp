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
 * @brief Serializes using the yyjson-primary implementation
 *
 * For the yyjson-primary implementation, this delegates to writeYyjsonValueInternal.
 *
 * @param prettyIndent Indentation level for pretty printing (0 for compact)
 * @return JSON string representation
 */
std::string UniValue::writeYyjson(unsigned int prettyIndent) const {
    // For the yyjson-primary implementation, we need to use custom write
    // to preserve string escaping compatibility with original UniValue
    return writeYyjsonValueInternal(*this, prettyIndent, 0);
}

/**
 * @brief Serializes the UniValue to a JSON string
 *
 * Uses custom formatter (writeYyjsonValueInternal) for all cases to ensure
 * identical output format to the original UniValue implementation.
 * This is required for format compatibility with existing test data,
 * particularly the script_tests which use JSONPrettyPrint() workaround.
 *
 * @param prettyIndent Indentation level for pretty printing (0 for compact)
 * @param indentLevel Current nesting level (unused in this implementation)
 * @return JSON string representation
 */
std::string UniValue::write(unsigned int prettyIndent, unsigned int /* indentLevel */) const {
    // Use custom formatter for all cases to match original UniValue output exactly
    // This ensures JSONPrettyPrint() workaround in script_tests.cpp produces consistent results
    return writeYyjsonValueInternal(*this, prettyIndent, 0);
}
