#include "Mei.h"

#include "Instruments.h"

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

            bool hasTab = false;

            for (const auto* staff : staves)
                hasTab = hasTab || staff->prop ("kind").asString() == "tab";

            if (staves.size() > 1)
                out += hasTab ? " symbol=\"bracket\" bar.thru=\"true\"" : " symbol=\"brace\" bar.thru=\"true\"";

            out += ">\n";

            for (const auto* staff : staves)
                staffDefinition (*staff);

            out += "   </staffGrp>\n  </scoreDef>\n  <section>\n";
            indexEvents (staves);

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

                if (i > 0 && first->prop ("break").asString() == "page")
                    out += "   <pb/>\n";
                else if (i > 0 && first->prop ("break").asString() == "system")
                    out += "   <sb/>\n";

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

                for (const auto* staff : staves)
                    if (i < staff->children.size())
                        markings (staff->children[i], (int) staff->prop ("n").asInt());

                spannersStartingIn (i);

                for (const auto& [id, text] : directions)
                    out += "    <dir startid=\"#" + xmlEscape (id) + "\" place=\"above\" staff=\"1\">" + xmlEscape (text) + "</dir>\n";

                directions.clear();

                out += "   </measure>\n";
            }

            out += "  </section>\n </score></mdiv></body></music>\n</mei>\n";
            return out;
        }

    private:
        const Score& score;
        std::string out;
        std::string kind;                 // "perc", "tab" or empty: the staff being written
        int layerNumber = 1;
        std::vector<std::pair<std::string, std::string>> directions;   // (id, text) of marks above notes, written after the staves

        // where every note, chord and rest is, for the marks that point at one
        struct EventInfo
        {
            size_t measure = 0;
            int staff = 1;
            bool rest = false;
        };

        std::map<std::string, EventInfo> where;

        void indexEvents (const std::vector<const Node*>& staves)
        {
            for (const auto* staff : staves)
                for (size_t i = 0; i < staff->children.size(); ++i)
                    for (const auto& layer : staff->children[i].children)
                        for (const auto& e : layer.children)
                            where[e.id] = { i, (int) staff->prop ("n").asInt(), e.type == nodeType::rest };
        }

        // Dynamics, fermatas and text that belong to a note or chord of this measure.
        void markings (const Node& measure, int staffNumber)
        {
            const auto staffText = " staff=\"" + std::to_string (staffNumber) + "\"";

            for (const auto& layer : measure.children)
            {
                if (layer.type != nodeType::layer)
                    continue;

                for (const auto& e : layer.children)
                {
                    const auto start = "startid=\"#" + xmlEscape (e.id) + "\"";

                    if (e.has ("dyn") && e.type != nodeType::rest)
                        out += "    <dynam " + start + staffText + " place=\"below\">" + xmlEscape (e.prop ("dyn").asString()) + "</dynam>\n";

                    if (e.prop ("fermata").asBool() && e.type != nodeType::rest)
                        out += "    <fermata " + start + staffText + " place=\"above\"/>\n";

                    if (e.has ("text"))
                        out += "    <dir " + start + staffText + " place=\"" + (e.prop ("textPlace").asString() == "below" ? "below" : "above")
                               + "\">" + xmlEscape (e.prop ("text").asString()) + "</dir>\n";
                }
            }
        }

        // Slurs and hairpins between two notes or chords; the control event sits in the measure where it starts.
        void spannersStartingIn (size_t measureIndex)
        {
            for (const auto& sp : score.root().children)
            {
                if (sp.type != nodeType::spanner)
                    continue;

                const auto from = where.find (sp.prop ("from").asString());
                const auto to = where.find (sp.prop ("to").asString());

                if (from == where.end() || to == where.end() || from->second.measure != measureIndex || from->second.rest || to->second.rest)
                    continue;

                const auto spanKind = sp.prop ("kind").asString();
                const auto ends = " startid=\"#" + xmlEscape (sp.prop ("from").asString()) + "\" endid=\"#" + xmlEscape (sp.prop ("to").asString())
                                  + "\" staff=\"" + std::to_string (from->second.staff) + "\"";

                if (spanKind == "slur")
                    out += "    <slur" + ends + "/>\n";
                else if (spanKind == "cresc" || spanKind == "dim")
                    out += std::string ("    <hairpin form=\"") + (spanKind == "cresc" ? "cres" : "dim") + "\"" + ends + " place=\"below\"/>\n";
            }
        }

        // The clef of a staff, and for a tablature staff its lines and tuning.
        void staffDefinition (const Node& staff)
        {
            const auto clef = staff.prop ("clef").asString();
            const auto n = std::to_string (staff.prop ("n").asInt());

            if (clef == "TAB")
            {
                const auto strings = (int) staff.prop ("strings").asInt (6);
                const auto open = openStrings (strings == 4 ? InstrumentType::bass : InstrumentType::guitar);
                out += "    <staffDef n=\"" + n + "\" lines=\"" + std::to_string (strings) + "\" notationtype=\"tab.guitar\" clef.shape=\"TAB\">\n"
                       "     <tuning>";

                for (int course = 1; course <= (int) open.size(); ++course)
                {
                    const auto midi = open[open.size() - (size_t) course];
                    static const char* const names[12] = { "c", "c", "d", "d", "e", "f", "f", "g", "g", "a", "a", "b" };
                    out += "<course n=\"" + std::to_string (course) + "\" pname=\"" + names[midi % 12] + "\" oct=\"" + std::to_string (midi / 12 - 1) + "\"/>";
                }

                out += "</tuning>\n    </staffDef>\n";
                return;
            }

            std::string shape = "G", line = "2", displacement;

            if (clef == "F" || clef == "F8") { shape = "F"; line = "4"; }
            if (clef == "perc")              { shape = "perc"; line = ""; }
            if (clef == "G8" || clef == "F8") displacement = " clef.dis=\"8\" clef.dis.place=\"below\"";

            out += "    <staffDef n=\"" + n + "\" lines=\"5\" clef.shape=\"" + shape + "\"" + (line.empty() ? "" : " clef.line=\"" + line + "\"")
                 + displacement + "/>\n";
        }

        void staffContent (const Node& staff, const Node& measure)
        {
            kind = staff.prop ("kind").asString();
            out += "    <staff n=\"" + std::to_string (staff.prop ("n").asInt()) + "\">\n";

            for (const auto& layer : measure.children)
            {
                if (layer.type != nodeType::layer)
                    continue;

                layerNumber = (int) layer.prop ("n").asInt (1);
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

        std::string noteElement (const Node& n, bool withDuration, const Node* durationSource, const std::string& idOwner)
        {
            std::string s = "<note xml:id=\"" + xmlEscape (idOwner.empty() ? n.id : idOwner) + "\"";

            if (withDuration && durationSource != nullptr)
                durationAttributes (s, *durationSource);

            if (kind == "perc")
            {
                attribute (s, "loc", std::to_string (n.prop ("loc").asInt()));
                const auto head = n.prop ("head").asString();

                if (head == "x" || head == "open-x")
                    attribute (s, "head.shape", "x");
                else if (head == "diamond")
                    attribute (s, "head.shape", "diamond");

                if (n.prop ("ghost").asBool())
                    attribute (s, "head.mod", "paren");

                if (withDuration)
                    attribute (s, "stem.dir", layerNumber == 1 ? "up" : "down");

                if (withDuration && n.has ("artic"))
                    attribute (s, "artic", n.prop ("artic").asString());
            }
            else
            {
                attribute (s, "pname", lowerLetter (n.prop ("step").asString()));
                attribute (s, "oct", std::to_string (n.prop ("oct").asInt()));

                if (withDuration && n.has ("stem"))
                    attribute (s, "stem.dir", n.prop ("stem").asString());

                if (withDuration && n.has ("artic"))
                    attribute (s, "artic", n.prop ("artic").asString());

                const auto alter = (int) n.prop ("alter").asInt();

                if (n.has ("accid"))
                {
                    attribute (s, "accid", n.prop ("accid").asString());
                }
                else if (const auto* name = alterName (alter))
                {
                    attribute (s, "accid.ges", name);
                }
            }

            const auto tie = n.prop ("tie").asString();

            if (! tie.empty())
                attribute (s, "tie", tie);

            s += "/>";
            return s;
        }

        // A note of a tablature staff: the course (1 = highest string) and the fret.
        static std::string tabNoteElement (const Node& n, const std::string& idOwner)
        {
            std::string s = "<note xml:id=\"" + xmlEscape (idOwner.empty() ? n.id : idOwner) + "\"";
            attribute (s, "tab.course", std::to_string (n.prop ("course").asInt()));
            attribute (s, "tab.fret", std::to_string (n.prop ("fret").asInt()));

            const auto tie = n.prop ("tie").asString();

            if (! tie.empty())
                attribute (s, "tie", tie);

            return s + "/>";
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
                return;
            }

            if (kind == "tab")
            {
                std::string s = "<tabGrp";

                // a group of several notes carries the id of the chord; a single note keeps its own id
                if (e.type == nodeType::chord)
                    s += " xml:id=\"" + xmlEscape (e.id) + "\"";

                durationAttributes (s, e);
                out += pad + s + "><tabDurSym/>";

                if (e.type == nodeType::chord)
                    for (const auto& n : e.children)
                        out += tabNoteElement (n, {});
                else
                    out += tabNoteElement (e, {});

                out += "</tabGrp>\n";
                return;
            }

            if (e.type == nodeType::chord)
            {
                std::string s = "<chord xml:id=\"" + xmlEscape (e.id) + "\"";
                durationAttributes (s, e);

                if (kind == "perc")
                    attribute (s, "stem.dir", layerNumber == 1 ? "up" : "down");
                else if (e.has ("stem"))
                    attribute (s, "stem.dir", e.prop ("stem").asString());

                if (e.has ("artic"))
                    attribute (s, "artic", e.prop ("artic").asString());

                out += pad + s + ">\n";

                bool open = false;

                for (const auto& n : e.children)
                {
                    out += pad + " " + noteElement (n, false, nullptr, {}) + "\n";
                    open = open || n.prop ("head").asString() == "open-x";
                }

                out += pad + "</chord>\n";

                if (open)
                    directions.push_back ({ e.id, "o" });
            }
            else if (e.type == nodeType::note)
            {
                out += pad + noteElement (e, true, &e, {}) + "\n";

                if (e.prop ("head").asString() == "open-x")
                    directions.push_back ({ e.id, "o" });
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
            case 128: return "128th";
            default: return "";
        }
    }

    std::string lowerCase (std::string s)
    {
        for (auto& c : s)
            c = (char) std::tolower ((unsigned char) c);

        return s;
    }

    // "snare", "string 3 fret 5" or "Eb5": what a note is called.
    std::string pitchText (const Node& n)
    {
        if (n.has ("drum"))
            return lowerCase (n.prop ("drum").asString()) + (n.prop ("ghost").asBool() ? " (ghost note)" : "");

        if (n.has ("course"))
            return "string " + std::to_string (n.prop ("course").asInt()) + " fret " + std::to_string (n.prop ("fret").asInt());

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
    {
        const auto clef = staff->prop ("clef").asString();

        if (clef == "perc")         text += "Drums";
        else if (clef == "TAB")     text += "Tab";
        else if (clef == "F")       text += "Left hand";
        else if (clef == "G")       text += staff->prop ("n").asInt() == 1 ? "Right hand" : "Staff " + std::to_string (staff->prop ("n").asInt());
        else                        text += "Staff";
    }

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

    // one note of a chord that was picked on its own
    const bool pickedNote = node != event && node->type == nodeType::note && event->type == nodeType::chord;

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
        const bool fretted = ! event->children.empty() && event->children.front().has ("course");

        for (const auto& c : event->children)
            names += (names.empty() ? "" : fretted ? ", " : " ") + pitchText (c);

        const bool drumChord = ! event->children.empty() && event->children.front().has ("drum");
        const bool tied = ! event->children.empty() && (event->children.front().prop ("tie").asString() == "i" || event->children.front().prop ("tie").asString() == "m");
        add (value + (drumChord ? " hit " : fretted ? " chord: " : " chord ") + names + (tied ? ", tied to the next" : "")
             + (event->has ("stem") ? ", stem " + event->prop ("stem").asString() : std::string()));
    }
    else
    {
        const auto tie = event->prop ("tie").asString();
        add (value + (event->has ("drum") ? " hit " : " note ") + pitchText (*event) + (tie == "i" || tie == "m" ? ", tied to the next note" : "")
             + (event->has ("stem") ? ", stem " + event->prop ("stem").asString() : std::string()));

        if (event->prop ("offGrid").asBool())
            add ("was far from the grid");
    }

    if (pickedNote)
        add ("this note: " + pitchText (*node));

    if (event->type != nodeType::rest)
    {
        static const std::map<std::string, std::string> articNames = { { "stacc", "staccato" }, { "acc", "accent" }, { "ten", "tenuto" }, { "marc", "marcato" } };
        std::string marks;

        auto mark = [&] (const std::string& m) { marks += (marks.empty() ? "" : ", ") + m; };

        if (event->has ("dyn"))
            mark (event->prop ("dyn").asString());

        if (event->has ("artic"))
        {
            const auto it = articNames.find (event->prop ("artic").asString());
            mark (it != articNames.end() ? it->second : event->prop ("artic").asString());
        }

        if (event->prop ("fermata").asBool())
            mark ("fermata");

        for (const auto& sp : score.root().children)
            if (sp.type == nodeType::spanner && sp.prop ("from").asString() == event->id)
                mark (sp.prop ("kind").asString() == "slur" ? "slur starts here" : sp.prop ("kind").asString() == "cresc" ? "crescendo starts here" : "diminuendo starts here");

        if (! marks.empty())
            add ("marks: " + marks);
    }

    if (event->has ("text"))
        add ("text \"" + event->prop ("text").asString() + "\" " + (event->prop ("textPlace").asString() == "below" ? "below" : "above"));

    return text;
}

std::string scoreToMei (const Score& score, const MeiOptions& options)
{
    return Writer (score).write (options);
}

}  // namespace trs
