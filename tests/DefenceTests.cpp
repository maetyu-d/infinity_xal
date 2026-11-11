#define GLM_FORCE_INLINE
#include "../src/SignalGameplayScenario.hpp"
#include "../src/TargetingScenario.hpp"
#include "../src/ThreatScenario.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/Defence.hpp"
#include "infinity/OrbitalDebris.hpp"
#include <iostream>
#include <limits>
#include <numbers>
using namespace infinity;
namespace {
unsigned checks = 0;
void check(bool b, const char* why) {
    ++checks;
    if (!b)
        throw std::runtime_error(why);
}
struct Rig {
    Universe u;
    Address home = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
    NpcPlayer p;
    WeaponSystem weapons;
    Rig() {
        p.reference = home;
        p.position = {0, std::pow(10., u.describe(home).frame.logRadius) * 4, 0};
        p.seconds = 60;
    }
    WeaponRay launch(double distance = 700) {
        WeaponRay r;
        r.reference = home;
        r.origin = p.position + glm::dvec3(0, 0, -distance);
        r.direction = {0, 0, 1};
        r.emitter = ActivityId{home, siegeRaiderSlot};
        return r;
    }
};
} // namespace
int main() {
    try {
        Rig r;
        const auto descriptor = descriptorFingerprint(r.u.describe(r.home));
        auto enemy = sampleCombatActivity(r.u, {r.home, siegeRaiderSlot}, 60);
        enemy.position = r.p.position + glm::dvec3(0, 0, -1800);
        enemy.velocity = {};
        enemy.orientation = glm::quatLookAtRH(glm::dvec3(0, 0, 1), glm::dvec3(0, 1, 0));
        enemy.hull = 100;
        enemy.hostile = true;
        NpcCombat npc;
        double firstLock = -1, firstLaunch = -1;
        unsigned locking = 0;
        for (unsigned i = 0; i < 160; ++i) {
            r.p.seconds += .025;
            const auto report = npc.update(r.u, r.p, std::span(&enemy, 1), r.weapons, .025);
            for (const auto& t : report.threats) {
                check(t.source == enemy.id, "NPC threat lost the real attacker identity");
                if (t.phase == ThreatPhase::Locking) {
                    ++locking;
                    if (firstLock < 0)
                        firstLock = r.p.seconds;
                }
            }
            if (!report.missileLaunches.empty()) {
                firstLaunch = r.p.seconds;
                check(report.missileLaunches.front().emitter == enemy.id,
                      "Launched missile omitted its actual source");
                break;
            }
        }
        check(locking > 15 && firstLaunch - firstLock >= 1.1,
              "Enemy launched without a useful, continuous acquisition warning");
        NpcCombat restoredNpc;
        check(restoredNpc.restore(npc.serialize(r.u.seed()), r.u) &&
                  restoredNpc.serialize(r.u.seed()) == npc.serialize(r.u.seed()),
              "NPC threat/acquisition state did not persist");
        ActivityBody cover;
        cover.id = {r.home, 1200};
        cover.kind = ActivityKind::Debris;
        cover.position = r.p.position + glm::dvec3(0, 0, -900);
        cover.radius = 250;
        NpcCombat covered;
        bool falseLock = false;
        for (unsigned i = 0; i < 100; ++i) {
            r.p.seconds += .025;
            const auto report = covered.update(r.u, r.p, std::span(&enemy, 1), r.weapons, .025, nullptr,
                                               std::span(&cover, 1));
            falseLock |= !report.missileLaunches.empty();
            for (const auto& t : report.threats)
                falseLock |= t.phase == ThreatPhase::Locking;
        }
        check(!falseLock, "Enemy acquired or launched a missile through solid orbital cover");
        // Actual hit and lock evidence becomes directional, named HUD information.
        CombatAwareness awareness;
        ThreatObservation hit{enemy.id,         r.home, r.p.position + glm::dvec3(200, 0, 0),
                              ThreatPhase::Hit, 1,      enemy.name};
        awareness.update(r.u, r.p, std::span(&hit, 1), {}, {}, .1);
        check(awareness.view().attacker == enemy.id &&
                  awareness.view().cues.front().phase == ThreatPhase::Hit &&
                  awareness.view().cues.front().direction.x > .99,
              "Hit cue does not point toward the actual attacker");
        hit.phase = ThreatPhase::Locking;
        hit.progress = .5;
        awareness.update(r.u, r.p, std::span(&hit, 1), {}, {}, .1);
        check(awareness.view().warning.find(enemy.name) != std::string::npos &&
                  awareness.view().warning.find("50%") != std::string::npos,
              "Recent hit flash hid the named missile-acquisition progress");
        hit.phase = ThreatPhase::Hit;
        auto rotated = r.p;
        rotated.orientation = glm::angleAxis(std::numbers::pi / 2, glm::dvec3(0, 0, 1));
        awareness.update(r.u, rotated, std::span(&hit, 1), {}, {}, .1);
        check(awareness.view().cues.front().direction.y < -.99, "Hit cue ignored physical ship roll");
        auto parent = r.p;
        parent.reference = r.home.parent();
        parent.position = r.u.pointRelative(
            r.home, r.p.position / std::pow(10., r.u.describe(r.home).frame.logRadius), parent.reference, 0);
        parent.orientation = r.u.relative(r.home, parent.reference, 0).orientation;
        awareness.update(r.u, parent, std::span(&hit, 1), {}, {}, .1);
        check(awareness.view().cues.front().direction.x > .99,
              "Threat bearing changed after a coordinate rebase");
        {
            CombatAwareness shots;
            auto firing = hit;
            firing.phase = ThreatPhase::Gunfire;
            shots.update(r.u, r.p, std::span(&firing, 1), {}, {}, .1);
            firing.phase = ThreatPhase::Tracking;
            shots.update(r.u, r.p, std::span(&firing, 1), {}, {}, .1);
            check(shots.view().cues.front().phase == ThreatPhase::Gunfire,
                  "Gunfire warning vanished one frame after a missed shot");
            shots.update(r.u, r.p, {}, {}, {}, .5);
            check(!shots.view().active, "Old gunfire warning failed to clear");
        }
        auto vanished = r.p;
        vanished.field = true;
        awareness.update(r.u, vanished, {}, {}, {}, .1);
        check(!awareness.view().active, "Warp retained stale threat warnings");
        // Finite rack, durable receipts, reserved cargo, exact restart and migration.
        auto network = signalProofNetwork(r.u);
        Economy economy(network);
        economy.advanceTo(20);
        EconomyPlayer player;
        player.credits = 10000000;
        const auto money = economy.moneyTotal() + player.credits;
        SurveyObserver observer;
        observer.reference = r.home;
        observer.position = r.p.position;
        observer.seconds = 100;
        auto execute = [&](EconomicCommand c) {
            c.sequence = player.lastCommand + 1;
            return economy.execute(c, player);
        };
        DefenceController controller;
        auto snapshot = signalProofSnapshot(economy, player);
        auto release = controller.command(snapshot, observer, false);
        check(bool(release), "Starter defence rack was unavailable");
        release->sequence = player.lastCommand + 1;
        controller.sent(release->sequence, false);
        check(economy.execute(*release, player).starts_with("DECOY RELEASE") &&
                  player.decoys == startingDecoys - 1,
              "Release was not backed by exactly one finite cartridge");
        const auto used = player.decoys;
        check(economy.execute(*release, player) == "ORDER ALREADY PROCESSED" && player.decoys == used,
              "Replayed release consumed or duplicated a cartridge");
        r.p.seconds = 100;
        check(controller.reconcile(signalProofSnapshot(economy, player), r.p, r.weapons) &&
                  r.weapons.decoys().size() == 3,
              "Durable release receipt did not create one physical burst");
        controller.reconcile(signalProofSnapshot(economy, player), r.p, r.weapons);
        check(r.weapons.decoys().size() == 3, "Repeated receipt spawned a second decoy burst");
        DefenceController restarted;
        WeaponSystem cold;
        restarted.reconcile(signalProofSnapshot(economy, player), r.p, cold);
        check(cold.decoys().empty(), "Reload replayed an already-spent defence effect");
        {
            DefenceController unloaded;
            EconomySnapshot empty;
            unloaded.command(empty, observer, false);
            WeaponSystem none;
            unloaded.reconcile(signalProofSnapshot(economy, player), r.p, none);
            check(none.decoys().empty(), "Unloaded economy snapshot allowed a historical receipt to replay");
        }

        check(!controller.command(signalProofSnapshot(economy, player), observer, false),
              "Ejector cooldown could be bypassed by another command");
        const auto wallet = player.serialize();
        EconomyPlayer reloaded;
        check(reloaded.restore(wallet) && reloaded.serialize() == wallet && reloaded.decoys == used,
              "Rack/cooldown receipt changed across restart");
        auto legacy = wallet;
        legacy.replace(legacy.find("WALLET 11"), 9, "WALLET 10");
        for (unsigned i = 0; i < 3; ++i)
            legacy.resize(legacy.rfind(' '));
        check(reloaded.restore(legacy) && reloaded.decoys == startingDecoys && !reloaded.decoyLaunch,
              "Legacy wallet did not receive a one-time starter rack");
        EconomicCommand build;
        build.action = EconomicAction::BuildDecoy;
        build.observer = observer;
        check(execute(build).starts_with("BUILD NEEDS"), "Empty cargo fabricated an unlimited decoy");
        for (auto good : {Good::Alloy, Good::Electronics}) {
            EconomicCommand buy;
            buy.port = network->sites[0].id;
            buy.action = EconomicAction::Buy;
            buy.good = good;
            buy.kg = 2;
            buy.limitCents = 1000000000;
            check(execute(buy).starts_with("BOUGHT"), "Real port could not supply decoy components");
        }
        const auto cargo = player.cargo;
        check(execute(build).starts_with("DECOY BUILT") && player.decoys == used + 1 &&
                  player.cargo[size_t(Good::Alloy)] == cargo[size_t(Good::Alloy)] - 1 &&
                  player.cargo[size_t(Good::Electronics)] == cargo[size_t(Good::Electronics)] - 1,
              "Repacking did not consume the finite two-component kit");
        check(economy.moneyTotal() + player.credits == money, "Defence manufacturing minted or lost money");
        const auto inv = economy.inventoryTotal(), initial = economy.initialTotal(),
                   delta = economy.externalGoodsDelta();
        for (size_t g = 0; g < goodCount; ++g)
            check(inv[g] == initial[g] + delta[g], "Countermeasure production double-counted a world export");
        Economy economicReload(network);
        check(economicReload.restore(economy.serialize()),
              "Consumed defence-material receipts did not restore");
        observer.size = 1e-6;
        release = EconomicCommand{};
        release->action = EconomicAction::DeployDecoy;
        release->observer = observer;
        observer.seconds = 106;
        const auto stock = player.decoys;
        execute(*release);
        check(player.decoys == stock, "Tiny ship deployed a full-size cartridge");
        controller.toggleAutomatic();
        DefenceController preference;
        check(preference.restore(controller.serialize()) && !preference.automatic(),
              "Emergency automatic/manual preference did not persist");
        // Flares attract a seeker physically, rather than deleting its projectile.
        Rig decoy;
        check(decoy.weapons.launchHostileMissile(decoy.launch()), "Cannot launch test missile");
        for (unsigned i = 0; i < 10; ++i)
            decoy.weapons.updateMissiles(decoy.u, decoy.p, {}, nullptr, .05, false, 0);
        decoy.weapons.deployDecoys(decoy.p, 1);
        decoy.weapons.deployDecoys(decoy.p, 1);
        check(decoy.weapons.decoys().size() == 3, "Decoy burst is not bounded/idempotent");
        bool diverted = false;
        double damage = 0;
        for (unsigned i = 0; i < 120; ++i) {
            damage += decoy.weapons.updateMissiles(decoy.u, decoy.p, {}, nullptr, .05, false, 0);
            for (const auto& e : decoy.weapons.defenceEvents())
                if (e.outcome == DefenceOutcome::Diverted) {
                    diverted = true;
                    check(!decoy.weapons.missiles().empty() &&
                              decoy.weapons.missiles().front().decoyTarget.has_value(),
                          "Decoy instantly deleted a missile instead of changing its seeker target");
                }
        }
        check(diverted && damage == 0, "Timely physical decoys failed to pull the seeker away");
        Rig late;
        late.weapons.launchHostileMissile(late.launch(32));
        late.weapons.deployDecoys(late.p, 1);
        check(late.weapons.updateMissiles(late.u, late.p, {}, nullptr, .01, false, 0) == 27,
              "Countermeasure supplied invulnerability to a physically unavoidable impact");
        Rig behind;
        behind.weapons.launchHostileMissile(behind.launch(1200));
        behind.p.position.x = 400;
        cover.position = {200, behind.p.position.y, -550};
        cover.radius = 110;
        bool lost = false;
        damage = 0;
        for (unsigned i = 0; i < 100; ++i) {
            damage += behind.weapons.updateMissiles(behind.u, behind.p, {}, nullptr, .1, false, 0,
                                                    std::span(&cover, 1));
            for (const auto& e : behind.weapons.defenceEvents())
                lost |= e.outcome == DefenceOutcome::LockBroken;
        }
        check(lost && damage == 0, "Cover did not break missile guidance into physical ballistic flight");
        Rig pd;
        pd.weapons.setLoadout({1, pointDefence, 0});
        pd.weapons.setMountReadiness(1, 1);
        pd.weapons.launchHostileMissile(pd.launch(600));
        bool intercepted = false;
        for (unsigned i = 0; i < 40; ++i) {
            pd.weapons.advance(.05);
            pd.weapons.updateMissiles(pd.u, pd.p, {}, nullptr, .05, true, 0);
            for (const auto& e : pd.weapons.defenceEvents())
                intercepted |= e.outcome == DefenceOutcome::Intercepted;
        }
        check(intercepted && pd.weapons.mount(1).shotsFired == 1,
              "Point-defence feedback did not correspond to an actual crew shot");
        Rig automatic;
        automatic.weapons.launchHostileMissile(automatic.launch(600));
        automatic.weapons.updateMissiles(automatic.u, automatic.p, {}, nullptr, .05, false, 0);
        auto ready = signalProofSnapshot(economy, player);
        ready.player = EconomyPlayer{};
        DefenceController autoController;
        check(autoController.automaticBurst(ready, automatic.p, automatic.weapons, true, 0) &&
                  !autoController.automaticBurst(ready, automatic.p, automatic.weapons, false, 0),
              "Routine decoy defence ignores actual crew readiness");
        automatic.weapons.setLoadout({1, pointDefence, 0});
        automatic.weapons.setMountReadiness(1, 1);
        check(!autoController.automaticBurst(ready, automatic.p, automatic.weapons, true, 0),
              "Auto defence wastes a canister when ready point defence covers the threat");
        Rig obstructed;
        obstructed.weapons.setLoadout({1, pointDefence, 0});
        obstructed.weapons.setMountReadiness(1, 1);
        obstructed.weapons.launchHostileMissile(obstructed.launch(600));
        cover.position = obstructed.p.position + glm::dvec3(0, 0, -300);
        cover.radius = 70;
        obstructed.weapons.updateMissiles(obstructed.u, obstructed.p, {}, nullptr, .05, true, 0,
                                          std::span(&cover, 1));
        check(obstructed.weapons.mount(1).shotsFired == 0,
              "Point defence fired through a solid orbital body");
        observer.size = 1;
        observer.landed = true;
        build.observer = observer;
        check(execute(build).starts_with("DECOY BUILT"),
              "Safe landed resupply could not assemble a finite cartridge");
        {
            auto rock = cover;
            rock.id = {r.home, orbitalDebrisFirstSlot};
            rock.position = r.p.position + glm::dvec3(0, 0, -500);
            rock.radius = 50;
            rock.hull = 100;
            auto hidden = enemy;
            hidden.position = r.p.position + glm::dvec3(0, 0, -1000);
            const std::array<ActivityBody, 2> scene{hidden, rock};
            WeaponSystem pulse;
            WeaponRay beam;
            beam.reference = r.home;
            beam.origin = r.p.position;
            check(pulse.fire(r.u, beam, scene) && pulse.damage().empty() && pulse.shots().back().hit,
                  "Player fire ignored or destroyed immutable orbital cover");
            TargetingComputer seeker;
            WeaponSystem launcher;
            launcher.setLevel(missileLauncher);
            for (unsigned i = 0; i < 20; ++i)
                seeker.update(r.u, beam, {}, hidden.id, launcher, 0, scene, .1, false, 1);
            check(!seeker.lockedTarget() && seeker.view().state == TargetLock::Blocked,
                  "Player seeker locked through orbital cover");
        }
        const auto targetView = targetingProofViews(r.u)[1];
        const auto views = threatProofViews(r.u);
        for (auto view : views)
            for (double aspect : {1., 16. / 9, 2.4})
                for (bool radio : {false, true}) {
                    FlightInstruments t;
                    t.awareness = std::make_shared<CombatAwarenessView>(view);
                    t.hostileContacts = 1;
                    t.weapon = "PULSE LASER I";
                    t.weaponStatus = "READY";
                    if (view.missiles)
                        t.localMissile = {0, 0, -1};
                    if (radio) {
                        auto d = std::make_shared<DiscoveryView>();
                        d->monitoring = true;
                        t.discovery = d;
                    }
                    t.targeting = std::make_shared<TargetingView>(targetView);
                    auto hud = cockpitOverlay(t, aspect, "");
                    check(cockpitTextConflicts(hud).empty(), "Threat/radio/defence text overlaps");
                    const auto lead = cockpitTargetMarker(targetView.intercept, aspect);
                    if (!lead.offscreen && !lead.aft)
                        check(std::none_of(hud.textRuns.begin(), hud.textRuns.end(),
                                           [&](const auto& text) {
                                               return text.bounds.overlaps(
                                                   {lead.point.x - 8, lead.point.y - 8, 16, 16});
                                           }),
                              "Attacker label covered the selected missile intercept cue");

                    for (const auto& run : hud.textRuns)
                        check(run.bounds.x >= 0 && run.bounds.y >= 0 &&
                                  run.bounds.x + run.bounds.w <= 900 * aspect + .01 &&
                                  run.bounds.y + run.bounds.h <= 900,
                              "Threat instrument clipped outside screen");
                }
        check(descriptorFingerprint(r.u.describe(r.home)) == descriptor,
              "Threat and defence systems modified procedural world descriptors");
        std::cout << "DEFENCE_PASS checks=" << checks
                  << " attacker_lock_cover_decoy_finite_receipts_pd_hud=passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "DEFENCE_FAIL " << e.what() << '\n';
        return 1;
    }
}
