#pragma once

#include <juce_core/juce_core.h>

#include <string>

// The bytes the host stores for the plugin: a small header, then the document's JSON, gzip
// compressed when that makes it smaller.
//
//   4 bytes  "TRSC"
//   4 bytes  container format (1)   -- how the bytes are wrapped; the schema version lives inside the JSON
//   4 bytes  0 = stored as is, 1 = gzip
//   4 bytes  size of the JSON in bytes
//   8 bytes  a checksum of the JSON (FNV-1a), to notice damage that decompression cannot
//   ...      the JSON, or its gzip form
namespace statecodec
{
    // Larger saved states than this get a warning in the page.
    constexpr size_t warnBytes = 5u * 1024u * 1024u;

    // Never read more than this many bytes of JSON back (guards against a damaged size field).
    constexpr size_t maxJsonBytes = 512u * 1024u * 1024u;

    void encode (const std::string& json, juce::MemoryBlock& result);

    enum class Decoded
    {
        ok,
        notOurs,    // empty, or written by something else (e.g. the Phase 1 test state): not an error
        damaged     // ours, but the contents cannot be read
    };

    Decoded decode (const void* data, size_t size, std::string& json);
}
