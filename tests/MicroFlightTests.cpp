#define GLM_FORCE_INLINE
#include "infinity/MicroView.hpp"
#include "infinity/Piloting.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace infinity;
namespace {
void check(bool b, const char* m) {
    if (!b)
        throw std::runtime_error(m);
}
void place(Navigation& nav, const Node& n, glm::dvec3 eye, glm::dquat q = {1, 0, 0, 0}) {
    std::ostringstream s;
    s.precision(17);
    const auto offset = eye - q * glm::dvec3(0, 0, 1);
    s << n.address.str() << ' ' << n.frame.logRadius << ' ' << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z
      << ' ' << offset.x << ' ' << offset.y << ' ' << offset.z;
    check(nav.restore(s.str()), "Micro flight fixture invalid");
}
} // namespace
int main() {
    try {
        Universe u;
        const auto rock =
            u.describe(Address::parse("0,0,0/1:0/2:9/3:0/4:61516819435380/5:4/6:4611686018427387905"));
        const auto grain = u.describe(u.children(rock, 1).front());
        const auto lattice = u.describe(u.children(grain, 1).front());
        Navigation branch(u);
        place(branch, rock, {0, 0, 1.4});
        branch.select(lattice.address);
        branch.changeFrame(grain.address);
        check(branch.selected == lattice.address && branch.hasExplicitSelection(),
              "A precision rebase discarded the selected material lineage");
        const auto branchSave = branch.serialize();
        check(branch.restore(branchSave) && branch.hasExplicitSelection() &&
                  branch.selected == lattice.address,
              "Saved microscopic destination stopped guiding frame refinement");
        // Rebase real momentum and body rates with a banked hull. Render-unit
        // changes must not act as a ship resize, throttle input or angular impulse.
        Navigation nav(u);
        RegionCache cache(u);
        FlightController flight(u, nav, cache);
        Pilot pilot(u, nav, flight);
        place(nav, rock, {.1, .2, 1.4}, glm::angleAxis(.63, glm::normalize(glm::dvec3(.2, .5, 1))));
        pilot.resetMotion();
        pilot.deploySpecimenProbe(rock.address);
        pilot.setVelocity({.012, -.007, .003});
        HelmInput roll;
        roll.roll = .4;
        for (unsigned i = 0; i < 30; ++i)
            pilot.update(roll, .01);
        const auto initial = nav.anchor;
        const auto initialQ = nav.orientation;
        const auto angular = pilot.ship().angularVelocity;
        const auto velocity = pilot.ship().velocity;
        const auto hull = pilot.ship().hull;
        const auto size = pilot.ship().logSize;
        const auto eye = nav.cameraPosition();
        const auto log = nav.logDistance;
        check(std::abs(angular.z) > .05, "Tiny hull cannot roll");
        for (const auto& address : {grain.address, lattice.address, grain.address, rock.address, initial}) {
            const auto pose = u.relative(initial, address, log);
            nav.changeFrame(address);
            pilot.synchronizeFrame();
            check(glm::length(pilot.ship().velocity - pose.orientation * velocity) < 1e-12,
                  "Automatic coordinate frame lost or rescaled ship momentum");
            check(pilot.ship().angularVelocity == angular && pilot.ship().hull == hull &&
                      pilot.ship().logSize == size,
                  "Coordinate frame changed body roll, damage or physical size");
            check(glm::length(nav.cameraPosition() - (pose.position + pose.orientation * eye)) < 1e-10 &&
                      std::abs(glm::dot(nav.orientation, pose.orientation * initialQ)) > 1 - 1e-12,
                  "Micro frame change moved or turned the physical eye");
            const auto physicalEye = nav.cameraPosition() * std::pow(10., nav.logDistance);
            nav.setRenderScale(log - .7);
            pilot.synchronizeFrame();
            check(glm::length(nav.cameraPosition() * std::pow(10., nav.logDistance) - physicalEye) < 1e-10 &&
                      pilot.ship().logSize == size && pilot.ship().hull == hull,
                  "Magnification altered the ship instead of render units");
            nav.setRenderScale(log);
        }
        const auto savedNav = nav.serialize(), savedShip = pilot.serialize();
        Navigation resumed(u);
        check(resumed.restore(savedNav), "Moving micro camera failed to restore");
        FlightController resumedFlight(u, resumed, cache);
        Pilot resumedPilot(u, resumed, resumedFlight);
        check(resumedPilot.restore(savedShip) && resumedPilot.serialize() == savedShip,
              "Moving and rolling tiny hull did not round-trip exactly");

        // Below the existing continuum threshold, bulk material is no longer the
        // collision surface. It must not remain an asymptotic speed barrier.
        double lo = 0, hi = 1.1;
        for (unsigned i = 0; i < 60; ++i) {
            const double mid = (lo + hi) * .5;
            if (u.grainSdf(grain, {0, 0, mid}) < 0)
                lo = mid;
            else
                hi = mid;
        }
        Navigation surfaceNav(u);
        place(surfaceNav, grain, {0, 0, hi + 1e-9});
        RegionCache surfaceCache(u);
        FlightController surfaceFlight(u, surfaceNav, surfaceCache);
        Pilot surfacePilot(u, surfaceNav, surfaceFlight);
        surfacePilot.fitToView();
        surfacePilot.adjustSize(0);
        surfaceCache.beginFrame();
        surfacePilot.update({}, .01);
        surfaceCache.finishFrame();
        check(surfacePilot.instruments().driveSpeed > 1e-5 && surfacePilot.ship().hull == 100,
              "Sub-continuum ship still slows to a halt at an inactive bulk surface");
        auto planet = rock.address;
        while (planet.kind() > Kind::Planet)
            planet = planet.parent();
        const auto sky = glm::normalize(-u.relative(planet, rock.address, rock.frame.logRadius).position);
        place(surfaceNav, rock, sky * 3.1);
        surfacePilot.resetMotion();
        surfacePilot.fitToView();
        surfacePilot.adjustSize(0);
        surfacePilot.setVelocity({.001, 0, 0});
        surfacePilot.restoreFullSize();
        const double beforeTime = surfacePilot.ship().flightSeconds, beforeSize = surfacePilot.ship().logSize;
        const auto beforeEye = surfaceNav.cameraPosition() * std::pow(10., surfaceNav.logDistance);
        surfaceCache.beginFrame();
        surfacePilot.update({}, .1);
        surfaceCache.finishFrame();
        check(std::abs(surfacePilot.ship().flightSeconds - beforeTime - .1) < 1e-10 &&
                  surfacePilot.ship().logSize > beforeSize &&
                  glm::length(surfaceNav.cameraPosition() * std::pow(10., surfaceNav.logDistance) -
                              beforeEye) > 1e-6,
              "Regrowing in open space exhausted the substep budget and lost physical time/travel");

        // The outside of a small grain can still be inside its enclosing rock.
        // Growing there must check both original fields, even in grain coordinates.
        glm::dvec3 enclosed{};
        bool found = false;
        const auto toHost =
            glm::normalize(u.relative(rock.address, grain.address, grain.frame.logRadius).position);
        for (double distance : {2., 4., 8., 16., 24.}) {
            const auto p = toHost * distance;
            const auto host = u.pointRelative(grain.address, p, rock.address, rock.frame.logRadius);
            const auto planetPoint = u.pointRelative(grain.address, p, planet, 0);
            const auto body = u.describe(planet);
            const double altitude = glm::length(planetPoint) - std::pow(10., body.frame.logRadius) -
                                    u.terrainHeight(body, glm::normalize(planetPoint));
            if (u.grainSdf(grain, p) > .1 && u.rockSdf(rock, host) < -.001 && altitude > .05) {
                enclosed = p;
                found = true;
                break;
            }
        }
        check(found, "No real above-ground grain/host overlap for growth regression");
        place(surfaceNav, grain, enclosed);
        surfacePilot.resetMotion();
        surfacePilot.fitToView();
        surfacePilot.adjustSize(0);
        surfacePilot.restoreFullSize();
        for (unsigned i = 0; i < 20; ++i) {
            surfaceCache.beginFrame();
            surfacePilot.update({}, .1);
            surfaceCache.finishFrame();
            surfaceCache.wait();
        }
        check(surfacePilot.instruments().growthBlocked && surfacePilot.ship().hull == 100 &&
                  surfacePilot.instruments().hullMetres <=
                      material(grain.environment.mineral).cellMetres * .101,
              "Regrowing outside a grain passed through its enclosing rock");

        // A restart can precede the first asynchronous atomic upload. Reject a
        // stale externally supplied scene and retain the physical state until the
        // canonical collision neighbourhood is available.
        Navigation atomNav(u);
        RegionCache atomCache(u);
        FlightController atomFlight(u, atomNav, atomCache);
        place(atomNav, lattice, {0, 0, 3.1});
        Pilot atomPilot(u, atomNav, atomFlight);
        atomPilot.fitToView();
        atomPilot.setVelocity({0, 0, -1e-9});
        auto fake = GeometryGenerator(u).generate(lattice, 0);
        fake->descriptor ^= 1;
        const auto atomEye = atomNav.cameraPosition() * std::pow(10., atomNav.logDistance);
        const auto atomVelocity = atomPilot.ship().velocity;
        atomCache.beginFrame();
        atomPilot.update({}, .02, fake);
        atomCache.finishFrame();
        check(glm::length(atomNav.cameraPosition() * std::pow(10., atomNav.logDistance) - atomEye) < 1e-20 &&
                  atomPilot.ship().velocity == atomVelocity && atomPilot.ship().hull == 100,
              "Unresolved or stale atomic detail moved or damaged the ship");
        atomCache.wait();
        const auto geometry = atomFlight.materialCollisionGeometry();
        check(geometry && geometry->descriptor == descriptorFingerprint(lattice),
              "Collision warmup did not request canonical atoms");
        atomCache.beginFrame();
        atomPilot.update({}, .02);
        atomCache.finishFrame();
        check(glm::length(atomNav.cameraPosition() * std::pow(10., atomNav.logDistance) - atomEye) > 1e-15,
              "Atomic flight never resumed after cache warmup");
        check(std::isfinite(atomPilot.instruments().speed) && atomPilot.instruments().hullMetres > 0,
              "Atomic flight lost finite physical instruments");
        // Fast swept contact against a real generated atom must not pass through
        // it, or be protected by the presentation-only inspection cavity.
        const auto atom = geometry->lattice.atoms.front();
        const double units = std::pow(10., geometry->logUnits);
        place(atomNav, lattice, glm::dvec3(atom.position) + glm::dvec3(0, 0, atom.radius * 5));
        atomPilot.resetMotion();
        atomPilot.fitToView();
        atomPilot.adjustSize(0);
        atomPilot.toggleAssist();
        atomPilot.setVelocity({0, 0, -1e-6});
        for (unsigned i = 0; i < 8 && atomPilot.ship().hull == 100; ++i) {
            atomCache.beginFrame();
            atomPilot.update({}, .01, geometry);
            atomCache.finishFrame();
        }
        const auto gap = extractMicroView(u, atomNav, atomPilot.instruments().hullMetres, geometry.get());
        std::cout << "ATOMIC_CONTACT atoms=" << gap.atomsKnown << " hull=" << atomPilot.ship().hull
                  << " anchor=" << atomNav.anchor.str() << " speed=" << atomPilot.instruments().speed
                  << " phase=" << atomPilot.instruments().phase << " radius=" << atom.radius
                  << " ship_radius=" << atomPilot.collisionRadius() / units << std::endl;
        check(gap.atomsKnown && gap.proximityMetres >= 0 && atomPilot.ship().hull < 100,
              "Tiny fast impact did not collide with actual atoms and fragile hull");
        const auto local = atomNav.cameraPosition() * std::pow(10., atomNav.logDistance - geometry->logUnits);
        for (const auto& a : geometry->lattice.atoms)
            check(glm::length(local - glm::dvec3(a.position)) >=
                      a.radius + atomPilot.collisionRadius() / units - 2e-7,
                  "Swept atomic collision left the hull inside resolved matter");
        std::cout << "PASS microscopic roll, SI momentum/attitude across rebases, exact moving save, atomic "
                     "loading hold and fragile swept contact\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
