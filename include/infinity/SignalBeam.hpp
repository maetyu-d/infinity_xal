#pragma once
#include "Activity.hpp"

namespace infinity {
// Transient emission in physical SI coordinates. Never a new world object.
struct SignalBeam {
    Address reference;
    glm::dvec3 origin{}, direction{0, 0, -1};
    std::optional<ActivityId> target;
    double range = 6000, strength = 0;
};
double signalBeamStrength(const Universe&, const SignalBeam&, const Address&, glm::dvec3 metres);
} // namespace infinity
