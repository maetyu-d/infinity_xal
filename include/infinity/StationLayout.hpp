#pragma once
#include "Activity.hpp"

namespace infinity {
struct DeckArea {
    double left, front, right, back;
    bool contains(glm::dvec2 point) const {
        return point.x >= left && point.x <= right && point.y >= front && point.y <= back;
    }
    auto operator<=>(const DeckArea&) const = default;
};

namespace stationDeck {
inline constexpr double centre=6.87, doorHalf=.50, doorTop=.88, doorTravel=2.02;
inline constexpr double doorClear=.985;
inline constexpr double shipDoor=-1.64, bridgeDoor=-3.70, floor=-1.10, ribPitch=1.55;
inline constexpr double bridgeHalf=.72, glassSill=-.68, glassShoulder=.49, bridgeRibWidth=.054;
inline constexpr size_t bridgeArchSegments=32;
inline constexpr double annexDoorTop=1.03, panoramaSill=-.47, panoramaHead=1.58, panoramaInset=.21;
}
inline constexpr double orbitalStationRadius=420.;

enum class StationRole { Freight, Habitat, Research, Industrial, Vault };
enum class StationModuleKind { Airlock, Bridge, Concourse, Habitat, Laboratory, Workshop, Vault, Freight,
                               Observation, Analysis, Engineering, Dispatch, Archive };

struct StationKey {
    uint64_t seed = 0;
    StationRole role = StationRole::Freight;
    bool vessel=false;
    auto operator<=>(const StationKey&) const = default;
};
struct StationModule {
    StationModuleKind kind;
    DeckArea floor;
    double ceiling;
    std::vector<DeckArea> fixtures;
};
struct StationAnnex {
    size_t parent;
    int side; // Negative is the front wall; positive is the back wall.
    StationModule room;
    double doorwayLeft;
    double doorwayRight;

    double insideWall() const { return side>0?room.floor.front:room.floor.back; }
    double outsideWall() const { return side>0?room.floor.back:room.floor.front; }
    bool panoramic() const { return room.kind==StationModuleKind::Observation; }
};
struct StationLayout {
    StationKey key;
    std::vector<StationModule> modules; // Shared wall at each consecutive pair; apertures align to the centreline.
    DeckArea freightPad;
    std::vector<StationAnnex> annexes;
    const StationModule& freightBay() const { return modules.back(); }
    const StationModule* roomAt(glm::dvec2) const;
    const StationAnnex* annexAt(size_t parent,int side) const;
};

StationKey stationKey(const ActivityId&, bool privateVault=false);
StationLayout generateStation(StationKey);
std::string_view stationRoleName(StationRole);
std::string_view stationModuleName(StationModuleKind);
glm::vec3 stationAccent(StationRole);

struct StationTrafficPad {
    glm::dvec3 centre; // Deck surface, in berth-local metres.
    glm::dvec2 halfSize;
};
inline constexpr std::array<StationTrafficPad,2> stationTrafficPads{{
    {{-54.,6.,108.},{9.,10.}}, {{58.,8.5,124.},{11.,12.}}
}};
enum class TrafficPhase { Inbound, AtBerth, Outbound, OnCircuit };
struct TrafficCraft {
    glm::dvec3 position; // Metres in the berth or wormhole axes.
    glm::dquat orientation;
    glm::vec3 paint;
    ActivityKind kind;
    double radius, thrust;
    bool strobe;
    glm::dvec3 velocity{};
    TrafficPhase phase=TrafficPhase::OnCircuit;
};
std::array<TrafficCraft,3> sampleStationTraffic(StationKey, double seconds);
std::array<TrafficCraft,5> sampleOrbitalTraffic(StationKey, double seconds);
std::array<TrafficCraft,2> sampleWormholeTraffic(double radius, double seconds);
std::array<glm::dvec3,4> wormholeBuoys(double radius);

enum class SpaceJunkKind { Satellite, Booster, SolarPanel, Count };
struct SpaceJunk {
    glm::dvec3 position; // Berth-local metres.
    glm::dquat orientation;
    double radius;
    SpaceJunkKind kind;
};
std::array<SpaceJunk,8> sampleSpaceJunk(StationKey, double seconds);
Mesh spaceJunkMesh(SpaceJunkKind, unsigned detail);

// The pressure shell's cross section, stored as height and lateral coordinates.
// An offset follows the surface normal for the layers of the frame.
std::array<glm::dvec2,stationDeck::bridgeArchSegments+3> bridgeGlassProfile(const StationModule&,
                                                                          double offset=0.);
Mesh stationGlazing(const StationLayout&);
Mesh modularStationHull(const StationLayout&, unsigned detail);
} // namespace infinity
