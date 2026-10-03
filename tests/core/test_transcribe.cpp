// Phase 4 tests: the transcription pipeline, stage by stage and as a whole.

#include "TestSupport.h"
#include "TranscribeSupport.h"

#include "MidiReader.h"
#include "core/Mei.h"
#include "core/Notation.h"
#include "XmlCheck.h"

#include <chrono>
#include <random>
#include <set>

using namespace trs;
using namespace tsupport;

#define REGISTER(fn, name) static testing::Registrar registrar_##fn (name, fn)

namespace
{
    ResolvedNote rn (int pitch, double on, double off)
    {
        ResolvedNote n;
        n.pitch = pitch;
        n.onPpq = on;
        n.offPpq = off;
        return n;
    }

    std::vector<Bar> fourFour (int count)
    {
        std::vector<Bar> bars;

        for (int i = 0; i < count; ++i)
            bars.push_back ({ i * 3840, 3840, 4, 4, false, false });

        return bars;
    }

    // "start/length" of every piece, with a tilde for a tie and a t for a tuplet, e.g. "0/960 960/960~".
    std::string describe (const std::vector<RhythmPiece>& pieces)
    {
        std::string s;

        for (const auto& p : pieces)
        {
            if (! s.empty())
                s += " ";

            s += std::to_string (p.on) + "/" + std::to_string (p.ticks) + ":" + std::to_string (p.dur) + std::string ((size_t) p.dots, '.');

            if (p.tuplet)
                s += "t";

            if (p.tiedToNext)
                s += "~";
        }

        return s;
    }

    //==========================================================================
    // Stage 1 and 2

    void testCleanNotes()
    {
        int dropped = 0;
        const auto out = cleanNotes ({ rn (60, 0, 1), rn (62, 1, 1), rn (64, 2, 3.0), rn (64, 2, 5.0), rn (65, 1, 1.0000001), rn (67, 4, 4) }, &dropped);

        CHECK_EQ ((int) out.size(), 2);   // three notes of no length go, the repeated E is one note
        CHECK_EQ (dropped, 4);
        CHECK_EQ (out[1].pitch, 64);
        CHECK_NEAR (out[1].offPpq, 5.0, 1.0e-9);   // the longer of the two
        CHECK_EQ (out[0].pitch, 60);
        CHECK (cleanNotes ({}).empty());
    }

    void testBuildBars()
    {
        // no bar lines from the host: 4/4 bars to the end
        {
            ResolvedCapture rc;
            const auto bars = buildBars (rc, 7680);
            CHECK_EQ ((int) bars.size(), 2);
            CHECK_EQ (bars[1].start, (int64_t) 3840);
            CHECK_EQ (bars[1].length, (int64_t) 3840);
        }

        // 4/4, 3/4, 4/4 from the host
        {
            auto rc = capture ({}, 11, { { 0, 4, 4 }, { 4, 3, 4 }, { 7, 4, 4 } });
            const auto bars = buildBars (rc, 11 * 960);
            CHECK_EQ ((int) bars.size(), 3);
            CHECK_EQ (bars[0].length, (int64_t) 3840);
            CHECK_EQ (bars[1].length, (int64_t) 2880);
            CHECK_EQ (bars[1].num, 3);
            CHECK_EQ (bars[2].start, (int64_t) 6720);
            CHECK (! bars[0].irregular && ! bars[1].irregular && ! bars[2].irregular);
        }

        // Live's short bar after a meter change: 4/4 that is only 1 beat long
        {
            auto rc = capture ({}, 8, { { 0, 4, 4 }, { 4, 4, 4 }, { 5, 3, 4 } });
            const auto bars = buildBars (rc, 8 * 960);
            CHECK (bars[1].irregular);
            CHECK_EQ (bars[1].length, (int64_t) 960);
            CHECK_EQ (bars[2].length, (int64_t) 2880);
            CHECK (bars.back().start + bars.back().length >= 8 * 960);
        }

        // time before the first bar line becomes a short bar
        {
            auto rc = capture ({}, 6, { { 1, 4, 4 }, { 5, 4, 4 } });
            const auto bars = buildBars (rc, 6 * 960);
            CHECK_EQ (bars[0].start, (int64_t) 0);
            CHECK_EQ (bars[0].length, (int64_t) 960);
            CHECK (bars[0].irregular);
        }

        // never empty, and no bars after the end
        {
            ResolvedCapture rc;
            CHECK_EQ ((int) buildBars (rc, 0).size(), 1);
            auto rc2 = capture ({}, 16);
            CHECK_EQ ((int) buildBars (rc2, 8 * 960).size(), 2);
        }
    }

    void testPickup()
    {
        auto bars = fourFour (2);
        applyPickup (bars, 2880);   // first note on beat 4
        CHECK (bars[0].pickup);
        CHECK_EQ (bars[0].start, (int64_t) 2880);
        CHECK_EQ (bars[0].length, (int64_t) 960);

        bars = fourFour (2);
        applyPickup (bars, 2400);   // beat 3 and a half: more than half the bar is empty
        CHECK_EQ (bars[0].length, (int64_t) 1920);
        CHECK (bars[0].pickup);

        bars = fourFour (2);
        applyPickup (bars, 1920);   // beat 3: exactly half, an ordinary bar that starts with a rest
        CHECK (! bars[0].pickup);

        bars = fourFour (2);
        applyPickup (bars, 960);    // beat 2: an ordinary bar that starts with a rest
        CHECK (! bars[0].pickup && bars[0].length == 3840);

        bars = fourFour (2);
        applyPickup (bars, 0);
        CHECK (! bars[0].pickup);

        bars = fourFour (2);
        applyPickup (bars, 3000);   // a little after beat 4 starts: the pickup begins at the beat
        CHECK_EQ (bars[0].start, (int64_t) 2880);

        bars = fourFour (2);
        bars[0].irregular = true;
        applyPickup (bars, 2880);
        CHECK (! bars[0].pickup);
    }

    //==========================================================================
    // Stage 3

    void testQuantizeStraight()
    {
        const auto bars = fourFour (2);
        TranscriptionSettings settings;

        // 16ths with a little human error
        const auto q = quantize ({ rn (60, 0.02, 0.26), rn (62, 0.24, 0.5), rn (64, 0.5, 0.76), rn (65, 0.73, 1.0), rn (67, 1.01, 2.0) }, bars, settings);
        CHECK_EQ ((int) q.notes.size(), 5);
        CHECK_EQ (q.offGrid, 0);
        const int64_t onsets[] = { 0, 240, 480, 720, 960 };
        const int64_t lengths[] = { 240, 240, 240, 240, 960 };

        for (size_t i = 0; i < 5; ++i)
        {
            CHECK_EQ (q.notes[i].on, onsets[i]);
            CHECK_EQ (q.notes[i].dur, lengths[i]);
            CHECK (! q.notes[i].tripletBeat);
        }

        CHECK_EQ (q.tripletBeats, 0);
    }

    void testQuantizeTriplets()
    {
        const auto bars = fourFour (1);
        TranscriptionSettings settings;

        // three eighth triplets, then straight eighths
        const auto q = quantize ({ rn (72, 0.0, 1.0 / 3), rn (74, 1.0 / 3, 2.0 / 3), rn (76, 2.0 / 3, 1.0),
                                   rn (77, 1.0, 1.5), rn (79, 1.5, 2.0) }, bars, settings);
        CHECK_EQ (q.tripletBeats, 1);
        CHECK_EQ (q.notes[1].on, (int64_t) 320);
        CHECK_EQ (q.notes[2].on, (int64_t) 640);
        CHECK_EQ (q.notes[1].dur, (int64_t) 320);
        CHECK (q.notes[0].tripletBeat && q.notes[2].tripletBeat);
        CHECK (! q.notes[3].tripletBeat);
        CHECK_EQ (q.notes[4].on, (int64_t) 1440);

        // a lone note a third into a beat is a triplet, a lone note a quarter in is a 16th
        const auto lone = quantize ({ rn (60, 1.0 / 3, 0.5), rn (62, 2.25, 2.5) }, bars, settings);
        CHECK_EQ (lone.notes[0].on, (int64_t) 320);
        CHECK_EQ (lone.notes[1].on, (int64_t) 2160);

        // triplets switched off: the same notes go to the straight grid
        settings.triplets = false;
        const auto off = quantize ({ rn (72, 0.0, 1.0 / 3), rn (74, 1.0 / 3, 2.0 / 3), rn (76, 2.0 / 3, 1.0) }, bars, settings);
        CHECK_EQ (off.tripletBeats, 0);
        CHECK_EQ (off.notes[1].on, (int64_t) 240);
        CHECK_EQ (off.notes[2].on, (int64_t) 720);

        // a quarter-note grid has no triplets either
        settings.triplets = true;
        settings.grid = 4;
        CHECK_EQ (tripletSlot (settings), 0);
    }

    void testQuantizeGridsAndEdges()
    {
        const auto bars = fourFour (2);
        TranscriptionSettings settings;

        // a note just before a bar line belongs to the next bar
        auto q = quantize ({ rn (60, 3.99, 4.5) }, bars, settings);
        CHECK_EQ (q.notes[0].on, (int64_t) 3840);

        // the grid setting
        settings.grid = 8;
        settings.triplets = false;
        CHECK_EQ (straightSlot (settings), 480);
        q = quantize ({ rn (60, 0.26, 0.5), rn (62, 0.76, 1.0) }, bars, settings);
        CHECK_EQ (q.notes[0].on, (int64_t) 480);   // 0.26 is nearer an eighth than a quarter-note beat
        CHECK_EQ (q.notes[1].on, (int64_t) 960);   // 0.76 is nearer 1.0 than 0.5

        // with triplets allowed the same lone note goes to the triplet position
        settings.triplets = true;
        q = quantize ({ rn (60, 0.26, 0.5) }, bars, settings);
        CHECK_EQ (q.notes[0].on, (int64_t) 320);
        settings.triplets = false;

        settings.grid = 32;
        settings.triplets = true;
        CHECK_EQ (straightSlot (settings), 120);
        CHECK_EQ (tripletSlot (settings), 80);
        q = quantize ({ rn (60, 0.126, 0.25) }, bars, settings);
        CHECK_EQ (q.notes[0].on, (int64_t) 120);

        settings.grid = 16;
        settings.triplets = true;

        // durations: legato to the next beat, staccato keeps one slot, and a long note
        q = quantize ({ rn (60, 0, 0.9), rn (62, 1, 1.1), rn (64, 2, 7.0) }, bars, settings);
        CHECK_EQ (q.notes[0].dur, (int64_t) 960);
        CHECK_EQ (q.notes[1].dur, (int64_t) 240);
        CHECK_EQ (q.notes[2].dur, (int64_t) 4800);

        // off-grid notes are flagged
        settings.triplets = false;
        q = quantize ({ rn (60, 0.115, 0.5), rn (62, 1.0, 1.5) }, bars, settings);
        settings.triplets = true;
        CHECK_EQ (q.offGrid, 1);
        CHECK (q.notes[0].offGrid && ! q.notes[1].offGrid);
        CHECK_EQ (q.notes[0].onError, -110);

        // the same pitch cannot sound twice at once
        q = quantize ({ rn (60, 0, 2), rn (60, 1, 2) }, bars, settings);
        CHECK_EQ ((int) q.notes.size(), 2);
        CHECK_EQ (q.notes[0].dur, (int64_t) 960);
        CHECK_EQ (q.notes[1].on, (int64_t) 960);

        CHECK (quantize ({}, bars, settings).notes.empty());
    }

    //==========================================================================
    // Stage 5

    void testSplitLength44()
    {
        const std::vector<std::pair<int64_t, int>> none;

        CHECK_STR (describe (splitLength (0, 3840, 4, 4, 3840, none, false)).c_str(), "0/3840:1");
        CHECK_STR (describe (splitLength (0, 3840, 4, 4, 3840, none, true)).c_str(), "0/3840:1");
        CHECK_STR (describe (splitLength (0, 1920, 4, 4, 3840, none, false)).c_str(), "0/1920:2");
        CHECK_STR (describe (splitLength (960, 960, 4, 4, 3840, none, false)).c_str(), "960/960:4");

        // through the middle of the bar: cut, and tie (a note) or not (a rest)
        CHECK_STR (describe (splitLength (960, 1920, 4, 4, 3840, none, false)).c_str(), "960/960:4~ 1920/960:4");
        CHECK_STR (describe (splitLength (960, 1920, 4, 4, 3840, none, true)).c_str(), "960/960:4 1920/960:4");

        // a dotted half from the first beat may cross the middle as a note, not as a rest
        CHECK_STR (describe (splitLength (0, 2880, 4, 4, 3840, none, false)).c_str(), "0/2880:2.");
        CHECK_STR (describe (splitLength (0, 2880, 4, 4, 3840, none, true)).c_str(), "0/1920:2 1920/960:4");

        // off the beat: an eighth then a quarter, never a dotted value that starts off the beat
        CHECK_STR (describe (splitLength (480, 1440, 4, 4, 3840, none, false)).c_str(), "480/480:8~ 960/960:4");
        CHECK_STR (describe (splitLength (1440, 1440, 4, 4, 3840, none, false)).c_str(), "1440/480:8~ 1920/960:4");
        CHECK_STR (describe (splitLength (3360, 480, 4, 4, 3840, none, false)).c_str(), "3360/480:8");
        CHECK_STR (describe (splitLength (0, 1440, 4, 4, 3840, none, false)).c_str(), "0/1440:4.");
        CHECK_STR (describe (splitLength (960, 1440, 4, 4, 3840, none, false)).c_str(), "960/960:4~ 1920/480:8");   // through the middle
        CHECK_STR (describe (splitLength (480, 960, 4, 4, 3840, none, false)).c_str(), "480/480:8~ 960/480:8");

        // five eighths
        CHECK_STR (describe (splitLength (0, 2400, 4, 4, 3840, none, false)).c_str(), "0/1920:2~ 1920/480:8");

        // a short value the grid can make
        CHECK_STR (describe (splitLength (0, 60, 4, 4, 3840, none, false)).c_str(), "0/60:64");
        CHECK_STR (describe (splitLength (120, 360, 4, 4, 3840, none, false)).c_str(), "120/120:32~ 240/240:16");
    }

    void testSplitLengthOtherMeters()
    {
        const std::vector<std::pair<int64_t, int>> none;

        // 3/4: no middle; a half note on beat 2 is cut at beat 3
        CHECK_STR (describe (splitLength (0, 2880, 3, 4, 2880, none, false)).c_str(), "0/2880:2.");
        CHECK_STR (describe (splitLength (960, 1920, 3, 4, 2880, none, false)).c_str(), "960/960:4~ 1920/960:4");

        // 6/8: cut at the dotted-quarter groups
        CHECK_STR (describe (splitLength (0, 1440, 6, 8, 2880, none, false)).c_str(), "0/1440:4.");
        CHECK_STR (describe (splitLength (1440, 1440, 6, 8, 2880, none, false)).c_str(), "1440/1440:4.");
        CHECK_STR (describe (splitLength (0, 2880, 6, 8, 2880, none, false)).c_str(), "0/2880:2.");
        CHECK_STR (describe (splitLength (480, 2400, 6, 8, 2880, none, false)).c_str(), "480/480:8~ 960/480:8~ 1440/1440:4.");
        CHECK_STR (describe (splitLength (480, 960, 6, 8, 2880, none, true)).c_str(), "480/480:8 960/480:8");

        // an irregular bar is not cut
        CHECK_STR (describe (splitLength (0, 960, 4, 4, 960, none, false)).c_str(), "0/960:4");
    }

    void testSplitLengthTriplets()
    {
        // a triplet beat with eighth-triplet slots (320)
        const std::vector<std::pair<int64_t, int>> t320 { { 0, 320 } };

        CHECK_STR (describe (splitLength (0, 320, 4, 4, 3840, t320, false)).c_str(), "0/320:8t");
        CHECK_STR (describe (splitLength (320, 320, 4, 4, 3840, t320, false)).c_str(), "320/320:8t");
        CHECK_STR (describe (splitLength (0, 640, 4, 4, 3840, t320, false)).c_str(), "0/640:4t");
        CHECK_STR (describe (splitLength (320, 640, 4, 4, 3840, t320, false)).c_str(), "320/640:4t");
        CHECK_STR (describe (splitLength (640, 320, 4, 4, 3840, t320, true)).c_str(), "640/320:8t");

        // a note that fills the beat is an ordinary quarter
        CHECK_STR (describe (splitLength (0, 960, 4, 4, 3840, t320, false)).c_str(), "0/960:4");
        CHECK_STR (describe (splitLength (0, 1920, 4, 4, 3840, t320, false)).c_str(), "0/1920:2");

        // a note from the triplet beat into a straight one: the beat as a quarter, the rest straight
        CHECK_STR (describe (splitLength (0, 1440, 4, 4, 3840, t320, false)).c_str(), "0/960:4~ 960/480:8");

        // a triplet that starts late in the beat and runs into the next beat (which then is a triplet beat too)
        const std::vector<std::pair<int64_t, int>> twoBeats { { 0, 320 }, { 960, 320 } };
        CHECK_STR (describe (splitLength (640, 640, 4, 4, 3840, twoBeats, false)).c_str(), "640/320:8t~ 960/320:8t");

        // sixteenth-triplet slots (160): two groups of three in the beat
        const std::vector<std::pair<int64_t, int>> t160 { { 0, 160 } };
        CHECK_STR (describe (splitLength (0, 160, 4, 4, 3840, t160, false)).c_str(), "0/160:16t");
        CHECK_STR (describe (splitLength (160, 320, 4, 4, 3840, t160, false)).c_str(), "160/320:8t");
        CHECK_STR (describe (splitLength (0, 480, 4, 4, 3840, t160, false)).c_str(), "0/480:8");
        CHECK_STR (describe (splitLength (480, 160, 4, 4, 3840, t160, false)).c_str(), "480/160:16t");

        // the group of a tuplet piece
        const auto pieces = splitLength (160, 320, 4, 4, 3840, t160, false);
        CHECK_EQ ((int) pieces.size(), 1);
        CHECK_EQ (pieces[0].groupStart, (int64_t) 0);
        CHECK_EQ (pieces[0].groupTicks, (int64_t) 480);
    }

    //==========================================================================
    // Phase 8c: grids finer than a 32nd (1/64 and 1/128)

    void testFineGrids()
    {
        const std::vector<std::pair<int64_t, int>> none;

        // written values down to a 128th (30 ticks); the strongest beat inside a length is where it is cut
        CHECK_STR (describe (splitLength (0, 30, 4, 4, 3840, none, false)).c_str(), "0/30:128");
        CHECK_STR (describe (splitLength (30, 30, 4, 4, 3840, none, false)).c_str(), "30/30:128");
        CHECK_STR (describe (splitLength (0, 45, 4, 4, 3840, none, false)).c_str(), "0/45:128.");
        CHECK_STR (describe (splitLength (30, 60, 4, 4, 3840, none, false)).c_str(), "30/30:128~ 60/30:128");
        CHECK_STR (describe (splitLength (60, 90, 4, 4, 3840, none, false)).c_str(), "60/90:64.");
        CHECK_STR (describe (splitLength (90, 150, 4, 4, 3840, none, false)).c_str(), "90/30:128~ 120/120:32");
        CHECK_STR (describe (splitLength (0, 30, 4, 4, 3840, none, true)).c_str(), "0/30:128");

        // triplets of 64ths (40 ticks) and 128ths (20)
        const std::vector<std::pair<int64_t, int>> t40 { { 0, 40 } };
        CHECK_STR (describe (splitLength (0, 40, 4, 4, 3840, t40, false)).c_str(), "0/40:64t");
        CHECK_STR (describe (splitLength (40, 80, 4, 4, 3840, t40, false)).c_str(), "40/80:32t");
        const std::vector<std::pair<int64_t, int>> t20 { { 0, 20 } };
        CHECK_STR (describe (splitLength (0, 20, 4, 4, 3840, t20, false)).c_str(), "0/20:128t");
        CHECK_STR (describe (splitLength (20, 40, 4, 4, 3840, t20, false)).c_str(), "20/40:64t");

        // the slots of the grids
        TranscriptionSettings s;
        s.grid = 64;
        CHECK_EQ (straightSlot (s), 60);
        CHECK_EQ (tripletSlot (s), 40);
        s.grid = 128;
        CHECK_EQ (straightSlot (s), 30);
        CHECK_EQ (tripletSlot (s), 20);

        // a note a 64th into the beat: a 32nd grid puts it on a 32nd, the finer grids keep it
        const auto bars = fourFour (2);
        s.triplets = false;
        s.grid = 32;
        CHECK_EQ (quantize ({ rn (60, 0.0625, 0.125) }, bars, s).notes[0].on, (int64_t) 120);
        s.grid = 64;
        auto q = quantize ({ rn (60, 0.0625, 0.125) }, bars, s);
        CHECK_EQ (q.notes[0].on, (int64_t) 60);
        CHECK_EQ (q.notes[0].dur, (int64_t) 60);
        s.grid = 128;
        q = quantize ({ rn (60, 0.03125, 0.0625) }, bars, s);
        CHECK_EQ (q.notes[0].on, (int64_t) 30);
        CHECK_EQ (q.notes[0].dur, (int64_t) 30);

        // triplets of 64ths in a beat
        s.grid = 64;
        s.triplets = true;
        q = quantize ({ rn (60, 0.0, 1.0 / 24), rn (62, 1.0 / 24, 2.0 / 24), rn (64, 2.0 / 24, 3.0 / 24), rn (65, 1.0, 2.0) }, bars, s);
        CHECK_EQ (q.tripletBeats, 1);
        CHECK_EQ (q.notes[1].on, (int64_t) 40);
        CHECK_EQ (q.notes[2].on, (int64_t) 80);
        CHECK_EQ (q.notes[1].dur, (int64_t) 40);

        // a run of 64ths is written as 64ths, in the score, in the MEI and in the sentence of a click
        const int scale[8] = { 60, 62, 64, 65, 67, 69, 71, 72 };
        std::vector<ResolvedNote> run;
        for (int i = 0; i < 8; ++i)
            run.push_back (rn (scale[i], i * 0.0625, (i + 1) * 0.0625));

        ResolvedCapture rc = capture ({}, 4.0);
        rc.notes = run;
        s.triplets = false;
        const auto result = transcribePiano (rc, s);
        CHECK (dumpScore (result.score).find ("C4/64 D4/64 E4/64 F4/64 G4/64 A4/64 B4/64 C5/64") != std::string::npos);
        CHECK (scoreToMei (result.score, {}).find ("dur=\"64\"") != std::string::npos);
        CHECK_EQ ((int) result.report.warnings.size(), 1);   // a whole take of 64ths: is the grid too fine?

        // the same run on a 128th grid with 128th notes
        ResolvedCapture fine = capture ({}, 4.0);
        for (int i = 0; i < 8; ++i)
            fine.notes.push_back (rn (scale[i], i * 0.03125, (i + 1) * 0.03125));

        s.grid = 128;
        const auto finest = transcribePiano (fine, s);
        CHECK (dumpScore (finest.score).find ("C4/128 D4/128 E4/128 F4/128 G4/128 A4/128 B4/128 C5/128") != std::string::npos);
        CHECK (scoreToMei (finest.score, {}).find ("dur=\"128\"") != std::string::npos);
        CHECK (xmlcheck::checkXml (scoreToMei (finest.score, {})).wellFormed);

        // a grid this fine for notes played a little unevenly: the report asks whether it is too fine
        ResolvedCapture loose = capture ({}, 4.0);
        for (int i = 0; i < 10; ++i)
            loose.notes.push_back (rn (60, i * 0.5 + 0.0625 * (double) (i % 3), i * 0.5 + 0.0625 * (double) (i % 3) + 0.0625));

        s.grid = 64;
        bool asked = false;
        for (const auto& w : transcribePiano (loose, s).report.warnings)
            asked = asked || w.find ("too fine") != std::string::npos;
        CHECK (asked);
        s.grid = 16;
        for (const auto& w : transcribePiano (loose, s).report.warnings)
            CHECK (w.find ("too fine") == std::string::npos);

        // the setting as JSON: 64 and 128 are kept, anything else is brought back
        for (const int grid : { 64, 128 })
        {
            TranscriptionSettings custom;
            custom.grid = grid;
            CHECK_EQ (TranscriptionSettings::fromJson (custom.toJson()).grid, grid);
        }

        auto bad = Json::object();
        bad.set ("grid", 100);
        CHECK_EQ (TranscriptionSettings::fromJson (bad).grid, 16);
    }

    //==========================================================================
    // Phase 8e: transposition for synths that transpose

    ResolvedCapture shifted (ResolvedCapture rc, int semitones)
    {
        for (auto& n : rc.notes)
            n.pitch += semitones;

        return rc;
    }

    void testTranspose()
    {
        // a C major tune with a chord and a left hand
        const auto tune = capture ({ { 60, 0, 1 }, { 62, 1, 1 }, { 64, 2, 1 }, { 67, 3, 1 }, { 64, 4, 2 }, { 72, 4, 2 }, { 48, 0, 2 }, { 55, 2, 2 }, { 50, 4, 4 } }, 8.0);

        // the score of the transposed take is the score of a take played that many semitones higher, for every instrument that has pitches
        for (const int k : { 2, 7, 12, -5, -12, 1 })
        {
            TranscriptionSettings s;
            s.transpose = k;
            CHECK_STR (dumpScore (transcribePiano (tune, s).score).c_str(), dumpScore (transcribePiano (shifted (tune, k), {}).score).c_str());

            // (guitar and bass: notes that can be played at both pitches)
            const auto riff = capture ({ { 52, 0, 1 }, { 55, 1, 1 }, { 57, 2, 1 }, { 59, 3, 1 }, { 57, 4, 2 }, { 62, 4, 2 } }, 8.0);
            CHECK_STR (dumpScore (transcribeFretted (riff, s, InstrumentType::guitar).score).c_str(),
                       dumpScore (transcribeFretted (shifted (riff, k), {}, InstrumentType::guitar).score).c_str());
            const auto line = capture ({ { 45, 0, 1 }, { 48, 1, 1 }, { 50, 2, 1 }, { 52, 3, 1 } }, 4.0);
            CHECK_STR (dumpScore (transcribeFretted (line, s, InstrumentType::bass).score).c_str(),
                       dumpScore (transcribeFretted (shifted (line, k), {}, InstrumentType::bass).score).c_str());
        }

        // the key follows: C major played two semitones up is D major
        TranscriptionSettings up;
        up.transpose = 2;
        const auto scale = capture ({ { 60, 0, 1 }, { 62, 1, 1 }, { 64, 2, 1 }, { 65, 3, 1 }, { 67, 4, 1 }, { 69, 5, 1 }, { 71, 6, 1 }, { 72, 7, 1 } }, 8.0);
        CHECK_EQ (transcribePiano (scale, up).key.fifths, 2);
        CHECK_EQ (transcribePiano (scale, {}).key.fifths, 0);

        // notes pushed out of 0-127 are left out and counted; the others stay
        TranscriptionSettings high;
        high.transpose = 10;
        const auto edge = transcribePiano (capture ({ { 60, 0, 1 }, { 120, 1, 1 }, { 124, 2, 1 } }, 4.0), high);
        CHECK_EQ (edge.report.notes, 1);
        bool told = false;
        for (const auto& w : edge.report.warnings)
            told = told || (w.find ("2 notes were out of range after transposing by 10") != std::string::npos);
        CHECK (told);

        // drums are not transposed
        const auto groove = capture ({ { 36, 0, 0.25 }, { 38, 1, 0.25 }, { 42, 2, 0.25 } }, 4.0);
        TranscriptionSettings seven;
        seven.transpose = 7;
        CHECK_STR (dumpScore (transcribeDrums (groove, seven, drumPreset ("gm")).score).c_str(), dumpScore (transcribeDrums (groove, {}, drumPreset ("gm")).score).c_str());

        // the setting as JSON; old files have none, and too much is brought back
        TranscriptionSettings custom;
        custom.transpose = -7;
        CHECK_EQ (TranscriptionSettings::fromJson (custom.toJson()).transpose, -7);
        CHECK (TranscriptionSettings::fromJson (custom.toJson()) == custom);
        CHECK_EQ (TranscriptionSettings::fromJson (Json()).transpose, 0);
        auto wild = Json::object();
        wild.set ("transpose", 100);
        CHECK_EQ (TranscriptionSettings::fromJson (wild).transpose, 48);
        wild.set ("transpose", -100);
        CHECK_EQ (TranscriptionSettings::fromJson (wild).transpose, -48);
        CHECK (! (TranscriptionSettings() == custom));
    }

    //==========================================================================
    // Phase 9: ten minute songs and dense MIDI through every pipeline

    void testTenMinuteSongs()
    {
        std::mt19937 rng (41);
        const auto seconds = [] (std::chrono::steady_clock::time_point since) { return std::chrono::duration<double> (std::chrono::steady_clock::now() - since).count(); };

        // 10 minutes at 120 bpm: 300 bars of 4/4
        std::vector<N> piano, riff, line, kit;

        for (int beat = 0; beat < 1200; ++beat)
        {
            const double b = beat;
            piano.push_back ({ 60 + (int) (rng() % 14), b, 0.5 });
            piano.push_back ({ 64 + (int) (rng() % 14), b + 0.5, 0.5 });
            piano.push_back ({ 40 + (int) (rng() % 12), b, 0.9 });
            riff.push_back ({ 40 + (int) (rng() % 36), b, 0.5 });
            riff.push_back ({ 40 + (int) (rng() % 36), b + 0.5, 0.5 });
            line.push_back ({ 28 + (int) (rng() % 26), b, 0.95 });
            kit.push_back ({ 42, b, 0.2 });
            kit.push_back ({ 42, b + 0.5, 0.2 });

            if (beat % 2 == 0)
                kit.push_back ({ 36, b, 0.2 });
            else
                kit.push_back ({ 38, b, 0.2 });
        }

        const auto take = [] (const std::vector<N>& notes)
        {
            std::vector<N> copy = notes;
            ResolvedCapture rc;

            for (const auto& n : copy)
            {
                ResolvedNote r;
                r.onPpq = n.start;
                r.offPpq = n.start + n.dur;
                r.pitch = n.pitch;
                r.velocity = n.vel;
                rc.notes.push_back (r);
            }

            rc.lengthPpq = 1200.0;

            for (double b = 0.0; b < 1200.0 - 1.0e-9; b += 4.0)
                rc.bars.push_back ({ b, 4, 4 });

            rc.tempoMap.push_back ({ 0.0, 120.0 });
            return rc;
        };

        const auto started = std::chrono::steady_clock::now();
        auto t = std::chrono::steady_clock::now();
        const auto pianoScore = transcribePiano (take (piano), {});
        std::printf ("    10 minutes of piano (%d notes): %.1f s, %d measures\n", pianoScore.report.notes, seconds (t), pianoScore.report.measures);
        CHECK_EQ (pianoScore.report.measures, 300);
        CHECK (pianoScore.score.validate().empty());
        CHECK (xmlcheck::checkXml (scoreToMei (pianoScore.score, {})).wellFormed);

        t = std::chrono::steady_clock::now();
        const auto guitar = transcribeFretted (take (riff), {}, InstrumentType::guitar);
        std::printf ("    10 minutes of guitar: %.1f s\n", seconds (t));
        CHECK_EQ (guitar.report.measures, 300);
        CHECK (guitar.score.validate().empty());

        t = std::chrono::steady_clock::now();
        const auto bass = transcribeFretted (take (line), {}, InstrumentType::bass);
        std::printf ("    10 minutes of bass: %.1f s\n", seconds (t));
        CHECK (bass.score.validate().empty());

        t = std::chrono::steady_clock::now();
        const auto drums = transcribeDrums (take (kit), {}, drumPreset ("gm"));
        std::printf ("    10 minutes of drums: %.1f s\n", seconds (t));
        CHECK_EQ (drums.report.measures, 300);
        CHECK (drums.score.validate().empty());
        CHECK (seconds (started) < 120.0);

        // dense MIDI: a hundred notes in every beat of two minutes, as every instrument
        std::vector<N> dense;

        for (int beat = 0; beat < 240; ++beat)
            for (int k = 0; k < 100; ++k)
                dense.push_back ({ 30 + (int) (rng() % 70), beat + (double) (rng() % 1000) / 1000.0, 0.02 + (double) (rng() % 400) / 1000.0, 1 + (int) (rng() % 127) });

        auto crowd = take (dense);
        crowd.lengthPpq = 240.0;
        crowd.bars.resize (60);
        t = std::chrono::steady_clock::now();
        const auto d1 = transcribePiano (crowd, {});
        const auto d2 = transcribeFretted (crowd, {}, InstrumentType::guitar);
        const auto d3 = transcribeDrums (crowd, {}, drumPreset ("gm"));
        std::printf ("    dense MIDI (24000 notes) as piano, guitar and drums: %.1f s\n", seconds (t));
        CHECK (d1.score.validate().empty() && d2.score.validate().empty() && d3.score.validate().empty());
        CHECK (xmlcheck::checkXml (scoreToMei (d1.score, {})).wellFormed);
        CHECK (xmlcheck::checkXml (scoreToMei (d2.score, {})).wellFormed);
        CHECK (xmlcheck::checkXml (scoreToMei (d3.score, {})).wellFormed);
        CHECK (seconds (t) < 120.0);
    }

    void testBeatGroups()
    {
        auto joined = [] (std::vector<int64_t> v)
        {
            std::string s;

            for (const auto x : v)
                s += (s.empty() ? "" : " ") + std::to_string (x);

            return s;
        };

        CHECK_STR (joined (beatGroups (4, 4, 3840)).c_str(), "960 960 960 960");
        CHECK_STR (joined (beatGroups (3, 4, 2880)).c_str(), "960 960 960");
        CHECK_STR (joined (beatGroups (6, 8, 2880)).c_str(), "1440 1440");
        CHECK_STR (joined (beatGroups (9, 8, 4320)).c_str(), "1440 1440 1440");
        CHECK_STR (joined (beatGroups (3, 8, 1440)).c_str(), "1440");
        CHECK_STR (joined (beatGroups (5, 8, 2400)).c_str(), "1440 960");
        CHECK_STR (joined (beatGroups (7, 8, 3360)).c_str(), "1440 960 960");
        CHECK_STR (joined (beatGroups (2, 2, 3840)).c_str(), "1920 1920");
        CHECK_STR (joined (beatGroups (4, 4, 960)).c_str(), "960");

        CHECK_EQ ((int) splitPointsFor (4, 4, 3840).size(), 1);
        CHECK_EQ (splitPointsFor (4, 4, 3840)[0], (int64_t) 1920);
        CHECK (splitPointsFor (3, 4, 2880).empty());
        CHECK_EQ ((int) splitPointsFor (12, 8, 5760).size(), 3);
        CHECK (splitPointsFor (4, 4, 960).empty());
    }

    //==========================================================================
    // Stage 6 and 7

    std::vector<QNote> qnotes (std::vector<int> pitches, int64_t dur = 960)
    {
        std::vector<QNote> out;

        for (const auto pch : pitches)
        {
            QNote q;
            q.pitch = pch;
            q.dur = dur;
            out.push_back (q);
        }

        return out;
    }

    void testKeyDetection()
    {
        struct Case { std::vector<int> pitches; int tonic; bool minor; int fifths; };

        const Case cases[] = {
            { { 60, 62, 64, 65, 67, 69, 71, 72 }, 0, false, 0 },        // C major scale
            { { 67, 69, 71, 72, 74, 76, 78, 79 }, 7, false, 1 },        // G major
            { { 63, 65, 67, 68, 70, 72, 74, 75 }, 3, false, -3 },       // E flat major
            { { 66, 68, 70, 71, 73, 75, 77, 78 }, 6, false, 6 },        // F sharp major
            { { 69, 71, 72, 74, 76, 77, 80, 81 }, 9, true, 0 },         // A harmonic minor
            { { 60, 64, 67, 60, 64, 67, 72, 67, 64 }, 0, false, 0 },    // a C major arpeggio
        };

        for (const auto& c : cases)
        {
            const auto k = detectKey (qnotes (c.pitches));
            CHECK_EQ (k.tonic, c.tonic);
            CHECK_EQ ((int) k.minor, (int) c.minor);
            CHECK_EQ (k.fifths, c.fifths);
        }

        // longer notes count for more: a long C against short everything else
        auto weighted = qnotes ({ 62, 64, 65, 67, 69, 71 }, 120);
        weighted.push_back (qnotes ({ 60 }, 7000).front());
        CHECK_EQ (detectKey (weighted).tonic, 0);

        // nothing to go on
        CHECK_EQ (detectKey ({}).tonic, 0);
        CHECK_NEAR (detectKey ({}).correlation, 0.0, 1.0e-9);
        CHECK (detectKey (qnotes ({ 60 })).correlation > -1.0);
    }

    void testKeySignatures()
    {
        struct Case { int tonic; bool minor; bool flats; int fifths; };

        const Case cases[] = {
            { 0, false, false, 0 },  { 7, false, false, 1 },  { 2, false, false, 2 },  { 9, false, false, 3 },
            { 4, false, false, 4 },  { 11, false, false, 5 }, { 6, false, false, 6 },  { 6, false, true, -6 },
            { 1, false, false, -5 }, { 8, false, false, -4 }, { 3, false, false, -3 }, { 10, false, false, -2 },
            { 5, false, false, -1 }, { 9, true, false, 0 },   { 4, true, false, 1 },   { 11, true, false, 2 },
            { 2, true, false, -1 },  { 7, true, false, -2 },  { 0, true, false, -3 },  { 6, true, false, 3 },
            { 1, true, false, 4 },   { 3, true, false, 6 },   { 3, true, true, -6 },   { 8, true, false, 5 },
        };

        for (const auto& c : cases)
            CHECK_EQ (fifthsForKey (c.tonic, c.minor, c.flats), c.fifths);

        // letters of the tonic: 0 = A ... 6 = G
        CHECK_EQ (tonicLetterForKey (0, false), 2);    // C
        CHECK_EQ (tonicLetterForKey (0, true), 0);     // A minor
        CHECK_EQ (tonicLetterForKey (-4, false), 0);   // A flat major
        CHECK_EQ (tonicLetterForKey (-5, false), 3);   // D flat
        CHECK_EQ (tonicLetterForKey (6, false), 5);    // F sharp
        CHECK_EQ (tonicLetterForKey (-6, false), 6);   // G flat
        CHECK_EQ (tonicLetterForKey (1, false), 6);    // G
        CHECK_EQ (tonicLetterForKey (-3, true), 2);    // C minor
    }

    std::string spelled (const std::vector<int>& pitches, int tonic = -1, int letter = 0)
    {
        std::vector<int64_t> onsets;

        for (size_t i = 0; i < pitches.size(); ++i)
            onsets.push_back ((int64_t) i * 960);

        std::string out;

        for (const auto& s : spellPitches (onsets, pitches, tonic, letter))
        {
            if (! out.empty())
                out += " ";

            out += s.step;
            out += std::string ((size_t) std::max (0, s.alter), '#') + std::string ((size_t) std::max (0, -s.alter), 'b');
            out += std::to_string (s.octave);
        }

        return out;
    }

    void testSpelling()
    {
        CHECK_STR (spelled ({ 60, 62, 64, 65, 67, 69, 71, 72 }).c_str(), "C4 D4 E4 F4 G4 A4 B4 C5");
        CHECK_STR (spelled ({ 67, 69, 71, 72, 74, 76, 78, 79 }).c_str(), "G4 A4 B4 C5 D5 E5 F#5 G5");
        CHECK_STR (spelled ({ 65, 67, 69, 70, 72, 74, 76, 77 }).c_str(), "F4 G4 A4 Bb4 C5 D5 E5 F5");
        CHECK_STR (spelled ({ 63, 65, 67, 68, 70, 72, 74, 75 }).c_str(), "Eb4 F4 G4 Ab4 Bb4 C5 D5 Eb5");
        CHECK_STR (spelled ({ 62, 64, 66, 67, 69, 71, 73, 74 }).c_str(), "D4 E4 F#4 G4 A4 B4 C#5 D5");
        CHECK_STR (spelled ({ 64, 66, 68, 69, 71, 73, 75, 76 }).c_str(), "E4 F#4 G#4 A4 B4 C#5 D#5 E5");
        CHECK_STR (spelled ({ 69, 71, 72, 74, 76, 77, 80, 81 }).c_str(), "A4 B4 C5 D5 E5 F5 G#5 A5");
        CHECK_STR (spelled ({ 60, 64, 67, 70 }).c_str(), "C4 E4 G4 Bb4");   // a dominant seventh
        CHECK_STR (spelled ({ 58, 62, 65, 70 }).c_str(), "Bb3 D4 F4 Bb4");
        CHECK_STR (spelled ({ 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72 }).c_str(),
                   "C4 Db4 D4 Eb4 E4 F4 F#4 G4 Ab4 A4 Bb4 B4 C5");

        // pitches with the same name in different octaves
        CHECK_STR (spelled ({ 48, 60, 72 }).c_str(), "C3 C4 C5");
        CHECK_STR (spelled ({ 21, 108 }).c_str(), "A0 C8");

        // starting from the tonic of the key fixes a flat key that begins on its tonic
        CHECK_STR (spelled ({ 68, 70, 72, 73, 75, 77, 79, 80 }).c_str(), "G#4 A#4 B#4 C#5 D#5 E#5 F##5 G#5");   // without
        CHECK_STR (spelled ({ 68, 70, 72, 73, 75, 77, 79, 80 }, 8, 0).c_str(), "Ab4 Bb4 C5 Db5 Eb5 F5 G5 Ab5");
        CHECK_STR (spelled ({ 61, 65, 68, 73 }, 1, 3).c_str(), "Db4 F4 Ab4 Db5");

        // a tie between two spellings goes to the one with fewer sharps or flats (G, not F double sharp)
        CHECK_STR (spelled ({ 61, 64, 67, 70 }, 1, 2).c_str(), "C#4 E4 G4 A#4");

        CHECK (spellPitches ({}, {}).empty());
        CHECK_EQ ((int) spellPitches ({ 0 }, { 60 }).size(), 1);
    }

    //==========================================================================
    // Stage 4

    void testPianoVoices()
    {
        TranscriptionSettings settings;

        auto note = [] (int pitch, int64_t on, int64_t dur)
        {
            QNote q;
            q.pitch = pitch;
            q.on = on;
            q.dur = dur;
            return q;
        };

        // by pitch
        {
            const std::vector<QNote> notes { note (40, 0, 960), note (72, 0, 960) };
            const auto staves = assignPianoVoices (notes, settings);
            CHECK_EQ ((int) staves.size(), 2);
            CHECK_EQ ((int) staves[0].voices.size(), 1);
            CHECK_EQ (staves[0].voices[0][0].notes[0], 1);
            CHECK_EQ (staves[1].voices[0][0].notes[0], 0);
        }

        // a chord is one event, low to high
        {
            const std::vector<QNote> notes { note (60, 0, 960), note (64, 0, 960), note (67, 0, 960) };
            const auto staves = assignPianoVoices (notes, settings);
            CHECK_EQ ((int) staves[0].voices[0].size(), 1);
            CHECK_EQ ((int) staves[0].voices[0][0].notes.size(), 3);
            CHECK_EQ (notes[(size_t) staves[0].voices[0][0].notes[2]].pitch, 67);
        }

        // a melody that dips below middle C stays with the right hand
        {
            const std::vector<QNote> notes { note (36, 0, 3840), note (64, 0, 960), note (62, 960, 960), note (60, 1920, 960),
                                             note (59, 2880, 960), note (57, 3840, 960) };
            const auto staves = assignPianoVoices (notes, settings);
            int rightHand = 0;

            for (const auto& e : staves[0].voices[0])
                rightHand += (int) e.notes.size();

            CHECK_EQ (rightHand, 5);   // all five melody notes
        }

        // with no hand nearby, the split point decides
        {
            const std::vector<QNote> notes { note (59, 0, 960), note (60, 4000, 960) };
            const auto staves = assignPianoVoices (notes, settings);
            CHECK_EQ ((int) staves[1].voices[0].size(), 1);
            CHECK_EQ ((int) staves[0].voices[0].size(), 1);
        }

        // the split point can be moved
        {
            settings.splitPoint = 64;
            const std::vector<QNote> notes { note (62, 0, 960) };
            const auto staves = assignPianoVoices (notes, settings);
            CHECK (staves[0].voices.empty());
            CHECK_EQ ((int) staves[1].voices.size(), 1);
            settings.splitPoint = 60;
        }

        // a long note and a moving line: two voices in one hand, the top line first
        {
            const std::vector<QNote> notes { note (72, 0, 3840), note (64, 0, 960), note (65, 960, 960), note (67, 1920, 960) };
            const auto staves = assignPianoVoices (notes, settings);
            CHECK_EQ ((int) staves[0].voices.size(), 2);
            CHECK_EQ ((int) staves[0].voices[0].size(), 1);
            CHECK_EQ ((int) staves[0].voices[1].size(), 3);
        }

        // later notes use the first free voice
        {
            const std::vector<QNote> notes { note (72, 0, 960), note (74, 960, 960), note (76, 1920, 960) };
            CHECK_EQ ((int) assignPianoVoices (notes, settings)[0].voices.size(), 1);
        }

        CHECK (assignPianoVoices ({}, settings)[0].voices.empty());
    }

    //==========================================================================
    // Tempo marks

    void testTempoMarks()
    {
        auto marks = [] (std::vector<TempoPoint> points, double length)
        {
            ResolvedCapture rc;
            rc.lengthPpq = length;
            rc.tempoMap = std::move (points);

            std::string s;

            for (const auto& m : tempoMarks (rc))
            {
                if (! s.empty())
                    s += " | ";

                s += std::to_string (m.tick) + ":" + (m.bpm > 0 ? std::to_string (m.bpm) : "") + m.text;
            }

            return s;
        };

        CHECK_STR (marks ({}, 8).c_str(), "");
        CHECK_STR (marks ({ { 0, 120 } }, 8).c_str(), "0:120");
        CHECK_STR (marks ({ { 0, 127.4 } }, 8).c_str(), "0:127");

        // a jump that holds
        CHECK_STR (marks ({ { 0, 120 }, { 8, 90 } }, 16).c_str(), "0:120 | 7680:90");

        // a jump that does not hold for a beat is not marked
        CHECK_STR (marks ({ { 0, 120 }, { 8, 90 }, { 8.5, 120 } }, 16).c_str(), "0:120");

        // a small wobble is ignored
        CHECK_STR (marks ({ { 0, 120 }, { 4, 120.3 }, { 8, 119.8 } }, 16).c_str(), "0:120");

        // a ramp up: accel. at its start, the new tempo where it holds
        CHECK_STR (marks ({ { 0, 120 }, { 4, 120 }, { 4.1, 121 }, { 4.2, 123 }, { 4.3, 125 }, { 4.5, 130 }, { 5, 140 }, { 6, 150 },
                            { 6.2, 150 }, { 7, 150 } }, 12).c_str(),
                   "0:120 | 3840:accel. | 5760:150");

        // a ramp down
        CHECK_STR (marks ({ { 0, 140 }, { 2, 140 }, { 2.2, 138 }, { 2.6, 130 }, { 3, 120 }, { 3.4, 100 }, { 4, 90 }, { 5, 90 } }, 9).c_str(),
                   "0:140 | 1920:rit. | 3840:90");

        // a ramp that runs to the end of the recording: the text only
        CHECK_STR (marks ({ { 0, 100 }, { 4, 100 }, { 4.5, 110 }, { 5, 120 }, { 5.5, 130 }, { 6, 140 } }, 6).c_str(),
                   "0:100 | 3840:accel.");

        // marks sit on a sixteenth
        CHECK_STR (marks ({ { 0, 120 }, { 8.1, 90 } }, 16).c_str(), "0:120 | 7680:90");
    }

    //==========================================================================
    // The whole pipeline

    std::string transcribed (const ResolvedCapture& rc, TranscriptionSettings settings = {}, std::string* problems = nullptr)
    {
        const auto result = transcribePiano (rc, settings);

        if (problems != nullptr)
            *problems = result.score.validate();

        return dumpScore (result.score);
    }

    void expectDump (const char* name, const ResolvedCapture& rc, const std::string& expected, TranscriptionSettings settings = {})
    {
        std::string problem;
        const auto actual = transcribed (rc, settings, &problem);
        const bool same = actual == expected;
        testing::report (same, __FILE__, __LINE__, std::string (name) + ": the score differs");

        if (! same)
            std::printf ("--- expected\n%s--- actual\n%s---\n", expected.c_str(), actual.c_str());

        testing::report (problem.empty(), __FILE__, __LINE__, std::string (name) + ": invalid score: " + problem);
    }

    void testGoldenScales()
    {
        expectDump ("quarter notes", capture ({ { 60, 0, 1 }, { 62, 1, 1 }, { 64, 2, 1 }, { 65, 3, 1 }, { 67, 4, 1 }, { 69, 5, 1 }, { 71, 6, 1 }, { 72, 7, 1 } }),
                    "score key=0 major\n"
                    "m1 4/4\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: C4/4 D4/4 E4/4 F4/4\n"
                    "  S2 v1: R\n"
                    "m2 4/4\n"
                    "  S1 v1: G4/4 A4/4 B4/4 C5/4\n"
                    "  S2 v1: R\n");

        expectDump ("eighth notes are beamed by the beat",
                    capture ({ { 60, 0, .5 }, { 62, .5, .5 }, { 64, 1, .5 }, { 65, 1.5, .5 }, { 67, 2, .5 }, { 69, 2.5, .5 }, { 71, 3, .5 }, { 72, 3.5, .5 } }),
                    "score key=0 major\n"
                    "m1 4/4\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: ( C4/8 D4/8 ) ( E4/8 F4/8 ) ( G4/8 A4/8 ) ( B4/8 C5/8 )\n"
                    "  S2 v1: R\n");

        expectDump ("sixteenths",
                    capture ({ { 72, 0, .25 }, { 74, .25, .25 }, { 76, .5, .25 }, { 77, .75, .25 }, { 79, 1, .5 }, { 81, 1.5, .5 }, { 83, 2, 1 }, { 84, 3, 1 } }),
                    "score key=0 major\n"
                    "m1 4/4\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: ( C5/16 D5/16 E5/16 F5/16 ) ( G5/8 A5/8 ) B5/4 C6/4\n"
                    "  S2 v1: R\n");

        expectDump ("E flat major",
                    capture ({ { p ("Eb4"), 0, 1 }, { p ("F4"), 1, 1 }, { p ("G4"), 2, 1 }, { p ("Ab4"), 3, 1 }, { p ("Bb4"), 4, 1 }, { p ("C5"), 5, 1 }, { p ("D5"), 6, 1 }, { p ("Eb5"), 7, 1 } }),
                    "score key=-3 major\n"
                    "m1 4/4\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: Eb4/4 F4/4 G4/4 Ab4/4\n"
                    "  S2 v1: R\n"
                    "m2 4/4\n"
                    "  S1 v1: Bb4/4 C5/4 D5/4 Eb5/4\n"
                    "  S2 v1: R\n");

        expectDump ("A flat major starting on its tonic",
                    capture ({ { 68, 0, 1 }, { 70, 1, 1 }, { 72, 2, 1 }, { 73, 3, 1 }, { 75, 4, 1 }, { 77, 5, 1 }, { 79, 6, 1 }, { 80, 7, 1 } }),
                    "score key=-4 major\n"
                    "m1 4/4\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: Ab4/4 Bb4/4 C5/4 Db5/4\n"
                    "  S2 v1: R\n"
                    "m2 4/4\n"
                    "  S1 v1: Eb5/4 F5/4 G5/4 Ab5/4\n"
                    "  S2 v1: R\n");
    }

    void testGoldenRhythm()
    {
        expectDump ("tie over the bar line",
                    capture ({ { 60, 2, 4 }, { 64, 6, 2 } }),
                    "score key=0 major\n"
                    "m1 4/4\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: r/2 C4/2~\n"
                    "  S2 v1: R\n"
                    "m2 4/4\n"
                    "  S1 v1: C4/2 E4/2\n"
                    "  S2 v1: R\n");

        expectDump ("syncopation",
                    capture ({ { 60, 0, .5 }, { 62, .5, 1.5 }, { 64, 2, 1 }, { 65, 3.5, 1 }, { 67, 5, 3 } }),
                    "score key=0 major\n"
                    "m1 4/4\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: ( C4/8 D4/8~ ) D4/4 E4/4 r/8 F4/8~\n"
                    "  S2 v1: R\n"
                    "m2 4/4\n"
                    "  S1 v1: F4/8 r/8 G4/4~ G4/2\n"
                    "  S2 v1: R\n");

        expectDump ("dotted values and ties across the beat",
                    capture ({ { 72, .5, 1.5 }, { 74, 2, .5 }, { 76, 2.5, 1.5 } }),
                    "score key=0 major\n"
                    "m1 4/4\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: r/8 C5/8~ C5/4 ( D5/8 E5/8~ ) E5/4\n"
                    "  S2 v1: R\n");

        expectDump ("a triplet",
                    capture ({ { 72, 0, 1. / 3 }, { 74, 1. / 3, 1. / 3 }, { 76, 2. / 3, 1. / 3 }, { 77, 1, 1 } }),
                    "score key=-1 major\n"
                    "m1 4/4\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: < ( C5/8 D5/8 E5/8 ) > F5/4 r/2\n"
                    "  S2 v1: R\n");

        expectDump ("rests of the right size",
                    capture ({ { 60, 4, 1 } }, 8),
                    "score key=-4 minor\n"
                    "m1 4/4\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: R\n"
                    "  S2 v1: R\n"
                    "m2 4/4\n"
                    "  S1 v1: C4/4 r/4 r/2\n"
                    "  S2 v1: R\n");
    }

    void testGoldenBars()
    {
        expectDump ("a pickup bar",
                    capture ({ { 67, 3, 1 }, { 72, 4, 2 }, { 71, 6, 1 }, { 69, 7, 1 } }, 8),
                    "score key=0 major\n"
                    "m0 4/4 pickup:960\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: G4/4\n"
                    "  S2 v1: R\n"
                    "m1 4/4\n"
                    "  S1 v1: C5/2 B4/4 A4/4\n"
                    "  S2 v1: R\n");

        expectDump ("a note that crosses into a pickup bar",
                    capture ({ { 60, 3, 2 }, { 64, 5, 1 } }),
                    "score key=0 major\n"
                    "m0 4/4 pickup:960\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: C4/4~\n"
                    "  S2 v1: R\n"
                    "m1 4/4\n"
                    "  S1 v1: C4/4 E4/4 r/2\n"
                    "  S2 v1: R\n");

        expectDump ("4/4, 3/4, 4/4",
                    capture ({ { 60, 0, 4 }, { 62, 4, 3 }, { 64, 7, 4 } }, 11, { { 0, 4, 4 }, { 4, 3, 4 }, { 7, 4, 4 } }),
                    "score key=0 major\n"
                    "m1 4/4\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: C4/1\n"
                    "  S2 v1: R\n"
                    "m2 3/4\n"
                    "  S1 v1: D4/2.\n"
                    "  S2 v1: R\n"
                    "m3 4/4\n"
                    "  S1 v1: E4/1\n"
                    "  S2 v1: R\n");

        expectDump ("3/4 with beams",
                    capture ({ { 60, 0, .5 }, { 62, .5, .5 }, { 64, 1, .5 }, { 65, 1.5, .5 }, { 67, 2, 1 }, { 69, 3, 2 }, { 71, 5, 1 } }, 6, { { 0, 3, 4 }, { 3, 3, 4 } }),
                    "score key=0 minor\n"
                    "m1 3/4\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: ( C4/8 D4/8 ) ( E4/8 F4/8 ) G4/4\n"
                    "  S2 v1: R\n"
                    "m2 3/4\n"
                    "  S1 v1: A4/2 B4/4\n"
                    "  S2 v1: R\n");

        expectDump ("6/8",
                    capture ({ { 60, 0, 1.5 }, { 62, 1.5, .5 }, { 64, 2, .5 }, { 65, 2.5, .5 }, { 67, 3, 1.5 } }, 6, { { 0, 6, 8 } }),
                    "score key=0 major\n"
                    "m1 6/8\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: C4/4. ( D4/8 E4/8 F4/8 )\n"
                    "  S2 v1: R\n"
                    "m2 6/8\n"
                    "  S1 v1: G4/4. r/4.\n"
                    "  S2 v1: R\n");
    }

    void testGoldenPiano()
    {
        expectDump ("a melody over a long bass note",
                    capture ({ { 36, 0, 4 }, { 60, 0, 1 }, { 64, 0, 1 }, { 67, 0, 1 }, { 62, 1, 1 }, { 64, 2, 2 } }),
                    "score key=0 major\n"
                    "m1 4/4\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: [C4 E4 G4]/4 D4/4 E4/2\n"
                    "  S2 v1: C2/1\n");

        expectDump ("two voices in one hand",
                    capture ({ { 72, 0, 4 }, { 64, 0, 1 }, { 65, 1, 1 }, { 67, 2, 1 }, { 69, 3, 1 } }),
                    "score key=0 major\n"
                    "m1 4/4\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: C5/1\n"
                    "  S1 v2: E4/4 F4/4 G4/4 A4/4\n"
                    "  S2 v1: R\n");

        expectDump ("chords with a bass line",
                    capture ({ { p ("C3"), 0, 2 }, { p ("C4"), 0, 2 }, { p ("E4"), 0, 2 }, { p ("G4"), 0, 2 },
                               { p ("F3"), 2, 2 }, { p ("A3"), 2, 2 }, { p ("C4"), 2, 2 }, { p ("F4"), 2, 2 },
                               { p ("G2"), 4, 2 }, { p ("B3"), 4, 2 }, { p ("D4"), 4, 2 }, { p ("G4"), 4, 2 },
                               { p ("C3"), 6, 2 }, { p ("C4"), 6, 2 }, { p ("E4"), 6, 2 }, { p ("G4"), 6, 2 } }),
                    "score key=0 major\n"
                    "m1 4/4\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: [C4 E4 G4]/2 F4/2\n"
                    "  S2 v1: C3/2 [F3 A3 C4]/2\n"
                    "m2 4/4\n"
                    "  S1 v1: [B3 D4 G4]/2 [C4 E4 G4]/2\n"
                    "  S2 v1: G2/2 C3/2\n");

        expectDump ("a melody that dips below middle C stays in the right hand",
                    capture ({ { 64, 0, 1 }, { 62, 1, 1 }, { 60, 2, 1 }, { 59, 3, 1 }, { 57, 4, 1 }, { 59, 5, 1 }, { 60, 6, 1 }, { 62, 7, 1 },
                               { 40, 0, 4 }, { 45, 4, 4 } }),
                    "score key=0 minor\n"
                    "m1 4/4\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: E4/4 D4/4 C4/4 B3/4\n"
                    "  S2 v1: E2/1\n"
                    "m2 4/4\n"
                    "  S1 v1: A3/4 B3/4 C4/4 D4/4\n"
                    "  S2 v1: A2/1\n");
    }

    void testGoldenAccidentalsAndQuantizing()
    {
        expectDump ("accidentals are shown once per measure",
                    capture ({ { p ("A4"), 0, 1 }, { p ("B4"), 1, 1 }, { p ("C5"), 2, 1 }, { p ("E5"), 3, 1 },
                               { p ("G#4"), 4, 1 }, { p ("G#4"), 5, 1 }, { p ("G4"), 6, 1 }, { p ("A4"), 7, 1 } }),
                    "score key=0 minor\n"
                    "m1 4/4\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: A4/4 B4/4 C5/4 E5/4\n"
                    "  S2 v1: R\n"
                    "m2 4/4\n"
                    "  S1 v1: G#4!/4 G#4/4 G4!/4 A4/4\n"
                    "  S2 v1: R\n");

        expectDump ("chromatic eighths",
                    capture ({ { 60, 0, .5 }, { 61, .5, .5 }, { 62, 1, .5 }, { 63, 1.5, .5 }, { 64, 2, .5 }, { 65, 2.5, .5 }, { 66, 3, .5 }, { 67, 3.5, .5 } }),
                    "score key=0 major\n"
                    "m1 4/4\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: ( C4/8 Db4!/8 ) ( D4!/8 Eb4!/8 ) ( E4!/8 F4/8 ) ( F#4!/8 G4/8 )\n"
                    "  S2 v1: R\n");

        expectDump ("played unevenly, written evenly",
                    capture ({ { 72, 0.02, .23 }, { 74, .24, .25 }, { 76, .5, .25 }, { 77, .73, .25 }, { 79, 1.01, .5 }, { 81, 1.52, .5 } }),
                    "score key=-1 major\n"
                    "m1 4/4\n"
                    "  tempo @0: 120\n"
                    "  S1 v1: ( C5/16 D5/16 E5/16 F5/16 ) ( G5/8 A5/8 ) r/2\n"
                    "  S2 v1: R\n");
    }

    void testReportAndSettings()
    {
        // off-grid notes are reported
        const auto result = transcribePiano (capture ({ { 72, 0, .5 }, { 74, .58, .4 }, { 76, 1, .5 }, { 77, 1.58, .4 } }), {});
        CHECK_EQ (result.report.offGridNotes, 2);
        CHECK_EQ ((int) result.report.warnings.size(), 1);
        CHECK_EQ (result.report.notes, 4);
        CHECK_EQ (result.report.measures, 1);

        // a key given by the user wins
        TranscriptionSettings s;
        s.keyTonic = 9;
        s.keyMinor = true;
        const auto minor = transcribePiano (capture ({ { 60, 0, 1 }, { 62, 1, 1 }, { 64, 2, 1 }, { 65, 3, 1 } }), s);
        CHECK_EQ (minor.key.tonic, 9);
        CHECK (minor.key.minor);
        CHECK_EQ (minor.key.fifths, 0);
        CHECK_EQ (minor.score.root().prop ("keyMode").asString() == "minor" ? 1 : 0, 1);

        // an empty recording gives bars of rests
        const auto empty = transcribePiano (capture ({}, 8), {});
        CHECK_EQ (empty.report.measures, 2);
        CHECK_STR (empty.score.validate().c_str(), "");

        // the split point moves notes between the hands
        s = {};
        s.splitPoint = 72;
        const auto dump = transcribed (capture ({ { 64, 0, 4 } }), s);
        CHECK (dump.find ("S2 v1: E4/1") != std::string::npos);

        // the settings as JSON
        TranscriptionSettings custom;
        custom.grid = 8;
        custom.triplets = false;
        custom.splitPoint = 55;
        custom.autoPickup = false;
        custom.keyTonic = 3;
        custom.keyMinor = true;
        CHECK (TranscriptionSettings::fromJson (custom.toJson()) == custom);
        CHECK (TranscriptionSettings::fromJson (Json()) == TranscriptionSettings());

        auto bad = Json::object();
        bad.set ("grid", 5);
        bad.set ("splitPoint", 1000);
        bad.set ("keyTonic", 99);
        const auto fixed = TranscriptionSettings::fromJson (bad);
        CHECK_EQ (fixed.grid, 16);
        CHECK_EQ (fixed.splitPoint, 108);
        CHECK_EQ (fixed.keyTonic, -1);
    }

    void testScoreContents()
    {
        const auto result = transcribePiano (capture ({ { 60, 0, 1 }, { 64, 0, 1 }, { 62, 1, 1 }, { 60, 3, 3 } }), {});
        const auto& root = result.score.root();
        CHECK_EQ (root.prop ("tpq").asInt(), (int64_t) 960);
        CHECK_EQ ((int) root.children.size(), 1);
        const auto& part = root.children[0];
        CHECK_EQ ((int) part.children.size(), 2);                       // two staves
        CHECK_STR (part.children[0].prop ("clef").asString().c_str(), "G");
        CHECK_STR (part.children[1].prop ("clef").asString().c_str(), "F");
        CHECK_EQ ((int) part.children[0].children.size(), 2);           // two bars

        // the first chord has two notes, each with its own pitch, spelling and the same onset
        const auto& layer = part.children[0].children[0].children[0];
        CHECK_STR (layer.type.c_str(), "layer");
        const auto& chord = layer.children[0];
        CHECK_STR (chord.type.c_str(), "chord");
        CHECK_EQ ((int) chord.children.size(), 2);
        CHECK_EQ (chord.children[0].prop ("pitch").asInt(), (int64_t) 60);
        CHECK_STR (chord.children[0].prop ("step").asString().c_str(), "C");
        CHECK_EQ (chord.children[0].prop ("oct").asInt(), (int64_t) 4);
        CHECK_EQ (chord.prop ("ticks").asInt(), (int64_t) 960);
        CHECK_EQ (chord.prop ("dur").asInt(), (int64_t) 4);

        // the tied note: initial in bar 1, terminal in bar 2
        const auto& lastOfBar1 = part.children[0].children[0].children[0].children.back();
        CHECK_STR (lastOfBar1.prop ("tie").asString().c_str(), "i");
        const auto& firstOfBar2 = part.children[0].children[1].children[0].children.front();
        CHECK_STR (firstOfBar2.prop ("tie").asString().c_str(), "t");

        // the score survives saving
        Json json;
        CHECK (Json::parse (result.score.toJson().dump(), json));
        Score loaded;
        std::string error;
        CHECK (Score::fromJson (json, loaded, &error));
        CHECK (loaded == result.score);
        CHECK_STR (dumpScore (loaded).c_str(), dumpScore (result.score).c_str());

        // the same input always gives the same score
        const auto again = transcribePiano (capture ({ { 60, 0, 1 }, { 64, 0, 1 }, { 62, 1, 1 }, { 60, 3, 3 } }), {});
        CHECK (again.score == result.score);

        // generating is not an edit
        CHECK_EQ ((int) result.score.revision(), 0);
    }

    // Whatever is played: every voice fills its measure, the notes keep their total length per pitch,
    // and every tie leads to a note of the same pitch.
    void checkScoreAddsUp (const TranscriptionResult& result, const std::string& what)
    {
        const auto& part = result.score.root().children[0];

        for (const auto& staff : part.children)
        {
            for (const auto& measure : staff.children)
            {
                for (const auto& layer : measure.children)
                {
                    if (layer.type != nodeType::layer)
                        continue;

                    int64_t sum = 0;

                    for (const auto& e : layer.children)
                        sum += e.prop ("ticks").asInt();

                    testing::report (sum == measure.prop ("ticks").asInt(), __FILE__, __LINE__,
                                     what + ": a voice does not fill its measure (" + std::to_string (sum) + " of "
                                     + std::to_string (measure.prop ("ticks").asInt()) + ")");
                }
            }
        }

        std::map<int, int64_t> written, quantised;

        for (const auto& q : result.notes)
            quantised[q.pitch] += q.dur;

        std::vector<const Node*> stack { &result.score.root() };

        while (! stack.empty())
        {
            const auto* n = stack.back();
            stack.pop_back();

            if (n->type == nodeType::note)
            {
                written[(int) n->prop ("pitch").asInt()] += n->prop ("ticks").asInt();
            }
            else if (n->type == nodeType::chord)
            {
                for (const auto& c : n->children)
                    written[(int) c.prop ("pitch").asInt()] += n->prop ("ticks").asInt();

                continue;
            }

            for (const auto& c : n->children)
                stack.push_back (&c);
        }

        testing::report (written == quantised, __FILE__, __LINE__, what + ": the notes changed length");

        // ties: the beginnings and the ends of ties per pitch must match
        std::map<int, int> opens, closes;
        stack = { &result.score.root() };

        while (! stack.empty())
        {
            const auto* n = stack.back();
            stack.pop_back();

            if (n->type == nodeType::note)
            {
                const auto tie = n->prop ("tie").asString();
                const auto pitch = (int) n->prop ("pitch").asInt();
                opens[pitch] += (tie == "i" || tie == "m") ? 1 : 0;
                closes[pitch] += (tie == "t" || tie == "m") ? 1 : 0;
            }

            for (const auto& c : n->children)
                stack.push_back (&c);
        }

        testing::report (opens == closes, __FILE__, __LINE__, what + ": ties do not match");
    }

    // Random clips: whatever is played, the bars add up, the notes keep their length and ties match.
    void testRandomClipsKeepTheirTimeAndNotes()
    {
        std::mt19937 rng (2026);
        int clips = 0;

        for (int round = 0; round < 150; ++round)
        {
            const auto numBars = 3 + (int) (rng() % 4);
            const bool threeFour = rng() % 4 == 0;
            const double barLength = threeFour ? 3.0 : 4.0;
            const auto total = numBars * barLength;

            ResolvedCapture rc;
            rc.lengthPpq = total;
            rc.tempoMap.push_back ({ 0.0, 120.0 });

            for (double b = 0.0; b < total - 1.0e-9; b += barLength)
                rc.bars.push_back ({ b, threeFour ? 3 : 4, 4 });

            const auto count = 5 + (int) (rng() % 40);

            for (int i = 0; i < count; ++i)
            {
                const bool triplet = rng() % 6 == 0;
                const auto slot = triplet ? 1.0 / 6.0 : 0.25;
                const auto start = (double) (rng() % (unsigned) (total / slot - 8)) * slot;
                const auto length = (double) (1 + rng() % 12) * slot;
                ResolvedNote n;
                n.pitch = 36 + (int) (rng() % 55);
                n.onPpq = start;
                n.offPpq = std::min (total, start + length);
                n.velocity = 80;

                if (n.offPpq > n.onPpq)
                    rc.notes.push_back (n);
            }

            TranscriptionSettings settings;
            settings.autoPickup = rng() % 2 == 0;
            const auto result = transcribePiano (rc, settings);
            ++clips;

            testing::report (result.score.validate().empty(), __FILE__, __LINE__, "random clip: invalid score");

            checkScoreAddsUp (result, "random clip");
        }

        CHECK_EQ (clips, 150);
    }

#ifdef TRANSCRIBER_FIXTURES_DIR
    void testFixtureFiles()
    {
        const std::string dir = TRANSCRIBER_FIXTURES_DIR;

        // 3 minutes of piano with chords, a bass line, triplets and a melody
        auto piano = midireader::read (dir + "/t23-piano-3min.mid");
        CHECK (piano.ok);

        if (piano.ok)
        {
            const auto result = transcribePiano (piano.capture, {});
            CHECK_EQ (result.report.notes, 801);
            CHECK_EQ (result.report.measures, 90);
            CHECK_EQ (result.report.offGridNotes, 0);
            CHECK_EQ (result.report.mergedNotes, 0);
            CHECK (result.report.warnings.empty());
            CHECK (result.report.maxVoices <= 3);
            CHECK_STR (result.score.validate().c_str(), "");
            checkScoreAddsUp (result, "piano fixture");
        }

        // every other clip we have must at least come out as a valid score that adds up
        for (const char* name : { "t21-loop1.mid", "t21-loop2.mid", "t21-loop4.mid", "t21-loop8.mid", "t22-verse.mid", "t22-chorus.mid",
                                  "t24-held-note.mid", "t11-piano.mid", "t11-drums.mid", "t13-timing.mid", "t23-drums-3min.mid" })
        {
            auto f = midireader::read (dir + "/" + name);
            testing::report (f.ok, __FILE__, __LINE__, std::string (name) + ": " + f.error);

            if (f.ok)
            {
                const auto result = transcribePiano (f.capture, {});
                testing::report (result.score.validate().empty(), __FILE__, __LINE__, std::string (name) + ": invalid score");
                checkScoreAddsUp (result, name);
            }
        }
    }
#endif

    REGISTER (testCleanNotes, "transcribe: clean-up");
    REGISTER (testTenMinuteSongs, "stress: ten minute songs and dense MIDI through every pipeline");
    REGISTER (testTranspose, "transcribe: transposition in semitones");
    REGISTER (testFineGrids, "transcribe: 1/64 and 1/128 grids");
    REGISTER (testBuildBars, "transcribe: bars");
    REGISTER (testPickup, "transcribe: pickup bar");
    REGISTER (testQuantizeStraight, "transcribe: quantise straight");
    REGISTER (testQuantizeTriplets, "transcribe: quantise triplets");
    REGISTER (testQuantizeGridsAndEdges, "transcribe: quantise grids, edges and durations");
    REGISTER (testSplitLength44, "transcribe: lengths in 4/4");
    REGISTER (testSplitLengthOtherMeters, "transcribe: lengths in 3/4 and 6/8");
    REGISTER (testSplitLengthTriplets, "transcribe: lengths with triplets");
    REGISTER (testBeatGroups, "transcribe: beat groups");
    REGISTER (testKeyDetection, "transcribe: key detection");
    REGISTER (testKeySignatures, "transcribe: key signatures");
    REGISTER (testSpelling, "transcribe: pitch spelling");
    REGISTER (testPianoVoices, "transcribe: hands and voices");
    REGISTER (testTempoMarks, "transcribe: tempo marks");
    REGISTER (testGoldenScales, "golden: scales and keys");
    REGISTER (testGoldenRhythm, "golden: rhythm");
    REGISTER (testGoldenBars, "golden: bars and meters");
    REGISTER (testGoldenPiano, "golden: piano texture");
    REGISTER (testGoldenAccidentalsAndQuantizing, "golden: accidentals and uneven playing");
    REGISTER (testReportAndSettings, "transcribe: report and settings");
    REGISTER (testScoreContents, "transcribe: the score's contents");
    REGISTER (testRandomClipsKeepTheirTimeAndNotes, "transcribe: 150 random clips keep their time and notes");
#ifdef TRANSCRIBER_FIXTURES_DIR
    REGISTER (testFixtureFiles, "transcribe: the test clips from the fixtures folder");
#endif
}
