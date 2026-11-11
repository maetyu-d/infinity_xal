#define GLM_FORCE_INLINE
#include "infinity/ShipSignals.hpp"
#include "infinity/NpcCombat.hpp"
#include "infinity/SaveTag.hpp"
#include "infinity/SignalTraffic.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numbers>
#include <set>
#include <sstream>

namespace infinity {
namespace {
bool finite(glm::dvec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool flying(ActivityKind k) {
    return k == ActivityKind::Freighter || k == ActivityKind::Ferry || k == ActivityKind::Patrol ||
           k == ActivityKind::Survey || k == ActivityKind::Raider;
}
glm::dvec3 point(const Universe& u, const Address& from, glm::dvec3 p, const Address& to) {
    return from == to ? p : u.pointRelative(from, p / std::pow(10., u.describe(from).frame.logRadius), to, 0);
}
std::string percent(double x) {
    return std::to_string(int(std::round(std::clamp(x, 0., 100.)))) + "%";
}
} // namespace
bool shipSignalId(const DiscoveryId& id) {
    return id.planet.kind() == Kind::Planet && (id.channel & 0xf0000000u) == 0x30000000u &&
           (id.channel & 0x0fffffffu) <= 1000000 && id.episode == 0;
}
double signalBeamStrength(const Universe& u, const SignalBeam& beam, const Address& frame, glm::dvec3 p) {
    if (!finite(beam.origin) || !finite(beam.direction) || !finite(p) || !std::isfinite(beam.range) ||
        beam.range <= 0 || !std::isfinite(beam.strength) || beam.strength <= 0 ||
        std::abs(glm::length(beam.direction) - 1) > .001)
        return 0;
    const auto delta = point(u, frame, p, beam.reference) - beam.origin;
    const double range = glm::length(delta);
    if (range < 1 || range > beam.range ||
        glm::dot(delta / range, beam.direction) < std::cos(12. * std::numbers::pi / 180))
        return 0;
    return std::clamp(beam.strength, 0., 1.) / (1 + .5 * std::pow(range / beam.range, 2));
}
std::vector<SignalSource> shipSignals(const Universe& u, const NpcPlayer& p,
                                      std::span<const ActivityBody> physical, const NpcCombat& npc,
                                      const VesselOperations& scans) {
    auto system = p.reference;
    while (system.kind() > Kind::System)
        system = system.parent();
    if (system.kind() != Kind::System || !finite(p.position) || !std::isfinite(p.seconds))
        return {};
    struct Candidate {
        const ActivityBody* body;
        double range;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(std::min<size_t>(physical.size(), 512));
    std::set<ActivityId> seen;
    for (const auto& body : physical.first(std::min<size_t>(physical.size(), 512))) {
        if (!flying(body.kind) || body.id.home.parent() != system || body.id.slot > 1000000 ||
            body.hull <= 0 || body.systems[size_t(ShipSystem::Engines)] <= 0 || !finite(body.position) ||
            !seen.insert(body.id).second)
            continue; // Existing distress adapter owns disabled ships.
        const auto range = glm::length(body.position - point(u, p.reference, p.position, body.id.home));
        if (range <= 30000)
            candidates.push_back({&body, range});
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        if (a.range != b.range)
            return a.range < b.range;
        return a.body->id < b.body->id;
    });
    std::vector<SignalSource> result;
    result.reserve(16);
    for (const auto& candidate : candidates) {
        if (result.size() == 16)
            break;
        const auto& b = *candidate.body;
        const auto agent = npc.agents().find(b.id);
        const bool tactical = b.kind == ActivityKind::Raider || b.hostile;
        // Quiet enemies cannot be discovered through a fictional public hail.
        if (tactical && (agent == npc.agents().end() ||
                         (agent->second.mode == NpcMode::Patrol && agent->second.signalAt <= p.seconds)))
            continue;
        SignalSource s;
        s.id = {b.id.home, 0x30000000u + b.id.slot, 0};
        s.contact = b.id;
        s.descriptor = liveSignalDescriptor(u, s.id);
        s.seed = mix64(s.descriptor);
        s.kind = tactical ? SignalKind::Tactical : SignalKind::Transponder;
        s.frequency = 65 + 2.5 * (s.seed % 365);
        s.bandwidth = tactical ? 5 : 8;
        s.period = tactical ? .8 : 1.5;
        s.reach = tactical ? 18000 : 30000;
        s.planetRadius = std::pow(10., u.describe(b.id.home).frame.logRadius);
        s.metres = b.position;
        s.worldLabel = b.name;
        s.transmitsPosition = !tactical;
        s.measuredFix = scans.confirmed(b.id, p.seconds);
        s.signature = std::string(tactical ? "EMITTER / " : "SHIP / ") + b.name;
        if (tactical) {
            const auto& a = agent->second;
            s.transmission[0] = a.missileLock > 0
                                    ? "FIRE CONTROL / MISSILE LOCK " + percent(a.missileLock / 1.2 * 100)
                                : a.signalAt > p.seconds
                                    ? "PURSUIT RELAY / " +
                                          std::to_string(int(std::ceil(a.signalAt - p.seconds))) + "S TO SEND"
                                    : "TACTICAL EMISSION / " + std::string(npcModeName(a.mode));
            s.transmission[1] = s.measuredFix ? "ACTIVE SCAN FIX / INTERNALS CONFIRMED"
                                              : "NO POSITION BROADCAST / BEARING ONLY";
            s.transmission[2] = "S FOCUS SCAN / X DIRECTIONAL JAM";
        } else {
            s.transmission[0] = b.status.empty() ? "ROUTINE TRANSPONDER / UNDER WAY" : b.status;
            s.transmission[1] = b.economicBrief.empty()
                                    ? "FLIGHT SPEED " + std::to_string(int(glm::length(b.velocity))) +
                                          " M/S / POSITION BROADCAST"
                                    : b.economicBrief;
            s.transmission[2] = "S INTERNAL SCAN / J APPROACH / CTRL-0 VESSEL";
        }
        result.push_back(std::move(s));
    }
    return result;
}
void ShipSignals::update(const Universe& u, const NpcPlayer& p, const std::optional<ActivityBody>& body,
                         bool scan, bool jam, bool decoded, double power, double navigator, double dt,
                         std::span<const ActivityBody> nearby, const Geometry* geometry) {
    beam_ = {};
    scanning_ = false;
    if (!std::isfinite(dt) || dt <= 0 || dt > 1 || !std::isfinite(power) || !std::isfinite(navigator) ||
        !finite(p.position) || !finite(p.forward) || !std::isfinite(p.size) || !std::isfinite(p.hull) ||
        !std::isfinite(p.seconds) || p.seconds < 0 || p.seconds > 1e12)
        return;
    power = std::clamp(power, 0., 1.);
    if (!jam && !p.field)
        capacitor_ = std::min(1., capacitor_ + dt * .075 * power);
    // An exhausted held key must not produce frame-by-frame pulses on recharge.
    if (cooling_ && capacitor_ >= .3)
        cooling_ = false;
    status_ =
        cooling_ ? "EMITTER COOLING / RELEASE X TO RECHARGE" : "PASSIVE / S SCAN / X JAM HOSTILE CARRIER";
    if (p.field || p.landed || p.hull <= 0 || p.size < .99 || power < .35) {
        if (scan || jam)
            status_ = "FULL SIZE / FREE FLIGHT / HELM POWER REQUIRED";
        return;
    }
    if (!scan && !jam)
        return;
    if (!body || !flying(body->kind) || !finite(body->position)) {
        status_ = "V SHIP CARRIERS / DECODE BEFORE S SCAN OR X JAM";
        return;
    }
    const auto target = point(u, body->id.home, body->position, p.reference);
    const auto delta = target - p.position;
    const double range = glm::length(delta);
    const double limit = jam ? 6000. : 8000.;
    if (range < 1 || range > limit || glm::length(p.forward) < .99 ||
        glm::dot(glm::normalize(p.forward), delta / range) < .985) {
        status_ =
            range > limit ? "OUT OF EMITTER RANGE / CLOSE ON BEARING" : "POINT NOSE AT CARRIER / NARROW BEAM";
        return;
    }
    const WeaponRay ray{p.reference, p.position, delta / range};
    const auto obstruction = traceWeapon(u, ray, range + body->radius + 1, nearby, geometry);
    if (!obstruction.body || obstruction.body->id != body->id) {
        status_ = "BEAM BLOCKED / TERRAIN OR SHIP COVER";
        return;
    }
    if (jam) {
        if (!decoded || !body->hostile) {
            status_ = "DECODE A HOSTILE EMITTER / JAM DOES NOT AFFECT CIVILIANS";
            return;
        }
        if (cooling_ || capacitor_ <= 0) {
            cooling_ = true;
            status_ = "EMITTER COOLING / RELEASE X TO RECHARGE";
            return;
        }
        capacitor_ = std::max(0., capacitor_ - dt * .25);
        if (capacitor_ <= 0)
            cooling_ = true;
        beam_ = {p.reference, p.position, delta / range,
                 body->id,    6000,       power * (.7 + .2 * std::clamp(navigator, 0., 1.5))};
        status_ = power < .65 ? "WEAK JAM POWER / RESTORE HELM BUS" : "JAM ACTIVE / GUNFIRE STILL DANGEROUS";
    } else {
        scanning_ = true;
        status_ = "ACTIVE INTERNAL SCAN / TRANSMITTING YOUR POSITION";
    }
}
ShipSignalView ShipSignals::view(const std::optional<ActivityBody>& body, const VesselOperations& scans,
                                 const VesselEngineering* engineering, double seconds) const {
    ShipSignalView v;
    v.scanning = scanning_;
    v.jamming = beam_.strength > 0;
    v.radiating = radiating();
    v.capacitor = capacitor_;
    v.status = status_;
    v.emission =
        radiating() ? "RADIATING / ENEMIES CAN DETECT YOU FARTHER AWAY" : "PASSIVE / NO ACTIVE EMISSION";
    if (!body) {
        v.condition = "NO CURRENT SHIP READING / CARRIER LOST";
        v.systems = "V NEXT SHIP / CLEAR LINE OF SIGHT";
        v.operations = "LAST ID RETAINED / NO LIVE FIX";
        return v;
    }
    v.tactical = body->hostile || body->kind == ActivityKind::Raider;
    const auto scan = scans.view(body, engineering, false);
    v.scan = scan.scan;
    v.confirmed = scans.confirmed(body->id, seconds);
    if (v.confirmed) {
        v.condition = "HULL " + percent(scan.systems[0]) + " / ENGINES " + percent(scan.systems[1]);
        v.systems = "THRUST " + percent(scan.systems[2]) + " / NOSE GUN " + percent(scan.systems[3]);
        v.operations = "CTRL-0 INTERNALS / CREW / REPAIR / RECOVERY";
    } else {
        v.condition = "INTERNAL SCAN " + percent(scan.scan * 100) + (scan.stale ? " / STALE" : "");
        v.systems = "HOLD S / SAME SCAN AS F12 / 8 KM RANGE";
        v.operations = "NO HIDDEN HULL OR CREW DATA BEFORE SCANNING";
    }
    return v;
}
std::string ShipSignals::serialize(uint64_t seed) const {
    std::ostringstream o;
    o << std::setprecision(17) << "INFINITY_SHIP_SIGNALS 1 " << seed << ' ' << capacitor_ << ' ' << cooling_;
    return o.str();
}
bool ShipSignals::restore(std::string_view text, uint64_t seed) {
    if (text.size() > 256)
        return false;
    std::istringstream in{std::string(text)};
    std::string tag;
    uint64_t savedSeed;
    unsigned version, cooling;
    double cap;
    if (!(in >> tag >> version >> savedSeed >> cap >> cooling) ||
        !matchesSaveTag(tag, "INFINITY_SHIP_SIGNALS") || version != 1 || savedSeed != seed ||
        !std::isfinite(cap) || cap < 0 || cap > 1 || cooling > 1)
        return false;
    in >> std::ws;
    if (!in.eof())
        return false;
    *this = ShipSignals{};
    capacitor_ = cap;
    cooling_ = cooling;
    return true;
}
} // namespace infinity
