#pragma once
#include "Weapons.hpp"
namespace infinity {
struct NpcPlayer;
struct ThreatCue {
    std::optional<ActivityId> source;
    glm::dvec3 direction{}; // Physical ship axes, presentation rotates for head motion.
    ThreatPhase phase = ThreatPhase::Tracking;
    double strength = 1, progress = 0;
    std::string name;
};
struct CombatAwarenessView {
    std::vector<ThreatCue> cues;
    std::optional<ActivityId> attacker;
    unsigned locking = 0, firing = 0, missiles = 0, decoys = 0, pointDefence = 0;
    bool active = false, automatic = true, crewReady = false, pointDefenceArmed = false;
    double cooldown = 0;
    std::string warning, action, defence;
};
class CombatAwareness {
  public:
    void update(const Universe&, const NpcPlayer&, std::span<const ThreatObservation>, const MissileThreat&,
                std::span<const DefenceEvent>, double dt);
    CombatAwarenessView view() const;
    std::optional<ActivityId> attacker() const {
        return view_.attacker;
    }

  private:
    struct Track {
        ThreatObservation observation;
        double ttl = 0, hit = 0;
    };
    std::vector<Track> tracks_;
    CombatAwarenessView view_;
    std::string defence_;
    double feedback_ = 0;
};
} // namespace infinity
