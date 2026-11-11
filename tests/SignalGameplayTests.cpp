#include "../src/DiscoveryScenario.hpp"
#include "../src/SignalGameplayScenario.hpp"
#include "infinity/Cockpit.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace infinity;
namespace {
unsigned checks = 0;
void check(bool yes, const char* why) {
    ++checks;
    if (!yes)
        throw std::runtime_error(why);
}
struct Radio {
    Universe u;
    std::shared_ptr<EconomicNetwork> network = signalProofNetwork(u);
    Economy economy{network};
    EconomyPlayer player;
    DiscoveryController receiver;
    ReceiverObserver eye;
    Radio() {
        economy.advanceTo(12);
        player.credits = 10000000;
        auto page = std::make_shared<DiscoveryPage>();
        page->system = network->system;
        receiver.page(page);
        refresh();
        check(!economy.recoveries().empty(), "Actual shortage did not produce a funded radio request");
        const auto& site = network->sites[economy.recoveries().front().site];
        eye.reference = site.id.home;
        eye.position = site.metres + glm::dvec3(1000, 1000, 0);
        eye.forward = glm::normalize(site.metres - eye.position);
        receiver.update(u, eye, .1, false);
        receiver.nextCall();
    }
    void refresh(std::span<const WeaponDamage> damage = {}) {
        receiver.traffic(trafficSignals(u, signalProofSnapshot(economy, player), damage, 100));
    }
    void listen(unsigned count = 80) {
        for (unsigned i = 0; i < count; ++i) {
            eye.seconds += .1;
            receiver.update(u, eye, .1, true, 1);
        }
    }
    std::string issue(EconomicCommand command) {
        command.sequence = player.lastCommand + 1;
        return economy.execute(command, player);
    }
};
} // namespace
int main() {
    try {
        Radio r;
        const auto before = r.u.describe(r.eye.reference);
        const auto fingerprint = descriptorFingerprint(before);
        check(r.receiver.view().calls > 0 && !r.receiver.offer() && !r.receiver.target(),
              "Scanning leaked an undecoded offer or position");
        r.eye.field = true;
        r.listen();
        check(!r.receiver.offer(), "Warp saturation decoded a live request");
        r.eye.field = false;
        r.listen();
        const auto source = *r.receiver.source();
        const auto job = r.economy.recoveries().front();
        check(r.receiver.view().live && r.receiver.target()->contact == r.network->sites[job.site].id,
              "Decoded beacon substituted a new port identity");
        check(r.receiver.target()->metres == r.network->sites[job.site].metres,
              "Beacon position differs from actual port");
        check(r.receiver.view().transmission[1].find("KG") != std::string::npos,
              "Radio omitted actual outstanding supplies");
        auto order = signalResponse(r.receiver, signalProofSnapshot(r.economy, r.player));
        check(order && order->contract == job.id && order->port == r.network->sites[job.site].id,
              "Radio did not select the existing funded job");
        {
            auto expired = signalProofSnapshot(r.economy, r.player);
            expired.tick = job.deadline + 1;
            check(!signalResponse(r.receiver, expired), "Stale decoded offer bypassed its real deadline");
            expired.tick = r.economy.tick();
            expired.recoveries.front().phase = RecoveryPhase::Accepted;
            check(!signalResponse(r.receiver, expired),
                  "Stale receiver bypassed an already acknowledged ledger");
        }
        r.eye.field = true;
        r.listen(1);
        check(!r.receiver.offer(), "Decoded radio sent a precision response through warp saturation");
        r.eye.field = false;
        r.listen(1);
        const auto money = r.economy.moneyTotal() + r.player.credits;
        check(r.issue(*order).starts_with("RECOVERY ACCEPTED"), "Radio job acknowledgement failed");
        r.refresh();
        r.listen(1);
        check(r.receiver.view().accepted &&
                  !signalResponse(r.receiver, signalProofSnapshot(r.economy, r.player)),
              "Accepted offer could be accepted twice");
        for (size_t g = 0; g < goodCount; ++g)
            if (job.required[g]) {
                EconomicCommand buy;
                buy.action = EconomicAction::Buy;
                buy.port = r.network->sites[0].id;
                buy.good = Good(g);
                buy.kg = job.required[g];
                buy.limitCents = 1000000000;
                check(r.issue(buy).starts_with("BOUGHT"), "Radio request could not source finite supplies");
            }
        EconomicCommand delivery;
        delivery.action = EconomicAction::DeliverRecovery;
        delivery.port = order->port;
        delivery.contract = order->contract;
        check(r.issue(delivery).starts_with("RECOVERY DELIVERED"),
              "Radio-sourced job did not complete through existing delivery loop");
        r.refresh();
        r.listen(1);
        check(r.receiver.view().closed && !r.receiver.target(),
              "Completed job kept broadcasting an actionable waypoint");
        check(!r.economy.outcomes().empty() && r.economy.moneyTotal() + r.player.credits == money,
              "Radio response bypassed outcomes or minted money");
        const auto inventory = r.economy.inventoryTotal(), initial = r.economy.initialTotal(),
                   delta = r.economy.externalGoodsDelta();
        for (size_t g = 0; g < goodCount; ++g)
            check(inventory[g] == initial[g] + delta[g], "Radio job violated commodity conservation");
        const auto credits = r.player.credits;
        r.issue(delivery);
        check(r.player.credits == credits, "Repeated radio delivery paid twice");
        DiscoveryController restored;
        const auto save = r.receiver.serialize(r.u.seed());
        check(restored.restore(save, r.u) && restored.serialize(r.u.seed()) == save,
              "Live job record did not persist with exact episode identity");
        restored.page(r.receiver.catalog());
        restored.traffic(trafficSignals(r.u, signalProofSnapshot(r.economy, r.player), {}, 100));
        check(restored.view().closed && !restored.offer(), "Reload revived a completed offer");
        {
            auto stale = signalProofSnapshot(r.economy, r.player);
            stale.recoveries.front().phase = RecoveryPhase::Expired;
            check(!signalResponse(restored, stale), "Expired ledger accepted stale radio response");
        }
        // A distinct tender at the same port is a new episode, with no inherited decode.
        {
            auto next = signalProofSnapshot(r.economy, r.player);
            next.recoveries.front().id += 1000;
            next.recoveries.front().phase = RecoveryPhase::Open;
            next.recoveries.front().escrow = job.escrow;
            auto signals = trafficSignals(r.u, next, {}, 100);
            check(!signals.empty() && signals.front().id != source.id,
                  "Two jobs at one port shared a discovery identity");
            check(signals.front().metres == source.metres && signals.front().descriptor != source.descriptor,
                  "Job evolution altered port position or reused an earlier episode descriptor");
        }
        // Distress attaches to the authoritative frozen home-frame damage position.
        WeaponDamage wreck;
        wreck.id = {r.eye.reference, 100};
        wreck.hull = 0;
        wreck.position = {1000, source.planetRadius + 50000, 0};
        auto snapshot = signalProofSnapshot(r.economy, r.player);
        snapshot.recoveries.clear();
        auto calls = trafficSignals(r.u, snapshot, std::span(&wreck, 1), 100);
        check(calls.size() == 1 && calls[0].contact == wreck.id && calls[0].metres == wreck.position,
              "Distress beacon replaced or moved the actual disabled ship");
        {
            EconomicCommand registerWreck;
            registerWreck.action = EconomicAction::RegisterWreck;
            registerWreck.port = wreck.id;
            registerWreck.combatTarget = wreck.id;
            registerWreck.wreck = wreck;
            check(r.issue(registerWreck).starts_with("WRECK REGISTERED"),
                  "Radio contact could not enter finite existing rescue ledger");
            EconomicCommand recover = registerWreck;
            recover.action = EconomicAction::RecoverWreck;
            recover.combatPresent = true;
            recover.targetDistance = 50;
            recover.relativeSpeed = 0;
            recover.crewAboard = 8;
            check(r.issue(recover).starts_with("RECOVERED"),
                  "Radio distress contact failed actual close-range recovery");
            auto recovered = signalProofSnapshot(r.economy, r.player);
            recovered.recoveries.clear();
            check(!trafficSignals(r.u, recovered, std::span(&wreck, 1), 100)[0].broadcasting,
                  "Real recovered wreck did not silence its beacon");
            const auto cargo = r.player.cargo;
            const auto passengers = r.player.rescued.size();
            r.issue(recover);
            check(r.player.cargo == cargo && r.player.rescued.size() == passengers,
                  "Repeated radio recovery duplicated cargo or survivors");
        }
        WreckRecovery receipt;
        receipt.id = wreck.id;
        receipt.rescued = true;
        snapshot.wrecks.push_back(receipt);
        check(!trafficSignals(r.u, snapshot, std::span(&wreck, 1), 100)[0].broadcasting,
              "Exhausted rescue/salvage kept broadcasting");
        snapshot.wrecks[0].scrap[size_t(Good::Alloy)] = 2;
        check(trafficSignals(r.u, snapshot, std::span(&wreck, 1), 100)[0].broadcasting,
              "Remaining real salvage disappeared after rescue");
        wreck.hull = 100;
        wreck.systems = intactSystems();
        check(trafficSignals(r.u, snapshot, std::span(&wreck, 1), 100).empty(),
              "Healthy ship emitted distress");
        wreck.systems[size_t(ShipSystem::Engines)] = 0;
        check(!trafficSignals(r.u, snapshot, std::span(&wreck, 1), 100).empty(),
              "Engine-disabled civilian could not request assistance");
        {
            auto different = snapshot;
            auto network = std::make_shared<EconomicNetwork>(*snapshot.network);
            network->system = network->system.parent().child(Kind::System, 10);
            different.network = network;
            check(trafficSignals(r.u, different, std::span(&wreck, 1), 100).empty(),
                  "Distant-system damage leaked into local emergency band");
        }
        check(descriptorFingerprint(r.u.describe(r.eye.reference)) == fingerprint,
              "Live signal adapter mutated procedural descriptors");
        // Passive navigator records real separated measurements while player flies.
        {
            Universe u;
            auto page = generateDiscoveryPage(u, r.network->system);
            auto found = std::find_if(page->sources.begin(), page->sources.end(),
                                      [](const auto& s) { return bool(s.relic); });
            auto local = std::make_shared<DiscoveryPage>();
            local->system = page->system;
            local->sources.push_back(*found);
            DiscoveryController nav;
            nav.page(local);
            nav.tune(found->frequency - nav.view().frequency);
            nav.toggleMonitor();
            ReceiverObserver eye;
            eye.reference = found->id.planet;
            const auto up = glm::normalize(found->metres), side = tangentFrame(up) * glm::dvec3(1, 0, 0);
            eye.position = found->metres + up * 20000. - side * 12000.;
            eye.forward = glm::normalize(found->metres - eye.position);
            for (unsigned i = 0; i < 100; ++i) {
                eye.seconds += .1;
                nav.update(u, eye, .1, true, 1);
            }
            check(nav.record()->count == 1 && !nav.target(), "Navigator manufactured stationary parallax");
            eye.position = found->metres + up * 20000. + side * 12000.;
            eye.forward = glm::normalize(found->metres - eye.position);
            nav.update(u, eye, .1, true, 1);
            check(nav.target().has_value(), "Navigator failed to triangulate a genuine flight baseline");
            // Exact version-1 migration retains old discoveries and all measurements.
            const auto& record = *nav.record();
            std::ostringstream old;
            old << std::setprecision(17) << "INFINITY_DISCOVERY 1 " << u.seed() << " 100 0 0 0 1 1 "
                << std::quoted(record.id.planet.str()) << " 1 " << record.descriptor << " 1 " << record.count
                << " 1 0 " << record.estimate.x << ' ' << record.estimate.y << ' ' << record.estimate.z << ' '
                << record.uncertainty << " 0";
            for (unsigned i = 0; i < record.count; ++i) {
                const auto& b = record.bearings[i];
                old << ' ' << b.origin.x << ' ' << b.origin.y << ' ' << b.origin.z << ' ' << b.direction.x
                    << ' ' << b.direction.y << ' ' << b.direction.z << ' ' << b.quality;
            }
            DiscoveryController migrated;
            check(migrated.restore(old.str(), u) && migrated.records()[0].estimate == record.estimate,
                  "Version-1 discovery migration moved a saved fix");
        }
        const auto view = signalGameplayProofView(r.u);
        for (double aspect : {1., 16. / 9, 2.4}) {
            FlightInstruments t;
            t.discovery = std::make_shared<DiscoveryView>(view);
            const auto hud = cockpitOverlay(t, aspect, "");
            check(cockpitTextConflicts(hud).empty(), "Live call UI text overlaps");
        }
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < 100; ++i)
            trafficSignals(r.u, snapshot, std::span(&wreck, 1), 100);
        std::cout
            << "RADIO_PERF adapter_us="
            << std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() /
                   100
            << " max_sources=32 refresh_hz=2\n";
        std::cout << "SIGNAL_GAMEPLAY_PASS checks=" << checks
                  << " funded_request_delivery_distress_identity_save=passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "SIGNAL_GAMEPLAY_FAIL " << e.what() << '\n';
        return 1;
    }
}
