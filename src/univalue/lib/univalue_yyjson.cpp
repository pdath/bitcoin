// Copyright 2026 The Bitcoin Knots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit-license.php.

#include <univalue.h>
#include <univalue_common.h>
#include <yyjson/yyjson.h>

#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

/**
 * @brief Forward declaration for recursive value serialization
 *
 * Handles primitive values (VSTR, VNUM, VBOOL, VNULL) that don't have their own
 * yyjson documents by creating temporary documents for serialization.
 *
 * @param uv The UniValue to serialize
 * @param prettyIndent Indentation level for pretty printing (0 for compact)
 * @param indentLevel Current nesting level for indentation
 * @return JSON string representation of the value
 */
static std::string writeYyjsonValueInternal(const UniValue& uv, unsigned int prettyIndent, unsigned int indentLevel);

/**
 * @brief Deep copy a yyjson value from source to target document
 *
 * Creates a copy of the yyjson value tree in the target document's memory pool.
 * This is necessary because yyjson values cannot be shared between documents.
 * Each UniValue that owns a yyjson tree must have its own document.
 *
 * Uses mutable iterators (yyjson_mut_arr_iter, yyjson_mut_obj_iter) for better
 * performance when copying from mutable documents (the common case in this implementation).
 *
 * @param src_val Source yyjson value to copy (can be from immutable or mutable document)
 * @param target_doc Target mutable document to copy into
 * @return New yyjson value in target document, or nullptr on failure
 */
static yyjson_mut_val* copyYyjsonValue(yyjson_val* src_val, yyjson_mut_doc* target_doc) {
    if (!src_val || !target_doc) return nullptr;
    
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
            }
            // Fallback for invalid nodes - should not occur in practice
            return yyjson_mut_null(target_doc);
        }
        case YYJSON_TYPE_STR: {
            const char* str = yyjson_get_str(src_val);
            size_t len = yyjson_get_len(src_val);
            return yyjson_mut_strncpy(target_doc, str, len);
        }
        case YYJSON_TYPE_ARR: {
            yyjson_mut_val* arr = yyjson_mut_arr(target_doc);
            if (!arr) return nullptr;
            
            // Use mutable iterator to traverse source array
            yyjson_mut_val *item;
            yyjson_mut_arr_iter miter;
            if (yyjson_mut_arr_iter_init((yyjson_mut_val*)src_val, &miter)) {
                while ((item = yyjson_mut_arr_iter_next(&miter))) {
                    yyjson_mut_val* copied = copyYyjsonValue((yyjson_val*)item, target_doc);
                    if (copied) {
                        yyjson_mut_arr_append(arr, copied);
                    }
                }
            }
            return arr;
        }
        case YYJSON_TYPE_OBJ: {
            yyjson_mut_val* obj = yyjson_mut_obj(target_doc);
            if (!obj) return nullptr;
            
            // Use mutable iterator to traverse source object
            yyjson_mut_val *key, *val;
            yyjson_mut_obj_iter miter;
            if (yyjson_mut_obj_iter_init((yyjson_mut_val*)src_val, &miter)) {
                while ((key = yyjson_mut_obj_iter_next(&miter))) {
                    val = yyjson_mut_obj_iter_get_val(key);
                    const char* kstr = yyjson_get_str((yyjson_val*)key);
                    size_t klen = yyjson_get_len((yyjson_val*)key);
                    if (kstr && klen > 0) {
                        yyjson_mut_val* new_key = yyjson_mut_strncpy(target_doc, kstr, klen);
                        yyjson_mut_val* new_val = copyYyjsonValue((yyjson_val*)val, target_doc);
                        if (new_key && new_val) {
                            yyjson_mut_obj_add(obj, new_key, new_val);
                        }
                    }
                }
            }
            return obj;
        }
        default:
            return nullptr;
    }
}

const UniValue NullUniValue;

/**
 * @brief Check if this UniValue represents true ("1")
 *
 * Triggers lazy materialization if the value hasn't been materialized yet.
 *
 * @return true if this is a boolean value equal to "1", false otherwise
 * @note In UniValue's encoding: "1" = true, "" (empty) = false
 */
bool UniValue::isTrue() const {
    if (typ != VBOOL) return false;
    if (!m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    return val == "1";
}

/**
 * @brief Check if this UniValue represents false ("")
 *
 * Triggers lazy materialization if the value hasn't been materialized yet.
 *
 * @return true if this is a boolean value not equal to "1", false otherwise
 * @note In UniValue's encoding: "1" = true, "" (empty) = false
 */
bool UniValue::isFalse() const {
    if (typ != VBOOL) return false;
    if (!m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    return val != "1";
}

/**
 * @brief Get type name as string
 *
 * @param t The VType to convert
 * @return String representation of the type ("null", "bool", "object", "array", "string", "number", or "unknown")
 */
const char *uvTypeName(UniValue::VType t)
{
    switch (t) {
    case UniValue::VNULL: return "null";
    case UniValue::VBOOL: return "bool";
    case UniValue::VOBJ: return "object";
    case UniValue::VARR: return "array";
    case UniValue::VSTR: return "string";
    case UniValue::VNUM: return "number";
    }
    return "unknown";
}

/**
 * @brief Custom deleter for yyjson mutable document shared_ptr
 *
 * Ensures proper cleanup of yyjson mutable documents when the shared_ptr goes out of scope.
 *
 * @param doc The document to free
 */
void UniValue::yyjson_doc_deleter(yyjson_mut_doc* doc) {
    yyjson_mut_doc_free(doc);
}

/**
 * @brief Set the root node of a yyjson mutable document
 *
 * Establishes ownership relationship between document and root node.
 * Required when creating new documents and assigning root nodes.
 *
 * @param doc The yyjson mutable document
 * @param node The root node to set
 */
static void setYyjsonRoot(yyjson_mut_doc* doc, yyjson_val* node) {
    if (doc && node) {
        yyjson_mut_doc_set_root(doc, (yyjson_mut_val*)node);
    }
}
/**
 * @brief Default constructor - creates a null UniValue
 *
 * Creates a primitive VNULL value without a yyjson document.
 * Primitives (VNULL, VBOOL, VSTR, VNUM) store values only in the `val` member
 * and do not create yyjson documents for efficiency.
 * Documents are only created when primitives are pushed into containers.
 */
UniValue::UniValue() : typ(VNULL) {
    m_yyjson_doc = nullptr;
    m_yyjson_node = nullptr;
    val.clear();
    m_materialized = true;  // Primitives are always materialized
}

/**
 * @brief Constructor with type and string value
 *
 * For containers (VOBJ, VARR): Creates a yyjson document and root node for tree building.
 * For primitives (VNULL, VSTR, VNUM, VBOOL): Stores value only in `val`, no document.
 *
 * This split approach optimizes memory usage:
 * - Containers need documents to build their yyjson tree structure
 * - Primitives don't need documents when they'll be pushed into containers
 *
 * @param type The value type (VOBJ, VARR, VNULL, VSTR, VNUM, or VBOOL)
 * @param str The string value (for VSTR, VNUM, VBOOL) or ignored for containers
 */
UniValue::UniValue(UniValue::VType type, std::string str) : typ(type) {
    // Containers need yyjson documents for tree building
    if (type == VOBJ || type == VARR) {
        m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
        
        switch (type) {
            case VOBJ:
                m_yyjson_node = (yyjson_val*)yyjson_mut_obj(m_yyjson_doc.get());
                break;
            case VARR:
                m_yyjson_node = (yyjson_val*)yyjson_mut_arr(m_yyjson_doc.get());
                break;
            default:
                // Should not happen
                m_yyjson_node = nullptr;
                break;
        }
        setYyjsonRoot(m_yyjson_doc.get(), m_yyjson_node);
        m_materialized = false;  // Containers are lazily materialized
    } else {
        // Primitive types: store in val only, no document needed
        m_yyjson_doc = nullptr;
        m_yyjson_node = nullptr;
        
        switch (type) {
            case VNULL:
                val.clear();
                break;
            case VSTR:
                val = str;  // Store string in val for fast access
                break;
            case VNUM:
                val = str;  // Store number string in val for fast access
                break;
            case VBOOL:
                val = str;  // Store "1" or "0" in val for fast access
                break;
            default:
                // Should not happen for primitives
                val.clear();
                break;
        }
        m_materialized = true;  // Primitives are always materialized
    }
}

/** @brief Destructor */
UniValue::~UniValue() {}

/**
 * @brief Copy constructor
 *
 * For primitives: Copies `val` directly (already populated, no document).
 * For containers: Deep copies the yyjson tree using copyYyjsonValue().
 * For values without documents: Copies val only, no yyjson state.
 *
 * @param other The UniValue to copy from
 */
UniValue::UniValue(const UniValue& other)
    : typ(other.typ)
{
    // For primitives, copy val directly (it's already populated)
    // For containers, don't copy val/keys/values - they'll be lazily materialized if needed
    if (other.typ != VARR && other.typ != VOBJ) {
        val = other.val;
        m_materialized = true;
    } else {
        m_materialized = false;
    }
    
    // Deep copy the yyjson tree (for containers and parsed primitives)
    // For manually constructed primitives, they don't have documents, so just set to null
    if (other.m_yyjson_doc && other.m_yyjson_node) {
        // Other has a document - deep copy it
        m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
        m_yyjson_node = (yyjson_val*)copyYyjsonValue(other.m_yyjson_node, m_yyjson_doc.get());
        if (!m_yyjson_node) {
            // Copy failed, clean up the document we just created
            m_yyjson_doc.reset();
        } else {
            setYyjsonRoot(m_yyjson_doc.get(), m_yyjson_node);
        }
    } else {
        // Other doesn't have a document (primitive without doc)
        m_yyjson_doc = nullptr;
        m_yyjson_node = nullptr;
    }
}

/**
 * @brief Move constructor
 *
 * Transfers ownership of yyjson state from other to this.
 * Other is left in a valid but unspecified state.
 *
 * @param other The UniValue to move from
 */
UniValue::UniValue(UniValue&& other) noexcept
    : typ(other.typ), val(std::move(other.val)), keys(std::move(other.keys)), values(std::move(other.values)),
      m_yyjson_doc(std::move(other.m_yyjson_doc)), 
      m_yyjson_node(other.m_yyjson_node),
      m_materialized(other.m_materialized)
{
    // Reset other to safe state
    other.typ = VNULL;
    other.m_yyjson_node = nullptr;
    other.m_materialized = false;
}

/**
 * @brief Copy assignment operator
 *
 * Similar to copy constructor: deep copy yyjson tree for containers,
 * copy val directly for primitives.
 *
 * @param other The UniValue to copy from
 * @return Reference to this
 */
UniValue& UniValue::operator=(const UniValue& other) {
    if (this != &other) {
        typ = other.typ;
        
        // For primitives, copy val directly (it's already populated)
        // For containers, don't copy val/keys/values - they'll be lazily materialized if needed
        if (other.typ != VARR && other.typ != VOBJ) {
            val = other.val;
            m_materialized = true;
        } else {
            m_materialized = false;
        }
        
        // Clear old yyjson state
        m_yyjson_doc.reset();
        m_yyjson_node = nullptr;
        
        // Deep copy the yyjson tree (for containers and parsed primitives)
        if (other.m_yyjson_doc && other.m_yyjson_node) {
            m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
            m_yyjson_node = (yyjson_val*)copyYyjsonValue(other.m_yyjson_node, m_yyjson_doc.get());
            if (!m_yyjson_node) {
                // Copy failed, clean up the document we just created
                m_yyjson_doc.reset();
            } else {
                setYyjsonRoot(m_yyjson_doc.get(), m_yyjson_node);
            }
        }
        // For primitives without documents, m_yyjson_doc and m_yyjson_node stay nullptr
        
        // Clear old container representation (will be lazily materialized if needed)
        keys.clear();
        values.clear();
    }
    return *this;
}

/**
 * @brief Move assignment operator
 *
 * Transfers ownership from other to this.
 * Other is left in a valid but unspecified state.
 *
 * @param other The UniValue to move from
 * @return Reference to this
 */
UniValue& UniValue::operator=(UniValue&& other) noexcept {
    if (this != &other) {
        typ = other.typ;
        val = std::move(other.val);
        keys = std::move(other.keys);
        values = std::move(other.values);
        m_yyjson_doc = std::move(other.m_yyjson_doc);
        m_yyjson_node = other.m_yyjson_node;  // Move node pointer (other's doc is now null after move)
        m_materialized = other.m_materialized;
        
        // Reset other to safe state
        other.typ = VNULL;
        other.m_yyjson_node = nullptr;
        other.m_materialized = false;
    }
    return *this;
}

/**
 * @brief Clear the UniValue, setting it to null
 *
 * Resets all state: type, val, keys, values, yyjson document and node.
 * The UniValue becomes a null value.
 */
void UniValue::clear() {
    typ = VNULL;
    val.clear();
    keys.clear();
    values.clear();
    m_yyjson_doc.reset();
    m_yyjson_node = nullptr;
    m_materialized = false;
}

/**
 * @brief Set this UniValue to null
 *
 * Optimization: Primitives don't need their own yyjson documents.
 * They will create nodes directly when pushed into containers.
 */
void UniValue::setNull() {
    clear();
    m_yyjson_doc = nullptr;
    m_yyjson_node = nullptr;
    typ = VNULL;
    val.clear();
    m_materialized = true;  // Primitives are always materialized
}

/**
 * @brief Set this UniValue to a boolean value
 *
 * Optimization: Primitives don't need their own yyjson documents.
 * They will create nodes directly when pushed into containers.
 *
 * @param val_ Boolean value (true becomes "1", false becomes "")
 */
void UniValue::setBool(bool val_) {
    clear();
    m_yyjson_doc = nullptr;
    m_yyjson_node = nullptr;
    typ = VBOOL;
    if (val_) {
        val = "1";
    } else {
        val.clear();  // Empty string for false
    }
    m_materialized = true;  // Primitives are always materialized
}

/**
 * @brief Check if character is a digit (0-9)
 *
 * @param ch Character to check (as int for compatibility with character classification)
 * @return true if ch is between '0' and '9' inclusive, false otherwise
 */
static bool json_isdigit(int ch) {
    return ((ch >= '0') && (ch <= '9'));
}

/**
 * @brief Validate a JSON number string according to UniValue's strict rules
 *
 * Ensures the string conforms to JSON number format while rejecting:
 * - Empty strings
 * - Strings with leading/trailing whitespace
 * - Embedded NUL characters
 * - Hex numbers (0x...)
 * - Leading zeros (except "0" itself)
 * - Invalid characters
 *
 * Supports:
 * - Optional minus sign
 * - Integer part (required)
 * - Optional decimal point and fractional part
 * - Optional exponent (e or E) with optional sign
 *
 * @param s The string to validate
 * @return true if valid JSON number, false otherwise
 */
static bool validNumStr(const std::string& s) {
    if (s.empty()) return false;
    if (s.size() >= 1 && (json_isspace(s[0]) || json_isspace(s[s.size()-1]))) return false;
    if (s.size() != strlen(s.c_str())) return false; // No embedded NUL
    if (s.size() >= 2 && s[0] == '0' && s[1] == 'x') return false; // No hex
    
    const char *raw = s.data();
    const char *end = raw + s.size();
    
    // Must start with digit, minus, or dot
    if (!json_isdigit(static_cast<unsigned char>(*raw)) && *raw != '-' && *raw != '.')
        return false;
    
    const char *firstDigit = raw;
    if (*firstDigit == '-') {
        firstDigit++;
        raw++; // Also advance raw past the sign
    }
    
    if (!json_isdigit(static_cast<unsigned char>(*firstDigit)))
        return false;
    
    // Check for leading zeros
    if (*firstDigit == '0' && firstDigit + 1 < end && json_isdigit(static_cast<unsigned char>(firstDigit[1]))) 
        return false;
    
    // Parse the integer part
    bool hasDigit = false;
    while (raw < end && json_isdigit(static_cast<unsigned char>(*raw))) {
        hasDigit = true;
        raw++;
    }
    
    // Fractional part
    if (raw < end && *raw == '.') {
        raw++;
        if (raw >= end || !json_isdigit(static_cast<unsigned char>(*raw))) 
            return false;
        while (raw < end && json_isdigit(static_cast<unsigned char>(*raw))) {
            hasDigit = true;
            raw++;
        }
    }
    
    // Exponent
    if (raw < end && (*raw == 'e' || *raw == 'E')) {
        raw++;
        if (raw < end && (*raw == '+' || *raw == '-'))
            raw++;
        if (raw >= end || !json_isdigit(static_cast<unsigned char>(*raw)))
            return false;
        while (raw < end && json_isdigit(static_cast<unsigned char>(*raw))) {
            hasDigit = true;
            raw++;
        }
    }
    
    return hasDigit && raw == end;
}

/**
 * @brief Set this UniValue to a number string
 *
 * Validates the string using validNumStr() before setting.
 * Creates a yyjson document and stores the number as raw text.
 *
 * @param str The number string to set
 * @throws std::runtime_error if the string is not a valid JSON number
 */
void UniValue::setNumStr(std::string str) {
    if (!validNumStr(str)) {
        throw std::runtime_error("The string '" + str + "' is not a valid JSON number");
    }
    
    clear();
    m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
    m_yyjson_node = (yyjson_val*)yyjson_mut_rawncpy(m_yyjson_doc.get(), str.data(), str.size());
    setYyjsonRoot(m_yyjson_doc.get(), m_yyjson_node);
    typ = VNUM;
    val = str;  // Store number string for fast access
    m_materialized = true;  // Primitives are always materialized
}

/**
 * @brief Set this UniValue to an unsigned 64-bit integer
 *
 * Converts the integer to a string and stores it.
 * Optimization: Primitives don't need their own yyjson documents.
 *
 * @param val_ The unsigned integer value
 */
void UniValue::setInt(uint64_t val_) {
    std::ostringstream oss;
    oss << val_;
    std::string str = oss.str();
    clear();
    m_yyjson_doc = nullptr;
    m_yyjson_node = nullptr;
    typ = VNUM;
    val = str;  // Store number string for fast access
    m_materialized = true;  // Primitives are always materialized
}

/**
 * @brief Set this UniValue to a signed 64-bit integer
 *
 * Converts the integer to a string and stores it.
 * Optimization: Primitives don't need their own yyjson documents.
 *
 * @param val_ The signed integer value
 */
void UniValue::setInt(int64_t val_) {
    std::ostringstream oss;
    oss << val_;
    std::string str = oss.str();
    clear();
    m_yyjson_doc = nullptr;
    m_yyjson_node = nullptr;
    typ = VNUM;
    val = str;  // Store number string for fast access
    m_materialized = true;  // Primitives are always materialized
}

/**
 * @brief Set this UniValue to a floating-point number
 *
 * Converts the double to a string with 15 digits of precision.
 * Optimization: Primitives don't need their own yyjson documents.
 *
 * @param val_ The floating-point value
 */
void UniValue::setFloat(double val_) {
    clear();
    std::ostringstream ss;
    ss << std::setprecision(15) << val_;
    std::string str = ss.str();
    m_yyjson_doc = nullptr;
    m_yyjson_node = nullptr;
    typ = VNUM;
    val = str;  // Store number string for fast access
    m_materialized = true;  // Primitives are always materialized
}

/**
 * @brief Set this UniValue to a string
 *
 * Optimization: Primitives don't need their own yyjson documents.
 * They will create nodes directly when pushed into containers.
 *
 * @param str The string value
 */
void UniValue::setStr(std::string str) {
    clear();
    m_yyjson_doc = nullptr;
    m_yyjson_node = nullptr;
    typ = VSTR;
    val = str;  // Store string for fast access
    m_materialized = true;  // Primitives are always materialized
}

/**
 * @brief Set this UniValue to an empty array
 *
 * Creates a yyjson document and an empty array node.
 */
void UniValue::setArray() {
    clear();
    m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
    m_yyjson_node = (yyjson_val*)yyjson_mut_arr(m_yyjson_doc.get());
    setYyjsonRoot(m_yyjson_doc.get(), m_yyjson_node);
    typ = VARR;
    // Don't populate val/keys/values for containers - use lazy materialization
    m_materialized = false;
}

/**
 * @brief Set this UniValue to an empty object
 *
 * Creates a yyjson document and an empty object node.
 */
void UniValue::setObject() {
    clear();
    m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
    m_yyjson_node = (yyjson_val*)yyjson_mut_obj(m_yyjson_doc.get());
    setYyjsonRoot(m_yyjson_doc.get(), m_yyjson_node);
    typ = VOBJ;
    // Don't populate val/keys/values for containers - use lazy materialization
    m_materialized = false;
}

/**
 * @brief Check if this UniValue is of the expected type
 *
 * @param expected The expected VType
 * @throws std::runtime_error if the type doesn't match
 */
void UniValue::checkType(const VType& expected) const {
    if (typ != expected) {
        throw type_error(std::string("UniValue type is not ") + uvTypeName(expected));
    }
}
/**
 * @brief Materialize the yyjson tree into the old representation (val/keys/values)
 *
 * Populates the `val`, `keys`, and `values` members from the yyjson tree.
 * This is called lazily when accessors need the old representation.
 *
 * For primitives: Extracts the value from the yyjson node into `val`
 * For arrays: Builds the `values` vector from the yyjson array
 * For objects: Builds both `keys` and `values` vectors from the yyjson object
 *
 * Once materialized, subsequent accesses use the cached representation.
 */
void UniValue::materialize() const {
    if (m_materialized) return;
    if (!m_yyjson_doc || !m_yyjson_node) return;
    
    UniValue* self = const_cast<UniValue*>(this);
    yyjson_type ytype = yyjson_get_type(m_yyjson_node);
    
    switch (ytype) {
        case YYJSON_TYPE_NULL:
            self->typ = VNULL;
            break;
        case YYJSON_TYPE_BOOL:
            self->typ = VBOOL;
            // yyjson_get_bool works with both immutable and mutable values
            if (yyjson_get_bool(m_yyjson_node)) {
                self->val = "1";
            } else {
                self->val.clear();  // Empty string for false
            }
            break;
        case YYJSON_TYPE_RAW:
        case YYJSON_TYPE_NUM: {
            const char* raw = yyjson_get_raw(m_yyjson_node);
            size_t len = yyjson_get_len(m_yyjson_node);
            self->typ = VNUM;
            if (raw && len > 0) {
                self->val.assign(raw, len);
            } else {
                self->val = "0"; // Fallback for invalid numbers
            }
            break;
        }
        case YYJSON_TYPE_STR: {
            const char* str = yyjson_get_str(m_yyjson_node);
            size_t len = yyjson_get_len(m_yyjson_node);
            self->typ = VSTR;
            if (str && len > 0) {
                self->val.assign(str, len);
            } else {
                self->val = ""; // Fallback for invalid strings
            }
            break;
        }
        case YYJSON_TYPE_ARR:
            self->typ = VARR;
            {
                // Clear old representation
                self->values.clear();
                
                // Optimization: Pre-allocate capacity to avoid reallocations
                size_t arr_size = yyjson_arr_size(self->m_yyjson_node);
                self->values.reserve(arr_size);
                
                size_t idx, max;
                yyjson_mut_val *item;
                // Use mutable foreach for mutable documents
                yyjson_mut_arr_foreach((yyjson_mut_val*)self->m_yyjson_node, idx, max, item) {
                    UniValue new_val;
                    new_val.clear();  // Clear to avoid memory leak from default constructor
                    new_val.m_yyjson_doc = self->m_yyjson_doc;
                    new_val.m_yyjson_node = (yyjson_val*)item;
                    new_val.materialize();
                    self->values.push_back(std::move(new_val));
                }
            }
            break;
        case YYJSON_TYPE_OBJ:
            self->typ = VOBJ;
            {
                // Clear old representation
                self->keys.clear();
                self->values.clear();
                
                // Optimization: Pre-allocate capacity to avoid reallocations
                size_t obj_size = yyjson_obj_size(self->m_yyjson_node);
                self->keys.reserve(obj_size);
                self->values.reserve(obj_size);
                
                // Use mutable iterator for mutable documents
                yyjson_mut_val *key, *v;
                yyjson_mut_obj_iter iter;
                if (yyjson_mut_obj_iter_init((yyjson_mut_val*)self->m_yyjson_node, &iter)) {
                    while ((key = yyjson_mut_obj_iter_next(&iter))) {
                        v = yyjson_mut_obj_iter_get_val(key);
                        const char* kstr = yyjson_get_str((yyjson_val*)key);
                        size_t klen = yyjson_get_len((yyjson_val*)key);
                        std::string k;
                        if (kstr && klen > 0) {
                            k.assign(kstr, klen);
                        }
                        UniValue new_val;
                        new_val.clear();  // Clear to avoid memory leak from default constructor
                        new_val.m_yyjson_doc = self->m_yyjson_doc;
                        new_val.m_yyjson_node = (yyjson_val*)v;
                        new_val.materialize();
                        self->keys.push_back(std::move(k));
                        self->values.push_back(std::move(new_val));
                    }
                }
            }
            break;
    }
    
    self->m_materialized = true;
}


/**
 * @brief Find a key in an object
 *
 * Searches for a key in the object's keys vector.
 * Triggers materialization if the object hasn't been materialized yet.
 *
 * @param key The key to find
 * @param retIdx Output parameter for the index if found
 * @return true if key was found, false otherwise
 */
bool UniValue::findKey(const std::string& key, size_t& retIdx) const {
    if (typ != VOBJ) return false;
    
    if (!m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i] == key) {
            retIdx = i;
            return true;
        }
    }
    return false;
}

/**
 * @brief Get the string representation of this value
 *
 * Triggers materialization if the value hasn't been materialized yet.
 * For primitives with yyjson documents, extracts the value from the tree.
 *
 * @return Reference to the val string
 */
const std::string& UniValue::getValStr() const {
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    return val;
}

/**
 * @brief Check if this container is empty
 *
 * Triggers materialization if the container hasn't been materialized yet.
 *
 * @return true if empty, false otherwise
 */
bool UniValue::empty() const {
    if ((typ == VOBJ || typ == VARR) && m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    return values.empty();
}

/**
 * @brief Get the size of this container
 *
 * Triggers materialization if the container hasn't been materialized yet.
 * After materialization, returns the cached size.
 *
 * @return Number of elements in the container
 */
size_t UniValue::size() const {
    if ((typ == VOBJ || typ == VARR) && m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    // For containers, return the materialized size
    // Note: After materialization, values.size() should match the yyjson container size
    return values.size();
}

/**
 * @brief Reserve capacity for an array
 *
 * Triggers materialization if the array hasn't been materialized yet.
 * Then reserves the requested capacity in the values vector.
 *
 * @param new_cap The new capacity to reserve
 */
void UniValue::reserve(size_t new_cap) {
    checkType(VARR);
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        materialize();
    }
    values.reserve(new_cap);
}

/**
 * @brief Append a value to an array
 *
 * Optimization: For primitives without their own documents, creates yyjson nodes
 * directly in the container's document, avoiding document-to-document copying.
 * For values with documents (containers, parsed primitives), copies the node.
 *
 * This is a key optimization that eliminates the overhead of creating temporary
 * documents for primitives when building arrays.
 *
 * @param val The value to append
 */
void UniValue::push_back(UniValue val) {
    checkType(VARR);
    
    // Add to yyjson array (primary storage)
    if (m_yyjson_doc && m_yyjson_node && yyjson_get_type(m_yyjson_node) == YYJSON_TYPE_ARR) {
        if (val.m_yyjson_doc && val.m_yyjson_node) {
            // val has its own yyjson tree - copy it
            yyjson_mut_val* new_val = copyYyjsonValue(val.m_yyjson_node, m_yyjson_doc.get());
            if (!new_val) {
                // Copy failed, cannot add to array
                return;
            }
            yyjson_mut_arr_append((yyjson_mut_val*)m_yyjson_node, new_val);
        } else {
            // Optimization: val is a primitive without its own document
            // Create yyjson node directly from val's value
            yyjson_mut_val* new_val = nullptr;
            switch (val.typ) {
                case VNULL:
                    new_val = yyjson_mut_null(m_yyjson_doc.get());
                    break;
                case VBOOL:
                    new_val = yyjson_mut_bool(m_yyjson_doc.get(), val.val == "1");
                    break;
                case VNUM:
                    new_val = (yyjson_mut_val*)yyjson_mut_rawncpy(m_yyjson_doc.get(), val.val.data(), val.val.size());
                    break;
                case VSTR:
                    new_val = (yyjson_mut_val*)yyjson_mut_strncpy(m_yyjson_doc.get(), val.val.data(), val.val.size());
                    break;
                case VOBJ:
                case VARR:
                    // Containers should have their own documents - this is an error case
                    // For now, just skip it
                    return;
            }
            if (!new_val) {
                // Node creation failed, cannot add to array
                return;
            }
            yyjson_mut_arr_append((yyjson_mut_val*)m_yyjson_node, new_val);
        }
        // Only mark as unmaterialized if we successfully added to the yyjson tree
        m_materialized = false;
    }
    // Don't add to old representation - will be materialized on demand from yyjson tree
}

/**
 * @brief Add a key-value pair to an object
 *
 * Optimization: For primitives without their own documents, creates yyjson nodes
 * directly in the container's document, avoiding document-to-document copying.
 * For values with documents (containers, parsed primitives), copies the node.
 *
 * If the key already exists, the old value is replaced.
 *
 * @param key The key to add/update
 * @param val The value to associate with the key
 */
void UniValue::pushKV(std::string key, UniValue val) {
    checkType(VOBJ);
    
    size_t idx;
    if (m_yyjson_doc && m_yyjson_node) {
        // Always update the yyjson tree (primary storage)
        // Check if key exists
        yyjson_mut_val* existing = yyjson_mut_obj_get((yyjson_mut_val*)m_yyjson_node, key.data());
        if (existing) {
            // Remove existing key-value pair
            yyjson_mut_obj_remove_str((yyjson_mut_val*)m_yyjson_node, key.data());
        }
        // Add new key-value pair
        // Create key first, then value - if either fails, we return without adding
        yyjson_mut_val* new_key = (yyjson_mut_val*)yyjson_mut_strncpy(m_yyjson_doc.get(), key.data(), key.size());
        if (!new_key) {
            // Key allocation failed, cannot add the pair
            return;
        }
        
        // Optimization: Handle primitives without documents directly
        if (val.m_yyjson_doc && val.m_yyjson_node) {
            // val has its own yyjson tree - copy it
            yyjson_mut_val* new_val = copyYyjsonValue(val.m_yyjson_node, m_yyjson_doc.get());
            if (!new_val) {
                // Copy failed, cannot add the pair
                return;
            }
            yyjson_mut_obj_add((yyjson_mut_val*)m_yyjson_node, new_key, new_val);
        } else {
            // val is a primitive without its own document
            // Create yyjson node directly from val's value
            switch (val.typ) {
                case VNULL:
                    yyjson_mut_obj_add((yyjson_mut_val*)m_yyjson_node, new_key, yyjson_mut_null(m_yyjson_doc.get()));
                    break;
                case VBOOL:
                    yyjson_mut_obj_add((yyjson_mut_val*)m_yyjson_node, new_key, yyjson_mut_bool(m_yyjson_doc.get(), val.val == "1"));
                    break;
                case VNUM:
                    yyjson_mut_obj_add((yyjson_mut_val*)m_yyjson_node, new_key, (yyjson_mut_val*)yyjson_mut_rawncpy(m_yyjson_doc.get(), val.val.data(), val.val.size()));
                    break;
                case VSTR:
                    yyjson_mut_obj_add((yyjson_mut_val*)m_yyjson_node, new_key, (yyjson_mut_val*)yyjson_mut_strncpy(m_yyjson_doc.get(), val.val.data(), val.val.size()));
                    break;
                case VOBJ:
                case VARR:
                    // Containers should have their own documents - this is an error case
                    // For now, just skip it
                    return;
            }
        }
    } else {
        // Fallback to old representation if this object doesn't have yyjson tree
        if (m_materialized && findKey(key, idx)) {
            values[idx] = std::move(val);
        } else {
            pushKVEnd(std::move(key), std::move(val));
        }
    }
    m_materialized = false;
}

/**
 * @brief Add a key-value pair to an object (end variant)
 *
 * Same as pushKV but takes ownership of the parameters.
 *
 * @param key The key to add/update
 * @param val The value to associate with the key
 */
void UniValue::pushKVEnd(std::string key, UniValue val) {
    pushKV(std::move(key), std::move(val));
}

/**
 * @brief Merge all key-value pairs from another object into this one
 *
 * If the other object has a yyjson tree, iterates directly over it without
 * materializing for maximum efficiency.
 *
 * @param obj The object to merge from (must be an object)
 */
void UniValue::pushKVs(UniValue obj) {
    checkType(VOBJ);
    obj.checkType(VOBJ);

    // If obj has a yyjson tree, iterate directly over it without materializing
    if (obj.m_yyjson_doc && obj.m_yyjson_node && !obj.m_materialized) {
        // Iterate over obj's yyjson tree directly
        yyjson_mut_val *key, *val;
        yyjson_mut_obj_iter iter;
        if (yyjson_mut_obj_iter_init((yyjson_mut_val*)obj.m_yyjson_node, &iter)) {
            while ((key = yyjson_mut_obj_iter_next(&iter))) {
                val = yyjson_mut_obj_iter_get_val(key);
                const char* kstr = yyjson_get_str((yyjson_val*)key);
                size_t klen = yyjson_get_len((yyjson_val*)key);
                std::string k(kstr, klen);
                
                // Create a new UniValue for the value
                UniValue v;
                v.m_yyjson_doc = obj.m_yyjson_doc;
                v.m_yyjson_node = (yyjson_val*)val;
                // Set typ based on yyjson type
                switch (yyjson_get_type((yyjson_val*)val)) {
                    case YYJSON_TYPE_NULL: v.typ = VNULL; break;
                    case YYJSON_TYPE_BOOL: v.typ = VBOOL; break;
                    case YYJSON_TYPE_NUM:
                    case YYJSON_TYPE_RAW: v.typ = VNUM; break;
                    case YYJSON_TYPE_STR: v.typ = VSTR; break;
                    case YYJSON_TYPE_ARR: v.typ = VARR; break;
                    case YYJSON_TYPE_OBJ: v.typ = VOBJ; break;
                    default: v.typ = VNULL; break;
                }
                v.m_materialized = false;
                
                // Add to target
                pushKV(k, v);
            }
        }
    } else {
        // Fallback: materialize and iterate over old representation
        if (!obj.m_materialized) {
            const_cast<UniValue&>(obj).materialize();
        }
        for (size_t i = 0; i < obj.keys.size(); i++)
            pushKV(obj.keys[i], obj.values[i]);
    }
}
/**
 * @brief Access an object value by key
 *
 * Searches for the key in the object and returns the corresponding value.
 * Returns NullUniValue if the key is not found or if this is not an object.
 *
 * @param key The key to look up
 * @return Reference to the value, or NullUniValue if not found
 */
const UniValue& UniValue::operator[](const std::string& key) const {
    if (typ != VOBJ)
        return NullUniValue;
    
    size_t idx;
    if (findKey(key, idx)) {
        return values[idx];
    }
    return NullUniValue;
}

/**
 * @brief Access an array or object value by index
 *
 * Returns the value at the specified index.
 * For arrays: Returns the element at the index.
 * For objects: Returns the value at the index (indexing by insertion order).
 * Returns NullUniValue if the index is out of bounds or if this is not a container.
 *
 * Triggers materialization if the container hasn't been materialized yet.
 *
 * @param index The index to access
 * @return Reference to the value, or NullUniValue if index is invalid
 */
const UniValue& UniValue::operator[](size_t index) const {
    if (typ != VOBJ && typ != VARR)
        return NullUniValue;
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    if (index < values.size()) {
        return values[index];
    }
    return NullUniValue;
}

/**
 * @brief Find a value in an object by key
 *
 * Searches for the key in the object and returns the corresponding value.
 * Returns NullUniValue if the key is not found.
 *
 * @param key The key to look up
 * @return Reference to the value, or NullUniValue if not found
 */
const UniValue& UniValue::find_value(std::string_view key) const {
    size_t idx;
    if (findKey(std::string(key), idx)) {
        return values[idx];
    }
    return NullUniValue;
}

/**
 * @brief Check if this object has the expected structure
 *
 * Verifies that the object contains all the keys specified in memberTypes
 * and that each key has the expected type.
 *
 * Triggers materialization if the object hasn't been materialized yet.
 *
 * @param memberTypes Map of key names to expected types
 * @return true if the object matches the expected structure, false otherwise
 */
bool UniValue::checkObject(const std::map<std::string,UniValue::VType>& memberTypes) const {
    if (typ != VOBJ) return false;
    
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    
    for (const auto& [key, expectedType] : memberTypes) {
        size_t idx;
        if (!findKey(key, idx)) {
            return false;
        }
        if (values[idx].typ != expectedType) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Append multiple values to an array
 *
 * Convenience method to append all values from a vector.
 *
 * @param vec The vector of values to append
 */
void UniValue::push_backV(const std::vector<UniValue>& vec)
{
    checkType(VARR);
    for (const auto& v : vec) {
        push_back(v);
    }
}

