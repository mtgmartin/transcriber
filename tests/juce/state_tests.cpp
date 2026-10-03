// Tests that need JUCE: the compressed state format (StateCodec) and the CaptureService, which turns
// recordings into versions and saves and loads them. A simulated Live (tests/core/SimHost.h) plays
// notes into the service's engine; the service's own thread does the rest, as in the plugin.

#include "CaptureService.h"
#include "SimHost.h"
#include "StateCodec.h"
#include "TestSupport.h"

#include <atomic>
#include <random>
#include <thread>

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
        const auto original = makeJson (400000);
        statecodec::encode (original, good);
        std::string json;
        CHECK (good.getSize() > 100);

        auto copyOf = [&] { return juce::MemoryBlock (good.getData(), good.getSize()); };

        // cut off anywhere after the magic number
        for (const size_t keep : { (size_t) 4, (size_t) 10, (size_t) 15, (size_t) 16, (size_t) 23, (size_t) 24, good.getSize() / 2, good.getSize() - 1 })
        {
            const auto result = statecodec::decode (good.getData(), keep, json);

            // Losing only the last bytes of the gzip trailer leaves the text intact, and the checksum proves it.
            if (keep == good.getSize() - 1 && result == statecodec::Decoded::ok)
                CHECK (json == original);
            else
                CHECK (result == statecodec::Decoded::damaged);
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
        const auto versions = service.getStatus().getProperty ("versions", {});
        return versions.getArray() != nullptr ? versions.getArray()->size() : 0;
    }

    // juce::var::operator[] must not be given an index past the end, so look through getArray().
    juce::var versionAt (CaptureService& service, int index)
    {
        const auto versions = service.getStatus().getProperty ("versions", {});
        const auto* array = versions.getArray();
        return array != nullptr && index >= 0 && index < array->size() ? (*array)[index] : juce::var();
    }

    juce::String versionName (CaptureService& service, int index)
    {
        return versionAt (service, index).getProperty ("name", {}).toString();
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
        return versionAt (service, index).getProperty ("id", {}).toString();
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
            service.renameVersion (idOf (service, 1), juce::String::fromUTF8 ("Verse \xc4\x8d \"A\""));
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

    void testTranscriptionSettings()
    {
        CaptureService service;
        record (service, 4, 0);
        CHECK (waitForVersions (service, 1));

        auto status = service.getStatus();
        CHECK (status.getProperty ("settings", {}).isObject());
        CHECK_EQ ((int) status.getProperty ("settings", {}).getProperty ("grid", 0), 16);
        CHECK ((int) status.getProperty ("transcription", {}).getProperty ("measures", 0) >= 4);
        CHECK (status.getProperty ("scoreText", {}).toString().contains ("S1 v1:"));

        // a setting changes the score, and is kept
        service.setTranscriptionSetting ("grid", 8);
        service.setTranscriptionSetting ("splitPoint", 70);
        service.setTranscriptionSetting ("triplets", false);
        service.setTranscriptionSetting ("keyTonic", 7);
        service.setTranscriptionSetting ("keyMinor", false);
        service.setTranscriptionSetting ("nonsense", 1);
        service.setTranscriptionSetting ("splitPoint", 5000);   // out of range: brought back into range

        status = service.getStatus();
        const auto settings = status.getProperty ("settings", {});
        CHECK_EQ ((int) settings.getProperty ("grid", 0), 8);
        CHECK_EQ ((int) settings.getProperty ("splitPoint", 0), 108);
        CHECK (! (bool) settings.getProperty ("triplets", true));
        CHECK_EQ ((int) status.getProperty ("transcription", {}).getProperty ("keyTonic", -1), 7);
        CHECK_EQ ((int) status.getProperty ("transcription", {}).getProperty ("keyFifths", 99), 1);   // G major

        // the key in one step: tonic * 2 + minor (A minor), and back to G major
        service.setTranscriptionSetting ("key", 9 * 2 + 1);
        status = service.getStatus();
        CHECK_EQ ((int) status.getProperty ("settings", {}).getProperty ("keyTonic", -1), 9);
        CHECK ((bool) status.getProperty ("settings", {}).getProperty ("keyMinor", false));
        CHECK_EQ ((int) status.getProperty ("transcription", {}).getProperty ("keyFifths", 99), 0);
        service.setTranscriptionSetting ("key", 7 * 2);
        status = service.getStatus();
        CHECK_EQ ((int) status.getProperty ("transcription", {}).getProperty ("keyFifths", 99), 1);

        // saved and loaded: the settings come back and the score is made again from the recording
        juce::MemoryBlock saved;
        service.saveState (saved);
        CHECK (saved.getSize() < 8000);   // the score itself is not stored

        CaptureService again;
        again.loadState (saved.getData(), saved.getSize());
        const auto loaded = again.getStatus();
        CHECK_EQ ((int) loaded.getProperty ("settings", {}).getProperty ("grid", 0), 8);
        CHECK_STR (loaded.getProperty ("scoreText", {}).toString().toRawUTF8(), status.getProperty ("scoreText", {}).toString().toRawUTF8());
    }

    int numberOfDrumMaps (const juce::var& status)
    {
        const auto* list = status.getProperty ("drumMaps", {}).getArray();
        return list != nullptr ? list->size() : -1;
    }

    void testInstrumentsAndDrumMaps()
    {
        CaptureService service;
        record (service, 4, 0);
        CHECK (waitForVersions (service, 1));

        auto status = service.getStatus();
        CHECK_STR (status.getProperty ("instrument", {}).toString().toRawUTF8(), "piano");

        // the instrument changes the score of the shown take
        service.setInstrument ("guitar");
        status = service.getStatus();
        CHECK_STR (status.getProperty ("instrument", {}).toString().toRawUTF8(), "guitar");
        CHECK (status.getProperty ("scoreText", {}).toString().contains ("S2 v1:"));

        service.setInstrument ("drums");
        status = service.getStatus();
        CHECK_STR (status.getProperty ("instrument", {}).toString().toRawUTF8(), "drums");
        CHECK_STR (status.getProperty ("drumMapId", {}).toString().toRawUTF8(), "gm");
        CHECK_EQ (numberOfDrumMaps (status), 2);
        CHECK (status.getProperty ("scoreText", {}).toString().contains ("S1 v1:"));

        // a map of your own is stored, selected and used
        const juce::String mine = "{\"id\":\"mine\",\"name\":\"My kit\",\"entries\":[{\"note\":48,\"name\":\"Rack tom\",\"loc\":6,\"head\":\"normal\",\"voice\":1,\"ghostBelow\":0},"
                                  "{\"note\":60,\"name\":\"Pad\",\"loc\":9,\"head\":\"x\",\"voice\":1,\"ghostBelow\":0}]}";
        CHECK (service.saveDrumMap (mine).isEmpty());
        status = service.getStatus();
        CHECK_STR (status.getProperty ("drumMapId", {}).toString().toRawUTF8(), "mine");
        CHECK_EQ (numberOfDrumMaps (status), 3);
        CHECK (status.getProperty ("scoreText", {}).toString().contains ("Rack_tom"));
        CHECK (status.getProperty ("scoreText", {}).toString().contains ("Pad"));
        CHECK (service.getDrumMapJson ("mine").contains ("Rack tom"));

        // errors are reported, nothing changes
        CHECK (! service.saveDrumMap ("not json").isEmpty());
        CHECK (! service.saveDrumMap ("{\"entries\":3}").isEmpty());
        CHECK (! service.importDrumMap ("[]").isEmpty());
        CHECK_EQ (numberOfDrumMaps (service.getStatus()), 3);

        // a built-in map that was changed is saved as a copy
        CHECK (service.saveDrumMap ("{\"id\":\"gm\",\"name\":\"Edited GM\",\"entries\":[{\"note\":48,\"name\":\"Tom\",\"loc\":4}]}").isEmpty());
        status = service.getStatus();
        CHECK_STR (status.getProperty ("drumMapId", {}).toString().toRawUTF8(), "user-1");
        CHECK_EQ (numberOfDrumMaps (status), 4);
        CHECK (service.getDrumMapJson ("gm").contains ("Snare"));    // the built-in one is unchanged

        // an imported map whose id is taken gets a new one
        CHECK (service.importDrumMap (mine).isEmpty());
        status = service.getStatus();
        CHECK_STR (status.getProperty ("drumMapId", {}).toString().toRawUTF8(), "user-2");
        CHECK_EQ (numberOfDrumMaps (status), 5);

        // saved and loaded: the maps, the instrument and the score come back
        juce::MemoryBlock saved;
        service.saveState (saved);
        CaptureService again;
        again.loadState (saved.getData(), saved.getSize());
        const auto loaded = again.getStatus();
        CHECK_STR (loaded.getProperty ("instrument", {}).toString().toRawUTF8(), "drums");
        CHECK_STR (loaded.getProperty ("drumMapId", {}).toString().toRawUTF8(), "user-2");
        CHECK_EQ (numberOfDrumMaps (loaded), 5);
        CHECK_STR (loaded.getProperty ("scoreText", {}).toString().toRawUTF8(), status.getProperty ("scoreText", {}).toString().toRawUTF8());

        // choosing a built-in map, deleting the one in use
        service.selectDrumMap ("gm2");
        CHECK_STR (service.getStatus().getProperty ("drumMapId", {}).toString().toRawUTF8(), "gm2");
        service.selectDrumMap ("mine");
        service.deleteDrumMap ("mine");
        status = service.getStatus();
        CHECK_STR (status.getProperty ("drumMapId", {}).toString().toRawUTF8(), "gm");
        CHECK_EQ (numberOfDrumMaps (status), 4);
        service.deleteDrumMap ("gm");                                 // built-in maps cannot be deleted
        CHECK_EQ (numberOfDrumMaps (service.getStatus()), 4);

        // a new recording gets the instrument that was chosen last
        service.setInstrument ("bass");
        record (service, 2, 3);
        CHECK (waitForVersions (service, 2));
        CHECK_STR (service.getStatus().getProperty ("instrument", {}).toString().toRawUTF8(), "bass");
    }

    juce::var editRequest (const char* op, const juce::String& id, int semitones = 0)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("op", op);

        if (id.isNotEmpty())
            o->setProperty ("id", id);

        if (semitones != 0)
            o->setProperty ("semitones", semitones);

        return juce::var (o);
    }

    void testTitleAndComposer()
    {
        juce::MemoryBlock saved;

        {
            CaptureService service;
            record (service, 4, 0);
            CHECK (waitForVersions (service, 1));

            // no title: none in the status, an empty title in the MEI (the page then leaves the header out)
            auto status = service.getStatus();
            CHECK_EQ (status.getProperty ("title", "x").toString().length(), 0);
            CHECK (! service.getMei().getProperty ("mei", {}).toString().contains ("<composer>"));

            // a title and a composer with letters of Latin Extended: in the status and in the MEI, and the keys still match
            const auto keyBefore = service.getMei().getProperty ("key", {}).toString();
            service.setScoreMeta (juce::String::fromUTF8 ("Strofa \xc4\x8d & \"A\""), juce::String::fromUTF8 ("M. \xc4\x86osi\xc4\x87"));
            status = service.getStatus();
            CHECK_STR (status.getProperty ("title", {}).toString().toRawUTF8(), "Strofa \xc4\x8d & \"A\"");
            CHECK_STR (status.getProperty ("composer", {}).toString().toRawUTF8(), "M. \xc4\x86osi\xc4\x87");

            const auto mei = service.getMei();
            CHECK (mei.getProperty ("key", {}).toString() != keyBefore);
            CHECK (mei.getProperty ("key", {}).toString() == status.getProperty ("scoreKey", {}).toString());
            CHECK (mei.getProperty ("mei", {}).toString().contains (juce::String::fromUTF8 ("<title>Strofa \xc4\x8d &amp; &quot;A&quot;</title>")));
            CHECK (mei.getProperty ("mei", {}).toString().contains (juce::String::fromUTF8 ("<composer>M. \xc4\x86osi\xc4\x87</composer>")));

            // setting the same again does not change the key (the page would engrave the score again for nothing)
            service.setScoreMeta (juce::String::fromUTF8 ("Strofa \xc4\x8d & \"A\""), juce::String::fromUTF8 ("M. \xc4\x86osi\xc4\x87"));
            CHECK (service.getMei().getProperty ("key", {}).toString() == mei.getProperty ("key", {}).toString());

            service.saveState (saved);
        }

        // saved with the take and back
        CaptureService again;
        again.loadState (saved.getData(), saved.getSize());
        CHECK_STR (again.getStatus().getProperty ("title", {}).toString().toRawUTF8(), "Strofa \xc4\x8d & \"A\"");
        CHECK_STR (again.getStatus().getProperty ("composer", {}).toString().toRawUTF8(), "M. \xc4\x86osi\xc4\x87");
    }

    void testEditing()
    {
        CaptureService service;
        record (service, 4, 0);
        CHECK (waitForVersions (service, 1));

        auto status = service.getStatus();
        const auto original = status.getProperty ("scoreText", {}).toString();
        CHECK (! (bool) status.getProperty ("transcription", {}).getProperty ("edited", true));
        CHECK (! (bool) status.getProperty ("edit", {}).getProperty ("canUndo", true));
        CHECK (status.getProperty ("edit", {}).getProperty ("blocked", "x").toString().isEmpty());

        // the id of the first note on the page
        const auto mei = service.getMei().getProperty ("mei", {}).toString();
        const auto at = mei.indexOf ("<note xml:id=\"");
        CHECK (at >= 0);
        const auto id = mei.substring (at + 14).upToFirstOccurrenceOf ("\"", false, false);

        // an edit marks the score as edited and can be undone
        auto result = service.editScore (editRequest ("pitch", id, 1));
        CHECK ((bool) result.getProperty ("ok", false));
        status = service.getStatus();
        const auto edited = status.getProperty ("scoreText", {}).toString();
        CHECK (edited != original);
        CHECK ((bool) status.getProperty ("transcription", {}).getProperty ("edited", false));
        CHECK ((bool) status.getProperty ("edit", {}).getProperty ("canUndo", false));
        CHECK_STR (status.getProperty ("edit", {}).getProperty ("undoName", {}).toString().toRawUTF8(), "Change pitch");
        CHECK (service.getMei().getProperty ("key", {}).toString() == status.getProperty ("scoreKey", {}).toString());

        // an edited score is not written again: the settings are locked
        service.setTranscriptionSetting ("grid", 4);
        service.setInstrument ("guitar");
        status = service.getStatus();
        CHECK_EQ ((int) status.getProperty ("settings", {}).getProperty ("grid", 0), 16);
        CHECK_STR (status.getProperty ("scoreText", {}).toString().toRawUTF8(), edited.toRawUTF8());
        service.setInstrument ("piano");

        // a refused edit says why and changes nothing
        result = service.editScore (editRequest ("pitch", "no-such-id", 1));
        CHECK (! (bool) result.getProperty ("ok", true));
        CHECK (result.getProperty ("message", {}).toString().isNotEmpty());
        CHECK_STR (service.getStatus().getProperty ("scoreText", {}).toString().toRawUTF8(), edited.toRawUTF8());

        // a new key: the status shows it, undo takes it back
        {
            auto* k = new juce::DynamicObject();
            k->setProperty ("op", "key");
            k->setProperty ("fifths", 2);
            k->setProperty ("minor", false);
            CHECK ((bool) service.editScore (juce::var (k)).getProperty ("ok", false));
            CHECK_EQ ((int) service.getStatus().getProperty ("transcription", {}).getProperty ("keyFifths", 99), 2);
            CHECK ((bool) service.editScore (editRequest ("undo", {})).getProperty ("ok", false));
            CHECK_STR (service.getStatus().getProperty ("scoreText", {}).toString().toRawUTF8(), edited.toRawUTF8());
            CHECK_EQ ((int) service.getStatus().getProperty ("transcription", {}).getProperty ("keyFifths", 99),
                      (int) status.getProperty ("transcription", {}).getProperty ("keyFifths", 98));
        }

        // the edit is saved with the document and comes back as it was
        juce::MemoryBlock saved;
        service.saveState (saved);
        CaptureService again;
        again.loadState (saved.getData(), saved.getSize());
        auto loaded = again.getStatus();
        CHECK_STR (loaded.getProperty ("scoreText", {}).toString().toRawUTF8(), edited.toRawUTF8());
        CHECK ((bool) loaded.getProperty ("transcription", {}).getProperty ("edited", false));
        CHECK (! (bool) loaded.getProperty ("edit", {}).getProperty ("canUndo", true));   // the history is not saved

        // undo and redo
        CHECK ((bool) service.editScore (editRequest ("undo", {})).getProperty ("ok", false));
        CHECK_STR (service.getStatus().getProperty ("scoreText", {}).toString().toRawUTF8(), original.toRawUTF8());
        CHECK ((bool) service.getStatus().getProperty ("edit", {}).getProperty ("canRedo", false));
        CHECK ((bool) service.editScore (editRequest ("redo", {})).getProperty ("ok", false));
        CHECK_STR (service.getStatus().getProperty ("scoreText", {}).toString().toRawUTF8(), edited.toRawUTF8());

        // discarding the edits writes the score from the recording again, and unlocks the settings
        service.discardEdits();
        status = service.getStatus();
        CHECK_STR (status.getProperty ("scoreText", {}).toString().toRawUTF8(), original.toRawUTF8());
        CHECK (! (bool) status.getProperty ("transcription", {}).getProperty ("edited", true));
        CHECK (! (bool) status.getProperty ("edit", {}).getProperty ("canUndo", true));
        service.setTranscriptionSetting ("grid", 4);
        CHECK_EQ ((int) service.getStatus().getProperty ("settings", {}).getProperty ("grid", 0), 4);

        // guitar scores are edited (the tab follows), and so are drum scores
        service.setInstrument ("guitar");
        CHECK (service.getStatus().getProperty ("edit", {}).getProperty ("blocked", {}).toString().isEmpty());
        service.setInstrument ("drums");
        CHECK (service.getStatus().getProperty ("edit", {}).getProperty ("blocked", {}).toString().isEmpty());
    }


    // the ids of the notes of the MEI, in order (a chord has none of its own that are plain notes: its notes count)
    juce::StringArray noteIdsOf (const juce::String& mei)
    {
        juce::StringArray ids;

        for (int from = 0;;)
        {
            const auto at = mei.indexOf (from, "<note xml:id=\"");

            if (at < 0)
                break;

            ids.add (mei.substring (at + 14).upToFirstOccurrenceOf ("\"", false, false));
            from = at + 14;
        }

        return ids;
    }

    void testSelecting()
    {
        CaptureService service;
        record (service, 4, 0);
        CHECK (waitForVersions (service, 1));

        const auto ids = noteIdsOf (service.getMei().getProperty ("mei", {}).toString());
        CHECK (ids.size() >= 3);

        // a selection changes nothing: the score is not edited, there is nothing to undo, and the answer has the ids
        auto selected = service.editScore (editRequest ("selectAll", ids[0]));
        CHECK ((bool) selected.getProperty ("ok", false));
        CHECK ((bool) selected.getProperty ("readOnly", false));
        CHECK (selected.getProperty ("selection", {}).size() >= 3);
        auto status = service.getStatus();
        CHECK (! (bool) status.getProperty ("transcription", {}).getProperty ("edited", true));
        CHECK (! (bool) status.getProperty ("edit", {}).getProperty ("canUndo", true));

        // the same pitch: a list of ids comes back (the ids sent with the request are the measures to look in)
        selected = service.editScore (editRequest ("selectSame", ids[0]));
        CHECK ((bool) selected.getProperty ("ok", false));
        CHECK (selected.getProperty ("selection", {}).size() >= 1);

        // an edit of several notes: the answer says which are selected, and the score is edited once
        auto* o = new juce::DynamicObject();
        o->setProperty ("op", "pitch");
        o->setProperty ("id", ids[0]);
        o->setProperty ("semitones", 1);
        juce::Array<juce::var> list;
        list.add (ids[0]);
        list.add (ids[1]);
        list.add (ids[2]);
        o->setProperty ("ids", list);
        auto result = service.editScore (juce::var (o));
        CHECK ((bool) result.getProperty ("ok", false));
        CHECK ((bool) ! (bool) result.getProperty ("readOnly", true));
        CHECK_EQ ((int) result.getProperty ("selection", {}).size(), 3);
        CHECK_STR (result.getProperty ("select", {}).toString().toRawUTF8(), ids[0].toRawUTF8());
        status = service.getStatus();
        CHECK ((bool) status.getProperty ("transcription", {}).getProperty ("edited", false));
        CHECK_STR (status.getProperty ("edit", {}).getProperty ("undoName", {}).toString().toRawUTF8(), "Change pitch");

        // one undo takes back all three
        CHECK ((bool) service.editScore (editRequest ("undo", {})).getProperty ("ok", false));
        CHECK (! (bool) service.getStatus().getProperty ("edit", {}).getProperty ("canUndo", true));
    }


    void testTransposing()
    {
        CaptureService service;

        // before the first recording the transposition is chosen for the next one
        service.setTranscriptionSetting ("transpose", 2);
        CHECK_EQ ((int) service.getStatus().getProperty ("defaultTranspose", 99), 2);

        record (service, 4, 0);
        CHECK (waitForVersions (service, 1));
        auto status = service.getStatus();
        CHECK_EQ ((int) status.getProperty ("settings", {}).getProperty ("transpose", 99), 2);
        const auto up = status.getProperty ("scoreText", {}).toString();

        // changed on the take: the score is made again from the recording, and the next take gets the value too
        service.setTranscriptionSetting ("transpose", 0);
        status = service.getStatus();
        CHECK_EQ ((int) status.getProperty ("settings", {}).getProperty ("transpose", 99), 0);
        CHECK_EQ ((int) status.getProperty ("defaultTranspose", 99), 0);
        CHECK (status.getProperty ("scoreText", {}).toString() != up);

        // too much is brought back to 48
        service.setTranscriptionSetting ("transpose", 100);
        CHECK_EQ ((int) service.getStatus().getProperty ("settings", {}).getProperty ("transpose", 99), 48);

        // saved with the take and the default, and back
        service.setTranscriptionSetting ("transpose", -3);
        juce::MemoryBlock saved;
        service.saveState (saved);
        CaptureService again;
        again.loadState (saved.getData(), saved.getSize());
        CHECK_EQ ((int) again.getStatus().getProperty ("settings", {}).getProperty ("transpose", 99), -3);
        CHECK_EQ ((int) again.getStatus().getProperty ("defaultTranspose", 99), -3);

        // the next recording gets the default
        record (again, 4, 1);
        CHECK (waitForVersions (again, 2));
        CHECK_EQ ((int) again.getStatus().getProperty ("settings", {}).getProperty ("transpose", 99), -3);

        // an edited take is not written again: it keeps its transposition, the default still follows
        const auto ids = noteIdsOf (again.getMei().getProperty ("mei", {}).toString());
        CHECK (ids.size() >= 1);
        CHECK ((bool) again.editScore (editRequest ("pitch", ids[0], 1)).getProperty ("ok", false));
        again.setTranscriptionSetting ("transpose", 5);
        CHECK_EQ ((int) again.getStatus().getProperty ("settings", {}).getProperty ("transpose", 99), -3);
        CHECK_EQ ((int) again.getStatus().getProperty ("defaultTranspose", 99), 5);
    }


    void testTunings()
    {
        CaptureService service;
        service.setInstrument ("guitar");
        record (service, 4, 0);
        CHECK (waitForVersions (service, 1));

        auto status = service.getStatus();
        CHECK (status.getProperty ("tunings", {}).size() >= 5);
        CHECK_STR (status.getProperty ("settings", {}).getProperty ("tuning", {}).toString().toRawUTF8(), "auto");
        CHECK_STR (status.getProperty ("transcription", {}).getProperty ("tuningName", {}).toString().toRawUTF8(), "Standard");

        // a tuning from the list: the take is written again for those strings
        service.setTuning ("preset:dropd");
        status = service.getStatus();
        CHECK_STR (status.getProperty ("settings", {}).getProperty ("tuning", {}).toString().toRawUTF8(), "38 45 50 55 59 64");
        CHECK_STR (status.getProperty ("transcription", {}).getProperty ("tuningName", {}).toString().toRawUTF8(), "Drop D");
        CHECK (status.getProperty ("tuningMessage", {}).toString().isEmpty());

        // typed by the user
        service.setTuning ("D2 A2 D3 G3 B3 D4");
        status = service.getStatus();
        CHECK_STR (status.getProperty ("settings", {}).getProperty ("tuning", {}).toString().toRawUTF8(), "38 45 50 55 59 62");
        CHECK_STR (status.getProperty ("transcription", {}).getProperty ("tuningName", {}).toString().toRawUTF8(), "Double drop D");

        // what is wrong is said, and nothing changes
        service.setTuning ("D2 A2 D3");
        status = service.getStatus();
        CHECK (status.getProperty ("tuningMessage", {}).toString().contains ("4 to 8"));
        CHECK_STR (status.getProperty ("settings", {}).getProperty ("tuning", {}).toString().toRawUTF8(), "38 45 50 55 59 62");
        service.setTuning ("banana");
        CHECK (service.getStatus().getProperty ("tuningMessage", {}).toString().isNotEmpty());
        service.setTuning ("auto");
        CHECK (service.getStatus().getProperty ("tuningMessage", {}).toString().isEmpty());
        CHECK_STR (service.getStatus().getProperty ("settings", {}).getProperty ("tuning", {}).toString().toRawUTF8(), "auto");

        // an edited score is not written again: the tuning is an edit (one undo step), and Automatic is not possible
        const auto ids = noteIdsOf (service.getMei().getProperty ("mei", {}).toString());
        CHECK (ids.size() >= 1);
        CHECK ((bool) service.editScore (editRequest ("pitch", ids[0], 1)).getProperty ("ok", false));
        service.setTuning ("preset:dropd");
        status = service.getStatus();
        CHECK_STR (status.getProperty ("transcription", {}).getProperty ("tuningName", {}).toString().toRawUTF8(), "Drop D");
        CHECK_STR (status.getProperty ("edit", {}).getProperty ("undoName", {}).toString().toRawUTF8(), "Change tuning");
        CHECK_STR (status.getProperty ("settings", {}).getProperty ("tuning", {}).toString().toRawUTF8(), "auto");   // the settings are not used any more
        service.setTuning ("auto");
        CHECK (service.getStatus().getProperty ("tuningMessage", {}).toString().contains ("discard"));
        CHECK ((bool) service.editScore (editRequest ("undo", {})).getProperty ("ok", false));
        CHECK_STR (service.getStatus().getProperty ("transcription", {}).getProperty ("tuningName", {}).toString().toRawUTF8(), "Standard");

        // for a piano there are no strings (the edits go first: an edited score keeps its instrument)
        service.discardEdits();
        service.setInstrument ("piano");
        CHECK_EQ (service.getStatus().getProperty ("tunings", {}).size(), 0);
    }


    //==========================================================================
    // Phase 9: stress

    // five plugin instances at once (five tracks), each recording its own take on its own thread
    void testFiveInstancesAtOnce()
    {
        constexpr int count = 5;
        std::vector<std::unique_ptr<CaptureService>> services;
        std::atomic<int> failures { 0 };

        for (int i = 0; i < count; ++i)
            services.push_back (std::make_unique<CaptureService>());

        std::vector<std::thread> threads;

        for (int i = 0; i < count; ++i)
        {
            threads.emplace_back ([&, i]
            {
                record (*services[(size_t) i], 4 + i, i);

                if (! waitForVersions (*services[(size_t) i], 1))
                    ++failures;
            });
        }

        for (auto& t : threads)
            t.join();

        CHECK_EQ ((int) failures, 0);

        for (int i = 0; i < count; ++i)
        {
            auto& s = *services[(size_t) i];
            CHECK_EQ (s.getNumVersions(), 1);
            const auto status = s.getStatus();
            CHECK_EQ ((int) status.getProperty ("versions", {}).getArray()->getFirst().getProperty ("notes", 0), 3 * (4 + i));
            CHECK (status.getProperty ("scoreText", {}).toString().isNotEmpty());

            // each one is saved and loaded on its own
            juce::MemoryBlock saved;
            s.saveState (saved);
            CaptureService again;
            again.loadState (saved.getData(), saved.getSize());
            CHECK_EQ (again.getNumVersions(), 1);
        }
    }

    // play and stop again and again: every recording with notes is a take, a silent one is none
    void testRapidPlayAndStop()
    {
        CaptureService service;
        CaptureModel unused;
        sim::Host host (service.getEngine(), unused, 48000.0, 256, false);
        host.setBpm (180.0);
        std::vector<sim::Note> notes { { 0.0, 0.5, 60, 100 }, { 0.5, 1.0, 64, 100 } };
        int expected = 0;

        for (int i = 0; i < 60; ++i)
        {
            const bool silent = i % 2 == 1;
            host.setMessages (silent ? std::vector<sim::Msg>() : sim::toMessages (notes));
            service.arm();
            host.play (0.0);
            host.run (1.5);
            host.stop();

            if (! silent)
            {
                ++expected;
                CHECK (waitForVersions (service, expected));
            }
            else
            {
                juce::Thread::sleep (20);
            }
        }

        CHECK_EQ (service.getNumVersions(), expected);
        CHECK_EQ (versionCount (service), expected);

        juce::MemoryBlock saved;
        service.saveState (saved);
        CaptureService again;
        again.loadState (saved.getData(), saved.getSize());
        CHECK_EQ (again.getNumVersions(), expected);
    }

    // dense MIDI: hundreds of notes in every beat, many of them the same note at the same time
    void testDenseMidi()
    {
        CaptureService service;
        CaptureModel unused;
        sim::Host host (service.getEngine(), unused, 48000.0, 256, false);
        host.setBpm (140.0);
        std::mt19937 rng (17);
        std::vector<sim::Note> notes;

        for (int beat = 0; beat < 32; ++beat)
            for (int k = 0; k < 150; ++k)
            {
                const double on = beat + (double) (rng() % 1000) / 1000.0;
                notes.push_back ({ on, on + 0.05 + (double) (rng() % 300) / 1000.0, 30 + (int) (rng() % 60), 1 + (int) (rng() % 127) });
            }

        host.setMessages (sim::toMessages (notes));
        service.arm();
        host.play (0.0);
        host.run (33.0);
        host.stop();
        CHECK (waitForVersions (service, 1));

        // every instrument can write it, without taking long, and the page can get the score
        const auto started = juce::Time::getMillisecondCounterHiRes();

        for (const char* instrument : { "piano", "guitar", "bass", "drums" })
        {
            service.setInstrument (instrument);
            const auto status = service.getStatus();
            CHECK (status.getProperty ("scoreText", {}).toString().isNotEmpty());
            CHECK (service.getMei().getProperty ("mei", {}).toString().isNotEmpty());
        }

        std::printf ("    dense MIDI (4800 notes, 32 beats) in four instruments: %.0f ms\n", juce::Time::getMillisecondCounterHiRes() - started);
        CHECK (juce::Time::getMillisecondCounterHiRes() - started < 120000.0);
    }

    // a ten minute song: it is recorded, written, saved and loaded within a sensible time and size
    void testTenMinuteTake()
    {
        CaptureService service;
        CaptureModel unused;
        sim::Host host (service.getEngine(), unused, 48000.0, 256, false);
        host.setBpm (120.0);
        std::mt19937 rng (23);
        std::vector<sim::Note> notes;

        for (int beat = 0; beat < 1200; ++beat)
        {
            notes.push_back ({ (double) beat, beat + 0.9, 48 + (int) (rng() % 12), 90 });
            notes.push_back ({ beat + 0.5, beat + 0.95, 60 + (int) (rng() % 24), 100 });

            if (beat % 4 == 0)
                notes.push_back ({ (double) beat, beat + 3.5, 36 + (int) (rng() % 7), 85 });
        }

        host.setMessages (sim::toMessages (notes));
        service.arm();
        const auto started = juce::Time::getMillisecondCounterHiRes();
        host.play (0.0);
        host.run (1201.0);
        host.stop();
        CHECK (waitForVersions (service, 1));
        const auto status = service.getStatus();
        CHECK (status.getProperty ("scoreText", {}).toString().isNotEmpty());
        CHECK ((int) status.getProperty ("transcription", {}).getProperty ("measures", 0) >= 300);

        juce::MemoryBlock saved;
        service.saveState (saved);
        CaptureService again;
        again.loadState (saved.getData(), saved.getSize());
        CHECK_EQ (again.getNumVersions(), 1);
        std::printf ("    ten minute take: %.0f ms, saved state %d KB\n", juce::Time::getMillisecondCounterHiRes() - started, (int) (saved.getSize() / 1024));
        CHECK (saved.getSize() < 5 * 1024 * 1024);
        CHECK (juce::Time::getMillisecondCounterHiRes() - started < 120000.0);
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
        { "service: transcription settings", testTranscriptionSettings },
        { "service: instruments and drum maps", testInstrumentsAndDrumMaps },
        { "service: title and composer", testTitleAndComposer },
        { "service: editing the score", testEditing },
        { "service: selecting several notes", testSelecting },
        { "service: transposition", testTransposing },
        { "service: tunings", testTunings },
        { "stress: five instances at once", testFiveInstancesAtOnce },
        { "stress: rapid play and stop", testRapidPlayAndStop },
        { "stress: dense MIDI", testDenseMidi },
        { "stress: a ten minute take", testTenMinuteTake },
    };
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);   // keep the output if a test crashes
    int failedTests = 0;

    for (const auto& t : tests)
    {
        const auto before = testing::failures;
        testing::currentTest = t.name;
        std::printf ("RUN   %s\n", t.name);
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
