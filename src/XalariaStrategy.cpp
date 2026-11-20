#include "infinity/XalariaStrategy.hpp"
#include "infinity/XalariaCombat.hpp"
#include "infinity/XalariaStories.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <queue>
namespace infinity {
namespace {
uint64_t portSeed(XPlace p){return mix64(uint64_t(p.system)^(p.world*0x9e3779b97f4a7c15ULL)^(uint64_t(p.station)<<32)^0x53544154ULL);}
uint32_t systemOf(Address a){while(a.kind()>Kind::System)a=a.parent();return uint32_t(a.id());}
}
std::string_view fleetOrderName(FleetOrder order){constexpr std::array names{"Hold position","Follow flagship","Trade route","Escort route","Blockade / capture","Recover salvage"};return names.at(unsigned(order));}
XalariaStrategy::XalariaStrategy(const Universe& u,XalariaEconomy& e,XalariaCombat& c):universe_(u),economy_(e),combat_(c){
 for(const auto& [id,star]:u.xalaria()->stars)for(const auto& [world,w]:star.worlds)for(unsigned n=0;n<w.stations;++n)addStation({id,world,n,false});
 for(const auto& a:e.assets())if(a.state==XAssetState::Active&&a.kind!=XAssetKind::Farm)addStation({a.system,a.world,100000+a.id,false});
}
void XalariaStrategy::addStation(const XPlace& p){
 if(stations_.contains(p))return;StrategicStation s;s.place=p;s.owner=economy_.baseFaction(p);s.playerOwned=p.station>=100000;
 const auto key=portSeed(p);Random random{key};s.industry=s.owner==XFaction::Nature?1:unsigned(key%4);s.garrison=3+unsigned(key%5);s.ore=24000+int64_t(key%76000);
 for(size_t g=0;g<xGoodCount;++g){bool available=true;
  if(XGood(g)==XGood::Seed)available=false;
  if(XGood(g)==XGood::Nanomites&&p.station>=100000)available=false;
  if(s.owner==XFaction::Nature&&g<=size_t(XGood::Iridium))available=random.unit(30+g)>.5;
  s.market.target[g]=available?int64_t(80+random.bits(10+g)%121):0;s.market.stock[g]=s.market.target[g];
 }stations_.emplace(p,std::move(s));
}
const StrategicStation* XalariaStrategy::station(const XPlace& p)const{const auto it=stations_.find(p);return it==stations_.end()?nullptr:&it->second;}
XMarket* XalariaStrategy::market(const XPlace& p)const{const auto it=stations_.find(p);if(it==stations_.end())return nullptr;it->second.dirty=true;return &it->second.market;}
void XalariaStrategy::adoptMarkets(){if(adoptedMarkets_)return;adoptedMarkets_=true;for(const auto& [p,m]:economy_.markets_)if(auto it=stations_.find(p);it!=stations_.end()){it->second.market=m;it->second.dirty=true;}}
bool XalariaStrategy::controls(const XPlace& p)const{const auto* s=station(p);return s&&s->playerOwned;}
bool XalariaStrategy::fail(std::string text){message=std::move(text);return false;}
void XalariaStrategy::report(uint32_t system,std::string text){reports_.push_back({nextReport_++,system,clock_,std::move(text)});while(reports_.size()>1024){const auto unknown=std::find_if(reports_.begin(),reports_.end(),[&](const auto& r){return !knowledge_.contains(r.system);});if(unknown!=reports_.end())reports_.erase(unknown);else reports_.pop_front();}}
bool XalariaStrategy::discover(uint32_t system,std::string_view reason){
 if(!universe_.xalaria()->stars.contains(system)||!knowledge_.insert(system).second)return false;
 report(system,"Intelligence established / "+std::string(reason));return true;
}
std::vector<uint32_t> XalariaStrategy::route(uint32_t a,uint32_t b)const{
 if(!universe_.xalaria()->stars.contains(a)||!universe_.xalaria()->stars.contains(b))return {};
 std::map<uint32_t,uint32_t> previous{{a,a}};std::queue<uint32_t> queue;queue.push(a);
 while(!queue.empty()&&!previous.contains(b)){auto at=queue.front();queue.pop();for(auto n:universe_.xalaria()->star(at).wormholes)if(previous.emplace(n,at).second)queue.push(n);}
 if(!previous.contains(b))return {};std::vector<uint32_t> result{b};while(result.back()!=a)result.push_back(previous.at(result.back()));std::reverse(result.begin(),result.end());return result;
}
int64_t XalariaStrategy::allocate(ConvoyKey key){
 if(auto it=shipments_.find(key);it!=shipments_.end())return it->second.units;
 const auto& cv=combat_.convoys();if(!cv.plans().contains(key.service)||key.cycle+4<uint64_t(clock_/convoyPeriod))return 0;
 if(shipments_.size()>=32768){std::erase_if(shipments_,[&](const auto& entry){return entry.second.settled&&entry.first.cycle+3<uint64_t(clock_/convoyPeriod);});if(shipments_.size()>=32768)return 0;}
 const auto& plan=cv.plan(key);auto& source=economy_.market(plan.origin);const auto g=size_t(plan.good);const auto count=std::min(source.stock[g],plan.units);
 source.stock[g]-=count;if(auto it=stations_.find(plan.origin);it!=stations_.end())it->second.dirty=true;
 shipments_.emplace(key,StrategicShipment{count,false,false});return count;
}
void XalariaStrategy::complete(ConvoyKey key,int64_t units,bool lost){
 if(!shipments_.contains(key))allocate(key);auto it=shipments_.find(key);if(it==shipments_.end()||it->second.settled)return;
 auto& lot=it->second;units=std::clamp<int64_t>(units,0,lot.units);
 const auto& plan=combat_.convoys().plan(key);const auto* recorded=combat_.convoys().record(key);
 if(!lost&&(!recorded||!recorded->physical||clock_-recorded->lastPhysical>5.))for(const auto& [id,c]:campaigns_)if(!c.finished&&c.target==plan.destination&&clock_>=c.arrival){
  int64_t seized=0;
  if(c.player){for(auto& [fid,f]:fleet_)if(!f.lost&&!f.underway&&f.order==FleetOrder::Blockade&&f.destination==c.target){int64_t held=0;for(auto n:f.cargo)held+=n;const auto take=std::min<int64_t>({units,int64_t(f.radius)-held,6});if(take>0){f.cargo[size_t(plan.good)]+=take;f.contraband=true;units-=take;seized+=take;}}}
  else {const auto take=std::min<int64_t>(units,c.force*3);economy_.market(c.origin).stock[size_t(plan.good)]+=take;units-=take;seized=take;stations_.at(c.origin).dirty=true;}
  if(seized)report(plan.destination.system,"Blockade seized "+std::to_string(seized)+" "+std::string(xGoodInfo(plan.good).name)+" from an inbound shipment");break;
 }
 if(auto r=combat_.convoys_.records_.find(key);r!=combat_.convoys_.records_.end())r->second.remaining=lost?0:units;
 lot.settled=true;lot.lost=lost;lot.units=lost?0:units;
 const auto& p=combat_.convoys().plan(key);
 if(!lost){auto& m=economy_.market(p.destination);m.stock[size_t(p.good)]+=units;tax(p.destination,units*100);if(auto s=stations_.find(p.destination);s!=stations_.end())s->second.dirty=true;}
 if(lost||units<p.units/2)report(p.destination.system,"Shipment interrupted / "+economy_.placeName(p.origin)+" / "+std::string(xGoodInfo(p.good).name));
}
void XalariaStrategy::manifestChanged(ConvoyKey key,int64_t units){if(!shipments_.contains(key))allocate(key);if(auto it=shipments_.find(key);it!=shipments_.end()&&!it->second.settled)it->second.units=std::clamp<int64_t>(units,0,it->second.units);}
bool XalariaStrategy::escorted(ConvoyKey key)const{for(const auto& [id,f]:fleet_)if(!f.lost&&!f.captain.empty()&&f.underway&&f.order==FleetOrder::Escort&&f.escort==key&&clock_>=f.departure)return true;return false;}
void XalariaStrategy::noticeDelivery(ConvoyKey key,bool escort){const auto& p=combat_.convoys().plan(key);if(escort)trust_[size_t(p.faction)]=std::min(1000,trust_[size_t(p.faction)]+3);}
double XalariaStrategy::marketFactor(const XPlace& p,XGood good)const{
 const auto* s=station(p);if(!s)return 1.;const auto g=size_t(good);if(!s->market.target[g])return 1.;
 return std::clamp(1.+double(s->market.target[g]-s->market.stock[g])/std::max<int64_t>(1,s->market.target[g])*.32,.72,1.7);
}
void XalariaStrategy::launchCampaign(XPlace origin,XPlace target,XFaction faction,unsigned force,double seconds,uint32_t fleet){
 if(campaigns_.size()>=512||nextCampaign_>=0x01000000u)return;
 for(const auto& [id,c]:campaigns_)if(!c.finished&&c.target==target&&(c.player||(!fleet&&c.attacker==faction)))return;
 const auto path=route(origin.system,target.system);if(path.empty())return;
 FactionCampaign c;c.id=nextCampaign_++;c.origin=origin;c.target=target;c.attacker=faction;c.force=std::clamp(force,1u,4u);c.departure=seconds;c.arrival=seconds+std::max<size_t>(1,path.size()-1)*120.;c.player=fleet!=0;c.fleet=fleet;
 campaigns_.emplace(c.id,c);report(target.system,std::string(c.player?"Your fleet":"Faction expedition")+" blockading "+economy_.placeName(target));
}
void XalariaStrategy::tick(double at){
 const uint64_t turn=uint64_t(at/strategicTurn);
 for(auto& [place,s]:stations_){
  auto& q=s.market.stock;s.dirty=true;const auto consume=[&](XGood g,int64_t n){auto& stock=q[size_t(g)];const auto used=std::min(stock,n);stock-=used;return used;};
  const auto add=[&](XGood g,int64_t n){auto& stock=q[size_t(g)];stock=std::min<int64_t>(1000000,stock+n);};
  consume(XGood::Neurocells,1);if(turn%2==0)consume(XGood::Nickel,1);
  if(s.industry==0&&s.ore>=8&&q[size_t(XGood::Nanomites)]>0){consume(XGood::Nanomites,1);s.ore-=8;add(XGood::Iron,4);add(XGood::Nickel,3);add(XGood::Titanium,1);++s.production;}
  if(s.industry==1){add(XGood::Neurocells,4);if(q[size_t(XGood::Iron)]>0&&turn%3==0){consume(XGood::Iron,1);add(XGood::Seed,3);}++s.production;}
  if(s.industry==2&&q[1]>=3&&q[3]>=2&&q[5]>=1&&s.garrison<14){q[1]-=3;q[3]-=2;--q[5];++s.garrison;++s.production;}
  if(s.industry==3&&q[0]>=3&&q[2]>=1){q[0]-=3;--q[2];add(XGood::Nanomites,2);++s.production;}
  s.shortage=std::clamp((q[3]<8?.5:0.)+(q[11]<8?.5:0.),0.,1.);
  if(s.shortage>.9&&turn%3==0&&s.garrison>0)--s.garrison;
 }
 const auto& cv=combat_.convoys();const uint64_t cycle=uint64_t(at/convoyPeriod);
 for(const auto& [service,p]:cv.plans())for(unsigned back=0;back<3;++back)if(cycle>=back){
  ConvoyKey key{service,cycle-back};if(at<cv.departure(key)-30.)continue;
  allocate(key);const auto* record=cv.record(key);
  if(record&&(record->dead&1)){complete(key,0,true);continue;}
  if(record&&record->haltedAt>=0){if(record->remaining==0)complete(key,0,true);continue;}
  if(at<cv.arrival(key)||(record&&record->physical&&at-record->lastPhysical<5.))continue;
  const bool lost=record?record->remaining==0:(!escorted(key)&&mix64(service^mix64(key.cycle)^0x434f4e564f59ULL)%9==0);
  const auto lot=shipments_.find(key);if(lot!=shipments_.end())complete(key,record?record->remaining:lot->second.units,lost);
 }
 for(auto& [id,c]:campaigns_){
  if(c.finished||at<c.arrival)continue;auto target=stations_.find(c.target);if(target==stations_.end()){c.finished=true;continue;}auto& s=target->second;
  unsigned strength=c.force-std::min(c.force,unsigned(std::popcount(c.dead&15u)));
  if(c.player){auto ship=fleet_.find(c.fleet);if(ship==fleet_.end()||ship->second.lost||ship->second.order!=FleetOrder::Blockade){c.finished=true;continue;}strength=0;for(const auto& [fid,f]:fleet_)if(!f.lost&&!f.underway&&!f.captain.empty()&&f.order==FleetOrder::Blockade&&f.destination==c.target)strength+=(f.kind==ActivityKind::Patrol||f.kind==ActivityKind::Raider)?4:1;}
  if(!strength){c.finished=true;report(c.target.system,"Expedition defeated / "+economy_.placeName(c.target));continue;}
  if(s.owner==c.attacker&&!c.player){c.finished=true;continue;}
  auto& nickel=s.market.stock[size_t(XGood::Nickel)];nickel=std::max<int64_t>(0,nickel-strength*3);
  c.siege+=strategicTurn;
  if(at-c.physicalAt>10.){
   const unsigned support=nickel>=8&&s.market.stock[11]>=8?2:0;
   if(strength+unsigned(s.shortage*3.)>=s.garrison+support){if(s.garrison)--s.garrison;}
   else if(turn%2==0&&!c.player){for(unsigned m=0;m<c.force;++m)if(!(c.dead&(1u<<m))){c.dead|=1u<<m;break;}}
  }
  if(s.garrison==0&&c.siege>=strategicTurn*2){
   const auto old=s.owner;s.owner=c.attacker;s.playerOwned=c.player;s.garrison=std::max(1u,strength/2);s.dirty=true;c.finished=c.victory=true;
   report(c.target.system,economy_.placeName(c.target)+" surrendered / "+std::string(xFactionName(old))+" to "+(c.player?"your fleet":std::string(xFactionName(c.attacker))));
   trust_[size_t(old)]=std::max(-1000,trust_[size_t(old)]-(c.player?25:0));
   // Defeated station crews escape aboard a finite, physical lifeboat.
   if(wrecks_.size()<2048){auto lifeboat=campaignBody(c,7,at);lifeboat.name="Evacuation / "+economy_.placeName(c.target);lifeboat.kind=ActivityKind::Ferry;lifeboat.radius=14.;lifeboat.hull=0.;lifeboat.disabled=true;lifeboat.velocity={};wrecks_.try_emplace(lifeboat.id,StrategicWreck{lifeboat,0,3,at,knowledge_.contains(c.target.system)});}
   if(c.player){trust_[size_t(c.attacker)]=std::min(1000,trust_[size_t(c.attacker)]+5);fleet_.at(c.fleet).order=FleetOrder::Hold;}
  }else if(c.siege>7200.){c.finished=true;report(c.target.system,"Blockade withdrawn / supplies exhausted / "+economy_.placeName(c.target));}
 }
 // Each occupied region supplies its own seeded frontier candidate. Fleets
 // are committed from an actual garrison and cannot appear without industry.
 std::map<std::pair<int,int>,XPlace> frontier;
 for(const auto& [p,s]:stations_)if(p.station==0&&s.garrison>=5&&!s.playerOwned){const auto r=universe_.xalaria()->region(p.system);const auto bucket=std::pair(int(r.x),int(r.y));auto it=frontier.find(bucket);if(it==frontier.end()||mix64(portSeed(p)^turn)<mix64(portSeed(it->second)^turn))frontier[bucket]=p;}
 if(turn%3==0)for(const auto& [region,origin]:frontier){
  auto& home=stations_.at(origin);const auto& edges=universe_.xalaria()->star(origin.system).wormholes;
  for(auto next:edges)if(auto port=cv.port(next)){auto it=stations_.find(*port);if(it==stations_.end()||it->second.owner==home.owner||it->second.owner==XFaction::Independent)continue;
   const auto n=campaigns_.size();launchCampaign(origin,*port,home.owner,3,at);if(campaigns_.size()>n)home.garrison-=3;break;}
 }
 std::erase_if(campaigns_,[&](const auto& pair){return pair.second.finished&&at-pair.second.arrival>7200.;});
 std::erase_if(shipments_,[&](const auto& pair){return pair.second.settled&&pair.first.cycle+3<cycle;});
}
void XalariaStrategy::advance(const CombatPlayer& player,double dt){
 if(!std::isfinite(dt)||dt<=0)return;clock_=std::max(clock_,player.seconds+dt);const double now=clock_;
 if(economy_.dockedPlace())discover(economy_.dockedPlace()->system,"station traffic and departure board");
 else if(economy_.nearOrbit())discover(economy_.currentSystem(),"local navigation and radio traffic");
 for(const auto& a:economy_.assets())if(a.state==XAssetState::Active&&a.kind!=XAssetKind::Farm)addStation({a.system,a.world,100000+a.id,false});
 // Bound catch-up per frame without discarding turns; the saved cursor carries
 // the backlog, so splitting a flight into different frame sizes is equivalent.
 for(unsigned n=0;n<8&&turn_+strategicTurn<=now;++n){turn_+=strategicTurn;clock_=turn_;tick(turn_);}
 clock_=now;advanceFleet(player,dt);
 for(auto it=combat_.ships_.begin();it!=combat_.ships_.end();)if(it->second.hull<=0&&wrecks_.contains(it->first)&&clock_-wrecks_.at(it->first).created>12.)it=combat_.ships_.erase(it);else ++it;
}
void XalariaStrategy::tax(const XPlace& p,int64_t fee){auto it=stations_.find(p);if(it==stations_.end()||!it->second.playerOwned)return;auto& s=it->second;const auto take=std::clamp<int64_t>(fee,0,std::min<int64_t>(s.market.cash,1000000000000LL-s.treasury));s.market.cash-=take;s.treasury+=take;s.dirty=true;}
bool XalariaStrategy::collectTaxes(){if(!economy_.dockedPlace())return fail("Dock at a station you control to collect its fees.");auto it=stations_.find(*economy_.dockedPlace());if(it==stations_.end()||!it->second.playerOwned)return fail("This station is not under your control.");auto& s=it->second;const auto take=std::min<int64_t>(s.treasury,1000000000000000LL-economy_.credits_);if(!take)return fail("No collected trading fees are waiting.");s.treasury-=take;economy_.credits_+=take;message="Collected station trading fees / "+xMoney(take);return true;}
bool XalariaStrategy::scan(const CombatPlayer& p){
 bool found=discover(systemOf(p.frame),"active local scan");
 for(const auto& b:bodies(systemOf(p.frame),p.seconds)){auto at=universe_.pointRelative(b.id.home,b.position/std::pow(10.,universe_.describe(b.id.home).frame.logRadius),p.frame,0.);if(glm::distance(at,p.position)>100000.)continue;
  if(auto w=wrecks_.find(b.id);w!=wrecks_.end())w->second.known=true;
  if(isStrategyActor(b.id)&&(b.id.slot&0xf0000000u)==campaignActivityTag){const auto& c=campaigns_.at((b.id.slot&0x0fffffffu)>>3);found|=discover(c.origin.system,"intercepted expedition traffic");found|=discover(c.target.system,"intercepted blockade orders");}
 }
 message=found?"Local intelligence updated.":"Local traffic scanned. No new remote intelligence was revealed.";return found;
}
bool XalariaStrategy::informer(){
 if(!economy_.dockedPlace())return fail("Dock to meet a local informant.");if(economy_.credits_<20000)return fail("Informant fee: 200 GC.");
 const auto home=economy_.dockedPlace()->system;std::vector<uint32_t> candidates=universe_.xalaria()->star(home).wormholes;
 for(const auto& [id,c]:campaigns_)if((c.origin.system==home||c.target.system==home)&&!c.finished){candidates.push_back(c.origin.system);candidates.push_back(c.target.system);}
 for(auto id:candidates)if(!knowledge_.contains(id)){economy_.credits_-=20000;discover(id,"paid local informant");message="Route report purchased / "+universe_.xalaria()->star(id).name;return true;}
 return fail("The informant has no fresh report. No fee charged.");
}
bool XalariaStrategy::pledge(){if(!economy_.dockedPlace())return fail("Dock at the faction you want to support.");allegiance_=economy_.faction(*economy_.dockedPlace());trust_[size_t(allegiance_)]=std::max(1,trust_[size_t(allegiance_)]);message="Declared support / "+std::string(xFactionName(allegiance_));return true;}
std::string XalariaStrategy::advisory(const XPlace& p)const{const auto* s=station(p);if(!s)return {};return std::string(s->playerOwned?"YOUR CONTROL / ":"")+std::string(xFactionName(s->owner))+" / "+(s->shortage>.5?"CRITICAL SUPPLY SHORTAGE / ":"")+"GARRISON "+std::to_string(s->garrison);}
}
