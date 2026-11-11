#pragma once
#include "Discovery.hpp"
#include "SignalBeam.hpp"
#include "VesselSystems.hpp"

namespace infinity {
class NpcCombat;
struct NpcPlayer;
bool shipSignalId(const DiscoveryId&);
std::vector<SignalSource> shipSignals(const Universe&, const NpcPlayer&, std::span<const ActivityBody>,
                                      const NpcCombat&, const VesselOperations&);
struct ShipSignalView {
    bool tactical = false, confirmed = false, scanning = false, jamming = false, radiating = false;
    double scan = 0, capacitor = 1;
    std::string status, emission, condition, systems, operations;
};
// Reuses VesselOperations' measured internal scans. Only the emitter capacitor
// lives here; no duplicate scan knowledge, NPC generation or resource rewards.
class ShipSignals {
  public:
    void update(const Universe&, const NpcPlayer&, const std::optional<ActivityBody>&, bool scan, bool jam,
                bool decodedCarrier, double power, double navigator, double dt, std::span<const ActivityBody>,
                const Geometry* = nullptr);
    bool scanning() const {
        return scanning_;
    }
    bool radiating() const {
        return scanning_ || beam_.strength > 0;
    }
    const SignalBeam* jammer() const {
        return beam_.strength > 0 ? &beam_ : nullptr;
    }
    ShipSignalView view(const std::optional<ActivityBody>&, const VesselOperations&, const VesselEngineering*,
                        double seconds) const;
    std::string serialize(uint64_t seed) const;
    bool restore(std::string_view, uint64_t seed);

  private:
    SignalBeam beam_;
    double capacitor_ = 1;
    bool scanning_ = false, cooling_ = false;
    std::string status_ = "PASSIVE / S SCAN / X JAM HOSTILE CARRIER";
};
} // namespace infinity
