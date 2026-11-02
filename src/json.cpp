#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

const Json kNull;

void escape(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

void number(std::string& out, double v) {
    if (!std::isfinite(v)) v = 0.0;
    char buf[32];
    if (v == std::floor(v) && std::fabs(v) < 1e15) std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
    else std::snprintf(buf, sizeof(buf), "%.6g", v);
    out += buf;
}

// Nesting depth of an array with no objects in it (-1 if it has any).
int arrayDepth(const Json& j) {
    if (j.type == Json::Obj) return -1;
    if (j.type != Json::Arr) return 0;
    int d = 0;
    for (const Json& v : j.arr) {
        int sub = arrayDepth(v);
        if (sub < 0) return -1;
        d = sub > d ? sub : d;
    }
    return d + 1;
}

// Short nested number arrays (one pattern step) stay on one line.
bool flat(const Json& j) {
    int d = arrayDepth(j);
    return d >= 0 && d <= 3;
}

void dumpTo(std::string& out, const Json& j, int indent, int depth) {
    const std::string pad(size_t(indent * (depth + 1)), ' '), padEnd(size_t(indent * depth), ' ');
    switch (j.type) {
    case Json::Null: out += "null"; break;
    case Json::Bool: out += j.b ? "true" : "false"; break;
    case Json::Num:  number(out, j.n); break;
    case Json::Str:  escape(out, j.s); break;
    case Json::Arr: {
        if (j.arr.empty()) { out += "[]"; break; }
        const bool oneLine = indent == 0 || flat(j);
        out += '[';
        for (size_t i = 0; i < j.arr.size(); ++i) {
            if (i) out += ',';
            if (!oneLine) out += "\n" + pad;
            dumpTo(out, j.arr[i], oneLine ? 0 : indent, depth + 1);
        }
        if (!oneLine) out += "\n" + padEnd;
        out += ']';
        break;
    }
    case Json::Obj: {
        if (j.obj.empty()) { out += "{}"; break; }
        out += '{';
        for (size_t i = 0; i < j.obj.size(); ++i) {
            if (i) out += ',';
            if (indent) out += "\n" + pad;
            escape(out, j.obj[i].first);
            out += indent ? ": " : ":";
            dumpTo(out, j.obj[i].second, indent, depth + 1);
        }
        if (indent) out += "\n" + padEnd;
        out += '}';
        break;
    }
    }
}

struct Parser {
    const std::string& t;
    size_t             i = 0;
    std::string        error;

    void ws() {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\n' || t[i] == '\r' || t[i] == '\t')) ++i;
    }
    bool fail(const char* what) {
        if (error.empty()) error = std::string(what) + " at byte " + std::to_string(i);
        return false;
    }
    bool literal(const char* word) {
        size_t n = 0;
        while (word[n]) ++n;
        if (t.compare(i, n, word) != 0) return fail("bad literal");
        i += n;
        return true;
    }
    bool str(std::string& out) {
        if (t[i] != '"') return fail("expected string");
        ++i;
        while (i < t.size() && t[i] != '"') {
            char c = t[i++];
            if (c != '\\') { out += c; continue; }
            if (i >= t.size()) return fail("bad escape");
            char e = t[i++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                if (i + 4 > t.size()) return fail("bad escape");
                unsigned cp = unsigned(std::strtoul(t.substr(i, 4).c_str(), nullptr, 16));
                i += 4;
                if (cp < 0x80) out += char(cp);
                else if (cp < 0x800) out += char(0xC0 | (cp >> 6)), out += char(0x80 | (cp & 0x3F));
                else out += char(0xE0 | (cp >> 12)), out += char(0x80 | ((cp >> 6) & 0x3F)), out += char(0x80 | (cp & 0x3F));
                break;
            }
            default: out += e; break;
            }
        }
        if (i >= t.size()) return fail("unterminated string");
        ++i;
        return true;
    }
    bool value(Json& out, int depth) {
        if (depth > 64) return fail("too deep");
        ws();
        if (i >= t.size()) return fail("unexpected end");
        char c = t[i];
        if (c == '{') {
            out = Json::object();
            ++i;
            ws();
            if (i < t.size() && t[i] == '}') { ++i; return true; }
            for (;;) {
                ws();
                std::string key;
                if (i >= t.size() || !str(key)) return fail("expected key");
                ws();
                if (i >= t.size() || t[i] != ':') return fail("expected ':'");
                ++i;
                Json v;
                if (!value(v, depth + 1)) return false;
                out.obj.emplace_back(std::move(key), std::move(v));
                ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == '}') { ++i; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            out = Json::array();
            ++i;
            ws();
            if (i < t.size() && t[i] == ']') { ++i; return true; }
            for (;;) {
                Json v;
                if (!value(v, depth + 1)) return false;
                out.arr.push_back(std::move(v));
                ws();
                if (i < t.size() && t[i] == ',') { ++i; continue; }
                if (i < t.size() && t[i] == ']') { ++i; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            out.type = Json::Str;
            return str(out.s);
        }
        if (c == 't') { out = Json(true); return literal("true"); }
        if (c == 'f') { out = Json(false); return literal("false"); }
        if (c == 'n') { out = Json(); return literal("null"); }
        const char* begin = t.c_str() + i;
        char*       end   = nullptr;
        double      v     = std::strtod(begin, &end);
        if (end == begin) return fail("unexpected character");
        i += size_t(end - begin);
        out = Json(v);
        return true;
    }
};

} // namespace

Json& Json::set(const std::string& key, Json v) {
    type = Obj;
    for (auto& kv : obj)
        if (kv.first == key) return kv.second = std::move(v);
    obj.emplace_back(key, std::move(v));
    return obj.back().second;
}

Json& Json::push(Json v) {
    type = Arr;
    arr.push_back(std::move(v));
    return arr.back();
}

const Json& Json::operator[](const std::string& key) const {
    if (type == Obj)
        for (const auto& kv : obj)
            if (kv.first == key) return kv.second;
    return kNull;
}

const Json& Json::operator[](size_t idx) const { return type == Arr && idx < arr.size() ? arr[idx] : kNull; }

bool Json::has(const std::string& key) const {
    if (type == Obj)
        for (const auto& kv : obj)
            if (kv.first == key) return true;
    return false;
}

std::string Json::dump(int indent) const {
    std::string out;
    dumpTo(out, *this, indent, 0);
    return out;
}

bool parseJson(const std::string& text, Json& out, std::string& error) {
    Parser p{text, 0, {}};
    if (!p.value(out, 0)) {
        error = p.error;
        return false;
    }
    p.ws();
    if (p.i != text.size()) {
        error = "trailing text at byte " + std::to_string(p.i);
        return false;
    }
    return true;
}
