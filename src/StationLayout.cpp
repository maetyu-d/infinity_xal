#include "infinity/StationLayout.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinity {
StationKey stationKey(const ActivityId& id,bool privateVault) {
    const auto seed=mix64(seedOf(id.home)^mix64(id.slot)^0x53544154494f4eULL);
    return {seed,privateVault?StationRole::Vault:StationRole(Random{seed}.bits(0)%4)};
}

StationLayout generateStation(StationKey key) {
    using namespace stationDeck;
    const Random random{key.seed};
    StationLayout result{key,{},{},{}};
    double entry=shipDoor;
    const auto add=[&](StationModuleKind kind,double length,double half,double ceiling) {
        const double end=entry-length;
        result.modules.push_back({kind,{end,centre-half,entry,centre+half},ceiling,{}});
        entry=end;
    };
    add(StationModuleKind::Airlock,shipDoor-bridgeDoor,doorHalf,1.19);
    const unsigned segments=key.vessel?2:3+random.bits(1)%4;
    for (unsigned i=0;i<segments;++i)
        add(StationModuleKind::Bridge,ribPitch,bridgeHalf,1.19);

    const auto serviceRoom=[&](StationModuleKind kind,unsigned ribs,double half,double ceiling) {
        add(kind,ribs*ribPitch,half,ceiling);
        const auto& room=result.modules.back().floor;
        auto& fixtures=result.modules.back().fixtures;
        for (double side : {-1.,1.}) {
            const auto z=centre+side*(half-.36);
            fixtures.push_back({room.left+.60,z-.27,room.right-.60,z+.27});
        }
    };
    if(key.vessel){
        const auto kind=key.role==StationRole::Research?StationModuleKind::Laboratory:
                        key.role==StationRole::Habitat?StationModuleKind::Habitat:
                        key.role==StationRole::Industrial?StationModuleKind::Workshop:StationModuleKind::Concourse;
        serviceRoom(kind,1+random.bits(2)%2,1.37,1.88);
    }else switch (key.role) {
    case StationRole::Freight:
        if (random.bits(2)%2) serviceRoom(StationModuleKind::Concourse,2,1.50,1.66);
        break;
    case StationRole::Habitat:
        serviceRoom(StationModuleKind::Concourse,2,1.59,1.79);
        serviceRoom(StationModuleKind::Habitat,3,2.26,2.30);
        break;
    case StationRole::Research:
        serviceRoom(StationModuleKind::Laboratory,3,2.13,2.13);
        break;
    case StationRole::Industrial:
        serviceRoom(StationModuleKind::Workshop,2+random.bits(2)%2,2.04,1.97);
        break;
    case StationRole::Vault:
        serviceRoom(StationModuleKind::Vault,2,1.69,1.72);
        break;
    }

    const double bayHalf=key.vessel?1.84+.12*double(random.bits(3)%3):2.45+.25*double(random.bits(3)%4);
    const double bayLength=key.vessel?3+random.bits(4)%2:4+random.bits(4)%3;
    add(StationModuleKind::Freight,bayLength*ribPitch,bayHalf,2.20+random.unit(5)*.32);
    const auto end=result.freightBay().floor.left;
    result.freightPad={end+(key.vessel?1.05:.55),centre-.49,end+(key.vessel?2.05:1.80),centre+.49};
    auto& supplies=result.modules.back().fixtures;
    supplies.push_back(result.freightPad);
    if(key.vessel){
        supplies.push_back({end+.78,centre+.91,end+2.03,centre+bayHalf-.15});
        return result;
    }
    for (double side : {-1.,1.}) {
        const double z=centre+side*(bayHalf-.73);
        supplies.push_back({end+.73,z-.51,end+2.16+random.unit(side<0?6:7)*.57,z+.51});
    }

    const auto addAnnex=[&](size_t parent,int side,StationModuleKind kind,double depth) {
        auto& host=result.modules[parent];
        const auto area=host.floor;
        const bool nearFreight=host.kind==StationModuleKind::Freight;
        const double door=nearFreight?area.right-1.42:(area.left+area.right)*.5+random.signedUnit(8)*.19;
        constexpr double halfOpening=.63, clearance=.17;
        const double inside=side>0?area.back:area.front, outside=inside+side*depth;
        const double left=nearFreight?area.right-3.41:area.left+.22;
        const double right=area.right-.22;
        StationAnnex annex{parent,side,{kind,{left,std::min(inside,outside),right,std::max(inside,outside)},
                                       kind==StationModuleKind::Observation?2.08:1.89,{}},
                           door-halfOpening,door+halfOpening};
        const auto place=[&](double from,double to,double near,double far) {
            const double a=inside+side*near,b=inside+side*far;
            annex.room.fixtures.push_back({from,std::min(a,b),to,std::max(a,b)});
        };
        if (kind==StationModuleKind::Observation) {
            const bool reverse=random.bits(10)%2!=0;
            const auto loungeFixture=[&](double from,double to,double near,double far) {
                place(reverse?left+(right-to):from,reverse?left+(right-from):to,near,far);
            };
            loungeFixture(left+.12,left+.78,.43,depth-.27);             // Optics counter
            loungeFixture(right-.84,right-.15,.37,depth-.27);           // Banquette
            loungeFixture(right-1.31,right-.86,depth-.98,depth-.34);     // Side table
        } else {
            const double fixtureWidth=kind==StationModuleKind::Analysis?.68:.46;
            place(left+.15,left+.15+fixtureWidth,depth*.27,depth*.81);
            place(right-.15-fixtureWidth,right-.15,depth*.27,depth*.81);
        }

        // Split the parent room's furnishings around the new doorway.
        std::vector<DeckArea> fixtures;
        for (auto item : host.fixtures) {
            const bool alongWall=side>0?item.front>centre:item.back<centre;
            const auto near=annex.doorwayLeft-clearance, far=annex.doorwayRight+clearance;
            if (!alongWall || item.right<=near || item.left>=far) {
                fixtures.push_back(item);
                continue;
            }
            if (near-item.left>=.54)
                fixtures.push_back({item.left,item.front,near,item.back});
            if (item.right-far>=.54)
                fixtures.push_back({far,item.front,item.right,item.back});
        }
        host.fixtures=std::move(fixtures);
        result.annexes.push_back(std::move(annex));
    };
    const auto last=result.modules.size()-1;
    switch (key.role) {
    case StationRole::Freight:
        addAnnex(last,1,StationModuleKind::Dispatch,2.45);
        break;
    case StationRole::Habitat:
        addAnnex(last-1,1,StationModuleKind::Observation,3.25+random.unit(9)*.48);
        break;
    case StationRole::Research:
        addAnnex(last-1,-1,StationModuleKind::Analysis,2.81);
        addAnnex(last-1,1,StationModuleKind::Observation,2.97+random.unit(9)*.44);
        break;
    case StationRole::Industrial:
        addAnnex(last-1,random.bits(9)%2?1:-1,StationModuleKind::Engineering,2.68);
        break;
    case StationRole::Vault:
        addAnnex(last-1,-1,StationModuleKind::Archive,2.33);
        break;
    }
    return result;
}

const StationModule* StationLayout::roomAt(glm::dvec2 position) const {
    const auto room=std::find_if(modules.begin(),modules.end(),[&](const auto& module){
        return module.floor.contains(position);
    });
    if (room!=modules.end()) return &*room;
    const auto wing=std::find_if(annexes.begin(),annexes.end(),[&](const auto& annex){
        return annex.room.floor.contains(position);
    });
    return wing==annexes.end()?nullptr:&wing->room;
}

const StationAnnex* StationLayout::annexAt(size_t parent,int side) const {
    const auto annex=std::find_if(annexes.begin(),annexes.end(),[&](const auto& room){
        return room.parent==parent&&room.side==side;
    });
    return annex==annexes.end()?nullptr:&*annex;
}

std::string_view stationRoleName(StationRole role) {
    constexpr std::array names{"Freight terminal","Orbital habitat","Research station","Industrial station","Private vault"};
    return names.at(size_t(role));
}

std::string_view stationModuleName(StationModuleKind kind) {
    constexpr std::array names{"Airlock","Docking bridge","Concourse","Habitation gallery",
                               "Laboratory","Engineering workshop","Vault antechamber","Station loading bay",
                               "Observation lounge","Analysis lab","Maintenance bay","Dispatch office","Vault archive"};
    return names.at(size_t(kind));
}

glm::vec3 stationAccent(StationRole role) {
    constexpr std::array<glm::vec3,5> accents{{
        {1.f,.64f,.29f},{.41f,.91f,.72f},{.49f,.76f,1.f},{1.f,.46f,.25f},{.78f,.63f,1.f}
    }};
    return accents.at(size_t(role));
}

std::array<TrafficCraft,3> sampleStationTraffic(StationKey key,double seconds) {
    constexpr double pi=std::numbers::pi;
    const Random random{key.seed};
    const auto phase=[&](double period,double offset) {
        const double t=std::fmod(seconds+offset,period);
        return t<0.?t+period:t;
    };
    const auto ease=[](double t) { return t*t*(3.-2.*t); };
    const auto attitude=[](glm::dvec3 forward,double bank) {
        return glm::quatLookAtRH(glm::normalize(forward),glm::dvec3(0,1,0))*
               glm::angleAxis(bank,glm::dvec3(0,0,-1));
    };
    std::array<TrafficCraft,3> ships;
    for (size_t i=0;i<stationTrafficPads.size();++i) {
        const bool cargo=i==1;
        constexpr double arrival=34., turnaround=18., departure=32.;
        const double clock=phase(arrival+turnaround+departure,(cargo?41.:14.)+random.unit(28+i)*8.);
        const auto pad=stationTrafficPads[i].centre;
        const double side=cargo?1.:-1.;
        const double radius=cargo?6.6:4.2;
        const auto dock=pad+glm::dvec3(0,cargo?4.88:1.05,0);
        auto& ship=ships[i];
        ship.kind=cargo?ActivityKind::Freighter:ActivityKind::Ferry;
        ship.radius=radius;
        ship.paint=cargo?glm::vec3(1.65f,1.23f,.80f):glm::vec3(1.06f,1.60f,1.85f);
        ship.strobe=phase(2.3,i*.71)<.095;
        if (clock>=arrival && clock<arrival+turnaround) {
            const double rotate=ease(std::clamp((clock-arrival-11.)/7.,0.,1.));
            ship.position=dock;
            ship.orientation=glm::angleAxis(rotate*pi,glm::dvec3(0,1,0));
            ship.thrust=.035+.15*std::sin(rotate*pi);
            ship.phase=TrafficPhase::AtBerth;
            continue;
        }
        const bool inbound=clock<arrival;
        const double u=inbound?clock/arrival:(clock-arrival-turnaround)/departure;
        // Match tangents at the far end of the circuit and ease to a stop at the pad.
        const double t=inbound?u+u*u*(1.-u):u*u*(2.-u), s=1.-t;
        const auto far=dock+glm::dvec3(side*(cargo?258.:230.),60.,560.);
        const auto tangent=glm::dvec3(-side,0.,.32);
        const auto a=inbound?far:dock;
        const auto b=inbound?far+170.*tangent:dock+glm::dvec3(0,0,95.);
        const auto c=inbound?dock+glm::dvec3(0,0,85.):far-160.*tangent;
        const auto d=inbound?dock:far;
        ship.position=a*(s*s*s)+b*(3.*s*s*t)+c*(3.*s*t*t)+d*(t*t*t);
        const auto heading=(b-a)*(s*s)+(c-b)*(2.*s*t)+(d-c)*(t*t);
        ship.orientation=attitude(heading,side*(inbound?.16:-.19)*std::sin(pi*t));
        const double throttle=inbound?1.-ease(u):ease(u);
        ship.thrust=.035+.67*throttle;
        const double parameterRate=inbound?(1.+2.*u-3.*u*u)/arrival:(4.*u-3.*u*u)/departure;
        ship.velocity=heading*(3.*parameterRate);
        ship.phase=inbound?TrafficPhase::Inbound:TrafficPhase::Outbound;
    }
    const double period=42.+random.unit(31)*6.;
    const double angle=phase(period,period*.16+random.unit(32)*period)*2.*pi/period;
    const double s=std::sin(angle),c=std::cos(angle);
    ships.back()={{221.*c,115.-33.*s,54.+229.*s},
                  attitude({-221.*s,-33.*c,229.*c},.16),
                  {1.69f,1.34f,.90f},ActivityKind::Freighter,15.6,.80,phase(2.6,1.47)<.095};
    ships.back().velocity=glm::dvec3(-221.*s,-33.*c,229.*c)*(2.*pi/period);
    return ships;
}

namespace {
TrafficCraft trafficOrbit(glm::dvec3 centre, glm::dvec2 axes, double angle, double period,double seconds,
                          ActivityKind kind, double radius, glm::vec3 paint) {
    const double s=std::sin(angle), c=std::cos(angle);
    const glm::dvec3 position=centre+glm::dvec3(axes.x*c,0.,axes.y*s);
    const auto tangent=glm::dvec3(-axes.x*s,0.,axes.y*c);
    const auto direction=glm::normalize(tangent);
    const auto rotation=glm::quatLookAtRH(direction,glm::dvec3(0,1,0))*
                        glm::angleAxis(.12+.09*s,glm::dvec3(0,0,-1));
    const double signal=std::abs(std::remainder(seconds+radius*.073,3.1));
    return {position,rotation,paint,kind,radius,.60+.13*c,signal<.055,
            tangent*(2.*std::numbers::pi/period),TrafficPhase::OnCircuit};
}

double orbitAngle(double seconds,double period,double phase) {
    return std::remainder(seconds,period)*2.*std::numbers::pi/period+phase;
}
} // namespace

std::array<TrafficCraft,5> sampleOrbitalTraffic(StationKey key,double seconds) {
    const Random random{key.seed};
    const double side=random.unit(61)>.5?1.:-1.;
    const double height=520.+random.unit(62)*330.;
    const glm::dvec3 convoy{side*(3020.+420.*random.unit(63)),height,8940.};
    const glm::dvec2 convoyAxes{1030.+random.unit(64)*200.,7540.+random.unit(65)*640.};
    const double freightPeriod=157.+random.unit(66)*32.,shuttlePeriod=79.+random.unit(68)*23.,
                 servicePeriod=125.+random.unit(70)*20.;
    const double leg=orbitAngle(seconds,freightPeriod,random.unit(67)*6.28);
    const double shuttle=orbitAngle(seconds,shuttlePeriod,random.unit(69)*6.28);
    const double service=orbitAngle(seconds,servicePeriod,random.unit(71)*6.28);
    const bool research=key.role==StationRole::Research;
    // The second freighter and its escort share the lead ship's clock. Their
    // vertical and lateral offsets give them clearance through the turns.
    return {{
        trafficOrbit(convoy,convoyAxes,leg,freightPeriod,seconds,ActivityKind::Freighter,78.,{.94f,.77f,.59f}),
        trafficOrbit(convoy+glm::dvec3(side*220.,300.,0.),convoyAxes,leg-.19,freightPeriod,seconds,
                     ActivityKind::Freighter,62.,{.65f,.81f,.92f}),
        trafficOrbit(convoy+glm::dvec3(side*550.,500.,0.),convoyAxes,leg-.12,freightPeriod,seconds,
                     ActivityKind::Patrol,32.,{.71f,.92f,.96f}),
        trafficOrbit({-side*2590.,650.,5500.},{820.,4400.},shuttle,shuttlePeriod,seconds,ActivityKind::Ferry,
                     39.,{.92f,.81f,.56f}),
        trafficOrbit({-side*4760.,1310.,8100.},{1050.,6240.},service,servicePeriod,seconds,
                     research?ActivityKind::Survey:ActivityKind::Patrol,51.,{.63f,.84f,1.04f})
    }};
}

std::array<TrafficCraft,2> sampleWormholeTraffic(double radius,double seconds) {
    const auto service=orbitAngle(seconds,56.,.72);
    const auto relay=orbitAngle(seconds,72.,2.85);
    return {{
        trafficOrbit(radius*glm::dvec3(-2.45,-.58,.86),radius*glm::dvec2(.39,1.58),service,56.,seconds,
                     ActivityKind::Ferry,42.,{.85f,.97f,1.07f}),
        trafficOrbit(radius*glm::dvec3(2.68,-.90,.40),radius*glm::dvec2(.48,1.72),relay,72.,seconds,
                     ActivityKind::Survey,60.,{.91f,.72f,.53f})
    }};
}

std::array<glm::dvec3,4> wormholeBuoys(double radius) {
    return {{radius*glm::dvec3(-1.53,-.64,.48),radius*glm::dvec3(-1.53,.64,.48),
             radius*glm::dvec3(1.53,-.64,.48),radius*glm::dvec3(1.53,.64,.48)}};
}

std::array<SpaceJunk,8> sampleSpaceJunk(StationKey key,double seconds) {
    const Random random{mix64(key.seed^0x4a554e4bULL)};
    const glm::dvec3 centre{2900.*(random.unit(0)>.5?1.:-1.),3200.+random.unit(1)*500.,
                            4350.+random.unit(2)*1650.};
    std::array<SpaceJunk,8> pieces;
    for (size_t i=0;i<pieces.size();++i) {
        const Random piece{random.bits(i+3)};
        // Drift the field above the traffic and tumble each fragment independently.
        const double drift=orbitAngle(seconds,750.+piece.unit(0)*310.,piece.unit(1)*6.28);
        const double bearing=2.*std::numbers::pi*double(i)/pieces.size();
        const double separation=260.+piece.unit(2)*690.;
        const auto offset=glm::dvec3(separation*std::cos(bearing)+120.*std::sin(drift),
                                    piece.signedUnit(3)*430.+110.*std::cos(drift),
                                    separation*std::sin(bearing)+185.*std::cos(drift));
        const auto axis=glm::normalize(glm::dvec3(.3+piece.unit(4),piece.signedUnit(5),piece.signedUnit(6)));
        const auto spin=orbitAngle(seconds,25.+piece.unit(7)*120.,piece.unit(8)*6.28);
        const auto kind=SpaceJunkKind(i%size_t(SpaceJunkKind::Count));
        const double radius=kind==SpaceJunkKind::Booster ? 31.+piece.unit(9)*19. :
                            kind==SpaceJunkKind::Satellite ? 19.+piece.unit(9)*14. : 8.+piece.unit(9)*13.;
        pieces[i]={centre+offset,glm::angleAxis(spin,axis),radius,kind};
    }
    return pieces;
}

std::array<glm::dvec2,stationDeck::bridgeArchSegments+3> bridgeGlassProfile(const StationModule& module,
                                                                          double offset) {
    using namespace stationDeck;
    const double half=(module.floor.back-module.floor.front)*.5;
    const double middle=(module.floor.back+module.floor.front)*.5;
    const double rise=module.ceiling-glassShoulder;
    std::array<glm::dvec2,bridgeArchSegments+3> profile;
    profile.front()={glassSill,module.floor.front-offset};
    profile.back()={glassSill,module.floor.back+offset};
    for (size_t i=0;i<=bridgeArchSegments;++i) {
        const double angle=std::numbers::pi*double(i)/bridgeArchSegments;
        const glm::dvec2 radial{std::sin(angle),-std::cos(angle)};
        const auto normal=glm::normalize(radial/glm::dvec2(rise,half));
        profile[i+1]=glm::dvec2(glassShoulder,middle)+radial*glm::dvec2(rise,half)+offset*normal;
    }
    return profile;
}

Mesh stationGlazing(const StationLayout& station) {
    const auto tint=glm::mix(glm::vec3(.47f,.73f,.81f),stationAccent(station.key.role),.15f);
    Mesh panes;
    const auto pane=[&](glm::dvec3 a,glm::dvec3 b,glm::dvec3 c,glm::dvec3 d) {
        const auto first=uint32_t(panes.vertices.size());
        for (const auto p : {a,b,c,d})
            panes.vertices.push_back({float(p.x),float(p.y),float(p.z),rgba(tint)});
        panes.indices.insert(panes.indices.end(),{first,first+1,first+2,first,first+2,first+3});
    };
    for (const auto& module : station.modules) {
        if (module.kind!=StationModuleKind::Bridge)
            continue;
        const auto profile=bridgeGlassProfile(module);
        for (size_t i=1;i<profile.size();++i) {
            const auto a=profile[i-1], b=profile[i];
            pane({module.floor.left,a},{module.floor.right,a},{module.floor.right,b},{module.floor.left,b});
        }
    }
    for (const auto& annex : station.annexes) {
        if (!annex.panoramic()) continue;
        using namespace stationDeck;
        const auto& area=annex.room.floor;
        const double z=annex.outsideWall();
        pane({area.left+panoramaInset,panoramaSill,z},{area.right-panoramaInset,panoramaSill,z},
             {area.right-panoramaInset,panoramaHead,z},{area.left+panoramaInset,panoramaHead,z});
    }
    panes.compact();
    return panes;
}
} // namespace infinity
