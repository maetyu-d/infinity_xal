#include "infinity/XalariaStrategy.hpp"
#include "infinity/XalariaCombat.hpp"
#include "infinity/XalariaStories.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
namespace infinity {
namespace {
uint32_t systemOf(Address a){while(a.kind()>Kind::System)a=a.parent();return uint32_t(a.id());}
glm::dquat heading(glm::dvec3 v,glm::dquat fallback={1,0,0,0}){if(glm::length(v)<1e-5)return fallback;v=glm::normalize(v);return glm::quatLookAtRH(v,std::abs(v.y)>.99?glm::dvec3(1,0,0):glm::dvec3(0,1,0));}
glm::dvec3 limited(glm::dvec3 v,double length){return v*std::min(1.,length/std::max(1e-9,glm::length(v)));}
}
bool XalariaStrategy::ownsSource(const ActivityId& id)const{for(const auto& [n,f]:fleet_)if(f.source==id)return true;return false;}
bool XalariaStrategy::suppresses(const ActivityId& id)const{return ownsSource(id)||wrecks_.contains(id);}
bool XalariaStrategy::validIdentity(const ActivityId& id)const{
 if(!isStrategyActor(id)||id.home.kind()<Kind::System)return false;
 const auto system=systemOf(id.home);if(!universe_.xalaria()->stars.contains(system))return false;
 try{universe_.describe(id.home);}catch(...){return false;}
 const auto kind=id.slot&0xf0000000u;return kind==fleetActivityTag?(id.slot&0x0fffffffu)>0:(id.slot&0x0fffffffu)>>3>0;
}
ActivityBody XalariaStrategy::fleetBody(const FleetShip& f,double seconds)const{
 ActivityBody b;b.id={f.frame,fleetActivityTag|f.id};b.kind=f.kind;b.seed=f.seed;b.name=f.name;b.radius=f.radius;b.position=f.mooring;b.velocity=f.velocity;b.orientation=f.axes;
 if(f.source&&!f.launched)b.id=*f.source;
 if(f.underway&&f.launched&&f.escort){
  const auto pose=combat_.convoys().stage(*f.escort,seconds);b.id={pose.frame,fleetActivityTag|f.id};b.position=pose.position+pose.orientation*glm::dvec3(320.+double(f.id%3)*140.,160.,-250.);b.velocity=pose.velocity;b.orientation=pose.orientation;
 }else if(f.underway&&f.launched&&!f.route.empty()){
  const size_t count=std::max<size_t>(1,f.route.size()-1);const double elapsed=std::max(0.,seconds-f.departure);
  const size_t leg=std::min(count-1,size_t(elapsed/120.));const double local=elapsed-double(leg)*120.;const bool inbound=local>=60.;
  const auto a=f.route[std::min(leg,f.route.size()-1)],z=f.route[std::min(leg+1,f.route.size()-1)],system=inbound?z:a;
  const auto gate=combat_.convoys().mouth(system,inbound?a:z);const auto port=inbound?f.destination:f.origin;
  auto harbour=combat_.convoys().port(system).value_or(port);if(system==f.origin.system)harbour=f.origin;if(system==f.destination.system)harbour=f.destination;
  const auto berth=*dockingBerth(economy_.portBody(harbour));
  auto dock=berth.metres+berth.orientation*glm::dvec3(0,140.,420.);
  dock=universe_.pointRelative(berth.id.home,dock/std::pow(10.,universe_.describe(berth.id.home).frame.logRadius),gate.reference,0.);
  auto begin=inbound?gate.metres:dock,end=inbound?dock:gate.metres;
  if(leg==0&&!inbound)begin=universe_.pointRelative(f.frame,f.mooring/std::pow(10.,universe_.describe(f.frame).frame.logRadius),gate.reference,0.);
  const double t=std::clamp((inbound?local-60.:local)/60.,0.,1.),blend=t*t*(3.-2.*t),speed=6.*t*(1.-t)/60.;
  b.id={gate.reference,fleetActivityTag|f.id};b.position=glm::mix(begin,end,blend);b.velocity=(end-begin)*speed;b.orientation=heading(end-begin,f.axes);
 }
 b.hull=f.hull;b.systems[size_t(ShipSystem::Hull)]=f.hull;b.systems[size_t(ShipSystem::Engines)]=f.engines;b.disabled=f.lost||f.hull<=0;
 b.tint={.27,.79,.94};b.status="YOUR FLEET / "+std::string(fleetOrderName(f.order))+" / "+(f.captain.empty()?"CAPTAIN REQUIRED":f.captain);
 return b;
}
ActivityBody XalariaStrategy::campaignBody(const FactionCampaign& c,unsigned member,double seconds)const{
 const bool defender=member>=4;const auto berth=*dockingBerth(economy_.portBody(c.target));
 ActivityBody b;b.id={berth.id.home,campaignActivityTag|(c.id<<3)|member};b.kind=ActivityKind::Patrol;b.seed=mix64(uint64_t(c.id)*31+member);
 const double phase=seconds*.018+member*1.570796;
 const double distance=defender?850.:1900.;
 b.position=berth.metres+berth.orientation*glm::dvec3(std::cos(phase)*distance,200.+std::sin(phase*.61)*220.,std::sin(phase)*distance+1800.);
 b.velocity=berth.orientation*glm::dvec3(-std::sin(phase)*distance*.018,std::cos(phase*.61)*220.*.018*.61,std::cos(phase)*distance*.018);
 b.orientation=heading(b.velocity,berth.orientation);b.radius=member==0?42.:29.;
 b.name=std::string(defender?"Defender ":"Expedition ")+std::to_string(c.id)+"-"+std::to_string(member+1);
 const auto* s=station(c.target);const auto faction=defender&&s?s->owner:c.attacker;
 b.tint=defender?glm::vec3(.27,.77,.91):glm::vec3(.89,.36,.24);
 b.hostile=(c.player&&defender)||(allegiance_!=XFaction::Independent&&faction!=allegiance_&&(c.attacker==allegiance_||(s&&s->owner==allegiance_)));b.status=std::string(xFactionName(faction))+" / "+(defender?"STATION DEFENCE":"BLOCKADE EXPEDITION");
 return b;
}
std::optional<XalariaTrafficContact> XalariaStrategy::contact(const ActivityId& id,double seconds)const{
 for(const auto& [n,f]:fleet_)if(!f.lost&&((f.source==id&&!f.launched)||((id.slot&0xf0000000u)==fleetActivityTag&&(id.slot&0x0fffffffu)==n))){
  auto b=fleetBody(f,seconds);if(b.id!=id)return {};economy_.applyTrafficRecovery(b,seconds);
  return XalariaTrafficContact{b,f.destination,TrafficService::Orbital,TrafficPhase::OnCircuit,{},0.,economy_.trafficShield(b,seconds)};
 }
 if(auto w=wrecks_.find(id);w!=wrecks_.end()){
  if(w->second.salvage==0&&w->second.survivors==0)return {};auto b=w->second.body;b.disabled=true;b.hull=0.;b.tint*=.2f;
  return XalariaTrafficContact{b,combat_.convoys().port(systemOf(id.home)).value_or(XPlace{}),TrafficService::Orbital,TrafficPhase::OnCircuit,{},0.,{}};
 }
 if((id.slot&0xf0000000u)==campaignActivityTag){
  const auto found=campaigns_.find((id.slot&0x0fffffffu)>>3);if(found==campaigns_.end())return {};const auto& c=found->second;const auto member=id.slot&7;
  if(c.finished||seconds<c.arrival||c.dead&(1u<<member)||(member<4&&(c.player||member>=c.force)))return {};
  const auto* s=station(c.target);if(!s||(member>=4&&member-4>=std::min(4u,s->garrison)))return {};
  auto b=campaignBody(c,member,seconds);if(b.id.home!=id.home)return {};economy_.applyTrafficRecovery(b,seconds);
  return XalariaTrafficContact{b,c.target,TrafficService::Orbital,TrafficPhase::OnCircuit,{},0.,economy_.trafficShield(b,seconds,combat_.shieldsLowered(id))};
 }
 return {};
}
std::vector<ActivityBody> XalariaStrategy::bodies(uint32_t system,double seconds)const{
 std::vector<ActivityBody> out;
 for(const auto& [id,f]:fleet_)if(!f.lost){auto b=fleetBody(f,seconds);if(systemOf(b.id.home)==system)if(auto c=contact(b.id,seconds))out.push_back(c->body);}
 for(const auto& [id,c]:campaigns_)if(!c.finished&&c.target.system==system&&seconds>=c.arrival)for(unsigned m=0;m<8;++m){auto b=campaignBody(c,m,seconds);if(suppresses(b.id))continue;if(auto contact=this->contact(b.id,seconds))out.push_back(contact->body);}
 for(const auto& [id,w]:wrecks_)if(systemOf(id.home)==system)if(auto c=contact(id,seconds))out.push_back(c->body);
 return out;
}
bool XalariaStrategy::capture(const StoryContext& context){
 if(!context.boardedVessel)return fail("Board a surrendered or disabled ship to claim its hull.");const auto id=*context.boardedVessel;
 if(ownsSource(id)||(id.slot&0xf0000000u)==fleetActivityTag)return fail("That hull is already registered or belongs to an active expedition.");
 auto c=xalariaTrafficContact(economy_,id,{},clock_);const auto state=combat_.ships_.find(id);
 if(!c||c->body.radius<minimumTrafficCollarRadius||state==combat_.ships_.end()||state->second.hull<=0||
   (state->second.mode!=CombatMode::Surrendered&&state->second.mode!=CombatMode::Disabled))return fail("A live surrendered or disabled boardable hull is required.");
 if(fleet_.size()>=128)return fail("Fleet registry is full.");FleetShip f;f.id=nextFleet_++;f.source=id;f.name=c->body.name+" / Prize";f.kind=c->body.kind;
 f.engines=state->second.engines;f.seed=c->body.seed;f.radius=c->body.radius;f.hull=state->second.hull;f.frame=id.home;f.mooring=c->body.position;f.axes=c->body.orientation;
 f.origin=f.destination=c->port;f.departure=clock_;f.contraband=!combat_.outlaw(id)&&!lawfulTarget(id)&&!combat_.convoys().lawfulTarget(id);
 if(isConvoyActor(id)){auto key=combat_.convoys().identity(id,clock_);if(key){auto& r=combat_.convoys_.records_.at(*key);const auto& plan=combat_.convoys().plan(*key);if(!r.delivered){f.cargo[size_t(plan.good)]=r.remaining;r.stolen+=r.remaining;r.remaining=0;}r.haltedAt=clock_;manifestChanged(*key,0);}}
 else {auto& state=combat_.ships_.at(id);if(state.cargoLeft<0)state.cargoLeft=c->body.kind==ActivityKind::Freighter?2+int64_t(c->body.seed%6):0;f.cargo[size_t(XGood(c->body.seed%7))]=state.cargoLeft;state.cargoLeft=0;}
 if(f.contraband)combat_.crime(id,120000);
 if((id.slot&0xf0000000u)==campaignActivityTag){auto it=campaigns_.find((id.slot&0x0fffffffu)>>3);if(it!=campaigns_.end()){const auto member=id.slot&7;it->second.dead|=1u<<member;if(member>=4){auto& depot=stations_.at(it->second.target);if(depot.garrison)--depot.garrison;if((it->second.dead&0xf0u)==0xf0u)depot.garrison=0;depot.dirty=true;}}}
 discover(context.system,"captured captain's navigation records");
 if(auto port=combat_.convoys().port(context.system)){f.origin=f.destination=*port;for(auto next:universe_.xalaria()->star(context.system).wormholes)discover(next,"captured route chart");}
 fleet_.emplace(f.id,std::move(f));report(context.system,"Captured hull registered. Commission a captain before giving orders.");message="Hull claimed / F10 Fleet / H hires a captain and repairs drives";return true;
}
bool XalariaStrategy::hire(uint32_t id,const StoryContext& context){
 auto it=fleet_.find(id);if(it==fleet_.end()||it->second.lost)return fail("Choose a surviving fleet ship.");auto& f=it->second;if(!f.captain.empty()&&f.hull>=100.&&f.engines>=100.)return fail("This vessel is fully crewed and repaired.");
 if(!((f.source&&context.boardedVessel==f.source)||(economy_.dockedPlace()&&*economy_.dockedPlace()==f.origin)))return fail("Meet the crew aboard this prize or at its home station.");
 const int64_t fee=(f.captain.empty()?80000:0)+int64_t(std::ceil(100.-f.hull))*1600+int64_t(std::ceil(100.-f.engines))*300;if(economy_.credits_<fee)return fail("Captain and drive commissioning costs "+xMoney(fee)+".");
 economy_.credits_-=fee;if(f.captain.empty()){f.captain=recruits_.empty()?"Captain "+std::to_string(100+mix64(f.seed)%900):recruits_.back();if(!recruits_.empty()){recruits_.pop_back();f.loyalty=65;}}
 f.hull=f.engines=100.;const auto active=fleetBody(f,clock_).id;if(auto it=combat_.ships_.find(active);it!=combat_.ships_.end()){it->second.hull=it->second.engines=100.;it->second.mode=CombatMode::Circuit;it->second.moving=false;}if(f.source){auto& s=combat_.ships_.at(*f.source);s.hull=f.hull;s.engines=100.;s.mode=CombatMode::Circuit;s.moving=false;}
 message=f.captain+" commissioned / drives repaired / orders available";return true;
}
bool XalariaStrategy::purchase(ActivityKind kind){
 if(!economy_.dockedPlace()||!economy_.canTrade(*economy_.dockedPlace()))return fail("Dock at a friendly shipyard to purchase a vessel.");if(fleet_.size()>=128)return fail("Fleet registry is full.");
 const bool warship=kind==ActivityKind::Patrol;const int64_t cost=warship?1200000:650000;auto& market=economy_.market(*economy_.dockedPlace());
 if(economy_.credits_<cost||market.stock[1]<10||market.stock[3]<6||market.stock[5]<2)return fail("Shipyard needs 10 iron, 6 nickel, 2 titanium and "+xMoney(cost)+".");
 economy_.credits_-=cost;market.cash+=cost;market.stock[1]-=10;market.stock[3]-=6;market.stock[5]-=2;
 FleetShip f;f.id=nextFleet_++;f.seed=mix64(uint64_t(f.id)^0x464c454554ULL);f.origin=f.destination=*economy_.dockedPlace();f.kind=warship?ActivityKind::Patrol:ActivityKind::Freighter;f.radius=warship?32.:48.;f.name=std::string(warship?"Warship ":"Freighter ")+std::to_string(f.id);f.launched=true;
 const auto berth=*dockingBerth(economy_.portBody(f.origin));f.frame=berth.id.home;f.axes=berth.orientation;f.mooring=berth.metres+berth.orientation*glm::dvec3(350.+f.id%5*160.,200.,450.);
 fleet_.emplace(f.id,std::move(f));message="Hull purchased / H commissions a captain at this station";return true;
}
bool XalariaStrategy::depart(FleetShip& f,XPlace destination,double seconds){
 auto path=route(f.origin.system,destination.system);if(path.empty())return fail("No directed wormhole route connects those ports.");
 if(f.order==FleetOrder::Trade){
  int64_t count=0;for(auto n:f.cargo)count+=n;
  if(!count){auto& source=economy_.market(f.origin);double best=0;XGood good=XGood::Nickel;int64_t units=0,price=0;
   for(size_t g=0;g<xGoodCount;++g){const auto amount=std::min<int64_t>({source.stock[g],int64_t(f.radius),80});if(amount<=0)continue;auto buy=economy_.quote(f.origin,XGood(g),amount,true),sell=economy_.quote(destination,XGood(g),amount,false);
    if(buy.available&&sell.available&&buy.total<=economy_.credits_&&sell.total>buy.total+5000&&double(sell.total-buy.total)>best){best=double(sell.total-buy.total);good=XGood(g);units=amount;price=buy.total;}}
   if(!units)return fail("Captain found no funded profitable cargo on this route.");source.stock[size_t(good)]-=units;source.cash+=price;economy_.credits_-=price;f.cargo[size_t(good)]=units;f.costBasis=price;f.contraband=false;
  }
 }
 f.escort.reset();
 if(f.order==FleetOrder::Escort){
  const auto cycle=uint64_t(seconds/convoyPeriod);for(const auto& [service,plan]:combat_.convoys().plans())if(plan.origin==f.origin&&plan.destination==destination){
   auto key=ConvoyKey{service,cycle};if(combat_.convoys().departure(key)<seconds-5.)++key.cycle;f.escort=key;f.route=plan.route;f.departure=combat_.convoys().departure(key);f.arrival=combat_.convoys().arrival(key);break;
  }
  if(!f.escort)return fail("No scheduled convoy directly serves these two ports. Choose its published destination.");
 }else{f.route=std::move(path);f.departure=seconds+5.;f.arrival=f.departure+std::max<size_t>(1,f.route.size()-1)*120.;}
 f.destination=destination;f.underway=true;return true;
}
bool XalariaStrategy::order(uint32_t id,FleetOrder order,uint32_t system){
 auto it=fleet_.find(id);if(it==fleet_.end()||it->second.lost)return fail("Choose a surviving fleet ship.");auto& f=it->second;if(f.captain.empty())return fail("Commission a captain before giving orders.");if(f.engines<=0&&order!=FleetOrder::Hold)return fail("Drives disabled. Board or dock at its port and use H to repair.");
 if(f.underway&&order!=FleetOrder::Hold)return fail("Captain is committed to the current gate transit. Hold first to cancel at the current position.");
 if(order==FleetOrder::Hold){const auto body=fleetBody(f,clock_);f.frame=body.id.home;f.mooring=body.position;f.axes=body.orientation;f.origin=combat_.convoys().port(systemOf(f.frame)).value_or(f.origin);f.underway=false;f.order=order;f.velocity={};message="Captain ordered to hold position.";return true;}
 if(f.loyalty<15)return fail("The captain refuses another deployment until morale recovers.");
 auto target=combat_.convoys().port(system);if(!target)return fail("Choose a system with a station in the atlas first.");
 if(order==FleetOrder::Blockade&&controls(*target))return fail("Your fleet already controls that station.");
 const auto previous=f.order;f.order=order;
 if(order==FleetOrder::Follow&&systemOf(f.frame)==system){f.destination=*target;message="Captain following the flagship in this system.";return true;}
 if(!depart(f,*target,clock_)){f.order=previous;return false;}
 report(f.origin.system,f.name+" dispatched / "+std::string(fleetOrderName(order))+" / "+economy_.placeName(*target));message="Orders transmitted / departure after the boarding collar is clear";return true;
}
void XalariaStrategy::arrive(FleetShip& f){
 const auto back=f.origin;const auto body=fleetBody(f,f.arrival);
 if(f.escort&&systemOf(body.id.home)!=f.destination.system){f.frame=body.id.home;f.mooring=body.position;f.axes=body.orientation;f.origin=f.destination=combat_.convoys().port(systemOf(body.id.home)).value_or(f.origin);f.underway=false;f.order=FleetOrder::Hold;f.escort.reset();report(f.origin.system,f.name+" escort stranded / convoy failed to arrive");return;}f.frame=body.id.home;f.mooring=body.position;f.axes=body.orientation;f.velocity={};f.origin=f.destination;f.underway=false;
 discover(f.destination.system,"captain's arrival report");++f.missions;f.loyalty=std::min(100u,f.loyalty+1);
 if(f.order==FleetOrder::Trade){
  const auto faction=economy_.faction(f.destination);if(f.contraband&&faction!=XFaction::Independent&&faction!=XFaction::Rebel){report(f.destination.system,f.name+" refused a lawful market with contraband aboard.");f.order=FleetOrder::Hold;return;}
  auto& market=economy_.market(f.destination);int64_t income=0;
  for(size_t g=0;g<xGoodCount;++g)if(f.cargo[g]){const auto q=economy_.quote(f.destination,XGood(g),f.cargo[g],false);if(!q.available||q.total>market.cash)continue;income+=q.total;market.cash-=q.total;market.stock[g]+=f.cargo[g];tax(f.destination,q.fee);f.cargo[g]=0;}
  const auto wage=std::min<int64_t>(income,5000);economy_.credits_=std::min<int64_t>(1000000000000000LL,economy_.credits_+income-wage);
  report(f.destination.system,f.name+" settled cargo / "+xMoney(income)+" / captain fee "+xMoney(wage));
  if(!depart(f,back,clock_)){f.order=FleetOrder::Hold;report(f.destination.system,f.name+" holding / "+message);}
 }else if(f.order==FleetOrder::Blockade){launchCampaign(back,f.destination,allegiance_,1,clock_,f.id);for(auto& [id,c]:campaigns_)if(c.fleet==f.id)c.arrival=clock_;}
 else if(f.order==FleetOrder::Escort){
  const auto lot=f.escort?shipments_.find(*f.escort):shipments_.end();
  if(lot!=shipments_.end()&&lot->second.settled&&!lot->second.lost&&lot->second.units>0){auto& m=economy_.market(f.destination);const auto pay=std::min<int64_t>(m.cash,8000+lot->second.units*200);m.cash-=pay;economy_.credits_=std::min<int64_t>(1000000000000000LL,economy_.credits_+pay);trust_[size_t(economy_.faction(f.destination))]=std::min(1000,trust_[size_t(economy_.faction(f.destination))]+1);report(f.destination.system,f.name+" escort completed / "+xMoney(pay));}
  else report(f.destination.system,f.name+" escort failed to deliver its manifest.");f.order=FleetOrder::Hold;f.escort.reset();
 }
}
void XalariaStrategy::advanceFleet(const CombatPlayer& p,double dt){
 for(auto& [id,f]:fleet_){if(f.lost)continue;const auto body=fleetBody(f,clock_);
  if(auto damage=combat_.ships_.find(body.id);damage!=combat_.ships_.end()){f.hull=damage->second.hull;f.engines=damage->second.engines;if(f.hull<=0){f.lost=true;report(systemOf(body.id.home),f.name+" lost. Survivors and salvage remain at its last position.");continue;}}
  if(f.engines<=0){if(f.underway||f.order!=FleetOrder::Hold){f.frame=body.id.home;f.mooring=body.position;f.axes=body.orientation;f.origin=combat_.convoys().port(systemOf(f.frame)).value_or(f.origin);f.underway=false;f.order=FleetOrder::Hold;f.escort.reset();report(systemOf(f.frame),f.name+" disabled / captain requests drive repair");}f.velocity={};continue;}
  if(p.clamped==body.id){if(f.underway){if(!f.escort){f.departure+=dt;f.arrival+=dt;}else if(clock_>=f.departure){f.order=FleetOrder::Hold;f.underway=false;f.escort.reset();report(f.origin.system,f.name+" missed departure while its boarding collar was occupied.");}}continue;}
  if(f.underway){if(clock_>=f.departure)f.launched=true;if(clock_>=f.arrival){if(f.escort){const auto* record=combat_.convoys().record(*f.escort);if(record&&record->haltedAt<0&&!record->delivered)continue;if(!record){allocate(*f.escort);const auto* lot=shipment(*f.escort);if(lot&&!lot->settled)complete(*f.escort,lot->units);}}arrive(f);}continue;}
  if(f.captain.empty())continue;
  if(f.order==FleetOrder::Follow&&!p.sheltered&&!p.transit){
   const auto system=systemOf(p.frame);if(system!=systemOf(f.frame)){if(auto port=combat_.convoys().port(system))depart(f,*port,clock_);continue;}
   const auto eye=universe_.pointRelative(p.frame,p.position/std::pow(10.,universe_.describe(p.frame).frame.logRadius),f.frame,0.);
   const auto forward=universe_.relative(p.frame,f.frame,0.).orientation*p.forward;auto goal=eye-forward*(350.+f.id%7*110.);
   if(f.frame.kind()==Kind::Planet){const auto radius=std::pow(10.,universe_.describe(f.frame).frame.logRadius);if(glm::length(goal)<radius+4000.)goal=glm::normalize(goal)*(radius+4000.);}
   const auto wanted=limited(goal-f.mooring,180.);f.velocity+=limited(wanted-f.velocity,dt*30.);f.mooring+=f.velocity*dt;f.axes=heading(f.velocity,f.axes);
  }
  if(f.order==FleetOrder::Salvage){
   for(auto& [target,w]:wrecks_)if(w.known&&target.home==f.frame&&w.salvage>0){auto goal=w.body.position;auto delta=goal-f.mooring;f.velocity=limited(delta,100.);f.mooring+=f.velocity*dt;f.axes=heading(f.velocity,f.axes);
    if(glm::length(delta)<400.){const auto units=std::min<int64_t>(w.salvage,40);w.salvage-=units;f.cargo[1]+=units;report(f.destination.system,f.name+" recovered "+std::to_string(units)+" iron. Return on a trade order to sell it.");f.order=FleetOrder::Hold;f.velocity={};}break;
   }
  }
 }
}
XFaction XalariaStrategy::side(const ActivityBody& b)const{
 if(isConvoyActor(b.id))if(auto key=combat_.convoys().identity(b.id,clock_)){const auto& plan=combat_.convoys().plan(*key);return (b.id.slot&15u)==3||(b.id.slot&15u)==4?plan.rival:plan.faction;}
 if((b.id.slot&0xf0000000u)==campaignActivityTag){auto it=campaigns_.find((b.id.slot&0x0fffffffu)>>3);if(it!=campaigns_.end()){if((b.id.slot&7)<4)return it->second.attacker;if(auto* s=station(it->second.target))return s->owner;}}
 if((b.id.slot&0xf0000000u)==fleetActivityTag||ownsSource(b.id))return allegiance_;
 if(auto p=economy_.orbitalPlace(trafficStation(b.id)))return economy_.faction(*p);
 return XFaction::Independent;
}
bool XalariaStrategy::lawfulTarget(const ActivityId& id)const{
 if((id.slot&0xf0000000u)!=campaignActivityTag)return false;auto it=campaigns_.find((id.slot&0x0fffffffu)>>3);if(it==campaigns_.end())return false;
 const auto& c=it->second;const bool defender=(id.slot&7)>=4;const auto* target=station(c.target);return (c.player&&defender)||(allegiance_!=XFaction::Independent&&target&&((defender&&c.attacker==allegiance_&&target->owner!=allegiance_)||(!defender&&target->owner==allegiance_&&c.attacker!=allegiance_)));
}
void XalariaStrategy::struck(const ActivityBody& body,double seconds,bool playerShot,bool destroyed){
 if((body.id.slot&0xf0000000u)==campaignActivityTag){auto c=campaigns_.find((body.id.slot&0x0fffffffu)>>3);if(c!=campaigns_.end()){
  c->second.physicalAt=seconds;const auto member=body.id.slot&7;if(destroyed){c->second.dead|=1u<<member;
   if(member>=4){auto& station=stations_.at(c->second.target);station.garrison=station.garrison?station.garrison-1:0;if((c->second.dead&0xf0u)==0xf0u)station.garrison=0;station.dirty=true;}
   if(playerShot)trust_[size_t(member<4?c->second.attacker:stations_.at(c->second.target).owner)]=std::max(-1000,trust_[size_t(side(body))]-4);
  }
 }}
 if(!destroyed||wrecks_.contains(body.id)||wrecks_.size()>=2048)return;
 StrategicWreck w;w.body=body;w.body.hull=0.;w.body.disabled=true;w.body.velocity={};w.created=seconds;w.salvage=std::max<int64_t>(1,int64_t(body.radius/3));w.survivors=1+unsigned(body.seed%5);w.known=knowledge_.contains(systemOf(body.id.home));wrecks_.emplace(body.id,std::move(w));
 report(systemOf(body.id.home),body.name+" destroyed / lifeboats and salvage reported");
}
bool XalariaStrategy::salvage(const ActivityId& id,const CombatPlayer& p,bool rescue){
 auto it=wrecks_.find(id);if(it==wrecks_.end())return fail("Select a destroyed crewed ship with remaining salvage or lifeboats.");auto& w=it->second;
 const auto eye=universe_.pointRelative(p.frame,p.position/std::pow(10.,universe_.describe(p.frame).frame.logRadius),id.home,0.);
 if(p.transit||p.sheltered||glm::distance(eye,w.body.position)>1200.)return fail("Close within 1.2 km in free flight to recover the wreck.");
 w.known=true;if(rescue){if(!w.survivors)return fail("No survivors remain here.");if(recruits_.size()+w.survivors>24)return fail("All 24 passenger berths are occupied. Commission rescued captains first.");
  for(unsigned n=0;n<w.survivors;++n)recruits_.push_back("Rescued captain "+std::to_string(100+mix64(w.body.seed+n)%900));
  w.survivors=0;const auto faction=side(w.body);trust_[size_t(faction)]=std::min(1000,trust_[size_t(faction)]+3);discover(systemOf(id.home),"survivor testimony");message="Survivors recovered. Their captains can join your fleet at H.";return true;
 }
 const auto n=std::min<int64_t>(w.salvage,std::max<int64_t>(0,int64_t(economy_.capacity_)-economy_.cargoUnits()));if(!n)return fail("Hold full, or this wreck has already been stripped.");w.salvage-=n;economy_.cargo_[1]+=n;message="Recovered "+std::to_string(n)+" iron / finite wreck salvage";return true;
}
bool XalariaStrategy::interdicts(const ActivityId& actor,ConvoyKey key)const{
 if(!combat_.convoys().plans().contains(key.service))return false;const auto target=combat_.convoys().plan(key).destination;
 if((actor.slot&0xf0000000u)==campaignActivityTag){const auto it=campaigns_.find((actor.slot&0x0fffffffu)>>3);return it!=campaigns_.end()&&!it->second.finished&&!it->second.player&&(actor.slot&7)<4&&clock_>=it->second.arrival&&it->second.target==target;}
 for(const auto& [id,f]:fleet_)if(!f.lost&&!f.underway&&!f.captain.empty()&&f.engines>0&&f.order==FleetOrder::Blockade&&f.destination==target&&
   ((f.source==actor&&!f.launched)||((actor.slot&0xf0000000u)==fleetActivityTag&&(actor.slot&0x0fffffffu)==id)))return true;
 return false;
}
void XalariaStrategy::simulate(const ActivityBody& raw,std::span<const ActivityBody> bodies,const CombatPlayer& p,double dt,const Geometry* geometry){
 if(raw.disabled||raw.hull<=0)return;const bool expedition=(raw.id.slot&0xf0000000u)==campaignActivityTag;
 if(expedition)if(auto c=campaigns_.find((raw.id.slot&0x0fffffffu)>>3);c!=campaigns_.end())c->second.physicalAt=p.seconds;
 const ActivityBody* enemy=nullptr;double distance=4500.;
 for(const auto& b:bodies){if(b.id==raw.id||b.disabled||b.hull<=0)continue;
  const bool opposite=expedition&&(b.id.slot&0xf0000000u)==campaignActivityTag&&((b.id.slot&0x0fffffffu)>>3)==((raw.id.slot&0x0fffffffu)>>3)&&((b.id.slot&7)<4)!=((raw.id.slot&7)<4);
  const bool fleetFight=((raw.id.slot&0xf0000000u)==fleetActivityTag||ownsSource(raw.id))&&(b.id.slot&0xf0000000u)==campaignActivityTag&&lawfulTarget(b.id);
  bool convoyFight=false;
  if((raw.id.slot&0xf0000000u)==fleetActivityTag)if(auto own=fleet_.find(raw.id.slot&0x0fffffffu);own!=fleet_.end()&&own->second.escort&&isConvoyActor(b.id)){
   auto key=combat_.convoys().identity(b.id,p.seconds);convoyFight=key==own->second.escort&&((b.id.slot&15u)==3||(b.id.slot&15u)==4);
  }
  const auto shipment=isConvoyActor(b.id)?combat_.convoys().identity(b.id,p.seconds):std::nullopt;
  const bool interdiction=shipment&&(b.id.slot&15u)<3&&b.systems[size_t(ShipSystem::Engines)]>0&&interdicts(raw.id,*shipment);
  if(!opposite&&!fleetFight&&!convoyFight&&!interdiction)continue;
  const auto at=universe_.pointRelative(b.id.home,b.position/std::pow(10.,universe_.describe(b.id.home).frame.logRadius),raw.id.home,0.);const auto range=glm::distance(at,raw.position);if(range<distance){distance=range;enemy=&b;}
 }
 bool player=raw.hostile&&!p.sheltered&&!p.transit;
 auto eye=universe_.pointRelative(p.frame,p.position/std::pow(10.,universe_.describe(p.frame).frame.logRadius),raw.id.home,0.);
 if(player&&glm::distance(eye,raw.position)<distance){distance=glm::distance(eye,raw.position);enemy=nullptr;}else player=false;
 if(!enemy&&!player)return;if(enemy)eye=universe_.pointRelative(enemy->id.home,enemy->position/std::pow(10.,universe_.describe(enemy->id.home).frame.logRadius),raw.id.home,0.);
 auto* state=combat_.state(raw,p.seconds);if(!state||state->engines<=0)return;state->cooldown=std::max(0.,state->cooldown-dt);if(state->cooldown>0||distance<raw.radius*2)return;
 auto direction=glm::normalize(eye-raw.position);WeaponRay shot;shot.reference=raw.id.home;shot.origin=raw.position+direction*raw.radius*1.8;shot.direction=direction;shot.emitter=raw.id;shot.seconds=p.seconds;
 auto cover=std::vector<ActivityBody>(bodies.begin(),bodies.end());ActivityBody flagship;flagship.id={p.frame,UINT32_MAX};flagship.position=p.position;flagship.radius=6.;if(!p.sheltered&&!p.transit)cover.push_back(flagship);
 bool disabling=false;auto strike=traceWeapon(universe_,shot,distance,cover,geometry);if(strike.body){if(strike.body->id==flagship.id){combat_.incomingFire=raw.name;combat_.incomingAt=p.seconds;combat_.pilot_.applyWeaponDamage(10.,glm::inverse(p.orientation)*(-direction));}
  else {const auto key=isConvoyActor(strike.body->id)?combat_.convoys().identity(strike.body->id,p.seconds):std::nullopt;
   disabling=key&&(strike.body->id.slot&15u)==0&&interdicts(raw.id,*key);
   combat_.hit(*strike.body,12.,disabling,p.seconds,-direction,false);}}
 combat_.effects_.presentShot({shot.reference,shot.origin,shot.origin+direction*strike.distance,0,1,bool(strike.body),disabling,2,true});state->cooldown=1.;
}
std::string XalariaStrategy::radio(const ActivityId& id)const{
 for(const auto& [n,f]:fleet_)if(f.source==id||((id.slot&0xf0000000u)==fleetActivityTag&&(id.slot&0x0fffffffu)==n))return f.name+" / "+(f.captain.empty()?"uncommissioned prize":f.captain)+" / "+std::string(fleetOrderName(f.order))+" / loyalty "+std::to_string(f.loyalty);
 if(auto w=wrecks_.find(id);w!=wrecks_.end())return "Wreck / "+std::to_string(w->second.salvage)+" salvage units / "+std::to_string(w->second.survivors)+" survivors. F10: S salvage, R rescue.";
 return "Faction expedition. F10 shows reports your crews have earned.";
}
void XalariaStrategy::step(int amount){
 size_t n=0;if(page==0)n=fleet_.size();else if(page==1){for(const auto& r:reports_)if(knowledge_.contains(r.system))++n;}else{for(const auto& [id,c]:campaigns_)if(knowledge_.contains(c.origin.system)||knowledge_.contains(c.target.system))++n;}
 if(n)row=size_t((int64_t(row)+amount+int64_t(n)*8)%int64_t(n));else row=0;
}
}
