#define GLM_FORCE_INLINE
#include "infinity/CrewMinimap.hpp"
#include <algorithm>
#include <cmath>

namespace infinity {
EngineeringLayout::EngineeringLayout(double aspect) {
    panel={900*std::max(1.,aspect)-310,496,286,100};
    for(size_t i=0;i<4;++i){presets[i]={panel.x+12+i*66,553,62,16};actions[i]={panel.x+12+i*66,575,62,15};}
}
std::string_view crewRoomCode(CrewStation station) {
    constexpr std::array<std::string_view, crewRoomCount> names{"BRG",  "NAV", "GUN", "ENG",
                                                                "LIFE", "MED", "BUNK"};
    return size_t(station) < names.size() ? names[size_t(station)] : "?";
}
bool crewMinimapVisible(const FlightInstruments& t, const CrewView* crew, bool obscured) {
    return crew && !crew->open && !obscured && !t.contactsExpanded && t.phase != "MINING PROBE CAMERA" &&
           t.phase != "SPECIMEN INSPECTION";
}
CrewMinimap crewMinimap(const CrewView& crew, double aspect) {
    CrewMinimap map;
    map.panel = {900 * std::max(1., aspect) - 310, 250, 286, 238};
    const auto centre = glm::dvec2(map.panel.x + map.panel.w * .5, map.panel.y + 112);
    const auto roomCentre = [&](CrewStation room) {
        return centre + crewRoomPosition(room) * glm::dvec2(40, 15);
    };
    for (size_t i = 0; i < crewRoomCount; ++i) {
        const auto p = roomCentre(CrewStation(i));
        map.rooms[i] = {p.x - 37, p.y - 16, 74, 34};
    }
    map.count = std::min(crew.members.size(), maxShipCrew);
    for (size_t i = 0; i < map.count; ++i) {
        const auto& m = crew.members[i];
        auto& dot = map.markers[i];
        dot.id = m.id;
        dot.roster = i;
        dot.room = size_t(m.room) < crewRoomCount ? m.room : CrewStation::Bridge;
        dot.next = size_t(m.walkTo) < crewRoomCount ? m.walkTo : dot.room;
        double blend = 0;
        if (std::isfinite(m.travel) && std::isfinite(m.travelDuration) && m.travel > 0 &&
            m.travelDuration > 0)
            blend = std::clamp(1 - m.travel / m.travelDuration, 0., 1.);
        else
            dot.next = dot.room;
        // Fixed roster lanes keep up to eight people in one compartment legible
        // and avoid shuffling stationary markers whenever a colleague leaves.
        const glm::dvec2 offset((double(i % 4) - 1.5) * 15, 3 + double(i / 4) * 11);
        dot.position = glm::mix(roomCentre(dot.room), roomCentre(dot.next), blend) + offset;
        dot.destination = roomCentre(dot.next) + offset;
        dot.label = dot.position;
        dot.moving = m.health > 0 && m.travel > 0 && m.travelDuration > 0 && dot.room != dot.next;
        dot.condition = m.health <= 0   ? CrewMapCondition::Lost
                        : m.health < 20 ? CrewMapCondition::Incapacitated
                        : m.health < 80 ? CrewMapCondition::Injured
                                        : CrewMapCondition::Ready;
        dot.resting = m.order == CrewOrder::Rest && !dot.moving && m.room == CrewStation::Bunks;
        if(i<crew.tasks.size()&&crew.tasks[i].status=="AWAY PARTY") {
            dot.position=dot.destination=dot.label={map.panel.x+267,map.panel.y+183};
            dot.moving=dot.resting=false;
        }
        // Separate numbered labels at corridor crossings, leaving the true
        // marker position and movement untouched. A short leader connects them.
        bool placed = false;
        for (int ring = 0; ring <= 4 && !placed; ++ring)
            for (int y = -ring; y <= ring && !placed; ++y)
                for (int x = -ring; x <= ring && !placed; ++x) {
                    if (std::max(std::abs(x), std::abs(y)) != ring)
                        continue;
                    const auto candidate = dot.position + glm::dvec2(x * 15, y * 11);
                    const CockpitRect label{candidate.x - 7, candidate.y - 4, 14, 9};
                    if (label.x < map.panel.x + 8 || label.x + label.w > map.panel.x + map.panel.w - 8 ||
                        label.y < map.panel.y + 30 || label.y + label.h > map.panel.y + 195)
                        continue;
                    bool clear = true;
                    for (size_t j = 0; j < i; ++j)
                        if (label.overlaps({map.markers[j].label.x - 7, map.markers[j].label.y - 4, 14, 9})) {
                            clear = false;
                            break;
                        }
                    if (clear) {
                        dot.label = candidate;
                        placed = true;
                    }
                }
    }
    return map;
}
std::optional<uint64_t> CrewMinimap::pickCrew(glm::dvec2 p) const {
    if (!panel.contains(p))
        return {};
    std::optional<uint64_t> result;
    double distance = 9;
    for (size_t i = 0; i < count; ++i) {
        const double d =
            std::min(glm::length(p - markers[i].label), glm::length(p - markers[i].position) + 4.);
        if (d < distance) {
            distance = d;
            result = markers[i].id;
        }
    }
    return result;
}
std::optional<CrewStation> CrewMinimap::pickRoom(glm::dvec2 p) const {
    for (size_t i = 0; i < rooms.size(); ++i)
        if (rooms[i].contains(p))
            return CrewStation(i);
    return {};
}
} // namespace infinity
