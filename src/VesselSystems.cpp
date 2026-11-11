#define GLM_FORCE_INLINE
#include "infinity/VesselSystems.hpp"
#include "infinity/NpcCombat.hpp"
#include "infinity/SaveTag.hpp"
#include <algorithm>
#include <iomanip>
#include <queue>
#include <set>
#include <sstream>
namespace infinity {
namespace {
void ratings(VesselProfile& p,const ActivityBody& b) {
    const auto seed = mix64(seedOf(b.id.home,b.seed) ^ mix64(b.id.slot) ^ 0x4445434b3634ULL);
    const bool freight = b.kind == ActivityKind::Freighter, patrol = b.kind == ActivityKind::Patrol;
    p.reactor = freight ? .78 : patrol ? 1. : .86 + double(seed % 13) / 100;
    p.cooling = freight ? .85 : patrol ? 1. : .72 + double((seed >> 8) % 24) / 100;
    p.endurance = freight ? 1. : .45 + double((seed >> 16) % 45) / 100;
    p.complement = std::clamp(unsigned(b.radius / 8) + 1, 2u, 8u);
    p.fingerprint=seed;
}
}
VesselProfile vesselProfile(const ActivityBody& b) {
    VesselProfile p; ratings(p,b);
    const auto seed=p.fingerprint;
    const bool freight=b.kind==ActivityKind::Freighter,patrol=b.kind==ActivityKind::Patrol;
    p.doors.reserve(8);
    // A connected spine with role-specific side rooms and optional cross-links.
    // Changing hull dimensions changes deck proportions, never the ship's mesh.
    const double beam = std::clamp(b.radius / 15, .8, 1.6);
    for (unsigned i = 0; i < crewRoomCount; ++i) {
        const unsigned rank = (i * 3 + unsigned(seed % 7)) % 7;
        p.rooms[i] = {double(rank % 2) * 2 - 1, double(rank / 2) * 1.4 - 2.1};
        p.rooms[i].x *= beam;
    }
    for (unsigned rank = 1; rank < 7; ++rank) {
        unsigned a = 0, c = 0;
        for (unsigned i = 0; i < 7; ++i) {
            if ((i * 3 + unsigned(seed % 7)) % 7 == rank - 1) a = i;
            if ((i * 3 + unsigned(seed % 7)) % 7 == rank) c = i;
        }
        p.doors.emplace_back(a, c);
    }
    if (freight || patrol) p.doors.emplace_back(0, 3);
    if ((seed >> 24) & 1) p.doors.emplace_back(2, 5);
    p.fingerprint = seed;
    return p;
}
std::optional<CrewMember> vesselSurvivor(uint64_t universeSeed, const ActivityId& id, double life) {
    const auto seed = mix64(seedOf(id.home, universeSeed) ^ mix64(id.slot) ^ 0x5245534355453532ULL);
    if (life <= 0 || seed % 4 == 0) return {};
    auto m = ShipCrew(universeSeed).recruit(seed % 1000000 + 1);
    m.id = (1ULL << 62) | (seed & ((1ULL << 62) - 1));
    m.health = 45 + double((seed >> 8) % 26); m.fatigue = 40; m.morale = 55;
    m.duty = m.room = m.target = m.walkTo = CrewStation::Bunks;
    m.weaponMount = noWeaponMount;
    return m;
}
void VesselEngineering::advance(const ActivityBody& b, double throttle, double weapons, double dt) {
    if (!std::isfinite(dt) || dt <= 0 || dt > 1) return;
    // Local simulation needs only ratings: no per-NPC deck/corridor allocation.
    VesselProfile profile;ratings(profile,b);
    if (!initialized) {
        for (size_t i = 0; i < engineeringSupplyCount; ++i)
            plant.stores[i] = engineeringCapacity[i] * profile.endurance;
        initialized = true;
    }
    constexpr std::array<unsigned, shipSystemCount> compartment{0, 3, 1, 2, 2, 2, 4};
    for (size_t s = 0; s < shipSystemCount; ++s) {
        auto& r = rooms[compartment[s]];
        const double hit = std::max(0., observed[s] - b.systems[s]);
        r.integrity = std::max(0., r.integrity - hit);
        if (hit >= 12) { r.fire = std::min(100., r.fire + hit * .6); r.breach = std::min(100., r.breach + hit * .25); }
        observed[s] = b.systems[s];
    }
    PlantDemand d;
    d.throttle = throttle; d.turn = throttle * .3; d.weapons = weapons;
    d.reactor = profile.reactor * rooms[3].integrity / 100;
    d.cooling = profile.cooling * rooms[4].integrity / 100;
    d.engineer = .5 + profile.complement * .08;
    for (const auto& r : rooms) { d.externalHeat += r.fire / 700; d.airLeak += r.breach * .0001; }
    plant.advance(d, dt);
    for (auto& r : rooms) {
        // The same pressure, fire and integrity quantities used by the player's deck.
        r.oxygen = std::clamp(r.oxygen + dt * (1.2 * plant.authority(PlantBus::Life) - r.breach * .1 - r.fire * .01), 0., 100.);
        r.fire = std::clamp(r.fire + dt * (r.oxygen < 18 ? -4. : -.12 * profile.complement), 0., 100.);
        r.integrity = std::max(0., r.integrity - r.fire * .002 * dt);
    }
    if (plant.heat >= .85 && plant.stores[1] >= 2) plant.flush();
}
void VesselEngineering::service() {
    plant.stores[0] = std::min(engineeringCapacity[0], plant.stores[0] + 2);
    plant.stores[1] = std::min(engineeringCapacity[1], plant.stores[1] + 2);
    plant.stores[2] = std::min(engineeringCapacity[2], plant.stores[2] + 1);
    plant.heat = std::min(plant.heat, .45); plant.reserve = std::max(plant.reserve, .3); plant.refresh();
    for (auto& r : rooms) { r.integrity = std::max(r.integrity, 60.); r.fire = 0; r.breach = 0; }
}
std::string VesselEngineering::serialize() const {
    std::ostringstream o; o << std::setprecision(17) << "VESSEL 1 " << initialized << ' ' << surrendered << ' ' << std::quoted(plant.serialize());
    for (const auto& r : rooms) o << ' ' << r.integrity << ' ' << r.oxygen << ' ' << r.fire << ' ' << r.breach;
    for (auto x : observed) o << ' ' << x;
    return o.str();
}
bool VesselEngineering::restore(std::string_view text) {
    VesselEngineering v; std::istringstream in{std::string(text)}; std::string tag, plantText; unsigned version, init, surrender;
    if (!(in >> tag >> version >> init >> surrender >> std::quoted(plantText)) || tag != "VESSEL" || version != 1 || init > 1 || surrender > 1 || !v.plant.restore(plantText)) return false;
    auto number = [&](double& x) { return bool(in >> x) && std::isfinite(x) && x >= 0 && x <= 100; };
    for (auto& r : v.rooms) if (!number(r.integrity) || !number(r.oxygen) || !number(r.fire) || !number(r.breach)) return false;
    for (auto& x : v.observed) if (!number(x)) return false;
    in >> std::ws; if (!in.eof()) return false;
    v.initialized = init; v.surrendered = surrender; *this = std::move(v); return true;
}
} // namespace infinity
