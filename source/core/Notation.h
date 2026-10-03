#pragma once

#include "Instruments.h"
#include "Json.h"
#include "Reading.h"
#include "Score.h"

#include <cstdint>
#include <string>
#include <vector>

// The transcription pipeline (plan section 5): it turns the notes of a recording, after the
// reading is applied, into a Score. Each stage is a separate function so it can be tested alone.
namespace trs
{

// One quarter note is 960 ticks: every straight value down to a 64th (60) and every triplet value
// (a third of a straight one) is a whole number of ticks.
constexpr int ticksPerQuarter = 960;

struct TranscriptionSettings
{
    int grid = 16;              // finest division kept: 4, 8, 16 or 32 (quarter ... 32nd notes)
    bool triplets = true;       // a beat may use the triplet grid when it fits better
    int splitPoint = 60;        // piano: notes from here up go to the right hand
    bool autoPickup = true;     // a first bar that starts late becomes a pickup bar
    int keyTonic = -1;          // -1 = detect the key; otherwise the tonic as a pitch class 0-11 ...
    bool keyMinor = false;      // ... and its mode

    Json toJson() const;
    static TranscriptionSettings fromJson (const Json&);
    bool operator== (const TranscriptionSettings&) const;
};

// A bar of the score, in ticks from the start of the transcription.
struct Bar
{
    int64_t start = 0;
    int64_t length = 0;
    int num = 4, den = 4;
    bool pickup = false;        // an incomplete first bar
    bool irregular = false;     // the host's bar is not as long as its meter says
};

// A note after quantisation.
struct QNote
{
    int64_t on = 0;             // ticks from the start
    int64_t dur = 0;            // ticks, at least one slot
    int pitch = 60, velocity = 90, channel = 1;
    int onError = 0;            // quantised onset minus played onset, in ticks
    int durError = 0;
    bool offGrid = false;       // moved by more than a quarter of a slot
    bool tripletBeat = false;   // its onset sits in a beat that uses the triplet grid
};

struct SpelledPitch
{
    char step = 'C';            // A-G
    int alter = 0;              // -2 ... +2 (flats negative)
    int octave = 4;             // scientific pitch notation (middle C is C4)
};

struct KeyResult
{
    int tonic = 0;              // pitch class
    bool minor = false;
    int fifths = 0;             // key signature: sharps positive, flats negative
    double correlation = 0.0;
    double scores[24] {};       // 0-11 major, 12-23 minor, by tonic pitch class
};

struct TranscriptionReport
{
    std::vector<std::string> warnings;
    int notes = 0;
    int offGridNotes = 0;
    int tripletBeats = 0;
    int mergedNotes = 0;        // duplicates and zero-length notes that were dropped
    int measures = 0;
    int maxVoices = 0;
};

struct TranscriptionResult
{
    Score score;
    KeyResult key;
    TranscriptionReport report;
    std::vector<Bar> bars;
    std::vector<QNote> notes;   // the quantised notes the score was made from
};

//==============================================================================
// Stage 1: clean-up. Drops notes of no length and repeats of the same note at the same moment.
// `dropped` counts them.
std::vector<ResolvedNote> cleanNotes (const std::vector<ResolvedNote>&, int* dropped = nullptr);

// Stage 2: the bars. The host's bar lines in ticks, an extra bar when the last note runs on, and an
// optional pickup bar. `endTick` is where the transcription has to reach (the end of the recording).
std::vector<Bar> buildBars (const ResolvedCapture&, int64_t endTick);

// A first bar that holds nothing until late becomes a pickup: it starts at the beat of the first note.
void applyPickup (std::vector<Bar>&, int64_t firstOnset);

// Stage 3: quantisation to the grid, choosing straight or triplet for every beat.
struct QuantizeResult
{
    std::vector<QNote> notes;                  // in order of onset, then pitch
    std::vector<std::pair<int64_t, bool>> beats;   // (bar-relative beat start, uses triplets) for the beats that hold onsets
    int offGrid = 0;
    int tripletBeats = 0;
};

QuantizeResult quantize (const std::vector<ResolvedNote>&, const std::vector<Bar>&, const TranscriptionSettings&);

// The grid slot sizes in ticks: straight and triplet.
int straightSlot (const TranscriptionSettings&);
int tripletSlot (const TranscriptionSettings&);

// Tempo marks (accepted rule): a mark where the tempo changes and then holds for at least one beat;
// a gradual change gets "accel." or "rit." followed by the new tempo.
struct TempoMark
{
    int64_t tick = 0;           // from the start of the transcription
    int bpm = 0;                // 0 for a pure text mark
    std::string text;           // "accel.", "rit." or empty
};

std::vector<TempoMark> tempoMarks (const ResolvedCapture&);

// Stage 6: the key, by Krumhansl-Schmuckler correlation with Temperley's (Kostka-Payne) profiles.
// Notes are weighted by their length.
KeyResult detectKey (const std::vector<QNote>&);

// The key signature (fifths) for a tonic and mode; `flatsPreferred` settles the enharmonic cases
// (F sharp / G flat major, ...).
int fifthsForKey (int tonic, bool minor, bool flatsPreferred);

// Stage 7: pitch spelling, PS13 (Meredith 2006). `onsets` and `pitches` are parallel and sorted by
// onset, then pitch. The algorithm needs one note whose letter is known to start from; by default
// that is the first note (spelled by sharps), which goes wrong for a piece that begins on a flat key's
// tonic. With `anchorTonic` (a pitch class) and `anchorLetter` (0 = A ... 6 = G) the tonic of the
// detected key is the starting point instead.
std::vector<SpelledPitch> spellPitches (const std::vector<int64_t>& onsets, const std::vector<int>& pitches,
                                        int anchorTonic = -1, int anchorLetter = 0);

// The letter (0 = A ... 6 = G) of the tonic of a key with this many sharps (flats negative).
int tonicLetterForKey (int fifths, bool minor);

//==============================================================================
// Stage 4: which staff and which voice each note belongs to.
struct VoiceEvent
{
    int64_t on = 0;
    int64_t dur = 0;
    std::vector<int> notes;     // indexes into the QNote list, one per pitch of the chord
};

struct StaffVoices
{
    std::vector<std::vector<VoiceEvent>> voices;   // voice 1 first
};

// Piano: two staves (right hand, left hand).
std::vector<StaffVoices> assignPianoVoices (const std::vector<QNote>&, const TranscriptionSettings&);

//==============================================================================
// Stage 5: durations. One piece of a note or rest after splitting at bar lines and the middle of
// 4/4, and after expressing the length in written values.
struct RhythmPiece
{
    int64_t on = 0;             // ticks from the start of the bar
    int64_t ticks = 0;          // sounding length
    int dur = 4;                // written value: 1, 2, 4, 8, 16, 32, 64
    int dots = 0;
    bool tuplet = false;        // 3 in the time of 2
    int64_t groupStart = -1;    // tuplet: where its group of three slots begins, from the bar start
    int64_t groupTicks = 0;     // tuplet: the length of that group
    bool tiedToNext = false;
    bool tiedFromPrevious = false;
};

// Ticks at which an event must be split inside a bar of this meter (the "imaginary barline").
std::vector<int64_t> splitPointsFor (int num, int den, int64_t barLength);

// Writes a length starting at `on` (ticks from the bar start) as pieces. `tripletBeats` holds the
// bar-relative starts of the beats that use the triplet grid, with the size in ticks of their slots (320, 160 or 80). `isRest` applies the stricter rule for rests.
std::vector<RhythmPiece> splitLength (int64_t on, int64_t length, int num, int den, int64_t barLength,
                                      const std::vector<std::pair<int64_t, int>>& tripletBeats, bool isRest);

// Beam groups of a bar: ticks lengths that add up to the bar.
std::vector<int64_t> beatGroups (int num, int den, int64_t barLength);

//==============================================================================
struct TranscriptionInput
{
    ResolvedCapture capture;
    TranscriptionSettings settings;
};

// The whole pipeline, for a piano.
TranscriptionResult transcribePiano (const ResolvedCapture&, const TranscriptionSettings&);

// Drums: one percussion staff, notes placed by the drum map. Notes the map does not know are left out
// with a warning.
TranscriptionResult transcribeDrums (const ResolvedCapture&, const TranscriptionSettings&, const DrumMap&);

// Where a note is played on a fretted instrument. `string` 0 is the lowest string.
struct TabNote
{
    int pitch = 0;
    int string = 0;
    int fret = 0;
};

// The strings and frets for a sequence of chords (each a list of MIDI notes), chosen so that the hand
// moves little and stays within a span. A chord comes back without the notes that cannot be played
// (out of range, or more than there are strings, or no way to play them together).
std::vector<std::vector<TabNote>> assignTab (const std::vector<std::vector<int>>& chords, const std::vector<int>& openStrings);

// Guitar or bass: standard notation (one voice, chords allowed) and a tablature staff under it.
TranscriptionResult transcribeFretted (const ResolvedCapture&, const TranscriptionSettings&, InstrumentType);

// Whichever the profile says.
TranscriptionResult transcribe (const ResolvedCapture&, const TranscriptionSettings&, const Profile&);

// A readable text form of a score for tests and for the page: one line per measure and voice,
// e.g. "m3 R v1: C5/8 D5/8 r/4 [C4 E4 G4]/2~".
std::string dumpScore (const Score&);

}  // namespace trs
