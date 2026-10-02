#pragma once

// A small Standard MIDI File reader for the tests: format 0 and 1, notes, tempo and time signature.

#include "core/Notation.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace midireader
{
    struct File
    {
        trs::ResolvedCapture capture;
        bool ok = false;
        std::string error;
    };

    inline File read (const std::string& path)
    {
        File out;
        std::ifstream in (path, std::ios::binary);

        if (! in)
        {
            out.error = "cannot open " + path;
            return out;
        }

        const std::vector<uint8_t> d ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        size_t pos = 0;

        auto u32 = [&] { auto v = ((uint32_t) d[pos] << 24) | ((uint32_t) d[pos + 1] << 16) | ((uint32_t) d[pos + 2] << 8) | d[pos + 3]; pos += 4; return v; };
        auto u16 = [&] { auto v = (uint32_t) ((d[pos] << 8) | d[pos + 1]); pos += 2; return v; };

        if (d.size() < 14 || std::string (d.begin(), d.begin() + 4) != "MThd")
        {
            out.error = "not a MIDI file";
            return out;
        }

        pos = 4;
        u32();
        u16();   // format
        const auto tracks = u16();
        const auto division = u16();

        if (division & 0x8000)
        {
            out.error = "SMPTE timing is not supported";
            return out;
        }

        struct Meter { double ppq; int num, den; };
        std::vector<Meter> meters;
        std::vector<trs::TempoPoint> tempos;
        double lastTick = 0.0;

        for (uint32_t t = 0; t < tracks && pos + 8 <= d.size(); ++t)
        {
            if (std::string (d.begin() + (long) pos, d.begin() + (long) pos + 4) != "MTrk")
            {
                out.error = "bad track";
                return out;
            }

            pos += 4;
            const auto length = u32();
            const auto end = pos + length;
            uint64_t tick = 0;
            uint8_t running = 0;
            std::map<int, std::pair<uint64_t, int>> held;   // channel*128+pitch -> start tick, velocity

            auto vlq = [&]
            {
                uint32_t v = 0;

                while (pos < end)
                {
                    const auto b = d[pos++];
                    v = (v << 7) | (b & 0x7f);

                    if (! (b & 0x80))
                        break;
                }

                return v;
            };

            while (pos < end)
            {
                tick += vlq();
                uint8_t status = d[pos];

                if (status & 0x80)
                    ++pos;
                else
                    status = running;

                if (status == 0xff)
                {
                    const auto type = d[pos++];
                    const auto len = vlq();

                    if (type == 0x51 && len == 3)
                    {
                        const auto micros = ((uint32_t) d[pos] << 16) | ((uint32_t) d[pos + 1] << 8) | d[pos + 2];
                        tempos.push_back ({ (double) tick / division, 60000000.0 / micros });
                    }
                    else if (type == 0x58 && len >= 2)
                    {
                        meters.push_back ({ (double) tick / division, d[pos], 1 << d[pos + 1] });
                    }

                    pos += len;
                    continue;
                }

                if (status == 0xf0 || status == 0xf7)
                {
                    pos += vlq();
                    continue;
                }

                running = status;
                const auto kind = status & 0xf0;
                const auto channel = status & 0x0f;
                const auto data1 = d[pos++];
                const auto data2 = (kind == 0xc0 || kind == 0xd0) ? (uint8_t) 0 : d[pos++];

                auto finish = [&] (uint8_t pitch)
                {
                    const auto it = held.find (channel * 128 + pitch);

                    if (it == held.end())
                        return;

                    trs::ResolvedNote n;
                    n.pitch = pitch;
                    n.velocity = it->second.second;
                    n.channel = channel + 1;
                    n.onPpq = (double) it->second.first / division;
                    n.offPpq = (double) tick / division;
                    out.capture.notes.push_back (n);
                    held.erase (it);
                };

                if (kind == 0x90 && data2 > 0)
                    held[channel * 128 + data1] = { tick, data2 };
                else if (kind == 0x80 || kind == 0x90)
                    finish (data1);
            }

            pos = end;
            lastTick = std::max (lastTick, (double) tick / division);
        }

        std::stable_sort (out.capture.notes.begin(), out.capture.notes.end(), [] (const trs::ResolvedNote& a, const trs::ResolvedNote& b)
        {
            return a.onPpq != b.onPpq ? a.onPpq < b.onPpq : a.pitch < b.pitch;
        });

        if (meters.empty())
            meters.push_back ({ 0.0, 4, 4 });

        if (tempos.empty())
            tempos.push_back ({ 0.0, 120.0 });

        out.capture.lengthPpq = lastTick;
        out.capture.tempoMap = tempos;

        // bar lines from the time signatures
        for (size_t i = 0; i < meters.size(); ++i)
        {
            const auto until = i + 1 < meters.size() ? meters[i + 1].ppq : lastTick;
            const auto barLength = meters[i].num * 4.0 / meters[i].den;

            for (double b = meters[i].ppq; b < until - 1.0e-9; b += barLength)
                out.capture.bars.push_back ({ b, meters[i].num, meters[i].den });
        }

        out.ok = true;
        return out;
    }
}
