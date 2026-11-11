#include "infinity/Geometry.hpp"
#include "infinity/SurfaceShading.hpp"
#include "infinity/World.hpp"
#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
using namespace infinity;
namespace {
void check(bool b, const char* m) {
    if (!b)
        throw std::runtime_error(m);
}
} // namespace
int main() {
    try {
        Universe u;
        GeometryGenerator generator(u);
        const auto rock =
            u.describe(Address::parse("0,0,0/1:0/2:9/3:0/4:61516819435380/5:4/6:4611686018427387905"));
        const auto grain = u.describe(u.children(rock, 1).front());
        const auto lattice = u.describe(u.children(grain, 1).front());
        for (const auto& child : {grain, lattice}) {
            const auto parent = u.describe(child.address.parent());
            const auto identity = descriptorFingerprint(parent);
            const int bucket = sectionBucket(child.frame.logRadius + .7);
            const auto legacy = generator.section(parent, child.address, bucket);
            const auto legacyHash = geometryFingerprint(*legacy);
            std::cout << "SECTION_REFERENCE " << kindName(parent.address.kind())
                      << " vertices=" << legacy->solid.vertices.size() << " bytes=" << legacy->residentBytes()
                      << " generation_ms=" << legacy->generationMs << '\n';
            double fineMs = 0;
            for (unsigned detail : {1u, 2u}) {
                const auto g = generator.section(parent, child.address, bucket, nullptr, detail);
                check(g && g->section && g->address == parent.address && g->referenceFrame == child.address &&
                          g->seed == parent.seed && g->descriptor == identity,
                      "Section lost its enclosing material identity");
                check(g->terrainNormals.size() == g->solid.vertices.size() &&
                          g->exposureColors.size() == g->solid.vertices.size(),
                      "Refined cutaway missing field lighting");
                check(g->solid.vertices.size() > 1000 && g->solid.vertices.size() < 60000 &&
                          g->residentBytes() < 4 * 1024 * 1024,
                      "Section exceeded bounded presentation budget");
                double cavityError = 0;
                size_t cavityVertices = 0;
                for (size_t i = 0; i < g->solid.vertices.size(); ++i) {
                    const auto& v = g->solid.vertices[i];
                    const glm::dvec3 p(v.x, v.y, v.z);
                    const double length = glm::length(p);
                    check(std::isfinite(length) && length >= g->inspectionRadius - 3e-6,
                          "Refined section intrudes into the inspection volume");
                    const auto normal = g->terrainNormals[i];
                    check(std::isfinite(glm::length(normal)) && std::abs(glm::length(normal) - 1) < 1e-5,
                          "Section normal is not finite and normalized");
                    if (std::abs(length - g->inspectionRadius) < 3e-6 &&
                        glm::dot(glm::dvec3(normal), -p / length) > .995) {
                        cavityError = std::max(cavityError, std::abs(length - g->inspectionRadius));
                        ++cavityVertices;
                    }
                }
                check(cavityVertices > 1000 && cavityError < 3e-6,
                      "Cavity curvature retains coarse false facets");
                for (size_t i = 0; i < g->solid.indexCount(); ++i)
                    check(g->solid.index(i) < g->solid.vertices.size(), "Invalid section topology");
                check(geometryFingerprint(*g) == geometryFingerprint(*generator.section(
                                                     parent, child.address, bucket, nullptr, detail)),
                      "Refined section depends on request order");
                fineMs = g->generationMs;
                std::cout << "SECTION " << kindName(parent.address.kind()) << " detail=" << detail
                          << " vertices=" << g->solid.vertices.size() << " bytes=" << g->residentBytes()
                          << " generation_ms=" << fineMs << " max_cavity_error=" << cavityError << '\n';
            }
            check(geometryFingerprint(*generator.section(parent, child.address, bucket)) == legacyHash &&
                      descriptorFingerprint(u.describe(parent.address)) == identity,
                  "Refinement changed original geometry recipe or geology");
            std::atomic<bool> cancelled{true};
            check(!generator.section(parent, child.address, bucket, &cancelled, 2),
                  "Cancelled section was published");
            RegionCache cache(u, 8 * 1024 * 1024, 2);
            cache.beginFrame();
            cache.requestSection(parent, child.address, bucket, 1);
            cache.finishFrame();
            cache.wait();
            cache.beginFrame();
            cache.requestSection(parent, child.address, bucket, 2);
            cache.finishFrame();
            cache.wait();
            const auto cached = cache.requestSection(parent, child.address, bucket, 2);
            check(cached && cached->detail == 2 && cache.stats().bytes <= 8 * 1024 * 1024 &&
                      cache.stats().peakPending <= 2,
                  "Refined sections bypassed asynchronous cache bounds");
        }
        for (double log : {-10., -8.5, -1.5, 1.}) {
            const glm::dvec3 eye(.17, .12, 1.1);
            const double physical = sectionViewScale(log, eye);
            for (double shift : {-1., 1.})
                check(std::abs(sectionViewScale(log + shift, eye * std::pow(10., -shift)) - physical) < 1e-12,
                      "Render-unit rebase changed physical section scale");
            int bucket = sectionBucket(physical);
            for (unsigned i = 0; i < 1000; ++i) {
                const double jitter = (i % 2 ? 1 : -1) * 1e-5;
                const auto next = sectionViewBucket(physical + jitter, bucket);
                if (i > 1)
                    check(next == bucket, "Stationary section oscillates at a cache boundary");
                bucket = next;
            }
        }
        check(sectionPresentationDetail(480, 0) == 1 && sectionPresentationDetail(1080, 1) == 2 &&
                  sectionPresentationDetail(750, 2) == 2 && sectionPresentationDetail(750, 1) == 1,
              "Section screen-space detail has no hysteresis");
        float previous = 1;
        for (unsigned i = 0; i <= 1000; ++i) {
            const auto fade = sectionCoverage(i / 1000., 1);
            check(std::isfinite(fade) && fade >= 0 && fade <= previous && previous - fade < .011,
                  "Obsolete section has an abrupt or invalid coverage edge");
            previous = fade;
        }
        check(sectionCoverage(.7, 1) == 1 && sectionCoverage(1, 1) == 0, "Section wall can occlude the eye");
        std::cout << "PASS refined physical material windows, smooth cavity normals, stable handovers, "
                     "cancellation and bounded cache\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
