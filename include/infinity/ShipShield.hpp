#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace infinity {
inline constexpr double playerShieldCapacity=28.;
inline constexpr glm::vec3 shieldBlue(.42f,.75f,1.f);

struct ShieldReadout {
    double capacity=0.,fraction=0.,age=0.,pulse=0.;
    bool raised=false;
    glm::dvec3 impactDirection{0,0,-1};
};

struct ShieldCharge {
    double spent=0.,hitAt=0.,strength=0.;
    glm::dvec3 direction{0,0,-1};

    double remaining(double capacity,double seconds) const {
        const double refill=std::max(0.,seconds-hitAt-3.2)*capacity*.19;
        return std::clamp(capacity-spent+refill,0.,capacity);
    }

    double absorb(double load,double capacity,double seconds,glm::dvec3 incoming) {
        const double charge=remaining(capacity,seconds);
        const double absorbed=std::clamp(load,0.,charge);
        if(absorbed>0.){
            spent=capacity-charge+absorbed;
            hitAt=seconds;
            strength=std::clamp(.30+1.15*absorbed/capacity,.30,1.);
            if(glm::dot(incoming,incoming)>1e-18)
                direction=glm::normalize(incoming);
        }
        return absorbed;
    }

    ShieldReadout read(double capacity,double seconds,bool deployed) const {
        if(capacity<=0.)return {};
        const double charge=remaining(capacity,seconds);
        const double age=std::max(0.,seconds-hitAt);
        const double pulse=deployed?strength*std::pow(std::max(0.,1.-age/.96),2.):0.;
        return {capacity,charge/capacity,age,pulse,deployed,direction};
    }

    bool valid(double capacity,double seconds) const {
        const double length=glm::length(direction);
        return std::isfinite(spent)&&spent>=0.&&spent<=capacity+1e-8&&
               std::isfinite(hitAt)&&hitAt>=0.&&hitAt<=seconds&&
               std::isfinite(strength)&&strength>=0.&&strength<=1.&&std::abs(length-1.)<1e-6;
    }
};
} // namespace infinity
