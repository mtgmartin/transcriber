#pragma once

#include "CaptureTypes.h"
#include "Reading.h"

#include <array>

namespace trs
{

// The non-realtime half of the capture: turns the engine's records into a RawCapture and
// keeps the reading (One loop / As played) that is applied to it. Not thread-safe; whoever
// owns it serialises access.
class CaptureModel
{
public:
    CaptureModel();

    void consume (const Record&);
    void clear();

    // Records were lost before reaching the model.
    void markIncomplete();

    const RawCapture& raw() const noexcept        { return capture; }
    const Reading& reading() const noexcept       { return currentReading; }
    const Detection& detection() const noexcept   { return lastDetection; }

    // How many recordings have finished. Never reset, so a caller can tell a new one has stopped.
    uint64_t recordingsStopped() const noexcept { return stopCounter; }

    // Increases whenever the capture or the reading changes.
    uint64_t revision() const noexcept { return revisionCounter; }

    // The user's choices. They replace whatever was detected until the next recording.
    void setMode (ReadingMode);
    void setLoopBars (int numBars);
    void setLoop (double startPpq, double lengthPpq);
    void redetect();

    int getLoopBars() const;

    const ResolvedCapture& resolved() const;

private:
    void onStart (const Record&);
    void onBlock (const Record&);
    void onMidi (const Record&);
    void onStop (const Record&);
    void closeNote (int channelIndex, int pitch, double ppq, bool heldAtStop);
    void closeAll (int channelIndex, double ppq, bool heldAtStop);

    RawCapture capture;
    Reading currentReading;
    Detection lastDetection;
    uint64_t revisionCounter = 0;
    uint64_t stopCounter = 0;

    bool haveBlock = false;
    double songEnd = 0.0;          // song position at the end of the previous block
    double offset = 0.0;           // capture time - song time, in the current segment
    std::array<std::array<int, 128>, 16> openNote {};   // index into capture.notes, or -1

    mutable ResolvedCapture cache;
    mutable uint64_t cacheRevision = ~(uint64_t) 0;
};

}  // namespace trs
