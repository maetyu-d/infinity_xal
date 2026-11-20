#include "infinity/XalariaStrategy.hpp"
#include "infinity/XalariaCombat.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>
namespace infinity {
namespace {
void place(std::ostream& o,XPlace p){o<<p.system<<' '<<p.world<<' '<<p.station<<' '<<p.city;}
void id(std::ostream& o,const ActivityId& p){o<<std::quoted(p.str());}
void vector(std::ostream& o,glm::dvec3 p){o<<p.x<<' '<<p.y<<' '<<p.z;}
void rotation(std::ostream& o,glm::dquat q){o<<q.w<<' '<<q.x<<' '<<q.y<<' '<<q.z;}
}
std::string XalariaStrategy::serialize()const{
 std::ostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(17)<<"XALARIA_STRATEGY 1 "<<universe_.xalaria()->fingerprint<<' '<<clock_<<' '<<turn_<<' '<<nextFleet_<<' '<<nextCampaign_<<' '<<nextReport_<<' '<<unsigned(allegiance_);
 for(auto t:trust_)o<<' '<<t;o<<' '<<knowledge_.size();for(auto system:knowledge_)o<<' '<<system;
 const auto dirty=std::count_if(stations_.begin(),stations_.end(),[](const auto& p){return p.second.dirty;});o<<' '<<dirty;
 for(const auto& [p,s]:stations_)if(s.dirty){o<<' ';place(o,p);o<<' '<<unsigned(s.owner)<<' '<<s.garrison<<' '<<s.industry<<' '<<s.playerOwned<<' '<<s.treasury<<' '<<s.ore<<' '<<s.production<<' '<<s.shortage<<' '<<s.market.cash<<' '<<s.market.restockedDay;
  for(auto n:s.market.stock)o<<' '<<n;for(auto n:s.market.target)o<<' '<<n;
 }
 o<<' '<<shipments_.size();for(const auto& [k,s]:shipments_)o<<' '<<k.service<<' '<<k.cycle<<' '<<s.units<<' '<<s.settled<<' '<<s.lost;
 o<<' '<<campaigns_.size();for(const auto& [n,c]:campaigns_){o<<' '<<n<<' ';place(o,c.origin);o<<' ';place(o,c.target);o<<' '<<unsigned(c.attacker)<<' '<<c.force<<' '<<c.dead<<' '<<c.departure<<' '<<c.arrival<<' '<<c.physicalAt<<' '<<c.siege<<' '<<c.finished<<' '<<c.victory<<' '<<c.player<<' '<<c.fleet;}
 o<<' '<<fleet_.size();for(const auto& [n,f]:fleet_){o<<' '<<n<<' '<<f.seed<<' '<<bool(f.source);if(f.source){o<<' ';id(o,*f.source);}o<<' ';place(o,f.origin);o<<' ';place(o,f.destination);
  o<<' '<<std::quoted(f.name)<<' '<<std::quoted(f.captain)<<' '<<unsigned(f.kind)<<' '<<unsigned(f.order)<<' '<<f.departure<<' '<<f.arrival<<' '<<f.hull<<' '<<f.engines<<' '<<f.radius<<' '<<f.costBasis<<' '<<f.contraband<<' '<<f.underway<<' '<<f.lost<<' '<<f.launched<<' '<<f.loyalty<<' '<<f.missions<<' '<<std::quoted(f.frame.str())<<' ';vector(o,f.mooring);o<<' ';vector(o,f.velocity);o<<' ';rotation(o,f.axes);
  o<<' '<<bool(f.escort);if(f.escort)o<<' '<<f.escort->service<<' '<<f.escort->cycle;
  for(auto n:f.cargo)o<<' '<<n;o<<' '<<f.route.size();for(auto system:f.route)o<<' '<<system;
 }
 o<<' '<<wrecks_.size();for(const auto& [key,w]:wrecks_){o<<' ';id(o,key);o<<' '<<w.salvage<<' '<<w.survivors<<' '<<w.created<<' '<<w.known<<' '<<unsigned(w.body.kind)<<' '<<w.body.seed<<' '<<w.body.radius<<' '<<std::quoted(w.body.name)<<' ';vector(o,w.body.position);o<<' ';rotation(o,w.body.orientation);o<<' '<<w.body.tint.r<<' '<<w.body.tint.g<<' '<<w.body.tint.b;}
 o<<' '<<reports_.size();for(const auto& r:reports_)o<<' '<<r.id<<' '<<r.system<<' '<<r.seconds<<' '<<std::quoted(r.text);
 o<<' '<<recruits_.size();for(const auto& r:recruits_)o<<' '<<std::quoted(r);return o.str();
}
bool XalariaStrategy::restore(std::string_view saved,double seconds){
 if(!std::isfinite(seconds)||seconds<0)return false;
 if(saved.empty()){
  clock_=seconds;turn_=std::floor(seconds/strategicTurn)*strategicTurn;
  // Old sessions already contain finite lots aboard their recorded convoys.
  // Import those manifests once; never recreate them when their list opens.
  for(const auto& [key,r]:combat_.convoys().records())if(!r.delivered){shipments_[key]={r.remaining,r.haltedAt>=0,r.haltedAt>=0};if(r.haltedAt<0){const auto& p=combat_.convoys().plan(key);auto& m=economy_.market(p.origin);m.stock[size_t(p.good)]=std::max<int64_t>(0,m.stock[size_t(p.good)]-r.remaining);if(auto it=stations_.find(p.origin);it!=stations_.end())it->second.dirty=true;}}
  return true;
 }
 try{
  XalariaStrategy next(universe_,economy_,combat_);std::istringstream in{std::string(saved)};in.imbue(std::locale::classic());std::string tag,fingerprint;unsigned version,ally;size_t n;
  auto count=[&](size_t limit){return bool(in>>n)&&n<=limit;};
  auto number=[&](double& x,double lo,double hi){return bool(in>>x)&&std::isfinite(x)&&x>=lo&&x<=hi;};
  auto boolean=[&](bool& value){unsigned b;if(!(in>>b)||b>1)return false;value=b;return true;};
  auto text=[&](std::string& value,size_t limit){return bool(in>>std::quoted(value))&&value.size()<=limit;};
  auto readPlace=[&](XPlace& p){unsigned city;if(!(in>>p.system>>p.world>>p.station>>city)||city>1)return false;p.city=city;return !p.city&&economy_.validPlace(p);};
  auto readAddress=[&](Address& a){std::string s;if(!text(s,512))return false;a=Address::parse(s);universe_.describe(a);return true;};
  auto readId=[&](ActivityId& a){std::string value;if(!text(value,600))return false;auto split=value.find("@activity1:");if(split==value.npos)return false;a.home=Address::parse(value.substr(0,split));auto digits=std::string_view(value).substr(split+11);auto [end,error]=std::from_chars(digits.data(),digits.data()+digits.size(),a.slot);if(error!=std::errc{}||end!=digits.data()+digits.size())return false;universe_.describe(a.home);return isXalariaTraffic(a);};
  auto readVector=[&](glm::dvec3& v){return number(v.x,-1e20,1e20)&&number(v.y,-1e20,1e20)&&number(v.z,-1e20,1e20);};
  auto readRotation=[&](glm::dquat& q){return number(q.w,-1,1)&&number(q.x,-1,1)&&number(q.y,-1,1)&&number(q.z,-1,1)&&std::abs(glm::length(q)-1.)<1e-6;};
  if(!(in>>tag>>version>>fingerprint)||tag!="XALARIA_STRATEGY"||version!=1||fingerprint!=universe_.xalaria()->fingerprint||!number(next.clock_,0,seconds+.11)||!number(next.turn_,0,next.clock_)||
    !(in>>next.nextFleet_>>next.nextCampaign_>>next.nextReport_>>ally)||!next.nextFleet_||next.nextFleet_>1000000||!next.nextCampaign_||next.nextCampaign_>=0x01000000u||!next.nextReport_||ally>4)return false;
  if(std::abs(next.turn_/strategicTurn-std::round(next.turn_/strategicTurn))>1e-7)return false;next.allegiance_=XFaction(ally);
  for(auto& t:next.trust_)if(!(in>>t)||t< -1000||t>1000)return false;
  if(!count(universe_.xalaria()->stars.size()))return false;for(size_t i=0;i<n;++i){uint32_t id;if(!(in>>id)||!universe_.xalaria()->stars.contains(id)||!next.knowledge_.insert(id).second)return false;}
  if(!count(next.stations_.size()))return false;std::set<XPlace> seen;
  for(size_t i=0;i<n;++i){XPlace p;unsigned owner;if(!readPlace(p)||!seen.insert(p).second||!next.stations_.contains(p))return false;auto& s=next.stations_.at(p);
   if(!(in>>owner>>s.garrison>>s.industry)||owner>4||s.garrison>100||s.industry>3||!boolean(s.playerOwned)||!(in>>s.treasury>>s.ore>>s.production)||s.treasury<0||s.treasury>1000000000000LL||s.ore<0||s.ore>1000000||s.production>1000000000000ULL||!number(s.shortage,0,1)||!(in>>s.market.cash>>s.market.restockedDay)||s.market.cash<0||s.market.cash>1000000000000000LL||s.market.restockedDay<0)return false;
   for(auto& stock:s.market.stock)if(!(in>>stock)||stock<0||stock>1000000000LL)return false;
   for(auto& stock:s.market.target)if(!(in>>stock)||stock<0||stock>1000000)return false;
   s.owner=XFaction(owner);s.dirty=true;
  }
  if(!count(32768))return false;for(size_t i=0;i<n;++i){ConvoyKey key;StrategicShipment s;if(!(in>>key.service>>key.cycle>>s.units)||!combat_.convoys().plans().contains(key.service)||key.cycle>uint64_t(seconds/convoyPeriod)+1||s.units<0||s.units>combat_.convoys().plan(key).units||!boolean(s.settled)||!boolean(s.lost)||(s.lost&&!s.settled)||!next.shipments_.emplace(key,s).second)return false;}
  if(!count(512))return false;for(size_t i=0;i<n;++i){FactionCampaign c;unsigned faction;
   if(!(in>>c.id)||!c.id||c.id>=next.nextCampaign_||!readPlace(c.origin)||!readPlace(c.target)||!(in>>faction>>c.force>>c.dead)||faction>4||c.force<1||c.force>4||c.dead>255||!number(c.departure,0,seconds+.11)||!number(c.arrival,c.departure,seconds+86400)||!number(c.physicalAt,-100,seconds+.11)||!number(c.siege,0,86400)||!boolean(c.finished)||!boolean(c.victory)||!boolean(c.player)||!(in>>c.fleet)||(c.victory&&!c.finished)||c.player!=(c.fleet!=0))return false;
   c.attacker=XFaction(faction);if(next.route(c.origin.system,c.target.system).empty()||!next.campaigns_.emplace(c.id,c).second)return false;
  }
  if(!count(128))return false;std::set<ActivityId> sources;
  for(size_t i=0;i<n;++i){FleetShip f;bool source;unsigned kind,order;
   if(!(in>>f.id>>f.seed)||!f.id||f.id>=next.nextFleet_||!boolean(source))return false;
   if(source){ActivityId s;if(!readId(s)||(s.slot&0xf0000000u)==fleetActivityTag||!sources.insert(s).second)return false;f.source=s;}
   if(!readPlace(f.origin)||!readPlace(f.destination)||!text(f.name,120)||!text(f.captain,120)||!(in>>kind>>order)||kind>unsigned(ActivityKind::Raider)||order>unsigned(FleetOrder::Salvage)||!number(f.departure,0,seconds+86400)||!number(f.arrival,0,seconds+86400)||!number(f.hull,0,100)||!number(f.engines,0,100)||!number(f.radius,12,500)||!(in>>f.costBasis)||f.costBasis<0||f.costBasis>1000000000000000LL||!boolean(f.contraband)||!boolean(f.underway)||!boolean(f.lost)||!boolean(f.launched)||!(in>>f.loyalty>>f.missions)||f.loyalty>100||f.missions>1000000000||!readAddress(f.frame)||!readVector(f.mooring)||!readVector(f.velocity)||!readRotation(f.axes))return false;
   if(kind==unsigned(ActivityKind::Harbour)||kind==unsigned(ActivityKind::Debris)||kind==unsigned(ActivityKind::SurfacePort))return false;f.kind=ActivityKind(kind);f.order=FleetOrder(order);
   bool escort;if(!boolean(escort))return false;if(escort){ConvoyKey key;if(!(in>>key.service>>key.cycle)||!combat_.convoys().plans().contains(key.service)||key.cycle>uint64_t(seconds/convoyPeriod)+1||f.order!=FleetOrder::Escort)return false;f.escort=key;}
   for(auto& stock:f.cargo)if(!(in>>stock)||stock<0||stock>1000)return false;
   size_t length;if(!(in>>length)||length>universe_.xalaria()->stars.size())return false;
   for(size_t j=0;j<length;++j){uint32_t system;if(!(in>>system)||!universe_.xalaria()->stars.contains(system))return false;if(j){const auto& edges=universe_.xalaria()->star(f.route.back()).wormholes;if(std::find(edges.begin(),edges.end(),system)==edges.end())return false;}f.route.push_back(system);}
   if(f.underway&&(f.route.empty()||f.route.front()!=f.origin.system||f.route.back()!=f.destination.system||f.arrival<f.departure||f.captain.empty()))return false;
   if(!next.fleet_.emplace(f.id,std::move(f)).second)return false;
  }
  for(const auto& [id,c]:next.campaigns_)if(c.player&&!next.fleet_.contains(c.fleet))return false;
  if(!count(2048))return false;for(size_t i=0;i<n;++i){StrategicWreck w;ActivityId key;unsigned kind;
   if(!readId(key)||!(in>>w.salvage>>w.survivors)||w.salvage<0||w.salvage>10000||w.survivors>24||!number(w.created,0,seconds+.11)||!boolean(w.known)||!(in>>kind>>w.body.seed)||kind>unsigned(ActivityKind::Raider)||!number(w.body.radius,0.1,5000)||!text(w.body.name,120)||!readVector(w.body.position)||!readRotation(w.body.orientation))return false;
   for(unsigned j=0;j<3;++j){double tint;if(!number(tint,0,10))return false;w.body.tint[j]=float(tint);}w.body.id=key;w.body.kind=ActivityKind(kind);w.body.hull=0;w.body.disabled=true;
   if(!next.wrecks_.emplace(key,std::move(w)).second)return false;
  }
  if(!count(1024))return false;uint64_t previous=0;for(size_t i=0;i<n;++i){StrategicReport r;if(!(in>>r.id>>r.system)||r.id<=previous||r.id>=next.nextReport_||!universe_.xalaria()->stars.contains(r.system)||!number(r.seconds,0,seconds+.11)||!text(r.text,1000))return false;previous=r.id;next.reports_.push_back(std::move(r));}
  if(!count(24))return false;for(size_t i=0;i<n;++i){std::string r;if(!text(r,120)||r.empty())return false;next.recruits_.push_back(std::move(r));}
  in>>std::ws;if(!in.eof())return false;
  stations_=std::move(next.stations_);shipments_=std::move(next.shipments_);fleet_=std::move(next.fleet_);campaigns_=std::move(next.campaigns_);wrecks_=std::move(next.wrecks_);knowledge_=std::move(next.knowledge_);reports_=std::move(next.reports_);recruits_=std::move(next.recruits_);trust_=next.trust_;allegiance_=next.allegiance_;clock_=next.clock_;turn_=next.turn_;nextFleet_=next.nextFleet_;nextCampaign_=next.nextCampaign_;nextReport_=next.nextReport_;adoptedMarkets_=true;row=0;return true;
 }catch(...){return false;}
}
}
