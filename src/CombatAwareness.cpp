#define GLM_FORCE_INLINE
#include "infinity/CombatAwareness.hpp"
#include "infinity/NpcCombat.hpp"
#include <algorithm>
#include <cmath>
namespace infinity {
void CombatAwareness::update(const Universe& u, const NpcPlayer& p, std::span<const ThreatObservation> input,
                             const MissileThreat& missile, std::span<const DefenceEvent> events, double dt) {
    if (!std::isfinite(dt) || dt <= 0 || dt > 1)
        return;
    view_ = {};
    feedback_ = std::max(0., feedback_ - dt);
    if (p.field || p.landed || p.hull <= 0) {
        tracks_.clear();
        feedback_ = 0;
        return;
    }
    for (auto& t : tracks_) {
        t.ttl -= dt;
        t.hit = std::max(0., t.hit - dt);
    }
    std::erase_if(tracks_, [](const auto& t) { return t.ttl <= 0; });
    for (const auto& observation : input.first(std::min<size_t>(32, input.size()))) {
        if (!std::isfinite(glm::length(observation.position)) || !std::isfinite(observation.progress))
            continue;
        auto found = std::find_if(tracks_.begin(), tracks_.end(),
                                  [&](const auto& t) { return t.observation.source == observation.source; });
        if (found == tracks_.end()) {
            if (tracks_.size() >= 16)
                continue;
            tracks_.push_back({});
            found = std::prev(tracks_.end());
        }
        const bool holdShot = observation.phase == ThreatPhase::Tracking &&
                              found->observation.phase == ThreatPhase::Gunfire && found->ttl > 0;
        const double shotTime = found->ttl;
        found->observation = observation;
        if (holdShot)
            found->observation.phase = ThreatPhase::Gunfire;
        found->ttl = holdShot                                    ? shotTime
                     : observation.phase == ThreatPhase::Hit     ? 1.
                     : observation.phase == ThreatPhase::Gunfire ? .45
                                                                 : .22;
        if (observation.phase == ThreatPhase::Hit)
            found->hit = 1.;
        found->ttl = std::max(found->ttl, found->hit);
    }
    for (const auto& event : events) {
        feedback_ = 2.2;
        defence_ = event.outcome == DefenceOutcome::Intercepted  ? "POINT DEFENCE / MISSILE INTERCEPTED"
                   : event.outcome == DefenceOutcome::Diverted   ? "DECOY TAKEN / KEEP MANOEUVRING"
                   : event.outcome == DefenceOutcome::LockBroken ? "SEEKER LOST / KEEP COVER"
                                                                 : "MISSILE IMPACT";
        if (event.outcome == DefenceOutcome::Impact) {
            ThreatObservation hit{event.source, event.reference, event.position, ThreatPhase::Hit, 1,
                                  "MISSILE"};
            auto found = std::find_if(tracks_.begin(), tracks_.end(),
                                      [&](const auto& t) { return t.observation.source == hit.source; });
            if (found != tracks_.end()) {
                found->hit = 1;
                found->ttl = 1;
            } else if (tracks_.size() < 16)
                tracks_.push_back({hit, 1, 1});
        }
    }
    struct Ranked {
        ThreatCue cue;
        int rank;
    };
    std::vector<Ranked> ranked;
    ranked.reserve(16);
    const double units = std::pow(10., u.describe(p.reference).frame.logRadius);
    for (const auto& t : tracks_) {
        const auto& o = t.observation;
        const auto eye = o.reference == p.reference
                             ? p.position
                             : u.pointRelative(p.reference, p.position / units, o.reference, 0);
        const auto delta = o.position - eye;
        if (glm::length(delta) < 1e-9)
            continue;
        const auto rotation = u.relative(o.reference, p.reference, 0).orientation;
        ThreatCue cue;
        cue.source = o.source;
        cue.name = o.name;
        cue.direction = glm::inverse(p.orientation) * (rotation * glm::normalize(delta));
        cue.phase = t.hit > 0 ? ThreatPhase::Hit : o.phase;
        cue.progress = o.progress;
        cue.strength = t.hit > 0 ? t.hit : std::clamp(t.ttl / .22, 0., 1.);
        const int rank = cue.phase == ThreatPhase::Hit       ? 5
                         : cue.phase == ThreatPhase::Missile ? 4
                         : cue.phase == ThreatPhase::Locking ? 3
                         : cue.phase == ThreatPhase::Gunfire ? 2
                                                             : 1;
        ranked.push_back({std::move(cue), rank});
        if (o.phase == ThreatPhase::Locking)
            ++view_.locking;
        if (o.phase == ThreatPhase::Gunfire || t.hit > 0)
            ++view_.firing;
    }
    if (missile.count && missile.source)
        for (auto& r : ranked)
            if (r.cue.source == missile.source && r.cue.phase != ThreatPhase::Hit) {
                r.cue.phase = ThreatPhase::Missile;
                r.rank = 4;
            }
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const auto& a, const auto& b) { return a.rank > b.rank; });
    for (const auto& t : ranked) {
        if (view_.cues.size() == 4)
            break;
        view_.cues.push_back(t.cue);
    }
    if (!ranked.empty())
        view_.attacker = ranked.front().cue.source;
    view_.missiles = missile.count;
    if (missile.count) {
        if (missile.source)
            view_.attacker = missile.source;
        view_.warning = missile.guided ? "INCOMING GUIDED MISSILE" : "INCOMING BALLISTIC MISSILE";
        const auto source = std::find_if(ranked.begin(), ranked.end(), [&](const auto& t) {
            return missile.source && t.cue.source == missile.source;
        });
        if (source != ranked.end())
            view_.warning += " / " + source->cue.name;
        view_.action = "BACKSLASH DECOY / 7 ARM POINT DEFENCE";
    } else if (view_.locking) {
        const auto locked = std::find_if(tracks_.begin(), tracks_.end(), [](const auto& t) {
            return t.observation.phase == ThreatPhase::Locking;
        });
        const unsigned progress =
            locked == tracks_.end() ? 0 : unsigned(std::clamp(locked->observation.progress, 0., 1.) * 100);
        view_.warning = "ENEMY MISSILE LOCK " + std::to_string(progress) + "% / " +
                        (locked == tracks_.end() ? std::string("HOSTILE") : locked->observation.name);
        view_.action = "BREAK SIGHT / ALT-SHIFT-H ATTACKER";
    }
    if (feedback_ > 0)
        view_.defence = defence_;
    view_.active = !view_.cues.empty() || missile.count || feedback_ > 0;
}
CombatAwarenessView CombatAwareness::view() const {
    return view_;
}
} // namespace infinity
