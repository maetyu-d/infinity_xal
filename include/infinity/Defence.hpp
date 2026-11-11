#pragma once
#include "CombatAwareness.hpp"
#include "Economy.hpp"
#include "NpcCombat.hpp"
namespace infinity {
std::vector<ActivityBody> defenceCover(std::span<const ActivityContact>);
class DefenceController {
  public:
    void toggleAutomatic() {
        automatic_ = !automatic_;
    }
    bool automatic() const {
        return automatic_;
    }
    std::optional<EconomicCommand> command(const EconomySnapshot&, const SurveyObserver&, bool build);
    bool automaticBurst(const EconomySnapshot&, const NpcPlayer&, const WeaponSystem&, bool crewReady,
                        unsigned captainMount, bool pointDefenceArmed = true) const;
    void sent(uint64_t sequence, bool build) {
        pending_ = sequence;
        building_ = build;
    }
    bool reconcile(const EconomySnapshot&, const NpcPlayer&, WeaponSystem&);
    void annotate(CombatAwarenessView&, const EconomySnapshot*, const NpcPlayer&, const WeaponSystem&,
                  bool crewReady) const;
    const std::string& status() const {
        return status_;
    }
    std::string serialize() const;
    bool restore(std::string_view);

  private:
    void baseline(const EconomySnapshot&);
    bool automatic_ = true, initialized_ = false, building_ = false;
    uint64_t seen_ = 0, pending_ = 0;
    std::string status_;
};
} // namespace infinity
