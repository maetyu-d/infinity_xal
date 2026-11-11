#define GLM_FORCE_INLINE
#include "infinity/Excavation.hpp"
#include "infinity/Piloting.hpp"
#include <algorithm>

namespace infinity {
ExcavationProfile excavationProfile(const Borehole& b, const CavernFind& f) {
    const auto frame = tangentFrame(glm::normalize(b.entry));
    const auto bedding = glm::inverse(frame) * b.geology.bedding;
    const auto fracture = glm::inverse(frame) * b.geology.fracture;
    const double slip = 1 - std::abs(glm::dot(bedding, f.inward));
    const double joint = 1 - std::abs(glm::dot(fracture, f.inward));
    const double softness = f.mineral == Mineral::Ice        ? .9
                            : f.mineral == Mineral::Graphite ? .8
                            : f.mineral == Mineral::Iron     ? .15
                                                             : .35;
    const double span = std::clamp(b.roomRadius() / 18., .5, 1.5);
    const double weakness = std::clamp(.08 + .31 * b.geology.fractureStrength + .17 * b.geology.sediment +
                                           .16 * slip + .09 * joint + .10 * span + .12 * softness,
                                       .1, .95);
    return {weakness, span, slip,
            f.mineral == Mineral::Ice ? "ICE SHEAR"
            : slip > .65              ? "BEDDING SLIP"
                                      : "JOINTED ROCK"};
}
double excavationCutDamage(const Borehole& b, const CavernFind& f, bool careful) {
    const double braces = 1 + 1.5 * b.excavation[f.index].supports;
    return (.08 + .12 * excavationProfile(b, f).weakness) * (careful ? .25 : 1.) / braces;
}
double excavationStrain(const Borehole& b, const CavernFind& f, double progress, bool careful) {
    return std::clamp(b.excavation[f.index].damage +
                          excavationCutDamage(b, f, careful) * std::clamp(progress, 0., 1.),
                      0., 1.);
}
bool advanceExcavation(Borehole& b, const CavernFind& f, bool careful, double seconds) {
    auto& s = b.excavation[f.index];
    s.damage += excavationCutDamage(b, f, careful);
    if (s.damage < 1)
        return false;
    s.damage = .30; // A local slab detaches; it does not heal the remaining joints.
    ++s.collapses;
    s.lastFall = seconds;
    if (s.supports) {
        --s.supports;
        ++s.buriedKits; // Recover the actual tool kit consumed when placing this brace.
    }
    return true;
}
glm::dvec3 cavernRubblePoint(const Borehole& b, const CavernFind& f) {
    const glm::dvec3 centre(0, -b.finalDepth(), 0);
    auto radial = f.local - centre;
    radial.y = -std::max(4., std::abs(radial.y));
    // Keep the shaft and the centre of the chamber free, including shallow blasts.
    const double horizontal = glm::length(glm::dvec2(radial.x, radial.z));
    if (horizontal < 3)
        radial.x += 4;
    radial = glm::normalize(radial);
    return centre + radial * (boreRoomRadius(b, radial) - 1.35);
}
double cavernRubbleRadius(const Borehole& b, const CavernFind& f) {
    const auto& s = b.excavation[f.index];
    return s.looseKg || s.buriedKits ? std::min(.85, .23 + std::cbrt(double(s.looseKg)) * .065) : 0.;
}
double cavernRubbleClearance(const Borehole& b, glm::dvec3 local) {
    double result = 1e30;
    if (std::none_of(b.excavation.begin(), b.excavation.end(),
                     [](const auto& s) { return s.looseKg || s.buriedKits; }))
        return result;
    for (const auto& f : cavernFinds(b)) {
        const double radius = cavernRubbleRadius(b, f);
        if (radius > 0)
            result = std::min(result, glm::length(local - cavernRubblePoint(b, f)) - radius);
    }
    return result;
}
namespace {
void strut(Mesh& mesh, glm::dvec3 from, glm::dvec3 to, double width, glm::vec3 colour) {
    const auto frame = tangentFrame(glm::normalize(to - from));
    const auto first = uint32_t(mesh.vertices.size());
    for (auto p : {from, to})
        for (auto corner :
             {glm::dvec3(-1, 0, -1), glm::dvec3(1, 0, -1), glm::dvec3(1, 0, 1), glm::dvec3(-1, 0, 1)}) {
            p += frame * corner * width;
            mesh.vertices.push_back({float(p.x), float(p.y), float(p.z), rgba(colour)});
            p -= frame * corner * width;
        }
    for (unsigned k = 0; k < 4; ++k) {
        const auto n = (k + 1) % 4;
        mesh.indices.insert(mesh.indices.end(),
                            {first + k, first + n, first + k + 4, first + n, first + n + 4, first + k + 4});
    }
    mesh.indices.insert(mesh.indices.end(),
                        {first, first + 2, first + 1, first, first + 3, first + 2, first + 4, first + 5,
                         first + 6, first + 4, first + 6, first + 7});
}
} // namespace
Mesh excavationFixtures(const Borehole& b) {
    Mesh mesh;
    for (const auto& f : cavernFinds(b)) {
        const auto& s = b.excavation[f.index];
        const auto basis = tangentFrame(f.inward);
        for (unsigned n = 0; n < s.supports; ++n) {
            const double x = (double(n) - double(s.supports - 1) * .5) * .7;
            const auto from = f.local + basis * glm::dvec3(x - .8, .12, -.9);
            const auto to = f.local + basis * glm::dvec3(x + .8, .6, .9);
            strut(mesh, from, to, .055, {.23f, .65f, .64f});
            strut(mesh, from + basis * glm::dvec3(1.6, 0, 0), to - basis * glm::dvec3(1.6, 0, 0), .045,
                  {.68f, .46f, .17f});
        }
        const double radius = cavernRubbleRadius(b, f);
        if (radius <= 0)
            continue;
        const auto point = cavernRubblePoint(b, f);
        // An octahedral ore clast inscribed in its conservative collision sphere.
        const auto first = uint32_t(mesh.vertices.size());
        for (auto axis : {glm::dvec3(1, 0, 0), glm::dvec3(-1, 0, 0), glm::dvec3(0, 1, 0),
                          glm::dvec3(0, -1, 0), glm::dvec3(0, 0, 1), glm::dvec3(0, 0, -1)}) {
            const auto p = point + axis * radius;
            const auto colour = material(f.mineral).color * float(.7 + .25 * axis.y);
            mesh.vertices.push_back({float(p.x), float(p.y), float(p.z), rgba(colour)});
        }
        for (auto tri : {glm::uvec3(0, 2, 4),
                         {4, 2, 1},
                         {1, 2, 5},
                         {5, 2, 0},
                         {4, 3, 0},
                         {1, 3, 4},
                         {5, 3, 1},
                         {0, 3, 5}})
            mesh.indices.insert(mesh.indices.end(), {first + tri.x, first + tri.y, first + tri.z});
        // The cyan tether/light identifies recoverable material, separate from cosmetic dust.
        strut(mesh, point + glm::dvec3(0, radius, 0), point + glm::dvec3(0, radius + .35, 0), .025,
              {.2f, 1.f, .82f});
    }
    return mesh;
}
ProbeMotes excavationMotes(const Borehole& b, const CavernFind& f, double strain, double seconds) {
    const auto& s = b.excavation[f.index];
    const double age = seconds - s.lastFall;
    const bool falling = s.lastFall >= 0 && age >= 0 && age < 2.4;
    if (!falling && strain < .5)
        return {};
    auto motes = iceFallMotes(b, {f, excavationProfile(b, f).weakness},
                              falling ? age : .15 + std::fmod(std::max(0., seconds), 1.2));
    const auto count = falling ? motes.count : std::min<size_t>(5, motes.count);
    motes.count = count;
    for (size_t i = 0; i < count; ++i) {
        auto& m = motes.items[i];
        m.colour = glm::mix(material(f.mineral).color, glm::vec3(.65f, .61f, .55f), .3f);
        m.radius *= falling ? 1 : .20;
        m.opacity *= falling ? 1 : .6;
    }
    return motes;
}
void annotateExcavation(FlightInstruments& t, const Borehole& b, const CavernFind& f,
                        const ExcavationWork& work, double engineer, double seconds) {
    const auto& s = b.excavation[f.index];
    const auto profile = excavationProfile(b, f);
    const double strain =
        excavationStrain(b, f, work.bore == b.id && work.face == f.index ? work.progress : 0, work.careful);
    const bool falling = s.lastFall >= 0 && seconds >= s.lastFall && seconds < s.lastFall + 2.4;
    t.weather = falling ? "LOCAL ROCKFALL / CLEAR FACE"
                        : std::string(profile.failure) + " / " + std::string(material(f.mineral).formula) +
                              " / LOAD " + std::to_string(int(strain * 100)) + "%";
    t.weatherDetail = std::to_string(s.supports) + " BRACES / " + std::to_string(s.looseKg) + " KG LOOSE / " +
                      std::to_string(s.buriedKits) + " KITS";
    t.weatherSeverity = falling || strain >= .75 ? 1 : strain >= .5 ? .5 : 0;
    if (falling)
        t.exploration[2] = "BACK AWAY / DETACHED ORE REMAINS RECOVERABLE";
    else if ((s.looseKg || s.buriedKits) && !t.exploration[2].starts_with("RECOVERING"))
        t.exploration[2] = "FACE MARKED DEPOSIT / HOLD 0 RECOVER ORE + KIT";
    else if (strain >= .5)
        t.exploration[2] = engineer > 0 ? "ENGINEER / BRACE OR USE A CAREFUL CUT"
                                        : "JOINTS OPENING / CAREFUL CUT OR BRACE / F9 CREW";
    if (b.finds[f.index].marked)
        t.exploration[3] = "ALT-CTRL-SPACE CAREFUL / ALT-0 BRACE / F1 HELP";
}
} // namespace infinity
