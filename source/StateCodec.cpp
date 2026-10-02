#include "StateCodec.h"

#include <algorithm>

namespace statecodec
{

namespace
{
    constexpr int magic = 0x43535254;   // 'T' 'R' 'S' 'C' in a little-endian int
    constexpr int containerFormat = 1;
    constexpr int headerBytes = 24;
    enum Compression { stored = 0, gzip = 1 };

    juce::uint64 checksum (const std::string& text)
    {
        juce::uint64 hash = 1469598103934665603ull;

        for (const auto c : text)
        {
            hash ^= (juce::uint8) c;
            hash *= 1099511628211ull;
        }

        return hash;
    }
}

void encode (const std::string& json, juce::MemoryBlock& result)
{
    juce::MemoryBlock compressed;

    {
        juce::MemoryOutputStream sink (compressed, false);
        juce::GZIPCompressorOutputStream gz (sink, 6, juce::GZIPCompressorOutputStream::windowBitsGZIP);
        gz.write (json.data(), json.size());
        gz.flush();
    }

    const auto useGzip = compressed.getSize() > 0 && compressed.getSize() < json.size();

    juce::MemoryOutputStream out (result, false);
    out.writeInt (magic);
    out.writeInt (containerFormat);
    out.writeInt (useGzip ? gzip : stored);
    out.writeInt ((int) json.size());
    out.writeInt64 ((juce::int64) checksum (json));

    if (useGzip)
        out.write (compressed.getData(), compressed.getSize());
    else
        out.write (json.data(), json.size());

    out.flush();
}

Decoded decode (const void* data, size_t size, std::string& json)
{
    if (data == nullptr || size < 4)
        return Decoded::notOurs;

    juce::MemoryInputStream in (data, size, false);

    if (in.readInt() != magic)
        return Decoded::notOurs;

    if (size < (size_t) headerBytes)
        return Decoded::damaged;

    const auto format = in.readInt();
    const auto compression = in.readInt();
    const auto jsonSize = in.readInt();
    const auto expectedChecksum = (juce::uint64) in.readInt64();

    if (format != containerFormat || (compression != stored && compression != gzip)
        || jsonSize < 0 || (size_t) jsonSize > maxJsonBytes)
        return Decoded::damaged;

    const auto* payload = static_cast<const char*> (data) + headerBytes;
    const auto payloadSize = size - (size_t) headerBytes;

    if (compression == stored)
    {
        if (payloadSize != (size_t) jsonSize)
            return Decoded::damaged;

        std::string text (payload, payloadSize);

        if (checksum (text) != expectedChecksum)
            return Decoded::damaged;

        json = std::move (text);
        return Decoded::ok;
    }

    juce::MemoryInputStream compressed (payload, payloadSize, false);
    juce::GZIPDecompressorInputStream gz (&compressed, false, juce::GZIPDecompressorInputStream::gzipFormat, (juce::int64) jsonSize);

    std::string text (static_cast<size_t> (jsonSize), '\0');
    size_t got = 0;

    while (got < text.size())
    {
        const auto wanted = (int) std::min<size_t> (1u << 20, text.size() - got);
        const auto read = gz.read (&text[got], wanted);

        if (read <= 0)
            break;

        got += (size_t) read;
    }

    // The data must end where the header says, and match its checksum.
    char extra = 0;

    if (got != text.size() || gz.read (&extra, 1) > 0 || checksum (text) != expectedChecksum)
        return Decoded::damaged;

    json = std::move (text);
    return Decoded::ok;
}

}  // namespace statecodec
