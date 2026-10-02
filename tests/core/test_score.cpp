// Phase 3 tests: JSON, the score model with undo/redo, versions and the saved document.

#include "SimHost.h"
#include "TestSupport.h"

#include "core/Commands.h"
#include "core/Document.h"
#include "core/Json.h"
#include "core/Score.h"

#include <cstdint>
#include <algorithm>
#include <cstring>
#include <random>
#include <set>

using namespace trs;

#define REGISTER(fn, name) static testing::Registrar registrar_##fn (name, fn)

namespace
{
    //==========================================================================
    // Json

    void testJsonRoundTrip()
    {
        auto o = Json::object();
        o.set ("int", (int64_t) 9007199254740993LL);   // beyond what a double holds exactly
        o.set ("neg", -42);
        o.set ("pi", 3.141592653589793);
        o.set ("third", 1.0 / 3.0);
        o.set ("tiny", 1.0e-300);
        o.set ("huge", 1.7976931348623157e308);
        o.set ("whole", 16.0);
        o.set ("yes", true);
        o.set ("no", false);
        o.set ("nothing", Json());
        o.set ("text", "quote \" backslash \\ newline \n tab \t bell \x07 slash / unicode \xc4\x8d\xc5\xa1 \xf0\x9f\x8e\xb5");
        auto a = Json::array();
        a.push (1);
        a.push ("two");
        a.push (Json::array());
        a.push (Json::object());
        o.set ("mixed", a);
        o.set ("numbers", Json::numbers ({ 0.1, 0.2, 0.30000000000000004, 0.0, 1.0e-7, 123456789.123456789 }));

        const auto text = o.dump();
        Json back;
        std::string error;
        CHECK (Json::parse (text, back, &error));
        CHECK_STR (error.c_str(), "");
        CHECK (back == o);
        CHECK_STR (back.dump().c_str(), text.c_str());
        CHECK_EQ (back.get ("int").asInt(), (int64_t) 9007199254740993LL);
        CHECK_STR (back.get ("text").asString().c_str(), o.get ("text").asString().c_str());
        CHECK (back.get ("numbers").numberAt (2) == 0.30000000000000004);   // exactly the same double
        CHECK (back.get ("third").asDouble() == 1.0 / 3.0);
        CHECK (back.get ("nothing").isNull());
        CHECK_EQ ((int) back.get ("mixed").size(), 4);
    }

    void testJsonOrderAndAccess()
    {
        Json j;
        CHECK (Json::parse (R"({"b":1,"a":2,"c":{"x":[1,2,3]}})", j));
        CHECK_STR (j.dump().c_str(), R"({"b":1,"a":2,"c":{"x":[1,2,3]}})");   // member order is kept
        CHECK_EQ (j.get ("c").get ("x").numberAt (1), 2.0);
        CHECK_EQ (j.get ("missing").asInt (7), 7);
        CHECK (j.get ("c").get ("nope").get ("deeper").isNull());
        CHECK (! j.has ("zzz"));
        CHECK_EQ (j.get ("a").asString ("fallback").size(), (size_t) 8);

        Json spaced;
        CHECK (Json::parse ("  \n { \"k\" : [ 1 , 2 ] }\t", spaced));
        CHECK_EQ ((int) spaced.get ("k").size(), 2);

        // a repeated member keeps the last value
        Json twice;
        CHECK (Json::parse (R"({"a":1,"a":2})", twice));
        CHECK_EQ (twice.get ("a").asInt(), (int64_t) 2);
        CHECK_EQ ((int) twice.keys().size(), 1);
    }

    void testJsonRejectsBadText()
    {
        for (const char* bad : { "", "{", "[1,2", "{\"a\":}", "{\"a\" 1}", "[1,]x", "tru", "\"unterminated", "{\"a\":1} extra",
                                 "[1 2]", "{1:2}", "\"\\x\"", "\"\\u12\"", "-", "[--1]", "nul" })
        {
            Json j;
            std::string error;
            const bool ok = Json::parse (bad, j, &error);
            CHECK (! ok);

            if (ok)
                std::printf ("    accepted: %s\n", bad);
            else
                CHECK (! error.empty());
        }

        // Deep nesting must fail cleanly, not overflow the stack.
        Json deep;
        std::string error;
        CHECK (! Json::parse (std::string (100000, '[') + std::string (100000, ']'), deep, &error));
        CHECK (error.find ("deeply") != std::string::npos);

        // The target is untouched by a failed parse.
        Json keep (5);
        CHECK (! Json::parse ("[", keep));
        CHECK_EQ (keep.asInt(), (int64_t) 5);
    }

    void testJsonUnicodeEscapes()
    {
        Json j;
        CHECK (Json::parse (R"("\u010d\u00e9 \ud83c\udfb5 \n")", j));
        CHECK_STR (j.asString().c_str(), "\xc4\x8d\xc3\xa9 \xf0\x9f\x8e\xb5 \n");
    }

    void testPackedArrays()
    {
        Json j;
        CHECK (Json::parse ("[1,2.5,-3]", j));
        CHECK_EQ (j.numberAt (1), 2.5);
        CHECK_EQ (j.numberAt (9, -1.0), -1.0);
        CHECK_EQ ((int) j.size(), 3);

        // pushing into a packed array unpacks it
        j.push ("x");
        CHECK_EQ ((int) j.size(), 4);
        CHECK_EQ (j.at (0).asInt(), (int64_t) 1);
        CHECK_STR (j.at (3).asString().c_str(), "x");
        CHECK_STR (j.dump().c_str(), R"([1,2.5,-3,"x"])");

        // a packed array equals the same numbers stored one by one
        auto plain = Json::array();
        plain.push (1);
        plain.push (2.5);
        plain.push (-3);
        CHECK (plain == Json::numbers ({ 1, 2.5, -3 }));
        CHECK (plain != Json::numbers ({ 1, 2.5, -4 }));

        // 600,000 numbers parse into one 4.8 MB vector, not 600,000 values
        std::vector<double> big (600000);

        for (size_t i = 0; i < big.size(); ++i)
            big[i] = (double) i * 0.001;

        Json bigJson = Json::numbers (big);
        Json parsed;
        CHECK (Json::parse (bigJson.dump(), parsed));
        CHECK_EQ ((int) parsed.size(), 600000);
        CHECK (parsed == bigJson);
    }

    //==========================================================================
    // Score

    struct Built
    {
        Score score;
        std::string part, staff, measure, layer;
        std::vector<std::string> notes;
    };

    // score -> part -> staff -> measure -> layer -> 4 notes, built with commands.
    Built buildScore (UndoManager* undo = nullptr)
    {
        Built b;
        UndoManager local (b.score);
        auto& um = undo != nullptr ? *undo : local;

        auto part = b.score.makeNode (nodeType::part);
        part.props["name"] = Json ("Piano");
        auto staff = b.score.makeNode (nodeType::staff);
        staff.props["clef"] = Json ("G");
        auto measure = b.score.makeNode (nodeType::measure);
        measure.props["meter"] = Json ("4/4");
        auto layer = b.score.makeNode (nodeType::layer);

        for (int i = 0; i < 4; ++i)
        {
            auto note = b.score.makeNode (nodeType::note);
            note.props["pitch"] = Json (60 + i);
            note.props["onset"] = Json (i * 960);
            note.props["dur"] = Json (960);
            b.notes.push_back (note.id);
            layer.children.push_back (std::move (note));
        }

        b.part = part.id;
        b.staff = staff.id;
        b.measure = measure.id;
        b.layer = layer.id;

        measure.children.push_back (std::move (layer));
        staff.children.push_back (std::move (measure));
        part.children.push_back (std::move (staff));
        CHECK (um.perform (std::make_unique<InsertNodeCommand> (b.score.root().id, 0, std::move (part))));
        return b;
    }

    void testScoreBuildAndValidate()
    {
        auto b = buildScore();
        CHECK_STR (b.score.validate().c_str(), "");
        CHECK_EQ ((int) b.score.countNodes(), 1 + 1 + 1 + 1 + 1 + 4);
        CHECK (b.score.find (b.notes[2]) != nullptr);
        CHECK_EQ (b.score.find (b.notes[2])->prop ("pitch").asInt(), (int64_t) 62);
        CHECK (b.score.find ("nope-1") == nullptr);

        size_t index = 99;
        const auto* parent = b.score.findParent (b.notes[3], &index);
        CHECK (parent != nullptr && parent->id == b.layer);
        CHECK_EQ ((int) index, 3);

        // every id is unique
        std::set<std::string> ids;
        std::vector<const Node*> stack { &b.score.root() };

        while (! stack.empty())
        {
            const auto* n = stack.back();
            stack.pop_back();
            CHECK (ids.insert (n->id).second);

            for (const auto& c : n->children)
                stack.push_back (&c);
        }
    }

    void testCommandsRejectBadEdits()
    {
        auto b = buildScore();
        UndoManager um (b.score);
        const auto before = b.score;
        const auto revision = b.score.revision();

        // a note cannot sit directly in a measure
        CHECK (! um.perform (std::make_unique<InsertNodeCommand> (b.measure, 0, b.score.makeNode (nodeType::note))));
        // unknown parent
        CHECK (! um.perform (std::make_unique<InsertNodeCommand> ("missing-1", 0, b.score.makeNode (nodeType::part))));
        // an id that is already in the score
        Node dup = b.score.makeNode (nodeType::note);
        dup.id = b.notes[0];
        CHECK (! um.perform (std::make_unique<InsertNodeCommand> (b.layer, 0, dup)));
        // a subtree that nests wrongly
        Node odd = b.score.makeNode (nodeType::measure);
        odd.children.push_back (b.score.makeNode (nodeType::note));
        CHECK (! um.perform (std::make_unique<InsertNodeCommand> (b.staff, 0, odd)));
        // missing targets
        CHECK (! um.perform (std::make_unique<SetPropertyCommand> ("missing-2", "x", Json (1))));
        CHECK (! um.perform (std::make_unique<RemoveNodeCommand> ("missing-3")));
        // the root cannot be removed
        CHECK (! um.perform (std::make_unique<RemoveNodeCommand> (b.score.root().id)));
        // list values are not allowed as properties
        CHECK (! um.perform (std::make_unique<SetPropertyCommand> (b.notes[0], "chordTones", Json::array())));
        // a node cannot move into its own subtree, or somewhere it does not belong
        CHECK (! um.perform (std::make_unique<MoveNodeCommand> (b.part, b.layer, 0)));
        CHECK (! um.perform (std::make_unique<MoveNodeCommand> (b.notes[0], b.measure, 0)));

        CHECK (! um.canUndo());
        CHECK (b.score == before);
        CHECK_EQ (b.score.revision(), revision);
    }

    void testUndoRedoEachCommand()
    {
        auto b = buildScore();
        UndoManager um (b.score);
        const auto original = b.score;

        // SetProperty: change, add, remove
        CHECK (um.perform (std::make_unique<SetPropertyCommand> (b.notes[1], "pitch", Json (70), "Change pitch")));
        CHECK_STR (um.undoName().c_str(), "Change pitch");
        CHECK_EQ (b.score.find (b.notes[1])->prop ("pitch").asInt(), (int64_t) 70);
        CHECK (um.perform (std::make_unique<SetPropertyCommand> (b.notes[1], "accid", Json ("s"))));
        CHECK (um.perform (std::make_unique<SetPropertyCommand> (b.notes[1], "vel", std::nullopt)));
        CHECK (! b.score.find (b.notes[1])->has ("vel"));

        // Insert and Remove
        auto extra = b.score.makeNode (nodeType::note);
        const auto extraId = extra.id;
        CHECK (um.perform (std::make_unique<InsertNodeCommand> (b.layer, 2, std::move (extra))));
        CHECK_STR (b.score.find (b.layer)->children[2].id.c_str(), extraId.c_str());
        CHECK (um.perform (std::make_unique<RemoveNodeCommand> (b.notes[0])));
        CHECK (b.score.find (b.notes[0]) == nullptr);

        // Move a note to a new measure
        auto measure2 = b.score.makeNode (nodeType::measure);
        auto layer2 = b.score.makeNode (nodeType::layer);
        const auto layer2Id = layer2.id;
        measure2.children.push_back (std::move (layer2));
        CHECK (um.perform (std::make_unique<InsertNodeCommand> (b.staff, 99, std::move (measure2))));
        CHECK (um.perform (std::make_unique<MoveNodeCommand> (b.notes[3], layer2Id, 0)));
        CHECK_EQ ((int) b.score.find (layer2Id)->children.size(), 1);
        CHECK_STR (b.score.validate().c_str(), "");

        const auto edited = b.score;

        // undo everything
        int steps = 0;

        while (um.undo())
            ++steps;

        CHECK_EQ (steps, 7);
        CHECK (b.score == original);
        CHECK (! um.canUndo() && um.canRedo());

        // redo everything
        while (um.redo())
            --steps;

        CHECK_EQ (steps, 0);
        CHECK (b.score == edited);
        CHECK (um.canUndo() && ! um.canRedo());

        // a new edit clears the redo history
        CHECK (um.undo());
        CHECK (um.canRedo());
        CHECK (um.perform (std::make_unique<SetPropertyCommand> (b.notes[2], "pitch", Json (1))));
        CHECK (! um.canRedo());
    }

    void testUndoGroups()
    {
        auto b = buildScore();
        UndoManager um (b.score);
        const auto original = b.score;

        // "Transpose" changes every note: one step
        um.beginGroup ("Transpose");

        for (const auto& id : b.notes)
            CHECK (um.perform (std::make_unique<SetPropertyCommand> (id, "pitch", Json (100))));

        um.endGroup();
        CHECK_STR (um.undoName().c_str(), "Transpose");
        CHECK_EQ (b.score.find (b.notes[3])->prop ("pitch").asInt(), (int64_t) 100);

        const auto transposed = b.score;
        CHECK (um.undo());
        CHECK (b.score == original);
        CHECK (! um.canUndo());
        CHECK (um.redo());
        CHECK (b.score == transposed);

        // an empty group leaves no step; a failed command inside a group is not recorded
        um.beginGroup ("Nothing");
        um.endGroup();
        CHECK_STR (um.undoName().c_str(), "Transpose");

        um.beginGroup ("Mixed");
        CHECK (um.perform (std::make_unique<SetPropertyCommand> (b.notes[0], "pitch", Json (1))));
        CHECK (! um.perform (std::make_unique<SetPropertyCommand> ("missing-9", "pitch", Json (1))));
        um.endGroup();
        CHECK (um.undo());
        CHECK_EQ (b.score.find (b.notes[0])->prop ("pitch").asInt(), (int64_t) 100);

        // undo() ends an open group first
        um.beginGroup ("Open");
        CHECK (um.perform (std::make_unique<SetPropertyCommand> (b.notes[1], "pitch", Json (5))));
        CHECK (um.undo());
        CHECK_EQ (b.score.find (b.notes[1])->prop ("pitch").asInt(), (int64_t) 100);
    }

    void testUndoLimit()
    {
        auto b = buildScore();
        UndoManager um (b.score, 5);

        for (int i = 0; i < 20; ++i)
            CHECK (um.perform (std::make_unique<SetPropertyCommand> (b.notes[0], "pitch", Json (i))));

        int steps = 0;

        while (um.undo())
            ++steps;

        CHECK_EQ (steps, 5);
        CHECK_EQ (b.score.find (b.notes[0])->prop ("pitch").asInt(), (int64_t) 14);
    }

    // Random edits, undone and redone: the score always comes back exactly.
    void testRandomEditsRoundTrip()
    {
        auto b = buildScore();
        UndoManager um (b.score, 100000);
        std::mt19937 rng (12345);

        auto allIds = [&] (const char* type)
        {
            std::vector<std::string> ids;
            std::vector<const Node*> stack { &b.score.root() };

            while (! stack.empty())
            {
                const auto* n = stack.back();
                stack.pop_back();

                if (n->type == type)
                    ids.push_back (n->id);

                for (const auto& c : n->children)
                    stack.push_back (&c);
            }

            std::sort (ids.begin(), ids.end());
            return ids;
        };

        auto pick = [&] (const std::vector<std::string>& v) { return v[rng() % v.size()]; };
        std::vector<Score> snapshots { b.score };
        int performed = 0;

        for (int step = 0; step < 3000; ++step)
        {
            const auto layers = allIds (nodeType::layer);
            const auto notes = allIds (nodeType::note);
            std::unique_ptr<Command> c;

            switch (rng() % 6)
            {
                case 0: c = std::make_unique<SetPropertyCommand> (pick (notes.empty() ? layers : notes), "k" + std::to_string (rng() % 4), Json ((int) (rng() % 100))); break;
                case 1: c = std::make_unique<SetPropertyCommand> (pick (notes.empty() ? layers : notes), "k" + std::to_string (rng() % 4), std::nullopt); break;
                case 2: c = std::make_unique<InsertNodeCommand> (pick (layers), rng() % 8, b.score.makeNode (nodeType::note)); break;
                case 3: if (! notes.empty()) c = std::make_unique<RemoveNodeCommand> (pick (notes)); break;
                case 4: if (! notes.empty()) c = std::make_unique<MoveNodeCommand> (pick (notes), pick (layers), rng() % 8); break;
                case 5: c = std::make_unique<InsertNodeCommand> (allIds (nodeType::staff).front(), rng() % 4,
                                                                  [&] { auto m = b.score.makeNode (nodeType::measure); m.children.push_back (b.score.makeNode (nodeType::layer)); return m; }()); break;
            }

            if (c != nullptr && um.perform (std::move (c)))
            {
                ++performed;

                if (performed % 250 == 0)
                    snapshots.push_back (b.score);
            }
        }

        CHECK (performed > 1500);
        CHECK_STR (b.score.validate().c_str(), "");
        const auto final = b.score;

        // step back through the snapshots
        int undone = 0;

        for (auto s = snapshots.size() - 1; s > 0; --s)
        {
            // undo until the score equals the snapshot before this one
            while (! (b.score == snapshots[s - 1]) && um.undo())
                ++undone;

            CHECK (b.score == snapshots[s - 1]);
            CHECK_STR (b.score.validate().c_str(), "");
        }

        while (um.undo())
            ++undone;

        CHECK_EQ (undone, performed);
        CHECK (b.score == snapshots.front());

        int redone = 0;

        while (um.redo())
            ++redone;

        CHECK_EQ (redone, performed);
        CHECK (b.score == final);
    }

    void testScoreJsonRoundTrip()
    {
        auto b = buildScore();
        UndoManager um (b.score);
        CHECK (um.perform (std::make_unique<SetPropertyCommand> (b.notes[0], "label", Json ("caf\xc3\xa9 \"x\""))));
        CHECK (um.perform (std::make_unique<SetPropertyCommand> (b.notes[1], "ghost", Json (true))));
        CHECK (um.perform (std::make_unique<SetPropertyCommand> (b.notes[2], "pos", Json (0.1 + 0.2))));

        Json parsed;
        CHECK (Json::parse (b.score.toJson().dump(), parsed));

        Score loaded;
        std::string error;
        CHECK (Score::fromJson (parsed, loaded, &error));
        CHECK_STR (error.c_str(), "");
        CHECK (loaded == b.score);
        CHECK_STR (loaded.toJson().dump().c_str(), b.score.toJson().dump().c_str());

        // ids handed out after loading never collide with the loaded ones
        auto fresh = loaded.makeNode (nodeType::note);
        CHECK (loaded.find (fresh.id) == nullptr);
        auto copy = loaded.cloneWithNewIds (*loaded.find (b.part));
        std::set<std::string> ids;
        std::vector<const Node*> stack { &copy };

        while (! stack.empty())
        {
            const auto* n = stack.back();
            stack.pop_back();
            CHECK (loaded.find (n->id) == nullptr);
            CHECK (ids.insert (n->id).second);

            for (const auto& c : n->children)
                stack.push_back (&c);
        }

        // and the copy can be inserted
        UndoManager um2 (loaded);
        CHECK (um2.perform (std::make_unique<InsertNodeCommand> (loaded.root().id, 99, copy)));
        CHECK_STR (loaded.validate().c_str(), "");
    }

    void testScoreJsonRejectsDamage()
    {
        auto b = buildScore();
        const auto good = b.score.toJson();
        Score target;
        std::string error;

        auto edited = [&] (const char* from, const char* to)
        {
            auto text = good.dump();
            const auto at = text.find (from);
            CHECK (at != std::string::npos);
            text.replace (at, std::strlen (from), to);
            Json j;
            CHECK (Json::parse (text, j));
            return j;
        };

        // the same id twice
        CHECK (! Score::fromJson (edited (("\"id\":\"" + b.notes[1] + "\"").c_str(), ("\"id\":\"" + b.notes[0] + "\"").c_str()), target, &error));
        CHECK (error.find ("twice") != std::string::npos);
        // a note straight under a measure
        CHECK (! Score::fromJson (edited ("\"type\":\"layer\"", "\"type\":\"note\""), target, &error));
        // not a score at all
        CHECK (! Score::fromJson (Json::object(), target, &error));
        CHECK (! Score::fromJson (edited ("\"type\":\"score\"", "\"type\":\"part\""), target, &error));
        // a failed load leaves the target as it was
        CHECK (target.countNodes() == 1);

        // a stale counter cannot cause a clash
        auto low = good;
        low.set ("nextId", 1);
        CHECK (Score::fromJson (low, target, &error));
        CHECK (target.find (target.makeNode (nodeType::note).id) == nullptr);
    }

    //==========================================================================
    // Documents

    RawCapture recordClip (double bpm, int bars, int variant)
    {
        CaptureEngine engine;
        CaptureModel model;
        sim::Host host (engine, model);
        host.setBpm (bpm);
        std::vector<sim::Note> notes;

        for (int j = 0; j < bars; ++j)
        {
            const double b = j * 4.0;
            notes.push_back ({ b, b + 1.0, 48 + (j + variant) % 24, 90 });
            notes.push_back ({ b + 1.0 / 3.0, b + 1.5, 60 + (j * 3 + variant) % 11, 100 });
            notes.push_back ({ b + 2.75, b + 6.0, 72 + variant, 110 });   // the last one is cut by the stop
        }

        host.setMessages (sim::toMessages (notes));
        engine.requestArm();
        host.play (0.0);
        host.run (bars * 4.0 - 1.0);
        host.stop();
        return model.raw();
    }

    bool sameCapture (const RawCapture& a, const RawCapture& b)
    {
        if (a.notes.size() != b.notes.size() || a.bars.size() != b.bars.size() || a.segments.size() != b.segments.size()
            || a.tempoMap.size() != b.tempoMap.size())
            return false;

        for (size_t i = 0; i < a.notes.size(); ++i)
        {
            const auto& x = a.notes[i];
            const auto& y = b.notes[i];

            if (x.onPpq != y.onPpq || x.offPpq != y.offPpq || x.pitch != y.pitch || x.velocity != y.velocity
                || x.channel != y.channel || x.heldAtStop != y.heldAtStop)
                return false;
        }

        for (size_t i = 0; i < a.bars.size(); ++i)
            if (a.bars[i].ppq != b.bars[i].ppq || a.bars[i].num != b.bars[i].num || a.bars[i].den != b.bars[i].den)
                return false;

        for (size_t i = 0; i < a.segments.size(); ++i)
        {
            const auto& x = a.segments[i];
            const auto& y = b.segments[i];

            if (x.songStartPpq != y.songStartPpq || x.songEndPpq != y.songEndPpq || x.capStartPpq != y.capStartPpq
                || x.capEndPpq != y.capEndPpq || x.wrap != y.wrap)
                return false;
        }

        for (size_t i = 0; i < a.tempoMap.size(); ++i)
            if (a.tempoMap[i].ppq != b.tempoMap[i].ppq || a.tempoMap[i].bpm != b.tempoMap[i].bpm)
                return false;

        return a.startPpq == b.startPpq && a.endPpq == b.endPpq && a.stopPpq == b.stopPpq && a.sampleRate == b.sampleRate
            && a.stopped == b.stopped && a.stoppedByUser == b.stoppedByUser && a.incomplete == b.incomplete;
    }

    Document makeDocument()
    {
        Document doc;

        for (int take = 0; take < 3; ++take)
        {
            const auto capture = recordClip (127.0 + take, 4 + 2 * take, take);
            doc.addVersion (capture, detectReading (capture).reading, detectReading (capture), 1790000000000LL + take);
        }

        return doc;
    }

    void testVersionManagement()
    {
        auto doc = makeDocument();
        CHECK_EQ ((int) doc.versions().size(), 3);
        CHECK_STR (doc.versions()[0].name.c_str(), "Take 1");
        CHECK_STR (doc.versions()[2].name.c_str(), "Take 3");
        CHECK_STR (doc.activeId().c_str(), doc.versions()[2].id.c_str());   // the newest take is active

        const auto id1 = doc.versions()[0].id, id2 = doc.versions()[1].id, id3 = doc.versions()[2].id;
        CHECK (id1 != id2 && id2 != id3 && id1 != id3);

        // select, rename
        CHECK (doc.select (id1));
        CHECK_STR (doc.active()->name.c_str(), "Take 1");
        CHECK (! doc.select ("ver-999"));
        CHECK_STR (doc.activeId().c_str(), id1.c_str());
        CHECK (doc.rename (id2, "Verse (keeper)"));
        CHECK (! doc.rename (id2, ""));
        CHECK (! doc.rename ("ver-999", "x"));
        CHECK_STR (doc.find (id2)->name.c_str(), "Verse (keeper)");

        // duplicate: placed after the source, becomes active, same capture, own id and score
        const auto copyId = doc.duplicate (id2, 1790000009999LL);
        CHECK (! copyId.empty() && copyId != id2);
        CHECK_STR (doc.versions()[2].id.c_str(), copyId.c_str());
        CHECK_STR (doc.find (copyId)->name.c_str(), "Verse (keeper) copy");
        CHECK_STR (doc.activeId().c_str(), copyId.c_str());
        CHECK (sameCapture (doc.find (copyId)->capture, doc.find (id2)->capture));
        CHECK_EQ (doc.find (copyId)->createdAtMs, (int64_t) 1790000009999LL);
        CHECK (doc.duplicate ("ver-999", 0).empty());

        // the copy's score is independent of the original's
        {
            auto* copy = doc.findMutable (copyId);
            UndoManager um (copy->score);
            auto part = copy->score.makeNode (nodeType::part);
            CHECK (um.perform (std::make_unique<InsertNodeCommand> (copy->score.root().id, 0, std::move (part))));
            CHECK_EQ ((int) doc.find (id2)->score.countNodes(), 1);
            CHECK_EQ ((int) doc.find (copyId)->score.countNodes(), 2);
        }

        // delete: the active version goes -> the one before it takes over
        CHECK (doc.remove (copyId));
        CHECK_STR (doc.activeId().c_str(), id2.c_str());
        CHECK (! doc.remove (copyId));
        CHECK (doc.remove (id1));                       // not active: the active one stays
        CHECK_STR (doc.activeId().c_str(), id2.c_str());
        CHECK (doc.select (id2));
        CHECK (doc.remove (id2));                       // first in the list: the new first takes over
        CHECK_STR (doc.activeId().c_str(), id3.c_str());
        CHECK (doc.remove (id3));
        CHECK (doc.versions().empty());
        CHECK (doc.activeId().empty() && doc.active() == nullptr);

        // numbering continues, ids are never reused
        const auto capture = recordClip (120.0, 2, 0);
        const auto fresh = doc.addVersion (capture, Reading {}, Detection {}, 5);
        CHECK (fresh != id1 && fresh != id2 && fresh != id3 && fresh != copyId);
        CHECK_STR (doc.find (fresh)->name.c_str(), "Take 4");
    }

    void testRevisionTracksChanges()
    {
        auto doc = makeDocument();
        auto r = doc.revision();
        const auto id = doc.versions()[0].id;

        CHECK (doc.select (id));
        CHECK (doc.revision() > r);
        r = doc.revision();

        CHECK (doc.select (id));   // already active: nothing changed
        CHECK_EQ (doc.revision(), r);

        CHECK (doc.rename (id, "x"));
        CHECK (doc.revision() > r);
        r = doc.revision();

        doc.findMutable (id)->setReading (readingWithMode (doc.find (id)->capture, doc.find (id)->reading, ReadingMode::oneLoop));
        CHECK (doc.revision() > r);
        r = doc.revision();

        auto* v = doc.findMutable (id);
        UndoManager um (v->score);
        CHECK (um.perform (std::make_unique<InsertNodeCommand> (v->score.root().id, 0, v->score.makeNode (nodeType::part))));
        CHECK (doc.revision() > r);
    }

    void testReadingIsPerVersion()
    {
        auto doc = makeDocument();
        auto* v = doc.findMutable (doc.versions()[0].id);
        const auto before = v->resolved().notes.size();
        v->setReading (readingWithLoopBars (v->capture, v->reading, 1));
        CHECK (v->reading.source == ReadingSource::user);
        CHECK (v->resolved().notes.size() != before || v->resolved().lengthPpq == 4.0);
        CHECK_EQ (v->resolved().lengthPpq, 4.0);
        // the raw capture is untouched
        CHECK (sameCapture (v->capture, recordClip (127.0, 4, 0)));
        // the other versions are untouched
        CHECK (doc.versions()[1].reading.source != ReadingSource::user);
    }

    void testDocumentRoundTrip()
    {
        auto doc = makeDocument();
        doc.rename (doc.versions()[1].id, "Chorus \xc4\x8d \"quoted\"");
        doc.uiPrefs.set ("zoom", 1.25);
        doc.uiPrefs.set ("view", "page");

        // edit a score, and a reading
        {
            auto* v = doc.findMutable (doc.versions()[0].id);
            UndoManager um (v->score);
            auto part = v->score.makeNode (nodeType::part);
            part.props["name"] = Json ("Piano");
            CHECK (um.perform (std::make_unique<InsertNodeCommand> (v->score.root().id, 0, std::move (part))));
            v->setReading (readingWithLoop (v->reading, 4.0, 8.0));
        }

        doc.select (doc.versions()[1].id);
        const auto original = doc;
        const auto text = doc.toJson().dump();

        Json parsed;
        std::string error;
        CHECK (Json::parse (text, parsed, &error));

        Document loaded;
        CHECK (Document::fromJson (parsed, loaded, &error) == LoadResult::ok);
        CHECK_STR (error.c_str(), "");

        // everything survived, bit for bit
        CHECK_STR (loaded.toJson().dump().c_str(), text.c_str());
        CHECK_EQ ((int) loaded.versions().size(), 3);
        CHECK_STR (loaded.activeId().c_str(), original.activeId().c_str());

        for (size_t i = 0; i < 3; ++i)
        {
            const auto& a = original.versions()[i];
            const auto& b = loaded.versions()[i];
            CHECK_STR (a.id.c_str(), b.id.c_str());
            CHECK_STR (a.name.c_str(), b.name.c_str());
            CHECK_EQ (a.createdAtMs, b.createdAtMs);
            CHECK (sameCapture (a.capture, b.capture));
            CHECK (a.score == b.score);
            CHECK (a.reading.mode == b.reading.mode && a.reading.loopStartPpq == b.reading.loopStartPpq
                   && a.reading.loopLengthPpq == b.reading.loopLengthPpq && a.reading.source == b.reading.source);
            CHECK_EQ (a.detection.candidateBars, b.detection.candidateBars);
            CHECK (a.detection.candidateMismatch == b.detection.candidateMismatch);
            CHECK (a.resolved().notes.size() == b.resolved().notes.size());
        }

        CHECK_EQ (loaded.uiPrefs.get ("zoom").asDouble(), 1.25);
        CHECK_STR (loaded.versions()[1].name.c_str(), "Chorus \xc4\x8d \"quoted\"");

        // new versions after loading get fresh ids and the next take number
        auto copy = loaded;
        const auto capture = recordClip (120.0, 2, 0);
        const auto fresh = copy.addVersion (capture, Reading {}, Detection {}, 1);

        for (const auto& v : original.versions())
            CHECK (v.id != fresh);

        CHECK_STR (copy.find (fresh)->name.c_str(), "Take 4");
    }

    void testEmptyDocumentRoundTrip()
    {
        Document empty;
        Document loaded;
        Json parsed;
        CHECK (Json::parse (empty.toJson().dump(), parsed));
        CHECK (Document::fromJson (parsed, loaded) == LoadResult::ok);
        CHECK (loaded.versions().empty() && loaded.activeId().empty());
    }

    void testLoadRejectsDamagedState()
    {
        auto doc = makeDocument();
        const auto good = doc.toJson().dump();
        Document target;
        std::string error;

        auto load = [&] (const std::string& text)
        {
            Json j;
            CHECK (Json::parse (text, j));
            return Document::fromJson (j, target, &error);
        };

        auto replaceFirst = [&] (const std::string& from, const std::string& to)
        {
            auto text = good;
            const auto at = text.find (from);
            CHECK (at != std::string::npos);
            text.replace (at, from.size(), to);
            return text;
        };

        CHECK (load (good) == LoadResult::ok);

        // no schema version / wrong type
        CHECK (load ("{}") == LoadResult::invalid);
        CHECK (load ("[]") == LoadResult::invalid);
        CHECK (load (replaceFirst ("\"schemaVersion\":1", "\"schemaVersion\":\"one\"")) == LoadResult::invalid);

        // a version id used twice
        CHECK (load (replaceFirst ("\"id\":\"ver-2\"", "\"id\":\"ver-1\"")) == LoadResult::invalid);

        // a notes array that does not divide into notes
        const auto notesAt = good.find ("\"notes\":[");
        CHECK (notesAt != std::string::npos);
        auto cut = good;
        cut.insert (notesAt + 9, "7,");
        CHECK (load (cut) == LoadResult::invalid);
        CHECK (error.find ("notes") != std::string::npos);

        // a note with an impossible pitch
        {
            auto bad = doc;
            bad.findMutable (bad.versions()[0].id)->capture.notes[0].pitch = 300;
            CHECK (load (bad.toJson().dump()) == LoadResult::invalid);
            CHECK (error.find ("pitch") != std::string::npos);
        }

        // a damaged score inside a version
        CHECK (load (replaceFirst ("\"root\":{\"id\":\"score-1\",\"type\":\"score\"", "\"root\":{\"id\":\"score-1\",\"type\":\"part\"")) == LoadResult::invalid);

        // a failed load leaves the target as it was
        Document untouched;
        auto j = Json::object();
        CHECK (Document::fromJson (j, untouched) == LoadResult::invalid);
        CHECK (untouched.versions().empty());

        // an active id that does not exist falls back to the newest version
        CHECK (load (replaceFirst ("\"activeVersionId\":\"ver-3\"", "\"activeVersionId\":\"ver-77\"")) == LoadResult::ok);
        CHECK_STR (target.activeId().c_str(), "ver-3");
    }

    void testSchemaMigration()
    {
        auto doc = makeDocument();
        Json v1 = doc.toJson();
        CHECK_EQ (v1.get ("schemaVersion").asInt(), (int64_t) Document::currentSchema);

        // a document from the future is refused, with its own result
        Json future = v1;
        future.set ("schemaVersion", Document::currentSchema + 1);
        Document target;
        std::string error;
        CHECK (Document::fromJson (future, target, &error) == LoadResult::tooNew);
        CHECK (error.find ("newer") != std::string::npos);

        // pretend the plugin is now at schema 3: step 1 -> 2 adds a field, step 2 -> 3 renames one
        int calls = 0;
        std::vector<Migration> steps;
        steps.push_back ([&] (Json& d, std::string*) { ++calls; d.set ("addedInTwo", "yes"); return true; });
        steps.push_back ([&] (Json& d, std::string*)
        {
            ++calls;
            d.set ("uiPrefs", d.get ("uiPrefs"));
            auto prefs = d.get ("uiPrefs");
            prefs.set ("migrated", true);
            d.set ("uiPrefs", prefs);
            return true;
        });

        Document migrated;
        CHECK (Document::fromJson (v1, migrated, &error, 3, steps) == LoadResult::ok);
        CHECK_EQ (calls, 2);
        CHECK (migrated.uiPrefs.get ("migrated").asBool());
        CHECK_EQ ((int) migrated.versions().size(), 3);

        // starting from schema 2, only the second step runs
        calls = 0;
        Json v2 = v1;
        v2.set ("schemaVersion", 2);
        CHECK (Document::fromJson (v2, migrated, &error, 3, steps) == LoadResult::ok);
        CHECK_EQ (calls, 1);

        // a missing step, and a failing step
        CHECK (Document::fromJson (v1, migrated, &error, 3, { steps[0] }) == LoadResult::invalid);
        CHECK (error.find ("no way to upgrade") != std::string::npos);
        std::vector<Migration> failing { [] (Json&, std::string* e) { if (e != nullptr) *e = "step failed"; return false; } };
        CHECK (Document::fromJson (v1, migrated, &error, 2, failing) == LoadResult::invalid);
        CHECK_STR (error.c_str(), "step failed");
    }

    void testLargeCaptureSize()
    {
        // A very long, dense recording: 100,000 notes.
        RawCapture big;
        big.startPpq = 0;
        big.endPpq = big.stopPpq = 50000.0;
        big.stopped = true;
        big.sampleRate = 48000.0;
        std::mt19937 rng (7);

        for (int i = 0; i < 100000; ++i)
        {
            RawNote n;
            n.onPpq = i * 0.5 + (rng() % 1000) / 1000.0 * 0.0001;
            n.offPpq = n.onPpq + 0.25 + (rng() % 7) * 0.125;
            n.pitch = 36 + (int) (rng() % 60);
            n.velocity = 40 + (int) (rng() % 80);
            big.notes.push_back (n);
        }

        for (int b = 0; b < 12500; ++b)
            big.bars.push_back ({ b * 4.0, 4, 4 });

        big.segments.push_back ({ 0, 50000, 0, 50000, false });
        big.tempoMap.push_back ({ 0, 127.0 });

        Document doc;
        doc.addVersion (big, Reading {}, Detection {}, 1);
        const auto text = doc.toJson().dump();

        std::printf ("    100,000-note capture: %zu bytes of JSON\n", text.size());
        CHECK (text.size() < 8 * 1024 * 1024);

        Json parsed;
        Document loaded;
        CHECK (Json::parse (text, parsed));
        CHECK (Document::fromJson (parsed, loaded) == LoadResult::ok);
        CHECK (sameCapture (loaded.versions()[0].capture, big));
    }

    REGISTER (testJsonRoundTrip, "json: round trip");
    REGISTER (testJsonOrderAndAccess, "json: order and access");
    REGISTER (testJsonRejectsBadText, "json: rejects bad text");
    REGISTER (testJsonUnicodeEscapes, "json: unicode escapes");
    REGISTER (testPackedArrays, "json: packed number arrays");
    REGISTER (testScoreBuildAndValidate, "score: build and validate");
    REGISTER (testCommandsRejectBadEdits, "score: bad edits are rejected");
    REGISTER (testUndoRedoEachCommand, "score: undo and redo every command");
    REGISTER (testUndoGroups, "score: undo groups");
    REGISTER (testUndoLimit, "score: undo limit");
    REGISTER (testRandomEditsRoundTrip, "score: 3000 random edits undo and redo exactly");
    REGISTER (testScoreJsonRoundTrip, "score: json round trip and fresh ids");
    REGISTER (testScoreJsonRejectsDamage, "score: json rejects damage");
    REGISTER (testVersionManagement, "versions: add, select, rename, duplicate, delete");
    REGISTER (testRevisionTracksChanges, "versions: revision tracks changes");
    REGISTER (testReadingIsPerVersion, "versions: the reading belongs to the version");
    REGISTER (testDocumentRoundTrip, "document: save and load round trip");
    REGISTER (testEmptyDocumentRoundTrip, "document: empty round trip");
    REGISTER (testLoadRejectsDamagedState, "document: damaged state is rejected");
    REGISTER (testSchemaMigration, "document: schema migration hook");
    REGISTER (testLargeCaptureSize, "document: 100,000-note capture");
}
