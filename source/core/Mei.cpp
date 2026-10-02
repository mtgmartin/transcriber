#include "Mei.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <map>

namespace trs
{

std::string xmlEscape (const std::string& text)
{
    std::string out;
    out.reserve (text.size());

    for (const auto c : text)
    {
        switch (c)
        {
            case '&':  out += "&amp;"; break;
            case '<':  out += "&lt;"; break;
            case '>':  out += "&gt;"; break;
            case '"':  out += "&quot;"; break;
            default:   out += c; break;
        }
    }

    return out;
}

namespace
{
    std::string keySignature (int fifths)
    {
        if (fifths == 0)
            return "0";

        return std::to_string (std::abs (fifths)) + (fifths > 0 ? "s" : "f");
    }

    std::string lowerLetter (const std::string& step)
    {
        if (step.empty())
            return "c";

        return std::string (1, (char) std::tolower ((unsigned char) step[0]));
    }

    const char* alterName (int alter)
    {
        switch (alter)
        {
            case 1:  return "s";
            case -1: return "f";
            case 2:  return "ss";
            case -2: return "ff";
            default: return nullptr;
        }
    }

    void attribute (std::string& out, const char* name, const std::string& value)
    {
        out += " ";
        out += name;
        out += "=\"" + xmlEscape (value) + "\"";
    }

    class Writer
    {
    public:
        explicit Writer (const Score& s) : score (s) {}

        std::string write (const MeiOptions& options)
        {
            const auto& root = score.root();
            const Node* part = nullptr;

            for (const auto& c : root.children)
                if (c.type == nodeType::part)
                    part = &c;

            out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
            out += "<mei xmlns=\"http://www.music-encoding.org/ns/mei\" meiversion=\"5.1\">\n";
            out += " <meiHead>\n  <fileDesc>\n   <titleStmt>\n    <title>" + xmlEscape (options.title) + "</title>\n";

            if (! options.composer.empty())
                out += "    <composer>" + xmlEscape (options.composer) + "</composer>\n";

            out += "   </titleStmt>\n   <pubStmt/>\n  </fileDesc>\n </meiHead>\n";
            out += " <music><body><mdiv><score>\n";

            const auto fifths = (int) root.prop ("keyFifths").asInt();
            int num = 4, den = 4;
            size_t measures = 0;
            std::vector<const Node*> staves;

            if (part != nullptr)
            {
                for (const auto& staff : part->children)
                {
                    if (staff.type != nodeType::staff)
                        continue;

                    staves.push_back (&staff);
                    measures = std::max (measures, staff.children.size());
                }

                if (! staves.empty() && ! staves.front()->children.empty())
                {
                    num = (int) staves.front()->children.front().prop ("num").asInt();
                    den = (int) staves.front()->children.front().prop ("den").asInt();
                }
            }

            out += "  <scoreDef keysig=\"" + keySignature (fifths) + "\" meter.count=\"" + std::to_string (num)
                 + "\" meter.unit=\"" + std::to_string (den) + "\">\n   <staffGrp";

            if (staves.size() > 1)
                out += " symbol=\"brace\" bar.thru=\"true\"";

            out += ">\n";

            for (const auto* staff : staves)
            {
                const auto clef = staff->prop ("clef").asString();
                out += "    <staffDef n=\"" + std::to_string (staff->prop ("n").asInt()) + "\" lines=\"5\" clef.shape=\""
                     + (clef.empty() ? "G" : clef) + "\" clef.line=\"" + (clef == "F" ? "4" : "2") + "\"/>\n";
            }

            out += "   </staffGrp>\n  </scoreDef>\n  <section>\n";

            for (size_t i = 0; i < measures; ++i)
            {
                const Node* first = nullptr;

                for (const auto* staff : staves)
                    if (i < staff->children.size()) { first = &staff->children[i]; break; }

                if (first == nullptr)
                    continue;

                const auto mNum = (int) first->prop ("num").asInt();
                const auto mDen = (int) first->prop ("den").asInt();

                if (i > 0 && (mNum != num || mDen != den))
                {
                    out += "   <scoreDef meter.count=\"" + std::to_string (mNum) + "\" meter.unit=\"" + std::to_string (mDen) + "\"/>\n";
                }

                num = mNum;
                den = mDen;

                out += "   <measure xml:id=\"" + xmlEscape (first->id) + "\" n=\"" + std::to_string (first->prop ("n").asInt()) + "\"";

                if (first->prop ("pickup").asBool() || first->prop ("irregular").asBool())
                    out += " metcon=\"false\"";

                if (i + 1 == measures)
                    out += " right=\"end\"";

                out += ">\n";

                for (const auto* staff : staves)
                {
                    if (i >= staff->children.size())
                        continue;

                    staffContent (*staff, staff->children[i]);
                }

                for (const auto* staff : staves)
                {
                    if (i >= staff->children.size())
                        continue;

                    for (const auto& c : staff->children[i].children)
                        if (c.type == nodeType::tempo)
                            tempoMark (c, (int) staff->prop ("n").asInt(), den);
                }

                out += "   </measure>\n";
            }

            out += "  </section>\n </score></mdiv></body></music>\n</mei>\n";
            return out;
        }

    private:
        const Score& score;
        std::string out;

        void staffContent (const Node& staff, const Node& measure)
        {
            out += "    <staff n=\"" + std::to_string (staff.prop ("n").asInt()) + "\">\n";

            for (const auto& layer : measure.children)
            {
                if (layer.type != nodeType::layer)
                    continue;

                out += "     <layer xml:id=\"" + xmlEscape (layer.id) + "\" n=\"" + std::to_string (layer.prop ("n").asInt()) + "\">\n";
                layerContent (layer);
                out += "     </layer>\n";
            }

            out += "    </staff>\n";
        }

        struct Container
        {
            bool tuplet = false;
            std::string id;
            size_t first = 0, last = 0;
        };

        void layerContent (const Node& layer)
        {
            const auto& events = layer.children;
            std::map<std::string, Container> beams, tuplets;

            for (size_t i = 0; i < events.size(); ++i)
            {
                const auto beam = events[i].prop ("beam").asString();
                const auto tuplet = events[i].prop ("tuplet").asString();

                if (! beam.empty())
                {
                    auto& b = beams[beam];

                    if (b.id.empty()) { b.id = beam; b.first = i; }
                    b.last = i;
                }

                if (! tuplet.empty())
                {
                    auto& t = tuplets[tuplet];

                    if (t.id.empty()) { t.id = tuplet; t.tuplet = true; t.first = i; }
                    t.last = i;
                }
            }

            // A beam and a tuplet can only be nested or apart; where they overlap partly the beam is left out.
            for (auto it = beams.begin(); it != beams.end();)
            {
                bool drop = it->second.last == it->second.first;

                for (const auto& t : tuplets)
                {
                    const auto& b = it->second;
                    const bool apart = b.last < t.second.first || t.second.last < b.first;
                    const bool inside = b.first >= t.second.first && b.last <= t.second.last;
                    const bool around = t.second.first >= b.first && t.second.last <= b.last;

                    if (! apart && ! inside && ! around)
                        drop = true;
                }

                it = drop ? beams.erase (it) : std::next (it);
            }

            std::vector<const Container*> open;

            for (size_t i = 0; i < events.size(); ++i)
            {
                std::vector<const Container*> starting;

                for (const auto& b : beams)
                    if (b.second.first == i) starting.push_back (&b.second);

                for (const auto& t : tuplets)
                    if (t.second.first == i) starting.push_back (&t.second);

                // the larger container goes outside; a tuplet outside a beam of the same extent
                std::stable_sort (starting.begin(), starting.end(), [] (const Container* a, const Container* b)
                {
                    const auto la = a->last - a->first, lb = b->last - b->first;
                    return la != lb ? la > lb : (a->tuplet && ! b->tuplet);
                });

                for (const auto* c : starting)
                {
                    out += c->tuplet ? "      <tuplet num=\"3\" numbase=\"2\">\n" : "      <beam>\n";
                    open.push_back (c);
                }

                event (events[i]);

                while (! open.empty() && open.back()->last == i)
                {
                    out += open.back()->tuplet ? "      </tuplet>\n" : "      </beam>\n";
                    open.pop_back();
                }
            }
        }

        static void durationAttributes (std::string& s, const Node& e)
        {
            attribute (s, "dur", std::to_string (e.prop ("dur").asInt()));

            if (e.prop ("dots").asInt() > 0)
                attribute (s, "dots", std::to_string (e.prop ("dots").asInt()));
        }

        static std::string noteElement (const Node& n, bool withDuration, const Node* durationSource)
        {
            std::string s = "<note xml:id=\"" + xmlEscape (n.id) + "\"";

            if (withDuration && durationSource != nullptr)
                durationAttributes (s, *durationSource);

            attribute (s, "pname", lowerLetter (n.prop ("step").asString()));
            attribute (s, "oct", std::to_string (n.prop ("oct").asInt()));

            const auto alter = (int) n.prop ("alter").asInt();

            if (n.has ("accid"))
            {
                attribute (s, "accid", n.prop ("accid").asString());
            }
            else if (const auto* name = alterName (alter))
            {
                attribute (s, "accid.ges", name);
            }

            const auto tie = n.prop ("tie").asString();

            if (! tie.empty())
                attribute (s, "tie", tie);

            s += "/>";
            return s;
        }

        void event (const Node& e)
        {
            const std::string pad = "       ";

            if (e.type == nodeType::rest)
            {
                if (e.prop ("measureRest").asBool())
                {
                    out += pad + "<mRest xml:id=\"" + xmlEscape (e.id) + "\"/>\n";
                    return;
                }

                std::string s = "<rest xml:id=\"" + xmlEscape (e.id) + "\"";
                durationAttributes (s, e);
                out += pad + s + "/>\n";
            }
            else if (e.type == nodeType::chord)
            {
                std::string s = "<chord xml:id=\"" + xmlEscape (e.id) + "\"";
                durationAttributes (s, e);
                out += pad + s + ">\n";

                for (const auto& n : e.children)
                    out += pad + " " + noteElement (n, false, nullptr) + "\n";

                out += pad + "</chord>\n";
            }
            else if (e.type == nodeType::note)
            {
                out += pad + noteElement (e, true, &e) + "\n";
            }
        }

        void tempoMark (const Node& t, int staffNumber, int meterUnit)
        {
            const auto beatTicks = 960.0 * 4.0 / (meterUnit > 0 ? meterUnit : 4);
            const auto tstamp = (double) t.prop ("onset").asInt() / beatTicks + 1.0;

            std::string s = "    <tempo xml:id=\"" + xmlEscape (t.id) + "\" staff=\"" + std::to_string (staffNumber) + "\" tstamp=\"";
            char buffer[32];
            snprintf (buffer, sizeof buffer, "%.4f", tstamp);
            s += buffer;
            s += "\"";

            const auto bpm = (int) t.prop ("bpm").asInt();
            const auto text = t.prop ("text").asString();

            if (bpm > 0)
            {
                attribute (s, "midi.bpm", std::to_string (bpm));
                attribute (s, "mm", std::to_string (bpm));
                attribute (s, "mm.unit", "4");
            }

            s += ">";

            if (! text.empty())
                s += xmlEscape (text) + (bpm > 0 ? " " : "");

            // the metronome mark: a quarter note, "=" and the number
            if (bpm > 0)
                s += "<symbol glyph.auth=\"smufl\" glyph.name=\"metNoteQuarterUp\"/> = " + std::to_string (bpm);

            s += "</tempo>\n";
            out += s;
        }
    };
}

namespace
{
    std::string valueName (int dur)
    {
        switch (dur)
        {
            case 1:  return "whole";
            case 2:  return "half";
            case 4:  return "quarter";
            case 8:  return "eighth";
            case 16: return "sixteenth";
            case 32: return "thirty-second";
            case 64: return "sixty-fourth";
            default: return "";
        }
    }

    std::string pitchText (const Node& n)
    {
        std::string s (1, n.prop ("step").asString().empty() ? '?' : n.prop ("step").asString()[0]);
        const auto alter = (int) n.prop ("alter").asInt();

        if (alter > 0)
            s += std::string ((size_t) alter, '#');
        else if (alter < 0)
            s += std::string ((size_t) -alter, 'b');

        return s + std::to_string (n.prop ("oct").asInt());
    }
}

std::string describeNode (const Score& score, const std::string& id)
{
    const auto* node = score.find (id);

    if (node == nullptr)
        return {};

    // The note, chord or rest, the voice, the measure and the staff around it.
    const Node* event = nullptr;
    const Node* layer = nullptr;
    const Node* measure = nullptr;
    const Node* staff = nullptr;

    for (const Node* n = node; n != nullptr;)
    {
        if (n->type == nodeType::note || n->type == nodeType::rest || n->type == nodeType::chord)
        {
            if (event == nullptr || n->type == nodeType::chord)
                event = n;
        }
        else if (n->type == nodeType::layer)   layer = n;
        else if (n->type == nodeType::measure) measure = n;
        else if (n->type == nodeType::staff)   staff = n;

        n = score.findParent (n->id);
    }

    std::string text;

    if (staff != nullptr)
        text += staff->prop ("clef").asString() == "F" ? "Left hand" : (staff->prop ("n").asInt() == 1 ? "Right hand" : "Staff " + std::to_string (staff->prop ("n").asInt()));

    auto add = [&] (const std::string& part)
    {
        text += (text.empty() ? "" : " \xC2\xB7 ") + part;
    };

    if (measure != nullptr)
        add ("measure " + std::to_string (measure->prop ("n").asInt()));

    if (layer != nullptr)
        add ("voice " + std::to_string (layer->prop ("n").asInt()));

    if (event == nullptr)
        return text;

    auto value = valueName ((int) event->prop ("dur").asInt());
    const auto dots = (int) event->prop ("dots").asInt();

    if (dots > 0)
        value = std::string (dots == 1 ? "dotted " : "double-dotted ") + value;

    if (! event->prop ("tuplet").asString().empty())
        value += " triplet";

    if (event->type == nodeType::rest)
    {
        add (event->prop ("measureRest").asBool() ? "measure rest" : value + " rest");
    }
    else if (event->type == nodeType::chord)
    {
        std::string names;

        for (const auto& c : event->children)
            names += (names.empty() ? "" : " ") + pitchText (c);

        const bool tied = ! event->children.empty() && (event->children.front().prop ("tie").asString() == "i" || event->children.front().prop ("tie").asString() == "m");
        add (value + " chord " + names + (tied ? ", tied to the next" : ""));
    }
    else
    {
        const auto tie = event->prop ("tie").asString();
        add (value + " note " + pitchText (*event) + (tie == "i" || tie == "m" ? ", tied to the next note" : ""));

        if (event->prop ("offGrid").asBool())
            add ("was far from the grid");
    }

    return text;
}

std::string scoreToMei (const Score& score, const MeiOptions& options)
{
    return Writer (score).write (options);
}

}  // namespace trs
