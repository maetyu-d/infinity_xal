#pragma once
#include "Core.hpp"
#include "CockpitMotion.hpp"
#include "StationLayout.hpp"
#include <array>

namespace infinity {

// Ship axes are measured from the seated eye. +Z runs aft.
namespace shipDeck {
inline constexpr double hatch = 1.15, hatchHalf = .47, bridgeFloor = -.82, deckFloor = -1.10;
inline constexpr double passageHalf = .79, passageRoof = .97;
inline constexpr double quartersFront = 1.83, quartersBack = 3.40, quartersWall = 2.11;
inline constexpr double quartersDoorFront = 2.03, quartersDoorBack = 3.22;
inline constexpr double cargoFront = 3.72, cargoBack = 7.42, cargoHalf = 1.64, cargoRoof = 1.19;
inline constexpr double cargoDoorHalf = .60;
inline constexpr double cargoScreenZ = 7.243, cargoScreenHalf = .53;
inline constexpr double cargoScreenTop = .381, cargoScreenHeight = .617;
inline constexpr DeckArea seat{-.30,-.27,.30,.55};
inline constexpr std::array cargoCrates{
    DeckArea{-1.52,4.10,-.83,4.93}, DeckArea{.90,4.06,1.52,4.84},
    DeckArea{-1.52,5.31,-.69,6.38}, DeckArea{.90,5.27,1.52,6.19},
    DeckArea{.72,6.58,1.53,7.21}
};
inline constexpr DeckArea bunk{-2.02,2.00,-1.56,3.22};
inline constexpr DeckArea systems{1.63,1.97,2.02,3.27};
}

static_assert(stationDeck::shipDoor == -shipDeck::cargoHalf && stationDeck::floor == shipDeck::deckFloor);

enum class DeckStation { Quarters, Systems, Cargo, Navigation };
enum class BoardingInteraction { None, ShipDoor, BridgeDoor, Freight };
enum class AirlockMotion { Holding, SealingShip, SealingBridge, OpeningShip, OpeningBridge };
struct DeckOperations {
    bool courseActive = false;
    bool loaded = false;
    bool damaged = false;
};
struct DeckCrewPose {
    glm::dvec2 position{};
    DeckStation station = DeckStation::Quarters;
    double yaw = 0, height = 1.78, stride = 0, taskPhase = 0;
    glm::vec3 rim{};
    bool moving = false;
};

class SeatTransition {
  public:
    void begin(const CockpitMotionPose& from, bool standing);
    void update(double seconds);
    bool active() const { return remaining_>0; }
    CockpitMotionPose pose(const CockpitMotionPose& destination) const;

  private:
    CockpitMotionPose from_{};
    double remaining_ = 0;
    bool standing_ = false;
};

class ShipInterior {
  public:
    ShipInterior();
    bool walking() const { return walking_; }
    // Stand at the side of the seat or sit down within reach of it.
    bool useSeat();
    bool seatInReach() const;
    bool cargoTerminalInReach() const;
    void setStation(std::optional<StationKey>);
    const StationLayout* station() const { return station_ ? &*station_ : nullptr; }
    bool aboardDockside() const;
    bool departureClear() const;
    BoardingInteraction boardingInteraction() const;
    void useAirlock(BoardingInteraction);
    double shipDoorOpening() const { return smoothstep(0.,1.,shipDoorTravel_); }
    double bridgeDoorOpening() const { return smoothstep(0.,1.,bridgeDoorTravel_); }
    AirlockMotion airlockMotion() const;
    void look(glm::dvec2 mouseDelta);
    void step(glm::dvec2 strafeForward, double seconds, bool fast = false, DeckOperations operations = {});
    glm::dquat orientation() const;
    glm::dvec3 eye() const;
    glm::dvec2 position() const { return position_; }
    std::string_view room() const;
    double hatchOpening() const { return hatchOpening_; }
    const std::array<DeckCrewPose,3>& crew() const { return crew_; }

  private:
    enum class AirlockSide { Sealed, Ship, Bridge };
    void updateCrew(double seconds, DeckOperations);
    void updateAirlock(double seconds);
    bool walking_ = false;
    glm::dvec2 position_{}, angles_{};
    double hatchOpening_ = 0;
    std::optional<StationLayout> station_;
    AirlockSide airlockSide_ = AirlockSide::Sealed;
    double shipDoorTravel_ = 0, bridgeDoorTravel_ = 0;
    std::array<DeckCrewPose,3> crew_{};
    std::array<double,3> dwell_{};
    std::array<unsigned,3> nextStop_{};
    // Workstations branch from a shared passage. One crew transfer at a
    // time leaves room for the player to pass in the narrow doorways.
    size_t travelling_ = 3, waypoint_ = 0;
    DeckStation destination_ = DeckStation::Quarters;
    std::array<glm::dvec2,3> waypoints_{};
};

} // namespace infinity
