#define GLM_FORCE_INLINE
#include "infinity/Discovery.hpp"
#include <algorithm>
#include <cmath>
namespace infinity {
std::optional<double> discoveryCollision(const Universe& u,const DiscoveryPage& page,const Address& reference,
        glm::dvec3 from,glm::dvec3 to,double radius){
    if(!std::isfinite(radius)||radius<0)return {};
    std::optional<double> hit;const double units=std::pow(10.,u.describe(reference).frame.logRadius);
    for(const auto& s:page.sources)if(s.relic){
        const auto a=reference==s.id.planet?from:u.pointRelative(reference,from/units,s.id.planet,0);
        const auto b=reference==s.id.planet?to:u.pointRelative(reference,to/units,s.id.planet,0);
        if(glm::length((a+b)*.5-s.relic->metres)>250+radius+glm::length(b-a)*.5)continue;
        for(const auto& block:s.relic->blocks){
            const auto inv=glm::inverse(block.orientation);
            const auto eye=inv*(a-block.metres),delta=inv*(b-a);
            const glm::dvec3 lo{-block.dimensions.x*.5-radius,-radius,-block.dimensions.z*.5-radius};
            const glm::dvec3 hi{block.dimensions.x*.5+radius,block.dimensions.y*1.1+radius,block.dimensions.z*.5+radius};
            double near=0,far=1;
            for(unsigned axis=0;axis<3;++axis){
                if(std::abs(delta[axis])<1e-20){if(eye[axis]<lo[axis]||eye[axis]>hi[axis]){near=2;break;}}
                else{auto first=(lo[axis]-eye[axis])/delta[axis],last=(hi[axis]-eye[axis])/delta[axis];if(first>last)std::swap(first,last);near=std::max(near,first);far=std::min(far,last);}
            }
            if(near<=far&&near>=0&&near<=1&&(!hit||near<*hit))hit=near;
        }
    }
    return hit;
}
} // namespace infinity
