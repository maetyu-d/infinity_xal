#include "infinity/Activity.hpp"
#include "infinity/OrbitalDebris.hpp"
#include "infinity/StationLayout.hpp"
#include "infinity/Docking.hpp"
#include "infinity/XalariaTraffic.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinity {
namespace {
const glm::vec3 hull{.31, .39, .45}, trim{.48, .62, .68}, glass{.13, .37, .53}, glow{.45, .91, .88};
void tri(Mesh& m, glm::dvec3 a, glm::dvec3 b, glm::dvec3 c, glm::vec3 color, float alpha=1.f) {
    const auto n = glm::normalize(glm::cross(b - a, c - a));
    const float light = float(.54 + .46 * std::max(0., glm::dot(n, glm::normalize(glm::dvec3(.4, .8, -.5)))));
    const auto first = uint32_t(m.vertices.size());
    for (auto p : {a, b, c})
        m.vertices.push_back({float(p.x), float(p.y), float(p.z), rgba(color * light,alpha)});
    m.indices.insert(m.indices.end(), {first, first + 1, first + 2});
}
void box(Mesh& m, glm::dvec3 c, glm::dvec3 dimensions, glm::vec3 color, double yaw = 0., float alpha=1.f) {
    const auto h = dimensions * .5;
    std::array<glm::dvec3,8> p{{{-h.x,-h.y,-h.z},{h.x,-h.y,-h.z},{h.x,h.y,-h.z},{-h.x,h.y,-h.z},
                               {-h.x,-h.y,h.z},{h.x,-h.y,h.z},{h.x,h.y,h.z},{-h.x,h.y,h.z}}};
    const double cs=std::cos(yaw), sn=std::sin(yaw);
    for (auto& v : p)
        v = c + glm::dvec3(v.x*cs-v.z*sn,v.y,v.x*sn+v.z*cs);
    for (auto face : {std::array{0, 3, 2, 1}, std::array{4, 5, 6, 7}, std::array{0, 4, 7, 3},
                      std::array{1, 2, 6, 5}, std::array{3, 7, 6, 2}, std::array{0, 1, 5, 4}}) {
        tri(m, p[face[0]], p[face[1]], p[face[2]], color,alpha);
        tri(m, p[face[0]], p[face[2]], p[face[3]], color,alpha);
    }
}
// Chamfered plate with three cross sections along its thickness. The bevels
// catch a grazing light even when the panel texture has faded at a distance.
void armour(Mesh& mesh,glm::dvec3 centre,glm::dvec3 size,double bevel,glm::vec3 colour) {
    const auto h=size*.5;
    const double cut=std::min({bevel,h.x*.42,h.y*.62,h.z*.42});
    const std::array<glm::dvec2,8> outline{{
        {-h.x+cut,-h.z},{h.x-cut,-h.z},{h.x,-h.z+cut},{h.x,h.z-cut},
        {h.x-cut,h.z},{-h.x+cut,h.z},{-h.x,h.z-cut},{-h.x,-h.z+cut}
    }};
    const auto point=[&](size_t i,double y,bool inset) {
        const auto v=outline[i]*(inset?glm::dvec2(1-cut/h.x,1-cut/h.z):glm::dvec2(1));
        return centre+glm::dvec3(v.x,y,v.y);
    };
    const std::array levels{-h.y,-h.y+cut,h.y-cut,h.y};
    for (size_t i=0;i<outline.size();++i) {
        const auto next=(i+1)%outline.size();
        for (size_t band=1;band<levels.size();++band) {
            const auto a=point(i,levels[band-1],band==1);
            const auto b=point(next,levels[band-1],band==1);
            const auto c=point(next,levels[band],band==3);
            const auto d=point(i,levels[band],band==3);
            tri(mesh,a,d,b,colour);
            tri(mesh,b,d,c,colour);
        }
        tri(mesh,centre+glm::dvec3(0,h.y,0),point(next,h.y,true),point(i,h.y,true),colour);
        tri(mesh,centre-glm::dvec3(0,h.y,0),point(i,-h.y,true),point(next,-h.y,true),colour);
    }
}
void spar(Mesh& mesh,glm::dvec3 from,glm::dvec3 to,double width,glm::vec3 colour,float alpha=1.f) {
    const auto axis=glm::normalize(to-from);
    const auto across=glm::normalize(glm::cross(axis,std::abs(axis.y)>.95?glm::dvec3(0,0,1):glm::dvec3(0,1,0)))*width*.5;
    const auto up=glm::cross(across,axis);
    const std::array offsets{-across-up,across-up,across+up,-across+up};
    for (size_t i=0;i<offsets.size();++i) {
        const auto a=offsets[i],b=offsets[(i+1)%offsets.size()];
        tri(mesh,from+a,to+a,to+b,colour,alpha);
        tri(mesh,from+a,to+b,from+b,colour,alpha);
    }
}
// Open, double-wall engine bells. The luminous throat is recessed behind
// the rim, so an oblique approach reads the dark interior and metal lip.
void engineBell(Mesh& mesh,glm::dvec3 centre,double radius,double length,unsigned detail) {
    const unsigned sectors=detail>1?24:detail?16:12;
    const glm::vec3 casing(.28f,.32f,.34f),lip(.59f,.62f,.59f),inside(.035f,.048f,.057f);
    const auto point=[&](unsigned i,double r,double z){
        const double a=i*2*std::numbers::pi/sectors;
        return centre+glm::dvec3(std::cos(a)*radius*r,std::sin(a)*radius*r,z*length);
    };
    constexpr std::array<glm::dvec2,4> profile{{{.59,0.},{.70,.38},{1.,.89},{.99,1.}}};
    for(unsigned i=0;i<sectors;++i){
        const unsigned j=(i+1)%sectors;
        for(size_t band=1;band<profile.size();++band){
            const auto a=profile[band-1],b=profile[band];
            tri(mesh,point(i,a.x,a.y),point(j,a.x,a.y),point(j,b.x,b.y),band==3?lip:casing);
            tri(mesh,point(i,a.x,a.y),point(j,b.x,b.y),point(i,b.x,b.y),band==3?lip:casing);
        }
        const auto a=point(i,.99,1.),b=point(j,.99,1.),c=point(j,.84,.96),d=point(i,.84,.96);
        tri(mesh,a,b,c,lip);tri(mesh,a,c,d,lip);
        const auto throatA=point(i,.34,.28),throatB=point(j,.34,.28);
        tri(mesh,d,c,throatB,inside);tri(mesh,d,throatB,throatA,inside);
        tri(mesh,centre+glm::dvec3(0,0,length*.27),throatA,throatB,{.13f,.62f,.81f},.12f);
        if(detail && i%2==0)
            spar(mesh,point(i,.71,.37),point(i,1.012,.88),radius*.045,casing*.55f);
    }
}

void radiatorBank(Mesh& mesh,glm::dvec3 centre,glm::dvec2 size,unsigned detail){
    armour(mesh,centre,{size.x,.035,size.y},.012,{.12f,.16f,.19f});
    const unsigned count=detail>1?10:5;
    for(unsigned i=0;i<count;++i){
        const double z=centre.z-size.y*.43+size.y*.86*i/(count-1);
        box(mesh,{centre.x,centre.y+.026,z},{size.x*.90,.027,size.y*.032},{.30f,.33f,.34f});
    }
    for(double side:{-1.,1.})
        spar(mesh,centre+glm::dvec3(side*size.x*.44,.031,-size.y*.46),
                  centre+glm::dvec3(side*size.x*.44,.031,size.y*.46),.018,{.49f,.29f,.14f});
}

// Pressure vessels, retaining hoops and pipe unions are actual geometry.
// The same major assemblies exist at every LOD; only their roundness varies.
void serviceTank(Mesh& mesh,glm::dvec3 centre,double radius,double length,unsigned detail) {
    const unsigned sectors=detail>1?32:detail?20:12;
    const glm::vec3 shell(.47f,.49f,.46f),dark(.075f,.10f,.12f),copper(.45f,.25f,.105f);
    const auto point=[&](unsigned i,double r,double z){
        const double angle=i*2*std::numbers::pi/sectors;
        return centre+glm::dvec3(std::cos(angle)*radius*r,std::sin(angle)*radius*r,z*length);
    };
    constexpr std::array<glm::dvec2,6> profile{{{.25,-.50},{.76,-.46},{1.,-.32},{1.,.32},{.76,.46},{.25,.50}}};
    for(unsigned i=0;i<sectors;++i){
        const auto j=(i+1)%sectors;
        for(size_t band=1;band<profile.size();++band){
            const auto a=profile[band-1],b=profile[band];
            tri(mesh,point(i,a.x,a.y),point(j,a.x,a.y),point(j,b.x,b.y),shell);
            tri(mesh,point(i,a.x,a.y),point(j,b.x,b.y),point(i,b.x,b.y),shell);
        }
        for(double end:{-1.,1.}){
            tri(mesh,centre+glm::dvec3(0,0,end*length*.5),point(i,.25,end*.5),point(j,.25,end*.5),dark);
            const double z=end*.27;
            const auto a=point(i,1.045,z-.018),b=point(j,1.045,z-.018);
            const auto c=point(j,1.045,z+.018),d=point(i,1.045,z+.018);
            tri(mesh,a,b,c,copper);tri(mesh,a,c,d,copper);
        }
    }
    for(double end:{-1.,1.}){
        spar(mesh,centre+glm::dvec3(0,0,end*length*.5),
                  centre+glm::dvec3(0,0,end*(length*.5+radius*.36)),radius*.26,dark);
        box(mesh,centre+glm::dvec3(0,-radius*.8,end*length*.27),
            {radius*1.55,radius*.70,length*.07},dark);
    }
}

void engineHarness(Mesh& mesh,glm::dvec3 centre,double radius,unsigned detail) {
    const glm::vec3 copper(.43f,.235f,.095f),steel(.39f,.43f,.44f),dark(.069f,.09f,.105f);
    for(double side:{-1.,1.}){
        const auto a=centre+glm::dvec3(side*radius*.72,radius*.70,-radius*1.65);
        const auto b=centre+glm::dvec3(side*radius*1.10,radius*.80,-radius*.65);
        const auto c=centre+glm::dvec3(side*radius*.77,radius*.57,radius*.15);
        spar(mesh,a,b,radius*.10,copper);spar(mesh,b,c,radius*.10,copper);
        for(double t:{.23,.71}){
            const auto p=glm::mix(a,b,t);
            spar(mesh,p+glm::dvec3(-radius*.11,0,0),p+glm::dvec3(radius*.11,0,0),radius*.14,steel);
        }
    }
    serviceTank(mesh,centre+glm::dvec3(0,radius*.89,-radius*1.10),radius*.31,radius*1.35,detail);
    armour(mesh,centre+glm::dvec3(0,radius*.87,-radius*.18),
           {radius*.62,radius*.19,radius*.46},radius*.07,dark);
}
void ring(Mesh& m, double radius, double thickness, double y, glm::vec3 color, unsigned sectors) {
    for (unsigned i = 0; i < sectors; i++) {
        const double a = i * 2 * std::numbers::pi / sectors, b = (i + 1) * 2 * std::numbers::pi / sectors;
        const auto p = [&](double r, double t, double h) {
            return glm::dvec3(std::cos(t) * r, h, std::sin(t) * r);
        };
        for (int side : {-1, 1}) {
            const double h = y + side * thickness * .5;
            tri(m, p(radius - thickness, a, h), p(radius + thickness, a, h), p(radius + thickness, b, h),
                color);
            tri(m, p(radius - thickness, a, h), p(radius + thickness, b, h), p(radius - thickness, b, h),
                color);
        }
        tri(m, p(radius + thickness, a, y - thickness * .5), p(radius + thickness, b, y - thickness * .5),
            p(radius + thickness, b, y + thickness * .5), color * .85f);
        tri(m, p(radius + thickness, a, y - thickness * .5), p(radius + thickness, b, y + thickness * .5),
            p(radius + thickness, a, y + thickness * .5), color * .85f);
    }
}
void drum(Mesh& mesh, glm::dvec3 centre, double radius, double height, glm::vec3 colour, unsigned sectors) {
    const auto point = [&](double angle, double r, double y) {
        return centre + glm::dvec3(r*std::cos(angle),y,r*std::sin(angle));
    };
    constexpr std::array bevel{glm::dvec2(.79,-.5),glm::dvec2(1.,-.33),glm::dvec2(1.,.33),glm::dvec2(.79,.5)};
    for (unsigned i=0; i<sectors; ++i) {
        const double a=i*2*std::numbers::pi/sectors, b=(i+1)*2*std::numbers::pi/sectors;
        for (size_t j=1; j<bevel.size(); ++j) {
            const auto lower=bevel[j-1], upper=bevel[j];
            const auto p=point(a,radius*lower.x,height*lower.y), q=point(b,radius*lower.x,height*lower.y);
            const auto u=point(a,radius*upper.x,height*upper.y), v=point(b,radius*upper.x,height*upper.y);
            tri(mesh,p,u,q,colour);
            tri(mesh,u,v,q,colour);
        }
        for (double side : {-1.,1.}) {
            const double y=side*height*.5;
            const auto p=point(a,radius*.79,y), q=point(b,radius*.79,y);
            tri(mesh,centre+glm::dvec3(0,y,0),side>0?q:p,side>0?p:q,colour);
        }
    }
}

void stationBand(Mesh& m,double inner,double outer,double y,double height,double start,double end,
                 glm::vec3 cap,glm::vec3 flank,unsigned segments) {
    const auto vertex=[](double radius,double heading,double level) {
        return glm::dvec3(std::cos(heading)*radius,level,std::sin(heading)*radius);
    };
    const auto quad=[&](glm::dvec3 a,glm::dvec3 b,glm::dvec3 c,glm::dvec3 d,glm::vec3 tint) {
        tri(m,a,b,c,tint);
        tri(m,a,c,d,tint);
    };
    const double bevel=std::min(height*.22,(outer-inner)*.24);
    for (unsigned i=0;i<segments;++i) {
        const double a=std::lerp(start,end,double(i)/segments);
        const double b=std::lerp(start,end,double(i+1)/segments);
        const double low=y-height*.5, high=y+height*.5;
        for (double side : {-1.,1.}) {
            const double level=side<0 ? low : high;
            quad(vertex(inner,a,level),vertex(inner,b,level),vertex(outer-bevel,b,level),
                 vertex(outer-bevel,a,level),cap);
            quad(vertex(outer-bevel,a,level),vertex(outer-bevel,b,level),
                 vertex(outer,b,level-side*bevel),vertex(outer,a,level-side*bevel),cap);
        }
        quad(vertex(outer,a,low+bevel),vertex(outer,b,low+bevel),vertex(outer,b,high-bevel),
             vertex(outer,a,high-bevel),flank);
        quad(vertex(inner,a,low),vertex(inner,b,low),vertex(inner,b,high),vertex(inner,a,high),flank);
    }
    for (const double endCap : {start,end})
        quad(vertex(inner,endCap,y-height*.5),vertex(outer-bevel,endCap,y-height*.5),
             vertex(outer-bevel,endCap,y+height*.5),vertex(inner,endCap,y+height*.5),cap);
}
} // namespace

Mesh modularStationHull(const StationLayout& plan,unsigned detail) {
    const auto accent=stationAccent(plan.key.role);
    const glm::vec3 plate{.54f,.58f,.60f}, dark{.106f,.145f,.174f}, pane{.115f,.30f,.52f};
    const auto paint=glm::mix(plate,accent,.52f);
    const glm::vec3 rails{.38f,.45f,.48f}, warning{.92f,.66f,.20f};
    const Random random{plan.key.seed};
    const unsigned sections=detail>1?32:detail?16:8;
    Mesh mesh;
    const auto dock=[&] {
        const auto& bay=plan.freightBay().floor;
        constexpr double bayScale=1./orbitalStationRadius;
        const double openingLeft=std::min(-.078,(bay.left-3.)*bayScale);
        constexpr double openingRight=-.0065, openingBack=dockingBerthOffset.z-.021;
        const auto apron=[&](double halfWidth,double back,double front,double y,double height,double bevel,
                             glm::vec3 material) {
            const auto slab=[&](double a,double b,double near,double far) {
                armour(mesh,{(a+b)*.5,y,(near+far)*.5},{b-a,height,far-near},bevel,material);
            };
            slab(-halfWidth,halfWidth,back,openingBack);
            slab(-halfWidth,openingLeft,openingBack,front);
            slab(openingRight,halfWidth,openingBack,front);
        };
        // The runway center and the forward clearance match dockingBerth().
        // A recess beside the runway opens the bridge's outboard sightline.
        apron(.26,.415,1.545,.054,.154,.046,hull);
        apron(.20,1.065,1.695,.121,.048,.014,dark);
        for (double x : {openingLeft-.0055,openingRight+.0055}) {
            box(mesh,{x,.140,1.554},{.014,.012,.247},rails);
            for (double z : {1.46,1.53,1.60,1.66}) {
                box(mesh,{x,.150,z},{.007,.017,.009},plate);
                if (detail)
                    box(mesh,{x,.161,z},{.005,.006,.007},warning);
            }
            spar(mesh,{x,.158,1.451},{x,.158,1.673},.004,rails);
            spar(mesh,{x,.050,1.533},{x,-.054,1.377},.022,rails);
        }
        box(mesh,{(openingLeft+openingRight)*.5,.141,openingBack-.009},
                 {openingRight-openingLeft,.015,.023},rails);
        if (detail) {
            // Service grilles and access covers sit below the inboard windows.
            for (double z : {1.312,1.369}) {
                for (double x : {openingLeft-.005,(openingLeft+openingRight)*.5,openingRight+.007}) {
                    box(mesh,{x,.1448,z},{.021,.004,.036},rails);
                    box(mesh,{x,.1473,z},{.017,.002,.031},dark);
                    for (int bar=-2;bar<=2;++bar)
                        box(mesh,{x,.149,z+bar*.005},{.0158,.0016,.0023},trim);
                }
            }
            for (double x : {openingLeft-.011,openingRight+.013})
                box(mesh,{x,.147,openingBack-.034},{.011,.006,.024},paint);
        }
        for (double side : {-1.,1.}) {
            const double x=side*.205;
            armour(mesh,{x,.104,1.43},{.070,.11,.53},.021,plate);
            armour(mesh,{x,.216,1.13},{.088,.25,.11},.026,plate);
            box(mesh,{x,.219,1.188},{.031,.133,.007},paint);
            spar(mesh,{x,.161,1.57},{side*.246,-.034,1.25},.031,rails);
            spar(mesh,{x,-.011,1.58},{side*.18,-.135,.94},.046,rails);
            box(mesh,{side*.179,.161,1.58},{.018,.013,.19},glow);
            if (detail) {
                for (unsigned i=0;i<7;++i) {
                    const double z=1.205+i*.065;
                    box(mesh,{side*.135,.1465,z},{.051,.003,.044},rails);
                    box(mesh,{side*.135,.149,z},{.026,.004,.017},i<2?warning:glow);
                }
                for (double z : {1.29,1.47,1.63})
                    armour(mesh,{side*.228,.079,z},{.060,.073,.050},.010,rails);
            }
        }
        armour(mesh,{0,.323,1.13},{.50,.076,.13},.021,plate);
        box(mesh,{0,.320,1.196},{.31,.022,.008},paint);
        if (detail) {
            for (double z : {1.28,1.47,1.60}) {
                const double reach=z<openingBack?.056:.0055;
                for (double side : {-1.,1.}) {
                    const glm::dvec3 a(0,.147,z-.024),b(side*reach,.147,z+.003),c(side*reach,.147,z+.020);
                    tri(mesh,a,c,b,side<0?warning:paint);
                }
            }
        }
        // The enclosed boarding passage sits along the port side of the clamped ship.
        const auto housing=[&](double x0,double x1,double z0,double z1,double y,double height,glm::vec3 colour) {
            if (x1<=x0 || z1<=z0 || height<=0.) return;
            box(mesh,dockingBerthOffset+glm::dvec3((x0+x1)*.5,y,(z0+z1)*.5)*bayScale,
                     {(x1-x0)*bayScale,height*bayScale,(z1-z0)*bayScale},colour);
        };
        using namespace stationDeck;
        constexpr double skin=.065;
        const auto berthPoint=[&](glm::dvec3 position) { return dockingBerthOffset+position*bayScale; };
        const auto bridgePoint=[&](double x,glm::dvec2 section) {
            return berthPoint({x,section});
        };
        // Opaque berth hardware carries its finish and fixture emission in the vertex alpha channel.
        const auto serviceBox=[&](glm::dvec3 p,glm::dvec3 size,glm::vec3 colour,float alpha=.5f) {
            box(mesh,berthPoint(p),size*bayScale,colour,0.,alpha);
        };
        const auto serviceBrace=[&](glm::dvec3 a,glm::dvec3 b,double width,glm::vec3 colour) {
            spar(mesh,berthPoint(a),berthPoint(b),width*bayScale,colour,.5f);
        };
        const glm::vec3 enamel{.21f,.28f,.32f}, deckTile{.29f,.35f,.37f}, cable{.48f,.27f,.13f};
        const auto inspectionLight=[&](glm::dvec3 position,glm::vec3 colour) {
            serviceBox(position-glm::dvec3(0,.065,0),{.27,.15,.20},dark);
            serviceBox(position,{.206,.046,.140},colour,.12f);
        };
        for (const auto& pad : stationTrafficPads) {
            const auto p=pad.centre;
            const double w=pad.halfSize.x,l=pad.halfSize.y;
            // Mount the deck plate above its backing so the two tops never compete for depth.
            armour(mesh,berthPoint(p-glm::dvec3(0,1.36,0)),{2.*w*bayScale,2.5*bayScale,2.*l*bayScale},
                   .85*bayScale,rails);
            serviceBox(p-glm::dvec3(0,.055,0),{2.*w-1.8,.11,2.*l-1.4},enamel);
            const double rootZ=p.x<0?54.:78.;
            for (double side : {-1.,1.}) {
                const double x=side*(w-1.28);
                serviceBrace(p+glm::dvec3(x,-1.54,-l*.80),{p.x+x*.55,-18.,rootZ},1.14,rails);
                serviceBrace(p+glm::dvec3(x,-1.68,l*.76),{p.x+x*.74,-13.5,p.z-l*.90},.89,dark);
                serviceBox(p+glm::dvec3(x,.04,0),{.20,.065,2.*l-2.72},warning);
                for (double z : {-l+1.4,0.,l-1.4}) {
                    const auto lamp=p+glm::dvec3(x,.22,z);
                    armour(mesh,berthPoint(lamp),glm::dvec3(.60,.41,.82)*bayScale,.09*bayScale,dark);
                    serviceBox(lamp+glm::dvec3(0,.214,0),{.41,.033,.57},rails);
                    for(int louvre=-1;louvre<=1;++louvre){
                        serviceBox(lamp+glm::dvec3(0,.241,louvre*.17),{.32,.026,.105},
                                   z>0?glm::vec3(.40f,.80f,1.f):glm::vec3(1.f,.69f,.30f),.12f);
                    }
                }
                if (detail) {
                    for (double z : {-l*.59,l*.59})
                        serviceBox(p+glm::dvec3(0,.012,z),{w*1.3,.027,.27},deckTile);
                    serviceBrace(p+glm::dvec3(x*1.04,-1.31,-l+.25),
                                 p+glm::dvec3(x*1.04,-1.31,l-.25),.21,cable);
                }
            }
            // The apron keeps the same main fittings at every LOD. These
            // sit clear of the landing lane and the bridge window sightline.
            for(double side:{-1.,1.}){
                const auto rail=p+glm::dvec3(side*(w-.57),.092,0);
                serviceBox(rail,{.16,.105,l*1.67},rails);
                for(int n=-2;n<=2;++n){
                    const auto at=p+glm::dvec3(side*(w-2.5),.025,n*l*.32);
                    serviceBox(at,{1.35,.047,.64},dark);
                    for(int fin=-2;fin<=2;++fin)
                        serviceBox(at+glm::dvec3(0,.037,fin*.10),{1.17,.027,.038},rails);
                    serviceBox(at+glm::dvec3(-side*.83,.011,0),{.12,.050,.71},warning);
                }
            }
        }
        for (size_t i=0;i<plan.modules.size();++i) {
            const auto& room=plan.modules[i];
            const auto& area=room.floor;
            if (room.kind!=StationModuleKind::Bridge) {
                const double middle=(room.ceiling+floor)*.5;
                const double height=room.ceiling-floor;
                // The world camera shares the deck camera's sightlines. Keep the shell
                // outside the floor and walls and open at every traversable doorway.
                housing(area.left,area.right,area.front-skin,area.back+skin,floor-skin*.5,skin,plate);
                housing(area.left,area.right,area.front-skin,area.back+skin,room.ceiling+skin*.5,skin,plate);
                for (int side : {-1,1}) {
                    const double z0=side<0?area.front-skin:area.back, z1=z0+skin;
                    if (const auto* annex=plan.annexAt(i,side)) {
                        housing(area.left,annex->doorwayLeft,z0,z1,middle,height,plate);
                        housing(annex->doorwayRight,area.right,z0,z1,middle,height,plate);
                        housing(annex->doorwayLeft,annex->doorwayRight,z0,z1,(annexDoorTop+room.ceiling)*.5,
                                room.ceiling-annexDoorTop,plate);
                    } else {
                        housing(area.left,area.right,z0,z1,middle,height,plate);
                    }
                }
                const auto frameOpening=[&](double x,double half,double head) {
                    housing(x,x+skin,area.front,centre-half,middle,height,plate);
                    housing(x,x+skin,centre+half,area.back,middle,height,plate);
                    housing(x,x+skin,centre-half,centre+half,(head+room.ceiling)*.5,room.ceiling-head,plate);
                };
                const auto connect=[&](double x,const StationModule& adjacent) {
                    const double opening=std::min(area.back,adjacent.floor.back)-centre;
                    if (adjacent.kind==StationModuleKind::Bridge && room.kind!=StationModuleKind::Airlock) {
                        frameOpening(x,opening,room.ceiling);
                        const auto profile=bridgeGlassProfile(adjacent);
                        for (size_t n=2;n+1<profile.size();++n) {
                            const auto a=profile[n-1],b=profile[n];
                            const auto lowerA=bridgePoint(x+skin,a),lowerB=bridgePoint(x+skin,b);
                            const auto upperA=bridgePoint(x+skin,{room.ceiling,a.y});
                            const auto upperB=bridgePoint(x+skin,{room.ceiling,b.y});
                            tri(mesh,lowerA,lowerB,upperB,plate);
                            tri(mesh,lowerA,upperB,upperA,plate);
                        }
                        return;
                    }
                    const double head=room.kind==StationModuleKind::Airlock?doorTop:std::min(room.ceiling,adjacent.ceiling);
                    frameOpening(x,opening,head);
                };
                if (i==0) frameOpening(area.right-skin,doorHalf,doorTop);
                else connect(area.right-skin,plan.modules[i-1]);
                if (i+1<plan.modules.size()) connect(area.left,plan.modules[i+1]);
                else housing(area.left-skin,area.left,area.front,area.back,middle,height,plate);
                housing(area.left+.12,area.right-.12,area.front+.14,area.back-.14,room.ceiling+.10,.28,rails);
                for (double z : {area.front,area.back})
                    housing(area.left+.12,area.right-.12,z-.024,z+.024,room.ceiling-.37,.15,paint);
                continue;
            }
            housing(area.left,area.right,area.front-.045,area.back+.045,floor-.095,.18,dark);
            for (double z : {area.front,area.back}) {
                housing(area.left,area.right,z-.036,z+.036,(floor+glassSill)*.5,glassSill-floor,plate);
                housing(area.left,area.right,z-.042,z+.042,glassSill-.014,.04,rails);
            }
            const auto profile=bridgeGlassProfile(room,.025);
            const auto band=[&](double x,double width) {
                const double from=x-width*.5,to=x+width*.5;
                for (size_t n=1;n<profile.size();++n) {
                    const auto a=profile[n-1],b=profile[n];
                    tri(mesh,bridgePoint(from,a),bridgePoint(to,a),bridgePoint(to,b),rails);
                    tri(mesh,bridgePoint(from,a),bridgePoint(to,b),bridgePoint(from,b),rails);
                }
            };
            band(area.right,plan.modules[i-1].kind==StationModuleKind::Airlock?.085:bridgeRibWidth);
            if (plan.modules[i+1].kind!=StationModuleKind::Bridge)
                band(area.left,.093);
            if (detail>1) {
                const double x=(area.left+area.right)*.5;
                // Inspection fittings sit away from the sill so they can be seen on a downward sightline.
                serviceBox({x,floor-.24,centre+.66},{ribPitch-.06,.22,.26},enamel);
                serviceBrace({area.right-.08,floor-.15,centre+.44},
                             {area.right-.08,floor-.79,centre+2.52},.12,rails);
                serviceBox({x,floor-.73,centre+2.27},{ribPitch-.07,.082,.105},cable);
                serviceBox({area.right-.08,floor-.77,centre+2.40},{.24,.084,.32},enamel);
                inspectionLight({area.right-.08,floor-.66,centre+2.41},{.48f,.82f,.91f});
            }
        }
        if (detail>1) {
            const auto stationEntrance=std::find_if(plan.modules.begin()+1,plan.modules.end(),[](const auto& room) {
                return room.kind!=StationModuleKind::Bridge;
            });
            const double left=stationEntrance->floor.right-.22, right=bridgeDoor+.68;
            const double middle=(left+right)*.5, length=right-left;
            constexpr double deck=floor-1.04, near=centre-1.58, far=centre-4.98;
            const double midZ=(near+far)*.5;
            serviceBox({middle,deck-.10,midZ},{length,.20,near-far},dark);
            for (double z : {near,far}) {
                serviceBox({middle,deck-.095,z},{length,.29,.17},enamel);
                serviceBox({middle,deck+.69,z},{length,.084,.072},rails);
            }
            const unsigned bays=unsigned(std::ceil(length/1.60));
            const double pitch=length/bays;
            for (unsigned n=0;n<bays;++n) {
                const double from=left+pitch*n,to=from+pitch,x=(from+to)*.5;
                for (double z : {midZ-.63,midZ+.63}) {
                    serviceBox({x,deck-.002,z},{pitch-.038,.066,1.21},n%2?deckTile:rails);
                    serviceBox({x,deck+.039,z-.38},{pitch-.25,.018,.054},dark);
                }
                const double upright=from+.075;
                for (double z : {far,near}) {
                    serviceBox({upright,deck+.35,z},{.083,.72,.073},rails);
                    serviceBrace({upright,deck+.17,z},{to-.068,deck+.55,z},.041,rails);
                }
                serviceBrace({upright,deck-.21,far+.14},{upright,floor-.23,centre-.43},.19,rails);
                serviceBox({x,deck-.28,far+.18},{pitch-.06,.19,.28},enamel);
                if (n%2==0) {
                    serviceBox({x,deck+.115,far+.47},{pitch-.20,.23,.53},enamel);
                    for (int louvre=-2;louvre<=2;++louvre)
                        serviceBox({x+louvre*.15,deck+.243,far+.47},{.075,.030,.38},dark);
                    inspectionLight({upright,deck+.74,far},{1.f,.76f,.43f});
                }
                serviceBox({x,deck+.037,near-.20},{pitch-.21,.016,.051},warning);
            }
            for (double z : {far-.19,far-.41}) {
                serviceBrace({left,deck-.37,z},{right,deck-.37,z},.125,cable);
                serviceBrace({left+.18,deck-.36,z},{left-.72,deck-.83,z},.115,enamel);
            }
            for (double x : {left+.12,right-.12}) {
                serviceBox({x,deck+.34,midZ},{.054,.091,near-far},rails);
                serviceBox({x,deck-.39,midZ},{.31,.22,near-far+.13},enamel);
            }
        }
        for (const auto& annex : plan.annexes) {
            using namespace stationDeck;
            const auto& room=annex.room;
            const auto& area=room.floor;
            const auto z=annex.outsideWall();
            const double wallY=(floor+room.ceiling)*.5;
            housing(area.left,area.right,area.front,area.back,floor-.122,.235,rails);
            housing(area.left,area.right,area.front,area.back,room.ceiling+.13,.26,plate);
            housing(area.left+.31,area.right-.31,area.front+.32,area.back-.32,room.ceiling+.37,.23,trim);
            for (double x : {area.left,area.right})
                housing(x-.038,x+.038,area.front,area.back,wallY,room.ceiling-floor,plate);
            if (annex.panoramic()) {
                const double left=area.left+panoramaInset, right=area.right-panoramaInset;
                housing(area.left,area.right,z-.044,z+.044,(floor+panoramaSill)*.5,panoramaSill-floor,plate);
                housing(area.left,area.right,z-.044,z+.044,(panoramaHead+room.ceiling)*.5,room.ceiling-panoramaHead,plate);
                for (double p : {0.,1./3,2./3,1.}) {
                    const double x=glm::mix(left,right,p);
                    const double halfWidth=p==0.||p==1.?panoramaInset:.034;
                    housing(x-halfWidth,x+halfWidth,z-.048,z+.048,(panoramaSill+panoramaHead)*.5,
                            panoramaHead-panoramaSill,rails);
                }
                housing(left,right,z-.065,z+.065,panoramaSill,.078,trim);
                housing(left,right,z-.065,z+.065,panoramaHead,.078,trim);
                if (detail>1) {
                    // The supports frame the lower corners of the view and stay below the glass.
                    const double side=annex.side, belt=floor-.24;
                    serviceBox({(left+right)*.5,belt,z+side*.21},{right-left,.22,.43},enamel);
                    for (double x : {left+.17,right-.17}) {
                        const double end=z+side*(2.75+random.unit(26)*.36);
                        serviceBox({x,belt,(z+end)*.5},{.36,.26,std::abs(end-z)},enamel);
                        serviceBrace({x,belt-.06,end},{x,floor-2.05,z+side*.09},.16,rails);
                        serviceBrace({x,belt-.35,z+side*.14},{x,belt-.35,end-side*.12},.084,cable);
                        inspectionLight({x,belt+.16,end-side*.27},{.55f,.83f,.90f});
                    }
                }
            } else {
                housing(area.left,area.right,z-.048,z+.048,wallY,room.ceiling-floor,plate);
                for (double x : {area.left+.62,area.right-.62}) {
                    housing(x-.24,x+.24,z-.15,z+.15,wallY,.94,rails);
                    housing(x-.17,x+.17,z-.17,z+.17,wallY+.155,.08,paint);
                }
            }
            housing(area.left+.14,area.right-.14,z-.069,z+.069,floor-.08,.071,paint);
            for (double x : {area.left+.42,area.right-.42}) {
                const double inner=annex.insideWall();
                spar(mesh,dockingBerthOffset+glm::dvec3(x,floor-.19,z-annex.side*.21)*bayScale,
                     dockingBerthOffset+glm::dvec3(x,floor-2.2,inner)*bayScale,.34*bayScale,rails);
            }
        }
        housing(bay.left-48.,bay.left,bay.front-3.,bay.back+3.,-2.4,4.2,trim);
        housing(bay.left-39.,bay.left-.16,bay.front-.11,bay.front+.22,-8.31,14.6,rails);
        housing(bay.left-39.,bay.left-.16,bay.back-.22,bay.back+.11,-8.31,14.6,rails);
    };
    const auto truss=[&](double x,double z,double reach) {
        box(mesh,{x*.53,-.14,z},{reach,.10,.18},dark);
        if (detail) {
            box(mesh,{x*.53,-.074,z-.065},{reach,.034,.034},trim);
            box(mesh,{x*.53,-.074,z+.065},{reach,.034,.034},trim);
            for (double from=x*.53-reach*.40;from<x*.53+reach*.28;from+=.093)
                for (double side : {-1.,1.})
                    spar(mesh,{from,-.16,z+side*.071},{from+.088,-.071,z+side*.071},.014,rails);
        }
    };
    const auto pressurePod=[&](double x,double z,double radius,double lift,bool windows) {
        drum(mesh,{x,lift,z},radius,.28,plate,sections);
        drum(mesh,{x,lift+.167,z},radius*.78,.067,trim,sections);
        drum(mesh,{x,lift+.035,z},radius*1.014,.056,windows?pane:accent,sections);
        drum(mesh,{x,lift-.066,z},radius*1.03,.038,rails,sections);
        if (!detail) return;
        const unsigned windowsAround=detail>1?16:8;
        for (unsigned i=0;i<windowsAround;++i) {
            const double angle=i*(2*std::numbers::pi/windowsAround);
            const double cx=x+std::cos(angle)*radius, cz=z+std::sin(angle)*radius;
            box(mesh,{cx,lift+.048,cz},{.057,.102,.022},dark,angle);
            box(mesh,{x+std::cos(angle)*radius*1.014,lift+.104,z+std::sin(angle)*radius*1.014},
                {.026,.026,.036},i%4==0?paint:rails,angle);
        }
    };
    const auto panels=[&](double side,double z,bool solar) {
        const double wing=.87+random.unit(11)*.19;
        const glm::vec3 surface=solar?glm::vec3(.12,.29,.42):glm::vec3(.19,.23,.27);
        truss(side*wing,z,wing*.98);
        armour(mesh,{side*wing,-.142,z},{.55,.042,.54},.010,rails);
        if (detail) {
            for (double dx : {-.167,0.,.167})
                for (double dz : {-.172,0.,.172})
                    armour(mesh,{side*wing+dx,-.116,z+dz},{.156,.017,.160},.006,surface);
            for (double x : {-.267,.267})
                box(mesh,{side*wing+x,-.115,z},{.021,.034,.57},plate);
            if (!solar)
                for (unsigned i=0;i<9;++i)
                    box(mesh,{side*wing,-.104,z-.236+i*.059},{.48,.026,.014},rails);
        } else {
            box(mesh,{side*wing,-.116,z},{.50,.009,.49},surface);
        }
    };
    const auto freight=[&](double x,double z,unsigned containers) {
        truss(x,z,.67);
        box(mesh,{x,-.044,z},{.45,.11,.64},dark);
        for (unsigned i=0;i<containers;++i) {
            const double y=.077+i*.119;
            armour(mesh,{x,y,z},{.41,.111,.53},.021,i%2?plate:paint);
            if (detail) {
                for (double offset : {-.16,.16})
                    armour(mesh,{x,y+.011,z+offset},{.427,.129,.027},.008,rails);
                box(mesh,{x,y,z+.266},{.28,.070,.011},dark);
                for (double dx : {-.085,.085})
                    box(mesh,{x+dx,y+.018,z+.273},{.019,.013,.008},warning);
            }
        }
        box(mesh,{x,.032,z+.282},{.25,.057,.020},accent);
    };
    const auto dish=[&](glm::dvec3 base,glm::dvec3 axis,double radius) {
        axis=glm::normalize(axis);
        const auto across=glm::normalize(glm::cross(axis,glm::dvec3(0,0,1)));
        const auto forward=glm::cross(axis,across);
        const auto hub=base+axis*.13;
        spar(mesh,base,hub,.034,rails);
        const auto ringPoint=[&](double angle,double r,double depth) {
            return hub+axis*depth+(across*std::cos(angle)+forward*std::sin(angle))*r;
        };
        for (unsigned i=0;i<sections;++i) {
            const double a=i*2*std::numbers::pi/sections,b=(i+1)*2*std::numbers::pi/sections;
            const auto p=ringPoint(a,radius*.49,.015),q=ringPoint(b,radius*.49,.015);
            const auto u=ringPoint(a,radius,.066),v=ringPoint(b,radius,.066);
            tri(mesh,hub,p,q,dark);
            tri(mesh,p,u,q,plate);
            tri(mesh,q,u,v,plate);
            if (detail && i%(sections/4)==0)
                spar(mesh,u,hub+axis*.14,.011,rails);
        }
        spar(mesh,hub,hub+axis*.15,.021,warning);
    };

    armour(mesh,{0,-.104,-.03},{.58,.25,1.65},.074,hull);
    armour(mesh,{0,-.236,.04},{.41,.146,1.50},.041,dark);
    pressurePod(0.,-.07,.33,.10,true);
    dock();
    const unsigned containers=unsigned(std::round((plan.freightBay().floor.right-plan.freightBay().floor.left)/
                                                   stationDeck::ribPitch))-2;
    const double tail=-.72-random.unit(12)*.19;
    switch (plan.key.role) {
    case StationRole::Freight:
        for (double side : {-1.,1.}) {
            freight(side*.61,.17,containers);
            freight(side*(.53+.13*random.unit(side<0?15:16)),tail,2);
            const double x=side*.60;
            armour(mesh,{x,.18,.58},{.24,.10,.20},.025,paint);
            spar(mesh,{x,.18,.57},{x,.54,.10},.044,rails);
            spar(mesh,{x,.54,.10},{side*.23,.54,.10},.036,paint);
            if (detail)
                spar(mesh,{side*.28,.54,.10},{side*.28,.35,.10},.011,rails);
        }
        break;
    case StationRole::Habitat: {
        const double ringRadius=.70+random.unit(17)*.22;
        const unsigned pods=6+unsigned(random.bits(18)%5);
        ring(mesh,ringRadius,.068,-.06,dark,sections*2);
        ring(mesh,ringRadius,.024,.015,accent,sections*2);
        for (unsigned i=0;i<pods;++i) {
            const double angle=(i+.5)*2*std::numbers::pi/pods;
            pressurePod(std::cos(angle)*ringRadius,std::sin(angle)*ringRadius,.135,.079,true);
            const double span=ringRadius*.84;
            box(mesh,{std::cos(angle)*span*.48,-.086,std::sin(angle)*span*.48},{span,.08,.084},hull,angle);
            const double halfArc=std::numbers::pi/pods*.71;
            stationBand(mesh,ringRadius-.053,ringRadius+.096,-.035,.126,angle-halfArc,angle+halfArc,
                        plate,pane,detail>1?7:4);
            stationBand(mesh,ringRadius+.074,ringRadius+.101,.030,.026,angle-halfArc,angle+halfArc,
                        paint,rails,detail>1?7:4);
        }
        freight(-.58,tail,2);
        break;
    }
    case StationRole::Research:
        armour(mesh,{0,.086,tail},{.37,.32,.33},.037,hull);
        for (double side : {-1.,1.}) {
            spar(mesh,{side*.10,-.145,tail+.23},{side*.18,.226,tail},.056,rails);
            spar(mesh,{side*.15,-.116,tail+.13},{side*.46,.037,tail-.15},.048,paint);
            armour(mesh,{side*.46,.017,tail-.15},{.11,.07,.12},.021,rails);
        }
        pressurePod(0.,tail,.32,.34,true);
        pressurePod(0.,tail,.22,.62,true);
        for (double side : {-1.,1.}) {
            panels(side,-.38,true);
            dish({side*.46,.020,tail-.15},{side*.49,1.,.27},.194);
        }
        freight(-.57,.38,2);
        break;
    case StationRole::Industrial:
        for (double side : {-1.,1.}) {
            truss(side*.60,-.12,.60);
            pressurePod(side*.53,.02,.25,.14,false);
            pressurePod(side*.71,tail,.24,.22,false);
            spar(mesh,{side*.12,-.216,tail+.18},{side*.72,.083,tail},.078,rails);
            panels(side,-.65,false);
            for (double pipe : {-.052,.052}) {
                const double x=side*.65+pipe;
                spar(mesh,{x,.14,tail},{x,-.027,tail+.21},.062,rails);
                spar(mesh,{x,-.027,tail+.21},{side*.46,-.027,-.12},.062,rails);
                if (detail) armour(mesh,{x,-.027,tail+.21},{.087,.080,.098},.016,paint);
            }
        }
        freight(.57,.63,2);
        break;
    case StationRole::Vault:
        for (double y : {-.12,.10,.32}) {
            drum(mesh,{0,y,-.36},.48,.22,dark,sections);
            armour(mesh,{0,y+.048,-.36},{.94,.081,.71},.035,rails);
        }
        drum(mesh,{0,.46,-.36},.39,.09,plate,sections);
        ring(mesh,.49,.019,.30,accent,sections);
        panels(-1.,.05,true);
        freight(.53,-.32,2);
        for (double side : {-1.,1.})
            spar(mesh,{side*.37,-.18,-.36},{side*.43,.48,-.36},.092,paint);
        break;
    }
    if (detail) {
        const double mast=plan.key.role==StationRole::Research?.84:.54+random.unit(21)*.20;
        box(mesh,{.12,mast*.52,tail},{.031,mast,.028},trim);
        box(mesh,{.12,mast,tail},{.27,.029,.047},plate);
        box(mesh,{.12,mast+.02,tail},{.047,.032,.039},accent);
    }
    mesh.compact();
    return mesh;
}

Mesh activityMesh(ActivityKind kind, unsigned detail) {
    detail = std::min(detail, 2u);
    Mesh m;
    // Source silhouettes: Hyperion flight/settlements.cpp::buildCraft and orbitalCore.
    if (kind == ActivityKind::Harbour) {
        const glm::vec3 ceramic{.53,.58,.56}, insulation{.58,.33,.126}, radiator{.095,.157,.205};
        const glm::vec3 armour{.33,.40,.42}, capLight{.67,.83,.44}, windows{.12,.33,.58};
        const unsigned sectors=detail ? 72 : 36;
        box(m, {0,-.04,0}, {1.60,.21,.39}, hull);
        drum(m,{0,.06,0},.35,.40,hull,sectors/2);
        drum(m,{0,.33,0},.265,.23,ceramic,sectors/2);
        drum(m,{0,.48,0},.179,.13,trim,sectors/2);
        ring(m,.80,.048,-.033,armour,sectors);
        ring(m,.788,.055,-.16,hull,sectors);
        ring(m,.284,.021,.28,glass,sectors/2);
        if (detail) {
            // An open arrival terrace on the retained harbour, facing local +Z.
            box(m, {0, .105, 1.12}, {.18, .065, .66}, hull);
            box(m, {0, .13, 1.45}, {.36, .025, .48}, {.055, .08, .11});
            for (int side : {-1, 1}) {
                box(m, {side * .18, .15, 1.45}, {.012, .025, .48}, glow);
                box(m, {side * .21, .20, 1.22}, {.024, .16, .035}, trim);
                box(m, {side * .21, .29, 1.22}, {.028, .025, .035}, {.74, .35, .93});
                for (int i = 0; i < 7; ++i)
                    box(m, {side * .12, .146, 1.25 + i * .06}, {.018, .006, .022}, glow);
            }
            box(m, {0, .146, 1.21}, {.30, .007, .016}, {.95, .65, .26});
            box(m, {0, .278, 1.22}, {.44, .025, .03}, hull);
            box(m, {0, .278, 1.24}, {.38, .010, .008}, {.74, .35, .93});
            if (detail == 2) {
                for (int i = 0; i < 8; ++i) {
                    const double z = 1.28 + i * .05;
                    for (int side : {-1, 1}) {
                        box(m, {side * .105, .144, z}, {.12, .004, .042}, {.035, .045, .06});
                        const glm::dvec3 a(0, .151, z - .012), b(side * .032, .151, z + .010),
                            c(side * .032, .151, z + .019);
                        tri(m, a, side > 0 ? c : b, side > 0 ? b : c, glow);
                    }
                }
            }
            // Glazed modules sit between two uninterrupted structural rails.
            ring(m,.820,.015,.061,trim,sectors);
            ring(m,.792,.014,-.18,trim,sectors);
            ring(m,.304,.015,.19,insulation,sectors/2);
            drum(m,{0,-.24,0},.23,.18,radiator,sectors/2);
            for (int segment=0; segment<12; ++segment) {
                constexpr double pitch=2*std::numbers::pi/12;
                const double angle=(segment+.5)*pitch;
                const auto radial=glm::dvec3(std::cos(angle),0,std::sin(angle));
                const double start=angle-pitch*.44,end=angle+pitch*.44;
                const unsigned slices=detail==2 ? 6 : 4;
                stationBand(m,.735,.903,-.035,.137,start,end,segment%3==0 ? insulation : ceramic,
                            armour,slices);
                stationBand(m,.894,.909,-.025,.043,start+.012,end-.012,armour,windows,slices);
                stationBand(m,.887,.900,.029,.013,start+.037,end-.037,trim,
                            segment%3==0 ? capLight : trim,slices);
                box(m,radial*.56+glm::dvec3(0,-.126,0),{.47,.067,.074},trim,angle);
                if (detail==2)
                    for (double offset : {-.16,-.081,0.,.081,.16}) {
                        const double t=angle+offset;
                        const glm::dvec3 rib{std::cos(t),0,std::sin(t)};
                        box(m,rib*.910+glm::dvec3(0,-.024,0),{.019,.052,.010},armour,t);
                    }
            }
            for (int side : {-1, 1}) {
                drum(m,{side*.59,.117,-.06},.168,.32,hull,sectors/3);
                drum(m,{side*.59,.293,-.06},.122,.071,ceramic,sectors/3);
                box(m, {side * .52, -.05, .58}, {.53, .025, .32}, glass);
                box(m,{side*.55,-.058,-.66},{.49,.048,.51},radiator);
                for (int fin=0; fin<(detail==2 ? 9 : 4); ++fin) {
                    const double z=-.88+fin*(detail==2 ? .055 : .139);
                    box(m,{side*.55,-.025,z},{.465,.025,.023},trim);
                }
                box(m, {side * .82, .035, 0}, {.04, .08, .20}, glow);
                box(m,{side*.423,-.12,.82},{.11,.14,.49},hull);
                box(m,{side*.433,-.044,.86},{.064,.018,.35},insulation);
            }
        }
        if (detail == 2) {
            for (int i = 0; i < 8; i++) {
                const double a = i * 2 * std::numbers::pi / 8;
                box(m, {std::cos(a) * .82, .12, std::sin(a) * .82}, {.13, .12, .13}, trim);
                box(m, {std::cos(a) * .82, .19, std::sin(a) * .82}, {.05, .02, .05}, glow);
            }
            for (int side : {-1, 1}) {
                box(m, {side * .45, .43, 0}, {.035, .42, .035}, trim);
                box(m, {side * .45, .64, -.16}, {.035, .035, .35}, trim);
                for (int port=0; port<5; ++port) {
                    const double z=-.34+port*.10;
                    box(m,{side*.602,.105,z},{.064,.038,.062},glass);
                    box(m,{side*.609,.104,z},{.046,.018,.031},glow);
                }
                box(m,{side*.157,.082,1.46},{.04,.045,.42},trim);
            }
            drum(m,{0,.59,0},.069,.19,hull,16);
            drum(m,{0,.686,0},.082,.046,insulation,16);
            box(m,{0,.744,0},{.022,.13,.022},trim);
            box(m,{0,.814,0},{.040,.024,.040},glow);
        }
    } else if (kind == ActivityKind::Freighter) {
        const glm::vec3 darkPlate(.18,.24,.27), holdSteel(.40,.43,.42), ochre(.51,.31,.13);
        armour(m,{0,.02,.12},{.83,.42,2.43},.10,darkPlate);
        armour(m,{0,.24,-1.23},{.87,.37,.76},.16,hull);
        armour(m,{0,.41,-1.31},{.63,.17,.42},.041,glass);
        box(m,{0,.34,.16},{.24,.075,1.63},holdSteel);
        for (int side : {-1, 1}) {
            spar(m,{0.,.03,.94},{side*.89,.035,.94},.15,holdSteel);
            spar(m,{0.,.03,-.81},{side*.89,.035,-.81},.14,holdSteel);
            armour(m,{side*.91,.045,.86},{.49,.37,.84},.10,hull);
            box(m,{side*.91,.045,1.293},{.29,.195,.027},darkPlate);
            // Exposed longitudinal tanks and a raised, split thermal jacket
            // break up the broad slab silhouette without covering the collar.
            serviceTank(m,{side*.59,.125,-.03},.112,.72,detail);
            engineHarness(m,{side*.91,.045,1.10},.16,detail);
            for(int plate=0;plate<3;++plate){
                const double z=.62-plate*.49;
                armour(m,{side*.342,.300,z},{.143,.060,.384},.018,plate==1?ochre:holdSteel);
                spar(m,{side*.382,.320,z-.10},{side*.440,.193,z-.10},.022,darkPlate);
            }
            for (int i=0;i<3;++i) {
                const double z=.71-i*.60;
                const auto paint=i==1?ochre:holdSteel;
                armour(m,{side*.71,-.30,z},{.61,.49,.49},.059,paint);
                for(double rib:{-.18,0.,.18})
                    box(m,{side*.71+rib,-.045,z},{.026,.025,.34},darkPlate);
                if (detail) {
                    for (double offset : {-.166,.166})
                        box(m,{side*.71,-.30,z+offset},{.638,.502,.031},darkPlate);
                    box(m,{side*1.017,-.30,z},{.016,.29,.25},paint*.62f);
                }
            }
            if (detail) {
                engineBell(m,{side*.91,.045,1.302},.142,.19,detail);
                box(m,{side*.35,.275,-1.49},{.027,.07,.19},glow*.68f);
                spar(m,{side*.19,.37,.91},{side*.21,.58,1.16},.029,holdSteel);
                radiatorBank(m,{side*.26,.258,.12},{.22,1.24},detail);
                for(int rib=0;rib<7;++rib){
                    const double z=-.72+rib*.24;
                    spar(m,{side*.34,.255,z},{side*.34,.255,z+.13},.035,holdSteel*.68f);
                }
                for(int i=0;i<3;++i){
                    const double z=.71-i*.60;
                    for(double latch:{-.18,.18}){
                        box(m,{side*1.025,-.25,z+latch},{.028,.11,.035},darkPlate);
                        box(m,{side*1.044,-.21,z+latch},{.018,.036,.026},ochre);
                    }
                }
            }
        }
    } else if (kind == ActivityKind::Ferry) {
        for (int side : {-1, 1}) {
            tri(m, {0, .16, -1.7}, {side * 1.2, 0, .7}, {0, -.16, .9}, hull);
            tri(m, {0, .16, -1.7}, {0, .28, .8}, {side * 1.2, 0, .7}, hull);
            tri(m, {0, -.16, .9}, {side * 1.2, 0, .7}, {0, .28, .8}, hull * .65f);
        }
        if (detail){
            armour(m,{0,.23,-.36},{.40,.26,1.31},.065,hull);
            armour(m,{0,.37,-.58},{.29,.13,.55},.029,glass);
            for(double side:{-1.,1.}){
                armour(m,{side*.44,-.015,.91},{.34,.29,.85},.064,hull*.76f);
                engineBell(m,{side*.44,0,1.325},.133,.18,detail);
                radiatorBank(m,{side*.58,.047,.25},{.26,.48},detail);
                spar(m,{side*.26,.10,-.39},{side*.96,.055,.65},.029,trim);
            }
        }
    } else if (kind == ActivityKind::Patrol) {
        armour(m,{0,.022,-.41},{.62,.28,1.80},.10,hull);
        armour(m,{0,.20,-.69},{.39,.21,.58},.047,glass);
        for (int side : {-1, 1}) {
            const double sign=double(side);
            for (double y : {-.08,.064})
                tri(m,{sign*.23,y,-.62},{sign*.94,y,.69},{sign*.19,y,.82},trim*.64f);
            armour(m,{sign*.65,-.033,.69},{.34,.31,1.20},.073,hull*.72f);
            engineHarness(m,{sign*.65,-.03,1.09},.135,detail);
            if (detail) {
                engineBell(m,{sign*.65,-.03,1.284},.137,.20,detail);
                spar(m,{sign*.65,.096,.90},{sign*.67,.37,1.14},.06,trim*.74f);
                radiatorBank(m,{sign*.65,.135,.49},{.21,.47},detail);
                box(m,{sign*.36,-.05,-.70},{.048,.054,.38},hull*.47f);
                box(m,{sign*.36,-.05,-.90},{.029,.030,.034},trim);
            }
        }
    } else if (kind == ActivityKind::Survey) {
        appendSphere(m, {}, 1, hull, detail ? 14 : 6, detail ? 28 : 12);
        for (auto& v : m.vertices) {
            v.y *= .29f;
            v.z *= 1.4f;
        }
        if (detail) {
            ring(m, 1.05, .025, 0, trim, 64);
            armour(m, {0, .32, -.3}, {.48, .22, .75},.048,glass);
            for(double side:{-1.,1.}){
                radiatorBank(m,{side*.59,.22,.14},{.34,.66},detail);
                engineBell(m,{side*.44,0,1.22},.13,.20,detail);
                engineHarness(m,{side*.44,0,1.05},.135,detail);
                spar(m,{side*.70,.12,-.48},{side*.93,.25,-.72},.025,trim);
            }
        }
    } else {
        appendSphere(m, {}, 1, {1, 1, 1}, detail ? 6 : 3, detail ? 10 : 6);
        for (auto& v : m.vertices) {
            const float f = .85f + .14f * std::sin(v.x * 9 + v.z * 5);
            v.x *= f;
            v.y *= .66f;
        }
    }
    m.compact();
    return m;
}
Mesh xalariaVesselCollar() {
    Mesh mesh;
    const glm::vec3 frame(.38f,.45f,.47f), rim(.69f,.65f,.44f);
    box(mesh, {-.35,0,0}, {1.42,3.0,3.1}, hull);
    for(double side:{-1.,1.}){
        box(mesh,{.42,side*1.18,0},{.63,.52,2.85},frame);
        box(mesh,{.42,0,side*1.23},{.63,2.25,.48},frame);
        for(double edge:{-1.,1.})
            box(mesh,{.74,edge*1.15,side*1.20},{.055,.17,.19},rim);
    }
    mesh.compact();
    return mesh;
}

Mesh xalariaVesselCollarMount() {
    Mesh mesh;
    box(mesh,{-.5,0,0},{1.,2.72,2.78},hull);
    for(double side:{-1.,1.}){
        box(mesh,{-.5,side*1.34,0},{.96,.18,2.62},trim*.65f);
        box(mesh,{-.5,0,side*1.38},{.95,1.45,.13},hull*.42f);
    }
    mesh.compact();
    return mesh;
}

Mesh spaceJunkMesh(SpaceJunkKind kind,unsigned detail) {
    Mesh mesh;
    const glm::vec3 alloy(.46,.48,.44), scorched(.12,.11,.10), exposed(.66,.44,.24), cells(.16,.28,.42);
    const auto panel=[&](glm::dvec3 origin,unsigned columns) {
        box(mesh,origin,{.25*columns,.042,.61},alloy);
        for (unsigned x=0;x<columns;++x)
            for (int row=-1;row<=1;++row)
                box(mesh,origin+glm::dvec3(.25*(double(x)-double(columns-1)*.5),.026,row*.195),
                    {.226,.012,.179},cells*(row==0?.75f:1.f));
    };
    if (kind==SpaceJunkKind::Booster) {
        const unsigned sides=detail>1?20:10;
        drum(mesh,{0,.16,0},.39,1.75,alloy,sides);
        for (double y : {-.48,.67,.95})
            ring(mesh,.394,.021,y,scorched,sides);
        drum(mesh,{0,-.86,0},.25,.31,scorched,sides);
        ring(mesh,.27,.038,-1.03,alloy,sides);
        for (double side : {-1.,1.})
            box(mesh,{side*.387,.49,.03},{.026,.46,.30},exposed);
    } else if (kind==SpaceJunkKind::Satellite) {
        armour(mesh,{}, {.67,.46,.74},.066,alloy);
        box(mesh,{-.24,.23,.04},{.25,.035,.47},exposed);
        spar(mesh,{.21,0,0},{1.08,-.09,-.06},.05,scorched);
        spar(mesh,{-.22,0,.02},{-.54,.13,.36},.07,exposed);
        panel({.85,-.06,-.09},3);
        drum(mesh,{-.02,.36,-.12},.12,.29,scorched,10);
        ring(mesh,.24,.042,.52,alloy,16);
    } else if (kind==SpaceJunkKind::SolarPanel) {
        panel({-.12,0,0},5);
        panel({.33,.021,-.49},2);
        spar(mesh,{-.72,-.02,.29},{.84,-.01,.39},.034,alloy);
        spar(mesh,{-.32,0,-.26},{-.49,.16,-.47},.038,exposed);
        box(mesh,{.57,.04,.11},{.26,.09,.21},scorched,.20);
    }
    mesh.compact();
    return mesh;
}

Mesh landableDebrisMesh(unsigned detail) {
    auto mesh = activityMesh(ActivityKind::Debris, std::max(1u, detail));
    for (auto& vertex : mesh.vertices)
        vertex.y = std::clamp(vertex.y, -float(debrisFaceHeight), float(debrisFaceHeight));
    return mesh;
}

Mesh investigationMesh(unsigned step,unsigned detail) {
    Mesh mesh;
    const glm::vec3 alloy{.24,.30,.34},edge{.66,.73,.75},orange{.94,.40,.06},dark{.045,.06,.075};
    if(step==0){
        box(mesh,{0,0,0},{.75,.12,.55},dark,.05);
        spar(mesh,{-.85,-.1,-.6},{.9,.12,.62},.07,alloy);
        spar(mesh,{-.82,0,.55},{.75,.12,-.65},.055,edge);
        box(mesh,{.5,.13,-.25},{.16,.10,.23},orange,.08);
        if(detail)for(int i=0;i<5;++i)box(mesh,{-.57+i*.25,.14,.11},{.07,.025,.32},alloy,.05);
    }else if(step==1){
        box(mesh,{0,0,0},{.68,.42,.5},orange,.09);
        box(mesh,{0,.43,0},{.56,.025,.39},dark,.05);
        for(double x:{-.6,.6})for(double z:{-.45,.45})box(mesh,{x,0,z},{.07,.47,.07},edge,.06);
        box(mesh,{0,.48,.03},{.12,.035,.13},{.10,.91,.96},.92);
        spar(mesh,{.34,.42,.26},{.45,.95,.25},.026,alloy);
    }else{
        box(mesh,{0,0,0},{.27,.38,.29},alloy,.05);
        for(double x:{-.9,.9}){
            spar(mesh,{0,0,0},{x,0,0},.04,edge);
            box(mesh,{x,0,0},{.54,.035,.38},{.035,.12,.22},.10);
        }
        spar(mesh,{0,.3,0},{0,1.,0},.035,edge);
        box(mesh,{0,1.,0},{.13,.05,.13},{.20,.94,.87},.75);
    }
    mesh.compact();return mesh;
}
} // namespace infinity
