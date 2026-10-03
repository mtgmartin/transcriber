#pragma once

#include "Commands.h"
#include "Json.h"
#include "Score.h"

#include <string>
#include <vector>

// The editing operations of the score (Phase 7). Every operation is one undo step made of
// Commands, so Undo puts the score back exactly. Part 7a covers notes and rhythms of a piano score.
namespace trs
{

struct EditResult
{
    bool ok = false;
    std::string message;   // what happened, or why nothing did
    std::string select;    // the id the page should select afterwards (empty: keep the selection)
    std::vector<std::string> selection;   // with several notes: the ids to select afterwards, the primary one first (empty: none were asked for)
    bool readOnly = false;                // a selection request: nothing was changed, nothing to undo
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
//
// Several notes at once (Phase 8b): every operation that works on a note, chord or rest also takes "ids" (an array of ids, the one in
// "id" is the primary one). It is done on each of them in time order (a longer note value goes from the last to the first), as one
// undo step. If it cannot be done on one of them nothing is changed (a few operations skip the notes it means nothing to: a
// dynamic that is there already, a tie that has nothing to tie to). Marks that toggle (a dynamic, an articulation, a ghost note, a
// tie, a dot) are set on all when one is without it, and taken away from all when every one has it. A slur or hairpin runs from the
// first to the last of the notes. key, break, perLine, spacing, tempo and string act on "id" only.
//
// Selections (they change nothing; the answer has "selection"):
//   selectAll    id                       every note and chord of the part the note is in (the notation staves, in time order)
//   selectRange  from, to                 the notes and chords of the staff of "from", from one to the other in time order
//   selectSame   id, mode, within         every note with the pitch of the note or chord ("pitch": the same MIDI note; "name": the same
//                                         note in every octave; a drum: the same drum), in the whole part, or only in the measures
//                                         the notes of "within" (an array of ids) are in
EditResult performEdit (Score&, UndoManager&, const Json& request);

// The pitch of a spelled note.
int pitchOf (char step, int alter, int octave);

}  // namespace trs
