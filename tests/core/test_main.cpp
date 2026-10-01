// Unit tests for the capture core. They need no JUCE and no host: SimHost plays notes into the
// engine the way Live does (see tests/core/SimHost.h).

#include "SimHost.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
    int failures = 0;
    int checks = 0;
    const char* currentTest = "";

    void report (bool ok, const char* file, int line, const std::string& what)
    {
        ++checks;

        if (! ok)
        {
            ++failures;
            std::printf ("  FAIL [%s] %s:%d  %s\n", currentTest, file, line, what.c_str());
        }
    }
}

#define CHECK(cond) report ((cond), __FILE__, __LINE__, #cond)
#define CHECK_EQ(a, b) report ((a) == (b), __FILE__, __LINE__, std::string (#a " == " #b "  (") + std::to_string (a) + " vs " + std::to_string (b) + ")")
#define CHECK_NEAR(a, b, tol) report (std::abs ((a) - (b)) <= (tol), __FILE__, __LINE__, std::string (#a " ~= " #b "  (") + std::to_string (a) + " vs " + std::to_string (b) + ")")

using namespace trs;

namespace
{
    struct Rig
    {
        CaptureEngine engine;
        CaptureModel model;
        sim::Host host;

        explicit Rig (size_t capacity = 1u << 15, double rate = 48000.0, int block = 256)
            : engine (capacity), host (engine, model, rate, block) {}
    };

    //==========================================================================
    // Clip content: every bar differs from every other, so no bar is mistaken for a repeat.

    std::vector<sim::Note> makeClip (int bars, int variant = 0)
    {
        std::vector<sim::Note> clip;

        for (int j = 0; j < bars; ++j)
        {
            const double b = j * 4.0;
            clip.push_back ({ b + 0.0,  b + 1.0,  48 + j % 24 + variant, 90 });
            clip.push_back ({ b + 1.5,  b + 2.0,  60 + (j * 3) % 11 + variant, 100 });
            clip.push_back ({ b + 2.75, b + 3.0,  72 + (j * 5) % 13, 110 });
        }

        return clip;
    }

    std::vector<sim::Note> shifted (std::vector<sim::Note> notes, double by)
    {
        for (auto& n : notes)
        {
            n.on += by;
            n.off += by;
        }

        return notes;
    }

    // Does the resolved score hold exactly these notes (positions relative to the origin)?
    bool sameNotes (const ResolvedCapture& r, std::vector<sim::Note> expected, double tol = 1.0e-3, bool compareOff = true)
    {
        auto got = r.notes;
        auto key = [] (double on, int pitch) { return std::make_pair (std::llround (on * 100.0), pitch); };

        std::sort (expected.begin(), expected.end(), [&] (const sim::Note& a, const sim::Note& b) { return key (a.on, a.pitch) < key (b.on, b.pitch); });
        std::sort (got.begin(), got.end(), [&] (const ResolvedNote& a, const ResolvedNote& b) { return key (a.onPpq, a.pitch) < key (b.onPpq, b.pitch); });

        if (got.size() != expected.size())
        {
            std::printf ("    note count: got %zu, expected %zu\n", got.size(), expected.size());
            return false;
        }

        for (size_t i = 0; i < got.size(); ++i)
        {
            const bool ok = got[i].pitch == expected[i].pitch
                         && std::abs (got[i].onPpq - expected[i].on) <= tol
                         && (! compareOff || std::abs (got[i].offPpq - expected[i].off) <= tol);

            if (! ok)
            {
                std::printf ("    note %zu: got pitch %d on %.4f off %.4f, expected pitch %d on %.4f off %.4f\n", i,
                             got[i].pitch, got[i].onPpq, got[i].offPpq, expected[i].pitch, expected[i].on, expected[i].off);
                return false;
            }
        }

        return true;
    }

    // Session View: the clip is launched at startPpq and loops until the transport stops.
    void recordSession (Rig& rig, const std::vector<sim::Note>& absoluteNotes, double startPpq, double beats)
    {
        rig.host.setMessages (sim::toMessages (absoluteNotes));
        rig.engine.requestArm();
        rig.host.play (startPpq);
        rig.host.run (beats);
        rig.host.stop();
    }

    //==========================================================================
    void testEngineStates()
    {
        Rig rig;
        rig.host.setMessages ({});
        CHECK (rig.engine.getState() == CaptureEngine::State::idle);

        // Armed, but the transport is not running: nothing is recorded.
        rig.engine.requestArm();
        rig.host.drain();
        trs::HostBlock stopped;
        stopped.hasPpq = stopped.hasBpm = true;
        stopped.bpm = 120.0;
        stopped.sampleRate = 48000.0;
        stopped.numSamples = 256;
        rig.engine.process (stopped, nullptr, 0);
        CHECK (rig.engine.getState() == CaptureEngine::State::armed);
        const auto pendingWhileStopped = (int) rig.engine.drain ([] (const Record&) {});
        CHECK_EQ (pendingWhileStopped, 0);

        // The transport starts: recording begins.
        rig.host.play (0.0);
        rig.host.run (1.0);
        CHECK (rig.engine.getState() == CaptureEngine::State::recording);
        CHECK (rig.model.raw().recording);

        // The user presses Stop while the transport runs.
        rig.engine.requestStop();
        rig.host.run (0.1);
        CHECK (rig.engine.getState() == CaptureEngine::State::stopped);
        CHECK (rig.model.raw().stopped);
        CHECK (rig.model.raw().stoppedByUser);
        CHECK_NEAR (rig.model.raw().stopPpq, 1.0, 0.02);

        // Blocks after the stop are not recorded.
        const auto end = rig.model.raw().endPpq;
        rig.host.run (1.0);
        CHECK_NEAR (rig.model.raw().endPpq, end, 1.0e-9);

        // Arming again starts a new capture.
        rig.engine.requestArm();
        rig.host.run (0.5);
        CHECK (rig.model.raw().recording);
        CHECK (! rig.model.raw().stopped);

        // The transport stops: the capture ends.
        rig.host.stop();
        CHECK (rig.engine.getState() == CaptureEngine::State::stopped);

        // Stopping while armed goes back to idle.
        rig.engine.requestArm();
        rig.host.stop();
        CHECK (rig.engine.getState() == CaptureEngine::State::armed);
        rig.engine.requestStop();
        rig.host.stop();
        CHECK (rig.engine.getState() == CaptureEngine::State::idle);
        rig.engine.requestArm();
        rig.host.stop();

        rig.engine.requestReset();
        rig.host.stop();
        CHECK (rig.engine.getState() == CaptureEngine::State::idle);
    }

    void testBasicCapture()
    {
        Rig rig;
        const auto clip = makeClip (4);
        recordSession (rig, clip, 0.0, 16.0);

        const auto& raw = rig.model.raw();
        CHECK (raw.stopped);
        CHECK_EQ ((int) raw.notes.size(), 12);
        CHECK_EQ ((int) raw.segments.size(), 1);
        CHECK_EQ ((int) raw.bars.size(), 4);
        CHECK_NEAR (raw.stopPpq, 16.0, 0.02);

        // 4 bars played once cannot be a loop.
        CHECK (rig.model.reading().mode == ReadingMode::asPlayed);
        CHECK (sameNotes (rig.model.resolved(), clip));
        CHECK_EQ ((int) rig.model.resolved().notes.size(), 12);
    }

    void testHeldNoteAtStop()
    {
        Rig rig;
        // One note that is still sounding when the transport stops at beat 2.
        recordSession (rig, { { 1.0, 5.0, 60, 100 } }, 0.0, 2.0);

        const auto& raw = rig.model.raw();
        CHECK_EQ ((int) raw.notes.size(), 1);
        CHECK (raw.notes[0].heldAtStop);
        CHECK_NEAR (raw.notes[0].offPpq, raw.stopPpq, 1.0e-6);
        CHECK_NEAR (raw.notes[0].onPpq, 1.0, 1.0e-3);

        // Stop without the host sending note-offs: the model closes the note itself.
        Rig bare;
        bare.host.setMessages (sim::toMessages ({ { 1.0, 5.0, 60, 100 } }));
        bare.engine.requestArm();
        bare.host.play (0.0);
        bare.host.run (2.0);
        bare.host.stop (false);
        CHECK (bare.model.raw().notes[0].heldAtStop);
        CHECK_NEAR (bare.model.raw().notes[0].offPpq, bare.model.raw().stopPpq, 1.0e-6);
    }

    //==========================================================================
    void testSessionLoopLengths()
    {
        for (const int bars : { 1, 2, 4, 8 })
        {
            Rig rig;
            const auto clip = makeClip (bars);
            const auto length = 4.0 * bars;
            const auto total = 3.55 * length;   // three passes and a bit
            recordSession (rig, sim::loopClip (clip, length, 0.0, total), 0.0, total);

            char name[64];
            std::snprintf (name, sizeof name, "session loop %d bars", bars);
            currentTest = name;

            const auto& d = rig.model.detection();
            CHECK (rig.model.reading().mode == ReadingMode::oneLoop);
            CHECK (rig.model.reading().source == ReadingSource::detected);
            CHECK_EQ (d.candidateBars, bars);
            CHECK_NEAR (rig.model.reading().loopLengthPpq, length, 1.0e-3);
            CHECK_NEAR (rig.model.reading().loopStartPpq, 0.0, 1.0e-3);
            CHECK_EQ (rig.model.getLoopBars(), bars);

            // The final pass was stopped partway; the rest comes from the pass before it.
            CHECK (sameNotes (rig.model.resolved(), clip));
        }

        currentTest = "";
    }

    void testSessionStartsMidLoop()
    {
        // Arming while the clip is already looping: the recording starts in the middle of a bar.
        Rig rig;
        const auto clip = makeClip (4);
        const auto absolute = sim::loopClip (clip, 16.0, 0.0, 80.0);
        rig.host.setMessages (sim::toMessages (absolute));
        rig.engine.requestArm();
        rig.host.play (18.5);   // bar 5, beat 3
        rig.host.run (50.0);
        rig.host.stop();

        CHECK (rig.model.reading().mode == ReadingMode::oneLoop);
        CHECK_EQ (rig.model.detection().candidateBars, 4);

        // Every loop position was played at least once; the result is the whole clip, rotated
        // to begin at the first full bar.
        const auto& res = rig.model.resolved();
        CHECK_EQ ((int) res.notes.size(), 12);
        CHECK_NEAR (res.lengthPpq, 16.0, 1.0e-3);
    }

    void testEditedClipBetweenPasses()
    {
        Rig rig;
        const auto v1 = makeClip (4, 0);
        const auto v2 = makeClip (4, 1);   // every note one semitone higher
        const double length = 16.0;

        auto absolute = sim::loopClip (v1, length, 0.0, 2 * length);
        const auto edited = sim::loopClip (v2, length, 2 * length, 2.5 * length);
        absolute.insert (absolute.end(), edited.begin(), edited.end());

        recordSession (rig, absolute, 0.0, 2.5 * length);

        // The edit makes the recording differ from a plain repeat, so it reads as As played.
        CHECK (rig.model.reading().mode == ReadingMode::asPlayed);
        CHECK_EQ (rig.model.detection().candidateBars, 4);
        CHECK_EQ ((int) rig.model.resolved().notes.size(), (int) absolute.size());

        // Switching to One loop gives: new notes up to the stop point, old notes after it.
        rig.model.setMode (ReadingMode::oneLoop);
        CHECK (rig.model.reading().source == ReadingSource::user);
        CHECK_NEAR (rig.model.reading().loopLengthPpq, length, 1.0e-3);

        std::vector<sim::Note> expected;

        for (const auto& n : v2)
            if (n.on < 8.0 - 1.0e-9)
                expected.push_back (n);

        for (const auto& n : v1)
            if (n.on >= 8.0 - 1.0e-9)
                expected.push_back (n);

        CHECK (sameNotes (rig.model.resolved(), expected));
        CHECK_EQ (rig.model.resolved().numPasses, 3);

        // Back to As played: everything again.
        rig.model.setMode (ReadingMode::asPlayed);
        CHECK_EQ ((int) rig.model.resolved().notes.size(), (int) absolute.size());
    }

    void testHeldNoteTakesPreviousPassLength()
    {
        // A 2-bar clip whose last note runs to the clip's end. The recording is stopped just
        // before that, while the note sounds.
        std::vector<sim::Note> clip = makeClip (2);
        clip.push_back ({ 6.0, 8.0, 64, 100 });
        const double length = 8.0;

        Rig rig;
        recordSession (rig, sim::loopClip (clip, length, 0.0, 3 * length + 7.0), 0.0, 3 * length + 7.0);
        CHECK (rig.model.reading().mode == ReadingMode::oneLoop);

        const ResolvedNote* held = nullptr;

        for (const auto& n : rig.model.resolved().notes)
            if (n.pitch == 64 && std::abs (n.onPpq - 6.0) < 0.01)
                held = &n;

        CHECK (held != nullptr);

        if (held != nullptr)
            CHECK_NEAR (held->offPpq - held->onPpq, 2.0, 1.0e-3);

        // With no earlier pass to copy, the note ends at the stop point.
        Rig once;
        recordSession (once, { { 6.0, 8.0, 64, 100 } }, 0.0, 7.0);
        CHECK_EQ ((int) once.model.resolved().notes.size(), 1);
        CHECK_NEAR (once.model.resolved().notes[0].offPpq, once.model.raw().stopPpq - once.model.resolved().originPpq, 1.0e-6);
    }

    void testClipSequenceIsWrittenOutInFull()
    {
        // Verse x2 then chorus x4, 4 bars each.
        const auto verse = makeClip (4, 0);
        const auto chorus = makeClip (4, 5);
        std::vector<sim::Note> all;

        for (const auto& v : { sim::loopClip (verse, 16.0, 0.0, 32.0), sim::loopClip (chorus, 16.0, 32.0, 96.0) })
            all.insert (all.end(), v.begin(), v.end());

        Rig rig;
        recordSession (rig, all, 0.0, 96.0);

        CHECK (rig.model.reading().mode == ReadingMode::asPlayed);
        CHECK_EQ ((int) rig.model.resolved().notes.size(), (int) all.size());
        CHECK (sameNotes (rig.model.resolved(), all));
    }

    void testLongPieceMatchesNoteForNote()
    {
        // Three minutes at 120 bpm: 90 bars, played once.
        const auto piece = makeClip (90);
        Rig rig;
        recordSession (rig, piece, 0.0, 360.0);

        CHECK_EQ ((int) rig.model.raw().notes.size(), (int) piece.size());
        CHECK_EQ ((int) rig.model.raw().bars.size(), 90);
        CHECK (rig.model.reading().mode == ReadingMode::asPlayed);
        CHECK (sameNotes (rig.model.resolved(), piece));
        CHECK_EQ ((int) rig.engine.getDroppedCount(), 0);
    }

    void testSilentRecordingIsNotALoop()
    {
        Rig rig;
        recordSession (rig, {}, 0.0, 32.0);
        CHECK (rig.model.reading().mode == ReadingMode::asPlayed);
        CHECK_EQ (rig.model.detection().candidateBars, 0);
        CHECK (rig.model.resolved().notes.empty());
    }

    void testSmallVariationStillALoop()
    {
        // 4-bar clip, 5 passes; one bar of one pass differs (e.g. a humanising MIDI effect).
        const auto clip = makeClip (4);
        auto absolute = sim::loopClip (clip, 16.0, 0.0, 80.0);

        // In pass 4 (beats 64-80) bar 2's middle note is a tone higher.
        for (auto& n : absolute)
            if (n.on >= 72.0 && n.on < 76.0 && n.pitch >= 60 && n.pitch < 72)
                n.pitch += 2;

        Rig rig;
        recordSession (rig, absolute, 0.0, 80.0);
        CHECK (rig.model.reading().mode == ReadingMode::oneLoop);
        CHECK_EQ (rig.model.detection().candidateBars, 4);

        // The last pass was played to the end, so it provides every note.
        CHECK_EQ ((int) rig.model.resolved().notes.size(), 12);
    }

    //==========================================================================
    void testArrangementLoopWithSplitBlocks()
    {
        // Loop brace from beat 8 to 24 (bars 3-6). The block that reaches the end is split.
        const auto clip = shifted (makeClip (4), 8.0);
        Rig rig;
        rig.host.setMessages (sim::toMessages (clip));
        rig.host.setLoop (8.0, 24.0);
        rig.engine.requestArm();
        rig.host.play (8.0);
        rig.host.run (3.5 * 16.0);
        rig.host.stop();

        const auto& raw = rig.model.raw();
        CHECK_EQ ((int) raw.segments.size(), 4);

        for (size_t i = 1; i < raw.segments.size(); ++i)
            CHECK (raw.segments[i].wrap);

        // Capture time keeps running through the wraps.
        CHECK_NEAR (raw.segments[1].capStartPpq, 24.0, 1.0e-3);
        CHECK_NEAR (raw.segments[2].capStartPpq, 40.0, 1.0e-3);
        CHECK_NEAR (raw.stopPpq, 8.0 + 56.0, 0.02);

        const auto& reading = rig.model.reading();
        CHECK (reading.mode == ReadingMode::oneLoop);
        CHECK (reading.source == ReadingSource::host);
        CHECK_NEAR (reading.loopLengthPpq, 16.0, 1.0e-3);
        CHECK_NEAR (reading.loopStartPpq, 24.0, 1.0e-3);

        // Every note arrives at the right place on every pass, including the one right at the
        // start of the loop (the block was split exactly there).
        CHECK_EQ ((int) raw.notes.size(), 12 * 4 - 6);   // the last pass is half

        const auto& res = rig.model.resolved();
        CHECK_EQ (res.numPasses, 4);
        CHECK (sameNotes (res, shifted (clip, -8.0)));

        // As played writes every pass out in order.
        rig.model.setMode (ReadingMode::asPlayed);
        CHECK_EQ ((int) rig.model.resolved().notes.size(), (int) raw.notes.size());
    }

    void testArrangementLoopEditedBetweenPasses()
    {
        const auto v1 = shifted (makeClip (4, 0), 8.0);
        const auto v2 = shifted (makeClip (4, 1), 8.0);

        Rig rig;
        rig.host.setMessagesPerPass ({ sim::toMessages (v1), sim::toMessages (v1), sim::toMessages (v2) });
        rig.host.setLoop (8.0, 24.0);
        rig.engine.requestArm();
        rig.host.play (8.0);
        rig.host.run (2.5 * 16.0);
        rig.host.stop();

        CHECK (rig.model.reading().source == ReadingSource::host);

        std::vector<sim::Note> expected;

        for (const auto& n : v2)
            if (n.on < 16.0 - 1.0e-9)
                expected.push_back (n);

        for (const auto& n : v1)
            if (n.on >= 16.0 - 1.0e-9)
                expected.push_back (n);

        CHECK (sameNotes (rig.model.resolved(), shifted (expected, -8.0)));
    }

    void testArrangementWithoutLoop()
    {
        const auto piece = makeClip (6);
        Rig rig;
        rig.host.setMessages (sim::toMessages (piece));
        rig.engine.requestArm();
        rig.host.play (0.0);
        rig.host.run (24.0);
        rig.host.stop();

        CHECK_EQ ((int) rig.model.raw().segments.size(), 1);
        CHECK (rig.model.reading().mode == ReadingMode::asPlayed);
        CHECK (sameNotes (rig.model.resolved(), piece));
    }

    void testUserJumpIsNotALoopWrap()
    {
        // The user relocates during playback: a new segment, but no wrap.
        const auto piece = makeClip (8);
        Rig rig;
        rig.host.setMessages (sim::toMessages (piece));
        rig.engine.requestArm();
        rig.host.play (0.0);
        rig.host.run (8.0);
        rig.host.play (20.0);   // jump forward
        rig.host.run (8.0);
        rig.host.stop();

        const auto& raw = rig.model.raw();
        CHECK_EQ ((int) raw.segments.size(), 2);
        CHECK (! raw.segments[1].wrap);
        CHECK_NEAR (raw.segments[1].capStartPpq, raw.segments[0].capEndPpq, 1.0e-9);
        CHECK (rig.model.reading().mode == ReadingMode::asPlayed);
    }

    //==========================================================================
    void testMeterChanges()
    {
        // 4/4 from beat 0, 3/4 from beat 4, 4/4 again from beat 10.
        Rig rig;
        rig.host.setMeters ({ { 0.0, 4, 4 }, { 4.0, 3, 4 }, { 10.0, 4, 4 } });
        rig.host.setMessages ({});
        rig.engine.requestArm();
        rig.host.play (0.0);
        rig.host.run (20.0);
        rig.host.stop();

        const std::vector<BarStart> expected { { 0.0, 4, 4 }, { 4.0, 3, 4 }, { 7.0, 3, 4 }, { 10.0, 4, 4 }, { 14.0, 4, 4 }, { 18.0, 4, 4 } };
        const auto& bars = rig.model.raw().bars;
        CHECK_EQ ((int) bars.size(), (int) expected.size());

        for (size_t i = 0; i < std::min (bars.size(), expected.size()); ++i)
        {
            CHECK_NEAR (bars[i].ppq, expected[i].ppq, 1.0e-6);
            CHECK_EQ (bars[i].num, expected[i].num);
            CHECK_EQ (bars[i].den, expected[i].den);
        }
    }

    void testMeterChangeKeepsNotesInPlace()
    {
        // Notes on the first beat of every bar across a 4/4 -> 3/4 -> 4/4 song.
        const std::vector<double> barStarts { 0, 4, 7, 10, 14, 18 };
        std::vector<sim::Note> notes;

        for (const auto b : barStarts)
            notes.push_back ({ b, b + 1.0, 60, 100 });

        Rig rig;
        rig.host.setMeters ({ { 0.0, 4, 4 }, { 4.0, 3, 4 }, { 10.0, 4, 4 } });
        recordSession (rig, notes, 0.0, 20.0);

        const auto& raw = rig.model.raw();
        CHECK_EQ ((int) raw.notes.size(), 6);
        CHECK_EQ ((int) raw.bars.size(), 6);

        // Each note sits on a bar line.
        for (size_t i = 0; i < std::min (raw.notes.size(), raw.bars.size()); ++i)
            CHECK_NEAR (raw.notes[i].onPpq, raw.bars[i].ppq, 1.0e-3);

        // A bar that follows a different meter is not a repeat of an earlier bar.
        CHECK (rig.model.reading().mode == ReadingMode::asPlayed);
    }

    void testGlobalMeterChangeCreatesShortBar()
    {
        // Live re-grids from song position 0 when the meter field changes during playback:
        // 4/4 bars up to beat 8, then 3/4 bars from the next line of the new grid (9, 12, ...).
        // The bar from 8 to 9 is a short one.
        Rig rig;
        rig.host.setMeters ({ { 0.0, 4, 4 }, { 9.0, 3, 4 } });
        rig.host.setMessages ({});
        rig.engine.requestArm();
        rig.host.play (0.0);
        rig.host.run (14.0);
        rig.host.stop();

        const auto& bars = rig.model.raw().bars;
        CHECK_EQ ((int) bars.size(), 5);
        CHECK_NEAR (bars[0].ppq, 0.0, 1.0e-6);
        CHECK_NEAR (bars[1].ppq, 4.0, 1.0e-6);
        CHECK_NEAR (bars[2].ppq, 8.0, 1.0e-6);
        CHECK_NEAR (bars[3].ppq, 9.0, 1.0e-6);
        CHECK_NEAR (bars[4].ppq, 12.0, 1.0e-6);
    }

    void testTempoRamp()
    {
        // 100 bpm rising to 260 over 16 beats, with the host reporting the tempo a block late.
        Rig rig;
        rig.host.tempoCurve = [] (double ppq) { return 100.0 + 10.0 * ppq; };
        rig.host.reportLag = 1;

        std::vector<sim::Note> notes;

        for (int beat = 0; beat < 16; ++beat)
            notes.push_back ({ (double) beat, beat + 0.25, 60 + beat, 100 });

        recordSession (rig, notes, 0.0, 16.0);

        const auto& raw = rig.model.raw();
        CHECK_EQ ((int) raw.segments.size(), 1);   // no false jumps from the late tempo
        CHECK (raw.tempoMap.size() > 10);
        CHECK_NEAR (raw.tempoMap.front().bpm, 100.0, 1.0);
        CHECK (raw.tempoMap.back().bpm > 240.0);
        CHECK (sameNotes (rig.model.resolved(), notes, 1.0e-3, false));
    }

    void testTempoStepAtFortyFourPointOneKilohertz()
    {
        Rig rig (1u << 15, 44100.0, 512);
        rig.host.setMessages (sim::toMessages (makeClip (2)));
        rig.engine.requestArm();
        rig.host.play (0.0);
        rig.host.run (3.0);
        rig.host.setBpm (180.0);
        rig.host.run (5.0);
        rig.host.stop();

        CHECK_EQ ((int) rig.model.raw().tempoMap.size(), 2);
        CHECK_EQ ((int) rig.model.raw().segments.size(), 1);
        CHECK (sameNotes (rig.model.resolved(), makeClip (2)));
    }

    //==========================================================================
    void testRingOverflowMarksCaptureIncomplete()
    {
        // A tiny ring that nobody drains while recording.
        CaptureEngine engine (16);
        trs::HostBlock b;
        b.playing = b.hasPpq = b.hasBpm = true;
        b.bpm = 120.0;
        b.sampleRate = 48000.0;
        b.numSamples = 256;

        engine.requestArm();

        for (int i = 0; i < 100; ++i)
        {
            b.ppq = i * 0.0107;
            engine.process (b, nullptr, 0);
        }

        CHECK (engine.getDroppedCount() > 0);
        const auto kept = (int) engine.drain ([] (const Record&) {});
        CHECK_EQ (kept, 16);

        CaptureModel model;
        model.markIncomplete();
        CHECK (model.raw().incomplete);
    }

    void testIgnoresUnrelatedMidi()
    {
        Rig rig;
        rig.engine.requestArm();

        trs::HostBlock b;
        b.playing = b.hasPpq = b.hasBpm = true;
        b.bpm = 120.0;
        b.sampleRate = 48000.0;
        b.numSamples = 256;
        b.ppq = 0.0;

        trs::MidiEvent bend, cc1, on;
        bend.size = 3; bend.data[0] = 0xe0; bend.data[1] = 0; bend.data[2] = 64;
        cc1.size = 3;  cc1.data[0] = 0xb0;  cc1.data[1] = 1; cc1.data[2] = 99;
        on.size = 3;   on.data[0] = 0x90;   on.data[1] = 60; on.data[2] = 100;
        const trs::MidiEvent events[] = { bend, cc1, on };

        rig.engine.process (b, events, 3);
        const auto pending = (int) rig.engine.drain ([] (const Record&) {});
        CHECK_EQ (pending, 3);   // start, block, one note-on
    }

    void testNoteOnWithZeroVelocityIsANoteOff()
    {
        Rig rig;
        rig.engine.requestArm();
        trs::HostBlock b;
        b.playing = b.hasPpq = b.hasBpm = true;
        b.bpm = 120.0;
        b.sampleRate = 48000.0;
        b.numSamples = 256;

        trs::MidiEvent on, off;
        on.size = 3;  on.data[0] = 0x92;  on.data[1] = 40; on.data[2] = 100;
        off.size = 3; off.data[0] = 0x92; off.data[1] = 40; off.data[2] = 0;
        off.sampleOffset = 100;
        const trs::MidiEvent events[] = { on, off };

        rig.engine.process (b, events, 2);
        rig.host.drain();
        b.playing = false;
        rig.engine.process (b, nullptr, 0);
        rig.host.drain();

        CHECK_EQ ((int) rig.model.raw().notes.size(), 1);
        CHECK_EQ (rig.model.raw().notes[0].channel, 3);
        CHECK (! rig.model.raw().notes[0].heldAtStop);
        CHECK_NEAR (rig.model.raw().notes[0].offPpq - rig.model.raw().notes[0].onPpq, 100.0 / 48000.0 * 2.0, 1.0e-9);
    }

    void testBarsToPpq()
    {
        const std::vector<BarStart> bars { { 0.0, 4, 4 }, { 4.0, 3, 4 }, { 7.0, 3, 4 }, { 10.0, 4, 4 } };
        CHECK_NEAR (barsToPpq (bars, 0.0, 1), 4.0, 1.0e-9);
        CHECK_NEAR (barsToPpq (bars, 0.0, 3), 10.0, 1.0e-9);
        CHECK_NEAR (barsToPpq (bars, 4.0, 2), 6.0, 1.0e-9);
        CHECK_NEAR (barsToPpq (bars, 7.0, 3), 11.0, 1.0e-9);   // runs past the last known bar line: 4/4 continues
        CHECK_NEAR (barsToPpq ({}, 0.0, 2), 8.0, 1.0e-9);
        CHECK_EQ (ppqToBars (bars, 0.0, 10.0), 3);
        CHECK_EQ (ppqToBars (bars, 4.0, 6.0), 2);
        CHECK_EQ (ppqToBars (bars, 0.0, 0.0), 0);
    }

    //==========================================================================
    struct TestCase { const char* name; void (*fn)(); };

    const TestCase tests[] = {
        { "engine states", testEngineStates },
        { "basic capture", testBasicCapture },
        { "note held at stop", testHeldNoteAtStop },
        { "session loop lengths 1/2/4/8 bars", testSessionLoopLengths },
        { "session recording starts mid loop", testSessionStartsMidLoop },
        { "clip edited between passes", testEditedClipBetweenPasses },
        { "held note takes the previous pass's length", testHeldNoteTakesPreviousPassLength },
        { "clip sequence is written out in full", testClipSequenceIsWrittenOutInFull },
        { "three-minute piece, note for note", testLongPieceMatchesNoteForNote },
        { "silent recording is not a loop", testSilentRecordingIsNotALoop },
        { "small variation is still a loop", testSmallVariationStillALoop },
        { "arrangement loop with split blocks", testArrangementLoopWithSplitBlocks },
        { "arrangement loop edited between passes", testArrangementLoopEditedBetweenPasses },
        { "arrangement without a loop", testArrangementWithoutLoop },
        { "user jump is not a loop wrap", testUserJumpIsNotALoopWrap },
        { "meter changes", testMeterChanges },
        { "meter change keeps notes in place", testMeterChangeKeepsNotesInPlace },
        { "global meter change makes a short bar", testGlobalMeterChangeCreatesShortBar },
        { "tempo ramp", testTempoRamp },
        { "tempo step at 44.1 kHz", testTempoStepAtFortyFourPointOneKilohertz },
        { "ring overflow", testRingOverflowMarksCaptureIncomplete },
        { "unrelated midi is ignored", testIgnoresUnrelatedMidi },
        { "note-on with velocity 0", testNoteOnWithZeroVelocityIsANoteOff },
        { "bars <-> quarter notes", testBarsToPpq },
    };
}

int main()
{
    int failedTests = 0;

    for (const auto& t : tests)
    {
        const auto before = failures;
        currentTest = t.name;
        t.fn();
        currentTest = "";
        const bool ok = failures == before;
        failedTests += ok ? 0 : 1;
        std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", t.name);
    }

    std::printf ("\n%d checks, %d failed; %d of %d tests failed\n", checks, failures, failedTests, (int) (sizeof tests / sizeof tests[0]));
    return failures == 0 ? 0 : 1;
}
