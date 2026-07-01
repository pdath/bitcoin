// Copyright 2024 The Bitcoin Core developers
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

// Forward declaration for recursive write
static std::string writeYyjsonValueInternal(const UniValue& uv, unsigned int prettyIndent, unsigned int indentLevel);

// Helper function to deep copy a yyjson value into a target document
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
            } else {
                // Fallback: convert to string and back
                // This shouldn't happen for properly constructed nodes
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
            if (!arr) return nullptr;
            yyjson_mut_val *item;
            // Use mutable iterator since we're copying from a mutable document
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
            yyjson_mut_val *key, *val;
            // Use mutable iterator since we're copying from a mutable document
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

// Implementations of inline methods for yyjson build
bool UniValue::isTrue() const {
    if (typ != VBOOL) return false;
    if (!m_materialized) {
        const_cast<UniValue*>(this)->materializeFromYyjson();
    }
    return val == "1";
}

bool UniValue::isFalse() const {
    if (typ != VBOOL) return false;
    if (!m_materialized) {
        const_cast<UniValue*>(this)->materializeFromYyjson();
    }
    return val != "1";
}

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

void UniValue::yyjson_doc_deleter(yyjson_mut_doc* doc) {
    yyjson_mut_doc_free(doc);
}

// Helper to set the root of a document
static void setYyjsonRoot(yyjson_mut_doc* doc, yyjson_val* node) {
    if (doc && node) {
        yyjson_mut_doc_set_root(doc, (yyjson_mut_val*)node);
    }
}

UniValue::UniValue() : typ(VNULL) {
    // Optimization: Primitives don't need their own yyjson documents
    m_yyjson_doc = nullptr;
    m_yyjson_node = nullptr;
    val.clear();
    m_materialized = true;  // Primitives are always materialized
}

UniValue::UniValue(UniValue::VType type, std::string str) : typ(type) {
    // Optimization: Primitives don't need their own yyjson documents
    // Only containers (VOBJ, VARR) need documents
    // For containers, create the document; for primitives, just store in val
    
    if (type == VOBJ || type == VARR) {
        m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
        
        switch (type) {
            case VOBJ:
                m_yyjson_node = (yyjson_val*)yyjson_mut_obj(m_yyjson_doc.get());
                // Don't populate val/keys/values for containers
                break;
            case VARR:
                m_yyjson_node = (yyjson_val*)yyjson_mut_arr(m_yyjson_doc.get());
                // Don't populate val/keys/values for containers
                break;
            default:
                // Should not happen
                m_yyjson_node = nullptr;
                break;
        }
        setYyjsonRoot(m_yyjson_doc.get(), m_yyjson_node);
        m_materialized = false;  // Containers are lazily materialized
    } else {
        // Primitive types: don't create document
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

UniValue::~UniValue() {}

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
        setYyjsonRoot(m_yyjson_doc.get(), m_yyjson_node);
    } else {
        // Other doesn't have a document (primitive without doc)
        m_yyjson_doc = nullptr;
        m_yyjson_node = nullptr;
    }
}

UniValue::UniValue(UniValue&& other) noexcept
    : typ(other.typ), val(std::move(other.val)), keys(std::move(other.keys)), values(std::move(other.values)),
      m_yyjson_doc(std::move(other.m_yyjson_doc)), 
      m_yyjson_node(other.m_yyjson_node),
      m_materialized(other.m_materialized)
{
    other.typ = VNULL;
    other.m_yyjson_node = nullptr;
    other.m_materialized = false;
}

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
            setYyjsonRoot(m_yyjson_doc.get(), m_yyjson_node);
        }
        // For primitives without documents, m_yyjson_doc and m_yyjson_node stay nullptr
        
        // Clear old container representation (will be lazily materialized if needed)
        keys.clear();
        values.clear();
    }
    return *this;
}

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

void UniValue::clear() {
    typ = VNULL;
    val.clear();
    keys.clear();
    values.clear();
    m_yyjson_doc.reset();
    m_yyjson_node = nullptr;
    m_materialized = false;
}

void UniValue::setNull() {
    clear();
    // Optimization: Primitives don't need their own yyjson documents
    // They will create nodes directly when pushed into containers
    m_yyjson_doc = nullptr;
    m_yyjson_node = nullptr;
    typ = VNULL;
    val.clear();
    m_materialized = true;  // Primitives are always materialized
}

void UniValue::setBool(bool val_) {
    clear();
    // Optimization: Primitives don't need their own yyjson documents
    // They will create nodes directly when pushed into containers
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

static bool json_isdigit(int ch) {
    return ((ch >= '0') && (ch <= '9'));
}

static bool validNumStr(const std::string& s) {
    if (s.empty()) return false;
    if (s.size() >= 1 && (json_isspace(s[0]) || json_isspace(s[s.size()-1]))) return false;
    if (s.size() != strlen(s.c_str())) return false; // No embedded NUL
    if (s.size() >= 2 && s[0] == '0' && s[1] == 'x') return false; // No hex
    
    const char *raw = s.data();
    const char *end = raw + s.size();
    
    // Must start with digit, minus, or plus (for scientific notation in some contexts)
    // But actually, JSON numbers can only start with -, digit, or .
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
    
    // Parse the number
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

void UniValue::setInt(uint64_t val_) {
    std::ostringstream oss;
    oss << val_;
    std::string str = oss.str();
    clear();
    // Optimization: Primitives don't need their own yyjson documents
    // They will create nodes directly when pushed into containers
    m_yyjson_doc = nullptr;
    m_yyjson_node = nullptr;
    typ = VNUM;
    val = str;  // Store number string for fast access
    m_materialized = true;  // Primitives are always materialized
}

void UniValue::setInt(int64_t val_) {
    std::ostringstream oss;
    oss << val_;
    std::string str = oss.str();
    clear();
    // Optimization: Primitives don't need their own yyjson documents
    // They will create nodes directly when pushed into containers
    m_yyjson_doc = nullptr;
    m_yyjson_node = nullptr;
    typ = VNUM;
    val = str;  // Store number string for fast access
    m_materialized = true;  // Primitives are always materialized
}

void UniValue::setFloat(double val_) {
    clear();
    std::ostringstream ss;
    ss << std::setprecision(15) << val_;
    std::string str = ss.str();
    // Optimization: Primitives don't need their own yyjson documents
    // They will create nodes directly when pushed into containers
    m_yyjson_doc = nullptr;
    m_yyjson_node = nullptr;
    typ = VNUM;
    val = str;  // Store number string for fast access
    m_materialized = true;  // Primitives are always materialized
}

void UniValue::setStr(std::string str) {
    clear();
    // Optimization: Primitives don't need their own yyjson documents
    // They will create nodes directly when pushed into containers
    m_yyjson_doc = nullptr;
    m_yyjson_node = nullptr;
    typ = VSTR;
    val = str;  // Store string for fast access
    m_materialized = true;  // Primitives are always materialized
}

void UniValue::setArray() {
    clear();
    m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
    m_yyjson_node = (yyjson_val*)yyjson_mut_arr(m_yyjson_doc.get());
    setYyjsonRoot(m_yyjson_doc.get(), m_yyjson_node);
    typ = VARR;
    // Don't populate val/keys/values for containers - use lazy materialization
    m_materialized = false;
}

void UniValue::setObject() {
    clear();
    m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
    m_yyjson_node = (yyjson_val*)yyjson_mut_obj(m_yyjson_doc.get());
    setYyjsonRoot(m_yyjson_doc.get(), m_yyjson_node);
    typ = VOBJ;
    // Don't populate val/keys/values for containers - use lazy materialization
    m_materialized = false;
}

void UniValue::checkType(const VType& expected) const {
    if (typ != expected) {
        throw type_error(std::string("UniValue type is not ") + uvTypeName(expected));
    }
}

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
            // Note: yyjson_get_bool works with both immutable and mutable values
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
                self->val = "0"; // Fallback
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
                self->val = ""; // Fallback
            }
            break;
        }
        case YYJSON_TYPE_ARR:
            self->typ = VARR;
            {
                // Clear old representation
                self->values.clear();
                
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

void UniValue::materializeFromYyjson() const {
    if (m_materialized) return;
    UniValue* self = const_cast<UniValue*>(this);
    if (!m_yyjson_doc || !m_yyjson_node) return;
    
    yyjson_type ytype = yyjson_get_type(m_yyjson_node);
    
    // Only materialize primitives, not containers
    if (ytype == YYJSON_TYPE_ARR || ytype == YYJSON_TYPE_OBJ) {
        self->materialize();
        return;
    }
    
    switch (ytype) {
        case YYJSON_TYPE_NULL:
            self->typ = VNULL;
            break;
        case YYJSON_TYPE_BOOL:
            self->typ = VBOOL;
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
                self->val = "0"; // Fallback
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
                self->val = ""; // Fallback
            }
            break;
        }
        default:
            break;
    }
    
    self->m_materialized = true;
}

void UniValue::materializeContainer() const {
    materialize();
}

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

const std::string& UniValue::getValStr() const {
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materializeFromYyjson();
    }
    return val;
}

bool UniValue::empty() const {
    if ((typ == VOBJ || typ == VARR) && m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    return values.empty();
}

size_t UniValue::size() const {
    if ((typ == VOBJ || typ == VARR) && m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    // For containers, return the materialized size
    // Note: After materialization, values.size() should match the yyjson container size
    return values.size();
}

void UniValue::reserve(size_t new_cap) {
    checkType(VARR);
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        materialize();
    }
    values.reserve(new_cap);
}

void UniValue::push_back(UniValue val) {
    checkType(VARR);
    m_materialized = false;
    
    // Add to yyjson array (primary storage)
    if (m_yyjson_doc && m_yyjson_node && yyjson_get_type(m_yyjson_node) == YYJSON_TYPE_ARR) {
        if (val.m_yyjson_doc && val.m_yyjson_node) {
            // val has its own yyjson tree - copy it
            yyjson_mut_val* new_val = copyYyjsonValue(val.m_yyjson_node, m_yyjson_doc.get());
            yyjson_mut_arr_append((yyjson_mut_val*)m_yyjson_node, new_val);
        } else {
            // Optimization: val is a primitive without its own document
            // Create yyjson node directly from val's value
            switch (val.typ) {
                case VNULL:
                    yyjson_mut_arr_append((yyjson_mut_val*)m_yyjson_node, yyjson_mut_null(m_yyjson_doc.get()));
                    break;
                case VBOOL:
                    yyjson_mut_arr_append((yyjson_mut_val*)m_yyjson_node, yyjson_mut_bool(m_yyjson_doc.get(), val.val == "1"));
                    break;
                case VNUM:
                    yyjson_mut_arr_append((yyjson_mut_val*)m_yyjson_node, (yyjson_mut_val*)yyjson_mut_rawncpy(m_yyjson_doc.get(), val.val.data(), val.val.size()));
                    break;
                case VSTR:
                    yyjson_mut_arr_append((yyjson_mut_val*)m_yyjson_node, (yyjson_mut_val*)yyjson_mut_strncpy(m_yyjson_doc.get(), val.val.data(), val.val.size()));
                    break;
                case VOBJ:
                case VARR:
                    // Containers should have their own documents - this is an error case
                    // For now, just skip it
                    break;
            }
        }
    }
    // Don't add to old representation - will be materialized on demand from yyjson tree
}

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
        yyjson_mut_val* new_key = (yyjson_mut_val*)yyjson_mut_strncpy(m_yyjson_doc.get(), key.data(), key.size());
        
        // Optimization: Handle primitives without documents directly
        if (val.m_yyjson_doc && val.m_yyjson_node) {
            // val has its own yyjson tree - copy it
            yyjson_mut_val* new_val = copyYyjsonValue(val.m_yyjson_node, m_yyjson_doc.get());
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
                    break;
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

void UniValue::pushKVEnd(std::string key, UniValue val) {
    pushKV(std::move(key), std::move(val));
}

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

// Recursive helper for writing UniValue using yyjson for containers but custom escaping for strings
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
            if (uv.empty()) return "[]";
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
            if (uv.empty()) return "{}";
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

std::string UniValue::writeYyjson(unsigned int prettyIndent) const {
    // For the yyjson-primary implementation, we need to use custom write
    // to preserve string escaping compatibility with original UniValue
    return writeYyjsonValueInternal(*this, prettyIndent, 0);
}

std::string UniValue::write(unsigned int prettyIndent, unsigned int /* indentLevel */) const {
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
    
    // Fast path for VSTR without yyjson document (manually constructed primitive)
    // Create a temporary document just for this write() call
    if (typ == VSTR && !m_yyjson_doc && !m_yyjson_node) {
        // Create a temporary document and node for this primitive
        // This is still faster than the old approach because we avoid the document-to-document copy
        // in push_back, and standalone primitives are rare in hot paths
        yyjson_mut_doc* temp_doc = yyjson_mut_doc_new(nullptr);
        yyjson_mut_val* temp_node = (yyjson_mut_val*)yyjson_mut_strncpy(temp_doc, val.data(), val.size());
        yyjson_mut_doc_set_root(temp_doc, temp_node);
        
        yyjson_write_flag flags = prettyIndent ? YYJSON_WRITE_PRETTY_TWO_SPACES : YYJSON_WRITE_NOFLAG;
        size_t len = 0;
        char* output = yyjson_mut_write_opts(temp_doc, flags, nullptr, &len, nullptr);
        std::string result(output, len);
        free(output);
        yyjson_mut_doc_free(temp_doc);
        
        // Post-process to match UniValue's escaping behavior for DEL (0x7f)
        // OPTIMIZATION: Early exit if no DEL characters present
        if (result.find(0x7f) == std::string::npos) {
            return result; // No processing needed
        }
        
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
    
    // For VSTR (with document), VOBJ, VARR: use yyjson_mut_write directly for maximum performance
    // yyjson properly handles JSON escaping for control characters 0x00-0x1f
    // but does NOT escape 0x7f (DEL) by default, which UniValue does.
    yyjson_write_flag flags = prettyIndent ? YYJSON_WRITE_PRETTY_TWO_SPACES : YYJSON_WRITE_NOFLAG;
    
    size_t len = 0;
    char* output = yyjson_mut_write_opts(m_yyjson_doc.get(), flags, nullptr, &len, nullptr);
    std::string result(output, len);
    free(output);
    
    // Post-process to match UniValue's escaping behavior:
    // 1. Replace raw DEL (0x7f) characters with \u007f
    // 2. Convert uppercase hex in escape sequences to lowercase
    // OPTIMIZATION: Early exit if no processing needed
    
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

const UniValue& UniValue::operator[](const std::string& key) const {
    if (typ != VOBJ)
        return NullUniValue;
    
    size_t idx;
    if (findKey(key, idx)) {
        return values[idx];
    }
    return NullUniValue;
}

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

const std::vector<std::string>& UniValue::getKeys() const {
    checkType(VOBJ);
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    return keys;
}

const std::vector<UniValue>& UniValue::getValues() const {
    if (typ != VOBJ && typ != VARR)
        throw std::runtime_error("JSON value is not an object or array as expected");
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    return values;
}

bool UniValue::get_bool() const {
    checkType(VBOOL);
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materializeFromYyjson();
    }
    return val == "1";
}

const std::string& UniValue::get_str() const {
    checkType(VSTR);
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materializeFromYyjson();
    }
    return val;
}

double UniValue::get_real() const {
    checkType(VNUM);
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materializeFromYyjson();
    }
    try {
        return std::stod(val);
    } catch (...) {
        throw std::runtime_error("JSON number out of range for double");
    }
}

const UniValue& UniValue::get_obj() const {
    checkType(VOBJ);
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    return *this;
}

const UniValue& UniValue::get_array() const {
    checkType(VARR);
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    return *this;
}

const UniValue& UniValue::find_value(std::string_view key) const {
    size_t idx;
    if (findKey(std::string(key), idx)) {
        return values[idx];
    }
    return NullUniValue;
}

void UniValue::getObjMap(std::map<std::string,UniValue>& kv) const {
    if (typ != VOBJ) return;
    
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    
    for (size_t i = 0; i < keys.size(); ++i) {
        kv[keys[i]] = values[i];
    }
}

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

void UniValue::push_backV(const std::vector<UniValue>& vec)
{
    checkType(VARR);
    for (const auto& v : vec) {
        push_back(v);
    }
}