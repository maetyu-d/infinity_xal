#define GLM_FORCE_INLINE
#include "infinity/Crew.hpp"
namespace infinity {
std::string ShipCrew::powerPreset(PowerPreset preset) {
    if (unsigned(preset) > 3)
        return "UNKNOWN POWER PRESET";
    plant_.preset = preset;
    plant_.refresh();
    return "POWER PRIORITY / " + std::string(powerPresetName(preset));
}
std::string ShipCrew::flushCooling() {
    return plant_.flush() ? "EMERGENCY COOLING / 2 KG WATER VENTED"
                          : "COOLING FLUSH / NEEDS HEAT, 2 KG COOLANT AND 10S RESET";
}
std::string ShipCrew::isolate(CrewStation room, bool closed) {
    if (size_t(room) >= crewRoomCount)
        return "UNKNOWN COMPARTMENT";
    uint16_t mask = 0;
    const auto edges = crewCorridors();
    for (size_t i = 0; i < edges.size(); ++i) {
        const auto [a, b] = edges[i];
        if (a != unsigned(room) && b != unsigned(room))
            continue;
        if (closed)
            for (const auto& m : members_)
                if (m.travel > 0 && ((unsigned(m.room) == a && unsigned(m.walkTo) == b) ||
                                     (unsigned(m.room) == b && unsigned(m.walkTo) == a)))
                    return "HATCH OCCUPIED / LET CREW CLEAR THE CORRIDOR";
        mask |= uint16_t(1u << i);
    }
    if (closed)
        closedDoors_ |= mask;
    else
        closedDoors_ &= uint16_t(~mask);
    return std::string(crewStationName(room)) +
           (closed ? " / BULKHEADS SEALED / CREW ROUTES UPDATED" : " / BULKHEADS OPEN / ACCESS RESTORED");
}
} // namespace infinity
