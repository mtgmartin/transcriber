#pragma once

#include "CaptureTypes.h"

namespace trs
{

// How the passes of a looping clip are turned into one score.
enum class ReadingMode { oneLoop, asPlayed };

// Who decided: the host (Arrangement loop wrap), the note-pattern search, or the user.
enum class ReadingSource { none, host, detected, user };

struct Reading
{
    ReadingMode mode = ReadingMode::asPlayed;

    // Where the loop starts and how long it is, in capture time. Kept even in asPlayed mode
    // when a loop was found, so switching to One loop has a sensible starting point.
    // loopLengthPpq <= 0 means no loop is known.
    double loopStartPpq = 0.0;
    double loopLengthPpq = 0.0;

    ReadingSource source = ReadingSource::none;
};

struct Detection
{
    Reading reading;
    double candidateMismatch = 1.0;   // share of differing bars for the best loop candidate
    int candidateBars = 0;            // its length in bars (0 = none)
};

// Chooses a reading from a finished capture: the host's loop wrap if there was one,
// otherwise the shortest bar pattern that repeats through the whole recording.
Detection detectReading (const RawCapture&);

struct ResolvedNote
{
    double onPpq = 0.0, offPpq = 0.0;   // relative to ResolvedCapture::originPpq
    int pitch = 0, velocity = 0, channel = 1;
    int pass = 0;                       // which pass of the loop the note comes from
};

// The notes the score is built from, after the reading has been applied.
struct ResolvedCapture
{
    Reading reading;
    double originPpq = 0.0;             // capture time of time 0 in this result
    double lengthPpq = 0.0;
    std::vector<ResolvedNote> notes;    // in order of onPpq
    std::vector<BarStart> bars;         // relative to originPpq
    std::vector<TempoPoint> tempoMap;   // relative to originPpq
    int numPasses = 1;
    bool barsComplete = true;           // false if the bar lines had to come from a partial pass
};

// One loop: every pass overwrites the previous one (see docs/BUILD_PLAN.md, section 2).
// As played: every pass is written out in order.
ResolvedCapture resolve (const RawCapture&, const Reading&);

// The user's choices, applied to a reading. They mark the reading as set by the user.
// With no loop known yet, One loop starts with the first bar.
Reading readingWithMode (const RawCapture&, Reading current, ReadingMode);
Reading readingWithLoopBars (const RawCapture&, Reading current, int numBars);   // also switches to One loop
Reading readingWithLoop (Reading current, double startPpq, double lengthPpq);    // also switches to One loop
int loopBarsOf (const RawCapture&, const Reading&);

// Length of numBars bars starting at startPpq, following the host's bar lines.
double barsToPpq (const std::vector<BarStart>& bars, double startPpq, int numBars);

// How many bars of the host's bar lines fit in [startPpq, startPpq + lengthPpq).
int ppqToBars (const std::vector<BarStart>& bars, double startPpq, double lengthPpq);

}  // namespace trs
