#define GLM_FORCE_INLINE
#include "../src/MicroExpeditionScenario.hpp"
using namespace infinity;
int main() {
    try {
        Universe u;
        Navigation nav(u);
        RegionCache cache(u);
        FlightController spatial(u, nav, cache);
        Pilot pilot(u, nav, spatial);
        MicroExpeditionScenario proof(u, nav, pilot);
        for (unsigned tick = 0; !proof.finished() && tick < 100000; ++tick) {
            cache.beginFrame();
            proof.advance(.1);
            cache.finishFrame();
            cache.wait();
            if (tick % 500 == 0)
                std::cout << "FLIGHT_TICK " << tick << " stage=" << proof.stage() << " / " << proof.label()
                          << " / speed=" << pilot.instruments().speed << " / hull=" << pilot.ship().hull
                          << " / size=" << pilot.instruments().hullMetres
                          << " / distance=" << proof.distance() << " / phase=" << pilot.instruments().phase
                          << std::endl;
        }
        if (!proof.finished())
            throw std::runtime_error("Flight expedition failed to complete");
        if (cache.stats().bytes > 32 * 1024 * 1024 || cache.stats().peakPending > 3)
            throw std::runtime_error("Flight expedition exceeded existing cache limits");
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
