            std::string out;
            CHECK (statecodec::decode (bad.getData(), bad.getSize(), out) == statecodec::Decoded::damaged);// Tests that need JUCE: the compressed state format (StateCodec) and the CaptureService, which turns
// recordings into versions and saves and loads them. A simulated Live (tests/core/SimHost.h) plays
// notes into the service's engine; the service's own thread does the rest, as in the plugin.

#include "CaptureService.h"
#include "SimHost.h"
#include "StateCodec.h"
#include "TestSupport.h"

#include <random>

using namespace trs;

namespace
{
    std::string makeJson (size_t approxBytes)
    {
        auto notes = Json::array();

        for (size_t i = 0; notes.size() < approxBytes / 8; ++i)
            notes.push (Json (0.125 * (double) i));

        auto o = Json::object();
        o.set ("what", "test");
        o.set ("numbers", notes);
        return o.dump();
    }

    void testCodecRoundTrips()
    {
        std::mt19937 rng (3);
        std::string incompressible (50000, '\0');

        for (auto& c : incompressible)
            c = (char) (rng() & 0xff);

        for (const auto& original : { std::string(), std::string ("{}"), makeJson (200), makeJson (2000000), incompressible })
        {
            juce::MemoryBlock block;
            statecodec::encode (original, block);
            CHECK (block.getSize() >= 24);

            std::string back;
            CHECK (statecodec::decode (block.getData(), block.getSize(), back) == statecodec::Decoded::ok);
            CHECK (back == original);

            // compressible data shrinks; random data is stored, at most a header larger
            if (original.size() > 100000 && original != incompressible)
                CHECK (block.getSize() < original.size() / 3);

            if (original == incompressible)
                CHECK_EQ ((int) block.getSize(), (int) original.size() + 24);
        }
    }

    void testCodecIgnoresForeignData()
    {
        std::string json;
        CHECK (statecodec::decode (nullptr, 0, json) == statecodec::Decoded::notOurs);

        const char legacy[] = "TRS1\x05\x00\x00\x00 and some Phase 1 test data after the header";
        CHECK (statecodec::decode (legacy, sizeof legacy, json) == statecodec::Decoded::notOurs);

        const char tiny[] = "ab";
        CHECK (statecodec::decode (tiny, 2, json) == statecodec::Decoded::notOurs);
    }

    void testCodecDetectsDamage()
    {
        juce::MemoryBlock good;
        statecodec::encode (makeJson (400000), good);
        std::string json;
        CHECK (good.getSize() > 100);

        auto copyOf = [&] { return juce::MemoryBlock (good.getData(), good.getSize()); };

        // cut off anywhere after the magic number
        for (const size_t keep : { (size_t) 4, (size_t) 10, (size_t) 15, (size_t) 16, (size_t) 23, (size_t) 24, good.getSize() / 2, good.getSize() - 1 })
        {
            CHECK (statecodec::decode (good.getData(), keep, json) == statecodec::Decoded::damaged);
        }

        // a changed container format, compression kind or size
        for (const size_t offset : { (size_t) 4, (size_t) 8, (size_t) 12, (size_t) 16, (size_t) 23 })
        {
            auto bad = copyOf();
            static_cast<juce::uint8*> (bad.getData())[offset] ^= 0x55;
            CHECK (statecodec::decode (bad.getData(), bad.getSize(), json) == statecodec::Decoded::damaged);
        }

        // a size field that claims far too much
        {
            auto bad = copyOf();
            static_cast<juce::uint8*> (bad.getData())[15] = 0x7f;
            CHECK (statecodec::decode (bad.getData(), bad.getSize(), json) == statecodec::Decoded::damaged);
        }

        // damage inside the compressed data, at several places
        for (const size_t at : { (size_t) 20, good.getSize() / 3, good.getSize() / 2, good.getSize() - 20 })
        {
            auto bad = copyOf();

            for (size_t i = 0; i < 8; ++i)
                static_cast<juce::uint8*> (bad.getData())[at + i] ^= 0xff;

            std::string out;
            CHECK (statecodec::decode (bad.getData(), bad.getSize(), out) == statecodec::Decoded::damaged);
        }
    }

    //==========================================================================
    // The service

    // Plays a short clip into the service's engine and stops the transport.
    void record (CaptureService& service, int bars, int variant, bool withNotes = true)
    {
        CaptureModel unused;
        sim::Host host (service.getEngine(), unused, 48000.0, 256, false);
        host.setBpm (127.0);

        std::vector<sim::Note> notes;

        for (int j = 0; withNotes && j < bars; ++j)
        {
            const double b = j * 4.0;
            notes.push_back ({ b, b + 1.0, 48 + (j + variant) % 24, 90 });
            notes.push_back ({ b + 1.0 / 3.0, b + 1.5, 60 + (j * 3 + variant) % 11, 100 });
            notes.push_back ({ b + 2.75, b + 3.0, 72 + variant, 110 });
        }

        host.setMessages (sim::toMessages (notes));
        service.arm();
        host.play (0.0);
        host.run (bars * 4.0 + 0.5);
        host.stop();
    }

    bool waitForVersions (CaptureService& service, int count)
    {
        for (int i = 0; i < 500; ++i)
        {
            if (service.getNumVersions() >= count)
                return true;

            juce::Thread::sleep (10);
        }

        return false;
    }

    int versionCount (CaptureService& service)
    {
        return service.getStatus().getProperty ("versions", {}).size();
    }

    juce::String versionName (CaptureService& service, int index)
    {
        return service.getStatus().getProperty ("versions", {})[index].getProperty ("name", {}).toString();
    }

    juce::String activeName (CaptureService& service)
    {
        const auto status = service.getStatus();
        const auto active = status.getProperty ("activeVersion", {}).toString();

        const auto versions = status.getProperty ("versions", {});

        for (const auto& v : *versions.getArray())
            if (v.getProperty ("id", {}).toString() == active)
                return v.getProperty ("name", {}).toString();

        return {};
    }

    juce::String idOf (CaptureService& service, int index)
    {
        return service.getStatus().getProperty ("versions", {})[index].getProperty ("id", {}).toString();
    }

    void testThreeTakesSurviveSavingAndLoading()
    {
        juce::MemoryBlock saved;
        std::vector<CapturedNote> notesBefore[3];
        juce::String before;

        {
            CaptureService service;

            for (int take = 0; take < 3; ++take)
            {
                record (service, 4 + take, take);
                CHECK (waitForVersions (service, take + 1));
                notesBefore[take] = service.getResolvedNotes();   // the newest take is the active one
            }

            CHECK_EQ (versionCount (service), 3);
            CHECK_STR (versionName (service, 0).toRawUTF8(), "Take 1");
            CHECK_STR (versionName (service, 2).toRawUTF8(), "Take 3");
            CHECK_STR (activeName (service).toRawUTF8(), "Take 3");

            // each take has its own notes
            CHECK_EQ ((int) notesBefore[0].size(), 12);
            CHECK_EQ ((int) notesBefore[2].size(), 18);

            // reading edits and names belong to the version
            service.selectVersion (idOf (service, 1));
            service.setLoopBars (2);
            service.renameVersion (idOf (service, 1), "Verse \xc4\x8d \"A\"");
            notesBefore[1] = service.getResolvedNotes();
            CHECK_EQ ((int) service.getStatus().getProperty ("loopBars", 0), 2);
            CHECK (notesBefore[1].size() > 0 && notesBefore[1].size() < 15);

            service.saveState (saved);
            before = juce::JSON::toString (service.getStatus());
        }

        CHECK (saved.getSize() > 100);

        // a new session: everything is back, the raw captures are unchanged
        CaptureService again;
        again.loadState (saved.getData(), saved.getSize());
        CHECK_EQ (versionCount (again), 3);
        CHECK_STR (versionName (again, 1).toRawUTF8(), "Verse \xc4\x8d \"A\"");
        CHECK_STR (activeName (again).toRawUTF8(), "Verse \xc4\x8d \"A\"");

        for (int take = 0; take < 3; ++take)
        {
            again.selectVersion (idOf (again, take));
            const auto notes = again.getResolvedNotes();
            CHECK_EQ ((int) notes.size(), (int) notesBefore[take].size());

            for (size_t i = 0; i < notes.size() && i < notesBefore[take].size(); ++i)
                CHECK (notes[i].onPpq == notesBefore[take][i].onPpq && notes[i].offPpq == notesBefore[take][i].offPpq
                       && notes[i].pitch == notesBefore[take][i].pitch);
        }

        // saving again gives the same document
        juce::MemoryBlock resaved;
        again.selectVersion (idOf (again, 1));
        again.saveState (resaved);
        CHECK (resaved == saved);

        // the loaded session keeps working: a new recording adds take 4
        record (again, 2, 5);
        CHECK (waitForVersions (again, 4));
        CHECK_STR (versionName (again, 3).toRawUTF8(), "Take 4");
        CHECK_EQ (again.getStatus().getProperty ("loadMessage", {}).toString().length(), 0);
    }

    void testVersionActions()
    {
        CaptureService service;
        record (service, 4, 0);
        record (service, 4, 1);
        CHECK (waitForVersions (service, 2));

        const auto first = idOf (service, 0), second = idOf (service, 1);

        service.duplicateVersion (first);
        CHECK_EQ (versionCount (service), 3);
        CHECK_STR (versionName (service, 1).toRawUTF8(), "Take 1 copy");
        CHECK_STR (activeName (service).toRawUTF8(), "Take 1 copy");

        service.renameVersion (second, "   Chorus   ");
        CHECK_STR (versionName (service, 2).toRawUTF8(), "Chorus");
        service.renameVersion (second, "   ");   // an empty name is refused
        CHECK_STR (versionName (service, 2).toRawUTF8(), "Chorus");

        service.deleteVersion (idOf (service, 1));   // the active copy
        CHECK_EQ (versionCount (service), 2);
        CHECK_STR (activeName (service).toRawUTF8(), "Take 1");

        service.deleteVersion (first);
        service.deleteVersion (second);
        CHECK_EQ (versionCount (service), 0);
        CHECK (service.getResolvedNotes().empty());

        // with nothing selected, reading edits are ignored
        service.setMode (ReadingMode::oneLoop);
        service.redetect();
        CHECK_EQ (versionCount (service), 0);
    }

    void testEmptyRecordingMakesNoVersion()
    {
        CaptureService service;
        record (service, 4, 0, false);

        // wait until the stop has certainly been handled
        for (int i = 0; i < 100; ++i)
        {
            if ((bool) service.getStatus().getProperty ("lastStopEmpty", false))
                break;

            juce::Thread::sleep (10);
        }

        CHECK ((bool) service.getStatus().getProperty ("lastStopEmpty", false));
        CHECK_EQ (versionCount (service), 0);

        // a real recording after it
        record (service, 4, 0);
        CHECK (waitForVersions (service, 1));
        CHECK (! (bool) service.getStatus().getProperty ("lastStopEmpty", true));
    }

    void testUnreadableStateIsKept()
    {
        // a good state from this version, then damaged in several ways
        juce::MemoryBlock good;

        {
            CaptureService s;
            record (s, 4, 0);
            CHECK (waitForVersions (s, 1));
            s.saveState (good);
        }

        // damaged
        juce::MemoryBlock damaged (good.getData(), good.getSize());
        static_cast<juce::uint8*> (damaged.getData())[good.getSize() / 2] ^= 0xff;
        static_cast<juce::uint8*> (damaged.getData())[good.getSize() / 2 + 1] ^= 0xff;
        static_cast<juce::uint8*> (damaged.getData())[good.getSize() / 2 + 2] ^= 0xff;

        // from a newer Transcriber: valid, but a schema we do not know
        auto future = Document().toJson();
        future.set ("schemaVersion", Document::currentSchema + 1);
        juce::MemoryBlock newer;
        statecodec::encode (future.dump(), newer);

        // valid compression, but not JSON
        juce::MemoryBlock notJson;
        statecodec::encode ("this is not json {", notJson);

        for (const auto* bytes : { &damaged, &newer, &notJson })
        {
            CaptureService s;
            s.loadState (bytes->getData(), bytes->getSize());
            CHECK_EQ (versionCount (s), 0);
            CHECK (s.getStatus().getProperty ("loadMessage", {}).toString().isNotEmpty());

            // saving without recording anything writes the same bytes back
            juce::MemoryBlock out;
            s.saveState (out);
            CHECK (out == *bytes);

            // recording something new replaces it, and clears the message
            record (s, 2, 0);
            CHECK (waitForVersions (s, 1));
            s.saveState (out);
            CHECK (! (out == *bytes));
            CHECK_EQ (s.getStatus().getProperty ("loadMessage", {}).toString().length(), 0);

            CaptureService t;
            t.loadState (out.getData(), out.getSize());
            CHECK_EQ (versionCount (t), 1);
        }

        // old test states and empty states are not an error
        CaptureService s;
        const char legacy[] = "TRS1 old phase one test state, not ours";
        s.loadState (legacy, sizeof legacy);
        CHECK_EQ (versionCount (s), 0);
        s.loadState (nullptr, 0);
        CHECK_EQ (versionCount (s), 0);
        CHECK_EQ (s.getStatus().getProperty ("loadMessage", {}).toString().length(), 0);
    }

    void testStateSizeIsReported()
    {
        CaptureService service;
        record (service, 8, 0);
        CHECK (waitForVersions (service, 1));
        juce::Thread::sleep (50);

        const auto status = service.getStatus();
        const auto reported = (juce::int64) status.getProperty ("stateBytes", 0);

        juce::MemoryBlock saved;
        service.saveState (saved);
        CHECK_EQ ((int) reported, (int) saved.getSize());
        CHECK (! (bool) status.getProperty ("stateTooBig", true));
        CHECK_EQ ((int) (juce::int64) status.getProperty ("stateWarnBytes", 0), (int) statecodec::warnBytes);
    }

    struct Test { const char* name; void (*fn)(); };

    const Test tests[] = {
        { "codec: round trips", testCodecRoundTrips },
        { "codec: foreign data is ignored", testCodecIgnoresForeignData },
        { "codec: damage is detected", testCodecDetectsDamage },
        { "service: three takes survive saving and loading", testThreeTakesSurviveSavingAndLoading },
        { "service: duplicate, rename, delete", testVersionActions },
        { "service: an empty recording makes no version", testEmptyRecordingMakesNoVersion },
        { "service: an unreadable state is kept", testUnreadableStateIsKept },
        { "service: the state size is reported", testStateSizeIsReported },
    };
}

int main()
{
    int failedTests = 0;

    for (const auto& t : tests)
    {
        const auto before = testing::failures;
        testing::currentTest = t.name;
        t.fn();
        testing::currentTest = "";
        const bool ok = testing::failures == before;
        failedTests += ok ? 0 : 1;
        std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", t.name);
    }

    std::printf ("\n%d checks, %d failed; %d of %d tests failed\n", testing::checks, testing::failures, failedTests,
                 (int) (sizeof tests / sizeof tests[0]));
    return testing::failures == 0 ? 0 : 1;
}
