#define GLM_FORCE_INLINE
#include "infinity/Targeting.hpp"
#include "infinity/StationControl.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinity {
unsigned targetingMount(const WeaponSystem& guns, unsigned playerMount, bool stationActive) {
    if (stationActive)
        return playerMount;
    unsigned chosen = playerMount;
    double best = 1e100;
    for (unsigned i = 0; i < maxWeaponMounts; ++i) {
        const auto& m = guns.mount(i);
        if (m.level != missileLauncher || m.integrity <= 0 || m.crewReadiness <= 0)
            continue;
        const double wait =
            std::max(m.cooldown, std::max(0., m.heat + weaponSpec(m.level).heatPerShot - 100) /
                                     weaponSpec(m.level).cooling) +
            (m.overheated ? 10 : 0);
        if (wait < best) {
            chosen = i;
            best = wait;
        }
    }
    return chosen;
}
namespace {
bool finite(glm::dvec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool enemy(const ActivityBody& b) {
    return b.hostile && !b.disabled && b.hull > 0;
}
} // namespace
std::optional<ActivityId> selectHostile(std::span<const ActivityContact> contacts,
                                        const std::optional<ActivityId>& selected, HostileSelection mode) {
    // The bounded display list is not reordered. Identity is the tie-breaker so
    // cycling does not depend on asynchronous presentation order or tiny range changes.
    std::vector<const ActivityContact*> choices;
    choices.reserve(std::min<size_t>(contacts.size(), 128));
    for (const auto& c : contacts) {
        if (!enemy(c.body) || c.occluded || !std::isfinite(c.distance) || c.distance < 0 ||
            !finite(c.localDirection))
            continue;
        choices.push_back(&c);
        if (choices.size() == 128)
            break;
    }
    if (choices.empty())
        return {};
    if (mode == HostileSelection::Crosshair) {
        const ActivityContact* best = nullptr;
        double alignment = std::cos(35. * std::numbers::pi / 180);
        for (const auto* c : choices) {
            const double length = glm::length(c->localDirection);
            if (length < 1e-12)
                continue;
            const double dot = -c->localDirection.z / length;
            if (dot > alignment || (dot == alignment && best && c->body.id < best->body.id)) {
                alignment = dot;
                best = c;
            }
        }
        return best ? std::optional(best->body.id) : std::nullopt;
    }
    if (selected && mode != HostileSelection::Nearest) {
        std::sort(choices.begin(), choices.end(), [](auto a, auto b) { return a->body.id < b->body.id; });
        const auto current =
            std::find_if(choices.begin(), choices.end(), [&](auto c) { return c->body.id == *selected; });
        if (current != choices.end()) {
            const size_t index = size_t(current - choices.begin());
            return choices[(index + choices.size() + (mode == HostileSelection::Previous ? -1 : 1)) %
                           choices.size()]
                ->body.id;
        }
    }
    if (mode != HostileSelection::Nearest) {
        const ActivityContact* relay = nullptr;
        for (const auto* c : choices)
            if (c->body.status.starts_with("TRANSMITTING /") && (!relay || c->distance < relay->distance))
                relay = c;
        if (relay)
            return relay->body.id;
    }
    const auto nearest = std::min_element(choices.begin(), choices.end(), [](auto a, auto b) {
        return a->distance == b->distance ? a->body.id < b->body.id : a->distance < b->distance;
    });
    return (*nearest)->body.id;
}
std::optional<double> missileInterceptTime(glm::dvec3 d, glm::dvec3 velocity) {
    if (!finite(d) || !finite(velocity))
        return {};
    const double a = glm::dot(velocity, velocity) - missileSpeedMetres * missileSpeedMetres,
                 b = 2 * glm::dot(d, velocity), c = glm::dot(d, d);
    if (c < 1e-12)
        return 0.;
    double time = -1;
    if (std::abs(a) < 1e-8) {
        if (b < 0)
            time = -c / b;
    } else {
        const double discriminant = b * b - 4 * a * c;
        if (discriminant >= 0) {
            const double root = std::sqrt(discriminant);
            // Stable quadratic roots also handle a target nearly matching missile speed.
            const double q = -.5 * (b + std::copysign(root, b));
            const double t1 = q / a, t2 = std::abs(q) > 1e-20 ? c / q : -1;
            if (t1 >= 0)
                time = t1;
            if (t2 >= 0 && (time < 0 || t2 < time))
                time = t2;
        }
    }
    if (!std::isfinite(time) || time < 0 || time > missileLifetimeSeconds)
        return {};
    return time;
}
void TargetingComputer::reset() {
    view_ = {};
    progress_ = missing_ = 0;
    traceAge_ = 1;
    acquired_ = lineClear_ = false;
    reference_.reset();
}
void TargetingComputer::update(const Universe& u, const WeaponRay& ray, glm::dvec3 shipVelocity,
                               const std::optional<ActivityId>& selected, const WeaponSystem& guns,
                               unsigned mount, std::span<const ActivityBody> bodies, double dt,
                               bool inhibited, double navigator, const Geometry* geometry) {
    view_.canLaunch = false;
    view_.cue = 0;
    if (!std::isfinite(dt) || dt <= 0 || dt > 1 || !finite(ray.origin) || !finite(ray.direction) ||
        !finite(shipVelocity) || std::abs(glm::length(ray.direction) - 1) > .01 ||
        !std::isfinite(ray.scale) || ray.scale <= 0 || ray.scale > 1 || !std::isfinite(navigator))
        return;
    if (selected != view_.target) {
        reset();
        view_.target = selected;
    }
    view_.cue = 0;
    view_.canLaunch = false;
    view_.interceptValid = false;
    view_.flightTime = -1;
    if (!selected) {
        view_.state = TargetLock::None;
        view_.status = "H SELECT HOSTILE / ALT-H CROSSHAIR";
        return;
    }
    const auto found =
        std::find_if(bodies.begin(), bodies.end(), [&](const auto& b) { return b.id == *selected; });
    if (found == bodies.end()) {
        if (acquired_)
            view_.cue = -1;
        acquired_ = false;
        progress_ = 0;
        view_.progress = 0;
        view_.state = TargetLock::Lost;
        view_.status = "CONTACT LOST / H NEXT HOSTILE";
        return;
    }
    auto body = *found;
    guns.apply(body);
    view_.name = body.name;
    view_.hull = body.hull;
    if (!enemy(body)) {
        if (acquired_)
            view_.cue = -1;
        acquired_ = false;
        progress_ = view_.progress = 0;
        view_.state = TargetLock::Disabled;
        view_.status = body.hull <= 0 || body.disabled ? "TARGET DISABLED / LOCK RELEASED"
                                                       : "NON-HOSTILE / NO WEAPON LOCK";
        return;
    }
    const auto muzzle = ray.origin + ray.right * (weaponMountLateralMetres(mount) * ray.scale);
    auto delta = body.position - muzzle, velocity = body.velocity;
    if (body.id.home != ray.reference) {
        const auto eye =
            u.pointRelative(ray.reference, muzzle / std::pow(10., u.describe(ray.reference).frame.logRadius),
                            body.id.home, 0);
        const auto rotation = u.relative(body.id.home, ray.reference, 0).orientation;
        delta = rotation * (body.position - eye);
        velocity = rotation * body.velocity;
    }
    view_.direction = delta;
    view_.intercept = delta;
    view_.range = glm::length(delta);
    if (view_.range < 1e-9) {
        reset();
        return;
    }
    view_.closing = -glm::dot(velocity - shipVelocity, delta / view_.range);
    view_.angle =
        std::acos(std::clamp(glm::dot(ray.direction, delta / view_.range), -1., 1.)) * 180 / std::numbers::pi;
    const unsigned level = mount < maxWeaponMounts ? guns.mount(mount).level : 0;
    view_.missile = level == missileLauncher;
    const double reach = (level ? weaponSpec(level).rangeMetres : 3000) * ray.scale;
    TargetLock failure = TargetLock::None;
    std::string why;
    if (inhibited) {
        failure = TargetLock::Inhibited;
        why = "WEAPONS INHIBITED / LOCK SUSPENDED";
    } else if (view_.range - body.radius > reach) {
        failure = TargetLock::OutOfRange;
        why = "OUT OF RANGE / CLOSE ON TARGET";
    } else if (view_.angle > (acquired_ ? 25. : 15.)) {
        failure = TargetLock::OffBoresight;
        why = "OUTSIDE SEEKER / TURN TOWARD TARGET";
    } else if (view_.missile && ray.scale < .99) {
        failure = TargetLock::Inhibited;
        why = "MISSILES REQUIRE FULL-SIZE SHIP";
    } else {
        traceAge_ += dt;
        if (!reference_ || *reference_ != ray.reference || traceAge_ >= .1 ||
            glm::length(muzzle - traceOrigin_) > std::max(2., view_.range * .01) ||
            glm::length(delta - traceDelta_) > std::max(2., view_.range * .01)) {
            auto sight = ray;
            sight.origin = muzzle;
            sight.direction = delta / view_.range;
            const auto hit =
                traceWeapon(u, sight, std::min(reach, view_.range + body.radius), bodies, geometry);
            lineClear_ = hit.body && hit.body->id == body.id;
            traceAge_ = 0;
            reference_ = ray.reference;
            traceOrigin_ = muzzle;
            traceDelta_ = delta;
        }
        if (!lineClear_) {
            failure = TargetLock::Blocked;
            why = "SIGHTLINE BLOCKED / REPOSITION";
        }
    }
    if (failure != TargetLock::None) {
        missing_ += dt;
        view_.state = failure;
        view_.status = why;
        if (acquired_ && missing_ < .3) {
            view_.state = TargetLock::Memory;
            view_.status = "TRACK MEMORY / " + why;
        } else {
            if (acquired_)
                view_.cue = -1;
            acquired_ = false;
            progress_ = std::max(0., progress_ - dt * 3.5);
        }
        view_.progress = progress_;
        return;
    }
    missing_ = 0;
    progress_ = std::min(1., progress_ + dt * (.8 + .3 * std::clamp(navigator, 0., 1.5)));
    view_.progress = progress_;
    if (progress_ < 1) {
        view_.state = TargetLock::Acquiring;
        view_.status = "ACQUIRING / HOLD IN SEEKER";
        return;
    }
    if (!acquired_)
        view_.cue = 1;
    acquired_ = true;
    view_.state = TargetLock::Locked;
    view_.status = "LOCKED / MANUAL GUN AIM";
    if (view_.missile) {
        if (const auto t = missileInterceptTime(delta, velocity)) {
            view_.flightTime = *t;
            view_.intercept = delta + velocity * (*t);
            view_.interceptValid = true;
            view_.canLaunch = true;
            view_.status = "MISSILE LOCK / SPACE LAUNCH";
            const auto& gun = guns.mount(mount);
            if (gun.integrity <= 0 || gun.crewReadiness <= 0) {
                view_.canLaunch = false;
                view_.status = "LOCKED / LAUNCHER OFFLINE";
            } else if (gun.cooldown > 1e-9) {
                view_.canLaunch = false;
                view_.status = "LOCKED / LAUNCHER RELOADING";
            } else if (gun.overheated || gun.heat + weaponSpec(gun.level).heatPerShot > 100) {
                view_.canLaunch = false;
                view_.status = "LOCKED / LAUNCHER COOLING";
            }
        } else
            view_.status = "LOCKED / NO MISSILE INTERCEPT";
    }
}
} // namespace infinity
