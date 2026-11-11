#pragma once
#include "infinity/NpcCombat.hpp"
#include "infinity/ShipSignals.hpp"
#include <stdexcept>

namespace infinity {
// Uses the playable receiver, real activity identity, scan and emitter state.
inline std::array<DiscoveryView, 3> shipSignalProofViews(const Universe& u) {
    const auto home = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
    NpcPlayer player;
    player.reference = home;
    player.seconds = 60;
    player.position = {0, std::pow(10., u.describe(home).frame.logRadius) * 4, 0};
    auto enemy = sampleCombatActivity(u, {home, siegeRaiderSlot}, 60);
    enemy.position = player.position + glm::dvec3(0, 0, -1800);
    enemy.velocity = {};
    enemy.hostile = true;
    enemy.orientation = glm::quatLookAtRH(glm::dvec3(0, 0, 1), glm::dvec3(0, 1, 0));
    WeaponSystem weapons;
    NpcCombat npc;
    VesselOperations scans;
    ShipSignals emitter;
    npc.update(u, player, std::span(&enemy, 1), weapons, .01);
    auto physical = enemy;
    npc.apply(physical);
    weapons.apply(physical);
    player.forward = glm::normalize(physical.position - player.position);
    auto page = std::make_shared<DiscoveryPage>();
    page->system = home.parent();
    DiscoveryController receiver;
    receiver.page(page);
    receiver.traffic(shipSignals(u, player, std::span(&physical, 1), npc, scans));
    ReceiverObserver observer;
    observer.reference = home;
    observer.position = player.position;
    observer.forward = player.forward;
    receiver.update(u, observer, .1, false);
    receiver.nextShip();
    auto listen = [&] {
        observer.seconds = player.seconds;
        receiver.update(u, observer, .1, true, 1);
    };
    for (unsigned i = 0; i < 70; ++i) {
        player.seconds += .1;
        listen();
    }
    if (!receiver.shipContact() || receiver.target())
        throw std::runtime_error("Passive hostile emitter leaked a range fix");
    std::array<DiscoveryView, 3> result;
    auto view = [&] {
        auto v = receiver.view(true);
        v.ship = std::make_shared<ShipSignalView>(
            emitter.view(physical, scans, npc.engineering(physical.id), player.seconds));
        return v;
    };
    result[0] = view();
    for (unsigned i = 0; i < 50; ++i) {
        player.seconds += .1;
        emitter.update(u, player, physical, true, false, true, 1, 1, .1, std::span(&physical, 1));
        scans.observe(u, player, physical, npc.engineering(physical.id), .1, emitter.scanning(), 1,
                      std::span(&physical, 1));
    }
    receiver.traffic(shipSignals(u, player, std::span(&physical, 1), npc, scans));
    listen();
    result[1] = view();
    emitter.update(u, player, physical, false, true, true, 1, 1, .4, std::span(&physical, 1));
    player.seconds += .1;
    npc.update(u, player, std::span(&enemy, 1), weapons, .1, nullptr, {}, emitter.jammer(),
               emitter.radiating());
    physical = enemy;
    npc.apply(physical);
    weapons.apply(physical);
    receiver.traffic(shipSignals(u, player, std::span(&physical, 1), npc, scans));
    listen();
    result[2] = view();
    if (!result[1].ship->confirmed || !result[1].located || !result[2].ship->jamming)
        throw std::runtime_error("Ship signal proof failed shared scan or directional emission");
    return result;
}
} // namespace infinity
