#include "infinity/XalariaTraffic.hpp"
#include "infinity/XalariaCombat.hpp"
#include <algorithm>
#include <iomanip>
#include <locale>
#include <set>
#include <sstream>

namespace infinity {
namespace {
constexpr uint32_t stationBits=0x0fffffffu;
constexpr std::array<uint32_t,3> laneStart{0,3,8};
constexpr std::array<uint32_t,3> laneCount{3,5,2};
constexpr double hardStrikeSpeed=12.,inspectionSeconds=8.5,lightRecoverySeconds=5.,heavyRecoverySeconds=9.;

double recoveryDuration(double started,double clearanceAt) {
    return clearanceAt>started?heavyRecoverySeconds:lightRecoverySeconds;
}

glm::dvec3 capped(glm::dvec3 value,double limit) {
    const double length=glm::length(value);
    return length>limit?value*(limit/length):value;
}

double segmentDistance(glm::dvec3 point,glm::dvec3 from,glm::dvec3 to) {
    const auto delta=to-from;
    const double part=std::clamp(glm::dot(point-from,delta)/std::max(1e-30,glm::dot(delta,delta)),0.,1.);
    return glm::length(point-(from+delta*part));
}

struct HullBox {
    glm::dvec3 centre,half;
};

// These envelopes follow the fuselage, pods and wings of the traffic meshes.
// Keeping the parts separate leaves room beside the hull to use the collar.
std::span<const HullBox> trafficHull(ActivityKind kind) {
    static constexpr std::array freighter{
        HullBox{{0,.02,.12},{.415,.21,1.215}},HullBox{{0,.26,-1.23},{.435,.24,.38}},
        HullBox{{-.71,-.30,.11},{.319,.251,.85}},HullBox{{.71,-.30,.11},{.319,.251,.85}},
        HullBox{{-.91,.045,.86},{.245,.185,.46}},HullBox{{.91,.045,.86},{.245,.185,.46}},
        HullBox{{0,.03,.94},{.89,.08,.08}},HullBox{{0,.03,-.81},{.89,.075,.075}}
    };
    static constexpr std::array ferry{
        HullBox{{0,.075,-.95},{.26,.16,.75}},HullBox{{0,.04,-.04},{.55,.18,.57}},
        HullBox{{0,.015,.55},{1.05,.16,.30}},HullBox{{-.44,0,1.02},{.125,.11,.21}},
        HullBox{{.44,0,1.02},{.125,.11,.21}}
    };
    static constexpr std::array patrol{
        HullBox{{0,.045,-.43},{.31,.16,.90}},HullBox{{0,.20,-.69},{.195,.105,.29}},
        HullBox{{-.65,-.033,.69},{.17,.155,.62}},HullBox{{.65,-.033,.69},{.17,.155,.62}},
        HullBox{{0,-.008,.53},{.91,.08,.19}}
    };
    static constexpr std::array survey{
        HullBox{{0,0,0},{.93,.29,.92}},HullBox{{0,0,0},{.53,.25,1.37}},
        HullBox{{0,.36,-.30},{.24,.08,.375}}
    };
    switch(kind) {
    case ActivityKind::Freighter:return freighter;
    case ActivityKind::Ferry:return ferry;
    case ActivityKind::Raider:case ActivityKind::Patrol:return patrol;
    case ActivityKind::Survey:return survey;
    default:return {};
    }
}

struct BoxImpact {
    double fraction;
    glm::dvec3 surface,normal;
};

std::optional<BoxImpact> sweepRoundedBox(glm::dvec3 from,glm::dvec3 to,const HullBox& box,double radius) {
    const auto a=from-box.centre,b=to-box.centre;
    const auto margin=box.half+radius;
    if(glm::any(glm::greaterThan(glm::min(a,b),margin)) ||
       glm::any(glm::lessThan(glm::max(a,b),-margin)))return {};

    const auto delta=b-a;
    const auto point=[&](double t){return a+delta*t;};
    const auto outside=[&](glm::dvec3 p){return p-glm::clamp(p,-box.half,box.half);};
    const auto contact=[&](double t){
        const auto d=outside(point(t));
        return glm::dot(d,d)<=radius*radius;
    };

    double fraction=0.;
    if(!contact(0.)){
        // Squared distance to the box is convex along a line. Find its
        // minimum, then the first entry on the decreasing part of the sweep.
        double lo=0.,hi=1.;
        for(unsigned i=0;i<24;++i){
            const double mid=(lo+hi)*.5;
            if(glm::dot(delta,outside(point(mid)))<0.)lo=mid;
            else hi=mid;
        }
        if(!contact(hi))return {};
        lo=0.;
        for(unsigned i=0;i<24;++i){
            const double mid=(lo+hi)*.5;
            if(contact(mid))hi=mid;
            else lo=mid;
        }
        fraction=hi;
    }
    const auto at=point(fraction);
    const auto d=outside(at);
    auto surface=at-d;
    glm::dvec3 normal(0);
    if(glm::dot(d,d)>1e-18)normal=glm::normalize(d);
    else {
        const auto depth=box.half-glm::abs(at);
        const int axis=depth.x<=depth.y&&depth.x<=depth.z?0:depth.y<=depth.z?1:2;
        normal[axis]=at[axis]<0.?-1.:1.;
        surface[axis]=normal[axis]*box.half[axis];
    }
    return BoxImpact{fraction,box.centre+surface,normal};
}

std::string_view activityName(TrafficService service,TrafficPhase phase) {
    if(service==TrafficService::Wormhole)return "SERVICING THE WORMHOLE";
    if(service==TrafficService::Orbital)return "ON ORBITAL ROUTE";
    switch(phase) {
    case TrafficPhase::Inbound: return "INBOUND TO AUXILIARY BERTH";
    case TrafficPhase::AtBerth: return "SECURE AT AUXILIARY BERTH";
    case TrafficPhase::Outbound: return "CLEARING THE AUXILIARY BERTH";
    case TrafficPhase::OnCircuit: return "ON STATION CIRCUIT";
    }
    return {};
}

std::string_view className(TrafficService service,ActivityKind kind) {
    if(service==TrafficService::Wormhole)
        return kind==ActivityKind::Ferry?"Gate tender":"Relay survey";
    switch(kind) {
    case ActivityKind::Freighter: return service==TrafficService::Port?"Cargo tug":"Freighter";
    case ActivityKind::Ferry: return service==TrafficService::Port?"Shuttle":"Ferry";
    case ActivityKind::Survey: return "Survey ship";
    default: return "Patrol";
    }
}
}

bool isXalariaTraffic(const ActivityId& id) {
    return isStrategyActor(id)||isConvoyActor(id)||(id.home.kind()==Kind::Planet&&(isCombatActor(id)||((id.slot&trafficActivityTag)&&(id.slot&15u)<10u)));
}

std::optional<ActivityId> xalariaOrbitalFreighter(const XalariaEconomy& economy,const XPlace& port) {
    const auto station=economy.orbitalStationId(port);
    if(!station)return {};
    return ActivityId{station->home,trafficActivityTag|(station->slot<<4)|laneStart[size_t(TrafficService::Orbital)]};
}

double trafficShieldCapacity(const ActivityBody& body) {
    if(!isXalariaTraffic(body.id)||body.radius<minimumTrafficCollarRadius)return 0.;
    if(body.kind==ActivityKind::Patrol)return 54.;
    if(body.kind==ActivityKind::Raider)return 42.;
    if(body.kind==ActivityKind::Survey)return 24.;
    if((body.id.slot&15u)>=laneStart[size_t(TrafficService::Wormhole)])return 20.;
    if(body.kind==ActivityKind::Freighter&&body.radius>=40.)return 34.;
    return 0.;
}

ShieldReadout XalariaEconomy::trafficShield(const ActivityBody& body,double seconds,bool lowered) const {
    const double capacity=trafficShieldCapacity(body);
    const auto found=trafficShields_.find(body.id);
    const bool deployed=!lowered&&!body.disabled&&body.hull>0.&&(!combat_||!combat_->shieldsLowered(body.id));
    return found==trafficShields_.end()?ShieldCharge{}.read(capacity,seconds,deployed):
                                       found->second.read(capacity,seconds,deployed);
}

XalariaEconomy::TrafficRecovery XalariaEconomy::TrafficRecovery::at(double seconds) const {
    const double elapsed=seconds-started,duration=recoveryDuration(started,clearanceAt);
    TrafficRecovery result;
    if(elapsed<0.||elapsed>=duration)return result;
    const double damping=clearanceAt>started?.80:1.38;
    const double easing=std::clamp(elapsed-duration+1.,0.,1.);
    const double fade=(1.-easing)*(1.-easing)*(1.+2.*easing);
    const double fadeRate=-6.*easing*(1.-easing);
    const double decay=std::exp(-damping*elapsed);
    const auto solve=[&](glm::dvec3 initial,glm::dvec3 rate,glm::dvec3& value,glm::dvec3& slope){
        const auto linear=rate+damping*initial;
        const auto curve=initial+linear*elapsed;
        value=curve*(decay*fade);
        slope=decay*(linear*fade+curve*(fadeRate-damping*fade));
    };
    solve(offset,velocity,result.offset,result.velocity);
    solve(turn,spin,result.turn,result.spin);
    return result;
}

bool XalariaEconomy::reactToTrafficStrike(const TrafficStrike& strike,double seconds) {
    if(!isXalariaTraffic(strike.target)||!std::isfinite(strike.closingSpeed)||strike.closingSpeed<=.15||
       !std::isfinite(strike.targetRadius)||strike.targetRadius<=0.||!std::isfinite(seconds)||seconds<0.||
       !std::isfinite(glm::length(strike.away))||!std::isfinite(glm::length(strike.leverage)))return false;
    if(!trafficRecovery_.contains(strike.target)&&trafficRecovery_.size()>=64)return false;
    auto& current=trafficRecovery_[strike.target];
    const bool hard=strike.closingSpeed>=hardStrikeSpeed;
    const bool waveOff=hard&&current.clearanceAt<=seconds;
    auto response=current.at(seconds);
    response.started=seconds;
    response.clearanceAt=hard?seconds+inspectionSeconds:current.clearanceAt;
    const double transfer=std::clamp(7./strike.targetRadius,.10,.64);
    ActivityBody vessel;
    vessel.id=strike.target;vessel.kind=strike.kind;vessel.radius=strike.targetRadius;
    const double capacity=trafficShieldCapacity(vessel);
    double transferScale=1.;
    if(capacity>0.&&!strike.clearance&&(trafficShields_.contains(strike.target)||trafficShields_.size()<64)){
        const double load=std::min(1e6,.42*strike.closingSpeed+.3);
        const auto absorbed=trafficShields_[strike.target].absorb(load,capacity,seconds,strike.shieldImpact);
        transferScale=std::lerp(1.,.60,absorbed/load);
    }
    const auto push=capped(strike.away*((transfer*strike.closingSpeed+(hard?2.4:0.))*transferScale),9.);
    response.velocity=capped(response.velocity+push,9.);
    // Keep repeat hits within the local flight lane while preserving the
    // current displacement and angle at the instant of the strike.
    const double distance=glm::length(response.offset);
    if(distance>0.){
        const auto radial=response.offset/distance;
        const double excess=glm::dot(response.velocity,radial)-std::max(0.,20.-distance);
        response.velocity-=radial*std::max(0.,excess);
    }
    response.spin=capped(response.spin+glm::cross(strike.leverage/strike.targetRadius,push)*.028,.25);
    current=response;
    return waveOff;
}

void XalariaEconomy::expireTrafficRecovery(double seconds) {
    std::erase_if(trafficRecovery_,[&](const auto& entry){
        const auto& recovery=entry.second;
        return seconds-recovery.started>=recoveryDuration(recovery.started,recovery.clearanceAt);
    });
    std::erase_if(trafficShields_,[&](const auto& entry){return seconds-entry.second.hitAt>=9.;});
}

double XalariaEconomy::trafficDockingHold(const ActivityId& id,double seconds) const {
    const auto found=trafficRecovery_.find(id);
    return found!=trafficRecovery_.end()&&seconds>=found->second.started?
        std::max(0.,found->second.clearanceAt-seconds):0.;
}

void XalariaEconomy::applyTrafficRecovery(ActivityBody& body,double seconds) const {
    if(isXalariaTraffic(body.id)&&!isConvoyActor(body.id)&&!isStrategyActor(body.id)){
        const ActivityId station{body.id.home,(body.id.slot&stationBits)>>4};
        body.velocity*=trafficClock(station,seconds).second;
        if(const auto port=orbitalPlace(station)){
            const auto advisory=trafficAdvisory(*port,seconds);
            if(!advisory.empty())body.status+=" / "+advisory;
        }
    }
    const auto found=trafficRecovery_.find(body.id);
    if(found==trafficRecovery_.end()){if(combat_)combat_->apply(body,seconds);return;}
    const auto& recovery=found->second;
    const double age=seconds-recovery.started;
    if(age<0.||age>=recoveryDuration(recovery.started,recovery.clearanceAt)){if(combat_)combat_->apply(body,seconds);return;}
    const auto motion=recovery.at(seconds);
    body.position+=motion.offset;
    body.velocity+=motion.velocity;
    const double angle=glm::length(motion.turn);
    if(angle>1e-12)
        body.orientation=glm::angleAxis(angle,motion.turn/angle)*body.orientation;
    if(seconds<recovery.clearanceAt||glm::length(motion.velocity)>.12){
        const auto split=body.status.find(" / ");
        if(split!=std::string::npos)body.status.resize(split);
        body.status+=seconds<recovery.clearanceAt?" / COLLISION RECOVERY":" / CORRECTING COURSE";
    }
    if(combat_)combat_->apply(body,seconds);
}

std::string XalariaEconomy::serializeTraffic(double seconds) const {
    std::ostringstream records;
    records.imbue(std::locale::classic());
    records<<std::setprecision(17);
    size_t count=0;
    for(const auto& [id,state]:trafficRecovery_){
        if(seconds<state.started||seconds-state.started>=recoveryDuration(state.started,state.clearanceAt))continue;
        records<<' '<<id.home.str()<<' '<<id.slot<<' '<<state.started<<' '<<state.clearanceAt;
        for(const auto v:{state.offset,state.velocity,state.turn,state.spin})records<<' '<<v.x<<' '<<v.y<<' '<<v.z;
        ++count;
    }
    std::ostringstream shields;
    shields.imbue(std::locale::classic());
    shields<<std::setprecision(17);
    size_t shieldCount=0;
    for(const auto& [id,state]:trafficShields_){
        if(seconds<state.hitAt||seconds-state.hitAt>=9.)continue;
        shields<<' '<<id.home.str()<<' '<<id.slot<<' '<<state.spent<<' '<<state.hitAt<<' '<<state.strength<<' '
               <<state.direction.x<<' '<<state.direction.y<<' '<<state.direction.z;
        ++shieldCount;
    }
    if(shieldCount)
        return "XALARIA_TRAFFIC 2 "+std::to_string(count)+records.str()+" "+std::to_string(shieldCount)+shields.str();
    if(!count)return {};
    return "XALARIA_TRAFFIC 1 "+std::to_string(count)+records.str();
}

bool XalariaEconomy::restoreTraffic(std::string_view text,double seconds) {
    if(text.empty()){trafficRecovery_.clear();trafficShields_.clear();return true;}
    try{
        std::istringstream in{std::string(text)};
        in.imbue(std::locale::classic());
        std::string tag;
        int version;
        size_t count;
        if(!(in>>tag>>version>>count)||tag!="XALARIA_TRAFFIC"||(version!=1&&version!=2)||count>64)return false;
        std::map<ActivityId,TrafficRecovery> recovered;
        const auto validId=[&](const ActivityId& id){
            return isStrategyActor(id)?(combat_&&combat_->strategy().validIdentity(id)):isConvoyActor(id)?catalog_.stars.contains((id.slot>>4)&65535u):isXalariaTraffic(id)&&orbitalPlace({id.home,(id.slot&stationBits)>>4}).has_value();
        };
        for(size_t i=0;i<count;++i){
            std::string home;
            ActivityId id;
            TrafficRecovery state;
            if(!(in>>home>>id.slot>>state.started>>state.clearanceAt))return false;
            id.home=Address::parse(home);
            if(!validId(id)||
               !std::isfinite(state.started)||!std::isfinite(state.clearanceAt)||state.started<0.||
               state.started>seconds||state.clearanceAt<0.||state.clearanceAt>state.started+inspectionSeconds+1e-7)
                return false;
            for(auto* v:{&state.offset,&state.velocity,&state.turn,&state.spin})
                if(!(in>>v->x>>v->y>>v->z)||!std::isfinite(glm::length(*v)))return false;
            if(glm::length(state.offset)>25.||glm::length(state.velocity)>9.01||
               glm::length(state.turn)>1.||glm::length(state.spin)>.251||
               !recovered.emplace(id,state).second)return false;
        }
        std::map<ActivityId,ShieldCharge> shields;
        if(version==2){
            if(!(in>>count)||count>64)return false;
            for(size_t i=0;i<count;++i){
                std::string home;
                ActivityId id;
                ShieldCharge state;
                if(!(in>>home>>id.slot>>state.spent>>state.hitAt>>state.strength
                       >>state.direction.x>>state.direction.y>>state.direction.z))return false;
                id.home=Address::parse(home);
                if(!validId(id)||!state.valid(54.,seconds)||!shields.emplace(id,state).second)return false;
            }
        }
        std::string extra;
        if(in>>extra)return false;
        trafficRecovery_=std::move(recovered);
        trafficShields_=std::move(shields);
        expireTrafficRecovery(seconds);
        return true;
    }catch(const std::exception&){return false;}
}

std::string xalariaTrafficCallsign(const ActivityId& id) {
    const Random name{mix64(seedOf(id.home)^mix64(id.slot))};
    constexpr std::array callsigns{"Morrow","Argosy","Lark","Sable","Keel","Tern","Beacon","Juniper",
                                   "Vesper","Gannet","Wayfarer","Aster","Kestrel","Flint","Meridian","Spindle"};
    return std::string(callsigns[name.bits(0)%callsigns.size()])+"-"+
           std::to_string(100+10*(name.bits(1)%90)+(id.slot&15u));
}

ActivityBody xalariaTrafficBody(const ActivityId& station,TrafficService service,size_t index,
                               const TrafficCraft& craft,glm::dvec3 centre,glm::dquat axes) {
    ActivityBody b;
    const auto lane=laneStart[size_t(service)]+uint32_t(index);
    b.id={station.home,trafficActivityTag|(station.slot<<4)|lane};
    b.seed=mix64(seedOf(b.id.home)^mix64(b.id.slot));
    b.kind=craft.kind;
    b.position=centre+axes*craft.position;
    b.velocity=axes*craft.velocity;
    b.orientation=axes*craft.orientation;
    b.radius=craft.radius;
    b.tint=craft.paint;
    b.name=xalariaTrafficCallsign(b.id);
    b.status=std::string(className(service,b.kind))+" / "+std::string(activityName(service,craft.phase));
    return b;
}

std::optional<XalariaTrafficContact> xalariaTrafficContact(const XalariaEconomy& economy,const ActivityId& id,
                 const std::optional<XalariaWormholeMouth>& mouth,double seconds) {
    if(economy.combat()&&(isStrategyActor(id)||economy.combat()->strategy().suppresses(id)))return economy.combat()->strategy().contact(id,seconds);
    if(!isXalariaTraffic(id))return {};
    if(isConvoyActor(id)||isCombatActor(id))return economy.combat()?economy.combat()->contact(id,seconds):std::nullopt;
    const ActivityId station{id.home,(id.slot&stationBits)>>4};
    const auto port=economy.orbitalPlace(station);
    if(!port)return {};
    const uint32_t slot=id.slot&15u;
    const auto service=slot>=laneStart[2]?TrafficService::Wormhole:
                       slot>=laneStart[1]?TrafficService::Orbital:TrafficService::Port;
    const auto index=slot-laneStart[size_t(service)];
    if(index>=laneCount[size_t(service)])return {};
    const auto contact=[&](const TrafficCraft& craft,glm::dvec3 centre,glm::dquat axes,
                           std::optional<uint32_t> destination){
        auto body=xalariaTrafficBody(station,service,index,craft,centre,axes);
        economy.applyTrafficRecovery(body,seconds);
        const auto shield=economy.trafficShield(body,seconds,craft.phase==TrafficPhase::AtBerth);
        return XalariaTrafficContact{std::move(body),*port,service,craft.phase,destination,
                                     economy.trafficDockingHold(id,seconds),shield};
    };
    if(service==TrafficService::Wormhole) {
        if(!mouth||mouth->reference!=station.home||port->station!=0)return {};
        const auto craft=sampleWormholeTraffic(mouth->radius,economy.trafficClock(station,seconds).first)[index];
        return contact(craft,mouth->metres,mouth->orientation,mouth->destination);
    }
    const auto berth=dockingBerth(economy.portBody(*port));
    if(!berth)return {};
    const auto key=economy.stationDesignKey(station);
    const auto schedule=economy.trafficClock(station,seconds).first;
    const auto craft=service==TrafficService::Port?sampleStationTraffic(key,schedule)[index]:
                                                       sampleOrbitalTraffic(key,schedule)[index];
    return contact(craft,berth->metres,berth->orientation,{});
}

std::vector<TrafficMotion> nearbyXalariaTraffic(const Universe& universe,const XalariaEconomy& economy,
        const Address& frame,glm::dvec3 eyeMetres,glm::dvec3 velocity,uint32_t system,
        const XalariaWormholeMouth* gate,double seconds,double dt) {
    std::vector<TrafficMotion> traffic;
    if(!(dt>0.)||!std::isfinite(dt))return traffic;
    const auto& catalog=*universe.xalaria();
    const auto finish=eyeMetres+velocity*dt;
    // Leave room for thrust or braking during this frame. Narrow-phase
    // collision still uses each actual physics step, not this estimate.
    const double courseChange=30000.*dt*dt;
    std::set<uint64_t> ownedOrbits;
    for(const auto& asset:economy.assets())
        if(asset.system==system&&asset.kind!=XAssetKind::Farm&&asset.state==XAssetState::Active)
            ownedOrbits.insert(asset.world);
    for(const auto& [world,record]:catalog.star(system).worlds){
        if(!record.stations&&!ownedOrbits.contains(world))continue;
        const auto reference=catalog.address(system).child(Kind::Planet,world);
        const auto pose=universe.relative(frame,reference,0.);
        const auto from=pose.position+pose.orientation*eyeMetres;
        const auto to=pose.position+pose.orientation*finish;
        const double orbit=std::abs(2.2*record.radius-8000.);
        const double orbitWidth=29000.+courseChange;
        if(segmentDistance({},from,to)>orbit+orbitWidth||std::max(glm::length(from),glm::length(to))<orbit-orbitWidth)
            continue;
        const auto append=[&](const ActivityId& station,TrafficService service,glm::dvec3 centre,glm::dquat axes,
                              std::span<const TrafficCraft> first,std::span<const TrafficCraft> last){
            for(size_t i=0;i<first.size();++i){
                const auto base=centre+axes*first[i].position;
                if(segmentDistance(base,from,to)>first[i].radius*2.+66.+courseChange+
                                                   glm::distance(first[i].position,last[i].position))
                    continue;
                auto start=xalariaTrafficBody(station,service,i,first[i],centre,axes);
                auto end=xalariaTrafficBody(station,service,i,last[i],centre,axes);
                if(economy.combat()&&economy.combat()->strategy().suppresses(start.id))continue;
                economy.applyTrafficRecovery(start,seconds);
                economy.applyTrafficRecovery(end,seconds+dt);
                const double reach=start.radius*2.+20.+courseChange+glm::distance(start.position,end.position);
                if(segmentDistance(start.position,from,to)<=reach)
                    traffic.push_back({std::move(start),end.position,end.orientation});
            }
        };
        for(const auto& port:economy.venues(system,world)){
            if(port.city)continue;
            const auto berth=dockingBerth(economy.portBody(port));
            if(!berth)continue;
            const auto range=segmentDistance(berth->metres,from,to);
            const auto key=economy.stationDesignKey(berth->id);
            const auto firstTime=economy.trafficClock(berth->id,seconds).first;
            const auto lastTime=economy.trafficClock(berth->id,seconds+dt).first;
            if(range<20500.+courseChange)
                append(berth->id,TrafficService::Orbital,berth->metres,berth->orientation,
                       sampleOrbitalTraffic(key,firstTime),sampleOrbitalTraffic(key,lastTime));
            if(range<1000.+courseChange)
                append(berth->id,TrafficService::Port,berth->metres,berth->orientation,
                       sampleStationTraffic(key,firstTime),sampleStationTraffic(key,lastTime));
            if(gate&&gate->reference==reference&&port.station==0&&
               segmentDistance(gate->metres,from,to)<gate->radius*4.5+courseChange)
                append(berth->id,TrafficService::Wormhole,gate->metres,gate->orientation,
                       sampleWormholeTraffic(gate->radius,firstTime),sampleWormholeTraffic(gate->radius,lastTime));
        }
    }
    if(economy.combat())for(auto& motion:economy.combat()->collisionMotions(frame,eyeMetres,system,seconds,dt)){
        const auto found=std::find_if(traffic.begin(),traffic.end(),[&](const auto& other){return other.start.id==motion.start.id;});
        if(found==traffic.end())traffic.push_back(std::move(motion));else *found=std::move(motion);
    }
    return traffic;
}

std::optional<TrafficImpact> sweepXalariaTraffic(const Universe& universe,const Address& frame,
        std::span<const TrafficMotion> traffic,glm::dvec3 from,glm::dvec3 to,
        glm::dquat fromAxes,glm::dquat toAxes,double radius,double begin,double end,double dt) {
    std::optional<TrafficImpact> earliest;
    for(const auto& motion:traffic){
        const auto& ship=motion.start;
        const auto pose=universe.relative(frame,ship.id.home,0.);
        const auto inverse=glm::conjugate(pose.orientation);
        const auto p=glm::mix(ship.position,motion.end,begin),q=glm::mix(ship.position,motion.end,end);
        const auto a=glm::slerp(ship.orientation,motion.endOrientation,begin);
        const auto b=glm::slerp(ship.orientation,motion.endOrientation,end);
        const auto first=pose.position+pose.orientation*from-p;
        const auto last=pose.position+pose.orientation*to-q;
        if(segmentDistance({},first,last)>ship.radius*2.+radius*2.5+4.)continue;
        for(double axial:{-1.375,0.,1.375}){
            const glm::dvec3 hullPoint(0,0,axial*radius);
            const auto tail=pose.orientation*(toAxes*hullPoint);
            const auto u=glm::conjugate(a)*(first+pose.orientation*(fromAxes*hullPoint));
            const auto v=glm::conjugate(b)*(last+tail);
            const auto hitBox=[&](const HullBox& box){
                const auto contact=sweepRoundedBox(u,v,box,radius);
                if(!contact||(earliest&&earliest->fraction<=contact->fraction))return;
                const auto normal=b*contact->normal;
                const auto corrected=q+b*contact->surface+normal*(radius+.025)-tail;
                const auto surfaceVelocity=((q-p)+(b*contact->surface-a*contact->surface))/dt;
                earliest=TrafficImpact{&ship,contact->fraction,inverse*(corrected-pose.position),
                                       inverse*normal,inverse*surfaceVelocity,b*contact->surface};
            };
            for(const auto& box:trafficHull(ship.kind))
                hitBox({box.centre*ship.radius,box.half*ship.radius});
            if(ship.radius>=minimumTrafficCollarRadius){
                const double collar=vesselCollarCentre(ship.radius),mount=vesselCollarMountLength(ship.radius);
                hitBox({{collar-.16,0,0},{.91,1.45,1.55}});
                hitBox({{collar-mount*.5,0,0},{mount*.5,1.43,1.45}});
            }
        }
    }
    return earliest;
}

std::string xalariaTrafficReply(const XalariaCatalog& catalog,const XalariaEconomy& economy,
                   const XalariaTrafficContact& contact,TrafficRequest request,double closing) {
    if(economy.combat()&&(isStrategyActor(contact.body.id)||economy.combat()->strategy().suppresses(contact.body.id)))return economy.combat()->strategy().radio(contact.body.id);
    if(isConvoyActor(contact.body.id)&&economy.combat())return economy.combat()->convoys().radio(contact.body.id,request,economy.combat()->convoys().seconds());
    const Random crew{contact.body.seed};
    const auto port=economy.placeName(contact.port);
    const bool boardable=xalariaVesselBerth(contact.body).has_value();
    if(const auto* combat=economy.combat()){
        if(contact.body.disabled)return "No response. This vessel is a wreck.";
        if(combat->shieldsLowered(contact.body.id))
            return request==TrafficRequest::Dock?"Our drives are stopped and shields lowered. Starboard collar unlocked. Approach slowly.":
                "We cannot fight. Our collar is unlocked; the transfer bay holds our remaining cargo.";
        if(!combat->mayDock(contact.body))
            return contact.body.kind==ActivityKind::Patrol?"Security intercept. Weapons active. Docking clearance refused.":
                "Stay away from our collar. Distress broadcast active. Docking clearance refused.";
    }
    if(contact.dockingHold>0.&&(request==TrafficRequest::Hail||request==TrafficRequest::Advisory||
       request==TrafficRequest::Rendezvous||request==TrafficRequest::FlightPlan||(boardable&&request==TrafficRequest::Dock))){
        const auto wait=std::to_string(int(std::ceil(contact.dockingHold)))+" s.";
        return boardable?"Stand clear. Checking our collar. Request clearance again in "+wait:
                         "Stand clear while we steady the ship. Check back in "+wait;
    }
    if(request==TrafficRequest::Dock){
        if(!boardable)
            return "This hull is too small for a transfer collar. We'll stay on the radio.";
        return "Cleared to our starboard collar. Follow the approach lights. J hands docking to the autopilot.";
    }
    if(request==TrafficRequest::PortMarket)
        return "Forwarding the market channel for "+port+".";
    if(request==TrafficRequest::FreightWork||request==TrafficRequest::FreightDispatch)
        return xalariaFreightBrief(economy,contact.port,xalariaTrafficFreight(economy,contact.port));
    if(request==TrafficRequest::Rendezvous) {
        if(contact.service==TrafficService::Orbital)
            return "Rendezvous approved. Match our velocity at the holding point below our lane.";
        if(contact.gateDestination)
            return "Unable to rendezvous during gate service. Keep the approach to the opening clear.";
        if(contact.phase==TrafficPhase::AtBerth)
            return "We're clamped in at "+port+". Contact station control to arrange your own berth.";
        return "Unable to rendezvous in the auxiliary lanes. Wait clear of the station.";
    }
    if(request==TrafficRequest::Advisory) {
        if(closing>90.)return "You're closing fast. Reduce speed and pass well clear of our hull.";
        if(contact.gateDestination)
            return "The pulsing buoys mark the edges. Keep to the opening when crossing to "+
                   catalog.star(*contact.gateDestination).name+".";
        if(contact.service==TrafficService::Port)
            return "Auxiliary craft use the outer berths. Follow the main approach lights for "+port+".";
        if(contact.body.kind==ActivityKind::Patrol)
            return "Keep clear of the freighter lanes. There is drifting wreckage above the routes.";
        return "Pass below our orbital lane. Broken equipment is drifting above the traffic.";
    }
    if(request==TrafficRequest::FlightPlan) {
        if(contact.gateDestination)
            return "We maintain the approach to "+catalog.star(*contact.gateDestination).name+
                   " from "+port+". Our service circuit stays outside the opening.";
        if(contact.phase==TrafficPhase::AtBerth)
            return "Turnaround at "+port+" now. We'll depart through the auxiliary lane when ready.";
        if(contact.phase==TrafficPhase::Inbound||contact.phase==TrafficPhase::Outbound)
            return std::string(contact.phase==TrafficPhase::Inbound?"Inbound to ":"Departing ")+port+
                   " on the auxiliary traffic circuit.";
        switch(contact.body.kind) {
        case ActivityKind::Freighter: {
            constexpr std::array cargo{XGood::Aluminium,XGood::Iron,XGood::Nickel,XGood::Chromium,
                                       XGood::Titanium,XGood::Nanomites};
            const auto good=cargo[crew.bits(6)%cargo.size()];
            return "We have sealed "+std::string(xGoodInfo(good).name)+" aboard on a freight circuit out of "+port+".";
        }
        case ActivityKind::Ferry: return "Passenger service out of "+port+". Next stop is our local berth.";
        case ActivityKind::Survey: {
            const auto& world=catalog.star(contact.port.system).worlds.at(contact.port.world);
            return "Surveying "+world.name+(world.ringOuter>world.ringInner?
                "'s ring plane":" outer orbit")+". We report our scans to "+port+".";
        }
        default: return "Patrolling the orbital lanes out of "+port+" and escorting local freight.";
        }
    }
    constexpr std::array greeting{"Reading you clearly.","Go ahead.","We're on this channel.","Signal received."};
    if(contact.phase==TrafficPhase::AtBerth)
        return std::string(greeting[crew.bits(4)%greeting.size()])+" We're on turnaround at "+port+".";
    if(contact.phase==TrafficPhase::Inbound)
        return std::string(greeting[crew.bits(4)%greeting.size()])+" We're on final approach to "+port+".";
    if(contact.phase==TrafficPhase::Outbound)
        return std::string(greeting[crew.bits(4)%greeting.size()])+" We're pulling away from "+port+".";
    return std::string(greeting[crew.bits(4)%greeting.size()])+" This is "+contact.body.name+
           ", working out of "+port+".";
}

std::optional<FlightWaypoint> xalariaTrafficRendezvous(const XalariaEconomy& economy,const XalariaTrafficContact& contact) {
    if(contact.service!=TrafficService::Orbital||contact.dockingHold>0.)return {};
    const auto& body=contact.body;
    const auto port=economy.portBody(contact.port);
    const auto offset=port.orientation*glm::dvec3(0,-900.,0);
    return FlightWaypoint{body.id.home,body.position+offset,body.velocity,0.,"HOLD BELOW "+body.name,false,{}};
}

void TrafficHoldProgress::update(double range,double relativeSpeed,double seconds) {
    if(matched_&&range<=65.&&relativeSpeed<=14.)
        return;
    matched_=false;
    if(range>40.||relativeSpeed>8.||!std::isfinite(range)||!std::isfinite(relativeSpeed)) {
        steady_=0.;
        return;
    }
    steady_+=std::max(0.,seconds);
    matched_=steady_>=.75;
}

std::optional<TrafficFreightLead> xalariaTrafficFreight(const XalariaEconomy& economy,const XPlace& port) {
    const auto& contracts=economy.contracts();
    const auto active=[&](const XContract& c){
        return c.state==XContract::State::Active&&economy.day()<=c.deadline;
    };
    // A crew at the receiving port can direct an incoming player to their delivery.
    const auto inbound=std::find_if(contracts.rbegin(),contracts.rend(),[&](const XContract& c){
        return active(c)&&c.destination==port;
    });
    if(inbound!=contracts.rend())
        return TrafficFreightLead{*inbound,true,economy.canTrade(inbound->destination)};
    const auto outbound=std::find_if(contracts.rbegin(),contracts.rend(),[&](const XContract& c){
        return active(c)&&c.origin==port;
    });
    if(outbound!=contracts.rend())
        return TrafficFreightLead{*outbound,true,economy.canTrade(outbound->destination)};
    const auto offer=economy.offer(port);
    if(!offer.units||economy.charterClaimed(offer.id))
        return {};
    return TrafficFreightLead{offer,false,economy.canTrade(offer.origin)};
}

std::string xalariaFreightBrief(const XalariaEconomy& economy,const XPlace& port,
                               const std::optional<TrafficFreightLead>& lead) {
    if(!lead)
        return "Dispatch at "+economy.placeName(port)+" has no unclaimed charter today.";
    const auto& c=lead->charter;
    const auto delivery=economy.placeName(c.destination);
    const auto terms=xMoney(c.reward)+" on delivery by day "+std::to_string(c.deadline)+".";
    if(lead->aboard)
        return "Your parcel for "+delivery+" is aboard. "+terms+
               (lead->atPort?" Open the delivery page to turn it in.":"");
    const auto shortage=economy.cargoUnits()+c.units-economy.holdCapacity();
    std::string instructions;
    if(shortage>0)
        instructions=" Free "+std::to_string(shortage)+" hold units before collection.";
    else if(lead->atPort)
        instructions=" Collect it from the station's loading bay.";
    return std::to_string(c.units)+" sealed units from "+economy.placeName(c.origin)+" to "+delivery+". "+
           terms+instructions;
}

std::optional<DockingBerth> xalariaVesselBerth(const ActivityBody& body) {
    if(!isXalariaTraffic(body.id)||!std::isfinite(body.radius)||body.radius<minimumTrafficCollarRadius||
       body.disabled||body.hull<=0.)
        return {};
    const glm::dvec3 offset{vesselCollarCentre(body.radius)+6.8,0.,-stationDeck::centre};
    return DockingBerth{body.id,body.position+body.orientation*offset,body.velocity,body.orientation,body.radius};
}

std::optional<FlightWaypoint> xalariaVesselApproach(const XalariaEconomy& economy,const ActivityId& id,
        const std::optional<XalariaWormholeMouth>& gate,double seconds) {
    const auto ship=xalariaTrafficContact(economy,id,gate,seconds);
    if(!ship||ship->dockingHold>0.||(economy.combat()&&!economy.combat()->mayDock(ship->body)))return {};
    auto berth=xalariaVesselBerth(ship->body);
    if(!berth)return {};
    // The collar is off-centre. Its closing speed includes the rotation
    // of the hull around the path sampled by the renderer.
    const double before=std::max(0.,seconds-.04),after=seconds+.04;
    const auto previous=xalariaTrafficContact(economy,id,gate,before);
    const auto next=xalariaTrafficContact(economy,id,gate,after);
    if(previous&&next){
        const auto a=xalariaVesselBerth(previous->body), b=xalariaVesselBerth(next->body);
        if(a&&b)berth->velocity=(b->metres-a->metres)/(after-before);
    }
    return FlightWaypoint{id.home,berth->metres,berth->velocity,0.,ship->body.name,false,berth};
}

StationKey xalariaVesselInterior(const ActivityBody& body) {
    const auto role=body.kind==ActivityKind::Freighter?StationRole::Freight:
                    body.kind==ActivityKind::Ferry?StationRole::Habitat:
                    body.kind==ActivityKind::Survey?StationRole::Research:StationRole::Industrial;
    return {mix64(body.seed^0x434142494eULL),role,true};
}

XContract xalariaVesselParcel(const XalariaEconomy& economy,const XalariaTrafficContact& contact) {
    const auto day=economy.day();
    const auto id=mix64(contact.body.seed^mix64(uint64_t(day))^0x53484950434f5552ULL);
    const Random load{id};
    return {id,contact.port,contact.port,day+3,
            25000+2500*int64_t(load.bits(0)%31),1+int64_t(load.bits(1)%3),XContract::State::Active};
}
} // namespace infinity
