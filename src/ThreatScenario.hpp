#pragma once
#include "infinity/Defence.hpp"
#include <array>
namespace infinity {
inline std::array<CombatAwarenessView, 4> threatProofViews(const Universe& u) {
    const auto home = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
    const double altitude = std::pow(10., u.describe(home).frame.logRadius) * 4;
    NpcPlayer player;
    player.reference = home;
    player.position = {0, altitude, 0};
    player.seconds = 60;
    const auto body = sampleCombatActivity(u, {home, siegeRaiderSlot}, 60);
    ThreatObservation observation{
        body.id, home, player.position + glm::dvec3(180, 45, -900), ThreatPhase::Locking, .65, body.name};
    CombatAwareness awareness;
    awareness.update(u, player, std::span(&observation, 1), {}, {}, .1);
    std::array<CombatAwarenessView, 4> views;
    views[0] = awareness.view();
    observation.phase = ThreatPhase::Hit;
    awareness.update(u, player, std::span(&observation, 1), {}, {}, .1);
    views[1] = awareness.view();
    MissileThreat incoming;
    incoming.source = body.id;
    incoming.count = 1;
    incoming.range = 440;
    incoming.seconds = 1.3;
    incoming.localDirection = glm::normalize(glm::dvec3(.2, 0, -1));
    awareness.update(u, player, std::span(&observation, 1), incoming, {}, .1);
    views[2] = awareness.view();
    DefenceEvent diverted{DefenceOutcome::Diverted, body.id, home, observation.position};
    awareness.update(u, player, {}, {}, std::span(&diverted, 1), .1);
    views[3] = awareness.view();
    for (auto& view : views) {
        view.decoys = 4;
        view.automatic = true;
        view.crewReady = true;
    }
    return views;
}
} // namespace infinity
