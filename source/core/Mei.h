#pragma once

#include "Score.h"

#include <string>

// Writes a Score as MEI 5, the format the engraving library (Verovio) reads. Every node keeps its id as
// the xml:id, so a click on the engraved page finds the node again.
namespace trs
{

struct MeiOptions
{
    std::string title = "Transcription";
    std::string composer;
};

std::string scoreToMei (const Score&, const MeiOptions& = {});

// A sentence about a node of the score for a click on the engraved page, e.g.
// "Right hand · measure 5 · voice 1 · eighth note Eb5, tied to the next note". Empty if the id is unknown.
std::string describeNode (const Score&, const std::string& id);

// Escapes the characters XML does not allow in text and attribute values.
std::string xmlEscape (const std::string&);

}  // namespace trs
