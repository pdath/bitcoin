// Copyright 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

/**
 * @file univalue_write_yyjson.cpp
 * @brief yyjson-based implementation of UniValue::write() with hybrid approach.
 *
 * This file contains the hybrid yyjson serialization implementation:
 * - Uses yyjson's native writer for objects, arrays, strings, booleans, and null
 * - Writes numbers directly from their raw `val` string to preserve exact formatting
 *
 * Design Rationale:
 * - yyjson's number writer may reformat numbers (e.g., change precision, remove trailing zeros)
 * - UniValue stores numbers as exact strings and must preserve this formatting
 * - All other types can use yyjson's writer for consistency and performance
 */

#include <univalue.h>
#include <univalue_common.h>
#include <yyjson/yyjson.h>

#include <memory>
#include <string>
#include <vector>

/**
 * @brief RAII wrapper for yyjson mutable document.
 *
 * Ensures proper cleanup of yyjson document and its allocated memory.
 */
class YyjsonDocWrapper {
private:
    yyjson_mut_doc* m_doc;
    char* m_output;

public:
    explicit YyjsonDocWrapper(yyjson_mut_doc* doc = nullptr) : m_doc(doc), m_output(nullptr) {}
    ~YyjsonDocWrapper() {
        if (m_output) {
            free(m_output);
            m_output = nullptr;
        }
        if (m_doc) {
            yyjson_mut_doc_free(m_doc);
            m_doc = nullptr;
        }
    }

    // Disable copying to prevent double-free
    YyjsonDocWrapper(const YyjsonDocWrapper&) = delete;
    YyjsonDocWrapper& operator=(const YyjsonDocWrapper&) = delete;

    // Allow move semantics
    YyjsonDocWrapper(YyjsonDocWrapper&& other) noexcept : m_doc(other.m_doc), m_output(other.m_output) {
        other.m_doc = nullptr;
        other.m_output = nullptr;
    }

    YyjsonDocWrapper& operator=(YyjsonDocWrapper&& other) noexcept {
        if (this != &other) {
            if (m_output) free(m_output);
            if (m_doc) yyjson_mut_doc_free(m_doc);
            m_doc = other.m_doc;
            m_output = other.m_output;
            other.m_doc = nullptr;
            other.m_output = nullptr;
        }
        return *this;
    }

    yyjson_mut_doc* get() const { return m_doc; }
    yyjson_mut_doc* release() {
        yyjson_mut_doc* temp = m_doc;
        m_doc = nullptr;
        return temp;
    }

    char* get_output() const { return m_output; }
    void set_output(char* output) { m_output = output; }

    void reset(yyjson_mut_doc* doc = nullptr) {
        if (m_output) {
            free(m_output);
            m_output = nullptr;
        }
        if (m_doc) {
            yyjson_mut_doc_free(m_doc);
            m_doc = nullptr;
        }
        m_doc = doc;
    }
};

/**
 * @brief Gets the appropriate yyjson write flags based on prettyIndent.
 *
 * @param prettyIndent The indentation level for pretty printing (0 for compact).
 * @return The yyjson write flags to use.
 */
static yyjson_write_flag getWriteFlags(unsigned int prettyIndent) {
    if (prettyIndent == 0) {
        return YYJSON_WRITE_NOFLAG;
    }
    // Use 2-space indent regardless of prettyIndent value
    // This accepts slight formatting difference per Option B
    return YYJSON_WRITE_PRETTY_TWO_SPACES;
}

/**
 * @brief Adds a value to a yyjson mutable array, handling numbers specially.
 */
bool UniValue::addValueToYyjsonArray(yyjson_mut_doc* doc, yyjson_mut_val* parent, const UniValue& value) const {
    yyjson_mut_val* new_val = nullptr;
    
    switch (value.typ) {
        case VNULL: {
            new_val = yyjson_mut_null(doc);
            break;
        }
        case VBOOL: {
            bool bool_val = (value.val == "1");
            new_val = yyjson_mut_bool(doc, bool_val);
            break;
        }
        case VNUM: {
            // CRITICAL: For numbers, we use raw value to preserve exact string representation
            const std::string& num_str = value.getValStr();
            new_val = yyjson_mut_rawncpy(doc, num_str.data(), num_str.size());
            break;
        }
        case VSTR: {
            const std::string& str_val = value.getValStr();
            // Use json_escape() for compatibility with UniValue's escaping
            // Then create a raw yyjson value with the already-escaped string
            std::string escaped_str = '"' + json_escape(str_val) + '"';
            new_val = yyjson_mut_rawncpy(doc, escaped_str.data(), escaped_str.size());
            break;
        }
        case VOBJ: {
            // Recursively build object
            new_val = yyjson_mut_obj(doc);
            if (!new_val) return false;
            if (!value.buildYyjsonObjectFromUniValue(doc, new_val)) return false;
            break;
        }
        case VARR: {
            // Recursively build array
            new_val = yyjson_mut_arr(doc);
            if (!new_val) return false;
            if (!value.buildYyjsonArrayFromUniValue(doc, new_val)) return false;
            break;
        }
    }
    
    if (!new_val) return false;
    yyjson_mut_arr_append(parent, new_val);
    return true;
}

/**
 * @brief Adds a key-value pair to a yyjson mutable object, handling numbers specially.
 */
bool UniValue::addKeyValueToYyjsonObject(yyjson_mut_doc* doc, yyjson_mut_val* parent, 
                                          const std::string& key, const UniValue& value) const {
    yyjson_mut_val* new_val = nullptr;
    
    switch (value.typ) {
        case VNULL: {
            new_val = yyjson_mut_null(doc);
            break;
        }
        case VBOOL: {
            bool bool_val = (value.val == "1");
            new_val = yyjson_mut_bool(doc, bool_val);
            break;
        }
        case VNUM: {
            // CRITICAL: For numbers, we use raw value to preserve exact string representation
            const std::string& num_str = value.getValStr();
            new_val = yyjson_mut_rawncpy(doc, num_str.data(), num_str.size());
            break;
        }
        case VSTR: {
            const std::string& str_val = value.getValStr();
            // Use json_escape() for compatibility with UniValue's escaping
            // Then create a raw yyjson value with the already-escaped string
            std::string escaped_str = '"' + json_escape(str_val) + '"';
            new_val = yyjson_mut_rawncpy(doc, escaped_str.data(), escaped_str.size());
            break;
        }
        case VOBJ: {
            // Recursively build object
            new_val = yyjson_mut_obj(doc);
            if (!new_val) return false;
            if (!value.buildYyjsonObjectFromUniValue(doc, new_val)) return false;
            break;
        }
        case VARR: {
            // Recursively build array
            new_val = yyjson_mut_arr(doc);
            if (!new_val) return false;
            if (!value.buildYyjsonArrayFromUniValue(doc, new_val)) return false;
            break;
        }
    }
    
    if (!new_val) return false;
    
    // Add the key-value pair to the object
    yyjson_mut_val* new_key = yyjson_mut_strcpy(doc, key.c_str());
    if (!new_key) {
        return false;
    }
    
    yyjson_mut_obj_add(parent, new_key, new_val);
    return true;
}

/**
 * @brief Recursively builds a yyjson array from a UniValue array.
 */
bool UniValue::buildYyjsonArrayFromUniValue(yyjson_mut_doc* doc, yyjson_mut_val* parent) const {
    // Materialize the array if it's lazy-loaded
    size_t num_values = size();
    
    for (size_t i = 0; i < num_values; i++) {
        if (!addValueToYyjsonArray(doc, parent, values[i])) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Recursively builds a yyjson object from a UniValue object.
 */
bool UniValue::buildYyjsonObjectFromUniValue(yyjson_mut_doc* doc, yyjson_mut_val* parent) const {
    // Materialize the object if it's lazy-loaded
    size_t num_values = size();
    
    for (size_t i = 0; i < num_values; i++) {
        if (!addKeyValueToYyjsonObject(doc, parent, keys[i], values[i])) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Builds a yyjson document from a UniValue, handling numbers specially.
 */
bool UniValue::buildYyjsonFromUniValue(yyjson_mut_doc* doc, yyjson_mut_val*& root) const {
    root = nullptr;
    
    switch (typ) {
        case VNULL: {
            root = yyjson_mut_null(doc);
            break;
        }
        case VBOOL: {
            bool bool_val = (val == "1");
            root = yyjson_mut_bool(doc, bool_val);
            break;
        }
        case VNUM: {
            // CRITICAL: For numbers, we use raw value to preserve exact string representation
            const std::string& num_str = getValStr();
            root = yyjson_mut_rawncpy(doc, num_str.data(), num_str.size());
            break;
        }
        case VSTR: {
            const std::string& str_val = getValStr();
            root = yyjson_mut_strcpy(doc, str_val.c_str());
            break;
        }
        case VOBJ: {
            root = yyjson_mut_obj(doc);
            if (!root) return false;
            if (!buildYyjsonObjectFromUniValue(doc, root)) return false;
            break;
        }
        case VARR: {
            root = yyjson_mut_arr(doc);
            if (!root) return false;
            if (!buildYyjsonArrayFromUniValue(doc, root)) return false;
            break;
        }
    }
    
    return root != nullptr;
}

/**
 * @brief Fallback write implementation using original string-based approach.
 * Used when yyjson writer fails or for non-yyjson builds.
 * 
 * This version inlines the logic of writeObject/writeArray to avoid the overhead
 * of size() calls with materialization checks for non-lazy-loaded UniValue objects.
 */
std::string UniValue::fallbackWrite(unsigned int prettyIndent, unsigned int indentLevel) const
{
    std::string s;
    s.reserve(1024);

    unsigned int modIndent = indentLevel;
    if (modIndent == 0)
        modIndent = 1;

    switch (typ) {
    case VNULL:
        s += "null";
        break;
    case VOBJ: {
        // Inlined writeObject logic to avoid size() overhead
        s += "{";
        if (prettyIndent)
            s += "\n";

        for (unsigned int i = 0; i < values.size(); i++) {
            if (prettyIndent)
                indentStr(prettyIndent, modIndent, s);
            s += "\"" + json_escape(keys[i]) + "\":";
            if (prettyIndent)
                s += " ";
            s += values.at(i).write(prettyIndent, modIndent + 1);
            if (i != (values.size() - 1))
                s += ",";
            if (prettyIndent)
                s += "\n";
        }

        if (prettyIndent)
            indentStr(prettyIndent, modIndent - 1, s);
        s += "}";
        break;
    }
    case VARR: {
        // Inlined writeArray logic to avoid size() overhead
        s += "[";
        if (prettyIndent)
            s += "\n";

        for (unsigned int i = 0; i < values.size(); i++) {
            if (prettyIndent)
                indentStr(prettyIndent, modIndent, s);
            s += values[i].write(prettyIndent, modIndent + 1);
            if (i != (values.size() - 1)) {
                s += ",";
            }
            if (prettyIndent)
                s += "\n";
        }

        if (prettyIndent)
            indentStr(prettyIndent, modIndent - 1, s);
        s += "]";
        break;
    }
    case VSTR: {
        // Get the string value, materializing if necessary
        const std::string& str_val = getValStr();
        s += '"' + json_escape(str_val) + '"';
        break;
    }
    case VNUM: {
        // Materialize if necessary
        const std::string& num_val = getValStr();
        s += num_val;
        break;
    }
    case VBOOL:
        s += (val == "1" ? "true" : "false");
        break;
    }

    return s;
}

/**
 * @brief Serializes a UniValue to JSON using yyjson writer.
 */
std::string UniValue::writeYyjsonValue(unsigned int prettyIndent, unsigned int /* indentLevel */) const
{
    YyjsonDocWrapper doc(yyjson_mut_doc_new(nullptr));
    if (!doc.get()) {
        // Fallback to original implementation if yyjson doc creation fails
        return fallbackWrite(prettyIndent, 0);
    }

    // Build the yyjson document from this UniValue
    yyjson_mut_val* root = nullptr;
    if (!buildYyjsonFromUniValue(doc.get(), root)) {
        // Fallback to original implementation if building fails
        return fallbackWrite(prettyIndent, 0);
    }

    // Set the root of the document
    doc.get()->root = root;

    // Get the write flags
    yyjson_write_flag flags = getWriteFlags(prettyIndent);
    
    // Write the document to a string
    size_t len = 0;
    char* output = yyjson_mut_write_opts(doc.get(), flags, nullptr, &len, nullptr);
    if (!output) {
        // Fallback to original implementation if writing fails
        return fallbackWrite(prettyIndent, 0);
    }

    // Store the output so it gets freed by the wrapper
    doc.set_output(output);
    
    // Return the string
    return std::string(output, len);
}

/**
 * @brief Serializes the UniValue to a JSON string using hybrid yyjson approach.
 *
 * Hybrid approach:
 * - For VNUM: Returns val directly (bypass yyjson to preserve exact number formatting)
 * - For VSTR: Use json_escape() + raw yyjson value to maintain compatibility
 * - For VBOOL, VNULL, VOBJ, VARR: Use yyjson writer for consistency and performance
 *
 * // NOLINTNEXTLINE(misc-no-recursion)
 */
std::string UniValue::write(unsigned int prettyIndent,
                            unsigned int indentLevel) const
{
#ifdef UNIVALUE_USE_YYJSON
    // Hybrid approach: yyjson for containers/strings, raw for numbers
    if (m_yyjson_doc) {
        // Special case: If this is a number, return the raw val string
        // This preserves exact number formatting which yyjson might change
        if (typ == VNUM) {
            // CRITICAL: Preserve exact number string representation
            return val;
        }
        
        // For non-number types, use yyjson writer
        return writeYyjsonValue(prettyIndent, indentLevel);
    }
#endif
    // Fall back to original implementation for non-yyjson builds or when no yyjson_doc
    return fallbackWrite(prettyIndent, indentLevel);
}

/**
 * @brief Serializes an array to JSON format.
 *
 * // NOLINTNEXTLINE(misc-no-recursion)
 */
void UniValue::writeArray(unsigned int prettyIndent, unsigned int indentLevel, std::string& s) const
{
    s += "[";
    if (prettyIndent)
        s += "\n";

    // Use size() to trigger materialization for lazy-loaded containers
    size_t num_values = size();
    for (size_t i = 0; i < num_values; i++) {
        if (prettyIndent)
            indentStr(prettyIndent, indentLevel, s);
        s += values[i].write(prettyIndent, indentLevel + 1);
        if (i != (num_values - 1)) {
            s += ",";
        }
        if (prettyIndent)
            s += "\n";
    }

    if (prettyIndent)
        indentStr(prettyIndent, indentLevel - 1, s);
    s += "]";
}

/**
 * @brief Serializes an object to JSON format.
 *
 * // NOLINTNEXTLINE(misc-no-recursion)
 */
void UniValue::writeObject(unsigned int prettyIndent, unsigned int indentLevel, std::string& s) const
{
    s += "{";
    if (prettyIndent)
        s += "\n";

    // Use size() to trigger materialization for lazy-loaded containers
    size_t num_values = size();
    for (size_t i = 0; i < num_values; i++) {
        if (prettyIndent)
            indentStr(prettyIndent, indentLevel, s);
        s += '"' + json_escape(keys[i]) + "\":";
        if (prettyIndent)
            s += " ";
        s += values.at(i).write(prettyIndent, indentLevel + 1);
        if (i != (num_values - 1))
            s += ",";
        if (prettyIndent)
            s += "\n";
    }

    if (prettyIndent)
        indentStr(prettyIndent, indentLevel - 1, s);
    s += "}";
}