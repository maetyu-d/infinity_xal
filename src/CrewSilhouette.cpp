#include "CrewSilhouette.hpp"
#include "infinity/Core.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace infinity {
namespace {
constexpr unsigned width=crewSpriteWidth, height=crewSpriteHeight;
constexpr double distanceRange=7.;

struct SpritePainter {
    std::array<uint8_t,width*height> distance{};
    double heading;

    glm::dvec2 project(glm::dvec3 p) const {
        return {width*.5+(p.x*std::cos(heading)-p.z*std::sin(heading))*crewSpriteTexelsPerMetre,
                height-crewSpriteGroundMargin-p.y*crewSpriteTexelsPerMetre};
    }

    // Keep the boundary of the union so a single line follows the whole pose.
    void stroke(glm::dvec3 a,glm::dvec3 b,double top,double bottom) {
        const auto from=project(a),to=project(b),axis=to-from;
        const double length2=glm::dot(axis,axis);
        const double r=std::max(top,bottom)*crewSpriteTexelsPerMetre+distanceRange;
        const int x0=std::max(0,int(std::floor(std::min(from.x,to.x)-r)));
        const int y0=std::max(0,int(std::floor(std::min(from.y,to.y)-r)));
        const int x1=std::min(int(width),int(std::ceil(std::max(from.x,to.x)+r)));
        const int y1=std::min(int(height),int(std::ceil(std::max(from.y,to.y)+r)));
        for (int y=y0;y<y1;++y) for (int x=x0;x<x1;++x) {
            const glm::dvec2 pixel(x+.5,y+.5);
            const double t=length2>1e-9?std::clamp(glm::dot(pixel-from,axis)/length2,0.,1.):0.;
            const double edge=std::lerp(top,bottom,t)*crewSpriteTexelsPerMetre-glm::length(pixel-from-axis*t);
            const auto encoded=uint8_t(std::lround(std::clamp(.5+edge/(2.*distanceRange),0.,1.)*255));
            const size_t at=size_t(y)*width+x;
            distance[at]=std::max(distance[at],encoded);
        }
    }
};

SpritePainter paint(double heading,unsigned frame) {
    SpritePainter sprite{{},heading};
    const bool walk=frame<8, working=frame>=8;
    const double beat=frame*std::numbers::pi/4.;
    const double bob=walk?.027*(1.-std::cos(2*beat)):.0;
    const double lean=walk?-.038:0.;
    const double shoulder=.195;
    sprite.stroke({0,.91+bob,0},{0,1.32+bob,lean},.137,.161);
    sprite.stroke({-.143,1.344+bob,lean},{.143,1.344+bob,lean},.080,.080);
    sprite.stroke({0,1.395+bob,lean},{0,1.49+bob,lean},.076,.063);
    sprite.stroke({0,1.535+bob,lean},{0,1.655+bob,lean+.014},.102,.089);
    sprite.stroke({0,1.17+bob,.094},{0,1.34+bob,.101},.107,.101);
    for (double side : {-1.,1.}) {
        const double swing=walk?std::sin(beat)*side:0.;
        const double lift=walk?std::max(0.,-std::cos(beat)*side)*.145:0.;
        const glm::dvec3 hip(side*.083,.89+bob,0), knee(side*.092,.47+lift*.3,swing*.12-.026);
        const glm::dvec3 ankle(side*.105,.094+lift,swing*.205);
        sprite.stroke(hip,knee,.090,.065);
        sprite.stroke(knee,ankle,.066,.050);
        sprite.stroke(ankle+glm::dvec3(0,-.031,.035),ankle+glm::dvec3(0,-.031,-.111),.053,.050);
        const double task=working&&side>0?.18+.045*std::sin(beat):0.;
        const glm::dvec3 upper(side*shoulder,1.34+bob,lean);
        const glm::dvec3 elbow(side*.225,1.105+bob+task,lean-swing*.10-task*.52);
        const glm::dvec3 hand(side*.222,.875+bob+task*1.52,lean-swing*.183-task*1.38-.026);
        sprite.stroke(upper,elbow,.067,.049);
        sprite.stroke(elbow,hand,.050,.036);
        sprite.stroke(hand,hand+glm::dvec3(0,-.037,-.009),.041,.027);
    }
    return sprite;
}
}

std::vector<uint8_t> crewSilhouettePixels() {
    constexpr unsigned atlasWidth=width*crewSpriteFrames,atlasHeight=height*crewSpriteDirections;
    std::vector<uint8_t> result(atlasWidth*atlasHeight);
    for(unsigned dir=0;dir<crewSpriteDirections;++dir) for(unsigned frame=0;frame<crewSpriteFrames;++frame) {
        const auto sprite=paint(dir*2*std::numbers::pi/crewSpriteDirections,frame);
        for(unsigned y=0;y<height;++y) {
            const size_t dest=size_t(dir*height+y)*atlasWidth+frame*width;
            std::copy_n(sprite.distance.data()+size_t(y)*width,width,result.data()+dest);
        }
    }
    return result;
}
}
