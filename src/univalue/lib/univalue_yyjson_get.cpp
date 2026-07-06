// Copyright 2026 The Bitcoin Knots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#include <univalue.h>
#include <yyjson/yyjson.h>

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * @brief Get the keys of this object
 *
 * Triggers materialization if the object hasn't been materialized yet.
 * Note: Uses const_cast to allow materialization in const context.
 * This is safe because materialization only populates the keys/values cache
 * and doesn't change the logical state of the object.
 *
 * @return Reference to the vector of object keys
 * @throws std::runtime_error if this is not an object
 */
const std::vector<std::string>& UniValue::getKeys() const {
    checkType(VOBJ);
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        // Materialize on-demand. This is safe because:
        // 1. We only populate the cache (keys/values) which is logically equivalent to the yyjson tree
        // 2. The original object was not declared as const (it was passed as const reference)
        // 3. Materialization is idempotent - calling it multiple times has the same result
        const_cast<UniValue*>(this)->materialize();
    }

    return keys;
}

/**
 * @brief Get the values of this object or array
 *
 * Triggers materialization if the container hasn't been materialized yet.
 * Note: Uses const_cast to allow materialization in const context.
 * This is safe because materialization only populates the keys/values cache
 * and doesn't change the logical state of the object.
 *
 * @return Reference to the vector of values
 * @throws std::runtime_error if this is not an object or array
 */
const std::vector<UniValue>& UniValue::getValues() const {
    if (typ != VOBJ && typ != VARR)
        throw std::runtime_error("JSON value is not an object or array as expected");
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        // Materialize on-demand. This is safe because:
        // 1. We only populate the cache (keys/values) which is logically equivalent to the yyjson tree
        // 2. The original object was not declared as const (it was passed as const reference)
        // 3. Materialization is idempotent - calling it multiple times has the same result
        const_cast<UniValue*>(this)->materialize();
    }
    return values;
}

/**
 * @brief Get the boolean value
 *
 * Triggers materialization if the value hasn't been materialized yet.
 *
 * @return true if the value is "1", false if "" (empty string)
 * @throws std::runtime_error if this is not a boolean
 */
bool UniValue::get_bool() const {
    checkType(VBOOL);
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    return val == "1";
}

/**
 * @brief Get the string value
 *
 * Triggers materialization if the value hasn't been materialized yet.
 *
 * @return Reference to the string value
 * @throws std::runtime_error if this is not a string
 */
const std::string& UniValue::get_str() const {
    checkType(VSTR);
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    return val;
}

/**
 * @brief Get the floating-point value
 *
 * Triggers materialization if the value hasn't been materialized yet.
 * Parses the string representation using std::stod.
 *
 * @return The double-precision floating-point value
 * @throws std::runtime_error if this is not a number or out of range
 */
double UniValue::get_real() const {
    checkType(VNUM);
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    try {
        return std::stod(val);
    } catch (...) {
        throw std::runtime_error("JSON number out of range for double");
    }
}

/**
 * @brief Get a reference to this UniValue as an object
 *
 * Triggers materialization if the object hasn't been materialized yet.
 *
 * @return Reference to this UniValue
 * @throws std::runtime_error if this is not an object
 */
const UniValue& UniValue::get_obj() const {
    checkType(VOBJ);
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    return *this;
}

/**
 * @brief Get a reference to this UniValue as an array
 *
 * Triggers materialization if the array hasn't been materialized yet.
 *
 * @return Reference to this UniValue
 * @throws std::runtime_error if this is not an array
 */
const UniValue& UniValue::get_array() const {
    checkType(VARR);
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    return *this;
}

/**
 * @brief Populate a map with all key-value pairs from this object
 *
 * Triggers materialization if the object hasn't been materialized yet.
 *
 * @param kv Output map to populate
 */
void UniValue::getObjMap(std::map<std::string,UniValue>& kv) const {
    if (typ != VOBJ) return;
    
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    
    for (size_t i = 0; i < keys.size(); ++i) {
        kv[keys[i]] = values[i];
    }
}
