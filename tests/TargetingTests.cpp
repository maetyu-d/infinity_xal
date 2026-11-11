#include "../src/TargetingScenario.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/CombatActors.hpp"
#include "infinity/NpcCombat.hpp"
#include "infinity/Targeting.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
using namespace infinity;
namespace {
unsigned checks = 0;
void check(bool yes, const char* why) {
    ++checks;
    if (!yes)
        throw std::runtime_error(why);
}
struct Rig {
    Universe u;
    Address home = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
    ActivityBody enemy = sampleRaider(u, home, 60);
    WeaponRay ray;
    WeaponSystem guns;
    TargetingComputer tracker;
    Rig() {
        const double altitude = std::pow(10., u.describe(home).frame.logRadius) * 4;
        enemy.position = {90, altitude, -1000};
        enemy.velocity = {};
        enemy.radius = 20;
        enemy.hostile = true;
        enemy.disabled = false;
        enemy.hull = 100;
        ray.reference = home;
        ray.origin = {0, altitude, 0};
        guns.setLevel(missileLauncher);
    }
    void tick(unsigned count = 15, std::span<const ActivityBody> input = {}) {
        if (input.empty())
            input = std::span(&enemy, 1);
        for (unsigned i = 0; i < count; ++i)
            tracker.update(u, ray, {}, enemy.id, guns, 0, input, .1, false, 1);
    }
};
} // namespace
int main() {
    try {
        Rig r;
        const auto before = descriptorFingerprint(r.u.describe(r.home));
        std::array<ActivityContact, 5> contacts;
        for (unsigned i = 0; i < contacts.size(); ++i) {
            contacts[i].body = r.enemy;
            contacts[i].body.id.slot += i;
            contacts[i].distance = 2000 - i * 200;
            contacts[i].localDirection = {double(i) * .01, 0, -1};
        }
        contacts[2].body.hostile = false;
        contacts[3].body.disabled = true;
        contacts[4].occluded = true;
        check(selectHostile(contacts, {}) == contacts[1].body.id,
              "Initial hostile selection ignored nearest valid enemy");
        check(selectHostile(contacts, contacts[1].body.id) == contacts[0].body.id,
              "H failed to cycle to another hostile");
        std::reverse(contacts.begin(), contacts.end());
        check(selectHostile(contacts, r.enemy.id) == contacts[3].body.id,
              "Presentation reorder changed stable hostile cycle");
        check(selectHostile(contacts, contacts[3].body.id, HostileSelection::Previous) == r.enemy.id,
              "Reverse hostile cycle failed");
        check(selectHostile(contacts, {}, HostileSelection::Crosshair) == r.enemy.id,
              "Crosshair selection ignored alignment");
        for (auto& c : contacts)
            c.localDirection = {0, 0, 1};
        check(!selectHostile(contacts, {}, HostileSelection::Crosshair),
              "Crosshair acquisition selected a ship behind the player");
        check(!selectHostile({}, {}), "Empty sensors invented a hostile");
        ActivityState state;
        state.select(r.enemy.id, true);
        state.seconds = 60;
        ActivityState restored;
        check(restored.restore(state.serialize(), r.u) && restored.combatSelection &&
                  restored.selected == state.selected,
              "Combat-only selection did not survive reload");
        check(restored.restore("INFINITY_ACTIVITY 1 60 1 " + r.enemy.id.str(), r.u) &&
                  !restored.combatSelection,
              "Legacy activity selection did not migrate as navigation");
        const auto valid = restored.serialize();
        check(!restored.restore("INFINITY_ACTIVITY 2 60 1 - 1", r.u) && restored.serialize() == valid,
              "Invalid combat selection corrupted valid activity state");
        r.tick(5);
        check(r.tracker.view().state == TargetLock::Acquiring && !r.tracker.lockedTarget(),
              "A selected target immediately became a missile lock");
        r.tick(10);
        check(r.tracker.view().state == TargetLock::Locked && r.tracker.lockedTarget() == r.enemy.id,
              "Continuous observation failed to acquire selected identity");
        check(r.tracker.view().progress == 1 && r.tracker.view().flightTime > 0 && r.tracker.view().cue == 0,
              "Completed lock or once-only audio cue is inconsistent");
        // Fire at an off-axis selected target while a different hostile is under the reticle.
        auto other = r.enemy;
        other.id.slot++;
        other.position = r.ray.origin + glm::dvec3(0, 0, -800);
        other.radius = 8;
        const std::array<ActivityBody, 2> enemies{r.enemy, other};
        auto guided = r.ray;
        guided.seekerControlled = true;
        check(!r.guns.fire(r.u, guided, enemies) && r.guns.shotsFired() == 0 && r.guns.heat() == 0,
              "Missile fired without a measured selected lock");
        guided.guidedTarget = r.tracker.lockedTarget();
        check(r.guns.fire(r.u, guided, enemies) && r.guns.missiles().front().target == r.enemy.id,
              "Guided launch silently switched to the hostile under the reticle");
        check(r.guns.damage().empty(), "Guided launch applied damage before physical missile flight");
        r.tick(1, enemies);
        check(!r.tracker.lockedTarget() && r.tracker.view().status.find("RELOADING") != std::string::npos,
              "Completed seeker lock hid launcher reload state");
        NpcPlayer player;
        player.reference = r.home;
        player.position = r.ray.origin;
        for (unsigned i = 0; i < 160; ++i)
            r.guns.updateMissiles(r.u, player, enemies, nullptr, .025, false, 0);
        check(r.guns.damage().size() == 1 && r.guns.damage().front().id == r.enemy.id &&
                  r.guns.damage().front().hull == 62,
              "Selected missile failed to hit its actual target once");
        Rig blocked;
        blocked.tick();
        auto friendly = blocked.enemy;
        friendly.id.slot = 100;
        friendly.hostile = false;
        friendly.kind = ActivityKind::Freighter;
        friendly.position = blocked.ray.origin + (blocked.enemy.position - blocked.ray.origin) * .5;
        friendly.radius = 30;
        const std::array<ActivityBody, 2> crowd{blocked.enemy, friendly};
        blocked.tick(1, crowd);
        check(blocked.tracker.view().state == TargetLock::Memory && !blocked.tracker.lockedTarget(),
              "Brief obstruction allowed firing through another ship");
        blocked.tick(4, crowd);
        check(blocked.tracker.view().state == TargetLock::Blocked && !blocked.tracker.lockedTarget(),
              "Occluded lock failed to expire");
        auto shot = blocked.ray;
        shot.seekerControlled = true;
        shot.guidedTarget = blocked.enemy.id;
        check(!blocked.guns.fire(blocked.u, shot, crowd) && blocked.guns.heat() == 0 &&
                  blocked.guns.shotsFired() == 0,
              "Final missile trace damaged an intervening friendly or consumed launch resources");
        blocked.tick(15);
        check(blocked.tracker.lockedTarget() == blocked.enemy.id,
              "Lock did not recover after sightline cleared");
        blocked.ray.direction = glm::normalize(glm::dvec3(.6, 0, -1));
        blocked.tick(4);
        check(blocked.tracker.view().state == TargetLock::OffBoresight && !blocked.tracker.lockedTarget(),
              "Seeker maintained lock outside its tracking cone");
        blocked.ray.direction = {0, 0, -1};
        blocked.tick();
        blocked.tracker.update(blocked.u, blocked.ray, {}, other.id, blocked.guns, 0, std::span(&other, 1),
                               .1, false, 1);
        check(blocked.tracker.view().progress < .2 && !blocked.tracker.lockedTarget(),
              "New identity inherited an old completed lock");
        blocked.tracker.update(blocked.u, blocked.ray, {}, other.id, blocked.guns, 0, {}, .1, false, 1);
        check(blocked.tracker.view().state == TargetLock::Lost && !blocked.tracker.lockedTarget(),
              "Disappearing contact left an actionable lock");
        Rig range;
        range.enemy.position.z = -4000;
        range.tick();
        check(range.tracker.view().state == TargetLock::OutOfRange && !range.tracker.lockedTarget(),
              "Out-of-range target became launchable");
        range.enemy.position.z = -1000;
        range.enemy.velocity = {0, 0, -500};
        range.tick();
        check(range.tracker.view().state == TargetLock::Locked && !range.tracker.lockedTarget() &&
                  range.tracker.view().flightTime < 0,
              "Uncatchable target received a false missile intercept");
        range.enemy.velocity = {40, 0, 0};
        range.tick();
        check(range.tracker.view().intercept.x > range.tracker.view().direction.x,
              "Moving target did not receive a physical interception cue");
        range.tracker.update(range.u, range.ray, {0, 0, -100}, range.enemy.id, range.guns, 0,
                             std::span(&range.enemy, 1), .1, false, 1);
        check(range.tracker.view().closing > 90, "Closing rate ignored ship velocity");
        range.guns.setLevel(1);
        range.tick();
        check(!range.tracker.view().missile && !range.tracker.view().interceptValid,
              "Hitscan laser received fictitious projectile lead");
        range.enemy.disabled = true;
        range.tick();
        check(range.tracker.view().state == TargetLock::Disabled && !range.tracker.lockedTarget(),
              "Disabled target remained locked");
        range.enemy.disabled = false;
        range.enemy.hostile = false;
        range.tick();
        check(!range.tracker.lockedTarget(), "Friendly or surrendered ship remained launchable");
        Rig tiny;
        tiny.ray.scale = 1e-6;
        tiny.tick();
        check(!tiny.tracker.lockedTarget(), "Microscopic ship launched a full-sized missile");
        Rig inhibit;
        inhibit.tick();
        inhibit.tracker.update(inhibit.u, inhibit.ray, {}, inhibit.enemy.id, inhibit.guns, 0,
                               std::span(&inhibit.enemy, 1), .1, true, 1);
        check(!inhibit.tracker.lockedTarget(), "Warp/modal/landing inhibition retained launch authority");
        // Same eye and targets in a parent coordinate frame must retain the fix.
        Rig parent;
        auto ray = parent.ray;
        ray.reference = parent.home.parent();
        ray.origin = parent.u.pointRelative(
            parent.home, parent.ray.origin / std::pow(10., parent.u.describe(parent.home).frame.logRadius),
            ray.reference, 0);
        const auto rotation = parent.u.relative(parent.home, ray.reference, 0).orientation;
        ray.direction = rotation * parent.ray.direction;
        ray.right = rotation * parent.ray.right;
        for (unsigned i = 0; i < 15; ++i)
            parent.tracker.update(parent.u, ray, {}, parent.enemy.id, parent.guns, 0,
                                  std::span(&parent.enemy, 1), .1, false, 1);
        check(parent.tracker.lockedTarget() == parent.enemy.id &&
                  std::abs(parent.tracker.view().range -
                           glm::length(parent.enemy.position - parent.ray.origin)) < .01,
              "Reference-frame change altered target identity, range or lock");
        check(!missileInterceptTime({0, 0, -1000}, {0, 0, -500}) &&
                  missileInterceptTime({0, 0, -1000}, {0, 0, 0}).has_value(),
              "Intercept solver disagrees with actual projectile speed");
        Rig invalid;
        invalid.tick();
        auto bad = invalid.ray;
        bad.origin.x = std::numeric_limits<double>::quiet_NaN();
        invalid.tracker.update(invalid.u, bad, {}, invalid.enemy.id, invalid.guns, 0,
                               std::span(&invalid.enemy, 1), .1, false, 1);
        check(!invalid.tracker.lockedTarget(), "Invalid camera coordinates retained launch authority");
        {
            WeaponSystem mixed;
            mixed.setLoadout({1, missileLauncher, missileLauncher});
            mixed.setMountReadiness(1, 1);
            mixed.setMountReadiness(2, 1);
            check(targetingMount(mixed, 0, false) == 1,
                  "Nose laser hid a staffed battery missile launcher from the seeker");
            check(targetingMount(mixed, 2, true) == 2, "Seeker ignored the captain's active weapon station");
            auto aim = r.ray;
            aim.direction = glm::normalize(r.enemy.position - r.ray.origin);
            check(mixed.fireMount(1, r.u, aim, std::span(&r.enemy, 1)),
                  "Mixed-battery fixture could not launch from its first staffed bay");
            check(targetingMount(mixed, 0, false) == 2,
                  "Reloading launcher hid a second ready missile station");
            TargetingComputer battery;
            for (unsigned i = 0; i < 15; ++i)
                battery.update(r.u, r.ray, {}, r.enemy.id, mixed, targetingMount(mixed, 0, false),
                               std::span(&r.enemy, 1), .1, false, 1);
            check(battery.lockedTarget() == r.enemy.id,
                  "Mixed battery failed to acquire a usable missile lock");
            auto command = r.ray;
            command.seekerControlled = true;
            command.guidedTarget = battery.lockedTarget();
            check(mixed.fireBattery(r.u, command, std::span(&r.enemy, 1)) & (1u << 2),
                  "Player battery failed to fire its locked secondary launcher");
        }
        const auto views = targetingProofViews(r.u);
        for (const auto& view : views)
            for (double aspect : {1., 16. / 9, 2.4})
                for (bool turret : {false, true}) {
                    FlightInstruments t;
                    t.targeting = std::make_shared<TargetingView>(view);
                    t.hostileContacts = 2;
                    t.weapon = "GUIDED MISSILE";
                    t.weaponStatus = "READY";
                    t.playerGun = turret ? 1 : noWeaponMount;
                    t.targetVisible = true;
                    t.localTarget = {20, 5, -1000};
                    t.targetLabel = "NAVIGATION DESTINATION";
                    const auto hud = cockpitOverlay(t, aspect, "");
                    check(cockpitTextConflicts(hud).empty(),
                          "Lock, navigation, turret and weapon text overlap");
                    if (view.missile && view.interceptValid) {
                        const auto cue = cockpitTargetMarker(view.intercept, aspect);
                        if (!cue.offscreen && !cue.aft)
                            check(std::none_of(hud.textRuns.begin(), hud.textRuns.end(),
                                               [&](const auto& run) {
                                                   return run.bounds.overlaps(
                                                       {cue.point.x - 8, cue.point.y - 8, 16, 16});
                                               }),
                                  "Target label obscured the physical missile intercept cue");
                    }
                    for (const auto& text : hud.textRuns)
                        check(text.bounds.x >= 0 && text.bounds.y >= 0 &&
                                  text.bounds.x + text.bounds.w <= 900 * aspect + .01 &&
                                  text.bounds.y + text.bounds.h <= 900,
                              "Targeting text escaped viewport");
                }
        check(descriptorFingerprint(r.u.describe(r.home)) == before,
              "Targeting modified procedural world descriptors");
        const auto start = std::chrono::steady_clock::now();
        Rig perf;
        for (unsigned i = 0; i < 300; ++i)
            perf.tracker.update(perf.u, perf.ray, {}, perf.enemy.id, perf.guns, 0, std::span(&perf.enemy, 1),
                                1. / 60, false, 1);
        std::cout
            << "TARGETING_PERF mean_update_us="
            << std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() /
                   300
            << " sightline_refresh_hz=10\n";
        std::cout << "TARGETING_PASS checks=" << checks
                  << " selection_lock_missiles_obstruction_rebase_navigation=passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "TARGETING_FAIL " << e.what() << '\n';
        return 1;
    }
}
