#define GLM_FORCE_INLINE
#include "../src/ShipSignalScenario.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/CombatAwareness.hpp"
#include "infinity/SignalTraffic.hpp"
#include <chrono>
#include <iostream>
#include <limits>

using namespace infinity;
namespace {
unsigned checks = 0;
void check(bool pass, const char* why) {
    ++checks;
    if (!pass)
        throw std::runtime_error(why);
}
struct Rig {
    Universe u;
    Address home = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
    NpcPlayer p;
    WeaponSystem weapons;
    NpcCombat npc;
    VesselOperations scans;
    ShipSignals signals;
    ActivityBody enemy;
    Rig() {
        p.reference = home;
        p.seconds = 60;
        p.position = {0, std::pow(10., u.describe(home).frame.logRadius) * 4, 0};
        enemy = sampleCombatActivity(u, {home, siegeRaiderSlot}, 60);
        enemy.position = p.position + glm::dvec3(0, 0, -1800);
        enemy.velocity = {};
        enemy.hostile = true;
        enemy.orientation = glm::quatLookAtRH(glm::dvec3(0, 0, 1), glm::dvec3(0, 1, 0));
    }
    ActivityBody physical() const {
        auto b = enemy;
        npc.apply(b);
        weapons.apply(b);
        return b;
    }
    void emit(bool scan, bool jam, bool decoded = true, double dt = .1) {
        auto b = physical();
        p.forward = glm::normalize(b.position - p.position);
        signals.update(u, p, b, scan, jam, decoded, 1, 1, dt, std::span(&b, 1));
    }
};
} // namespace
int main() {
    try {
        Rig r;
        const auto fingerprint = descriptorFingerprint(r.u.describe(r.home));
        const auto canonical = r.enemy;
        // Only actual active enemy emissions appear; no fictional public pirate beacon.
        check(shipSignals(r.u, r.p, std::span(&r.enemy, 1), r.npc, r.scans).empty(),
              "Silent enemy emitted a fictional carrier");
        r.npc.update(r.u, r.p, std::span(&r.enemy, 1), r.weapons, .1);
        auto physical = r.physical();
        auto sources = shipSignals(r.u, r.p, std::span(&physical, 1), r.npc, r.scans);
        check(sources.size() == 1 && shipSignalId(sources[0].id) && liveSignalId(sources[0].id),
              "Real tactical emitter not adapted");
        check(sources[0].contact == physical.id && sources[0].metres == physical.position &&
                  !sources[0].transmitsPosition,
              "Tactical carrier replaced identity, position or invented a transponder fix");
        check(sources[0].transmission[0].find("MISSILE LOCK") != std::string::npos,
              "Emission omitted actual partial missile acquisition");
        auto page = std::make_shared<DiscoveryPage>();
        page->system = r.home.parent();
        DiscoveryController receiver;
        receiver.page(page);
        receiver.traffic(sources);
        ReceiverObserver eye;
        eye.reference = r.home;
        eye.position = r.p.position;
        eye.forward = glm::normalize(physical.position - eye.position);
        auto tick = [&](unsigned n, bool listen) {
            for (unsigned i = 0; i < n; ++i) {
                eye.seconds += .1;
                receiver.update(r.u, eye, .1, listen, 1);
            }
        };
        tick(1, false);
        check(receiver.view().ships == 1 && receiver.view().calls == 0 && !receiver.shipContact(),
              "Sweep leaked unexamined identity or polluted emergency calls");
        receiver.nextCall();
        check(!receiver.source(), "Emergency cycling selected routine ship traffic");
        receiver.nextShip();
        tick(70, true);
        check(receiver.shipContact() == physical.id && !receiver.target() && !receiver.view().located,
              "Passive hostile decoding exposed a precision waypoint");
        check(receiver.recordBearing().starts_with("MOVING SHIP") && receiver.record()->count == 0,
              "Moving emitter manufactured static parallax");
        // Receiver and existing boarding/targeting scans share the exact same record.
        for (unsigned i = 0; i < 50; ++i) {
            r.p.seconds += .1;
            r.emit(true, false);
            auto b = r.physical();
            r.scans.observe(r.u, r.p, b, r.npc.engineering(b.id), .1, r.signals.scanning(), 1,
                            std::span(&b, 1));
        }
        check(r.scans.confirmed(physical.id, r.p.seconds),
              "Focused signals scan failed existing internal scan");
        const auto readout = r.signals.view(physical, r.scans, r.npc.engineering(physical.id), r.p.seconds);
        check(readout.confirmed && readout.condition.find("HULL 100%") != std::string::npos,
              "Scan report is not based on actual ship systems");
        receiver.traffic(shipSignals(r.u, r.p, std::span(&physical, 1), r.npc, r.scans));
        tick(1, false);
        check(receiver.target() && receiver.target()->contact == physical.id &&
                  receiver.target()->metres == physical.position,
              "Confirmed active scan did not fix the same existing ship");
        const auto save = receiver.serialize(r.u.seed());
        DiscoveryController restored;
        check(restored.restore(save, r.u) && restored.serialize(r.u.seed()) == save,
              "New ship channels lost exact saved identity");
        // Old measurements never act as an everlasting live hostile position source.
        r.p.seconds += 46;
        receiver.traffic(shipSignals(r.u, r.p, std::span(&physical, 1), r.npc, r.scans));
        tick(1, false);
        check(!receiver.target() && !receiver.view().located, "Expired active fix remained actionable");
        r.weapons.receiveNpcDamage(physical, 15, r.p.seconds);
        physical = r.physical();
        r.scans.observe(r.u, r.p, physical, r.npc.engineering(physical.id), .1, false, 1,
                        std::span(&physical, 1));
        check(!r.signals.view(physical, r.scans, r.npc.engineering(physical.id), r.p.seconds).confirmed,
              "Changed physical hull retained a confirmed old internal scan");
        eye.position = -physical.position;
        eye.forward = glm::normalize(physical.position - eye.position);
        tick(1, false);
        check(!receiver.shipContact() && !receiver.target() &&
                  receiver.view().transmission[0].starts_with("CARRIER LOST"),
              "Planetary occlusion leaked updated ship telemetry");
        // Honest public transponder fix, bounded nearest contacts, immutable source data.
        std::vector<ActivityBody> traffic;
        for (unsigned i = 0; i < 40; ++i) {
            auto b = physical;
            b.id.slot = 100 + i;
            b.kind = ActivityKind::Freighter;
            b.hostile = false;
            b.position = r.p.position + glm::dvec3(i * 100., 0, -1000);
            traffic.push_back(b);
        }
        auto publicSignals = shipSignals(r.u, r.p, traffic, r.npc, r.scans);
        check(publicSignals.size() == 16 && publicSignals[0].contact == traffic[0].id &&
                  publicSignals[0].transmitsPosition,
              "Transponder catalog is unbounded or ignores physical nearest order");
        receiver.traffic({publicSignals[0]});
        eye.position = r.p.position;
        eye.forward = glm::normalize(traffic[0].position - eye.position);
        tick(1, false);
        receiver.nextShip();
        tick(70, true);
        check(receiver.target() && receiver.target()->contact == traffic[0].id,
              "Civilian transponder could not broadcast its real fix");
        // Beam charge, targeting, line-of-sight, size and power are physical limits.
        Rig j;
        j.npc.update(j.u, j.p, std::span(&j.enemy, 1), j.weapons, .1);
        j.emit(false, true, false);
        check(!j.signals.jammer(), "Undecoded carrier was jammed");
        j.emit(false, true);
        check(j.signals.jammer() && j.signals.radiating(), "Valid carrier did not produce a real beam");
        const auto beam = *j.signals.jammer();
        check(signalBeamStrength(j.u, beam, j.home, j.enemy.position) > .3 &&
                  signalBeamStrength(j.u, beam, j.home, j.p.position + glm::dvec3(2000, 0, 0)) == 0,
              "Directional jammer affects ships outside its cone");
        auto block = j.enemy;
        block.id.slot = 1200;
        block.kind = ActivityKind::Debris;
        block.radius = 100;
        block.position = j.p.position + glm::dvec3(0, 0, -900);
        const std::array blocked{j.physical(), block};
        j.signals.update(j.u, j.p, blocked[0], false, true, true, 1, 1, .1, blocked);
        check(!j.signals.jammer(), "Jammer reached through physical orbital cover");
        j.p.size = .001;
        j.emit(false, true);
        check(!j.signals.jammer(), "Tiny ship emitted full-size electronic warfare power");
        j.p.size = 1;
        j.p.field = true;
        j.emit(true, true);
        check(!j.signals.radiating(), "Warp allowed an effective scan or jammer");
        j.p.field = false;
        auto neutral = j.physical();
        neutral.hostile = false;
        neutral.kind = ActivityKind::Freighter;
        j.signals.update(j.u, j.p, neutral, false, true, true, 1, 1, .1, std::span(&neutral, 1));
        check(!j.signals.jammer(), "Jammer suppresses civilian navigation signals");
        j.signals.update(j.u, j.p, j.physical(), true, false, true, 0, 1, .1, std::span(&neutral, 1));
        check(!j.signals.radiating(), "Disabled helm bus powered active sensors");
        for (unsigned i = 0; i < 60; ++i)
            j.emit(false, true);
        check(!j.signals.jammer() &&
                  j.signals.view(j.physical(), j.scans, nullptr, j.p.seconds).capacitor == 0,
              "Held jammer key bypassed finite charge");
        const auto capacitorSave = j.signals.serialize(j.u.seed());
        ShipSignals resumed;
        check(resumed.restore(capacitorSave, j.u.seed()) && resumed.serialize(j.u.seed()) == capacitorSave &&
                  !resumed.radiating(),
              "Reload refilled jammer or replayed an emission");
        check(!resumed.restore(capacitorSave + " garbage", j.u.seed()) &&
                  !resumed.restore(capacitorSave, j.u.seed() + 1),
              "Invalid emitter save accepted");
        j.emit(false, false, true, 1);
        check(j.signals.view(j.physical(), j.scans, nullptr, j.p.seconds).capacitor > 0,
              "Released emitter failed to recharge");
        for (unsigned i = 0; i < 5; ++i)
            j.emit(false, false, true, 1);
        j.emit(false, true);
        check(j.signals.jammer(), "Charged emitter failed to recover after lockout");
        // Rebased eye and source retain exactly the same physical beam strength.
        auto rebase = beam;
        rebase.reference = j.home.parent();
        rebase.origin = j.u.pointRelative(
            j.home, beam.origin / std::pow(10., j.u.describe(j.home).frame.logRadius), rebase.reference, 0);
        rebase.direction = j.u.relative(j.home, rebase.reference, 0).orientation * beam.direction;
        check(std::abs(signalBeamStrength(j.u, rebase, j.home, j.enemy.position) -
                       signalBeamStrength(j.u, beam, j.home, j.enemy.position)) < 1e-6,
              "Frame rebase changed electronic warfare coverage");
        // A jam interrupts actual enemy acquisition; release permits reacquisition.
        Rig fight;
        bool launched = false;
        for (unsigned i = 0; i < 80; ++i) {
            fight.p.seconds += .025;
            fight.emit(false, true, true, .025);
            const auto report = fight.npc.update(fight.u, fight.p, std::span(&fight.enemy, 1), fight.weapons,
                                                 .025, nullptr, {}, fight.signals.jammer(), true);
            launched |= !report.missileLaunches.empty();
        }
        check(!launched && fight.npc.agents().at(fight.enemy.id).missileLock == 0,
              "Jammed NPC still completed seeker acquisition");
        for (unsigned i = 0; i < 150 && !launched; ++i) {
            fight.p.seconds += .025;
            launched = !fight.npc.update(fight.u, fight.p, std::span(&fight.enemy, 1), fight.weapons, .025)
                            .missileLaunches.empty();
        }
        check(launched, "Jamming permanently disabled enemy weapons");
        // Mixed missile/gun batteries still shoot while their seeker is suppressed.
        Rig mixed;
        mixed.enemy = sampleRaider(mixed.u, mixed.home, 60);
        mixed.enemy.position = mixed.p.position + glm::dvec3(0, 0, -1800);
        mixed.enemy.velocity = {};
        mixed.enemy.hostile = true;
        mixed.enemy.orientation = glm::quatLookAtRH(glm::dvec3(0, 0, 1), glm::dvec3(0, 1, 0));
        SignalBeam mixedBeam{mixed.home, mixed.p.position, {0, 0, -1}, mixed.enemy.id, 6000, 1};
        for (unsigned i = 0; i < 400; ++i) {
            mixed.p.seconds += .025;
            const auto report = mixed.npc.update(mixed.u, mixed.p, std::span(&mixed.enemy, 1), mixed.weapons,
                                                 .025, nullptr, {}, &mixedBeam, true);
            check(report.missileLaunches.empty(), "Jammed mixed battery still launched guided fire");
        }
        check(mixed.npc.agents().at(mixed.enemy.id).shots > 6,
              "Jamming incorrectly silenced a mixed battery's unguided guns");
        Rig scout;
        scout.enemy = sampleCombatActivity(scout.u, {scout.home, raiderScoutSlot}, 60);
        scout.enemy.position = scout.p.position + glm::dvec3(0, 0, -1800);
        scout.enemy.velocity = {};
        scout.enemy.hostile = true;
        SignalBeam relayBeam{scout.home, scout.p.position, {0, 0, -1}, scout.enemy.id, 6000, 1};
        for (unsigned i = 0; i < 100; ++i) {
            scout.p.seconds += .025;
            scout.npc.update(scout.u, scout.p, std::span(&scout.enemy, 1), scout.weapons, .025, nullptr, {},
                             &relayBeam, true);
        }
        check(scout.npc.agents().at(scout.enemy.id).signalAt == 0 &&
                  !scout.npc.reinforcementActive(scout.home, scout.p.seconds),
              "Jammed scout kept transmitting its pursuit relay");
        for (unsigned i = 0; i < 480; ++i) {
            scout.p.seconds += .025;
            scout.npc.update(scout.u, scout.p, std::span(&scout.enemy, 1), scout.weapons, .025);
        }
        check(scout.npc.reinforcementActive(scout.home, scout.p.seconds),
              "Jamming permanently erased a scout's reinforcement ability");
        // In-flight missiles lose steering, rather than disappearing or becoming harmless.
        Rig missile;
        WeaponRay launch;
        launch.reference = missile.home;
        launch.origin = missile.p.position + glm::dvec3(0, 0, -1000);
        launch.direction = {0, 0, 1};
        launch.emitter = missile.enemy.id;
        check(missile.weapons.launchHostileMissile(launch), "Missile fixture failed");
        SignalBeam missileBeam{missile.home, missile.p.position, {0, 0, -1}, missile.enemy.id, 6000, 1};
        for (unsigned i = 0; i < 8; ++i) {
            missile.p.seconds += .1;
            missile.weapons.updateMissiles(missile.u, missile.p, {}, nullptr, .1, false, 0, {}, &missileBeam);
        }
        check(missile.weapons.missiles().size() == 1 && missile.weapons.missiles()[0].seekerLost,
              "Directional jammer did not interrupt real incoming guidance");
        double damage = 0;
        for (unsigned i = 0; i < 30; ++i)
            damage += missile.weapons.updateMissiles(missile.u, missile.p, {}, nullptr, .1, false, 0);
        check(damage > 0, "Jammer deleted a ballistic missile or granted hull immunity");
        // Radiating draws attention farther out without making peaceful patrols hostile.
        Rig attention;
        attention.enemy.position = attention.p.position + glm::dvec3(0, 0, -15000);
        NpcCombat passive, active;
        passive.update(attention.u, attention.p, std::span(&attention.enemy, 1), attention.weapons, .1);
        active.update(attention.u, attention.p, std::span(&attention.enemy, 1), attention.weapons, .1,
                      nullptr, {}, nullptr, true);
        check(passive.agents().at(attention.enemy.id).mode == NpcMode::Patrol &&
                  active.agents().at(attention.enemy.id).mode != NpcMode::Patrol,
              "Active emission had no tactical detection cost");
        // Shared data and all native-size layouts remain consistent.
        const auto views = shipSignalProofViews(r.u);
        for (const auto& view : views)
            for (double aspect : {1., 16. / 9, 2.4}) {
                FlightInstruments t;
                t.discovery = std::make_shared<DiscoveryView>(view);
                check(cockpitTextConflicts(cockpitOverlay(t, aspect, "")).empty(),
                      "Ship receiver text overlaps");
            }
        check(r.enemy.id == canonical.id && r.enemy.position == canonical.position &&
                  r.enemy.seed == canonical.seed &&
                  descriptorFingerprint(r.u.describe(r.home)) == fingerprint,
              "Signals altered procedural objects or seeds");
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < 100; ++i)
            shipSignals(r.u, r.p, traffic, r.npc, r.scans);
        std::cout
            << "SHIP_SIGNALS_PERF adapter_us="
            << std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() /
                   100
            << " max_ship_sources=16 refresh_hz=2\n";
        std::cout << "SHIP_SIGNALS_PASS checks=" << checks
                  << " identity_passive_scan_jamming_cover_finite_save_hud=passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "SHIP_SIGNALS_FAIL " << e.what() << '\n';
        return 1;
    }
}
