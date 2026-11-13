#pragma once
#include "Core.hpp"
#include <array>

namespace infinity {

struct DeckArea {
    double left, front, right, back;

    bool contains(glm::dvec2 point) const {
        return point.x >= left && point.x <= right && point.y >= front && point.y <= back;
    }
};

// Ship axes are measured from the seated eye. +Z runs aft.
namespace shipDeck {
inline constexpr double hatch = 1.15, hatchHalf = .47, bridgeFloor = -.82, deckFloor = -1.10;
inline constexpr double passageHalf = .71, passageRoof = .97;
inline constexpr double quartersFront = 1.83, quartersBack = 3.40, quartersWall = 1.96;
inline constexpr double quartersDoorFront = 2.13, quartersDoorBack = 3.12;
inline constexpr double cargoFront = 3.72, cargoBack = 7.42, cargoHalf = 1.64, cargoRoof = 1.19;
inline constexpr double cargoDoorHalf = .60;
inline constexpr DeckArea seat{-.30,-.27,.30,.55};
inline constexpr std::array cargoCrates{
    DeckArea{-1.52,4.10,-.83,4.93}, DeckArea{.90,4.06,1.52,4.84},
    DeckArea{-1.52,5.31,-.69,6.38}, DeckArea{.90,5.27,1.52,6.19},
    DeckArea{.72,6.58,1.53,7.21}
};
inline constexpr DeckArea bunk{-1.86,2.00,-1.44,3.22};
inline constexpr DeckArea systems{1.48,1.97,1.86,3.27};
}

struct DeckCrewPose {
    glm::dvec3 feet;
    double yaw, height, taskMotion;
    glm::vec3 rim;
};
std::array<DeckCrewPose,3> shipDeckCrew(double seconds);

class ShipInterior {
  public:
    bool walking() const { return walking_; }
    // Stand at the side of the seat or sit down within reach of it.
    bool useSeat();
    bool seatInReach() const;
    void look(glm::dvec2 mouseDelta);
    void step(glm::dvec2 strafeForward, double seconds, bool fast = false, double flightSeconds = 0);
    glm::dquat orientation() const;
    glm::dvec3 eye() const;
    glm::dvec2 position() const { return position_; }
    std::string_view room() const;
    double hatchOpening() const { return hatchOpening_; }

  private:
    bool walking_ = false;
    glm::dvec2 position_{}, angles_{};
    double hatchOpening_ = 0;
};

} // namespace infinity
