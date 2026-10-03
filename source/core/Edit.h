#pragma once

#include "Commands.h"
#include "Json.h"
#include "Score.h"

#include <string>

// The editing operations of the score (Phase 7). Every operation is one undo step made of
// Commands, so Undo puts the score back exactly. Part 7a covers notes and rhythms of a piano score.
namespace trs
{

struct EditResult
{
    bool ok = false;
    std::string message;   // what happened, or why nothing did
    std::string select;    // the id the page should select afterwards (empty: keep the selection)
};

// Empty if the score can be edited; otherwise why not (e.g. guitar and drum scores come later).
std::string editBlocker (const Score&);

// One operation, as the page sends it. The member "op" says which:
//   pitch     id, semitones          note, chord (all its notes) or one note of a chord: up or down. +-12 keeps the spelling.
//   letter    id, letter "A".."G"    a rest becomes a note of that letter (the octave nearest to the music before it);
//                                    on a note it changes the letter. Optional "alter" -2..2 instead of the key's.
//   duration  id, dur, dots          the note value (1 whole ... 64); a longer value takes the rests (and notes) after it
//   dot       id                     adds or removes the dot
//   delete    id                     a note or chord becomes a rest; one note of a chord is taken out
//   interval  id, interval 2..8      adds a note above the top note (a chord is made)
//   tie       id                     ties the note to the next one of the same pitch, or takes the tie away
//   undo, redo
EditResult performEdit (Score&, UndoManager&, const Json& request);

// The pitch of a spelled note.
int pitchOf (char step, int alter, int octave);

}  // namespace trs
