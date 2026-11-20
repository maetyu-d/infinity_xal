#pragma once
#include "Activity.hpp"
#include <algorithm>

namespace infinity {
inline constexpr glm::dvec3 dockingBerthOffset{0,.18,1.45};
inline constexpr uint32_t trafficActivityTag=0x80000000u;
inline constexpr uint32_t combatActivityTag=0x20000000u;
// A presentation/flight adapter for an existing station or vessel.
struct DockingBerth {
    ActivityId id;
    glm::dvec3 metres{}, velocity{};
    glm::dquat orientation{1, 0, 0, 0};
    double radius = 420;
    bool vessel() const { return (id.slot&(trafficActivityTag|combatActivityTag|0x10000000u))!=0; }
    double laneLength() const {
        if(vessel())return std::clamp(radius*1.45,55.,115.);
        return std::max(600., radius * 3);
    }
    double halfWidth() const {
        if(vessel())return std::clamp(radius*.13,6.,13.);
        return std::clamp(radius * .14, 32., 64.);
    }
};
std::optional<DockingBerth> dockingBerth(const ActivityBody&);
enum class DockingPhase : uint8_t { None, Arrival, Align, Glide, Capture, Docked, Departure, ManualApproach, ManualCapture };
std::string_view dockingPhaseName(DockingPhase);
inline bool manualDocking(DockingPhase phase) {
    return phase == DockingPhase::ManualApproach || phase == DockingPhase::ManualCapture;
}
struct DockingReadout {
    DockingPhase phase = DockingPhase::None;
    bool available = false, active = false, docked = false;
    glm::dvec3 offset{}, localForward{0, 0, -1}, localUp{0, 1, 0};
    double range = 0, closing = 0, relativeSpeed = 0, heading = 0, roll = 0, width = 1, capture = 0;
};
struct DockingGuidance {
    glm::dvec3 point{}, velocity{}, forward{0, 0, -1}, up{0, 1, 0};
    bool clamp = false, finished = false;
};
DockingReadout dockingReadout(const DockingBerth&, glm::dvec3 eye, glm::dvec3 velocity, glm::dquat attitude);
glm::dvec3 dockingFrameVelocity(const DockingBerth&,glm::dvec3 eye,glm::dvec3 turn);
bool manualClampReady(const DockingReadout&);
double manualDockingSpeed(double berthRange);
class DockingController {
  public:
    void begin();
    void beginFinal();
    void beginManual();
    bool updateManual(const DockingReadout&, double dt);
    void cancel();
    void depart();
    bool resumeClamp(const DockingReadout&);
    DockingPhase phase() const {
        return phase_;
    }
    double capture() const {
        return dwell_ / 1.6;
    }
    DockingGuidance update(const DockingBerth&, glm::dvec3 eye, glm::dvec3 velocity, glm::dquat attitude,
                           double dt);
    DockingGuidance update(const DockingBerth&, glm::dvec3 eye, glm::dvec3 velocity, glm::dquat attitude,
                           double dt,glm::dvec3 turn);

  private:
    DockingPhase phase_ = DockingPhase::None;
    double dwell_ = 0;
    bool routeChecked_ = false, bypass_ = false;
    glm::dvec3 bypassPoint_{};
};
// Bounded lines in metres relative to the berth; actual eye subtraction happens in the renderer.
Mesh dockingLane(const DockingBerth&, double seconds, DockingPhase);
Mesh dockingLane(const DockingBerth&, double seconds, DockingPhase, double capture, float visibility);
} // namespace infinity
