#define GLM_FORCE_INLINE
#include "infinity/Economy.hpp"
#include "infinity/Excavation.hpp"
#include "infinity/MiningConsequences.hpp"
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
glm::dquat face(glm::dvec3 direction) {
    return glm::quatLookAtRH(glm::normalize(direction), glm::dvec3(0, 1, 0));
}
} // namespace
int main() {
    try {
        Universe u;
        const auto home = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0);
        const auto planet = u.describe(home);
        const auto fingerprint = descriptorFingerprint(planet);
        glm::dvec3 entry{};
        for (unsigned n = 0; n < 96; ++n) {
            const auto d = glm::normalize(glm::dvec3(std::sin(n * 2.4) + .2, .7, std::cos(n * 2.4)));
            const double h = u.terrainHeight(planet, d), r = std::pow(10., planet.frame.logRadius);
            if (planet.geology->liquidWater <= .1 || h / r >= -.0008) {
                entry = d * (r + h);
                break;
            }
        }
        const auto up = glm::normalize(entry);
        WeaponRay ray;
        ray.reference = home;
        ray.origin = entry + up * 500.;
        ray.direction = -up;
        ray.seconds = 100;
        const auto planned = planMiningProbe(u, home, ray, 0, 620062);
        check(bool(planned.hole), "No natural excavation fixture");
        auto b = *planned.hole;
        const auto geometry = geometryFingerprint(*boreGeometry(u, b, true));
        const auto pool = home.child(Kind::Surface, encodeTile(tileAt(up, 7)));
        const auto capacity = prospectResources(u, pool);
        const auto faces = cavernFinds(b);
        const auto f = *std::max_element(faces.begin(), faces.end(), [&](const auto& a, const auto& b) {
            return capacity[a.resource] < capacity[b.resource];
        });
        const auto profile = excavationProfile(b, f);
        check(profile.weakness > .1 && profile.weakness < 1, "Structural profile is unbounded");
        auto weak = b;
        weak.geology.fractureStrength = 1;
        weak.geology.sediment = 1;
        auto strong = b;
        strong.geology.fractureStrength = 0;
        strong.geology.sediment = 0;
        check(excavationProfile(weak, f).weakness > excavationProfile(strong, f).weakness,
              "Inherited joints and sediment do not affect stability");
        check(excavationCutDamage(b, f, true) < excavationCutDamage(b, f, false) * .3,
              "Careful feed does not reduce damage");
        auto braced = b;
        braced.excavation[f.index].supports = 2;
        check(excavationCutDamage(braced, f, false) < excavationCutDamage(b, f, false) * .3,
              "Braces do not carry the load");
        for (auto mineral :
             {Mineral::Ice, Mineral::Quartz, Mineral::Iron, Mineral::Graphite, Mineral::Olivine}) {
            auto test = f;
            test.mineral = mineral;
            auto bore = b;
            unsigned cuts = 0;
            while (!advanceExcavation(bore, test, false, b.blastTime() + 10 + cuts * 10) && cuts < 20)
                ++cuts;
            check(cuts >= 4 && cuts < 20, "A material fails without several cuts of warning, or never fails");
            check(bore.excavation[f.index].collapses == 1 && bore.excavation[f.index].damage == .30,
                  "Collapse is unbounded or heals all damage");
        }
        auto network = std::make_shared<EconomicNetwork>();
        network->system = home.parent();
        network->universeSeed = u.seed();
        network->fingerprint = 620062;
        EconomicSite port;
        port.id = {home, 0};
        port.orbital = true;
        port.metres = entry + up * 30000.;
        port.radius = 100;
        port.capacityKg = 2000000;
        port.initial.fill(10000);
        network->sites.push_back(port);
        Economy economy(network);
        EconomyPlayer player;
        ExplorationLedger ledger;
        SurveyObserver observer;
        observer.reference = home;
        observer.position = port.metres;
        observer.seconds = 100;
        uint64_t sequence = 0;
        unsigned engineer = 100;
        bool careful = false;
        auto order = [&](EconomicAction action, double work = 8) {
            EconomicCommand c;
            c.sequence = ++sequence;
            c.action = action;
            c.contract = b.id;
            c.cavernFind = f.index;
            c.weaponMount =
                action == EconomicAction::FitProbeLauncher || action == EconomicAction::LaunchProbe ? 0 : 1;
            c.port = port.id;
            c.observer = observer;
            c.workSeconds = work;
            c.serviceUnits = 100;
            c.engineering = engineer;
            c.carefulCut = careful;
            c.combatPresent = true;
            c.limitCents = 100000000;
            c.surveyTarget = home;
            c.probeRay = ray;
            return ledger.execute(u, economy, player, c);
        };
        check(order(EconomicAction::FitProbeLauncher).starts_with("PROBE LAUNCHER FITTED"), "No launcher");
        check(order(EconomicAction::FitMiningHead).starts_with("MINING HEAD FITTED"), "No mining head");
        observer.position = ray.origin;
        check(order(EconomicAction::LaunchProbe).starts_with("PROBE LAUNCHED"), "No saved borehole");
        // The ledger assigns command identity, so use its actual generated bore/face.
        b = ledger.state().probes.holes.back();
        const auto actual = cavernFinds(b)[f.index];
        observer.seconds = b.blastTime() + 10;
        const auto point = cavernFindPoint(b, actual);
        observer.position = entry - up * b.finalDepth();
        observer.attitude = face(point - observer.position);
        check(order(EconomicAction::ScanCavern).starts_with("CAVERN OVERVIEW"), "No overview");
        observer.size = .0005;
        observer.position = point + tangentFrame(up) * actual.inward * .5;
        observer.attitude = face(point - observer.position);
        check(order(EconomicAction::ScanCavern).starts_with("CLOSE ASSAY"), "No close assay");
        check(order(EconomicAction::MarkCavern).starts_with("CAVERN MATERIAL MARKED"), "No material mark");
        check(!order(EconomicAction::BraceCavern).starts_with("ENGINEER BRACED"),
              "Tiny ship placed a full-size support");
        observer.size = 1;
        observer.position = entry - up * b.finalDepth();
        observer.attitude = face(point - observer.position);
        const auto freshGeometry = geometryFingerprint(*boreGeometry(u, b, true));
        const auto key = probeGeometryKey(b);
        check(!order(EconomicAction::BraceCavern).starts_with("ENGINEER BRACED"),
              "Free support without materials");
        player.cargo[size_t(Good::Alloy)] = 8;
        player.cargo[size_t(Good::Tools)] = 4;
        engineer = 0;
        check(!order(EconomicAction::BraceCavern).starts_with("ENGINEER BRACED"),
              "Unstaffed engineer placed a brace");
        engineer = 100;
        check(order(EconomicAction::BraceCavern).starts_with("ENGINEER BRACED"),
              "Ready engineer could not brace the face");
        check(player.availableCargo(Good::Alloy) == 6 && player.availableCargo(Good::Tools) == 3,
              "Brace materials were not consumed exactly");
        const auto braceState = ledger.serialize();
        check(ledger.execute(u, economy, player,
                             EconomicCommand{.sequence = sequence, .action = EconomicAction::BraceCavern}) ==
                      "ORDER ALREADY PROCESSED" && ledger.serialize() == braceState,
              "Replayed brace changed saved state");
        const auto reserve = [&]() {
            return ledger.state().prospects.empty() ? int64_t(0)
                                                    : ledger.state().prospects[0].extracted[actual.resource];
        };
        bool collapsed = false;
        unsigned cuts = 0;
        while (cuts++ < 50 && !collapsed) {
            observer.seconds += 10;
            const auto result = order(EconomicAction::ExtractCavern, 6);
            collapsed = result.starts_with("LOCAL COLLAPSE");
            check(collapsed || result.starts_with("CAVERN EXTRACTED"), result.c_str());
        }
        check(collapsed, "Actual finite reserve exhausted before its first braced collapse");
        const auto& fallen = ledger.state().probes.holes.back();
        const auto& damage = fallen.excavation[f.index];
        check(damage.looseKg > 0 && damage.buriedKits == 1 && damage.supports == 0,
              "Failure did not detach finite ore and bury the deployed kit");
        check(reserve() == int64_t(ledger.state().minedKg + damage.looseKg),
              "Detached ore was duplicated or counted as collected cargo");
        check(!cavernTailings(fallen, cavernWear(u, fallen, ledger.state())).vertices.empty(),
              "Collected-cut wear disappeared");
        check(cavernRubbleClearance(fallen, cavernRubblePoint(fallen, actual)) < 0,
              "Recoverable ore has no physical collision");
        check(boreClearance(fallen, entry - up * fallen.finalDepth(), observer.seconds) > 2.5,
              "Collapse blocked the chamber centre or escape route");
        check(geometryFingerprint(*boreGeometry(u, fallen, true)) == freshGeometry &&
                  probeGeometryKey(fallen) == key,
              "Player damage regenerated the canonical wall or crystals");
        check(!excavationFixtures(fallen).vertices.empty(), "Loose material has no visible geometry");
        check(excavationMotes(fallen, actual, .3, observer.seconds + .6).count > 0,
              "Collapse has no falling material");
        check(excavationMotes(fallen, actual, .3, observer.seconds + 3).count == 0,
              "Fall particles never expire");
        const auto saved = ledger.serialize();
        ExplorationLedger reloaded;
        check(reloaded.restore(saved, u) && reloaded.serialize() == saved,
              "Damage/support/loose ore did not survive reload exactly");
        check(!order(EconomicAction::RecoverCavern, 6).starts_with("CAVERN RECOVERED"),
              "Recovered material during active rockfall");
        observer.seconds += 3;
        const auto holdCapacity=player.capacityKg;
        player.capacityKg=player.massKg();
        const auto retained=ledger.state().probes.holes.back().excavation[f.index].looseKg;
        check(!order(EconomicAction::RecoverCavern,6).starts_with("CAVERN RECOVERED")&&
              ledger.state().probes.holes.back().excavation[f.index].looseKg==retained,
              "Full hold consumed or lost detached ore");
        player.capacityKg=holdCapacity;
        const auto cargoBefore = player.cargo;
        const auto reserveBefore = reserve();
        check(!order(EconomicAction::RecoverCavern, 1).starts_with("CAVERN RECOVERED"),
              "Instant recovery skipped work");
        const auto recovered = order(EconomicAction::RecoverCavern, 6);
        check(recovered.starts_with("CAVERN RECOVERED"), recovered.c_str());
        check(reserve() == reserveBefore && ledger.state().minedKg == uint64_t(reserve()),
              "Recovery debited the reserve twice");
        check(player.cargo[size_t(Good::Tools)] == cargoBefore[size_t(Good::Tools)] + 1,
              "Buried kit was not recovered exactly once");
        const auto cargo = player.cargo;
        check(!order(EconomicAction::RecoverCavern, 6).starts_with("CAVERN RECOVERED") &&
                  player.cargo == cargo,
              "Ore or kit respawned after recovery");
        check(reloaded.restore(ledger.serialize(), u), "Recovered expedition cannot reload");
        // Legacy version 2 has exactly the old fields, with no invented damage/supports.
        MiningProbeState old;
        old.holes.push_back(b);
        auto current = serializeProbes(old);
        std::istringstream lines(current);
        std::ostringstream legacy;
        std::string line;
        unsigned row = 0;
        while (std::getline(lines, line)) {
            if (row == 0)
                line.replace(line.find(" 3 "), 3, " 2 ");
            if (row < 5)
                legacy << line << '\n';
            ++row;
        }
        MiningProbeState migrated;
        check(restoreProbes(migrated, legacy.str(), u), "Pre-stability probe save no longer loads");
        check(migrated.holes[0].excavation[0].damage == 0 && migrated.holes[0].excavation[0].looseKg == 0,
              "Old save invented excavation damage");
        auto bad = old;
        bad.holes[0].excavation[0].damage = 1;
        check(!restoreProbes(migrated, serializeProbes(bad), u), "Corrupt damage accepted");
        check(descriptorFingerprint(u.describe(home)) == fingerprint &&
                  geometryFingerprint(*boreGeometry(u, *planned.hole, true)) == geometry,
              "Generation changed after excavation");
        std::cout << "PASS: " << checks
                  << " geology-driven damage, braces, finite clasts, physical rubble, crew recovery, replay "
                     "and migration checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
