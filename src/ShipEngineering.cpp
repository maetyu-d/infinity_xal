#define GLM_FORCE_INLINE
#include "infinity/ShipEngineering.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
namespace infinity {
std::string_view powerPresetName(PowerPreset p) {
    constexpr std::array<std::string_view, 4> names{"AUTO", "FLIGHT", "COMBAT", "WORK"};
    return unsigned(p) < names.size() ? names[unsigned(p)] : "AUTO";
}
void ShipPlant::refresh() {
    view_.preset = preset;
    view_.stores = stores;
    view_.heat = heat;
    view_.reserve = reserve;
    view_.critical = heat >= .92 || stores[0] <= 0 || reserve < .1;
    view_.status = heat >= .92      ? "THERMAL LIMIT / FLUSH COOLANT OR REDUCE LOAD"
                   : heat >= .72    ? "HEAT RISING / ENGINEER / SELECT PRIORITY"
                   : stores[0] <= 0 ? "REACTOR FUEL EMPTY / PROTECTED BATTERY"
                   : reserve < .1   ? "BATTERY RESERVE LOW / SHED OPTIONAL LOAD"
                   : stores[0] < 12 ? "LOW FUEL / PROPULSION RESERVE"
                   : stores[1] < 8  ? "COOLANT LOW / WATER REQUIRED"
                   : stores[2] < 5  ? "AIR RESERVE LOW / OXYGEN REQUIRED"
                   : view_.brownout ? "POWER LIMITED / LOAD SHEDDING"
                                    : "POWER AND COOLING NORMAL";
}
void ShipPlant::advance(const PlantDemand& d, double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0 || seconds > 10)
        return;
    for (double value : {d.throttle, d.boost, d.turn, d.weapons, d.drill, d.drive, d.shrink, d.externalHeat,
                         d.reactor, d.cooling, d.engineer, d.airLeak})
        if (!std::isfinite(value))
            return;
    while (seconds > 1e-10) {
        const double stepSeconds = std::min(.25, seconds);
        step(d, stepSeconds);
        seconds -= stepSeconds;
    }
}
void ShipPlant::step(const PlantDemand& in, double dt) {
    auto bounded = [](double x) { return std::clamp(x, 0., 1.); };
    const double throttle = bounded(in.throttle), weapons = bounded(in.weapons), drill = bounded(in.drill),
                 drive = bounded(in.drive), shrink = bounded(in.shrink);
    const std::array<double, plantBusCount> requested{
        6., 10 + 28 * throttle + 8 * bounded(in.boost) + 8 * bounded(in.turn), 32 * weapons,
        35 * drill + 12 * shrink, 54 * drive};
    const double total = std::accumulate(requested.begin(), requested.end(), 0.);
    double generated =
        stores[0] > 0 ? 120 * (.1 + .9 * bounded(in.reactor)) * (1 - .35 * smoothstep(.85, 1.05, heat)) : 0.;
    generated = std::min({generated, total, stores[0] * 30000 / dt});
    const double protectedReserve = stores[0] > 0 ? .25 : 0.;
    const double battery = std::min(
        {30., std::max(0., total - generated), std::max(0., reserve - protectedReserve) * 12000 / dt});
    reserve =
        std::clamp(reserve - battery * dt / 12000 + (total < 65 && stores[0] > 0 ? dt * .0008 : 0.), 0., 1.);
    stores[0] = std::max(0., stores[0] - generated * dt / 30000);
    double available = generated + battery;
    std::array<double, plantBusCount> granted{};
    std::array<unsigned, plantBusCount> order{0, 1, 2, 3, 4};
    if (preset == PowerPreset::Flight || (preset == PowerPreset::Automatic && drive > .5))
        order = {0, 1, 4, 2, 3};
    if (preset == PowerPreset::Industrial || (preset == PowerPreset::Automatic && drill > .1))
        order = {0, 1, 3, 2, 4};
    for (auto bus : order) {
        granted[bus] = std::min(available, requested[bus]);
        available -= granted[bus];
    }
    view_.brownout = false;
    for (size_t i = 0; i < plantBusCount; ++i) {
        view_.authority[i] = requested[i] > 0 ? granted[i] / requested[i] : 1.;
        view_.brownout |= view_.authority[i] < .95;
    }
    const double cooling = bounded(in.cooling) * (.15 + .85 * std::min(1., stores[1] / 8.)) *
                           (.7 + .3 * std::clamp(in.engineer, 0., 1.5));
    view_.cooling = cooling;
    const double gain = .0004 + .004 * drive * view_.authority[4] + .007 * drill * view_.authority[3] +
                        .003 * shrink + .004 * weapons * view_.authority[2] + .002 * throttle +
                        .025 * bounded(in.externalHeat);
    const double loss = .0006 + .0065 * cooling * view_.authority[0];
    heat = std::clamp(heat + (gain - loss) * dt, 0., 1.2);
    view_.warningSeconds = gain > loss && heat < .92 ? (.92 - heat) / (gain - loss) : -1;
    const double thermal = 1 - smoothstep(.78, .98, heat);
    view_.authority[2] *= thermal;
    view_.authority[3] *= thermal;
    if (heat >= .95)
        view_.authority[4] = 0;
    if (stores[2] <= 0)
        view_.authority[0] = 0;
    stores[1] = std::max(0., stores[1] - std::max(0., .55 - bounded(in.cooling)) * .015 * dt);
    stores[2] = std::max(0., stores[2] - std::clamp(in.airLeak, 0., .1) * dt);
    flushCooldown = std::max(0., flushCooldown - dt);
    view_.load = total / 120;
    view_.output = generated + battery;
    view_.requested = total;
    refresh();
}
bool ShipPlant::flush() {
    if (stores[1] < 2 || heat < .15 || flushCooldown > 0)
        return false;
    stores[1] -= 2;
    heat = std::max(0., heat - .25);
    flushCooldown = 10;
    refresh();
    return true;
}
void ShipPlant::reconcile(const EngineeringReceipts& r) {
    for (size_t i = 0; i < stores.size(); ++i)
        if (r.kg[i] >= applied.kg[i]) {
            stores[i] = std::min(engineeringCapacity[i], stores[i] + double(r.kg[i] - applied.kg[i]));
            applied.kg[i] = r.kg[i];
        }
    refresh();
}
std::string ShipPlant::serialize() const {
    std::ostringstream o;
    o << std::setprecision(17) << "PLANT 1 " << unsigned(preset) << ' ' << heat << ' ' << reserve << ' '
      << flushCooldown;
    for (auto x : stores)
        o << ' ' << x;
    for (auto x : applied.kg)
        o << ' ' << x;
    return o.str();
}
bool ShipPlant::restore(std::string_view text) {
    ShipPlant p;
    std::istringstream in{std::string(text)};
    std::string tag;
    unsigned version, mode;
    if (!(in >> tag >> version >> mode >> p.heat >> p.reserve >> p.flushCooldown) || tag != "PLANT" ||
        version != 1 || mode > 3 || !std::isfinite(p.heat) || p.heat < 0 || p.heat > 1.2 ||
        !std::isfinite(p.reserve) || p.reserve < 0 || p.reserve > 1 || !std::isfinite(p.flushCooldown) ||
        p.flushCooldown < 0 || p.flushCooldown > 10)
        return false;
    for (size_t i = 0; i < p.stores.size(); ++i)
        if (!(in >> p.stores[i]) || !std::isfinite(p.stores[i]) || p.stores[i] < 0 ||
            p.stores[i] > engineeringCapacity[i])
            return false;
    for (auto& x : p.applied.kg)
        if (!(in >> x) || x > 1000000000000ULL)
            return false;
    in >> std::ws;
    if (!in.eof())
        return false;
    p.preset = PowerPreset(mode);
    p.refresh();
    if (!p.stores[0])
        p.view_.authority = {p.reserve > 0 ? .5 : 0, p.reserve > 0 ? .4 : 0, 0, 0, 0};
    const double thermal=1-smoothstep(.78,.98,p.heat);
    p.view_.authority[size_t(PlantBus::Weapons)]*=thermal;
    p.view_.authority[size_t(PlantBus::Industry)]*=thermal;
    if(p.heat>=.95)p.view_.authority[size_t(PlantBus::Drive)]=0;
    if(p.stores[2]<=0)p.view_.authority[size_t(PlantBus::Life)]=0;
    *this = p;
    return true;
}
} // namespace infinity
