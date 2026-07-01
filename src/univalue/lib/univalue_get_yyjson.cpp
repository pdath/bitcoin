// Copyright 2026 The Bitcoin Knots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

/**
 * @file univalue_get_yyjson.cpp
 * @brief yyjson-specific implementations of UniValue getter methods.
 *
 * This file contains the yyjson-based implementations of getter methods that
 * trigger lazy materialization when needed.
 */

#include <univalue.h>
#include <univalue_common.h>
#include <yyjson/yyjson.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * @brief Returns the keys of this object.
 *
 * For yyjson parser, this triggers materialization of lazy-loaded objects.
 *
 * @return Reference to the vector of object keys.
 * @throws std::runtime_error if the value is not an object.
 */
const std::vector<std::string>& UniValue::getKeys() const
{
    checkType(VOBJ);
    // Trigger materialization for lazy-loaded containers
    if (m_yyjson_doc && m_yyjson_node && keys.empty() && values.empty()) {
        const_cast<UniValue*>(this)->materializePrimitive();
    }
    return keys;
}

/**
 * @brief Returns the values of this object or array.
 *
 * For yyjson parser, this triggers materialization of lazy-loaded containers.
 *
 * @return Reference to the vector of values.
 * @throws std::runtime_error if the value is not an object or array.
 */
const std::vector<UniValue>& UniValue::getValues() const
{
    if (typ != VOBJ && typ != VARR)
        throw std::runtime_error("JSON value is not an object or array as expected");
    // Trigger materialization for lazy-loaded containers
    if (m_yyjson_doc && m_yyjson_node && keys.empty() && values.empty()) {
        const_cast<UniValue*>(this)->materializePrimitive();
    }
    return values;
}

/**
 * @brief Returns the boolean value.
 *
 * @return true if the value is true (stored as "1"), false if false (stored as "").
 * @throws std::runtime_error if the value is not a boolean.
 */
bool UniValue::get_bool() const
{
    checkType(VBOOL);
    return isTrue();
}

/**
 * @brief Returns the string value.
 *
 * For yyjson parser, this triggers materialization of lazy-loaded strings.
 *
 * @return Reference to the string value.
 * @throws std::runtime_error if the value is not a string.
 */
const std::string& UniValue::get_str() const
{
    checkType(VSTR);
    if (m_yyjson_doc && m_yyjson_node && val.empty()) {
        const_cast<UniValue*>(this)->materializePrimitive();
    }
    return getValStr();
}

/**
 * @brief Returns a zero-copy string view of the string value.
 *
 * This method provides a zero-copy view into yyjson's internal buffer when the
 * string has not been materialized yet. After materialization, it returns a view
 * of the materialized std::string.
 *
 * @return A string_view pointing to the string data.
 * @throws std::runtime_error if the value is not a string.
 * @note The returned string_view is valid as long as the UniValue object exists
 *       and the underlying yyjson document is not modified.
 */
std::string_view UniValue::get_str_view() const
{
    checkType(VSTR);
    if (!m_yyjson_doc || !m_yyjson_node) {
        // Already materialized to val, return string_view to it
        return std::string_view(val);
    }
    // Zero-copy: return string_view directly into yyjson's buffer
    // Note: m_yyjson_node for strings is always YYJSON_TYPE_STR
    const char* s = yyjson_get_str(m_yyjson_node);
    if (!s) {
        // Fallback to materialized value
        return std::string_view(val);
    }
    size_t len = yyjson_get_len(m_yyjson_node);
    return std::string_view(s, len);
}

/**
 * @brief Returns the floating-point value.
 *
 * For yyjson parser, this triggers materialization if the number was lazy-loaded
 * (though numbers are typically materialized immediately during parsing).
 *
 * @return The double-precision floating-point value.
 * @throws std::runtime_error if the value is not a number or out of range.
 */
double UniValue::get_real() const
{
    checkType(VNUM);
    // Ensure string is materialized
    if (m_yyjson_doc && m_yyjson_node && val.empty()) {
        const_cast<UniValue*>(this)->materializePrimitive();
    }
    
    double retval;
    if (!ParseDouble(getValStr(), &retval))
        throw std::runtime_error("JSON double out of range");
    return retval;
}

/**
 * @brief Returns a reference to this object.
 *
 * @return Reference to this UniValue as an object.
 * @throws std::runtime_error if the value is not an object.
 */
const UniValue& UniValue::get_obj() const
{
    checkType(VOBJ);
    return *this;
}

/**
 * @brief Returns a reference to this array.
 *
 * @return Reference to this UniValue as an array.
 * @throws std::runtime_error if the value is not an array.
 */
const UniValue& UniValue::get_array() const
{
    checkType(VARR);
    return *this;
}
