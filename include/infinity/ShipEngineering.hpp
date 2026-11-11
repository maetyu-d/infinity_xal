#pragma once
#include "Core.hpp"
#include <array>
#include <iosfwd>
namespace infinity {
enum class PowerPreset : uint8_t { Automatic, Flight, Combat, Industrial };
enum class PlantBus : uint8_t { Life, Helm, Weapons, Industry, Drive, Count };
enum class EngineeringSupply : uint8_t { Fuel, Coolant, Oxygen, Count };
inline constexpr size_t plantBusCount = size_t(PlantBus::Count),
                        engineeringSupplyCount = size_t(EngineeringSupply::Count);
inline constexpr std::array<double, engineeringSupplyCount> engineeringCapacity{120, 40, 30};
struct EngineeringReceipts {
    std::array<uint64_t, engineeringSupplyCount> kg{};
};
struct PlantDemand {
    double throttle = 0, boost = 0, turn = 0, weapons = 0, drill = 0, drive = 0, shrink = 0, externalHeat = 0;
    double reactor = 1, cooling = 1, engineer = 1, airLeak = 0;
};
struct PlantReadout {
    PowerPreset preset = PowerPreset::Automatic;
    std::array<double, plantBusCount> authority{1, 1, 1, 1, 1};
    std::array<double, engineeringSupplyCount> stores = engineeringCapacity;
    double heat = 0, reserve = 1, load = 0, output = 0, requested = 0, cooling = 1, warningSeconds = -1;
    bool brownout = false, critical = false;
    std::string status;
};
// One ship-local source of power, thermal inertia and consumable state. No
// address, render magnification, universe seed or flight-control gains enter it.
class ShipPlant {
  public:
    PowerPreset preset = PowerPreset::Automatic;
    std::array<double, engineeringSupplyCount> stores = engineeringCapacity;
    EngineeringReceipts applied;
    double heat = 0, reserve = 1, flushCooldown = 0;
    void advance(const PlantDemand&, double seconds);
    const PlantReadout& readout() const {
        return view_;
    }
    double authority(PlantBus bus) const {
        return view_.authority[size_t(bus)];
    }
    bool flush();
    void reconcile(const EngineeringReceipts&);
    void refresh();
    std::string serialize() const;
    bool restore(std::string_view);

  private:
    PlantReadout view_;
    void step(const PlantDemand&, double seconds);
};
std::string_view powerPresetName(PowerPreset);
} // namespace infinity
