#pragma once
#include "Xalaria.hpp"
#include "Piloting.hpp"

namespace infinity {
class XalariaEconomy;
// Preview selections never change an in-flight route. The committed destination
// and its world are retained through browsing, pauses, reference changes and saves.
class XalariaNavigation {
  public:
    enum class Phase { Idle, Transit, BetweenLegs, FinalApproach, Paused, Arrived };
    XalariaNavigation(const Universe&,Navigation&,Pilot&);
    XalariaMapView view;
    void setEconomy(XalariaEconomy* economy) {economy_=economy;}
    void selectSystem(uint32_t);
    void selectWorld(uint64_t);
    void selectFromFlight(const Address&);
    void search(std::string);
    void nextMatch(int direction=1);
    bool fly();
    bool targetWormhole();
    bool advanceFlight(const HelmInput&, double seconds, const std::shared_ptr<const Geometry>& visible = {});
    void pause();
    bool resume();
    void cancel();
    void update(double seconds);
    FlightInstruments instruments() const;
    Phase phase() const {return phase_;}
    std::string serialize() const;
    bool restore(std::string_view);
  private:
    const Universe& universe_;
    Navigation& navigation_;
    Pilot& pilot_;
    const XalariaCatalog& catalog_;
    XalariaEconomy* economy_=nullptr;
    std::optional<std::array<XalariaWormholeMouth,2>> wormholeMouths_;
    Phase phase_=Phase::Idle,pausedPhase_=Phase::Transit;
    std::vector<uint32_t> route_;
    std::optional<uint64_t> world_;
    Address target_;
    size_t leg_=0;
    uint64_t arrival_=0;
    double legRange_=1,delay_=0;
    uint32_t plannedFrom_=UINT32_MAX,plannedTo_=UINT32_MAX;
    Address defaultWorld(uint32_t) const;
    const XalariaWormholeMouth* wormholeAt(uint32_t system) const;
    bool manualFlight() const;
    glm::dvec3 eyeInMetres(const Address&) const;
    bool crossWormhole(const XalariaWormholeMouth&, glm::dvec3 before, glm::dvec3 after);
    bool beginLeg();
    void refresh();
};
} // namespace infinity
