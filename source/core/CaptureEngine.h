#pragma once

#include "CaptureTypes.h"

#include <atomic>
#include <cstddef>
#include <vector>

namespace trs
{

// The audio-thread half of the capture. process() is real-time safe: it allocates nothing,
// takes no locks and only copies small records into a single-producer/single-consumer ring.
//
//   idle --arm--> armed --transport plays--> recording --transport stops / Stop--> stopped
//
// Commands (arm, stop, reset) may come from any thread; they take effect at the next block.
class CaptureEngine
{
public:
    enum class State : int { idle, armed, recording, stopped };

    // capacity is rounded up to a power of two
    explicit CaptureEngine (size_t capacity = 1u << 15);

    void requestArm() noexcept   { command.store (cmdArm); }
    void requestStop() noexcept  { command.store (cmdStop); }
    void requestReset() noexcept { command.store (cmdReset); }

    State getState() const noexcept { return (State) state.load(); }

    // Audio thread only.
    void process (const HostBlock&, const MidiEvent* events, int numEvents) noexcept;

    // Consumer thread only. Calls f(const Record&) for every pending record; returns how many.
    template <typename Fn>
    size_t drain (Fn&& f)
    {
        const auto write = head.load (std::memory_order_acquire);
        auto read = tail.load (std::memory_order_relaxed);
        const auto count = write - read;

        for (; read != write; ++read)
            f (ring[read & mask]);

        tail.store (write, std::memory_order_release);
        return count;
    }

    // Records or events that could not be stored (ring full, too many events in one block).
    uint32_t getDroppedCount() const noexcept { return dropped.load(); }
    void addDropped (uint32_t n) noexcept     { dropped.fetch_add (n); }

private:
    enum Command : int { cmdNone = 0, cmdArm, cmdStop, cmdReset };

    void push (const Record&) noexcept;
    void pushStart (const HostBlock&) noexcept;
    void pushBlock (const HostBlock&) noexcept;
    void pushMidi (const MidiEvent&, double ppq, uint8_t flags) noexcept;
    void pushStop (double ppq, uint8_t flags) noexcept;

    static bool isRecordedMessage (const MidiEvent&) noexcept;
    static bool isReleaseMessage (const MidiEvent&) noexcept;

    std::vector<Record> ring;
    size_t mask = 0;
    std::atomic<size_t> head { 0 };   // written by the audio thread
    std::atomic<size_t> tail { 0 };   // written by the consumer
    std::atomic<int> command { cmdNone };
    std::atomic<int> state { (int) State::idle };
    std::atomic<uint32_t> dropped { 0 };

    double lastEndPpq = 0.0;          // audio thread only: end of the last recorded block
};

}  // namespace trs
