#include "Notation.h"

#include <array>
#include <cmath>

namespace trs
{

// The ps13 pitch-spelling algorithm: Meredith, "The ps13 Pitch Spelling Algorithm", Journal of New
// Music Research 35(2), 2006, in the form of its published pseudocode (steps are numbered as in the
// paper). Follows the structure of the implementation in partitura (Apache-2.0), with the same context
// window: 10 notes before and 40 after.
//
// Pitches are counted from A0 ("chromatic pitch" = MIDI - 21), chroma 0 is A, and the seven
// morphetic classes are A B C D E F G = 0 ... 6.

namespace
{
    constexpr int contextBefore = 10;
    constexpr int contextAfter = 40;

    const int initialMorph[12] = { 0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6 };
    const int morphInterval[12] = { 0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6 };
    const int naturalChroma[7] = { 0, 2, 3, 5, 7, 8, 10 };   // A B C D E F G
    const char steps[7] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G' };

    int mod (int a, int b) { return ((a % b) + b) % b; }

    int floorDivide (int a, int b)
    {
        auto q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    }
}

int tonicLetterForKey (int fifths, bool minor)
{
    // Along the line of fifths the letters run F C G D A E B, then around again (F sharp, ...).
    static const int letterOrder[7] = { 5, 2, 6, 3, 0, 4, 1 };   // F C G D A E B as 0 = A ... 6 = G
    const auto position = minor ? fifths + 3 : fifths;
    return letterOrder[mod (position + 1, 7)];
}

std::vector<SpelledPitch> spellPitches (const std::vector<int64_t>&, const std::vector<int>& pitches,
                                        int anchorTonic, int anchorLetter)
{
    const auto n = (int) pitches.size();
    std::vector<SpelledPitch> result ((size_t) n);

    if (n == 0)
        return result;

    std::vector<int> chroma ((size_t) n), chromatic ((size_t) n);

    for (int i = 0; i < n; ++i)
    {
        chromatic[(size_t) i] = pitches[(size_t) i] - 21;
        chroma[(size_t) i] = mod (chromatic[(size_t) i], 12);
    }

    // The chroma distribution in the window around each note.
    std::vector<std::array<int, 12>> window ((size_t) n);
    std::array<int, 12> counts {};

    for (int i = 0; i < std::min (n, contextAfter); ++i)
        ++counts[(size_t) chroma[(size_t) i]];

    window[0] = counts;

    for (int i = 1; i < n; ++i)
    {
        if (i + contextAfter <= n)
            ++counts[(size_t) chroma[(size_t) (i + contextAfter - 1)]];

        if (i - contextBefore > 0)
            --counts[(size_t) chroma[(size_t) (i - contextBefore - 1)]];

        window[(size_t) i] = counts;
    }

    // Lines 2-8: the morphetic class of the first note fixes the class of every possible tonic.
    const auto firstChroma = anchorTonic >= 0 ? mod (anchorTonic - 9, 12) : chroma[0];
    const auto firstMorph = anchorTonic >= 0 ? mod (anchorLetter, 7) : initialMorph[firstChroma];
    int tonicMorph[12];

    for (int t = 0; t < 12; ++t)
        tonicMorph[t] = mod (firstMorph - morphInterval[mod (firstChroma - t, 12)], 7);

    // Lines 12-24: for each note, the class that most of the nearby pitch classes (as possible
    // tonics) would give it.
    std::vector<int> morph ((size_t) n);

    for (int j = 0; j < n; ++j)
    {
        int strength[7] {};

        for (int t = 0; t < 12; ++t)
        {
            const auto m = mod (morphInterval[mod (chroma[(size_t) j] - t, 12)] + tonicMorph[t], 7);
            strength[m] += window[(size_t) j][(size_t) t];
        }

        int best = 0;

        for (int m = 1; m < 7; ++m)
            if (strength[m] > strength[best])
                best = m;

        morph[(size_t) j] = best;
    }

    // Morphetic pitch: the octave of the class that puts it nearest the chromatic pitch.
    for (int j = 0; j < n; ++j)
    {
        const auto cp = chromatic[(size_t) j];
        const auto octave1 = floorDivide (cp, 12);
        const int candidates[3] = { octave1, octave1 + 1, octave1 - 1 };
        const auto here = (double) octave1 + chroma[(size_t) j] / 12.0;

        int bestOctave = candidates[0];
        double bestDistance = 1.0e9;

        for (const auto o : candidates)
        {
            const auto distance = std::abs (((double) o + morph[(size_t) j] / 7.0) - here);

            if (distance < bestDistance - 1.0e-12)
            {
                bestDistance = distance;
                bestOctave = o;
            }
        }

        const auto morpheticPitch = morph[(size_t) j] + 7 * bestOctave;

        // Pitch name from chromatic and morphetic pitch.
        const auto m = mod (morpheticPitch, 7);
        auto& out = result[(size_t) j];
        out.step = steps[m];
        out.alter = cp - 12 * floorDivide (morpheticPitch, 7) - naturalChroma[m];
        out.octave = floorDivide (morpheticPitch, 7) + (m > 1 ? 1 : 0);
    }

    return result;
}

}  // namespace trs
