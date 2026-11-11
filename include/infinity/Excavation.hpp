#pragma once
#include "CavernHazard.hpp"

namespace infinity {
struct ExcavationProfile {
    double weakness = 0, span = 0, beddingSlip = 0;
    std::string_view failure;
};
struct ExcavationWork {
    uint64_t bore = 0;
    unsigned face = 0;
    double progress = 0;
    bool careful = false;
};
ExcavationProfile excavationProfile(const Borehole&, const CavernFind&);
double excavationCutDamage(const Borehole&, const CavernFind&, bool careful);
double excavationStrain(const Borehole&, const CavernFind&, double progress, bool careful);
bool advanceExcavation(Borehole&, const CavernFind&, bool careful, double seconds);
glm::dvec3 cavernRubblePoint(const Borehole&, const CavernFind&);
double cavernRubbleRadius(const Borehole&, const CavernFind&);
double cavernRubbleClearance(const Borehole&, glm::dvec3 local);
// Read-only, bounded presentation; never edits the geological field.
Mesh excavationFixtures(const Borehole&);
ProbeMotes excavationMotes(const Borehole&, const CavernFind&, double strain, double seconds);
void annotateExcavation(FlightInstruments&, const Borehole&, const CavernFind&, const ExcavationWork&,
                        double engineer, double seconds);
} // namespace infinity
