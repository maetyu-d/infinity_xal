#pragma once
#include "Cockpit.hpp"

namespace infinity {
enum class CrewMapCondition : uint8_t { Ready, Injured, Incapacitated, Lost };
struct CrewMapMarker {
    uint64_t id = 0;
    size_t roster = 0;
    CrewStation room = CrewStation::Bridge, next = CrewStation::Bridge;
    glm::dvec2 position{}, destination{}, label{};
    CrewMapCondition condition = CrewMapCondition::Ready;
    bool moving = false, resting = false;
};
// A read-only schematic of the existing room graph and current journey leg.
// Roster numbers and stable IDs refer to the same people as F8/F9.
struct CrewMinimap {
    CockpitRect panel;
    std::array<CockpitRect, crewRoomCount> rooms;
    std::array<CrewMapMarker, maxShipCrew> markers{};
    size_t count = 0;
    std::optional<uint64_t> pickCrew(glm::dvec2) const;
    std::optional<CrewStation> pickRoom(glm::dvec2) const;
};
CrewMinimap crewMinimap(const CrewView&, double aspect);
bool crewMinimapVisible(const FlightInstruments&, const CrewView*, bool obscured = false);
std::string_view crewRoomCode(CrewStation);
struct EngineeringLayout {
    CockpitRect panel;
    std::array<CockpitRect,4> presets;
    std::array<CockpitRect,4> actions; // isolate, open, cooling flush, cargo resupply
    explicit EngineeringLayout(double aspect);
};
} // namespace infinity
