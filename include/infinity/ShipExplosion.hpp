#pragma once
#include "Activity.hpp"
#include <array>
#include <string_view>

namespace infinity {
inline constexpr double shipExplosionLifetime = 10.;
inline constexpr size_t maxShipExplosions = 16;
enum class ShipPropellant : uint8_t { Hypergolic, KeroseneLox, HydrogenLox, MethaneLox };
ShipPropellant shipPropellant(const ActivityBody&);
std::string_view shipPropellantName(ShipPropellant);

// SI coordinates in the destroyed ship's home frame. Presentation never
// changes the damage ledger, nearby ships, collision or salvage contents.
struct ShipExplosion {
    ActivityId source;
    glm::dvec3 position{}, velocity{};
    glm::dquat orientation{1,0,0,0};
    double radius = 32., age = 0.;
    uint64_t seed = 0;
    ShipPropellant propellant = ShipPropellant::KeroseneLox;
};
enum class ExplosionParticleKind { Fire, Fragment, Ember, Vapour };
struct ExplosionParticle {
    glm::dvec3 offset{}, velocity{}, axis{0,1,0};
    glm::vec3 colour{};
    double radius = 0., spin = 0.;
    float opacity = 0.;
    ExplosionParticleKind kind = ExplosionParticleKind::Fire;
};
struct ShipExplosionImage {
    std::array<ExplosionParticle,204> particles{};
    size_t count = 0;
    double coreRadius = 0., haloRadius = 0., shockRadius = 0.;
    float flash = 0., afterglow = 0., shock = 0.;
    glm::vec3 coreColour{}, haloColour{}, shockColour{};
};
ShipExplosionImage sampleShipExplosion(const ShipExplosion&);
} // namespace infinity
