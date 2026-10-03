#pragma once

// A small well-formedness checker for the MEI the writer produces.

#include <cctype>
#include <map>
#include <string>
#include <vector>

namespace xmlcheck
{
    struct XmlCheck
    {
        bool wellFormed = true;
        std::string problem;
        std::map<std::string, int> tagCount;
        std::vector<std::string> ids;
    };

    // A small checker, enough for what the writer produces: tags nest, attribute values are quoted,
    // text has no raw "<" or "&" that is not an entity.
    inline XmlCheck checkXml (const std::string& xml)
    {
        XmlCheck r;
        std::vector<std::string> stack;
        size_t i = 0;

        auto fail = [&] (const std::string& why) { r.wellFormed = false; r.problem = why + " at " + std::to_string (i); };

        while (i < xml.size() && r.wellFormed)
        {
            if (xml[i] != '<')
            {
                if (xml[i] == '&')
                {
                    const auto semi = xml.find (';', i);

                    if (semi == std::string::npos || semi - i > 6)
                        fail ("bad entity");
                }

                ++i;
                continue;
            }

            if (xml.compare (i, 2, "<?") == 0)
            {
                const auto end = xml.find ("?>", i);

                if (end == std::string::npos) { fail ("unclosed declaration"); break; }

                i = end + 2;
                continue;
            }

            const auto end = xml.find ('>', i);

            if (end == std::string::npos) { fail ("unclosed tag"); break; }

            auto inner = xml.substr (i + 1, end - i - 1);
            const bool closing = ! inner.empty() && inner[0] == '/';
            const bool selfClosing = ! inner.empty() && inner.back() == '/';

            if (closing)
                inner = inner.substr (1);

            if (selfClosing)
                inner.pop_back();

            const auto space = inner.find_first_of (" \n\t");
            const auto name = inner.substr (0, space);

            if (name.empty()) { fail ("empty tag name"); break; }

            if (closing)
            {
                if (stack.empty() || stack.back() != name) { fail ("mismatched </" + name + ">"); break; }

                stack.pop_back();
            }
            else
            {
                ++r.tagCount[name];

                // attributes: name="value" pairs
                size_t a = space == std::string::npos ? inner.size() : space;

                while (a < inner.size())
                {
                    while (a < inner.size() && std::isspace ((unsigned char) inner[a])) ++a;

                    if (a >= inner.size()) break;

                    const auto eq = inner.find ('=', a);

                    if (eq == std::string::npos || eq + 1 >= inner.size() || inner[eq + 1] != '"') { fail ("bad attribute in <" + name + ">"); break; }

                    const auto close = inner.find ('"', eq + 2);

                    if (close == std::string::npos) { fail ("unclosed attribute value"); break; }

                    if (inner.substr (a, eq - a) == "xml:id")
                        r.ids.push_back (inner.substr (eq + 2, close - eq - 2));

                    a = close + 1;
                }

                if (! selfClosing)
                    stack.push_back (name);
            }

            i = end + 1;
        }

        if (r.wellFormed && ! stack.empty())
        {
            r.wellFormed = false;
            r.problem = "<" + stack.back() + "> is never closed";
        }

        return r;
    }

}
