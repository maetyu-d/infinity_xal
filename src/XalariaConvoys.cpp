#include "infinity/XalariaConvoys.hpp"
#include "infinity/XalariaCombat.hpp"
#include "infinity/XalariaNavigation.hpp"
#include "infinity/XalariaStories.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>

namespace infinity {
namespace {
uint32_t systemOf(Address a){while(a.kind()>Kind::System)a=a.parent();return uint32_t(a.id());}
uint64_t seed(ConvoyKey k){return mix64(k.service^mix64(k.cycle)^0x434f4e564f59ULL);}
double smooth(double x){x=std::clamp(x,0.,1.);return x*x*x*(10.+x*(-15.+x*6.));}
double derivative(double x){return x<=0||x>=1?0.:30.*x*x*(1.-x)*(1.-x);}
glm::dvec3 limited(glm::dvec3 p,double cap){return p*std::min(1.,cap/std::max(1e-12,glm::length(p)));}
glm::dquat heading(glm::dvec3 direction){if(glm::length(direction)<1e-6)return {1,0,0,0};direction=glm::normalize(direction);return glm::quatLookAtRH(direction,std::abs(direction.y)>.99?glm::dvec3(0,0,1):glm::dvec3(0,1,0));}
uint32_t serviceOf(const ActivityId& id){return (id.slot>>4)&65535u;}
unsigned memberOf(const ActivityId& id){return id.slot&15u;}
bool opponent(unsigned member){return member==3||member==4;}
}
std::string_view convoyPhaseName(ConvoyPhase p){constexpr std::array names{"Loading / departure watch","Outbound to wormhole","Inbound to harbour","Freight delivered","Stranded / drives disabled","Convoy lost"};return names.at(unsigned(p));}
XalariaConvoys::XalariaConvoys(const Universe& u,XalariaEconomy& e,XalariaCombat& c):universe_(u),economy_(e),combat_(c){
 const auto& cat=*u.xalaria();std::map<uint32_t,std::vector<uint32_t>> reverse;
 for(const auto& [system,star]:cat.stars){
  for(auto next:star.wormholes)reverse[next].push_back(system);
  for(const auto& [world,r]:star.worlds)if(r.stations){ports_[system]={system,world,0,false};break;}
 }
 ports_[eunhiaStation.system]=eunhiaStation;ports_[phehmyarStation.system]=phehmyarStation;
 const auto find=[&](uint32_t start,bool backwards,bool allowStart,std::optional<uint32_t> avoid){
  std::vector<std::vector<uint32_t>> queue{{start}};std::set<uint32_t> visited{start};
  for(size_t i=0;i<queue.size()&&i<cat.stars.size();++i){
   auto path=queue[i];const auto last=path.back();
   if((allowStart||path.size()>1)&&ports_.contains(last)&&last!=avoid)return path;
   if(path.size()>=7)continue;
   auto edges=backwards?reverse[last]:cat.star(last).wormholes;
   if(!edges.empty())std::rotate(edges.begin(),edges.begin()+mix64(start)%edges.size(),edges.end());
   for(auto next:edges)if(visited.insert(next).second){auto branch=path;branch.push_back(next);queue.push_back(std::move(branch));}
  }return std::vector<uint32_t>{};
 };
 for(const auto& [system,star]:cat.stars){
  auto from=find(system,true,true,{});if(from.empty())continue;std::reverse(from.begin(),from.end());
  auto to=find(system,false,false,from.front());if(to.empty())to=find(system,false,false,{});
  if(to.empty()){if(!ports_.contains(system))continue;to={system};}
  auto route=from;route.insert(route.end(),to.begin()+1,to.end());
  if(system==eunhiaStation.system)route={system,phehmyarStation.system};
  if(route.size()>9||!ports_.contains(route.front())||!ports_.contains(route.back()))continue;
  const auto origin=ports_.at(route.front()),destination=ports_.at(route.back());
  const auto faction=economy_.baseFaction(origin);auto rival=economy_.baseFaction(destination);
  if(rival==faction)rival=XFaction((unsigned(faction)+1+mix64(system)%4)%5);
  const auto good=system==eunhiaStation.system?XGood::Nickel:faction==XFaction::Nature?XGood::Neurocells:XGood(mix64(system)%7);
  plans_.emplace(system,ConvoyPlan{system,origin,destination,route,faction,rival,good,system==eunhiaStation.system?12:12+int64_t(mix64(system^55)%13),system==eunhiaStation.system?20.:double(mix64(system)%240)});
  for(auto place:std::set<uint32_t>(route.begin(),route.end()))through_[place].push_back(system);
 }
}
const ConvoyPlan& XalariaConvoys::plan(ConvoyKey key)const{return plans_.at(key.service);}
const ConvoyRecord* XalariaConvoys::record(ConvoyKey key)const{const auto it=records_.find(key);return it==records_.end()?nullptr:&it->second;}
std::optional<XPlace> XalariaConvoys::port(uint32_t system)const{const auto it=ports_.find(system);return it==ports_.end()?std::nullopt:std::optional{it->second};}
double XalariaConvoys::departure(ConvoyKey key)const{return double(key.cycle)*convoyPeriod+plan(key).offset;}
double XalariaConvoys::arrival(ConvoyKey key)const{return departure(key)+225.+240.*double(std::max<size_t>(1,plan(key).route.size()-1)-1);}
bool XalariaConvoys::factionBattle(ConvoyKey key)const{return key.service!=eunhiaStation.system&&seed(key)%3==0;}
XalariaWormholeMouth XalariaConvoys::mouth(uint32_t system,uint32_t destination)const{
 if(const auto found=mouths_.find(system);found!=mouths_.end()){auto mouth=found->second;mouth.destination=destination;return mouth;}
 const auto& cat=*universe_.xalaria();const auto p=port(system);XalariaWormholeMouth gate;
 if(p){const auto b=*dockingBerth(economy_.portBody(*p));const auto outward=b.orientation*glm::dvec3(0,0,1),up=b.orientation*glm::dvec3(0,1,0);
  gate={b.id.home,b.metres+outward*8200.,glm::quatLookAtRH(outward,up),1050.,destination};}
 else {const auto outward=glm::normalize(glm::dvec3(.3,.4,1.));gate={cat.address(system),outward*(cat.star(system).radius*2.4+50000.),glm::quatLookAtRH(outward,glm::dvec3(0,1,0)),1050.,destination};}
 mouths_.emplace(system,gate);return gate;
}

ConvoyStage XalariaConvoys::stage(ConvoyKey key,double seconds)const{
 const auto& p=plan(key);const auto* r=record(key);const double at=r&&r->haltedAt>=0?std::min(seconds,r->haltedAt):seconds;
 const auto count=std::max<size_t>(1,p.route.size()-1);const double elapsed=at-departure(key);
 const auto leg=std::min(count-1,size_t(std::max(0.,elapsed)/240.));const double t=elapsed-double(leg)*240.;
 const auto a=p.route[std::min(leg,p.route.size()-1)],b=p.route[std::min(leg+1,p.route.size()-1)];
 const bool inbound=t>=105.;const auto system=inbound?b:a;const auto gate=mouth(system,inbound?a:b);
 const auto anchor=[&](uint32_t id){
  if(const auto port=this->port(id)){const auto berth=*dockingBerth(economy_.portBody(*port));return berth.metres+berth.orientation*glm::dvec3(180.,35.,300.);}
  return mouth(id,id).metres+glm::dvec3(400.,0.,-1700.);
 };
 const auto begin=inbound?gate.metres:anchor(a),end=inbound?anchor(b):gate.metres;
 const double fraction=inbound?(t-105.)/120.:t/105.;const auto delta=end-begin;
 const auto velocity=delta*(derivative(fraction)/(inbound?120.:105.));
 auto phase=elapsed<0?ConvoyPhase::Loading:at>=arrival(key)?ConvoyPhase::Delivered:inbound?ConvoyPhase::Inbound:ConvoyPhase::Outbound;
 if(r&&r->haltedAt>=0)phase=r->dead&1?ConvoyPhase::Lost:ConvoyPhase::Stranded;
 return {gate.reference,begin+delta*smooth(fraction),velocity,heading(delta),system,leg,t,phase};
}
ActivityId XalariaConvoys::id(ConvoyKey key,unsigned member,const Address& frame)const{return {frame,convoyActivityTag|(uint32_t(key.cycle&255)<<20)|(key.service<<4)|member};}
bool XalariaConvoys::validIdentity(const ActivityId& id)const{
 if(!isConvoyActor(id)||!plans_.contains(serviceOf(id)))return false;
 const auto& p=plans_.at(serviceOf(id));const auto system=systemOf(id.home);
 if(std::find(p.route.begin(),p.route.end(),system)==p.route.end())return false;
 return mouth(system,system).reference==id.home;
}
std::optional<ConvoyKey> XalariaConvoys::identity(const ActivityId& id,double seconds)const{
 if(!validIdentity(id))return {};
 const auto cycle=uint64_t(std::max(0.,seconds)/convoyPeriod);const auto code=(id.slot>>20)&255u;
 for(unsigned back=0;back<4;++back)if(cycle>=back&&((cycle-back)&255)==code)return ConvoyKey{serviceOf(id),cycle-back};
 for(const auto& [key,r]:records_)if(key.service==serviceOf(id)&&(key.cycle&255)==code&&r.haltedAt>=0)return key;
 return {};
}
ActivityBody XalariaConvoys::sample(ConvoyKey key,unsigned member,double seconds)const{
 const auto pose=stage(key,seconds);const auto& p=plan(key);const auto* r=record(key);const auto salt=seed(key);
 glm::dvec3 offset{};
 if(member==1)offset={-230.,90.,-160.};if(member==2)offset={230.,-70.,140.};
 if(opponent(member)){
  const double sign=member==3?-1.:1.;offset=glm::mix(glm::dvec3(sign*5200.,450.,-2000.),glm::dvec3(sign*1100.,170.,-400.),smooth((pose.localSeconds-12.)/28.));
 }
 if(member==5)offset=glm::mix(glm::dvec3(1500.,700.,4800.),glm::dvec3(0.,270.,-900.),smooth((pose.localSeconds-58.)/35.));
 ActivityBody body;body.id=id(key,member,pose.frame);body.seed=mix64(salt^member);body.position=pose.position+pose.orientation*offset;
 // Differentiate the same continuous formation curve used to draw the hull.
 body.velocity=pose.velocity;
 if(opponent(member)){
  const double sign=member==3?-1.:1.;const auto delta=glm::dvec3(sign*1100.,170.,-400.)-glm::dvec3(sign*5200.,450.,-2000.);
  body.velocity+=pose.orientation*delta*(derivative((pose.localSeconds-12.)/28.)/28.);
 }
 if(member==5)body.velocity+=pose.orientation*(glm::dvec3(0.,270.,-900.)-glm::dvec3(1500.,700.,4800.))*(derivative((pose.localSeconds-58.)/35.)/35.);
 body.orientation=pose.orientation;body.radius=member==0?62.:member==5?39.:opponent(member)?25.:32.;
 body.kind=member==0?ActivityKind::Freighter:opponent(member)&&!factionBattle(key)?ActivityKind::Raider:ActivityKind::Patrol;
 constexpr std::array colours{glm::vec3(.65,.82,1.),glm::vec3(.43,.89,.60),glm::vec3(.94,.70,.42),glm::vec3(.93,.38,.37),glm::vec3(.75,.56,.94)};
 body.tint=colours[size_t(opponent(member)?p.rival:p.faction)];
 body.name=member==0?"Convoy "+std::to_string(key.service)+"-"+std::to_string(key.cycle%1000):
   std::string(opponent(member)?factionBattle(key)?"Interdictor ":"Cutlass ":member==5?"Relief patrol ":"Escort ")+std::to_string(100+body.seed%900);
 body.status=std::string(xFactionName(opponent(member)?p.rival:p.faction))+" / "+
   (member==0?std::string(convoyPhaseName(pose.phase)):opponent(member)?factionBattle(key)?"FACTION INTERDICTION":"PIRATE AMBUSH":member==5?"DISTRESS RESPONSE":"CONVOY ESCORT");
 body.hostile=body.kind==ActivityKind::Raider||(r&&((r->duty==ConvoyDuty::Escort&&opponent(member))||(r->betrayed&&!opponent(member)&&member)));
 return body;
}
std::optional<XalariaTrafficContact> XalariaConvoys::contact(const ActivityId& actor,double seconds)const{
 const auto key=identity(actor,seconds);if(!key)return {};
 const auto* r=record(*key);const auto member=memberOf(actor);
 if(seconds>=arrival(*key)&&(!r||(!r->physical&&!r->background))){
  const unsigned dead=r?r->dead:0;
  const bool lost=(dead&0x18)!=0x18&&((dead&0x26)==0x26||seed(*key)%9==0);
  if(lost||opponent(member))return {};
 }
 auto body=sample(*key,member,seconds);
 if(combat_.strategy().suppresses(actor))return {};
 if(r&&(r->dead&(1u<<member))){
  const auto wreck=combat_.ships_.find(actor);if(wreck==combat_.ships_.end()||wreck->second.hull>0)return {};
  body.id=actor;body.position=wreck->second.position;body.orientation=wreck->second.orientation;
 }else if(body.id.home!=actor.home)return {};
 if(r&&r->frame.kind()>=Kind::System&&r->frame!=actor.home){
  const auto old=combat_.ships_.find(id(*key,member,r->frame));
  if(old!=combat_.ships_.end()){body.hull=old->second.hull;body.systems[size_t(ShipSystem::Engines)]=old->second.engines;}
 }
 combat_.apply(body,seconds);
 const auto p=stage(*key,seconds).system==plan(*key).destination.system?plan(*key).destination:plan(*key).origin;
 return XalariaTrafficContact{body,p,TrafficService::Orbital,TrafficPhase::OnCircuit,{},0.,economy_.trafficShield(body,seconds,combat_.shieldsLowered(actor))};
}
std::vector<ConvoyKey> XalariaConvoys::localKeys(uint32_t system,double seconds)const{
 std::set<ConvoyKey> keys;const auto cycle=uint64_t(std::max(0.,seconds)/convoyPeriod);const auto it=through_.find(system);
 if(it!=through_.end())for(auto service:it->second)for(unsigned back=0;back<3;++back)if(cycle>=back){
  ConvoyKey key{service,cycle-back};if(seconds<departure(key)-30.||seconds>arrival(key)+600.)continue;
  if(stage(key,seconds).system==system)keys.insert(key);
 }
 for(const auto& [key,r]:records_)if(r.haltedAt>=0&&stage(key,seconds).system==system&&(seconds-r.haltedAt<7200.||tracked_==key))keys.insert(key);
 return {keys.begin(),keys.end()};
}
std::vector<ActivityBody> XalariaConvoys::bodies(const Address& frame,double seconds)const{
 std::vector<ActivityBody> out;
 for(const auto key:localKeys(systemOf(frame),seconds))for(unsigned member=0;member<6;++member){
  if(const auto c=contact(id(key,member,frame),seconds))out.push_back(c->body);
 }
 // Wrecks remain at the scene instead of reappearing at the next wormhole.
 for(const auto& [id,s]:combat_.ships_)if(isConvoyActor(id)&&id.home==frame&&s.hull<=0&&std::none_of(out.begin(),out.end(),[&](const auto& b){return b.id==id;}))
  if(auto c=contact(id,seconds))out.push_back(c->body);
 return out;
}
std::vector<ActivityBody> XalariaConvoys::localBodies(uint32_t system,double seconds)const{return bodies(mouth(system,system).reference,seconds);}
ConvoyRecord& XalariaConvoys::activate(ConvoyKey key,double seconds){
 if(const auto it=records_.find(key);it!=records_.end())return it->second;
 if(records_.size()>=4096)std::erase_if(records_,[&](const auto& entry){return !entry.second.known&&seconds>arrival(entry.first)+7200.;});
 if(records_.size()>=4096){
  auto oldest=records_.end();for(auto it=records_.begin();it!=records_.end();++it)if(tracked_!=it->first&&(oldest==records_.end()||departure(it->first)<departure(oldest->first)))oldest=it;
  if(oldest!=records_.end())records_.erase(oldest);
 }
 ConvoyRecord r;
 // Load each departure from the same finite station inventory used by traders.
 r.remaining=combat_.strategy().allocate(key);
 if(const auto* lot=combat_.strategy().shipment(key);lot&&lot->settled){r.delivered=!lot->lost;if(lot->lost){r.dead|=1;r.haltedAt=departure(key)+80.;}}
 r.frame=stage(key,seconds).frame;
 // Activity slots repeat only after 128 hours of flight; stale damage must not attach to a new departure.
 std::erase_if(combat_.ships_,[&](const auto& entry){return isConvoyActor(entry.first)&&serviceOf(entry.first)==key.service&&((entry.first.slot>>20)&255)==(key.cycle&255)&&entry.second.until<departure(key)-30.;});
 return records_.emplace(key,r).first->second;
}
bool XalariaConvoys::discover(ConvoyKey key){if(!plans_.contains(key.service))return false;auto& r=activate(key,seconds_);if(r.known)return false;r.known=true;message="Convoy report / "+economy_.placeName(plan(key).origin)+" to "+economy_.placeName(plan(key).destination);return true;}
bool XalariaConvoys::hear(const ActivityId& id,double seconds){seconds_=seconds;const auto key=identity(id,seconds);return key&&discover(*key);}
std::vector<ConvoyKey> XalariaConvoys::known()const{std::vector<ConvoyKey> out;for(const auto& [key,r]:records_)if(r.known)out.push_back(key);std::stable_sort(out.begin(),out.end(),[&](auto a,auto b){return std::tuple(tracked_!=a,-departure(a),a)<std::tuple(tracked_!=b,-departure(b),b);});return out;}
std::optional<ConvoyKey> XalariaConvoys::selected()const{const auto list=known();return list.empty()?std::nullopt:std::optional{list[std::min(row,list.size()-1)]};}
void XalariaConvoys::step(int n){const auto list=known();if(!list.empty())row=(row+list.size()+n)%list.size();}
void XalariaConvoys::reputation(XFaction faction,int points){auto& value=standing_[size_t(faction)];value=std::clamp(value+points,-1000,1000);}
bool XalariaConvoys::join(ConvoyKey key,ConvoyDuty duty,const CombatPlayer& player){
 auto it=records_.find(key);if(it==records_.end()||!it->second.known||duty==ConvoyDuty::None){message="Discover this convoy through a crew or local departure board first.";return false;}
 auto& r=it->second;const auto pose=stage(key,player.seconds);
 const auto eye=universe_.pointRelative(player.frame,player.position/std::pow(10.,universe_.describe(player.frame).frame.logRadius),pose.frame,0.);
 const bool atSource=economy_.dockedPlace()==plan(key).origin;
 if((!atSource&&glm::distance(eye,pose.position)>2500.)||pose.phase>=ConvoyPhase::Delivered||r.duty!=ConvoyDuty::None||
    (tracked_&&*tracked_!=key&&record(*tracked_)&&!record(*tracked_)->claimed&&!record(*tracked_)->betrayed&&record(*tracked_)->duty==ConvoyDuty::Escort&&stage(*tracked_,player.seconds).phase<ConvoyPhase::Delivered)){
  message="Join before arrival, at the departure berth or within 2.5 km. Finish your existing escort first.";return false;
 }
 if(duty==ConvoyDuty::Escort&&(r.betrayed||standing(plan(key).faction)<-20||combat_.wanted(plan(key).origin.system))){message="The convoy refuses your escort credentials.";return false;}
 r.duty=duty;r.joined=player.seconds;tracked_=key;row=0;
 message=duty==ConvoyDuty::Escort?"Escort accepted. Accompany the freighter, defend it and report at the destination berth.":"Convoy marked as a prize. Attacking it brings warrants and retaliation; board to take its cargo.";return true;
}
void XalariaConvoys::prepare(ConvoyKey key,double seconds){
 auto& r=activate(key,seconds);const auto pose=stage(key,seconds);if(r.frame==pose.frame)return;
 for(unsigned member=0;member<6;++member){
  if(r.dead&(1u<<member))continue;
  const auto old=id(key,member,r.frame),next=id(key,member,pose.frame);const auto found=combat_.ships_.find(old);
  if(found!=combat_.ships_.end()){
   auto s=found->second;const auto canonical=sample(key,member,seconds);s.previous=s.position=canonical.position;s.velocity=canonical.velocity;
   s.previousOrientation=s.orientation=canonical.orientation;s.from=s.until=seconds;s.moving=false;combat_.ships_.erase(found);combat_.ships_[next]=s;
  }
  if(const auto charge=economy_.trafficShields_.find(old);charge!=economy_.trafficShields_.end()){economy_.trafficShields_[next]=charge->second;economy_.trafficShields_.erase(charge);}
  if(combat_.target==old)combat_.target=next;
 }
 r.frame=pose.frame;
}
void XalariaConvoys::finish(ConvoyKey key,double seconds){
 auto& r=records_.at(key);if(r.delivered||r.haltedAt>=0||seconds<arrival(key))return;
 if(!r.physical||seconds-r.lastPhysical>3.){
  r.background=true;
  if(!combat_.strategy().escorted(key)&&(r.dead&0x18)!=0x18&&((r.dead&0x26)==0x26||seed(key)%9==0)){r.remaining=0;r.dead|=1;r.haltedAt=std::min(seconds,departure(key)+80.);combat_.strategy().complete(key,0,true);return;}
  r.dead|=(1u<<3)|(1u<<4);
 }
 r.delivered=true;const auto& p=plan(key);
 combat_.strategy().complete(key,r.remaining);
 combat_.strategy().noticeDelivery(key,r.duty==ConvoyDuty::Escort&&!r.betrayed);
 if(tracked_==key)message="Convoy arrived / "+std::to_string(r.remaining)+" "+std::string(xGoodInfo(p.good).name)+" delivered. Report at "+economy_.placeName(p.destination)+".";
}
void XalariaConvoys::advance(const CombatPlayer& player,double dt){
 seconds_=player.seconds;const auto system=systemOf(player.frame);
 for(const auto key:localKeys(system,player.seconds)){
  const auto pose=stage(key,player.seconds);const auto eye=universe_.pointRelative(player.frame,player.position/std::pow(10.,universe_.describe(player.frame).frame.logRadius),pose.frame,0.);
  const double range=glm::distance(eye,pose.position);const bool docked=economy_.dockedPlace()==plan(key).origin||economy_.dockedPlace()==plan(key).destination;
  if(range<80000.||docked||tracked_==key){
   prepare(key,player.seconds);auto& r=records_.at(key);
   if(player.seconds>=arrival(key)&&!r.physical)finish(key,player.seconds);
   if((range<18000.&&player.seconds-lastNotice_>=20.)||docked){if(discover(key))lastNotice_=player.seconds;}
   if(range<80000.&&!player.transit&&pose.phase!=ConvoyPhase::Loading)r.physical=true;
   if(r.duty==ConvoyDuty::Escort&&!r.betrayed&&(pose.phase==ConvoyPhase::Outbound||pose.phase==ConvoyPhase::Inbound)&&range<2500.&&!player.sheltered&&!player.transit&&r.lastPhysical<=player.seconds&&player.seconds-r.lastPhysical<.21)
    r.accompanied+=std::clamp(dt,0.,.1);
   r.lastPhysical=player.seconds;
  }
 }
 std::vector<ConvoyKey> active;for(const auto& [key,r]:records_)if(!r.delivered&&r.haltedAt<0)active.push_back(key);
 for(auto key:active)finish(key,player.seconds+dt);
}
bool XalariaConvoys::raider(const ActivityId& id)const{const auto key=identity(id,seconds_);return key&&opponent(memberOf(id))&&!factionBattle(*key);}
bool XalariaConvoys::lawfulTarget(const ActivityId& id)const{if(!isConvoyActor(id))return hostile(id);const auto key=identity(id,seconds_);if(!key)return false;const auto* r=record(*key);return raider(id)||(r&&r->duty==ConvoyDuty::Escort&&!r->betrayed&&opponent(memberOf(id)));}
bool XalariaConvoys::hostile(const ActivityId& id)const{
 if(isConvoyActor(id)){const auto key=identity(id,seconds_);return key&&sample(*key,memberOf(id),seconds_).hostile;}
 if(!isCombatActor(id)||!tracked_)return false;const auto* r=record(*tracked_);
 if(!r||r->duty!=ConvoyDuty::Escort||r->betrayed||!factionBattle(*tracked_)||stage(*tracked_,seconds_).system!=systemOf(id.home))return false;
 const auto state=combat_.ships_.find(id);if(state==combat_.ships_.end()||state->second.mode!=CombatMode::Skirmish)return false;
 ActivityBody body;body.id=id;return side(*tracked_,body)==1;
}
void XalariaConvoys::struck(const ActivityBody& body,double seconds,bool playerShot){
 const auto key=identity(body.id,seconds);if(!key)return;auto& r=activate(*key,seconds);const auto member=memberOf(body.id);
 if(playerShot)++r.playerHits;
 if(playerShot&&!opponent(member)&&!r.betrayed){r.betrayed=true;reputation(plan(*key).faction,-8);if(r.duty==ConvoyDuty::Escort){message="Escort contract void / you opened fire on the convoy.";following=false;}}
 const auto it=combat_.ships_.find(body.id);if(it==combat_.ships_.end())return;
 if(it->second.hull<=0){r.dead|=1u<<member;if(playerShot&&opponent(member)&&factionBattle(*key))reputation(plan(*key).rival,-2);}
 if(member==0&&(it->second.hull<=0||it->second.engines<=0)){if(r.haltedAt<0)r.haltedAt=seconds;message=it->second.hull<=0?"Convoy freighter destroyed. Its shipment will not arrive.":"Convoy stranded / engines disabled. Its cargo remains aboard.";}
}
int XalariaConvoys::side(ConvoyKey key,const ActivityBody& body)const{
 if(isConvoyActor(body.id)){const auto other=identity(body.id,seconds_);return other==key?int(opponent(memberOf(body.id))):-1;}
 if(combat_.strategy().interdicts(body.id,key))return 1;
 for(const auto& [id,f]:combat_.strategy().fleet())if(!f.lost&&f.underway&&f.escort==key&&
    ((f.source==body.id&&!f.launched)||((body.id.slot&0xf0000000u)==fleetActivityTag&&(body.id.slot&0x0fffffffu)==id)))return 0;
 if(!isCombatActor(body.id))return -1;
 if((body.id.slot&15u)==0)return 1;
 if(!factionBattle(key))return 0;
 const auto port=economy_.orbitalPlace(trafficStation(body.id));if(!port)return -1;
 const auto faction=economy_.faction(*port);return faction==plan(key).faction?0:faction==plan(key).rival?1:-1;
}
void XalariaConvoys::simulate(const ActivityBody& raw,std::span<const ActivityBody> bodies,const CombatPlayer& player,double dt,const Geometry* geometry){
 const auto key=identity(raw.id,player.seconds);if(!key||raw.disabled)return;auto& r=activate(*key,player.seconds);if(!r.physical)return;r.lastPhysical=player.seconds;
 const auto member=memberOf(raw.id);auto* s=combat_.state(raw,player.seconds);if(!s)return;
 s->cooldown=std::max(0.,s->cooldown-dt);
 const bool battle=stage(*key,player.seconds).localSeconds>=30.||r.betrayed;
 const ActivityBody* victim=nullptr;bool targetPlayer=false;
 if(member&&s->engines>0&&battle){
  if(((r.betrayed&&!opponent(member))||(opponent(member)&&s->threatened))&&!player.sheltered&&!player.transit)targetPlayer=true;
  else for(const auto& other:bodies){
   if(other.disabled||other.systems[size_t(ShipSystem::Engines)]<=0||side(*key,other)!=int(!opponent(member)))continue;
   if(isConvoyActor(other.id)&&memberOf(other.id)==0&&member==3)continue;
   const auto point=universe_.pointRelative(other.id.home,other.position/std::pow(10.,universe_.describe(other.id.home).frame.logRadius),raw.id.home,0.);
   if(glm::distance(raw.position,point)>8500.)continue;
   if(!victim||glm::distance(raw.position,point)<glm::distance(raw.position,universe_.pointRelative(victim->id.home,victim->position/std::pow(10.,universe_.describe(victim->id.home).frame.logRadius),raw.id.home,0.)))victim=&other;
  }
 }
 const auto canonical=sample(*key,member,player.seconds);
 if(!s->moving&&!victim&&!targetPlayer&&s->engines>0)return;
 s->moving=true;s->previous=raw.position;s->previousOrientation=raw.orientation;s->from=player.seconds;s->until=player.seconds+dt;
 glm::dvec3 aim{};double distance=0.;glm::dvec3 goal{};
 if(victim||targetPlayer){
  const auto at=targetPlayer?universe_.pointRelative(player.frame,player.position/std::pow(10.,universe_.describe(player.frame).frame.logRadius),raw.id.home,0.):universe_.pointRelative(victim->id.home,victim->position/std::pow(10.,universe_.describe(victim->id.home).frame.logRadius),raw.id.home,0.);
  distance=glm::distance(at,raw.position);aim=(at-raw.position)/std::max(1.,distance);
  auto tangent=glm::cross(aim,glm::dvec3(.1,1.,.2));if(glm::length(tangent)<1e-6)tangent={1,0,0};
  goal=canonical.velocity+aim*std::clamp((distance-1050.)*.18,-65.,165.)+glm::normalize(tangent)*23.;s->mode=CombatMode::Attack;
 }else{goal=canonical.velocity+limited((canonical.position-raw.position)*.7,140.);s->mode=CombatMode::Return;}
 if(member==0&&s->engines>0)goal=canonical.velocity+limited((canonical.position-raw.position)*.8,140.);
 if(s->engines<=0){goal={};s->mode=CombatMode::Disabled;}
 s->velocity+=limited(goal-s->velocity,dt*36.);s->position=s->previous+s->velocity*dt;
 if(glm::length(s->velocity)>2.)s->orientation=heading(s->velocity);
 if((victim||targetPlayer)&&s->engines>0&&distance>100.&&distance<2600.&&s->cooldown<=0.){
  auto cover=std::vector<ActivityBody>(bodies.begin(),bodies.end());
  ActivityBody hull;hull.id={player.frame,UINT32_MAX};hull.position=player.position;hull.radius=6.;if(!player.sheltered&&!player.transit)cover.push_back(hull);
  WeaponRay ray;ray.reference=raw.id.home;ray.origin=raw.position+aim*raw.radius*1.9;ray.direction=aim;ray.seconds=player.seconds;ray.emitter=raw.id;
  const auto trace=traceWeapon(universe_,ray,distance+70.,cover,geometry);
  if(trace.body){
   const bool friendly=(trace.body->id==hull.id&&!targetPlayer)||side(*key,*trace.body)==int(opponent(member));
   if(friendly){s->cooldown=.2;return;}
  }
  const double damage=member==5?10.:opponent(member)?9.:7.;
  if(trace.body){
   ++r.npcHits;
   if(trace.body->id==hull.id){combat_.incomingFire=raw.name;combat_.incomingAt=player.seconds;combat_.pilot_.applyWeaponDamage(damage,glm::inverse(player.orientation)*(universe_.relative(raw.id.home,player.frame,0.).orientation*(-aim)));}
   else combat_.hit(*trace.body,damage,false,player.seconds,-aim,false);
  }
  combat_.effects_.presentShot({ray.reference,ray.origin,ray.origin+aim*trace.distance,0,1,bool(trace.body),false,2,opponent(member)});
  ++r.npcShots;s->cooldown=opponent(member)?1.6:1.3;
 }
}
std::optional<ActivityId> XalariaConvoys::localTarget(const ActivityBody& raw,std::span<const ActivityBody> bodies,const CombatPlayer& player)const{
 if(!isCombatActor(raw.id))return {};
 const bool raiding=(raw.id.slot&15u)==0;
 if(!raiding&&combat_.wanted(systemOf(raw.id.home)))return {};
 const ActivityBody* target=nullptr;
 for(const auto& body:bodies){
  if(body.disabled||body.systems[size_t(ShipSystem::Engines)]<=0||body.id==raw.id)continue;
  bool eligible=raiding?isConvoyActor(body.id)&&memberOf(body.id)==0:(isCombatActor(body.id)&&(body.id.slot&15u)==0);
  if(!raiding&&isConvoyActor(body.id))if(const auto key=identity(body.id,player.seconds)){const auto team=side(*key,raw);eligible=team>=0&&side(*key,body)==1-team;}
  if(!eligible)continue;
  const auto position=universe_.pointRelative(body.id.home,body.position/std::pow(10.,universe_.describe(body.id.home).frame.logRadius),raw.id.home,0.);
  if(glm::distance(raw.position,position)>7000.)continue;
  if(!target||glm::distance(raw.position,position)<glm::distance(raw.position,target->position))target=&body;
 }
 (void)player;return target?std::optional{target->id}:std::nullopt;
}
int64_t XalariaConvoys::prizeCargo(const ActivityId& id)const{const auto key=identity(id,seconds_);const auto* r=key?record(*key):nullptr;return r&&memberOf(id)==0?r->remaining:0;}
bool XalariaConvoys::seize(const ActivityId& id,const StoryContext& context){
 const auto key=identity(id,seconds_);if(!key||memberOf(id)!=0||context.boardedVessel!=id||context.system!=systemOf(id.home)||!combat_.shieldsLowered(id)){
  message="Board the disabled convoy freighter to seize its actual manifest.";return false;
 }
 const auto vessel=contact(id,seconds_);if(!vessel||vessel->body.hull<=0){message="The freighter is a wreck; no safe transfer bay remains.";return false;}
 auto& r=activate(*key,seconds_);if(r.delivered){message="This manifest was already unloaded at its destination.";return false;}const auto count=std::min(r.remaining,std::max<int64_t>(0,int64_t(economy_.capacity_)-economy_.cargoUnits()));
 if(!count){message=r.remaining?"Your cargo hold is full.":"The convoy's cargo has already been transferred.";return false;}
 const auto& p=plan(*key);economy_.cargo_[size_t(p.good)]+=count;combat_.stolen_[size_t(p.good)]+=count;r.remaining-=count;combat_.strategy().manifestChanged(*key,r.remaining);r.stolen+=count;
 if(!r.betrayed){r.betrayed=true;reputation(p.faction,-8);}combat_.crime(id,80000);
 message="Seized "+std::to_string(count)+" "+std::string(xGoodInfo(p.good).name)+" from the convoy. Its destination loses that freight.";
 combat_.message=message;return true;
}
bool XalariaConvoys::report(){
 if(!tracked_){message="Choose an escort assignment first.";return false;}auto& r=records_.at(*tracked_);const auto& p=plan(*tracked_);
 if(r.claimed||r.duty!=ConvoyDuty::Escort||r.betrayed||!r.delivered||r.accompanied<45.||r.remaining*5<p.units*3||economy_.dockedPlace()!=p.destination){
  message="Escort reports require delivery of at least 60% of the cargo, 45 seconds accompanying the ship, and your berth at its destination.";return false;
 }
 const int64_t payment=200000+r.remaining*5000;
 if(economy_.credits_>1000000000000000LL-payment)return false;
 economy_.credits_+=payment;r.claimed=true;following=false;reputation(p.faction,5);
 message="Escort report accepted / "+xMoney(payment)+" / "+std::string(xFactionName(p.faction))+" standing +5.";return true;
}
bool XalariaConvoys::follow(Navigation& nav,Pilot& pilot,XalariaNavigation& course){
 const auto key=selected();if(!key){message="Discover a convoy first.";return false;}tracked_=key;following=true;
 if(pilot.instruments().docking.active){message="Clearing the berth before joining convoy formation.";return true;}
 const auto pose=stage(*key,pilot.ship().flightSeconds);const auto system=systemOf(nav.anchor);
 if(pose.system!=system){
  const auto& route=plan(*key).route;const auto here=std::find(route.begin(),route.end(),system);
  if(here!=route.end()&&here+1!=route.end()){
   course.selectSystem(*(here+1));course.targetWormhole();
  }else{course.selectSystem(pose.system);if(pose.frame.kind()==Kind::Planet)course.selectWorld(pose.frame.id());course.fly();return true;}
 }
 CombatPlayer player;player.frame=nav.anchor;player.position=nav.cameraPosition()*std::pow(10.,nav.logDistance);player.seconds=pilot.ship().flightSeconds;
 if(const auto point=waypoint(player)){pilot.navigateWaypoint(*point);return true;}return false;
}
std::optional<FlightWaypoint> XalariaConvoys::waypoint(const CombatPlayer& player)const{
 if(!following||!tracked_)return {};const auto key=*tracked_;const auto pose=stage(key,player.seconds);const auto system=systemOf(player.frame);
 if(pose.system!=system){
  const auto& route=plan(key).route;const auto here=std::find(route.begin(),route.end(),system);if(here==route.end()||here+1==route.end())return {};
  const auto gate=mouth(system,*(here+1));return FlightWaypoint{gate.reference,gate.metres+gate.orientation*glm::dvec3(0,0,-450.),{},0.,"CONVOY WORMHOLE CROSSING",false,{}};
 }
 if(const auto ship=contact(id(key,0,pose.frame),player.seconds)){
  auto goal=ship->body.position+ship->body.orientation*glm::dvec3(900.,180.,450.);
  // The rear formation offset must stay on the arrival side of a portal.
  // Otherwise a following ship can be sent back through the aperture.
  if(pose.localSeconds>=105.){
   const auto gate=mouth(pose.system,pose.system);const auto inward=gate.orientation*glm::dvec3(0,0,1);
   const double clearance=glm::dot(goal-gate.metres,inward);if(clearance<200.)goal+=inward*(200.-clearance);
  }
  return FlightWaypoint{pose.frame,goal,ship->body.velocity,0.,"CONVOY FORMATION / "+ship->body.name,false,{}};
 }
 return {};
}
double XalariaConvoys::marketFactor(const XPlace& p,XGood good)const{
 const auto it=through_.find(p.system);if(it==through_.end())return 1.;double factor=1.;const auto cycle=uint64_t(seconds_/convoyPeriod);
 for(auto service:it->second)for(unsigned back=0;back<3;++back){
  if(cycle<back)continue;const ConvoyKey key{service,cycle-back};const auto& route=plan(key);
  if(route.destination!=p||route.good!=good)continue;const double age=seconds_-arrival(key);if(age<0.||age>convoyPeriod)continue;
  const auto* r=record(key);const bool lost=r?(r->remaining<route.units/2||r->dead&1):seed(key)%9==0;
  factor+=(lost?.12:-.055)*(1.-age/convoyPeriod);
 }
 return std::clamp(factor,.80,1.3);
}

std::string XalariaConvoys::summary(ConvoyKey key,double seconds)const{
 const auto& p=plan(key);const auto* r=record(key);return std::string(convoyPhaseName(stage(key,seconds).phase))+" / "+
  std::to_string(r?r->remaining:p.units)+" "+std::string(xGoodInfo(p.good).name)+" / "+(factionBattle(key)?"faction interdiction":"pirate ambush risk");
}
std::string XalariaConvoys::radio(const ActivityId& id,TrafficRequest request,double seconds)const{
 const auto key=identity(id,seconds);if(!key)return "Convoy channel unavailable.";const auto& p=plan(*key);
 if(request==TrafficRequest::Dock)return combat_.shieldsLowered(id)?"Our drives are out and the collar is unlocked. Transfer bay access granted.":
  "Docking refused while the convoy is making passage. Use F9 to join the escort or follow our formation.";
 return "Convoy from "+economy_.placeName(p.origin)+" to "+economy_.placeName(p.destination)+". "+summary(*key,seconds)+
  ". "+std::string(xFactionName(p.faction))+" escorts are on watch. F9 opens this operation.";
}
std::string XalariaConvoys::serialize()const{
 std::ostringstream out;out<<std::setprecision(17)<<"XALARIA_CONVOYS 1 "<<seconds_<<' '<<lastNotice_<<' '<<following<<' '<<bool(tracked_);
 if(tracked_)out<<' '<<tracked_->service<<' '<<tracked_->cycle;for(auto standing:standing_)out<<' '<<standing;
 out<<' '<<records_.size();for(const auto& [key,r]:records_){out<<' '<<key.service<<' '<<key.cycle<<' '<<r.known<<' '<<r.physical<<' '<<r.delivered<<' '<<r.claimed<<' '<<r.betrayed<<' '<<r.background<<' '
  <<unsigned(r.duty)<<' '<<r.joined<<' '<<r.accompanied<<' '<<r.haltedAt<<' '<<r.lastPhysical<<' '<<r.remaining<<' '<<r.stolen<<' '<<r.dead<<' '<<std::quoted(r.frame.str())<<' '<<r.npcShots<<' '<<r.npcHits<<' '<<r.playerHits;}
 return out.str();
}
bool XalariaConvoys::restore(std::string_view text,double seconds){
 if(text.empty()){seconds_=seconds;return true;}
 try{
  std::istringstream in{std::string(text)};std::string tag;unsigned version,following,tracked;double clock,notice;size_t count;std::optional<ConvoyKey> selected;
  if(!(in>>tag>>version>>clock>>notice>>following>>tracked)||tag!="XALARIA_CONVOYS"||version!=1||!std::isfinite(clock)||clock<0||clock>seconds+.11||!std::isfinite(notice)||notice< -30||notice>seconds||following>1||tracked>1)return false;
  if(tracked){ConvoyKey k;if(!(in>>k.service>>k.cycle)||!plans_.contains(k.service)||k.cycle>uint64_t(seconds/convoyPeriod)+1)return false;selected=k;}
  std::array<int,5> standings;for(auto& value:standings)if(!(in>>value)||value< -1000||value>1000)return false;
  if(!(in>>count)||count>4096)return false;std::map<ConvoyKey,ConvoyRecord> records;
  for(size_t i=0;i<count;++i){
   ConvoyKey key;ConvoyRecord r;unsigned known,physical,delivered,claimed,betrayed,background,duty;std::string frame;
   if(!(in>>key.service>>key.cycle>>known>>physical>>delivered>>claimed>>betrayed>>background>>duty>>r.joined>>r.accompanied>>r.haltedAt>>r.lastPhysical>>r.remaining>>r.stolen>>r.dead>>std::quoted(frame)>>r.npcShots>>r.npcHits>>r.playerHits)||
    !plans_.contains(key.service)||key.cycle>uint64_t(seconds/convoyPeriod)+1||known>1||physical>1||delivered>1||claimed>1||betrayed>1||background>1||duty>2||r.dead>63||r.remaining<0||r.stolen<0||r.remaining+r.stolen>plan(key).units||r.npcHits>r.npcShots||r.npcShots>1000000000ULL||r.playerHits>1000000000ULL)return false;
   for(auto value:{r.joined,r.accompanied,r.lastPhysical})if(!std::isfinite(value)||value<0||value>seconds+.11)return false;
   if(!std::isfinite(r.haltedAt)||r.haltedAt< -1||r.haltedAt>seconds+.11)return false;
   r.frame=Address::parse(frame);if(!validIdentity(id(key,0,r.frame)))return false;
   r.known=known;r.physical=physical;r.delivered=delivered;r.claimed=claimed;r.betrayed=betrayed;r.background=background;r.duty=ConvoyDuty(duty);
   if(r.claimed&&(r.duty!=ConvoyDuty::Escort||!r.delivered||r.betrayed))return false;
   if(!records.emplace(key,r).second)return false;
  }
  in>>std::ws;if(!in.eof()||(selected&&(!records.contains(*selected)||!records.at(*selected).known)))return false;
  records_=std::move(records);standing_=standings;tracked_=selected;following=false;seconds_=clock;lastNotice_=notice;return true;
 }catch(...){return false;}
}
} // namespace infinity
