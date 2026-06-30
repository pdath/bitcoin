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

const UniValue NullUniValue;

// Implementations of inline methods for yyjson build
bool UniValue::isTrue() const {
    return (typ == VBOOL) && (val == "1");
}

bool UniValue::isFalse() const {
    return (typ == VBOOL) && (val != "1");
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

UniValue::UniValue() : typ(VNULL) {
    m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
    m_yyjson_node = (yyjson_val*)yyjson_mut_null(m_yyjson_doc.get());
    m_materialized = false;
}

UniValue::UniValue(UniValue::VType type, std::string str) : typ(type) {
    m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
    
    switch (type) {
        case VNULL:
            m_yyjson_node = (yyjson_val*)yyjson_mut_null(m_yyjson_doc.get());
            break;
        case VOBJ:
            m_yyjson_node = (yyjson_val*)yyjson_mut_obj(m_yyjson_doc.get());
            break;
        case VARR:
            m_yyjson_node = (yyjson_val*)yyjson_mut_arr(m_yyjson_doc.get());
            break;
        case VSTR:
            m_yyjson_node = (yyjson_val*)yyjson_mut_strcpy(m_yyjson_doc.get(), str.c_str());
            val = str;
            break;
        case VNUM:
            m_yyjson_node = (yyjson_val*)yyjson_mut_rawncpy(m_yyjson_doc.get(), str.data(), str.size());
            val = str;
            break;
        case VBOOL:
            m_yyjson_node = (yyjson_val*)yyjson_mut_bool(m_yyjson_doc.get(), str == "1");
            val = str;
            break;
    }
    m_materialized = !val.empty();
}

UniValue::~UniValue() {}

UniValue::UniValue(const UniValue& other)
    : typ(other.typ), val(other.val), keys(other.keys), values(other.values),
      m_yyjson_doc(other.m_yyjson_doc), m_yyjson_node(other.m_yyjson_node),
      m_materialized(other.m_materialized) {}

UniValue::UniValue(UniValue&& other) noexcept
    : typ(other.typ), val(std::move(other.val)), 
      keys(std::move(other.keys)), values(std::move(other.values)),
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
        val = other.val;
        keys = other.keys;
        values = other.values;
        m_yyjson_doc = other.m_yyjson_doc;
        m_yyjson_node = other.m_yyjson_node;
        m_materialized = other.m_materialized;
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
        m_yyjson_node = other.m_yyjson_node;
        m_materialized = other.m_materialized;
        
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
    m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
    m_yyjson_node = (yyjson_val*)yyjson_mut_null(m_yyjson_doc.get());
    typ = VNULL;
}

void UniValue::setBool(bool val_) {
    clear();
    val = val_ ? "1" : "0";
    m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
    m_yyjson_node = (yyjson_val*)yyjson_mut_bool(m_yyjson_doc.get(), val_);
    typ = VBOOL;
    m_materialized = true;
}

void UniValue::setNumStr(std::string str) {
    clear();
    val = str;
    m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
    m_yyjson_node = (yyjson_val*)yyjson_mut_rawncpy(m_yyjson_doc.get(), str.data(), str.size());
    typ = VNUM;
    m_materialized = true;
}

void UniValue::setInt(uint64_t val_) {
    clear();
    val = std::to_string(val_);
    m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
    m_yyjson_node = (yyjson_val*)yyjson_mut_uint(m_yyjson_doc.get(), val_);
    typ = VNUM;
    m_materialized = true;
}

void UniValue::setInt(int64_t val_) {
    clear();
    val = std::to_string(val_);
    m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
    m_yyjson_node = (yyjson_val*)yyjson_mut_int(m_yyjson_doc.get(), val_);
    typ = VNUM;
    m_materialized = true;
}

void UniValue::setFloat(double val_) {
    clear();
    std::ostringstream ss;
    ss << std::setprecision(15) << val_;
    val = ss.str();
    m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
    m_yyjson_node = (yyjson_val*)yyjson_mut_rawncpy(m_yyjson_doc.get(), val.data(), val.size());
    typ = VNUM;
    m_materialized = true;
}

void UniValue::setStr(std::string str) {
    clear();
    val = str;
    m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
    m_yyjson_node = (yyjson_val*)yyjson_mut_strcpy(m_yyjson_doc.get(), str.c_str());
    typ = VSTR;
    m_materialized = true;
}

void UniValue::setArray() {
    clear();
    m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
    m_yyjson_node = (yyjson_val*)yyjson_mut_arr(m_yyjson_doc.get());
    typ = VARR;
    m_materialized = false;
}

void UniValue::setObject() {
    clear();
    m_yyjson_doc = std::shared_ptr<yyjson_mut_doc>(yyjson_mut_doc_new(nullptr), yyjson_doc_deleter);
    m_yyjson_node = (yyjson_val*)yyjson_mut_obj(m_yyjson_doc.get());
    typ = VOBJ;
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
            self->val = yyjson_get_bool(m_yyjson_node) ? "1" : "0";
            break;
        case YYJSON_TYPE_RAW:
        case YYJSON_TYPE_NUM:
            self->typ = VNUM;
            self->val.assign(yyjson_get_raw(m_yyjson_node), yyjson_get_len(m_yyjson_node));
            break;
        case YYJSON_TYPE_STR:
            self->typ = VSTR;
            self->val.assign(yyjson_get_str(m_yyjson_node), yyjson_get_len(m_yyjson_node));
            break;
        case YYJSON_TYPE_ARR:
            self->typ = VARR;
            {
                size_t idx, max;
                yyjson_val *item;
                yyjson_arr_foreach(m_yyjson_node, idx, max, item) {
                    UniValue new_val;
                    new_val.m_yyjson_doc = m_yyjson_doc;
                    new_val.m_yyjson_node = item;
                    new_val.materialize();
                    self->values.push_back(std::move(new_val));
                }
            }
            break;
        case YYJSON_TYPE_OBJ:
            self->typ = VOBJ;
            {
                yyjson_val *key, *v;
                yyjson_obj_iter iter;
                yyjson_obj_iter_init(m_yyjson_node, &iter);
                while ((key = yyjson_obj_iter_next(&iter))) {
                    v = yyjson_obj_iter_get_val(key);
                    std::string k(yyjson_get_str(key), yyjson_get_len(key));
                    UniValue new_val;
                    new_val.m_yyjson_doc = m_yyjson_doc;
                    new_val.m_yyjson_node = v;
                    new_val.materialize();
                    self->keys.push_back(std::move(k));
                    self->values.push_back(std::move(new_val));
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
            self->val = yyjson_get_bool(m_yyjson_node) ? "1" : "0";
            break;
        case YYJSON_TYPE_RAW:
        case YYJSON_TYPE_NUM:
            self->typ = VNUM;
            self->val.assign(yyjson_get_raw(m_yyjson_node), yyjson_get_len(m_yyjson_node));
            break;
        case YYJSON_TYPE_STR:
            self->typ = VSTR;
            self->val.assign(yyjson_get_str(m_yyjson_node), yyjson_get_len(m_yyjson_node));
            break;
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
    if (m_yyjson_doc && m_yyjson_node && val.empty() && !m_materialized) {
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
    values.push_back(std::move(val));
}

void UniValue::pushKV(std::string key, UniValue val) {
    checkType(VOBJ);
    m_materialized = false;
    keys.push_back(std::move(key));
    values.push_back(std::move(val));
}

void UniValue::pushKVEnd(std::string key, UniValue val) {
    pushKV(std::move(key), std::move(val));
}

void UniValue::pushKVs(UniValue obj) {
    checkType(VOBJ);
    if (!obj.isObject()) return;
    
    if (obj.m_yyjson_doc && obj.m_yyjson_node && !obj.m_materialized) {
        const_cast<UniValue&>(obj).materialize();
    }
    
    for (size_t i = 0; i < obj.keys.size(); ++i) {
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
    return writeYyjson(prettyIndent);
}

const UniValue& UniValue::operator[](const std::string& key) const {
    size_t idx;
    if (findKey(key, idx)) {
        return values[idx];
    }
    throw type_error(std::string("UniValue key not found: ") + key);
}

const UniValue& UniValue::operator[](size_t index) const {
    checkType(VARR);
    if (m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    if (index < values.size()) {
        return values[index];
    }
    throw type_error("UniValue array index out of range");
}

const std::vector<std::string>& UniValue::getKeys() const {
    if (typ == VOBJ && m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    return keys;
}

const std::vector<UniValue>& UniValue::getValues() const {
    if ((typ == VOBJ || typ == VARR) && m_yyjson_doc && m_yyjson_node && !m_materialized) {
        const_cast<UniValue*>(this)->materialize();
    }
    return values;
}

bool UniValue::get_bool() const {
    checkType(VBOOL);
    if (m_yyjson_doc && m_yyjson_node && val.empty() && !m_materialized) {
        const_cast<UniValue*>(this)->materializeFromYyjson();
    }
    return val == "1";
}

const std::string& UniValue::get_str() const {
    checkType(VSTR);
    if (m_yyjson_doc && m_yyjson_node && val.empty() && !m_materialized) {
        const_cast<UniValue*>(this)->materializeFromYyjson();
    }
    return val;
}

double UniValue::get_real() const {
    checkType(VNUM);
    if (m_yyjson_doc && m_yyjson_node && val.empty() && !m_materialized) {
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