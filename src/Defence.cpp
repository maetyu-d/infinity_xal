#define GLM_FORCE_INLINE
#include "infinity/Defence.hpp"
#include "infinity/OrbitalDebris.hpp"
#include "infinity/SaveTag.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
namespace infinity {
std::vector<ActivityBody> defenceCover(std::span<const ActivityContact> contacts) {
    std::vector<ActivityBody> result;
    result.reserve(orbitalDebrisCapacity);
    for (const auto& c : contacts)
        if (landableDebris(c.body) && !c.occluded && c.distance < 12000) {
            result.push_back(c.body);
            if (result.size() == orbitalDebrisCapacity)
                break;
        }
    return result;
}
void DefenceController::baseline(const EconomySnapshot& s) {
    if (!initialized_ && s.network && !s.catchingUp) {
        seen_ = s.player.decoyLaunch;
        initialized_ = true;
    }
}
std::optional<EconomicCommand> DefenceController::command(const EconomySnapshot& s, const SurveyObserver& o,
                                                          bool build) {
    baseline(s);
    if (s.catchingUp || !s.network || pending_) {
        status_ = "DEFENCE / SHIP LEDGER BUSY";
        return {};
    }
    if (o.field || (!build && o.landed) || o.size < .99 || o.hull <= 0) {
        status_ = "DEFENCE / FULL-SIZE FREE FLIGHT REQUIRED";
        return {};
    }
    if (build) {
        if (s.player.decoys >= decoyCapacity) {
            status_ = "DECOY RACK FULL";
            return {};
        }
        if (s.player.availableCargo(Good::Alloy) < 1 || s.player.availableCargo(Good::Electronics) < 1) {
            status_ = "BUILD NEEDS 1KG ALLOY + 1KG ELECTRONICS / F2 GOODS";
            return {};
        }
    } else {
        if (!s.player.decoys) {
            status_ = "DECOY RACK EMPTY / CTRL-BACKSLASH BUILD";
            return {};
        }
        if (s.player.decoyLastAt >= 0 && o.seconds - s.player.decoyLastAt < decoyCooldownSeconds) {
            status_ = "DECOY EJECTOR CYCLING / WAIT";
            return {};
        }
    }
    EconomicCommand c;
    c.action = build ? EconomicAction::BuildDecoy : EconomicAction::DeployDecoy;
    c.observer = o;
    status_ = build ? "BUILD ORDER / AWAITING RECEIPT" : "DECOY RELEASE / AWAITING RECEIPT";
    return c;
}
bool DefenceController::automaticBurst(const EconomySnapshot& s, const NpcPlayer& p, const WeaponSystem& w,
                                       bool crewReady, unsigned captainMount, bool pointDefenceArmed) const {
    const auto& threat = w.missileThreat();
    if (!automatic_ || !crewReady || pending_ || p.field || p.landed || p.size < .99 || p.hull <= 0 ||
        !s.player.decoys || s.catchingUp ||
        (s.player.decoyLastAt >= 0 && p.seconds - s.player.decoyLastAt < decoyCooldownSeconds) ||
        !threat.count || threat.seconds < 0 || threat.seconds > 1.8)
        return false;
    return !pointDefenceArmed || threat.seconds < .8 ||
           !w.pointDefenceCovers(threat.localDirection, threat.range, captainMount);
}
bool DefenceController::reconcile(const EconomySnapshot& s, const NpcPlayer& p, WeaponSystem& w) {
    if (!s.network || s.catchingUp)
        return false;
    baseline(s);
    bool burst = false;
    if (s.player.decoyLaunch > seen_) {
        seen_ = s.player.decoyLaunch;
        w.deployDecoys(p, seen_);
        burst = !p.field && !p.landed && p.size >= .99 && p.hull > 0;
        status_ = burst ? "DECOYS RELEASED / KEEP MANOEUVRING" : "RELEASE SPENT / SHIP STATE CHANGED";
    }
    if (pending_ && s.player.lastCommand >= pending_) {
        if (building_)
            status_ = "DECOY RACK / " + std::to_string(s.player.decoys) + " READY";
        else if (s.player.decoyLaunch != pending_)
            status_ = "DECOY ORDER REJECTED / CHECK RACK AND FLIGHT STATE";
        pending_ = 0;
    }
    return burst;
}
void DefenceController::annotate(CombatAwarenessView& v, const EconomySnapshot* s, const NpcPlayer& p,
                                 const WeaponSystem& w, bool crewReady) const {
    v.automatic = automatic_;
    v.crewReady = crewReady;
    v.pointDefence = w.readyPointDefence();
    if (s) {
        v.decoys = s->player.decoys;
        v.cooldown = s->player.decoyLastAt < 0
                         ? 0
                         : std::max(0., decoyCooldownSeconds - p.seconds + s->player.decoyLastAt);
    }
    if (pending_)
        v.defence = building_ ? "BUILDING DECOY / AWAITING RECEIPT" : "RELEASE ORDER / AWAITING RECEIPT";
}
std::string DefenceController::serialize() const {
    return "INFINITY_DEFENCE 1 " + std::to_string(automatic_);
}
bool DefenceController::restore(std::string_view text) {
    std::istringstream in{std::string(text)};
    std::string tag;
    unsigned version, automatic;
    if (!(in >> tag >> version >> automatic) || !matchesSaveTag(tag, "INFINITY_DEFENCE") || version != 1 ||
        automatic > 1)
        return false;
    in >> std::ws;
    if (!in.eof())
        return false;
    automatic_ = automatic;
    return true;
}
} // namespace infinity
