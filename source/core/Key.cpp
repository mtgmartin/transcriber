#include "Notation.h"

#include <cmath>

namespace trs
{

namespace
{
    // Temperley's profiles from the Kostka-Payne corpus, in the form music21 uses
    // (https://extras.humdrum.org/man/keycor/), tonic first.
    const double majorProfile[12] = { 0.748, 0.060, 0.488, 0.082, 0.670, 0.460, 0.096, 0.715, 0.104, 0.366, 0.057, 0.400 };
    const double minorProfile[12] = { 0.712, 0.084, 0.474, 0.618, 0.049, 0.460, 0.105, 0.747, 0.404, 0.067, 0.133, 0.330 };

    double correlation (const double* distribution, const double* profile, int tonic)
    {
        double meanX = 0.0, meanY = 0.0;

        for (int i = 0; i < 12; ++i)
        {
            meanX += distribution[(tonic + i) % 12];
            meanY += profile[i];
        }

        meanX /= 12.0;
        meanY /= 12.0;

        double sxy = 0.0, sxx = 0.0, syy = 0.0;

        for (int i = 0; i < 12; ++i)
        {
            const auto dx = distribution[(tonic + i) % 12] - meanX;
            const auto dy = profile[i] - meanY;
            sxy += dx * dy;
            sxx += dx * dx;
            syy += dy * dy;
        }

        return sxx > 0.0 && syy > 0.0 ? sxy / std::sqrt (sxx * syy) : 0.0;
    }
}

int fifthsForKey (int tonic, bool minor, bool flatsPreferred)
{
    const auto relativeMajor = (((minor ? tonic + 3 : tonic) % 12) + 12) % 12;
    const auto sharpSide = (relativeMajor * 7) % 12;       // 0 ... 11
    const auto flatSide = sharpSide - 12;                  // -12 ... -1

    const bool sharpOk = sharpSide <= 7;
    const bool flatOk = flatSide >= -7;

    if (sharpOk && ! flatOk) return sharpSide;
    if (flatOk && ! sharpOk) return flatSide;
    if (std::abs (sharpSide) != std::abs (flatSide)) return std::abs (sharpSide) < std::abs (flatSide) ? sharpSide : flatSide;
    return flatsPreferred ? flatSide : sharpSide;
}

KeyResult detectKey (const std::vector<QNote>& notes)
{
    KeyResult result;
    double distribution[12] {};

    for (const auto& n : notes)
        distribution[((n.pitch % 12) + 12) % 12] += (double) n.dur;

    double best = -2.0;

    for (int tonic = 0; tonic < 12; ++tonic)
    {
        result.scores[tonic] = correlation (distribution, majorProfile, tonic);
        result.scores[12 + tonic] = correlation (distribution, minorProfile, tonic);
    }

    for (int i = 0; i < 24; ++i)
    {
        if (result.scores[i] > best + 1.0e-12)
        {
            best = result.scores[i];
            result.tonic = i % 12;
            result.minor = i >= 12;
        }
    }

    result.correlation = best;
    result.fifths = fifthsForKey (result.tonic, result.minor, false);
    return result;
}

}  // namespace trs
