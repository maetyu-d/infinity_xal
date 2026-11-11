#define GLM_FORCE_INLINE
#include "infinity/CrewMinimap.hpp"
#include "infinity/Economy.hpp"
#include <iostream>

using namespace infinity;
namespace {
unsigned checks = 0;
void check(bool yes, const char* why) {
    ++checks;
    if (!yes)
        throw std::runtime_error(why);
}
bool has(const CockpitOverlay& h, std::string_view value) {
    return std::any_of(h.textRuns.begin(), h.textRuns.end(),
                       [&](const auto& r) { return r.value.starts_with(value); });
}
bool legendGlyph(const CockpitOverlay& h,char glyph) {
    for(const auto& run:h.textRuns)if(run.value.starts_with("> MOVING")) {
        const auto position=run.value.find(glyph);if(position==std::string::npos)return false;
        const double pixel=run.bounds.h/7.,start=run.bounds.x+position*pixel*6;
        for(size_t i=run.firstVertex;i<run.firstVertex+run.vertexCount;++i) {
            const auto& v=h.lettering.vertices[i];
            if(v.x>start+.01&&v.x<start+pixel*5-.01)return true;
        }
    }
    return false;
}
void fits(const CockpitOverlay& h, double aspect) {
    const auto conflicts = cockpitTextConflicts(h);
    if (!conflicts.empty()) {
        const auto [a, b] = conflicts.front();
        std::cerr << h.textRuns[a].value << " OVERLAPS " << h.textRuns[b].value << '\n';
    }
    check(conflicts.empty(), "Crew minimap overlaps cockpit text");
    for (const auto& r : h.textRuns)
        check(r.bounds.x >= 0 && r.bounds.y >= 0 && r.bounds.x + r.bounds.w <= 900 * aspect + .01 &&
                  r.bounds.y + r.bounds.h <= 900.01,
              "Crew/map text outside the fitted canvas");
}
} // namespace
int main() {
    try {
        ShipCrew ship(628);
        const auto saved = ship.serialize();
        auto view = ship.view();
        FlightInstruments t;
        t.phase = "PLANETARY FLIGHT";
        t.hull = 100;
        check(crewMinimapVisible(t, &view) && !crewMinimapVisible(t, nullptr),
              "Wrong default map visibility");
        for (double aspect : {1., 1.2, 4. / 3, 16. / 9, 2.4}) {
            auto map = crewMinimap(view, aspect);
            check(map.panel.x >= 0 && map.panel.x + map.panel.w <= 900 * aspect &&
                      map.panel.y + map.panel.h < 758,
                  "Minimap outside screen or overlaps instruments");
            check(map.count == ship.members().size(), "Missing crew markers");
            for (size_t i = 0; i < map.count; ++i) {
                const auto& dot = map.markers[i];
                check(map.rooms[size_t(dot.room)].contains(dot.position),
                      "Stationary crew not in the actual room");
                check(dot.id == ship.members()[i].id && dot.roster == i, "Roster or crew identity changed");
                check(map.pickCrew(dot.label) == dot.id, "Marker selects the wrong person");
                check(!dot.moving, "Stationary crew show a fabricated journey");
            }
            check(!map.pickCrew({-1, -1}) && !map.pickRoom({-1, -1}), "Map accepts outside clicks");
            const auto overlay =
                cockpitOverlay(t, aspect, "HYPERION", {}, {}, {}, false, 1, nullptr, {1, 0, 0, 0}, &view);
            check(has(overlay, "CREW DECK"), "Crew minimap missing from normal flight");
            check(legendGlyph(overlay,'>')&&legendGlyph(overlay,'!'),"Movement/injury legend has invisible font glyphs");
            fits(overlay, aspect);
            check(std::any_of(overlay.hitRegions.begin(), overlay.hitRegions.end(),
                              [&](const auto& r) {
                                  return r.x == map.panel.x && r.y == map.panel.y && r.w == map.panel.w;
                              }),
                  "Crew map allows world clicks or markers through its card");
            for (bool console : {false, true})
                for (unsigned situation = 0; situation < 6; ++situation) {
                    auto crew = view;
                    crew.flightOpen = console;
                    auto flight = t;
                    if (situation == 0) {
                        flight.phase = "MANUAL WARP";
                        flight.warp = WarpPhase::Manual;
                    }
                    if (situation == 1) {
                        flight.phase = "CAVERN EXPLORATION";
                        flight.altitude = 3;
                        flight.explorationActive = true;
                        flight.exploration = {"MARKED QUARTZ", "ASSAY 2 / 20 KG", "MINING FACE",
                                              "CTRL-SPACE MINE"};
                        flight.weather = "JOINTED ROCK / LOAD 96%";
                        flight.weatherDetail = "BRACES 1 / LOOSE 20 KG";
                    }
                    if (situation == 2) {
                        flight.shrinking = true;
                        flight.hullMetres = .00001;
                        flight.phase = "SURFACE FLIGHT";
                        flight.altitude = .000001;
                    }
                    if (situation == 3) {
                        flight.stationStatus = "CAPTAIN AT PORT GUN / RELIEF HELM";
                        flight.playerGun = 1;
                        flight.weapon = "PULSE LASER I";
                    }
                    if (situation == 4) {
                        flight.hull = 12;
                        crew.rooms[3].fire = 70;
                        crew.rooms[4].oxygen = 10;
                        crew.summary = "CREW EMERGENCY";
                        crew.alert = "ENGINEERING FIRE";
                    }
                    if (situation == 5) {
                        flight.landed = true;
                        flight.altitude = 0;
                        flight.phase = "LANDED";
                    }
                    fits(cockpitOverlay(flight, aspect, "HYPERION", "ORDER RECEIVED", {}, {}, false, 1,
                                        nullptr, {1, 0, 0, 0}, &crew),
                         aspect);
                }
        }
        // Every movement sample is the actual current edge, not a shortcut to the final target.
        auto moving = view;
        auto& person = moving.members[1];
        person.room = CrewStation::Navigation;
        person.walkTo = CrewStation::Engineering;
        person.target = CrewStation::Bunks;
        person.travelDuration = 10;
        person.travel = 10;
        const auto start = crewMinimap(moving, 16. / 9).markers[1].position;
        person.travel = 0;
        person.room = CrewStation::Engineering;
        const auto end = crewMinimap(moving, 16. / 9).markers[1].position;
        person.room = CrewStation::Navigation;
        for (double elapsed : {0., 2.5, 5., 7.5, 9.999}) {
            person.travel = 10 - elapsed;
            const auto map = crewMinimap(moving, 16. / 9);
            check(glm::length(map.markers[1].position - glm::mix(start, end, elapsed / 10)) < 1e-9,
                  "Movement disagrees with the crew solver's current corridor leg");
            check(map.markers[1].next == CrewStation::Engineering && map.markers[1].moving,
                  "Final target replaced the actual walking destination");
            check(map.pickCrew(map.markers[1].label) == person.id, "Moving marker lost selection identity");
        }
        // A crowded compartment and intersecting journeys retain eight readable identities.
        auto crowded = view;
        crowded.members.resize(maxShipCrew);
        for (size_t i = 0; i < maxShipCrew; ++i) {
            auto& m = crowded.members[i];
            m.id = 100 + i;
            m.room = m.walkTo = CrewStation::Engineering;
            m.travel = 0;
            m.health = 100;
            m.name = "CREW " + std::to_string(i + 1);
        }
        for (unsigned phase = 0; phase < 20; ++phase) {
            for (size_t i = 0; i < maxShipCrew; ++i) {
                auto& m = crowded.members[i];
                if (phase) {
                    m.room = i % 2 ? CrewStation::Navigation : CrewStation::Weapons;
                    m.walkTo = i % 2 ? CrewStation::Weapons : CrewStation::Navigation;
                    m.travelDuration = 10;
                    m.travel = 10 - phase * .5;
                }
            }
            const auto map = crewMinimap(crowded, 1.);
            for (size_t i = 0; i < map.count; ++i) {
                check(map.pickCrew(map.markers[i].label) == map.markers[i].id,
                      "Crowded crew cannot be picked individually");
                for (size_t j = 0; j < i; ++j)
                    check(
                        !CockpitRect{map.markers[i].label.x - 7, map.markers[i].label.y - 4, 14, 9}.overlaps(
                            {map.markers[j].label.x - 7, map.markers[j].label.y - 4, 14, 9}),
                        "Crew numbers overlap at a corridor crossing");
            }
            fits(cockpitOverlay(t, 1., "HYPERION", {}, {}, {}, false, 1, nullptr, {1, 0, 0, 0}, &crowded),
                 1.);
        }
        crowded.members[0].health = 0;
        crowded.members[1].health = 10;
        crowded.members[2].health = 50;
        const auto injured = crewMinimap(crowded, 1.);
        check(injured.markers[0].condition == CrewMapCondition::Lost && !injured.markers[0].moving &&
                  injured.markers[1].condition == CrewMapCondition::Incapacitated &&
                  injured.markers[2].condition == CrewMapCondition::Injured,
              "Crew health/loss is not represented accurately");
        for (const auto phase : {"MINING PROBE CAMERA", "SPECIMEN INSPECTION"}) {
            t.phase = phase;
            check(!crewMinimapVisible(t, &view) && !has(cockpitOverlay(t, 1., "HYPERION", {}, {}, {}, false,
                                                                       1, nullptr, {1, 0, 0, 0}, &view),
                                                        "CREW DECK"),
                  "Remote feed contains the ship's map");
        }
        t.phase = "PLANETARY FLIGHT";
        t.contactsExpanded = true;
        check(!has(cockpitOverlay(t, 1., "HYPERION", {}, {}, {}, false, 1, nullptr, {1, 0, 0, 0}, &view),
                   "CREW DECK"),
              "Expanded contacts collide with the minimap");
        t.contactsExpanded = false;
        view.open = true;
        check(!crewMinimapVisible(t, &view), "Full deck view duplicates the minimap");
        view.open = false;
        check(!crewMinimapVisible(t, &view, true), "Modal/map visibility guard ignored");
        MarketView market;
        market.open = true;
        check(!has(cockpitOverlay(t, 1., "HYPERION", {}, {}, {}, false, 1, &market, {1, 0, 0, 0}, &view),
                   "CREW DECK"),
              "Market covers a live minimap");
        check(!has(cockpitOverlay(t, 1., "HYPERION", {}, {}, {}, true, 1, nullptr, {1, 0, 0, 0}, &view),
                   "CREW DECK"),
              "Help covers a live minimap");
        check(ship.serialize() == saved, "Drawing the minimap changed crew simulation or persistence");
        check(crewRoomCode(CrewStation::Engineering) == "ENG" && crewRoomCode(CrewStation::Bunks) == "BUNK",
              "Room codes do not identify existing stations");
        std::cout << "PASS: " << checks
                  << " crew locations, interpolation, stable selection, crowds, hazards, modes and fitted "
                     "minimap checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
