#include "Json.h"

#include <charconv>
#include <cmath>

namespace trs
{

namespace
{
    const Json nullValue;
    const std::string emptyString;
    constexpr int maxDepth = 200;
}

bool Json::asBool (bool fallback) const noexcept
{
    if (kind == Type::boolean || kind == Type::integer)
        return intValue != 0;

    return fallback;
}

int64_t Json::asInt (int64_t fallback) const noexcept
{
    if (kind == Type::integer || kind == Type::boolean) return intValue;
    if (kind == Type::number && std::isfinite (numberValue)) return (int64_t) std::llround (numberValue);
    return fallback;
}

double Json::asDouble (double fallback) const noexcept
{
    if (kind == Type::number) return numberValue;
    if (kind == Type::integer) return (double) intValue;
    return fallback;
}

const std::string& Json::asString() const noexcept
{
    return kind == Type::string ? text : emptyString;
}

std::string Json::asString (const std::string& fallback) const
{
    return kind == Type::string ? text : fallback;
}

size_t Json::size() const noexcept
{
    if (kind == Type::array && packed)
        return nums.size();

    return kind == Type::array || kind == Type::object ? values.size() : 0;
}

Json Json::numbers (std::vector<double> v)
{
    Json j = array();
    j.nums = std::move (v);
    j.packed = ! j.nums.empty();
    return j;
}

void Json::unpack()
{
    if (! packed)
        return;

    values.reserve (nums.size());

    for (const auto d : nums)
        values.emplace_back (d);

    nums.clear();
    packed = false;
}

double Json::numberAt (size_t index, double fallback) const noexcept
{
    if (kind != Type::array)
        return fallback;

    if (packed)
        return index < nums.size() ? nums[index] : fallback;

    return index < values.size() ? values[index].asDouble (fallback) : fallback;
}

const Json& Json::at (size_t index) const noexcept
{
    return kind == Type::array && index < values.size() ? values[index] : nullValue;
}

Json& Json::push (Json value)
{
    if (kind != Type::array)
        *this = array();

    unpack();
    values.push_back (std::move (value));
    return values.back();
}

bool Json::has (const std::string& key) const noexcept
{
    if (kind != Type::object)
        return false;

    for (const auto& n : names)
        if (n == key)
            return true;

    return false;
}

const Json& Json::get (const std::string& key) const noexcept
{
    if (kind == Type::object)
        for (size_t i = 0; i < names.size(); ++i)
            if (names[i] == key)
                return values[i];

    return nullValue;
}

Json& Json::set (const std::string& key, Json value)
{
    if (kind != Type::object)
        *this = object();

    for (size_t i = 0; i < names.size(); ++i)
    {
        if (names[i] == key)
        {
            values[i] = std::move (value);
            return values[i];
        }
    }

    names.push_back (key);
    values.push_back (std::move (value));
    return values.back();
}

bool Json::operator== (const Json& o) const
{
    if (isNumber() && o.isNumber())
        return kind == o.kind ? (kind == Type::integer ? intValue == o.intValue : numberValue == o.numberValue)
                              : asDouble() == o.asDouble();

    if (kind != o.kind)
        return false;

    switch (kind)
    {
        case Type::null:    return true;
        case Type::boolean: return intValue == o.intValue;
        case Type::string:  return text == o.text;
        case Type::array:
        {
            if (size() != o.size())
                return false;

            if (! packed && ! o.packed)
                return values == o.values;

            for (size_t i = 0; i < size(); ++i)
            {
                const auto a = packed ? Json (nums[i]) : values[i];
                const auto b = o.packed ? Json (o.nums[i]) : o.values[i];

                if (a != b)
                    return false;
            }

            return true;
        }

        case Type::object:  return names == o.names && values == o.values;
        case Type::integer:
        case Type::number:  break;
    }

    return false;
}

//==============================================================================
// Writing

namespace
{
    void writeString (std::string& out, const std::string& s)
    {
        out += '"';

        for (const auto c : s)
        {
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if ((unsigned char) c < 0x20)
                    {
                        static const char* hex = "0123456789abcdef";
                        out += "\\u00";
                        out += hex[((unsigned char) c >> 4) & 15];
                        out += hex[(unsigned char) c & 15];
                    }
                    else
                    {
                        out += c;
                    }
            }
        }

        out += '"';
    }
}

void Json::write (std::string& out) const
{
    switch (kind)
    {
        case Type::null:    out += "null"; break;
        case Type::boolean: out += intValue != 0 ? "true" : "false"; break;
        case Type::integer: out += std::to_string (intValue); break;

        case Type::number:
        {
            if (! std::isfinite (numberValue))
            {
                out += "null";
                break;
            }

            char buffer[40];
            const auto result = std::to_chars (buffer, buffer + sizeof buffer, numberValue);
            out.append (buffer, (size_t) (result.ptr - buffer));
            break;
        }

        case Type::string:  writeString (out, text); break;

        case Type::array:
            out += '[';

            if (packed)
            {
                for (size_t i = 0; i < nums.size(); ++i)
                {
                    if (i > 0)
                        out += ',';

                    Json (nums[i]).write (out);
                }

                out += ']';
                break;
            }

            for (size_t i = 0; i < values.size(); ++i)
            {
                if (i > 0)
                    out += ',';

                values[i].write (out);
            }

            out += ']';
            break;

        case Type::object:
            out += '{';

            for (size_t i = 0; i < values.size(); ++i)
            {
                if (i > 0)
                    out += ',';

                writeString (out, names[i]);
                out += ':';
                values[i].write (out);
            }

            out += '}';
            break;
    }
}

std::string Json::dump() const
{
    std::string out;
    write (out);
    return out;
}

//==============================================================================
// Parsing

namespace
{
    class Parser
    {
    public:
        explicit Parser (const std::string& t) : s (t) {}

        bool parseDocument (Json& result, std::string* error)
        {
            skip();

            if (! value (result, 0))
                return fail (error);

            skip();

            if (pos != s.size())
            {
                message = "unexpected text after the value";
                return fail (error);
            }

            return true;
        }

    private:
        bool fail (std::string* error)
        {
            if (error != nullptr)
                *error = message + " (at character " + std::to_string (pos) + ")";

            return false;
        }

        bool bad (const char* why)
        {
            if (message.empty())
                message = why;

            return false;
        }

        void skip()
        {
            while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\n' || s[pos] == '\r' || s[pos] == '\t'))
                ++pos;
        }

        bool literal (const char* word)
        {
            size_t i = 0;

            for (; word[i] != 0; ++i)
                if (pos + i >= s.size() || s[pos + i] != word[i])
                    return false;

            pos += i;
            return true;
        }

        bool value (Json& out, int depth)
        {
            if (depth > maxDepth)
                return bad ("nested too deeply");

            if (pos >= s.size())
                return bad ("unexpected end");

            const auto c = s[pos];

            if (c == '{') return objectValue (out, depth);
            if (c == '[') return arrayValue (out, depth);

            if (c == '"')
            {
                std::string str;

                if (! stringValue (str))
                    return false;

                out = Json (std::move (str));
                return true;
            }

            if (literal ("true"))  { out = Json (true);  return true; }
            if (literal ("false")) { out = Json (false); return true; }
            if (literal ("null"))  { out = Json();       return true; }

            return numberValue (out);
        }

        bool numberValue (Json& out)
        {
            const auto start = pos;
            bool isFloat = false;

            if (pos < s.size() && s[pos] == '-')
                ++pos;

            while (pos < s.size())
            {
                const auto c = s[pos];

                if (c >= '0' && c <= '9')
                {
                    ++pos;
                }
                else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
                {
                    isFloat = true;
                    ++pos;
                }
                else
                {
                    break;
                }
            }

            if (pos == start || (pos == start + 1 && s[start] == '-'))
                return bad ("a value was expected");

            const char* first = s.data() + start;
            const char* last = s.data() + pos;

            if (! isFloat)
            {
                int64_t v = 0;
                const auto r = std::from_chars (first, last, v);

                if (r.ec == std::errc() && r.ptr == last)
                {
                    out = Json (v);
                    return true;
                }
            }

            double d = 0.0;
            const auto r = std::from_chars (first, last, d);

            if (r.ec != std::errc() || r.ptr != last)
                return bad ("bad number");

            out = Json (d);
            return true;
        }

        static void appendUtf8 (std::string& out, uint32_t cp)
        {
            if (cp < 0x80)
            {
                out += (char) cp;
            }
            else if (cp < 0x800)
            {
                out += (char) (0xc0 | (cp >> 6));
                out += (char) (0x80 | (cp & 0x3f));
            }
            else if (cp < 0x10000)
            {
                out += (char) (0xe0 | (cp >> 12));
                out += (char) (0x80 | ((cp >> 6) & 0x3f));
                out += (char) (0x80 | (cp & 0x3f));
            }
            else
            {
                out += (char) (0xf0 | (cp >> 18));
                out += (char) (0x80 | ((cp >> 12) & 0x3f));
                out += (char) (0x80 | ((cp >> 6) & 0x3f));
                out += (char) (0x80 | (cp & 0x3f));
            }
        }

        bool hex4 (uint32_t& v)
        {
            if (pos + 4 > s.size())
                return bad ("bad \\u escape");

            v = 0;

            for (int i = 0; i < 4; ++i)
            {
                const auto c = s[pos++];
                v <<= 4;

                if (c >= '0' && c <= '9')      v |= (uint32_t) (c - '0');
                else if (c >= 'a' && c <= 'f') v |= (uint32_t) (c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') v |= (uint32_t) (c - 'A' + 10);
                else return bad ("bad \\u escape");
            }

            return true;
        }

        bool stringValue (std::string& out)
        {
            ++pos;   // the opening quote

            while (pos < s.size())
            {
                const auto c = s[pos++];

                if (c == '"')
                    return true;

                if (c != '\\')
                {
                    out += c;
                    continue;
                }

                if (pos >= s.size())
                    break;

                const auto e = s[pos++];

                switch (e)
                {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;

                    case 'u':
                    {
                        uint32_t cp = 0;

                        if (! hex4 (cp))
                            return false;

                        if (cp >= 0xd800 && cp < 0xdc00 && pos + 1 < s.size() && s[pos] == '\\' && s[pos + 1] == 'u')
                        {
                            pos += 2;
                            uint32_t low = 0;

                            if (! hex4 (low))
                                return false;

                            cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                        }

                        appendUtf8 (out, cp);
                        break;
                    }

                    default: return bad ("bad escape");
                }
            }

            return bad ("unterminated string");
        }

        bool arrayValue (Json& out, int depth)
        {
            ++pos;
            out = Json::array();
            skip();

            if (pos < s.size() && s[pos] == ']')
            {
                ++pos;
                return true;
            }

            std::vector<double> run;   // numbers read so far, while the array holds only numbers
            bool allNumbers = true;

            auto finishRun = [&]
            {
                if (allNumbers && ! run.empty())
                    out = Json::numbers (std::move (run));
            };

            for (;;)
            {
                skip();
                Json item;

                if (! value (item, depth + 1))
                    return false;

                if (allNumbers && item.isNumber())
                {
                    run.push_back (item.asDouble());
                }
                else
                {
                    if (allNumbers)
                    {
                        for (const auto d : run)
                            out.push (Json (d));

                        run.clear();
                        allNumbers = false;
                    }

                    out.push (std::move (item));
                }

                skip();

                if (pos >= s.size())
                    return bad ("unterminated array");

                if (s[pos] == ',')
                {
                    ++pos;
                    continue;
                }

                if (s[pos] == ']')
                {
                    ++pos;
                    finishRun();
                    return true;
                }

                return bad ("expected , or ]");
            }
        }

        bool objectValue (Json& out, int depth)
        {
            ++pos;
            out = Json::object();
            skip();

            if (pos < s.size() && s[pos] == '}')
            {
                ++pos;
                return true;
            }

            for (;;)
            {
                skip();

                if (pos >= s.size() || s[pos] != '"')
                    return bad ("expected a member name");

                std::string name;

                if (! stringValue (name))
                    return false;

                skip();

                if (pos >= s.size() || s[pos] != ':')
                    return bad ("expected :");

                ++pos;
                skip();
                Json member;

                if (! value (member, depth + 1))
                    return false;

                out.set (name, std::move (member));
                skip();

                if (pos >= s.size())
                    return bad ("unterminated object");

                if (s[pos] == ',')
                {
                    ++pos;
                    continue;
                }

                if (s[pos] == '}')
                {
                    ++pos;
                    return true;
                }

                return bad ("expected , or }");
            }
        }

        const std::string& s;
        size_t pos = 0;
        std::string message;
    };
}

bool Json::parse (const std::string& text, Json& result, std::string* error)
{
    Json parsed;
    Parser parser (text);

    if (! parser.parseDocument (parsed, error))
        return false;

    result = std::move (parsed);
    return true;
}

}  // namespace trs
