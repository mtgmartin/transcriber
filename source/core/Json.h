#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace trs
{

// A small JSON value for the saved state. It has no JUCE dependency so that the unit tests can
// cover the whole save/load path. Doubles are written in the shortest form that reads back as
// exactly the same number, and object members keep the order they were added in.
class Json
{
public:
    enum class Type { null, boolean, integer, number, string, array, object };

    Json() = default;
    Json (bool v) : kind (Type::boolean), intValue (v ? 1 : 0) {}
    Json (int v) : kind (Type::integer), intValue (v) {}
    Json (int64_t v) : kind (Type::integer), intValue (v) {}
    Json (double v) : kind (Type::number), numberValue (v) {}
    Json (const char* v) : kind (Type::string), text (v) {}
    Json (std::string v) : kind (Type::string), text (std::move (v)) {}

    static Json array()  { Json j; j.kind = Type::array;  return j; }
    static Json object() { Json j; j.kind = Type::object; return j; }

    // An array of numbers kept as plain doubles (8 bytes each instead of a whole Json value), so
    // that the notes of a long recording stay small in memory. Arrays of numbers read from text
    // are stored this way too; use numberAt() to read any array of numbers. Integers beyond 2^53
    // are not exact in such an array.
    static Json numbers (std::vector<double> values);

    Type type() const noexcept { return kind; }
    bool isNull() const noexcept   { return kind == Type::null; }
    bool isNumber() const noexcept { return kind == Type::integer || kind == Type::number; }
    bool isString() const noexcept { return kind == Type::string; }
    bool isArray() const noexcept  { return kind == Type::array; }
    bool isObject() const noexcept { return kind == Type::object; }

    // Reading never fails: a value of the wrong type gives the fallback.
    bool asBool (bool fallback = false) const noexcept;
    int64_t asInt (int64_t fallback = 0) const noexcept;
    double asDouble (double fallback = 0.0) const noexcept;
    const std::string& asString() const noexcept;
    std::string asString (const std::string& fallback) const;

    // Arrays
    size_t size() const noexcept;
    const Json& at (size_t index) const noexcept;   // a null value past the end (and for packed number arrays)
    double numberAt (size_t index, double fallback = 0.0) const noexcept;
    Json& push (Json value);
    const std::vector<Json>& items() const noexcept { return values; }

    // Objects
    bool has (const std::string& key) const noexcept;
    const Json& get (const std::string& key) const noexcept;   // a null value if missing
    Json& set (const std::string& key, Json value);            // replaces an existing member
    const std::vector<std::string>& keys() const noexcept { return names; }

    // The compact text form.
    std::string dump() const;

    // false (with a message) for text that is not valid JSON or is nested too deeply.
    static bool parse (const std::string& text, Json& result, std::string* error = nullptr);

    bool operator== (const Json&) const;
    bool operator!= (const Json& other) const { return ! (*this == other); }

private:
    void write (std::string& out) const;

    Type kind = Type::null;
    int64_t intValue = 0;
    double numberValue = 0.0;
    std::string text;
    void unpack();

    std::vector<double> nums;          // a packed array of numbers
    bool packed = false;
    std::vector<Json> values;          // array items, or object member values
    std::vector<std::string> names;    // object member names
};

}  // namespace trs
