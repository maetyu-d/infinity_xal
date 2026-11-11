#pragma once
#include "MicroExpeditionScenario.hpp"
#include "infinity/Renderer.hpp"
#include <filesystem>
#include <set>

namespace infinity {
class MicroFlightValidation {
    MicroExpeditionScenario proof_;
    Navigation& nav_;
    Renderer& renderer_;
    unsigned hold_ = 1, stable_ = 0, age_ = 0;
    std::set<unsigned> captured_;
    bool done_ = false;
    bool captureStage() const {
        return std::set<unsigned>{0, 3, 6, 7, 8, 10, 11, 13}.contains(proof_.stage());
    }

  public:
    MicroFlightValidation(const Universe& u, Navigation& nav, Pilot& pilot, Renderer& renderer)
        : proof_(u, nav, pilot), nav_(nav), renderer_(renderer) {
        renderer_.setActivity(nullptr);
        renderer_.setCockpitValidation(true);
        renderer_.setEconomy(proof_.snapshot(), nullptr);
    }
    bool finished() const {
        return done_;
    }
    double clock() const {
        return proof_.clock();
    }
    void advance() {
        ++age_;
        if (hold_) {
            --hold_;
            return;
        }
        if (captureStage() && !captured_.contains(proof_.stage()))
            return;
        const auto prior = proof_.stage();
        // Accelerate the proof's simulation timeline, never the solver's dt.
        // Every displacement still runs the production bounded helm update.
        for (unsigned i = 0; i < 8 && proof_.stage() == prior; ++i)
            proof_.advance(.1, renderer_.submittedGeometry(nav_.anchor));
        if (proof_.stage() != prior) {
            stable_ = age_ = 0;
            hold_ = 12;
        }
        renderer_.setEconomy(proof_.snapshot(), nullptr);
    }
    void annotate(FlightInstruments& t) const {
        t.explorationActive = true;
        t.exploration = {"FLIGHT-CONTROLLED SEAM EXPEDITION", proof_.label(),
                         "CANONICAL ROCK / MINERAL / ATOMIC LINEAGE",
                         "REAL SHIP HELM / SAVED MARK / FINITE CARGO"};
    }
    void observe(const FrameStats& stats, const RegionCache& cache, const std::string& directory) {
        if (done_)
            return;
        if (age_ > 15000)
            throw std::runtime_error("Native micro flight capture stalled");
        if (proof_.finished() && captured_.contains(13) && hold_ == 0) {
            done_ = true;
            return;
        }
        if (!captureStage() || captured_.contains(proof_.stage()))
            return;
        const bool settled =
            stats.visibleObjects && !stats.lodTransitions && !stats.uploadDeferred && !cache.stats().pending;
        stable_ = settled ? stable_ + 1 : 0;
        if (stable_ < 12)
            return;
        if (proof_.stage() == 0 && !stats.cavernMeshes)
            return;
        if ((proof_.stage() == 11 || proof_.stage() == 13) && (!stats.terrainTiles || !stats.atmospheres))
            throw std::runtime_error(
                "Full-size return lost planetary terrain/atmosphere in material coordinates");
        if (proof_.stage() == 7 || proof_.stage() == 8) {
            const auto atoms = renderer_.submittedGeometry(proof_.lattice().address);
            if (!atoms || atoms->lattice.atoms.empty())
                return;
        }
        const auto root = std::filesystem::path(directory);
        std::filesystem::create_directories(root);
        renderer_.screenshot((root / ("micro-flight-" + std::to_string(proof_.stage()) + ".ppm")).string());
        std::cout << "MICRO_FLIGHT_CAPTURE " << proof_.stage() << " / " << proof_.label()
                  << " / triangles=" << stats.triangles << " / atoms=" << stats.atoms << std::endl;
        captured_.insert(proof_.stage());
        hold_ = 12;
    }
};
} // namespace infinity
