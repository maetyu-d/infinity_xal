#pragma once
#include "Activity.hpp"
#include "Crew.hpp"
#include "ShipEngineering.hpp"
namespace infinity {
struct VesselProfile {
    double reactor = 1, cooling = 1, endurance = 1;
    unsigned complement = 2;
    std::array<glm::dvec2, crewRoomCount> rooms{};
    std::vector<std::pair<unsigned, unsigned>> doors;
    uint64_t fingerprint = 0;
};
// Hull-local interiors and ratings derive from the existing activity identity.
// They never write coordinates, routes, seeds or universe geometry.
VesselProfile vesselProfile(const ActivityBody&);
std::optional<CrewMember> vesselSurvivor(uint64_t universeSeed, const ActivityId&, double lifeSupport);
struct VesselEngineering {
    ShipPlant plant;
    std::array<CrewRoom, crewRoomCount> rooms{};
    SystemHealth observed = intactSystems();
    bool initialized = false, surrendered = false;
    void advance(const ActivityBody&, double throttle, double weapons, double seconds);
    void service();
    std::string serialize() const;
    bool restore(std::string_view);
};
enum class VesselTask : uint8_t { None, Repair, Evacuate, Salvage, Return };
std::string_view vesselTaskName(VesselTask);
struct VesselView {
    bool open = false, confirmed = false, stale = false, accessible = false;
    std::string name, identity, status, scanStatus, taskStatus, party, supplies;
    double scan = 0, heat = 0, power = 0, fuel = 0, air = 0, progress = 0;
    SystemHealth systems = intactSystems();
    std::array<CrewRoom, crewRoomCount> rooms{};
    VesselProfile deck;
    unsigned partyRoom = 0, destination = 0;
    bool deployed = false;
};
struct VesselTaskResult { ActivityId id; VesselTask task; uint64_t crew = 0; double seconds = 0; };
struct NpcPlayer;
class VesselOperations {
  public:
    // Scans require a centred, unobstructed live contact. Confirmed locks expire.
    void observe(const Universe&, const NpcPlayer&, const std::optional<ActivityBody>&,
                 const VesselEngineering*, double dt, bool focus, double navigator,
                 std::span<const ActivityBody>, const Geometry* = nullptr);
    bool confirmed(const ActivityId&, double seconds) const;
    std::string begin(VesselTask, const ActivityBody&, const VesselEngineering*, ShipCrew&);
    std::optional<VesselTaskResult> advance(const Universe&, const NpcPlayer&,
                 const std::optional<ActivityBody>&, const VesselEngineering*, ShipCrew&, double dt,
                 std::span<const ActivityBody>, const Geometry* = nullptr);
    void recall();
    void acknowledge(bool success);
    VesselView view(const std::optional<ActivityBody>&, const VesselEngineering*, bool open) const;
    std::string serialize(uint64_t seed) const;
    bool restore(std::string_view, const Universe&);
    std::optional<ActivityId> target() const;
    uint64_t party() const { return party_; }
  private:
    struct Scan { ActivityId id; double progress = 0, at = 0; SystemHealth systems = intactSystems(); };
    std::vector<Scan> scans_; // At most 64; oldest observation evicted.
    std::optional<ActivityId> selected_, target_;
    double clock_ = 0, range_ = -1, relativeSpeed_ = 0, work_ = 0, leg_ = 0, returning_ = 0;
    VesselTask task_ = VesselTask::None;
    uint64_t party_ = 0;
    unsigned room_ = 0, destination_ = 0;
    bool accessible_ = false, waiting_ = false, issued_ = false;
    std::string status_, partyName_;
    void returnParty(ShipCrew&);
};
} // namespace infinity
