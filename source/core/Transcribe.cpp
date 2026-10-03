#include "Notation.h"
#include "TranscribeInternal.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace trs
{

//==============================================================================
// Settings

Json TranscriptionSettings::toJson() const
{
    auto j = Json::object();
    j.set ("grid", grid);
    j.set ("triplets", triplets);
    j.set ("transpose", transpose);
    j.set ("splitPoint", splitPoint);
    j.set ("autoPickup", autoPickup);
    j.set ("keyTonic", keyTonic);
    j.set ("keyMinor", keyMinor);
    return j;
}

TranscriptionSettings TranscriptionSettings::fromJson (const Json& j)
{
    TranscriptionSettings s;
    const auto grid = (int) j.get ("grid").asInt (s.grid);
    s.grid = grid == 4 || grid == 8 || grid == 16 || grid == 32 || grid == 64 || grid == 128 ? grid : s.grid;
    s.triplets = j.get ("triplets").asBool (s.triplets);
    s.transpose = std::max (-48, std::min (48, (int) j.get ("transpose").asInt (0)));
    s.splitPoint = std::max (21, std::min (108, (int) j.get ("splitPoint").asInt (s.splitPoint)));
    s.autoPickup = j.get ("autoPickup").asBool (s.autoPickup);
    const auto tonic = (int) j.get ("keyTonic").asInt (-1);
    s.keyTonic = tonic >= 0 && tonic < 12 ? tonic : -1;
    s.keyMinor = j.get ("keyMinor").asBool (false);
    return s;
}

bool TranscriptionSettings::operator== (const TranscriptionSettings& o) const
{
    return grid == o.grid && triplets == o.triplets && transpose == o.transpose && splitPoint == o.splitPoint && autoPickup == o.autoPickup
        && keyTonic == o.keyTonic && keyMinor == o.keyMinor;
}

//==============================================================================
using namespace detail;

//==============================================================================
TranscriptionResult transcribePiano (const ResolvedCapture& capture, const TranscriptionSettings& settings)
{
    TranscriptionResult result;
    auto& report = result.report;

    auto prepared = prepareNotes (capture, settings, result);
    auto& quantized = prepared.quantized;
    auto& bars = prepared.bars;

    const auto spelled = analyseKey (quantized.notes, settings, result);

    // voices
    const auto staves = assignPianoVoices (quantized.notes, settings);

    for (const auto& s : staves)
        report.maxVoices = std::max (report.maxVoices, (int) s.voices.size());

    if (report.maxVoices > 4)
        report.warnings.push_back ("A hand needs " + std::to_string (report.maxVoices) + " voices; more than 4 is hard to read.");

    addCommonWarnings (report);

    // the score
    Score& score = result.score;
    Builder builder (score, quantized.notes, spelled, quantized.beats, result.key.fifths);

    auto part = score.makeNode (nodeType::part);
    part.props["name"] = Json ("Piano");

    const auto tempos = tempoMarks (capture);
    const int measureNumber = bars.front().pickup ? 0 : 1;

    for (int s = 0; s < 2; ++s)
    {
        auto staff = score.makeNode (nodeType::staff);
        staff.props["n"] = Json (s + 1);
        staff.props["clef"] = Json (s == 0 ? "G" : "F");

        int number = measureNumber;

        for (const auto& bar : bars)
        {
            auto m = builder.measure (bar, number++, staves[(size_t) s]);
            builder.addAccidentals (m);

            if (s == 0)
                addTempoMarks (score, m, bar, bars, tempos);

            staff.children.push_back (std::move (m));
        }

        part.children.push_back (std::move (staff));
    }

    finishScore (result, std::move (part), "transcribePiano", std::move (bars), std::move (quantized.notes), true);
    return result;
}

//==============================================================================
namespace
{
    std::string pitchName (const Node& note)
    {
        if (note.has ("drum"))
        {
            auto name = note.prop ("drum").asString();

            for (auto& c : name)
                if (c == ' ')
                    c = '_';

            return note.prop ("ghost").asBool() ? "(" + name + ")" : name;
        }

        if (note.has ("course"))
            return std::to_string (note.prop ("course").asInt()) + ":" + std::to_string (note.prop ("fret").asInt());

        std::string s (1, note.prop ("step").asString().empty() ? '?' : note.prop ("step").asString()[0]);
        const int alter = (int) note.prop ("alter").asInt();

        if (alter > 0)
            s += std::string ((size_t) alter, '#');
        else if (alter < 0)
            s += std::string ((size_t) -alter, 'b');

        return s + std::to_string (note.prop ("oct").asInt());
    }

    std::string valueText (const Node& e)
    {
        auto s = "/" + std::to_string (e.prop ("dur").asInt());

        for (int i = 0; i < (int) e.prop ("dots").asInt(); ++i)
            s += ".";

        return s;
    }

    std::string tieText (const Node& e)
    {
        // a chord is tied when its notes are; the first note says
        const Node* n = &e;

        if (e.type == nodeType::chord && ! e.children.empty())
            n = &e.children.front();

        const auto tie = n->prop ("tie").asString();
        return tie == "i" || tie == "m" ? "~" : "";
    }

    std::string accidText (const Node& n)
    {
        return n.has ("accid") ? "!" : "";
    }
}

std::string dumpScore (const Score& score)
{
    std::string out;
    const auto& root = score.root();

    out += "score key=" + std::to_string (root.prop ("keyFifths").asInt()) + " " + root.prop ("keyMode").asString() + "\n";

    for (const auto& part : root.children)
    {
        if (part.type != nodeType::part)
            continue;

        size_t measures = 0;

        for (const auto& staff : part.children)
            measures = std::max (measures, staff.children.size());

        for (size_t i = 0; i < measures; ++i)
        {
            for (const auto& staff : part.children)
            {
                if (i >= staff.children.size())
                    continue;

                const auto& m = staff.children[i];

                if (staff.prop ("n").asInt() == 1)
                {
                    out += "m" + std::to_string (m.prop ("n").asInt()) + " " + std::to_string (m.prop ("num").asInt()) + "/"
                         + std::to_string (m.prop ("den").asInt());

                    if (m.prop ("pickup").asBool())
                        out += " pickup:" + std::to_string (m.prop ("ticks").asInt());

                    if (m.prop ("irregular").asBool())
                        out += " irregular:" + std::to_string (m.prop ("ticks").asInt());

                    out += "\n";

                    for (const auto& c : m.children)
                    {
                        if (c.type != nodeType::tempo)
                            continue;

                        out += "  tempo @" + std::to_string (c.prop ("onset").asInt()) + ":";

                        if (c.has ("text"))
                            out += " " + c.prop ("text").asString();

                        if (c.has ("bpm"))
                            out += " " + std::to_string (c.prop ("bpm").asInt());

                        out += "\n";
                    }
                }

                for (const auto& layer : m.children)
                {
                    if (layer.type != nodeType::layer)
                        continue;

                    out += "  S" + std::to_string (staff.prop ("n").asInt()) + " v" + std::to_string (layer.prop ("n").asInt()) + ":";
                    std::string openTuplet, openBeam;

                    for (const auto& e : layer.children)
                    {
                        const auto tuplet = e.prop ("tuplet").asString();
                        const auto beam = e.prop ("beam").asString();

                        // closing: beam first, then the tuplet that holds it; opening: the other way round
                        if (beam != openBeam && ! openBeam.empty())
                            out += " )";

                        if (tuplet != openTuplet && ! openTuplet.empty())
                            out += " >";

                        if (tuplet != openTuplet && ! tuplet.empty())
                            out += " <";

                        if (beam != openBeam && ! beam.empty())
                            out += " (";

                        openTuplet = tuplet;
                        openBeam = beam;

                        out += " ";

                        if (e.type == nodeType::rest)
                        {
                            out += e.prop ("measureRest").asBool() ? "R" : "r" + valueText (e);
                        }
                        else if (e.type == nodeType::chord)
                        {
                            out += "[";

                            for (size_t k = 0; k < e.children.size(); ++k)
                                out += (k > 0 ? " " : "") + pitchName (e.children[k]) + accidText (e.children[k]);

                            out += "]" + valueText (e) + tieText (e);
                        }
                        else
                        {
                            out += pitchName (e) + accidText (e) + valueText (e) + tieText (e);
                        }
                    }

                    if (! openBeam.empty())
                        out += " )";

                    if (! openTuplet.empty())
                        out += " >";

                    out += "\n";
                }
            }
        }
    }

    return out;
}

}  // namespace trs
