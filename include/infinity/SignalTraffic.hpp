#pragma once
#include "Discovery.hpp"
#include "Economy.hpp"

namespace infinity {
bool liveSignalId(const DiscoveryId&);
uint64_t liveSignalDescriptor(const Universe&, const DiscoveryId&);
// Adapts authoritative ledger jobs and persistent physical damage. No spawning,
// payments, cargo transfers, geometry changes or generator mutations occur here.
std::vector<SignalSource> trafficSignals(const Universe&, const EconomySnapshot&,
                                         std::span<const WeaponDamage>, double seconds);
// Revalidate a decoded offer against the latest ledger before queueing it.
std::optional<EconomicCommand> signalResponse(const DiscoveryController&, const EconomySnapshot&);
} // namespace infinity
