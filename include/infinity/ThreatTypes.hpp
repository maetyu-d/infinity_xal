#pragma once
#include "Activity.hpp"
namespace infinity {
enum class ThreatPhase : uint8_t { Tracking, Locking, Gunfire, Missile, Hit };
struct ThreatObservation {
    std::optional<ActivityId> source;
    Address reference;
    glm::dvec3 position{}; // SI source position in reference axes.
    ThreatPhase phase = ThreatPhase::Tracking;
    double progress = 0;
    std::string name;
};
enum class DefenceOutcome : uint8_t { Intercepted, Diverted, LockBroken, Impact };
struct DefenceEvent {
    DefenceOutcome outcome = DefenceOutcome::Intercepted;
    std::optional<ActivityId> source;
    Address reference;
    glm::dvec3 position{};
};
inline constexpr unsigned decoyCapacity = 6, startingDecoys = 4;
inline constexpr double decoyCooldownSeconds = 6.;
} // namespace infinity
