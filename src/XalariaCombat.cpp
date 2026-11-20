#include "infinity/XalariaCombat.hpp"
#include "infinity/XalariaStories.hpp"
#include "infinity/Cockpit.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace infinity {
namespace {
constexpr int64_t walletLimit=1000000000000000LL;
bool fence(XFaction f){return f==XFaction::Independent||f==XFaction::Rebel;}
bool finite(glm::dvec3 p){return std::isfinite(glm::length(p));}
glm::dvec3 limited(glm::dvec3 p,double maximum){return p*std::min(1.,maximum/std::max(1e-12,glm::length(p)));}
uint32_t systemOf(const ActivityId& id){auto a=id.home;while(a.kind()>Kind::System)a=a.parent();return uint32_t(a.id());}
}
ActivityId trafficStation(const ActivityId& id){return {id.home,(id.slot&0x0fffffffu)>>4};}
std::string_view combatFitName(CombatFit fit){constexpr std::array names{"Civilian","Corsair","Gunship"};return names.at(unsigned(fit));}
std::string_view combatModeName(CombatMode mode){constexpr std::array names{"On circuit","Engaging","Evading / distress sent","Surrendered","Engines disabled","Wreck","Rejoining route","Engaging hostile traffic"};return names.at(unsigned(mode));}
XalariaCombat::XalariaCombat(const Universe& u,XalariaEconomy& e,Pilot& p):universe_(u),economy_(e),pilot_(p),convoys_(u,e,*this),strategy_(u,e,*this){effects_.setLoadout({0,0,0});}
bool XalariaCombat::outlaw(const ActivityId& id)const{return (isCombatActor(id)&&(id.slot&15u)==0)||convoys_.raider(id);}
void XalariaCombat::configurePilot(){pilot_.setCombatFit(fit_==CombatFit::Gunship?80.:fit_==CombatFit::Corsair?48.:28.,fit_==CombatFit::Gunship?.65:fit_==CombatFit::Corsair?.85:1.);}
int64_t XalariaCombat::wanted(uint32_t system)const{const auto it=warrants_.find(system);return it==warrants_.end()?0:it->second;}
bool XalariaCombat::permits(const XPlace& port)const{return strategy_.controls(port)||fence(economy_.faction(port))||(wanted(port.system)<50000&&convoys_.standing(economy_.faction(port))>=-25&&strategy_.trust(economy_.faction(port))>-40);}
bool XalariaCombat::maySell(const XPlace& p,XGood good,int64_t count)const{
    return fence(economy_.faction(p))||count<=economy_.cargo_[size_t(good)]-stolen_[size_t(good)];
}
void XalariaCombat::cargoSold(XGood good,int64_t count,int64_t before){
    const auto legal=std::max<int64_t>(0,before-stolen_[size_t(good)]);
    stolen_[size_t(good)]=std::max<int64_t>(0,stolen_[size_t(good)]-std::max<int64_t>(0,count-legal));
}
bool XalariaCombat::refit(CombatFit fit){
    if(unsigned(fit)>2||!economy_.dockedPlace()||!economy_.canTrade(*economy_.dockedPlace())){message="Dock at a station that will service you to refit.";return false;}
    if(fit==fit_){message="That refit is already installed. Service to repair and rearm.";return false;}
    const int64_t cost=fit==CombatFit::Civilian?100000:fit==CombatFit::Corsair?350000:900000;
    if(economy_.credits_<cost){message="Insufficient credits for this refit.";return false;}
    economy_.credits_-=cost;economy_.market(*economy_.dockedPlace()).cash+=cost;
    fit_=fit;torpedoes_=fit==CombatFit::Gunship?10:fit==CombatFit::Corsair?4:0;
    heat_=cooldown_=0;overheated_=false;armed=false;configurePilot();
    message=std::string(combatFitName(fit_))+" installed. Undock, press M to arm, then hold Space to fire.";return true;
}
bool XalariaCombat::service(){
    if(!economy_.dockedPlace()||!economy_.canTrade(*economy_.dockedPlace())){message="Dock at a friendly station or independent haven for service.";return false;}
    const unsigned maximum=fit_==CombatFit::Gunship?10:fit_==CombatFit::Corsair?4:0;
    const auto repairs=uint64_t(std::ceil(std::max(0.,100.-pilot_.ship().hull)));
    const int64_t cost=int64_t(maximum-torpedoes_)*25000+int64_t(repairs)*1600;
    if(economy_.credits_<cost){message="Insufficient credits for repair and torpedoes.";return false;}
    economy_.credits_-=cost;economy_.market(*economy_.dockedPlace()).cash+=cost;
    repairs_=std::max(repairs_,pilot_.ship().appliedRepairPoints)+repairs;pilot_.applyPortRepairs(repairs_);
    torpedoes_=maximum;message="Hull serviced and torpedoes replenished.";return true;
}
bool XalariaCombat::claimBounty(){
    if(const auto fine=localWarrant()){
        if(!economy_.nearOrbit()){message="Approach a world in this jurisdiction to settle its warrant.";return false;}
        if(economy_.credits_<fine){message="Insufficient credits to settle the local warrant.";return false;}
        economy_.credits_-=fine;
        if(economy_.dockedPlace())economy_.market(*economy_.dockedPlace()).cash+=fine;
        warrants_.erase(economy_.currentSystem());
        message="Local warrant settled through security control. Other jurisdictions retain their own records.";return true;
    }
    if(!economy_.dockedPlace()){message="Dock at a lawful station to redeem patrol vouchers.";return false;}
    const auto p=*economy_.dockedPlace();
    if(fence(economy_.faction(p))||!bountyDue_){message="Patrol vouchers are paid at lawful stations.";return false;}
    const auto payout=std::min(bountyDue_,walletLimit-economy_.credits_);
    economy_.credits_+=payout;bountyDue_-=payout;
    message="Patrol vouchers redeemed / "+xMoney(payout);return payout>0;
}
ActivityBody XalariaCombat::canonical(const ActivityId& id,double seconds)const{
    if(isStrategyActor(id)||strategy_.suppresses(id)){if(auto c=strategy_.contact(id,seconds))return c->body;throw std::invalid_argument("Unknown strategic hull");}
    const auto port=economy_.orbitalPlace(trafficStation(id));
    if(!port)throw std::invalid_argument("No combat harbour");
    const auto berth=*dockingBerth(economy_.portBody(*port));
    const auto design=economy_.stationDesignKey(berth.id);
    const unsigned lane=id.slot&15;
    const auto fleet=sampleOrbitalTraffic(design,economy_.trafficClock(berth.id,seconds).first);
    if(!isCombatActor(id)){
        if(lane>=8){
            if(!lastGate_||lastGate_->reference!=id.home)throw std::invalid_argument("No wormhole traffic frame");
            const auto craft=sampleWormholeTraffic(lastGate_->radius,economy_.trafficClock(berth.id,seconds).first)[lane-8];
            return xalariaTrafficBody(berth.id,TrafficService::Wormhole,lane-8,craft,lastGate_->metres,lastGate_->orientation);
        }
        const auto craft=lane<3?sampleStationTraffic(design,economy_.trafficClock(berth.id,seconds).first)[lane]:fleet[lane-3];
        return xalariaTrafficBody(berth.id,lane<3?TrafficService::Port:TrafficService::Orbital,
                                  lane<3?lane:lane-3,craft,berth.metres,berth.orientation);
    }
    const auto& route=fleet[std::min(4u,lane+1)];
    ActivityBody body;
    body.id=id;body.kind=lane==0?ActivityKind::Raider:ActivityKind::Patrol;
    body.seed=mix64(seedOf(id.home)^id.slot^0x57415253484950ULL);
    body.position=berth.metres+berth.orientation*(route.position+glm::dvec3(lane?6800.:-7200.,1800.,6500.+lane*1500.));
    body.orientation=berth.orientation*route.orientation;body.velocity=berth.orientation*route.velocity*economy_.trafficClock(berth.id,seconds).second;
    body.radius=lane==2?39.:lane?31.:25.;body.hostile=lane==0;
    body.name=std::string(lane?"Marshal-":"Rook-")+std::to_string(100+body.seed%900);
    body.status=lane?"SYSTEM SECURITY / PATROL":"RAIDER / UNLICENSED ARMS";
    body.tint=lane?glm::vec3(.42,.72,.94):glm::vec3(.9,.27,.12);
    return body;
}
std::vector<ActivityBody> XalariaCombat::actors(const XPlace& p,double seconds)const{
    std::vector<ActivityBody> bodies;
    const auto station=economy_.orbitalStationId(p);if(!station)return bodies;
    const auto seed=mix64(seedOf(station->home)^station->slot);
    for(unsigned lane=0;lane<3;++lane){
        if(lane==0&&seed%4!=0)continue;
        if(lane==2&&seed%3!=0)continue;
        const ActivityId actor{station->home,combatActivityTag|(station->slot<<4)|lane};
        if(strategy_.suppresses(actor))continue;auto body=canonical(actor,seconds);
        apply(body,seconds);bodies.push_back(std::move(body));
    }
    return bodies;
}
std::optional<XalariaTrafficContact> XalariaCombat::contact(const ActivityId& id,double seconds)const{
    if(isStrategyActor(id)||strategy_.suppresses(id))return strategy_.contact(id,seconds);
    if(isConvoyActor(id))return convoys_.contact(id,seconds);
    if(!isCombatActor(id))return {};
    const auto p=economy_.orbitalPlace(trafficStation(id));if(!p)return {};
    const auto bodies=actors(*p,seconds);
    const auto it=std::find_if(bodies.begin(),bodies.end(),[&](const auto& b){return b.id==id;});
    if(it==bodies.end())return {};
    return XalariaTrafficContact{*it,*p,TrafficService::Orbital,TrafficPhase::OnCircuit,{},0.,economy_.trafficShield(*it,seconds,shieldsLowered(id))};
}
CombatShip* XalariaCombat::state(const ActivityBody& body,double seconds){
    auto it=ships_.find(body.id);if(it!=ships_.end())return &it->second;
    if(ships_.size()>=2048)return nullptr;
    CombatShip state;state.hull=std::clamp(body.hull,0.,100.);state.engines=std::clamp(body.systems[size_t(ShipSystem::Engines)],0.,100.);
    if(state.hull<=0)state.mode=CombatMode::Wreck;else if(state.engines<=0)state.mode=CombatMode::Disabled;
    state.previous=state.position=body.position;state.velocity=body.velocity;
    state.previousOrientation=state.orientation=body.orientation;state.from=state.until=seconds;
    return &ships_.emplace(body.id,state).first->second;
}
void XalariaCombat::apply(ActivityBody& body,double seconds)const{
    const auto it=ships_.find(body.id);if(it==ships_.end())return;
    const auto& s=it->second;
    if(s.moving){
        const double t=s.until>s.from?std::clamp((seconds-s.from)/(s.until-s.from),0.,1.):1.;
        body.position=glm::mix(s.previous,s.position,t);
        if(seconds>s.until)body.position+=s.velocity*std::min(.2,seconds-s.until);
        body.velocity=s.velocity;body.orientation=glm::slerp(s.previousOrientation,s.orientation,t);
    }
    body.hull=s.hull;body.systems[size_t(ShipSystem::Hull)]=s.hull;body.systems[size_t(ShipSystem::Engines)]=s.engines;
    body.disabled=s.hull<=0.;
    if(isConvoyActor(body.id))body.hostile=convoys_.hostile(body.id);
    else body.hostile|=(s.mode==CombatMode::Attack&&(!isCombatActor(body.id)||(body.id.slot&15u)==0||wanted(systemOf(body.id))))||convoys_.hostile(body.id);
    if(strategy_.ownsSource(body.id)||(body.id.slot&0xf0000000u)==fleetActivityTag)body.hostile=false;
    if(s.mode!=CombatMode::Circuit)body.status=std::string(combatModeName(s.mode))+" / HULL "+std::to_string(int(std::ceil(s.hull)))+"%";
    if(body.disabled)body.tint*=.20f;
}
bool XalariaCombat::shieldsLowered(const ActivityId& id)const{
    const auto found=ships_.find(id);return found!=ships_.end()&&(found->second.mode==CombatMode::Surrendered||found->second.engines<=0||found->second.hull<=0);
}
bool XalariaCombat::mayDock(const ActivityBody& body)const{
    if(body.disabled)return false;
    if(isStrategyActor(body.id)||strategy_.ownsSource(body.id))return !body.hostile||shieldsLowered(body.id);
    if(isConvoyActor(body.id)){
        const auto key=convoys_.identity(body.id,lastSeconds_);
        return key&&(shieldsLowered(body.id)||convoys_.stage(*key,lastSeconds_).phase==ConvoyPhase::Delivered);
    }
    const auto it=ships_.find(body.id);
    if(it==ships_.end())return !body.hostile;
    return it->second.mode==CombatMode::Surrendered||it->second.mode==CombatMode::Disabled||
           (!body.hostile&&it->second.mode!=CombatMode::Attack&&it->second.mode!=CombatMode::Flee&&it->second.mode!=CombatMode::Skirmish);
}
void XalariaCombat::crime(const ActivityId& id,int64_t amount){
    auto& record=warrants_[systemOf(id)];record=std::min<int64_t>(1000000000,record+amount);
}
bool XalariaCombat::demand(const ActivityId& id,const CombatPlayer& player){
    const auto contact=xalariaTrafficContact(economy_,id,player.gate,player.seconds);
    if(!armed||player.sheltered||player.transit||player.weaponsInhibited||!contact||contact->body.disabled){message="Arm weapons and hail a live cargo ship in flight.";return false;}
    const auto& body=contact->body;
    const auto eye=universe_.pointRelative(player.frame,player.position/std::pow(10.,universe_.describe(player.frame).frame.logRadius),id.home,0.);
    if(glm::distance(eye,body.position)>2500.){message="Close within 2.5 km to make a cargo demand.";return false;}
    if(body.kind!=ActivityKind::Freighter&&!outlaw(id)){message="That vessel carries no transferable cargo.";return false;}
    auto* s=state(body,player.seconds);if(!s)return false;
    if(!s->threatened&&!outlaw(id))crime(id,25000);s->threatened=true;s->aggroUntil=player.seconds+600.;
    if(!isConvoyActor(id)&&s->cargoLeft<0)s->cargoLeft=2+int64_t(body.seed%6);
    if(s->hull<65.||s->engines<=0||economy_.trafficShield(body,player.seconds).fraction<.3||
       (fit_==CombatFit::Gunship&&body.seed%3==0)){
        s->mode=s->engines<=0?CombatMode::Disabled:CombatMode::Surrendered;s->moving=true;
        message=body.name+" surrenders. Request docking clearance, board, then seize cargo in F8.";return true;
    }
    s->mode=outlaw(id)?CombatMode::Attack:CombatMode::Flee;s->moving=true;
    message=body.name+" refuses and sends a distress call. Its captain has reported your threat.";return false;
}
bool XalariaCombat::seize(const ActivityId& id,const StoryContext& context){
    if(strategy_.ownsSource(id)||(id.slot&0xf0000000u)==fleetActivityTag){message="Your captain manages this fleet hold through F10.";return false;}
    if(isConvoyActor(id)){const bool result=convoys_.seize(id,context);message=convoys_.message;return result;}
    const auto it=ships_.find(id);
    if(context.boardedVessel!=id||context.system!=systemOf(id)||it==ships_.end()||
       (it->second.mode!=CombatMode::Surrendered&&it->second.mode!=CombatMode::Disabled)||it->second.hull<=0){
        message="Board a surrendered or disabled ship to take its cargo.";return false;
    }
    auto& s=it->second;
    const auto body=isCombatActor(id)?canonical(id,lastSeconds_):xalariaTrafficContact(economy_,id,lastGate_,lastSeconds_)->body;
    if(s.cargoLeft<0)s.cargoLeft=2+int64_t(body.seed%6);
    const int64_t units=std::min(s.cargoLeft,std::max<int64_t>(0,int64_t(economy_.capacity_)-economy_.cargoUnits()));
    if(!units){message=s.cargoLeft?"Your hold is full.":"That vessel has no cargo left to seize.";return false;}
    const auto good=XGood(unsigned(body.seed%7));
    economy_.cargo_[size_t(good)]+=units;s.cargoLeft-=units;
    if(!outlaw(id)){stolen_[size_t(good)]+=units;crime(id,80000);}
    message="Seized "+std::to_string(units)+" "+std::string(xGoodInfo(good).name)+
        (outlaw(id)?" from a raider.":". Sell stolen cargo at Independent or Rebel markets.");return true;
}
std::vector<ActivityBody> XalariaCombat::localBodies(const CombatPlayer& player)const{
    std::vector<ActivityBody> bodies;
    auto system=player.frame;while(system.kind()>Kind::System)system=system.parent();
    if(system.kind()!=Kind::System)return bodies;
    const auto id=uint32_t(system.id());
    for(const auto& [world,record]:universe_.xalaria()->star(id).worlds){
        if(!record.stations)continue;
        const auto frame=universe_.xalaria()->address(id).child(Kind::Planet,world);
        const auto eye=universe_.pointRelative(player.frame,player.position/std::pow(10.,universe_.describe(player.frame).frame.logRadius),frame,0.);
        if(std::abs(glm::length(eye)-record.radius*2.2)>120000.)continue;
        for(const auto& port:economy_.venues(id,world)){
            if(port.city)continue;
            const auto station=*economy_.orbitalStationId(port);
            bodies.push_back(economy_.portBody(port));
            for(unsigned lane=0;lane<(player.gate&&player.gate->reference==frame&&port.station==0?10u:8u);++lane)
                if(auto c=xalariaTrafficContact(economy_,{frame,trafficActivityTag|(station.slot<<4)|lane},player.gate,player.seconds))bodies.push_back(c->body);
            for(auto& body:actors(port,player.seconds))bodies.push_back(std::move(body));
        }
    }
    for(auto& body:convoys_.localBodies(id,player.seconds))bodies.push_back(std::move(body));
    std::erase_if(bodies,[&](const auto& b){return strategy_.suppresses(b.id);});
    for(auto& b:strategy_.bodies(id,player.seconds))bodies.push_back(std::move(b));
    return bodies;
}
void XalariaCombat::hit(ActivityBody body,double load,bool ion,double seconds,glm::dvec3 impact,bool playerShot){
    if(!isXalariaTraffic(body.id)||body.disabled)return;
    auto* s=state(body,seconds);if(!s)return;
    if(s->hull<=0.)return;
    if(playerShot&&isConvoyActor(body.id))s->threatened=true;
    if(playerShot&&!outlaw(body.id)&&!convoys_.lawfulTarget(body.id)&&!strategy_.lawfulTarget(body.id))crime(body.id,4000);
    const double capacity=trafficShieldCapacity(body);
    double remaining=load;
    if(capacity>0.&&!shieldsLowered(body.id)){
        if(!economy_.trafficShields_.contains(body.id)&&economy_.trafficShields_.size()>=64){
            const auto oldest=std::min_element(economy_.trafficShields_.begin(),economy_.trafficShields_.end(),[](const auto& a,const auto& b){return a.second.hitAt<b.second.hitAt;});
            economy_.trafficShields_.erase(oldest);
        }
        remaining-=economy_.trafficShields_[body.id].absorb(load,capacity,seconds,impact);
    }
    if(ion)s->engines=std::max(0.,s->engines-remaining*2.4);
    else s->hull=std::max(0.,s->hull-remaining);
    if(s->hull<=0.){
        if(explosions_.size()>=maxShipExplosions)explosions_.erase(explosions_.begin());
        explosions_.push_back({body.id,body.position,body.velocity,body.orientation,body.radius,0.,body.seed,shipPropellant(body)});
    }
    s->aggroUntil=seconds+600.;s->moving=!(isStrategyActor(body.id)||strategy_.ownsSource(body.id));
    if(isStrategyActor(body.id)&&!strategy_.ownsSource(body.id)&&s->engines<=0&&s->hull>0){
        s->moving=true;s->previous=s->position=body.position;s->previousOrientation=s->orientation=body.orientation;
        s->velocity={};s->from=s->until=seconds;
    }
    s->mode=s->hull<=0?CombatMode::Wreck:s->engines<=0?CombatMode::Disabled:
        body.kind==ActivityKind::Freighter||body.kind==ActivityKind::Ferry?CombatMode::Flee:CombatMode::Attack;
    if(!playerShot&&s->hull>0&&s->engines>0&&body.kind!=ActivityKind::Freighter&&body.kind!=ActivityKind::Ferry)s->mode=CombatMode::Skirmish;
    if(playerShot&&(s->hull<=0||s->engines<=0)&&outlaw(body.id)&&!s->bountyRecorded){
        s->bountyRecorded=true;++victories_;bountyDue_+=120000+int64_t(body.seed%80000);
        message="Raider neutralized. Patrol voucher recorded; redeem it at a lawful station.";
    }else if(playerShot&&s->hull<=0&&!outlaw(body.id)&&!convoys_.lawfulTarget(body.id)&&!strategy_.lawfulTarget(body.id))crime(body.id,150000);
    if(isConvoyActor(body.id))convoys_.struck(body,seconds,playerShot);
    strategy_.struck(body,seconds,playerShot,s->hull<=0.);
}
bool XalariaCombat::fire(const CombatPlayer& player,const Geometry* geometry){
    if(!armed||player.sheltered||player.transit||player.weaponsInhibited||pilot_.ship().hull<=0||cooldown_>1e-8||overheated_)return false;
    if(weapon!=CombatWeapon::Pulse&&fit_==CombatFit::Civilian){message="This fit has only a defensive pulse gun.";return false;}
    if(weapon==CombatWeapon::Torpedo&&!torpedoes_){message="No torpedoes remain. Service at a station.";return false;}
    const double heat=weapon==CombatWeapon::Pulse?(fit_==CombatFit::Gunship?24.:17.):weapon==CombatWeapon::Ion?32.:42.;
    if(heat_+heat>100.){overheated_=true;message="Weapons overheated / release fire to cool.";return false;}
    const auto bodies=localBodies(player);
    const double range=weapon==CombatWeapon::Pulse?(fit_==CombatFit::Gunship?10000.:fit_==CombatFit::Corsair?6000.:3000.):weapon==CombatWeapon::Ion?1600.:7000.;
    WeaponRay ray;ray.reference=player.frame;ray.origin=player.position+player.forward*10.;
    ray.direction=player.forward;ray.right=player.orientation*glm::dvec3(1,0,0);ray.seconds=player.seconds;
    const auto trace=traceWeapon(universe_,ray,range,bodies,geometry);
    if(weapon==CombatWeapon::Torpedo){
        if(!trace.body||!isXalariaTraffic(trace.body->id)||trace.body->disabled){message="Aim at a live ship to launch a torpedo.";return false;}
        if(missiles_.size()>=16)return false;
        missiles_.push_back({player.frame,ray.origin,ray.direction,trace.body->id,0.});--torpedoes_;
        if(!outlaw(trace.body->id)&&!convoys_.lawfulTarget(trace.body->id))crime(trace.body->id,4000);
    }else{
        const unsigned battery=weapon==CombatWeapon::Ion?1:fit_==CombatFit::Gunship?3:fit_==CombatFit::Corsair?2:1;
        for(unsigned gun=0;gun<battery;++gun){
            auto muzzle=ray;muzzle.origin+=ray.right*(double(gun)-double(battery-1)*.5)*2.5;
            const auto strike=traceWeapon(universe_,muzzle,range,bodies,geometry);
            if(strike.body)hit(*strike.body,weapon==CombatWeapon::Ion?32.:fit_==CombatFit::Gunship?24.:fit_==CombatFit::Corsair?16.:10.,weapon==CombatWeapon::Ion,player.seconds,-ray.direction);
            effects_.presentShot({player.frame,muzzle.origin,muzzle.origin+ray.direction*strike.distance,0,1,bool(strike.body),false,
                weapon==CombatWeapon::Ion?ionDisruptor:fit_==CombatFit::Gunship?3u:2u,false,gun});
        }
    }
    heat_+=heat;cooldown_=weapon==CombatWeapon::Pulse?.28:weapon==CombatWeapon::Ion?.9:2.;++shots_;
    return true;
}
void XalariaCombat::advance(const CombatPlayer& player,double dt,const Geometry* geometry){
    if(!std::isfinite(dt)||dt<=0)return;dt=std::min(dt,.1);lastSeconds_=player.seconds+dt;lastGate_=player.gate;
    effects_.advance(dt);heat_=std::max(0.,heat_-dt*26.);cooldown_=std::max(0.,cooldown_-dt);if(heat_<35.)overheated_=false;
    for(auto& explosion:explosions_)explosion.age+=dt;
    std::erase_if(explosions_,[](const auto& explosion){return explosion.age>=shipExplosionLifetime;});
    strategy_.advance(player,dt);
    convoys_.advance(player,dt);
    const auto bodies=localBodies(player);
    for(const auto& raw:bodies){
        if(!isXalariaTraffic(raw.id)||raw.disabled)continue;
        if(isStrategyActor(raw.id)||strategy_.ownsSource(raw.id)){strategy_.simulate(raw,bodies,player,dt,geometry);continue;}
        if(isConvoyActor(raw.id)){convoys_.simulate(raw,bodies,player,dt,geometry);continue;}
        auto eye=universe_.pointRelative(player.frame,player.position/std::pow(10.,universe_.describe(player.frame).frame.logRadius),raw.id.home,0.);
        const auto npcTarget=convoys_.localTarget(raw,bodies,player);
        if(npcTarget){const auto victim=std::find_if(bodies.begin(),bodies.end(),[&](const auto& b){return b.id==npcTarget;});
            if(victim!=bodies.end())eye=universe_.pointRelative(victim->id.home,victim->position/std::pow(10.,universe_.describe(victim->id.home).frame.logRadius),raw.id.home,0.);}

        const double distance=glm::distance(eye,raw.position);
        auto it=ships_.find(raw.id);
        const auto port=economy_.orbitalPlace(trafficStation(raw.id));
        const bool police=isCombatActor(raw.id)&&(raw.id.slot&15u)!=0&&port&&!fence(economy_.faction(*port));
        const bool provoked=outlaw(raw.id)&&(armed||economy_.cargoUnits()>0);
        if(it==ships_.end()&&distance<9000.&&(npcTarget||(!player.sheltered&&!player.transit&&(provoked||(police&&wanted(systemOf(raw.id)))))))
            if(auto* s=state(raw,player.seconds)){s->moving=true;s->mode=CombatMode::Attack;s->aggroUntil=player.seconds+120.;it=ships_.find(raw.id);}
        if(it==ships_.end())continue;
        auto& s=it->second;s.cooldown=std::max(0.,s.cooldown-dt);
        if(police&&!npcTarget&&!wanted(systemOf(raw.id))&&s.mode==CombatMode::Attack)s.mode=CombatMode::Return;
        if(police&&wanted(systemOf(raw.id))&&distance<14000.&&s.engines>0&&s.mode!=CombatMode::Surrendered){s.mode=CombatMode::Attack;s.aggroUntil=player.seconds+120.;}
        if((s.mode==CombatMode::Disabled||s.mode==CombatMode::Surrendered)&&player.seconds>s.aggroUntil&&player.clamped!=raw.id){s.engines=100.;s.mode=CombatMode::Return;}
        if(!npcTarget&&s.mode==CombatMode::Skirmish)s.mode=outlaw(raw.id)&&provoked&&!player.sheltered&&!player.transit?CombatMode::Attack:CombatMode::Return;
        if(npcTarget&&s.engines>0){s.mode=CombatMode::Skirmish;s.aggroUntil=player.seconds+120.;s.moving=true;}
        if(!s.moving)continue;
        s.previous=raw.position;s.previousOrientation=raw.orientation;s.from=player.seconds;s.until=player.seconds+dt;
        glm::dvec3 goal{};
        const auto direction=(eye-raw.position)/std::max(1.,distance);
        if(player.seconds>s.aggroUntil&&(s.mode==CombatMode::Attack||s.mode==CombatMode::Flee))s.mode=CombatMode::Return;
        if(s.mode==CombatMode::Attack||npcTarget){
            auto tangent=glm::cross(direction,glm::dvec3(.1,1.,.1));
            if(glm::length(tangent)<1e-6)tangent=glm::dvec3(1,0,0);
            goal=direction*std::clamp((distance-1250.)*.12,-80.,170.)+glm::normalize(tangent)*42.;
        }
        else if(s.mode==CombatMode::Flee)goal=-direction*150.;
        else if(s.mode==CombatMode::Return){
            const auto canonicalBody=canonical(raw.id,player.seconds);
            goal=limited(canonicalBody.position-raw.position,130.)+canonicalBody.velocity;
            if(glm::distance(canonicalBody.position,raw.position)<2.&&glm::length(s.velocity-canonicalBody.velocity)<2.)s.moving=false;
        }
        s.velocity+=limited(goal-s.velocity,dt*36.);s.position=s.previous+s.velocity*dt;
        if(glm::length(s.velocity)>2.)s.orientation=glm::quatLookAtRH(glm::normalize(s.velocity),glm::dvec3(0,1,0));
        if((s.mode==CombatMode::Attack||npcTarget)&&(npcTarget||(!player.sheltered&&!player.transit))&&distance>120.&&distance<4500.&&s.cooldown<=0.){
            const auto muzzle=raw.position+direction*raw.radius*1.8;
            ActivityBody playerBody;playerBody.id={player.frame,UINT32_MAX};playerBody.position=player.position;playerBody.radius=6.;
            auto cover=bodies;if(!player.sheltered&&!player.transit)cover.push_back(playerBody);
            WeaponRay shot;shot.reference=raw.id.home;shot.origin=muzzle;shot.direction=direction;shot.seconds=player.seconds;shot.emitter=raw.id;
            const auto strike=traceWeapon(universe_,shot,distance,cover,geometry);
            const bool hitPlayer=strike.body&&strike.body->id==playerBody.id;
            if(npcTarget&&strike.body&&strike.body->id!=*npcTarget&&(hitPlayer||isXalariaTraffic(strike.body->id))){s.cooldown=.2;continue;}
            if(strike.body&&!hitPlayer)hit(*strike.body,outlaw(raw.id)?9.:12.,false,player.seconds,-direction,false);
            if(hitPlayer){incomingFire=raw.name;incomingAt=player.seconds;}
            if(hitPlayer)pilot_.applyWeaponDamage(outlaw(raw.id)?9.:12.,
                glm::inverse(player.orientation)*(universe_.relative(raw.id.home,player.frame,0.).orientation*(-direction)));
            effects_.presentShot({shot.reference,muzzle,muzzle+direction*strike.distance,0,1,hitPlayer,false,2,true});
            s.cooldown=outlaw(raw.id)?.9:1.05;
        }
    }
    for(auto it=missiles_.begin();it!=missiles_.end();){
        it->age+=dt;
        const auto victim=std::find_if(bodies.begin(),bodies.end(),[&](const auto& b){return b.id==it->target;});
        if(it->age>12.||victim==bodies.end()||victim->disabled||player.transit){it=missiles_.erase(it);continue;}
        const auto point=universe_.pointRelative(victim->id.home,victim->position/std::pow(10.,universe_.describe(victim->id.home).frame.logRadius),it->frame,0.);
        const auto aim=glm::normalize(point-it->position);
        it->direction=glm::normalize(glm::mix(it->direction,aim,std::min(1.,dt*2.8)));
        WeaponRay ray;ray.reference=it->frame;ray.origin=it->position-it->direction*6.;ray.direction=it->direction;
        const auto strike=traceWeapon(universe_,ray,650.*dt+6.,bodies,geometry);
        if(strike.blocked){
            if(strike.body)hit(*strike.body,72.,false,player.seconds,-it->direction);
            effects_.presentShot({it->frame,it->position,ray.origin+it->direction*strike.distance,0,1,true,false,missileLauncher,false});
            it=missiles_.erase(it);continue;
        }
        const auto previous=it->position;it->position+=it->direction*650.*dt;
        effects_.presentShot({it->frame,previous-it->direction*24.,it->position,0,1,false,false,missileLauncher,false});++it;
    }
}
std::vector<TrafficMotion> XalariaCombat::collisionMotions(const Address& frame,glm::dvec3 eye,uint32_t system,double seconds,double dt)const{
    std::vector<TrafficMotion> result;
    const auto& star=universe_.xalaria()->star(system);
    for(const auto& [world,record]:star.worlds){
        if(!record.stations)continue;
        const auto home=universe_.xalaria()->address(system).child(Kind::Planet,world);
        const auto local=universe_.pointRelative(frame,eye/std::pow(10.,universe_.describe(frame).frame.logRadius),home,0.);
        if(std::abs(glm::length(local)-record.radius*2.2)>120000.)continue;
        for(const auto& p:economy_.venues(system,world))if(!p.city)
            for(auto body:actors(p,seconds))if(glm::distance(body.position,local)<50000.){
                const auto next=contact(body.id,seconds+dt);
                if(next)result.push_back({body,next->body.position,next->body.orientation});
            }
    }
    // A frightened or intercepted civilian may be far from its seeded circuit.
    // Keep that same moving hull available to collision and docking queries.
    for(const auto& [id,state]:ships_){
        if(isConvoyActor(id)||isCombatActor(id)||isStrategyActor(id)||strategy_.suppresses(id)||!state.moving||systemOf(id)!=system)continue;
        const auto c=xalariaTrafficContact(economy_,id,lastGate_,seconds);
        const auto next=xalariaTrafficContact(economy_,id,lastGate_,seconds+dt);
        if(!c||!next)continue;
        const auto local=universe_.pointRelative(frame,eye/std::pow(10.,universe_.describe(frame).frame.logRadius),id.home,0.);
        if(glm::distance(c->body.position,local)<50000.)result.push_back({c->body,next->body.position,next->body.orientation});
    }
    for(auto body:strategy_.bodies(system,seconds)){
        const auto local=universe_.pointRelative(frame,eye/std::pow(10.,universe_.describe(frame).frame.logRadius),body.id.home,0.);
        if(glm::distance(body.position,local)<50000.)if(auto next=strategy_.contact(body.id,seconds+dt))result.push_back({body,next->body.position,next->body.orientation});
    }
    for(auto body:convoys_.localBodies(system,seconds)){
        const auto local=universe_.pointRelative(frame,eye/std::pow(10.,universe_.describe(frame).frame.logRadius),body.id.home,0.);
        if(glm::distance(body.position,local)<50000.)if(auto next=convoys_.contact(body.id,seconds+dt))result.push_back({body,next->body.position,next->body.orientation});
    }
    return result;
}
void XalariaCombat::annotate(FlightInstruments& instruments)const{
    effects_.annotate(instruments, !armed);
}
int64_t XalariaCombat::prizeCargo(const ActivityId& id)const {
    if(isConvoyActor(id))return convoys_.prizeCargo(id);
    const auto found=ships_.find(id);if(found==ships_.end())return 0;
    if(found->second.cargoLeft>=0)return found->second.cargoLeft;
    const auto c=xalariaTrafficContact(economy_,id,lastGate_,lastSeconds_);
    return c?2+int64_t(c->body.seed%6):0;
}
std::optional<XalariaTrafficContact> XalariaCombat::selected()const{
    return target?xalariaTrafficContact(economy_,*target,lastGate_,lastSeconds_):std::nullopt;
}
bool XalariaCombat::recover(Navigation& nav){
    if(pilot_.ship().hull>0){message="Emergency recovery is available only after the ship is destroyed.";return false;}
    std::optional<XPlace> haven;
    const auto& catalogue=*universe_.xalaria();
    for(const auto& [system,star]:catalogue.stars){
        for(const auto& [world,record]:star.worlds)if(record.stations){
            const XPlace port{system,world,0,false};
            if(fence(economy_.faction(port))){haven=port;break;}
        }
        if(haven)break;
    }
    if(!haven)return false;
    economy_.credits_-=std::min<int64_t>(500000,economy_.credits_);
    economy_.cargo_.fill(0);economy_.artwork_.clear();stolen_.fill(0);
    for(auto& contract:economy_.contracts_)if(contract.state==XContract::State::Active)contract.state=XContract::State::Expired;
    repairs_=std::max(repairs_,pilot_.ship().appliedRepairPoints)+100;pilot_.applyPortRepairs(repairs_);
    pilot_.brake();pilot_.clearWaypoint();pilot_.releaseWaypointControl();
    const auto berth=*dockingBerth(economy_.portBody(*haven));
    nav.changeFrame(berth.id.home);nav.setRenderScale(3.);nav.orientation=berth.orientation;
    const auto eye=berth.metres+berth.orientation*glm::dvec3(0,0,1.);
    nav.offset=eye/1000.-nav.orientation*glm::dvec3(0,0,1);
    pilot_.synchronizeFrame();pilot_.resetMotion();economy_.sync(nav,pilot_);
    economy_.show(haven->system,haven->world);economy_.view.place=*haven;
    economy_.travelToPort(*haven,nav,pilot_);armed=false;missiles_.clear();
    message="Recovered at "+economy_.placeName(*haven)+". Cargo was lost; recovery cost up to 5000 GC. Warrants remain.";return true;
}
std::string XalariaCombat::serialize()const{
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17);
    out<<"XALARIA_WARFARE 2 "<<unsigned(fit_)<<' '<<unsigned(weapon)<<' '<<armed<<' '<<heat_<<' '<<cooldown_<<' '
       <<overheated_<<' '<<torpedoes_<<' '<<shots_<<' '<<victories_<<' '<<repairs_<<' '<<bountyDue_<<' '<<lastSeconds_<<' '<<bool(target);
    if(target)out<<' '<<std::quoted(target->str());
    for(auto units:stolen_)out<<' '<<units;
    out<<' '<<warrants_.size();for(const auto& [system,amount]:warrants_)out<<' '<<system<<' '<<amount;
    out<<' '<<ships_.size();
    for(const auto& [id,s]:ships_){
        out<<' '<<std::quoted(id.str())<<' '<<s.hull<<' '<<s.engines<<' '<<s.aggroUntil<<' '<<s.cooldown<<' '
           <<unsigned(s.mode)<<' '<<s.moving<<' '<<s.bountyRecorded<<' '<<s.threatened<<' '<<s.cargoLeft<<' '<<s.from<<' '<<s.until;
        for(const auto p:{s.previous,s.position,s.velocity})out<<' '<<p.x<<' '<<p.y<<' '<<p.z;
        for(const auto q:{s.previousOrientation,s.orientation})out<<' '<<q.w<<' '<<q.x<<' '<<q.y<<' '<<q.z;
    }
    out<<' '<<missiles_.size();for(const auto& missile:missiles_){
        out<<' '<<std::quoted(missile.frame.str())<<' '<<std::quoted(missile.target.str())<<' '<<missile.age;
        for(const auto p:{missile.position,missile.direction})out<<' '<<p.x<<' '<<p.y<<' '<<p.z;
    }
    return out.str();
}
bool XalariaCombat::restore(std::string_view saved,double seconds){
    if(saved.empty()){configurePilot();return true;}
    try{
        XalariaCombat next(universe_,economy_,pilot_);std::istringstream in{std::string(saved)};in.imbue(std::locale::classic());
        std::string tag;unsigned version,fit,mode,active,hot,selected;size_t count;
        if(!(in>>tag>>version>>fit>>mode>>active>>next.heat_>>next.cooldown_>>hot>>next.torpedoes_>>next.shots_>>next.victories_>>next.repairs_>>next.bountyDue_>>next.lastSeconds_>>selected)||
           tag!="XALARIA_WARFARE"||(version!=1&&version!=2)||fit>2||mode>2||active>1||hot>1||selected>1||
           !std::isfinite(next.heat_)||next.heat_<0||next.heat_>100||!std::isfinite(next.cooldown_)||next.cooldown_<0||next.cooldown_>8||
           next.torpedoes_>(fit==2?10u:fit==1?4u:0u)||next.shots_>1000000000000ULL||next.victories_>1000000000ULL||
           next.repairs_>1000000000000ULL||next.bountyDue_<0||next.bountyDue_>walletLimit||
           !std::isfinite(next.lastSeconds_)||next.lastSeconds_<0||next.lastSeconds_>seconds+.11)return false;
        next.fit_=CombatFit(fit);next.weapon=CombatWeapon(mode);next.armed=active;next.overheated_=hot;
        const auto valid=[&](const ActivityId& id){return isStrategyActor(id)?strategy_.validIdentity(id):isConvoyActor(id)?convoys_.validIdentity(id):isXalariaTraffic(id)&&economy_.orbitalPlace(trafficStation(id)).has_value();};
        const auto id=[&]()->std::optional<ActivityId>{std::string text;if(!(in>>std::quoted(text)))return {};const auto split=text.find("@activity1:");if(split==text.npos)return {};
            ActivityId value{Address::parse(text.substr(0,split)),0};const auto digits=std::string_view(text).substr(split+11);
            const auto [end,error]=std::from_chars(digits.data(),digits.data()+digits.size(),value.slot);
            if(error!=std::errc{}||end!=digits.data()+digits.size())return {};
            return valid(value)?std::optional{value}:std::nullopt;};
        if(selected){next.target=id();if(!next.target)return false;}
        for(unsigned i=0;i<xGoodCount;++i)if(!(in>>next.stolen_[i])||next.stolen_[i]<0||next.stolen_[i]>economy_.cargo_[i])return false;
        if(!(in>>count)||count>10000)return false;
        for(size_t i=0;i<count;++i){uint32_t system;int64_t amount;if(!(in>>system>>amount)||!universe_.xalaria()->stars.contains(system)||amount<=0||amount>1000000000||!next.warrants_.emplace(system,amount).second)return false;}
        if(!(in>>count)||count>2048)return false;
        for(size_t i=0;i<count;++i){
            const auto key=id();CombatShip s;unsigned mode,moving,bounty,threat;
            if(!key||!(in>>s.hull>>s.engines>>s.aggroUntil>>s.cooldown>>mode>>moving>>bounty>>threat>>s.cargoLeft>>s.from>>s.until)||
               !std::isfinite(s.hull)||s.hull<0||s.hull>100||!std::isfinite(s.engines)||s.engines<0||s.engines>100||
               !std::isfinite(s.aggroUntil)||s.aggroUntil<0||s.aggroUntil>seconds+10000||!std::isfinite(s.cooldown)||s.cooldown<0||s.cooldown>8||
               mode>unsigned(version==1?CombatMode::Return:CombatMode::Skirmish)||moving>1||bounty>1||threat>1||s.cargoLeft< -1||s.cargoLeft>7||
               !std::isfinite(s.from)||!std::isfinite(s.until)||s.from<0||s.until<s.from||s.until>seconds+.11)return false;
            s.mode=CombatMode(mode);s.moving=moving;s.bountyRecorded=bounty;s.threatened=threat;
            for(auto* p:{&s.previous,&s.position,&s.velocity})if(!(in>>p->x>>p->y>>p->z)||!finite(*p)||glm::length(*p)>1e20)return false;
            if(glm::length(s.velocity)>5000)return false;
            for(auto* q:{&s.previousOrientation,&s.orientation})if(!(in>>q->w>>q->x>>q->y>>q->z)||!std::isfinite(glm::length(*q))||std::abs(glm::length(*q)-1)>1e-6)return false;
            if(!next.ships_.emplace(*key,s).second)return false;
        }
        if(!(in>>count)||count>16)return false;
        for(size_t i=0;i<count;++i){
            Torpedo missile;std::string frame;if(!(in>>std::quoted(frame)))return false;missile.frame=Address::parse(frame);universe_.describe(missile.frame);
            const auto victim=id();if(!victim||!(in>>missile.age)||!std::isfinite(missile.age)||missile.age<0||missile.age>12)return false;missile.target=*victim;
            for(auto* p:{&missile.position,&missile.direction})if(!(in>>p->x>>p->y>>p->z)||!finite(*p)||glm::length(*p)>1e20)return false;
            if(std::abs(glm::length(missile.direction)-1)>1e-6)return false;next.missiles_.push_back(missile);
        }
        in>>std::ws;if(!in.eof())return false;
        fit_=next.fit_;weapon=next.weapon;armed=next.armed;heat_=next.heat_;cooldown_=next.cooldown_;overheated_=next.overheated_;
        torpedoes_=next.torpedoes_;shots_=next.shots_;victories_=next.victories_;repairs_=next.repairs_;bountyDue_=next.bountyDue_;
        lastSeconds_=next.lastSeconds_;target=next.target;stolen_=next.stolen_;warrants_=std::move(next.warrants_);ships_=std::move(next.ships_);missiles_=std::move(next.missiles_);
        explosions_.clear();configurePilot();return true;
    }catch(...){return false;}
}
} // namespace infinity
