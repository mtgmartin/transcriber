#pragma once

// Helpers for building the input of the transcription tests.

#include "core/Notation.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>

namespace tsupport
{
    struct N
    {
        int pitch;
        double start;   // quarter notes
        double dur;
        int vel = 90;
    };

    // A capture of notes played from beat 0, with 4/4 bars (or the given ones). `length` is the
    // length of the recording in quarter notes.
    inline trs::ResolvedCapture capture (std::initializer_list<N> notes, double length = -1.0,
                                         std::vector<trs::BarStart> bars = {}, double bpm = 120.0)
    {
        trs::ResolvedCapture rc;
        double end = 0.0;

        for (const auto& n : notes)
        {
            trs::ResolvedNote r;
            r.onPpq = n.start;
            r.offPpq = n.start + n.dur;
            r.pitch = n.pitch;
            r.velocity = n.vel;
            rc.notes.push_back (r);
            end = std::max (end, r.offPpq);
        }

        rc.lengthPpq = length > 0.0 ? length : std::ceil (end / 4.0) * 4.0;

        if (bars.empty())
            for (double b = 0.0; b < rc.lengthPpq - 1.0e-9; b += 4.0)
                bars.push_back ({ b, 4, 4 });

        rc.bars = std::move (bars);
        rc.tempoMap.push_back ({ 0.0, bpm });
        return rc;
    }

    // Pitches by name, e.g. p("C4") = 60, p("F#3") = 54, p("Bb4") = 70.
    inline int p (const char* name)
    {
        static const int base[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
        int i = 0;
        const int step = base[name[i++] - 'A'];
        int alter = 0;

        while (name[i] == '#' || name[i] == 'b')
            alter += name[i++] == '#' ? 1 : -1;

        return 12 * (std::atoi (name + i) + 1) + step + alter;
    }
}
