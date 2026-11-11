#define GLM_FORCE_INLINE
#include "infinity/SignalTraffic.hpp"
#include "infinity/CombatActors.hpp"
#include "infinity/ShipSignals.hpp"
#include <algorithm>
#include <cmath>

namespace infinity {
namespace {
constexpr unsigned distressChannel = 0x10000000, reliefChannel = 0x20000000;
SignalSource beacon(const Universe& u, ActivityId id, unsigned channel, uint64_t episode) {
    SignalSource s;
    s.id = {id.home, channel + id.slot, episode};
    s.contact = id;
    s.descriptor = liveSignalDescriptor(u, s.id);
    s.seed = mix64(s.descriptor);
    // Separate stream: existing natural/archive frequencies and seeds stay intact.
    s.frequency = 65 + 2.5 * (s.seed % 365);
    s.bandwidth = 8;
    s.period = 1.2;
    s.reach = 8e9;
    s.planetRadius = std::pow(10., u.describe(id.home).frame.logRadius);
    return s;
}
} // namespace
bool liveSignalId(const DiscoveryId& id) {
    if (id.planet.kind() != Kind::Planet)
        return false;
    const auto channel = id.channel & 0xf0000000u;
    return (id.channel & 0x0fffffffu) <= 1000000 &&
           ((channel == distressChannel && id.episode == 0) || (channel == reliefChannel && id.episode != 0) ||
            shipSignalId(id));
}
uint64_t liveSignalDescriptor(const Universe& u, const DiscoveryId& id) {
    return mix64(u.describe(id.planet).seed ^ mix64(id.channel) ^ mix64(id.episode) ^ 0x524144494f3636ULL);
}
std::vector<SignalSource> trafficSignals(const Universe& u, const EconomySnapshot& economy,
                                         std::span<const WeaponDamage> damage, double seconds) {
    std::vector<SignalSource> result;
    result.reserve(32);
    if (!economy.network || economy.catchingUp || !std::isfinite(seconds) || seconds < 0)
        return result;
    // Accepted work first, then open work, then retained receipts. Old closed
    // offers cannot crowd new emergencies out of the bounded receiver catalog.
    for (unsigned pass = 0; pass < 3 && result.size() < 16; ++pass)
        for (const auto& job : economy.recoveries) {
            const bool open = job.phase == RecoveryPhase::Open,
                       accepted = job.phase == RecoveryPhase::Accepted;
            if ((pass == 0 && !accepted) || (pass == 1 && !open) || (pass == 2 && (open || accepted)))
                continue;
            if (job.site >= economy.network->sites.size() || (!job.escrow && pass < 2))
                continue;
            const auto& site = economy.network->sites[job.site];
            if (site.id.slot > 1000000)
                continue;
            auto s = beacon(u, site.id, reliefChannel, job.id);
            s.kind = SignalKind::Relief;
            s.metres = site.metres;
            s.offer = job.id;
            s.accepted = accepted;
            s.broadcasting = (open || accepted) && economy.tick <= job.deadline;
            s.worldLabel = site.name;
            s.signature = (job.kind == RecoveryKind::Relief        ? "RELIEF / "
                           : job.kind == RecoveryKind::Maintenance ? "REPAIR / "
                                                                   : "RESTART / ") +
                          site.name;
            s.transmission[0] = job.phase == RecoveryPhase::Complete  ? "SUPPLIES RECEIVED / JOB COMPLETE"
                                : job.phase == RecoveryPhase::Working ? "SUPPLIES RECEIVED / CREW WORKING"
                                : !s.broadcasting                     ? "OFFER CLOSED / NO FURTHER RESPONSE"
                                : accepted                            ? "YOUR RESPONSE ACKNOWLEDGED"
                                           : "FUNDED REQUEST / R ACCEPT OVER RADIO";
            std::string needs;
            for (size_t g = 0; g < goodCount; ++g)
                if (job.required[g] > job.delivered[g]) {
                    if (!needs.empty())
                        needs += " / ";
                    needs += std::to_string(job.required[g] - job.delivered[g]) + "KG " +
                             std::string(goodInfo(Good(g)).name);
                }
            s.transmission[1] = needs.empty() ? "NO FURTHER MATERIAL REQUIRED" : needs;
            s.transmission[2] =
                s.broadcasting
                    ? std::to_string((job.deadline - economy.tick) * uint64_t(economyTickSeconds)) +
                          "S LEFT / J PORT / F2 JOBS TO DELIVER"
                    : "F5 OUTCOMES / LOCAL ECONOMY UPDATED";
            result.push_back(std::move(s));
            if (result.size() >= 16)
                break;
        }
    for (const auto& d : damage) {
        if (result.size() >= 32)
            break;
        if (d.id.home.parent() != economy.network->system || d.id.slot > 1000000 ||
            (d.hull > 0 && d.systems[size_t(ShipSystem::Engines)] > 0))
            continue;
        const auto body = sampleCombatActivity(u, d.id, seconds);
        if (body.hostile || (body.kind != ActivityKind::Freighter && body.kind != ActivityKind::Ferry &&
                             body.kind != ActivityKind::Survey))
            continue;
        const auto receipt = std::find_if(economy.wrecks.begin(), economy.wrecks.end(),
                                          [&](const auto& w) { return w.id == d.id; });
        auto s = beacon(u, d.id, distressChannel, 0);
        s.kind = SignalKind::Distress;
        s.metres = d.position;
        s.worldLabel = body.name;
        s.signature = "DISTRESS / " + body.name;
        s.transmission[0] =
            d.hull > 0 ? "DRIVE OFFLINE / ADRIFT / ASSISTANCE NEEDED" : "HULL LOST / EMERGENCY TRANSPONDER";
        s.transmission[1] = "SURVIVORS UNCONFIRMED / APPROACH TO ASSESS";
        s.transmission[2] = "J APPROACH / HOLD 0 RECOVER / CTRL-0 BOARD";
        if (receipt != economy.wrecks.end()) {
            const bool life = receipt->survivor && !receipt->rescued;
            s.transmission[1] = life               ? "SURVIVOR / " + receipt->survivor->name
                                : receipt->rescued ? "SURVIVOR SAFELY ABOARD YOUR SHIP"
                                                   : "NO SURVIVOR CONFIRMED / FINITE SALVAGE";
            s.broadcasting =
                life ||
                std::any_of(receipt->scrap.begin(), receipt->scrap.end(), [](auto kg) { return kg > 0; }) ||
                (d.hull > 0 && receipt->fieldRepairAt < 0);
            if (life && receipt->rescueDeadline > economy.tick)
                s.transmission[1] +=
                    " / " +
                    std::to_string((receipt->rescueDeadline - economy.tick) * uint64_t(economyTickSeconds)) +
                    "S";
            if (!s.broadcasting)
                s.transmission[2] = "RECOVERY COMPLETE / BEACON SILENT";
        }
        result.push_back(std::move(s));
    }
    return result;
}
std::optional<EconomicCommand> signalResponse(const DiscoveryController& receiver, const EconomySnapshot& s) {
    const auto offer = receiver.offer();
    if (!offer || !s.network || s.catchingUp)
        return {};
    const auto site = s.siteIndex(offer->first);
    if (!site)
        return {};
    const auto job = std::find_if(s.recoveries.begin(), s.recoveries.end(),
                                  [&](const auto& j) { return j.id == offer->second && j.site == *site; });
    if (job == s.recoveries.end() || job->phase != RecoveryPhase::Open || s.tick > job->deadline ||
        job->escrow <= 0)
        return {};
    EconomicCommand command;
    command.port = offer->first;
    command.contract = offer->second;
    command.action = EconomicAction::AcceptRecovery;
    return command;
}
} // namespace infinity
