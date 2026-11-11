#pragma once
#include "infinity/CombatActors.hpp"
#include "infinity/Targeting.hpp"
#include <stdexcept>
namespace infinity {
inline std::array<TargetingView, 4> targetingProofViews(const Universe& u) {
    const auto home = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
    auto body = sampleRaider(u, home, 60);
    body.hostile = true;
    body.disabled = false;
    body.hull = 100;
    const auto altitude = std::pow(10., u.describe(home).frame.logRadius) * 4;
    body.position = {90, altitude, -1000};
    body.velocity = {35, 0, 0};
    WeaponRay ray;
    ray.reference = home;
    ray.origin = {0, altitude, 0};
    WeaponSystem guns;
    guns.setLevel(missileLauncher);
    TargetingComputer tracker;
    std::array<TargetingView, 4> views;
    for (unsigned i = 0; i < 5; ++i)
        tracker.update(u, ray, {}, body.id, guns, 0, std::span(&body, 1), .1, false, 1);
    views[0] = tracker.view();
    for (unsigned i = 0; i < 10; ++i)
        tracker.update(u, ray, {}, body.id, guns, 0, std::span(&body, 1), .1, false, 1);
    views[1] = tracker.view();
    auto obstruction = body;
    obstruction.id.slot = 100;
    obstruction.kind = ActivityKind::Freighter;
    obstruction.hostile = false;
    obstruction.position = ray.origin + (body.position - ray.origin) * .5;
    obstruction.radius = 70;
    const std::array<ActivityBody, 2> blocked{body, obstruction};
    for (unsigned i = 0; i < 5; ++i)
        tracker.update(u, ray, {}, body.id, guns, 0, blocked, .1, false, 1);
    views[2] = tracker.view();
    ray.direction = {0, 0, 1};
    tracker.update(u, ray, {}, body.id, guns, 0, std::span(&body, 1), .5, false, 1);
    views[3] = tracker.view();
    views[3].direction = {-700, 200, 700};
    // Views below are ship-local HUD fixtures. Actual game conversion uses the
    // camera's physical attitude and retains the authoritative reference axes.
    if (views[0].state != TargetLock::Acquiring || !views[1].canLaunch ||
        views[2].state != TargetLock::Blocked)
        throw std::runtime_error("Targeting proof did not exercise acquisition, lock and obstruction");
    return views;
}
} // namespace infinity
