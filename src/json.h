#pragma once

#include <string>
#include <utility>
#include <vector>

// A small JSON value for the project file: build one and dump() it, or parse()
// text into one. Lookups that miss return a shared null, so reading a file
// from an older version just falls back to the defaults.
struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj };
    Type        type = Null;
    bool        b    = false;
    double      n    = 0.0;
    std::string s;
    std::vector<Json>                         arr;
    std::vector<std::pair<std::string, Json>> obj;

    Json() = default;
    Json(bool v) : type(Bool), b(v) {}
    Json(int v) : type(Num), n(v) {}
    Json(unsigned v) : type(Num), n(v) {}
    Json(double v) : type(Num), n(v) {}
    Json(float v) : type(Num), n(v) {}
    Json(const char* v) : type(Str), s(v) {}
    Json(std::string v) : type(Str), s(std::move(v)) {}
    static Json array() { Json j; j.type = Arr; return j; }
    static Json object() { Json j; j.type = Obj; return j; }

    // Building
    Json& set(const std::string& key, Json v); // adds or replaces; returns the stored value
    Json& push(Json v);                       // appends to an array; returns the stored value

    // Reading
    const Json& operator[](const std::string& key) const;
    const Json& operator[](size_t i) const;
    size_t      size() const { return type == Arr ? arr.size() : type == Obj ? obj.size() : 0; }
    bool        has(const std::string& key) const;
    double      num(double def = 0.0) const { return type == Num ? n : type == Bool ? (b ? 1.0 : 0.0) : def; }
    int         integer(int def = 0) const { return type == Num ? int(n) : def; }
    bool        boolean(bool def = false) const { return type == Bool ? b : type == Num ? n != 0.0 : def; }
    std::string str(const std::string& def = "") const { return type == Str ? s : def; }

    // Arrays whose items are all numbers stay on one line, so patterns stay compact.
    std::string dump(int indent = 0) const;
};

// Returns false (and a message in `error`) for text that is not JSON.
bool parseJson(const std::string& text, Json& out, std::string& error);
