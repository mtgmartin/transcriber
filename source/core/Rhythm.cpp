#include "Notation.h"

#include <algorithm>

namespace trs
{

namespace
{
    struct WrittenValue
    {
        int64_t ticks;
        int dur;
        int dots;
        int64_t base;   // the undotted length of the value
    };

    // Largest first.
    const WrittenValue writtenValues[] = {
        { 5760, 1, 1, 3840 }, { 3840, 1, 0, 3840 },
        { 2880, 2, 1, 1920 }, { 1920, 2, 0, 1920 },
        { 1440, 4, 1, 960 },  { 960, 4, 0, 960 },
        { 720, 8, 1, 480 },   { 480, 8, 0, 480 },
        { 360, 16, 1, 240 },  { 240, 16, 0, 240 },
        { 180, 32, 1, 120 },  { 120, 32, 0, 120 },
        { 90, 64, 1, 60 },    { 60, 64, 0, 60 },
    };

    bool isCompound (int num, int den)
    {
        return den == 8 && num >= 6 && num % 3 == 0;
    }

    // A value starts only where a note of its own size could start; in compound meters a dotted
    // quarter may also start on any dotted-quarter beat.
    bool aligned (const WrittenValue& v, int64_t position, bool compound)
    {
        if (compound && v.dots == 1 && v.base == 960)
            return position % 1440 == 0;

        return position % v.base == 0;
    }

    // The written value that is exactly `length` and may start at `position`, if there is one.
    const WrittenValue* singleValue (int64_t position, int64_t length, bool compound)
    {
        for (const auto& v : writtenValues)
            if (v.ticks == length && aligned (v, position, compound))
                return &v;

        return nullptr;
    }

    // How strong the metrical position is: the biggest unit it is a multiple of.
    int64_t strengthOf (int64_t position, bool compound)
    {
        static const int64_t units[] = { 3840, 1920, 1440, 960, 480, 240, 120, 60 };

        for (const auto u : units)
            if ((compound || u != 1440) && position % u == 0)
                return u;

        return 1;
    }

    // Writes a length as written values: one value if a single aligned one fits, otherwise it is cut at
    // the strongest beat inside it and both halves are written the same way. (An eighth followed by
    // a quarter, never a dotted eighth followed by a dotted sixteenth.)
    void decomposeStraight (int64_t position, int64_t length, bool compound, std::vector<RhythmPiece>& out)
    {
        if (length <= 0)
            return;

        if (const auto* v = singleValue (position, length, compound))
        {
            RhythmPiece p;
            p.on = position;
            p.ticks = v->ticks;
            p.dur = v->dur;
            p.dots = v->dots;
            out.push_back (p);
            return;
        }

        int64_t cut = 0, cutStrength = 0;

        for (int64_t b = (position / 60 + 1) * 60; b < position + length; b += 60)
        {
            const auto strength = strengthOf (b, compound);

            if (strength > cutStrength)
            {
                cutStrength = strength;
                cut = b;
            }
        }

        if (cut == 0)
        {
            // Not a length the grid can produce; keep it as one piece rather than lose time.
            RhythmPiece p;
            p.on = position;
            p.ticks = length;
            p.dur = 64;
            out.push_back (p);
            return;
        }

        decomposeStraight (position, cut - position, compound, out);
        decomposeStraight (cut, position + length - cut, compound, out);
    }

    // What the written value of a tuplet piece is: three of them fit in the time of two.
    bool tupletValue (int64_t ticks, int& dur)
    {
        const auto written = ticks * 3 / 2;

        if (written * 2 != ticks * 3)
            return false;

        for (const auto& v : writtenValues)
        {
            if (v.dots == 0 && v.ticks == written && v.dur <= 32)
            {
                dur = v.dur;
                return true;
            }
        }

        return false;
    }

    int tripletUnitAt (int64_t beatStart, const std::vector<std::pair<int64_t, int>>& beats)
    {
        for (const auto& [start, unit] : beats)
            if (start == beatStart)
                return unit;

        return 0;
    }

    void decomposeFragment (int64_t position, int64_t length, bool compound, int64_t barLength,
                            const std::vector<std::pair<int64_t, int>>& tripletBeats, std::vector<RhythmPiece>& out)
    {
        const auto end = position + length;

        while (position < end)
        {
            const auto beatStart = position / ticksPerQuarter * ticksPerQuarter;
            const auto beatEnd = std::min<int64_t> (beatStart + ticksPerQuarter, barLength);
            const auto unit = tripletUnitAt (beatStart, tripletBeats);

            if (unit == 0)
            {
                // straight up to the next triplet beat
                auto stop = end;

                for (auto b = beatEnd; b < end; b += ticksPerQuarter)
                {
                    if (tripletUnitAt (b, tripletBeats) != 0)
                    {
                        stop = b;
                        break;
                    }
                }

                decomposeStraight (position, stop - position, compound, out);
                position = stop;
                continue;
            }

            // Whole triplet beats that are completely covered are written as ordinary values.
            if (position == beatStart && end - position >= beatEnd - beatStart && beatEnd - beatStart == ticksPerQuarter)
            {
                const auto beats = (end - position) / ticksPerQuarter;
                decomposeStraight (position, beats * ticksPerQuarter, compound, out);
                position += beats * ticksPerQuarter;
                continue;
            }

            const int64_t group = 3 * (int64_t) unit;
            const auto groupStart = beatStart + (position - beatStart) / group * group;
            const auto groupEnd = std::min (groupStart + group, beatEnd);
            const auto piece = std::min (end, groupEnd) - position;

            if (position == groupStart && piece == group)
            {
                // a whole group of three slots is an ordinary value
                decomposeStraight (position, piece, compound, out);
                position += piece;
                continue;
            }

            int dur = 0;
            RhythmPiece p;
            p.on = position;
            p.ticks = piece;

            if (tupletValue (piece, dur))
            {
                p.dur = dur;
                p.tuplet = true;
                p.groupStart = groupStart;
                p.groupTicks = group;
                out.push_back (p);
            }
            else
            {
                decomposeStraight (position, piece, compound, out);
            }

            position += piece;
        }
    }

    bool isSingleValue (int64_t length)
    {
        for (const auto& v : writtenValues)
            if (v.ticks == length)
                return true;

        return false;
    }
}

std::vector<int64_t> splitPointsFor (int num, int den, int64_t barLength)
{
    const auto nominal = den > 0 ? (int64_t) num * 4 * ticksPerQuarter / den : 0;

    if (nominal != barLength)
        return {};

    if (num == 4 && den == 4)
        return { 2 * ticksPerQuarter };

    if (isCompound (num, den))
    {
        std::vector<int64_t> points;

        for (int64_t p = 1440; p < barLength; p += 1440)
            points.push_back (p);

        return points;
    }

    return {};
}

std::vector<int64_t> beatGroups (int num, int den, int64_t barLength)
{
    const auto nominal = den > 0 ? (int64_t) num * 4 * ticksPerQuarter / den : 0;
    std::vector<int64_t> groups;

    if (nominal != barLength)
    {
        groups.push_back (barLength);
        return groups;
    }

    if (isCompound (num, den))
    {
        for (int i = 0; i < num / 3; ++i)
            groups.push_back (1440);
    }
    else if (den == 8)
    {
        // 3/8, 5/8, 7/8 ...: a group of three, then twos
        auto left = num;

        if (left >= 3)
        {
            groups.push_back (1440);
            left -= 3;
        }

        while (left > 0)
        {
            const auto n = std::min (left, 2);
            groups.push_back (n * 480);
            left -= n;
        }
    }
    else if (den == 2)
    {
        for (int i = 0; i < num; ++i)
            groups.push_back (1920);
    }
    else
    {
        for (int i = 0; i < num; ++i)
            groups.push_back (4 * ticksPerQuarter / std::max (1, den));
    }

    return groups;
}

std::vector<RhythmPiece> splitLength (int64_t on, int64_t length, int num, int den, int64_t barLength,
                                      const std::vector<std::pair<int64_t, int>>& tripletBeats, bool isRest)
{
    std::vector<RhythmPiece> pieces;

    if (length <= 0)
        return pieces;

    const auto compound = isCompound (num, den);
    const auto cuts = splitPointsFor (num, den, barLength);

    // A note that starts the bar and is a single value may run through the middle of it (a dotted
    // half in 4/4); a rest may do so only if it fills the bar.
    const auto exempt = isRest ? (on == 0 && length == barLength) : (on == 0 && isSingleValue (length));

    std::vector<int64_t> points;
    points.push_back (on);

    if (! exempt)
        for (const auto c : cuts)
            if (c > on && c < on + length)
                points.push_back (c);

    points.push_back (on + length);

    for (size_t i = 0; i + 1 < points.size(); ++i)
        decomposeFragment (points[i], points[i + 1] - points[i], compound, barLength, tripletBeats, pieces);

    if (! isRest)
        for (size_t i = 0; i + 1 < pieces.size(); ++i)
        {
            pieces[i].tiedToNext = true;
            pieces[i + 1].tiedFromPrevious = true;
        }

    return pieces;
}

}  // namespace trs
