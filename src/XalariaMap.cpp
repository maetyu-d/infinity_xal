#include "infinity/Xalaria.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinity {
XalariaMapLayout::XalariaMapLayout(double aspect):width(900*aspect){
    const double sidebar=std::clamp(width*.32,290.,440.);
    chart={24,154,width-sidebar-72,554};details={chart.x+chart.w+24,130,sidebar,578};
    search={details.x,132,sidebar,32};results={details.x,172,sidebar,100};
    galaxy={24,87,112,34};system={144,87,112,34};
    locate={274,87,100,34};fitRoute={382,87,112,34};fitGalaxy={502,87,112,34};
    economy={626,87,112,34};
    fly={24,802,230,38};pause={266,802,174,38};cancel={452,802,140,38};
    wormhole={604,802,222,38};
}
glm::dvec2 xalariaMapPoint(const XalariaMapView& v,glm::dvec2 grid,double aspect){
    const auto r=XalariaMapLayout(aspect).chart;
    auto delta=grid-v.centre;
    if(v.span<150)for(int i=0;i<2;++i)delta[i]-=std::round(delta[i]/v.catalog->grid)*v.catalog->grid;
    return glm::dvec2(r.x+r.w/2,r.y+r.h/2)+delta*(r.h/v.span);
}
glm::dvec2 xalariaMapPoint(const XalariaMapView& v,uint32_t seed,double aspect){return xalariaMapPoint(v,{double(seed%v.catalog->grid),double(seed/v.catalog->grid)},aspect);}
glm::dvec2 xalariaMapGrid(const XalariaMapView& v,glm::dvec2 canvas,double aspect){
    const auto r=XalariaMapLayout(aspect).chart;
    return v.centre+(canvas-glm::dvec2(r.x+r.w/2,r.y+r.h/2))*(v.span/r.h);
}
bool xalariaMapContains(glm::dvec2 p,double aspect){return XalariaMapLayout(aspect).chart.contains(p);}
std::optional<uint32_t> xalariaPickStar(const XalariaMapView& v,glm::dvec2 p,double aspect){
    if(!xalariaMapContains(p,aspect))return {};
    std::optional<uint32_t> found;double best=12;
    for(const auto& [id,s]:v.catalog->stars){const auto point=xalariaMapPoint(v,id,aspect);if(!xalariaMapContains(point,aspect))continue;
        const double d=glm::length(p-point);if(d<best){best=d;found=id;}}
    return found;
}
void xalariaZoomMap(XalariaMapView& v,double wheel,glm::dvec2 canvas,double aspect){
    const auto before=xalariaMapGrid(v,canvas,aspect);
    v.span=std::clamp(v.span*std::exp(-wheel*.16),8.,230.);
    v.centre+=before-xalariaMapGrid(v,canvas,aspect);
}
void xalariaFitRoute(XalariaMapView& v,double aspect){
    const auto& path=(v.flying||v.paused)&&!v.activeRoute.empty()?v.activeRoute:v.route;
    if(path.empty())return;
    glm::dvec2 lo=v.ship,hi=v.ship;
    for(auto id:path){const glm::dvec2 p{double(id%v.catalog->grid),double(id/v.catalog->grid)};lo=glm::min(lo,p);hi=glm::max(hi,p);}
    const auto r=XalariaMapLayout(aspect).chart;v.centre=(lo+hi)*.5;
    v.span=std::clamp(std::max((hi.x-lo.x+8)*r.h/r.w,hi.y-lo.y+8),8.,230.);
}
namespace {
glm::dvec3 worldPosition(const XalariaStar& s,const XalariaWorld& w){
    const double a=w.period>0?std::fmod(1700000000./w.period,1.)*2*std::numbers::pi:0;
    glm::dvec3 p{w.distance*std::cos(a),w.distance*w.tilt*std::sin(a+w.tiltRotation),w.distance*std::sin(a)};
    if(w.primary>=0)p+=worldPosition(s,s.worlds.at(uint64_t(w.primary)));
    return p;
}
glm::dvec2 radialPoint(const XalariaStar& s,glm::dvec3 position,const XalariaMapRect& r){
    double minimum=1e30,maximum=1;
    for(const auto& [id,w]:s.worlds)if(w.primary<0){minimum=std::min(minimum,w.distance);maximum=std::max(maximum,w.distance);}
    const double d=std::max(1.,glm::length(glm::dvec2(position.x,position.z)));
    const double f=maximum>minimum*1.001?.20+.72*std::log(d/minimum)/std::log(maximum/minimum):.64;
    const double radius=std::clamp(f,.05,1.1)*(std::min(r.w,r.h)*.5-44);
    const double a=std::atan2(position.z,position.x);
    return {r.x+r.w*.5+radius*std::cos(a),r.y+r.h*.5+radius*std::sin(a)};
}
}
glm::dvec2 xalariaSystemPoint(const XalariaMapView& v,uint64_t id,double aspect){
    const auto r=XalariaMapLayout(aspect).chart;const auto& s=v.catalog->star(v.selected);const auto& w=s.worlds.at(id);
    if(w.primary<0)return radialPoint(s,worldPosition(s,w),r);
    const auto p=xalariaSystemPoint(v,uint64_t(w.primary),aspect);
    const double a=w.period>0?std::fmod(1700000000./w.period,1.)*2*std::numbers::pi:0;
    const double reach=20+double(id&65535)*7;
    return p+glm::dvec2(std::cos(a),std::sin(a))*reach;
}
glm::dvec2 xalariaSystemShipPoint(const XalariaMapView& v,double aspect){
    const auto& s=v.catalog->star(v.selected);
    if(v.current==v.selected&&v.currentWorld&&s.worlds.contains(*v.currentWorld)){
        const auto& w=s.worlds.at(*v.currentWorld);
        const auto delta=v.systemShip-worldPosition(s,w);
        if(glm::length(delta)<w.radius*100){
            const glm::dvec2 offset{std::asinh(delta.x/w.radius)*4,std::asinh(delta.z/w.radius)*4};
            return xalariaSystemPoint(v,w.id,aspect)+offset;
        }
    }
    return radialPoint(s,v.systemShip,XalariaMapLayout(aspect).chart);
}
std::optional<uint64_t> xalariaPickWorld(const XalariaMapView& v,glm::dvec2 p,double aspect){
    if(!xalariaMapContains(p,aspect))return {};
    std::optional<uint64_t> found;double best=14;
    for(const auto& [id,w]:v.catalog->star(v.selected).worlds){const double d=glm::length(xalariaSystemPoint(v,id,aspect)-p);if(d<best){best=d;found=id;}}
    return found;
}
} // namespace infinity
