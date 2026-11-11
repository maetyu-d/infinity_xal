#pragma once
#include "infinity/SignalTraffic.hpp"
#include <stdexcept>

namespace infinity {
// Isolated shortage fixture for CPU and native proofs. Uses real economic offer,
// escrow and delivery rules; never touches the player's ledger or universe.
inline std::shared_ptr<EconomicNetwork> signalProofNetwork(const Universe& u) {
    auto n = std::make_shared<EconomicNetwork>();
    n->system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
    n->universeSeed = u.seed();
    n->fingerprint = 660066;
    auto home = n->system.child(Kind::Planet, 0);
    for (const auto& id : u.children(u.describe(n->system), 256))
        if (hasSolidSurface(u.describe(id))) {
            home = id;
            break;
        }
    const double radius = std::pow(10., u.describe(home).frame.logRadius);
    for (unsigned i = 0; i < 2; ++i) {
        EconomicSite site;
        site.id = {home, i};
        site.name = i ? "COLDREACH COLONY" : "SUPPLY HARBOUR";
        site.orbital = !i;
        site.sealed = i;
        site.population = i ? 6000 : 0;
        site.metres = {0, radius + 10000 + i * 10000., 0};
        site.systemMetres = site.metres;
        site.workers = site.power = 20;
        site.capacityKg = 2000000;
        if (!i)
            site.initial.fill(50000);
        else {
            site.initial[size_t(Good::Alloy)] = 100;
            site.initial[size_t(Good::Tools)] = 20;
            site.recipes = {1};
            for (auto g : {Good::Ore, Good::Silicate, Good::Carbon})
                site.target[size_t(g)] = 1000;
            site.reserves.fill(1000000);
        }
        n->sites.push_back(site);
    }
    return n;
}
inline EconomySnapshot signalProofSnapshot(const Economy& e, const EconomyPlayer& p) {
    EconomySnapshot s;
    s.network = std::make_shared<EconomicNetwork>(e.network());
    s.tick = e.tick();
    s.sites = e.sites();
    s.recoveries = e.recoveries();
    s.wrecks = e.wrecks();
    s.player = p;
    return s;
}
inline DiscoveryView signalGameplayProofView(const Universe& u) {
    Economy economy(signalProofNetwork(u));
    economy.advanceTo(12);
    EconomyPlayer player;
    const auto snapshot = signalProofSnapshot(economy, player);
    auto page = std::make_shared<DiscoveryPage>();
    page->system = snapshot.network->system;
    DiscoveryController controller;
    controller.page(page);
    controller.traffic(trafficSignals(u, snapshot, {}, 100));
    if (snapshot.recoveries.empty())
        throw std::runtime_error("Radio proof has no actual funded offer");
    const auto& site = snapshot.network->sites[snapshot.recoveries.front().site];
    ReceiverObserver observer;
    observer.reference = site.id.home;
    observer.position = site.metres + glm::dvec3(1000, 1000, 0);
    observer.forward = glm::normalize(site.metres - observer.position);
    controller.update(u, observer, .1, false);
    controller.nextCall();
    for (unsigned i = 0; i < 80; ++i) {
        observer.seconds += .1;
        controller.update(u, observer, .1, true, 1);
    }
    const auto view = controller.view(true);
    if (!view.live || !view.located || !controller.offer())
        throw std::runtime_error("Native radio proof did not decode a live ledger offer");
    return view;
}
} // namespace infinity
