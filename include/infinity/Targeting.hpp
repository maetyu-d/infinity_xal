#pragma once
#include "Weapons.hpp"

namespace infinity {
enum class HostileSelection { Next, Previous, Crosshair, Nearest };
// At the helm, direct a usable missile launcher in the mixed battery. At a
// weapon station, always use that station's own sight and weapon limits.
unsigned targetingMount(const WeaponSystem&, unsigned playerMount, bool stationActive);
std::optional<ActivityId> selectHostile(std::span<const ActivityContact>, const std::optional<ActivityId>&,
                                        HostileSelection = HostileSelection::Next);
enum class TargetLock {
    None,
    Acquiring,
    Locked,
    Memory,
    Blocked,
    OutOfRange,
    OffBoresight,
    Disabled,
    Inhibited,
    Lost
};
struct TargetingView {
    std::optional<ActivityId> target;
    TargetLock state = TargetLock::None;
    glm::dvec3 direction{}, intercept{}; // Reference axes; adapter rotates these into ship axes for HUD.
    double progress = 0, range = 0, closing = 0, hull = 0, angle = 0, flightTime = -1;
    bool missile = false, canLaunch = false, interceptValid = false;
    int cue = 0; // Once on acquisition/loss; never per-frame audio retriggering.
    std::string name, status;
};
// Transient measured lock on an existing activity identity. No generation,
// steering, camera movement or save-state changes occur in this controller.
class TargetingComputer {
  public:
    void update(const Universe&, const WeaponRay&, glm::dvec3 shipVelocity, const std::optional<ActivityId>&,
                const WeaponSystem&, unsigned mount, std::span<const ActivityBody>, double dt, bool inhibited,
                double navigator = 0, const Geometry* = nullptr);
    const TargetingView& view() const {
        return view_;
    }
    std::optional<ActivityId> lockedTarget() const {
        return view_.canLaunch ? view_.target : std::nullopt;
    }
    void reset();

  private:
    TargetingView view_;
    double progress_ = 0, missing_ = 0, traceAge_ = 1;
    bool acquired_ = false, lineClear_ = false;
    std::optional<Address> reference_;
    glm::dvec3 traceOrigin_{}, traceDelta_{};
};
// Constant-velocity cue using the actual missile speed/lifetime. Lasers are
// instantaneous and deliberately do not receive a fictitious lead marker.
std::optional<double> missileInterceptTime(glm::dvec3 displacement, glm::dvec3 targetVelocity);
} // namespace infinity
