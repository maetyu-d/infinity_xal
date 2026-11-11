#define GLM_FORCE_INLINE
#include "infinity/CrewMinimap.hpp"
#include "infinity/Navigation.hpp"
#include "infinity/Streaming.hpp"
#include <iostream>
using namespace infinity;
namespace {
unsigned checks = 0;
void check(bool x, const char* why) {
    ++checks;
    if (!x)
        throw std::runtime_error(why);
}
void run(ShipPlant& plant, const PlantDemand& demand, double seconds) {
    while (seconds > 0) {
        const double dt = std::min(.25, seconds);
        plant.advance(demand, dt);
        seconds -= dt;
    }
}
// Isolated damage fixtures, without exposing mutation APIs in the game.
auto& rooms(ShipCrew& c) {
    return const_cast<std::array<CrewRoom, crewRoomCount>&>(c.rooms());
}
} // namespace
int main() {
    try {
        ShipPlant normal;
        PlantDemand flight;
        flight.throttle = 1;
        flight.drive = 1;
        run(normal, flight, 1200);
        check(normal.heat < .1 && normal.authority(PlantBus::Helm) == 1 &&
                  normal.authority(PlantBus::Drive) == 1,
              "Healthy travel needs constant power management or changes helm authority");
        check(normal.stores[0] > 100 && normal.stores[0] < 120 && normal.stores[1] == 40 &&
                  normal.stores[2] == 30,
              "Healthy endurance is too short or closed-loop fluids disappear");
        PlantDemand contested;
        contested.reactor = .1;
        contested.weapons = 1;
        contested.drill = 1;
        contested.drive = 1;
        ShipPlant combat, travel, work;
        combat.preset = PowerPreset::Combat;
        travel.preset = PowerPreset::Flight;
        work.preset = PowerPreset::Industrial;
        for (auto* p : {&combat, &travel, &work}) {
            p->reserve = .25;
            p->advance(contested, .25);
            check(p->readout().brownout, "Damaged plant creates free power");
            check(p->authority(PlantBus::Life) == 1 && p->authority(PlantBus::Helm) == 1,
                  "Load shedding sacrifices life support or helm before optional systems");
        }
        check(combat.authority(PlantBus::Weapons) > travel.authority(PlantBus::Weapons) &&
                  travel.authority(PlantBus::Drive) > combat.authority(PlantBus::Drive) &&
                  work.authority(PlantBus::Industry) > combat.authority(PlantBus::Industry),
              "Presets do not change actual allocations");
        ShipPlant hot;
        PlantDemand mine;
        mine.drill = 1;
        mine.cooling = .1;
        mine.engineer = 0;
        run(hot, mine, 120);
        check(hot.heat > .72 && hot.readout().warningSeconds > 0,
              "Cooling damage gives no warning before the thermal limit");
        const auto fluid = hot.stores[1], heat = hot.heat;
        check(hot.flush() && hot.stores[1] == fluid - 2 && hot.heat < heat - .24,
              "Flush does not consume finite coolant or relieve heat");
        check(!hot.flush(), "Repeated flush bypasses reset or creates cooling");
        run(hot, mine, 10);
        check(hot.flush(), "Flush never becomes available again");
        hot.stores[1] = 1;
        check(!hot.flush(), "Emergency cooling invents water");
        ShipPlant powerless;
        powerless.stores[0] = 0;
        powerless.reserve = .01;
        run(powerless, contested, 30);
        check(powerless.reserve >= 0 && powerless.stores[0] == 0 &&
                  powerless.authority(PlantBus::Weapons) == 0,
              "Emergency battery is infinite or negative");
        ShipPlant a, b;
        run(a, mine, 20);
        for (unsigned i = 0; i < 20; ++i)
            b.advance(mine, 1);
        check(a.serialize() == b.serialize(), "Plant fixed-step histories disagree");
        const auto saved = hot.serialize();
        ShipPlant loaded;
        check(loaded.restore(saved) && loaded.serialize() == saved,
              "Heat, tanks or priority lost on restart");
        check(!loaded.restore("PLANT 1 4 0 1 0 120 40 30 0 0 0") && loaded.serialize() == saved,
              "Invalid plant mode corrupts the previous state");
        ShipPlant receipt;
        receipt.stores[0] = 0;
        EngineeringReceipts supply;
        supply.kg[0] = 20;
        receipt.reconcile(supply);
        check(receipt.stores[0] == 20, "Cargo receipt did not fill the tank");
        receipt.reconcile(supply);
        check(receipt.stores[0] == 20, "Cargo receipt applied twice");
        ShipCrew crew(77);
        const auto before = crew.serialize();
        crew.toggleAutomatic();rooms(crew)[0].integrity=80;
        check(crew.isolate(CrewStation::Bridge, true).find("SEALED") != std::string::npos,
              "Cannot seal bulkheads");
        crew.order(1, CrewOrder::Repair, CrewStation::Bridge);
        check(crew.task(1).blocked && crew.task(1).status == "ROUTE BLOCKED",
              "Crew pathfinding ignores closed bulkheads");
        crew.advance(5);
        check(crew.members()[1].room == CrewStation::Navigation && crew.members()[1].travel == 0,
              "Crew walks through a sealed hatch");
        crew.isolate(CrewStation::Bridge, false);
        bool arrived=false;
        for (unsigned i = 0; i < 80; ++i){crew.advance(.25);arrived|=crew.members()[1].room==CrewStation::Bridge;}
        check(arrived, "Crew cannot resume through an opened route");
        ShipCrew moving(77);
        moving.order(1, CrewOrder::Rest, CrewStation::Bunks);
        moving.advance(.25);
        const auto person = moving.members()[1];
        check(person.travel > 0, "No actual corridor journey in fixture");
        check(moving.isolate(person.room, true).starts_with("HATCH OCCUPIED") && moving.closedDoors() == 0,
              "Closing a hatch teleports or traps a moving crew marker");
        ShipCrew open(78), sealed(78);
        open.toggleAutomatic();
        sealed.toggleAutomatic();
        rooms(open)[2].vented = true;
        rooms(sealed)[2].vented = true;
        sealed.isolate(CrewStation::Weapons, true);
        for (unsigned i = 0; i < 160; ++i) {
            open.advance(.25);
            sealed.advance(.25);
        }
        check(open.rooms()[1].oxygen < sealed.rooms()[1].oxygen && sealed.rooms()[1].oxygen > 99,
              "Bulkheads do not contain pressure loss");
        check(open.plant().stores[2] < 30 && sealed.plant().stores[2] < 30,
              "Breaches or vents do not use the air reserve");
        ShipCrew fire(79), contained(79);
        fire.toggleAutomatic();
        contained.toggleAutomatic();
        rooms(fire)[2].fire = 90;
        rooms(contained)[2].fire = 90;
        contained.isolate(CrewStation::Weapons, true);
        for (unsigned i = 0; i < 120; ++i) {
            fire.advance(.25);
            contained.advance(.25);
        }
        check(fire.rooms()[1].fire > 0 && contained.rooms()[1].fire == 0,
              "Fire spread ignores the shared corridor doors");
        ShipCrew restored(77);
        check(restored.restore(crew.serialize()) && restored.serialize() == crew.serialize(),
              "Plant/doors/crew save is not exact");
        auto legacy = before;
        legacy.replace(legacy.find("CREW 5"), 6, "CREW 4");
        legacy.resize(legacy.rfind(" \"PLANT"));
        check(restored.restore(legacy) && restored.closedDoors() == 0 &&
                  restored.plant().stores == engineeringCapacity,
              "Existing crew saves do not migrate safely");
        ShipCrew rateA(80), rateB(80);
        rateA.setPlantDemand(mine);
        rateB.setPlantDemand(mine);
        for (unsigned i = 0; i < 100; ++i)
            rateA.advance(.25);
        for (unsigned i = 0; i < 500; ++i)
            rateB.advance(.05);
        check(rateA.serialize() == rateB.serialize(), "Ship engineering depends on rendering frame rate");
        Universe u;
        RegionCache cache(u);
        Navigation navA(u), navB(u);
        FlightController fA(u, navA, cache), fB(u, navB, cache);
        Pilot pilotA(u, navA, fA), pilotB(u, navB, fB);
        pilotA.startInSystem();
        pilotB.startInSystem();
        pilotB.setSystemAuthority(normal.authority(PlantBus::Helm), normal.authority(PlantBus::Helm));
        HelmInput input;
        input.throttle = .3;
        input.roll = .05;
        for (unsigned i = 0; i < 30; ++i) {
            pilotA.update(input, .1);
            pilotB.update(input, .1);
        }
        check(pilotA.serialize() == pilotB.serialize() && navA.serialize() == navB.serialize(),
              "Healthy power integration changes existing flight handling or roll");
        for (double aspect : {1., 4. / 3, 16. / 9, 2.4}) {
            auto view = crew.view();
            view.flightOpen = true;
            const EngineeringLayout panel(aspect);
            const auto map = crewMinimap(view, aspect);
            check(panel.panel.y >= map.panel.y + map.panel.h && panel.panel.y + panel.panel.h < 603,
                  "Engineering panel overlaps crew map or weapon station");
            const auto hud = cockpitOverlay(pilotA.instruments(), aspect, "HYPERION", {}, {}, {}, false, 1,
                                            nullptr, {1, 0, 0, 0}, &view);
            const auto conflicts = cockpitTextConflicts(hud);
            if (!conflicts.empty())
                std::cerr << hud.textRuns[conflicts[0].first].value << " / "
                          << hud.textRuns[conflicts[0].second].value << '\n';
            check(conflicts.empty(), "Engineering controls overlap cockpit text");
        }
        std::cout << "PASS: " << checks
                  << " shared power, thermal warnings, finite fluids, receipts, bulkheads, fire/pressure, "
                     "persistence and unchanged helm checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
