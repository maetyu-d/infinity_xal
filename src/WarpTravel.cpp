#include "infinity/WarpTravel.hpp"
#include <algorithm>
#include <cmath>

namespace infinity {
double warpExitEstimate(double range, double drop, double closing, double limit, double offset) {
    if (!std::isfinite(range) || !std::isfinite(drop) || !std::isfinite(closing) ||
        !std::isfinite(limit) || range < 0 || drop <= 0 || limit <= 0)
        return -1;
    if (range <= drop)
        return 0;
    if (closing <= 1)
        return -1;
    if (offset >= 0 && std::isfinite(offset)) {
        // A clearance-limited envelope falls as we approach the body. Continue
        // the measured fractional closing rate instead of assuming today's
        // speed cap will remain constant all the way to the drop boundary.
        const double rate = std::min(1., closing / (range + offset));
        return std::min(1e12, std::log((range + offset) / (drop + offset)) / rate);
    }
    // Integrate the route's capped proportional deceleration, rather than
    // dividing distance by instantaneous velocity throughout the braking leg.
    const double cap = std::min(limit, closing * 1.05);
    const double bend = cap / .45;
    double seconds = std::max(0., range - std::max(drop, bend)) / cap;
    const double near = std::min(range, bend);
    if (near > drop)
        seconds += std::log(near / drop) / .45;
    return std::min(seconds, 1e12);
}
double warpDestinationReveal(double range, double drop, double radius) {
    if (!std::isfinite(range) || !std::isfinite(drop) || !std::isfinite(radius) || drop <= 0 || radius <= 0)
        return 0;
    // About eight seconds of proportional braking, also allowing a large
    // body's silhouette to resolve before the last part of the approach.
    const double start = std::max(drop * 36., drop + radius * .4);
    const double t = std::clamp(std::log(std::max(drop, range) / drop) / std::log(start / drop), 0., 1.);
    return 1 - t * t * (3 - 2 * t);
}
double warpTunnelAperture(double reveal, double angularRadius, double fov) {
    if (!std::isfinite(reveal) || !std::isfinite(angularRadius) || !std::isfinite(fov) || fov <= 0)
        return .21;
    const double disc = std::tan(std::clamp(angularRadius, 0., 1.35)) /
                        std::tan(std::clamp(fov, .1, 2.8) * .5);
    return std::lerp(.21, std::clamp(disc * 1.14 + .10, .42, 1.4), std::clamp(reveal, 0., 1.));
}
void WarpOptics::Track::approach(double target,double response,double seconds) {
    const double error=value-target;
    const double slope=velocity+response*error;
    const double decay=std::exp(-response*seconds);
    value=target+(error+slope*seconds)*decay;
    velocity=(velocity-response*slope*seconds)*decay;
}

WarpOpticsSample WarpOptics::update(bool engaged,bool guided,double speed,double bore,double seconds) {
    if(std::isfinite(seconds)&&seconds>0.){
        const double pace=std::isfinite(speed)?std::log10(std::max(1.,speed)):0.;
        const double energy=smoothstep(3.7,7.3,pace);
        closure_.approach(engaged?1.:0.,engaged?5.6:guided?2.7:4.4,seconds);
        light_.approach(engaged?std::lerp(guided?.68:.28,1.,energy):0.,engaged?5.4:guided?2.9:4.1,seconds);
        // Settle toward the last optical target through drop while the field opens up.
        if(engaged&&std::isfinite(bore))
            boreTarget_=std::clamp(bore,.21,1.4);
        bore_.approach(boreTarget_,10.,seconds);
    }
    return {std::clamp(closure_.value,0.,1.),std::clamp(light_.value,0.,1.),std::clamp(bore_.value,.21,1.4)};
}
} // namespace infinity
