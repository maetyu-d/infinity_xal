#include "infinity/XalariaEconomy.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <numbers>
#include <sstream>
#include <stdexcept>
namespace infinity {
namespace {
constexpr int64_t moneyLimit=1000000000000000LL;
constexpr std::array<XGoodInfo,xGoodCount> goods{{
 {"ALUMINIUM",4000,true},{"IRON",5000,true},{"TIN",6500,false},{"NICKEL",12000,true},
 {"CHROMIUM",18000,true},{"TITANIUM",32000,true},{"IRIDIUM",280000,false},
 {"MARBLE",22000,false},{"GRANITE",16000,false},{"CONSTRUCTION NANOMITES",200000,false},
 {"SEED",2800,false},{"QUANTUM NEUROCELLS",500000,false}}};
constexpr std::array<int64_t,13> levels{0,10,20,50,100,200,500,1000,2000,5000,10000,20000,50000};
unsigned level(int64_t xp){unsigned n=1;while(n<13&&xp>=levels[n])++n;return n;}
uint64_t key(const XPlace& p){return mix64(uint64_t(p.system)^(p.world*0x9e3779b97f4a7c15ULL)^(uint64_t(p.station)<<32)^(p.city?0x43495459ULL:0x53544154ULL));}
int64_t interpolate(unsigned a,const std::array<std::pair<unsigned,int64_t>,7>& table){
 if(a<1||a>22)throw std::invalid_argument("Advancement must be 1 to 22");
 for(size_t i=1;i<table.size();++i)if(a<=table[i].first){
  const auto [x,v]=table[i-1];const auto [y,w]=table[i];return int64_t(std::ceil(v*std::pow(double(w)/v,double(a-x)/(y-x))-1e-8));
 }
 return table.back().second;
}
Address planetOf(Address a){while(a.kind()>Kind::Planet)a=a.parent();return a;}
unsigned bitCount(unsigned n){return std::popcount(n&7u);}
void writePlace(std::ostream& s,const XPlace& p){s<<p.system<<' '<<p.world<<' '<<p.station<<' '<<p.city;}
bool readPlace(std::istream& s,XPlace& p){return bool(s>>p.system>>p.world>>p.station>>p.city);}
}
const XGoodInfo& xGoodInfo(XGood g){return goods.at(size_t(g));}
std::string xMoney(int64_t cents){std::ostringstream s;s.imbue(std::locale::classic());s<<cents/100;if(cents%100)s<<'.'<<std::setw(2)<<std::setfill('0')<<std::abs(cents%100);return s.str()+" GC";}
std::string_view xFactionName(XFaction f){constexpr std::array<std::string_view,5> names{"TRADE AND MANUFACTURING","NATURE PRESERVATION","FREEDOM FIGHTERS","REBEL COALITION","INDEPENDENT"};return names.at(size_t(f));}
XalariaEconomy::XalariaEconomy(const Universe& u):universe_(u),catalog_(*u.xalaria()){
 view.place={14940,2,0,false};if(!validPlace(view.place)){const auto& s=catalog_.stars.begin()->second;view.place.system=s.seed;view.place.world=s.worlds.begin()->first;}
}
unsigned XalariaEconomy::technology() const{return level(technologyXP_);}
unsigned XalariaEconomy::culture() const{return level(cultureXP_);}
int64_t XalariaEconomy::cargoUnits() const{
 int64_t n=artwork_.size();for(auto q:cargo_)n+=q;
 for(const auto& c:contracts_)if(c.state==XContract::State::Active)n+=c.units;return n;
}
unsigned XalariaEconomy::aesthetics(uint32_t s,uint64_t w) const{auto it=known_.find({s,w});return it==known_.end()?0:bitCount(it->second);}
bool XalariaEconomy::failure(std::string message){view.message=std::move(message);return false;}
double XalariaEconomy::random(){return Random{0x5845434f4e4f4d59ULL}.unit(++randomCounter_);}
void XalariaEconomy::spent(int64_t n){tradeSpend_=std::min(moneyLimit,tradeSpend_+n);}
XAsset* XalariaEconomy::asset(uint32_t id){for(auto& a:assets_)if(a.id==id)return &a;return nullptr;}
std::vector<XPlace> XalariaEconomy::venues(uint32_t id,uint64_t wid) const{
 const auto& w=catalog_.star(id).worlds.at(wid);std::vector<XPlace> out;
 for(unsigned n=0;n<w.stations;++n)out.push_back({id,wid,n,false});
 for(const auto& a:assets_)if(a.system==id&&a.world==wid&&a.kind!=XAssetKind::Farm&&a.state==XAssetState::Active)out.push_back({id,wid,100000+a.id,false});
 if(w.communities)out.push_back({id,wid,0,true});return out;
}
bool XalariaEconomy::validPlace(const XPlace& p) const{
 auto s=catalog_.stars.find(p.system);if(s==catalog_.stars.end())return false;
 auto w=s->second.worlds.find(p.world);if(w==s->second.worlds.end())return false;
 if(p.city)return w->second.communities>0;
 if(p.station<100000)return p.station<w->second.stations;
 for(const auto& a:assets_)if(a.id==p.station-100000&&a.system==p.system&&a.world==p.world&&a.kind!=XAssetKind::Farm&&a.state==XAssetState::Active)return true;
 return false;
}
std::string XalariaEconomy::placeName(const XPlace& p) const{
 const auto& w=catalog_.star(p.system).worlds.at(p.world);
 if(p.city)return w.name+" EXCHANGE";
 if(p.station>=100000)return w.name+" / YOUR "+std::string([&](){for(const auto& a:assets_)if(a.id==p.station-100000)return a.kind==XAssetKind::Vault?"VAULT":"STATION";return "STATION";}());
 return w.name+" STATION "+std::to_string(p.station+1);
}
XFaction XalariaEconomy::faction(const XPlace& p) const{
 if(p.station>=100000)for(const auto& a:assets_)if(a.id==p.station-100000)return a.tradeFlag?XFaction::Trade:XFaction::Independent;
 const auto r=mix64(key(p))%20;return r<7?XFaction::Trade:r<12?XFaction::Nature:r<16?XFaction::Freedom:r<18?XFaction::Rebel:XFaction::Independent;
}
bool XalariaEconomy::canTrade(const XPlace& p) const{
 if(!validPlace(p))return false;
 if(p.city)return landed_&&currentSystem_==p.system&&currentWorld_==p.world;
 return docked_&&*docked_==p;
}
void XalariaEconomy::show(uint32_t s,uint64_t w){
 catalog_.star(s).worlds.at(w);auto list=venues(s,w);view.place=list.empty()?XPlace{s,w,0,false}:list.front();view.scroll=0;view.open=true;
}
void XalariaEconomy::cycleVenue(){
 auto list=venues(view.place.system,view.place.world);if(list.empty()){failure("NO STATION OR CITY MARKET ON THIS WORLD");return;}
 auto it=std::find(list.begin(),list.end(),view.place);view.place=it==list.end()?list.front():list[(size_t(it-list.begin())+1)%list.size()];
}
XMarket& XalariaEconomy::market(const XPlace& p) const{
 if(!validPlace(p))throw std::invalid_argument("No market at this location");
 auto [it,created]=markets_.try_emplace(p);auto& m=it->second;Random r{key(p)};
 if(created){for(size_t g=0;g<xGoodCount;++g){
  bool available=true;
  if(XGood(g)==XGood::Nanomites&&(p.city||p.station>=100000))available=false;
  if(XGood(g)==XGood::Seed&&!p.city)available=false;
  if(faction(p)==XFaction::Nature&&(g<=size_t(XGood::Iridium)))available=r.unit(30+g)>.5;
  m.target[g]=available?int64_t(80+r.bits(10+g)%121):0;m.stock[g]=m.target[g];
 }m.restockedDay=day();}
 if(day()>m.restockedDay){
  const auto days=std::min<int64_t>(36500,day()-m.restockedDay);
  for(size_t g=0;g<xGoodCount;++g){const auto replenishment=std::min<int64_t>(m.target[g],days*std::max<int64_t>(1,m.target[g]/3));m.stock[g]=std::min(m.target[g]*3,m.stock[g]+replenishment);}
  m.restockedDay=day();
 }return m;
}
int64_t XalariaEconomy::midpoint(const XPlace& p,XGood good,int64_t amount) const{
 const size_t g=size_t(good);const auto& m=market(p);Random r{mix64(p.system^0x505249434553ULL)};
 double multiplier=.70+.70*r.unit(g);const size_t rich=size_t(mix64(p.system)%9);
 if(g==rich)multiplier*=.60+.15*r.unit(50+g);
 double scarcity=1+double(m.target[g]-amount)/std::max<int64_t>(1,m.target[g])*.18;
 double trust=p.city?std::max(.55,1-.15*aesthetics(p.system,p.world)):1.;
 return std::max<int64_t>(1,int64_t(std::llround(goods[g].base*multiplier*std::clamp(scarcity,.55,1.8)*trust)));
}
int64_t XalariaEconomy::stock(const XPlace& p,XGood g) const{return validPlace(p)?market(p).stock[size_t(g)]:0;}
XQuote XalariaEconomy::quote(const XPlace& p,XGood good,int64_t units,bool buying) const{
 XQuote q;q.units=units;if(!validPlace(p)||size_t(good)>=xGoodCount||units<=0||units>10000)return q;
 const auto& m=market(p);const size_t g=size_t(good);
 if(buying&&m.stock[g]<units)return q;
 if(!buying&&(m.stock[g]+units>m.target[g]*3+1000))return q;
 if(good==XGood::Nanomites&&p.station>=100000)return q;
 for(int64_t i=0;i<units;++i){
  const auto mid=midpoint(p,good,m.stock[g]+(buying?-i:i+1));
  const auto unit=buying?(mid*105+99)/100:mid*95/100;q.total+=unit;
 }
 q.fee=goodwill()>50?0:(q.total*2+99)/100;
 if(buying)q.total+=q.fee;else q.total=std::max<int64_t>(0,q.total-q.fee);
 q.available=true;return q;
}
bool XalariaEconomy::buy(XGood good,int64_t count){
 if(!canTrade(view.place))return failure("DOCK HERE, OR LAND AT THIS CITY, TO TRADE");
 const auto q=quote(view.place,good,count,true);
 if(!q.available)return failure("THE MARKET CANNOT SUPPLY THAT QUANTITY");
 if(cargoUnits()+count>capacity_)return failure("NOT ENOUGH ROOM IN THE HOLD");
 if(q.total>credits_)return failure("NOT ENOUGH GALACTIC CREDITS");
 credits_-=q.total;market(view.place).cash+=q.total;market(view.place).stock[size_t(good)]-=count;cargo_[size_t(good)]+=count;spent(q.total);
 view.message="BOUGHT "+std::to_string(count)+" "+std::string(goods[size_t(good)].name)+" / "+xMoney(q.total);return true;
}
bool XalariaEconomy::sell(XGood good,int64_t count){
 if(!canTrade(view.place))return failure("YOU MUST BE AT THIS MARKET TO SELL");
 if(count<=0||count>10000||size_t(good)>=xGoodCount||cargo_[size_t(good)]<count)return failure("THAT CARGO IS NOT IN YOUR HOLD");
 const auto q=quote(view.place,good,count,false);if(!q.available||market(view.place).cash<q.total||credits_>moneyLimit-q.total)return failure("THE MARKET CANNOT TAKE THAT SALE");
 credits_+=q.total;market(view.place).cash-=q.total;market(view.place).stock[size_t(good)]+=count;cargo_[size_t(good)]-=count;
 view.message="SOLD "+std::to_string(count)+" "+std::string(goods[size_t(good)].name)+" / "+xMoney(q.total);return true;
}
bool XalariaEconomy::refuel(){
 if(!canTrade(view.place))return failure("DOCK OR VISIT A CITY FUEL STATION FIRST");
 const double missing=fuelCapacity_-fuel_;if(missing<.001)return failure("THE TANK IS FULL");
 int64_t unit=250;if(view.place.city)unit*=2;if(view.place.station>=100000)for(const auto& a:assets_)if(a.id==view.place.station-100000&&a.kind==XAssetKind::Vault){if(!a.fuelStation)return failure("THIS VAULT HAS NO FUEL STATION");unit/=2;}
 const auto cost=int64_t(std::ceil(missing*unit));if(credits_<cost)return failure("NOT ENOUGH CREDITS FOR FUEL");
 credits_-=cost;market(view.place).cash+=cost;fuel_=fuelCapacity_;view.message="TANK FILLED / "+xMoney(cost);return true;
}
bool XalariaEconomy::buyTool(bool precision){
 if(!canTrade(view.place)||view.place.city)return failure("BUY MINING TOOLS AT A STATION");
 if(faction(view.place)==XFaction::Nature)return failure("NATURE STATIONS DO NOT SELL MINING TOOLS");
 const auto cost=precision?600000:150000;if((precision?precision_:tool_))return failure("THAT MINING TOOL IS ALREADY FITTED");
 if(precision&&catalog_.star(view.place.system).technology<7)return failure("A PRECISION TOOL NEEDS A TECHNOLOGICAL STATION");
 if(credits_<cost)return failure("NOT ENOUGH CREDITS FOR THAT TOOL");
 credits_-=cost;market(view.place).cash+=cost;tool_=true;precision_=precision_||precision;spent(cost);view.message=precision?"PRECISION MINING TOOL FITTED":"TREMBLE MINING TOOL FITTED";return true;
}
bool XalariaEconomy::buyFillingTool(){
 if(!canTrade(view.place)||view.place.city||faction(view.place)!=XFaction::Nature)return failure("NATURE STATIONS SELL THE MINE FILLING TOOL");
 if(fillingTool_)return failure("MINE FILLING TOOL ALREADY FITTED");
 if(credits_<10000)return failure("THE MINE FILLING TOOL COSTS 100 GC");
 credits_-=10000;market(view.place).cash+=10000;spent(10000);fillingTool_=true;view.message="MINE FILLING TOOL FITTED";return true;
}
bool XalariaEconomy::upgradeHold(){
 constexpr std::array<unsigned,7> hold{30,60,120,200,300,450,600};constexpr std::array<int64_t,7> prices{1000000,2000000,4000000,8000000,16000000,32000000,64000000};
 if(!canTrade(view.place)||view.place.city)return failure("VISIT A STATION SHIPYARD");
 if(shipTier_>=6)return failure("THE BELUGA HOLD IS THE LARGEST");
 const auto price=prices[shipTier_+1]-prices[shipTier_]/2;
 if(credits_<price)return failure("SHIP EXCHANGE NEEDS "+xMoney(price));
 credits_-=price;market(view.place).cash+=price;spent(price);++shipTier_;capacity_=hold[shipTier_];
 constexpr std::array<double,7> tanks{80,100,120,160,200,280,400};fuelCapacity_=tanks[shipTier_];
 view.message="SHIP EXCHANGED / HOLD "+std::to_string(capacity_)+" / FUEL "+std::to_string(int(fuelCapacity_));return true;
}
bool XalariaEconomy::buyArtwork(){
 if(!canTrade(view.place))return failure("VISIT THE LOCAL ORNAMENT SHOP");
 const auto price=int64_t(100000+(mix64(key(view.place)^nextArt_)%8)*100000);
 if(credits_<price||cargoUnits()>=capacity_)return failure("NOT ENOUGH CREDITS OR HOLD SPACE");
 credits_-=price;market(view.place).cash+=price;spent(price);artwork_.push_back({nextArt_++,price,day()});view.message="AUTHENTIC ORNAMENT BOUGHT / "+xMoney(price);return true;
}
bool XalariaEconomy::studyCulture(){
 const auto& p=view.place;const auto& w=catalog_.star(p.system).worlds.at(p.world);
 if(!p.city||!canTrade(p))return failure("LAND AND VISIT THIS CITY'S CULTURAL CENTRE");
 auto& known=known_[{p.system,p.world}];if(known==7)return failure("YOU ALREADY KNOW ALL THREE AESTHETICS");
 const unsigned adv=std::min(20u,catalog_.star(p.system).technology);
 const unsigned fav=unsigned(mix64(w.uniqueId)%3);
 unsigned choice=3,required=99;
 for(unsigned n=0;n<3;++n)if(!(known&(1u<<n))){
  const double base=adv<=5?3+(adv-1)*.75:adv<=10?6+(adv-5)*.8:adv<=15?10+(adv-10)*.8:14+(adv-15)*.4;
  unsigned ask=std::min(16u,unsigned(std::round(base))+unsigned(mix64(w.uniqueId+n)%4));
  if(n==fav)ask=ask>10?ask-10:1;
  if(ask<required){choice=n;required=ask;}}
 const auto ticket=int64_t(std::llround(50000*std::pow(4./3,std::min(19u,adv-1))));
 if(credits_<ticket)return failure("A CULTURAL VISIT COSTS "+xMoney(ticket));
 credits_-=ticket;market(p).cash+=ticket;
 if(culture()<required)return failure("THE ART STAYS UNFAMILIAR / RETURN WITH MORE CULTURE EXPERIENCE");
 known|=1u<<choice;cultureXP_=std::min<int64_t>(50000,cultureXP_+ticket/1000);
 view.message="LOCAL "+std::string(choice==0?"MUSIC":choice==1?"ART":"DANCE")+" UNDERSTOOD / CITY PRICES IMPROVE";return true;
}
int64_t XalariaEconomy::stationNanomites(unsigned a){return interpolate(a,{{{1,14},{4,26},{8,63},{12,154},{16,372},{20,900},{22,1400}}});}
int64_t XalariaEconomy::stationIncome(unsigned a){return interpolate(a,{{{1,15000},{4,42000},{8,170000},{12,680000},{16,2700000},{20,10900000},{22,21700000}}});}
unsigned XalariaEconomy::constructionChance(unsigned a,unsigned tech,bool vault){
 if(a<1||a>22||tech<1||tech>13)return 0;const int extra=int(tech)+(vault?2:0)-int((a+1)/2);
 constexpr std::array<unsigned,7> chances{25,50,75,90,97,99,100};return extra<0?0:chances[std::min(extra,6)];
}
bool XalariaEconomy::atAsset(const XAsset& a,bool dock) const{
 if(currentSystem_!=a.system||currentWorld_!=a.world)return false;
 if(dock)return docked_&&docked_->station==100000+a.id&&!docked_->city;
 return a.kind==XAssetKind::Farm?landed_:nearOrbit_;
}
bool XalariaEconomy::seedAsset(XAssetKind kind,unsigned adv){
 if(currentSystem_!=view.place.system||currentWorld_!=view.place.world)return failure("VISIT THE SELECTED WORLD TO BUILD THERE");
 if(!currentWorld_)return failure("APPROACH A WORLD BEFORE BUILDING");
 const auto& w=catalog_.star(currentSystem_).worlds.at(*currentWorld_);
 if(assets_.size()>=128)return failure("THE HOLDINGS REGISTER IS FULL");
 if(kind==XAssetKind::Farm){
  if(!landed_||w.composition==0||w.temperature>=1100)return failure("A FARM NEEDS A SOLID, NON-MOLTEN SURFACE");
  for(const auto& a:assets_)if(a.kind==kind&&a.state==XAssetState::Frame)return failure("FINISH YOUR OTHER UNSOWN FIELD FIRST");
  const unsigned grade=w.temperature>330||w.temperature<240?3:unsigned(mix64(w.uniqueId)%3);
  XAsset a;a.id=uint32_t(nextAsset_++);a.kind=kind;a.system=currentSystem_;a.world=*currentWorld_;a.seedNeeded=60+30*grade;a.lastFed=int64_t(seconds_);a.lastPaidDay=day();assets_.push_back(a);
  view.message="FIELD CLEARED / NEEDS "+std::to_string(a.seedNeeded)+" SEED";return true;
 }
 if(!nearOrbit_||landed_)return failure("BUILD STATIONS AND VAULTS IN ORBIT");
 if(kind==XAssetKind::Station&&(technology()<4||adv>22||adv<1))return failure("STATION BUILDERS NEED TECHNOLOGY LEVEL 4");
 size_t orbitStations=w.stations;
 for(const auto& a:assets_)if(a.system==currentSystem_&&a.world==*currentWorld_&&a.state!=XAssetState::Lost){
  if(kind==XAssetKind::Vault&&a.kind==kind)return failure("YOU ALREADY KEEP A VAULT IN THIS ORBIT");
  if(a.kind==XAssetKind::Station)++orbitStations;
 }
 if(kind==XAssetKind::Station&&orbitStations>=3)return failure("THIS ORBIT ALREADY HAS THREE STATIONS OR FRAMES");
 const unsigned a=kind==XAssetKind::Vault?2:adv;const auto chance=constructionChance(a,technology(),kind==XAssetKind::Vault);
 if(!chance)return failure("THAT ADVANCEMENT IS BEYOND YOUR TECHNOLOGY");
 const int64_t seed=kind==XAssetKind::Vault?7:5+a;
 if(cargo_[size_t(XGood::Nanomites)]<seed||energy_<12)return failure("NEED "+std::to_string(seed)+" NANOMITES AND 60% ENERGY");
 cargo_[size_t(XGood::Nanomites)]-=seed;energy_-=12;
 if(random()*100>=chance){cargo_[size_t(XGood::Nanomites)]+=seed/2;return failure("THE FRAME BUCKLED / HALF THE SEED NANOMITES RETURNED");}
 XAsset asset;asset.id=uint32_t(nextAsset_++);asset.kind=kind;asset.system=currentSystem_;asset.world=*currentWorld_;asset.level=a;
 asset.nanoNeeded=kind==XAssetKind::Vault?0:std::max<int64_t>(0,stationNanomites(a)-seed);asset.metalNeeded=20*a;asset.lastFed=int64_t(seconds_);asset.lastPaidDay=day();assets_.push_back(asset);
 view.message="FRAME SEEDED / FEED ITS REMAINING NANOMITES AND STRUCTURAL METAL";return true;
}
bool XalariaEconomy::feedAsset(uint32_t id){
 auto* a=asset(id);if(!a||a->state!=XAssetState::Frame||!atAsset(*a))return failure("VISIT YOUR UNFINISHED HOLDING TO FEED IT");
 int64_t delivered=0;
 if(a->kind==XAssetKind::Farm){auto& held=cargo_[size_t(XGood::Seed)];const auto n=std::min(held,a->seedNeeded);a->seedNeeded-=n;held-=n;delivered+=n;}
 else{
  auto& nano=cargo_[size_t(XGood::Nanomites)];const auto n=std::min(nano,a->nanoNeeded);a->nanoNeeded-=n;nano-=n;delivered+=n;
  for(size_t g=0;g<xGoodCount;++g)if(goods[g].structural){const auto q=std::min(cargo_[g],a->metalNeeded);cargo_[g]-=q;a->metalNeeded-=q;delivered+=q;}
 }
 if(!delivered)return failure("NONE OF THE NEEDED MATERIALS ARE IN THE HOLD");
 a->lastFed=int64_t(seconds_);
 if(!a->nanoNeeded&&!a->metalNeeded&&!a->seedNeeded){
  const auto& w=catalog_.star(a->system).worlds.at(a->world);
  const unsigned grade=w.temperature>330||w.temperature<240?3:unsigned(mix64(w.uniqueId)%3);
  a->state=XAssetState::Active;a->lastPaidDay=day();spent(a->kind==XAssetKind::Farm?int64_t(60+grade*30)*2800:(a->kind==XAssetKind::Vault?7:stationNanomites(a->level))*200000);
  view.message="YOUR "+std::string(a->kind==XAssetKind::Farm?"FARM":a->kind==XAssetKind::Vault?"VAULT":"STATION")+" IS COMPLETE";
 }else view.message="MATERIALS DELIVERED / "+std::to_string(a->nanoNeeded)+" NANOMITES / "+std::to_string(a->metalNeeded)+" METAL / "+std::to_string(a->seedNeeded)+" SEED LEFT";
 return true;
}
int64_t XalariaEconomy::dailyIncome(const XAsset& a) const{
 if(a.state!=XAssetState::Active)return 0;
 if(a.kind==XAssetKind::Station)return stationIncome(a.level)*(a.tradeFlag?.5:1.);
 if(a.kind==XAssetKind::Vault)return a.fuelStation?20000:0;
 if(a.guards)return int64_t((35+141.*culture()/13)*(1+.1*aesthetics(a.system,a.world))*100);
 return int64_t((34+76.*technology()/13)*100);
}
bool XalariaEconomy::collect(uint32_t id){
 auto* a=asset(id);if(!a||a->state!=XAssetState::Active||!atAsset(*a,a->kind!=XAssetKind::Farm))return failure("VISIT YOUR FARM OR DOCK AT THIS HOLDING TO COLLECT");
 const auto total=std::min(moneyLimit-credits_,a->till+a->culturalTill);credits_+=total;a->till=0;a->culturalTill=0;view.message="COLLECTED "+xMoney(total);return true;
}
bool XalariaEconomy::guard(uint32_t id){
 auto* a=asset(id);if(!a||a->state!=XAssetState::Active||!atAsset(*a))return failure("VISIT YOUR HOLDING TO ARRANGE GUARDS");
 uint64_t city=a->world;
 if(!catalog_.star(a->system).worlds.at(city).communities){
  if(a->kind==XAssetKind::Farm)return failure("A FARM'S GUARDS MUST COME FROM ITS OWN WORLD");
  auto it=std::find_if(catalog_.star(a->system).worlds.begin(),catalog_.star(a->system).worlds.end(),[&](const auto& p){return p.second.communities&&aesthetics(a->system,p.first);});
  if(it==catalog_.star(a->system).worlds.end())return failure("LEARN A LOCAL CITY'S CULTURE BEFORE ASKING FOR GUARDS");city=it->first;
 }
 const unsigned understood=aesthetics(a->system,city);if(!understood)return failure("UNDERSTAND ONE OF THE CITY'S AESTHETICS FIRST");
 for(const auto& other:assets_)if(other.id!=a->id&&other.system==a->system&&other.guardCity==city&&other.guards&&other.state==XAssetState::Active)return failure("THIS CITY'S GUARDS ALREADY SERVE ANOTHER HOLDING");
 if(a->guards)return failure("THIS HOLDING ALREADY HAS ITS GUARDS");
 const int64_t cost=int64_t(300000*(1-.15*understood)*(city==a->world?1:3));if(credits_<cost)return failure("GUARDS COST "+xMoney(cost));
 credits_-=cost;a->guardCity=city;a->guards=2+unsigned(mix64(city)%8);a->lasers=a->kind==XAssetKind::Farm?0:catalog_.star(a->system).technology>=14?2:catalog_.star(a->system).technology>=7?1:0;
 view.message="CITY GUARDS HIRED / "+std::to_string(a->guards)+" GUARDS";return true;
}
bool XalariaEconomy::flag(uint32_t id){
 auto* a=asset(id);if(!a||a->kind!=XAssetKind::Station||!atAsset(*a,true))return failure("DOCK AT YOUR STATION TO CHANGE ITS FLAG");
 if(!a->tradeFlag&&goodwill()<=50)return failure("TRADE MEMBERSHIP NEEDS MORE THAN 50 GOODWILL");
 a->tradeFlag=!a->tradeFlag;view.message=a->tradeFlag?"TRADE FLAG / FLEET PROTECTION / HALF THE TILL":"INDEPENDENT FLAG / WHOLE TILL / ARRANGE GUARDS";return true;
}
bool XalariaEconomy::buildHall(uint32_t id,bool gallery){
 auto* a=asset(id);if(!a||a->kind!=XAssetKind::Station||a->state!=XAssetState::Active||!atAsset(*a,true))return failure("DOCK AT YOUR STATION TO BUILD CULTURAL HALLS");
 if(gallery?a->gallery:a->hall)return failure("THAT HALL ALREADY STANDS");
 const auto stone=gallery?XGood::Marble:XGood::Granite;
 if(cargo_[size_t(XGood::Nanomites)]<5||cargo_[size_t(stone)]<40)return failure("NEED 5 NANOMITES AND 40 "+std::string(xGoodInfo(stone).name));
 cargo_[size_t(XGood::Nanomites)]-=5;cargo_[size_t(stone)]-=40;if(gallery)a->gallery=true;else a->hall=true;
 view.message=gallery?"GALLERY BUILT / DONATE THREE AUTHENTIC PIECES TO OPEN":"CONCERT HALL OPEN / INCOME WAITS IN ITS OWN TILL";return true;
}
bool XalariaEconomy::donateArtwork(uint32_t id){
 auto* a=asset(id);if(!a||!a->gallery||!atAsset(*a,true))return failure("DOCK AT YOUR GALLERY TO DONATE");
 if(artwork_.empty())return failure("BUY AN AUTHENTIC ORNAMENT FIRST");
 auto piece=artwork_.back();artwork_.pop_back();piece.placedDay=day();a->artwork.push_back(piece);
 view.message="ORNAMENT DONATED / "+std::to_string(a->artwork.size())+" PIECES ON SHOW";return true;
}
bool XalariaEconomy::buildFuelStation(uint32_t id){
 auto* a=asset(id);if(!a||a->kind!=XAssetKind::Vault||!atAsset(*a,true))return failure("DOCK AT YOUR VAULT");
 if(a->fuelStation)return failure("FUEL STATION ALREADY OPEN");
 const bool city=std::any_of(catalog_.star(a->system).worlds.begin(),catalog_.star(a->system).worlds.end(),[](const auto& w){return w.second.communities>0;});
 if(!city||cargo_[size_t(XGood::Nanomites)]<3)return failure("NEED A CITY SUPPLIER AND 3 NANOMITES");
 cargo_[size_t(XGood::Nanomites)]-=3;a->fuelStation=true;view.message="VAULT FUEL STATION OPEN / HALF-PRICE FUEL";return true;
}
bool XalariaEconomy::store(uint32_t id,XGood good,int64_t units,bool retrieve){
 auto* a=asset(id);if(!a||a->kind!=XAssetKind::Vault||!atAsset(*a,true)||units<=0||units>10000||size_t(good)>=xGoodCount)return failure("DOCK AT YOUR VAULT TO MOVE CARGO");
 auto& from=retrieve?a->stored[size_t(good)]:cargo_[size_t(good)];auto& to=retrieve?cargo_[size_t(good)]:a->stored[size_t(good)];
 if(from<units||(retrieve&&cargoUnits()+units>capacity_)||to>1000000-units)return failure("NOT ENOUGH CARGO OR STORAGE ROOM");
 from-=units;to+=units;view.message=retrieve?"CARGO RETRIEVED":"CARGO STORED IN YOUR PRIVATE VAULT";return true;
}
bool XalariaEconomy::survey(){
 if(currentSystem_!=view.place.system||currentWorld_!=view.place.world)return failure("VISIT THE SELECTED WORLD TO SURVEY IT");
 if(!currentWorld_)return failure("APPROACH A WORLD FIRST");
 const auto pair=std::pair{currentSystem_,*currentWorld_};const auto& w=catalog_.star(currentSystem_).worlds.at(*currentWorld_);
 if(!landed_)return failure("LAND ON THE WORLD TO SURVEY IT");
 if(surveyed_.contains(pair))return failure("THIS WORLD'S DEPOSITS ARE ALREADY CHARTED");
 surveyed_.insert(pair);technologyXP_+=3;if(w.communities)cultureXP_+=10+catalog_.star(currentSystem_).technology;
 Random r{mix64(w.uniqueId^0x4d494e4553ULL)};
 for(unsigned i=0;i<w.mines;++i){
  XDeposit d;d.system=currentSystem_;d.world=*currentWorld_;d.index=i;d.total=d.left=1+int64_t(r.bits(i*3)%20);
  d.good=w.composition==1&&r.unit(i*3+1)<.25?(r.unit(i*3+2)<.5?XGood::Marble:XGood::Granite):XGood(r.bits(i*3+2)%7);
  deposits_[{d.system,d.world,i}]=d;
 }
 view.message=w.mines?std::to_string(w.mines)+" FINITE DEPOSITS CHARTED":"NO DEPOSITS / THE CULTURAL SURVEY IS RECORDED";return true;
}
bool XalariaEconomy::mine(unsigned index,bool filling){
 if(currentSystem_!=view.place.system||currentWorld_!=view.place.world)return failure("VISIT THE SELECTED WORLD TO WORK ITS DEPOSITS");
 if(!landed_||!currentWorld_)return failure("LAND AT THE DEPOSIT TO WORK IT");
 auto it=deposits_.find({currentSystem_,*currentWorld_,index});if(it==deposits_.end())return failure("SURVEY THIS WORLD FIRST");
 auto& d=it->second;if(d.filled&&!filling)return failure("THIS DEPOSIT HAS BEEN RESTORED AND CLOSED");
 if(miningTimer_>0)return failure("MINING TOOL RECHARGING");
 if(filling){
  if(!fillingTool_)return failure("FIT THE MINE FILLING TOOL FROM A NATURE STATION");
  const auto n=std::min<int64_t>(technology(),d.total-d.left-d.restored);
  if(n<=0||energy_<2)return failure("NO WORKED LEVELS TO FILL OR TOOL ENERGY LOW");
  d.restored+=n;d.filled=true;energy_-=2;miningTimer_=2;view.message="FILLED "+std::to_string(n)+" WORKED LEVELS / EXTRACTION CLOSED";return true;
 }
 if(!tool_)return failure("FIT A TREMBLE OR PRECISION MINING TOOL");
 if(d.left<=0)return failure("THIS DEPOSIT IS EXHAUSTED");
 const double energyCost=precision_?1.:2.;
 if(cargoUnits()>=capacity_||energy_<energyCost)return failure("HOLD FULL OR TOOL ENERGY LOW");
 energy_-=energyCost;miningTimer_=2;
 if(random()*d.total>=d.left&&random()*26>=technology()){view.message="THE DEPOSIT RESISTS THIS STRIKE";return true;}
 int64_t take=precision_&&random()<technology()/26.?2:1;take=std::min({take,d.left,int64_t(capacity_)-cargoUnits()});d.left-=take;cargo_[size_t(d.good)]+=take;technologyXP_+=take;
 view.message="MINED "+std::to_string(take)+" "+std::string(xGoodInfo(d.good).name)+" / "+std::to_string(d.left)+" LEFT";return true;
}
XContract XalariaEconomy::offer(const XPlace& p) const{
 XContract c;c.origin=p;c.deadline=day()+7;Random r{key(p)^mix64(uint64_t(day()))};
 c.id=mix64(key(p)^mix64(uint64_t(day()))^0x434f4e5452414354ULL);c.reward=std::array<int64_t,3>{100000,300000,800000}[r.bits(0)%3];c.units=2+r.bits(1)%4;
 const auto& links=catalog_.star(p.system).wormholes;
 for(auto id:links)for(const auto& [wid,w]:catalog_.star(id).worlds){auto list=venues(id,wid);if(!list.empty()){c.destination=list.front();return c;}}
 c.units=0;return c;
}
bool XalariaEconomy::acceptContract(){
 if(!canTrade(view.place))return failure("VISIT THE STATION OR CITY TO ACCEPT ITS WORK");
 const auto c=offer(view.place);if(!c.units)return failure("NO DELIVERY WORK AT THIS MARKET");
 if(contracts_.size()>=1000)return failure("THE CONTRACT JOURNAL IS FULL");
 for(const auto& old:contracts_)if(old.id==c.id)return failure("THIS CHARTER HAS ALREADY BEEN TAKEN");
 if(cargoUnits()+c.units>capacity_)return failure("MAKE ROOM FOR THE SEALED PARCEL");
 contracts_.push_back(c);view.message="PARCEL LOADED / DELIVER TO "+placeName(c.destination);return true;
}
bool XalariaEconomy::deliverContracts(){
 int64_t paid=0;
 for(auto& c:contracts_)if(c.state==XContract::State::Active&&day()<=c.deadline&&canTrade(c.destination)){
  const auto amount=std::min(c.reward,moneyLimit-credits_);credits_+=amount;paid+=amount;c.state=XContract::State::Delivered;technologyXP_+=5;
 }
 if(!paid)return failure("NO PARCEL IS DUE AT THIS MARKET");
 view.message="DELIVERIES PAID / "+xMoney(paid);return true;
}
std::vector<XOpportunity> XalariaEconomy::opportunities(XGood good,int64_t units) const{
 std::vector<XOpportunity> out;if(units<1||units>capacity_||!validPlace(view.place))return out;
 const auto cost=quote(view.place,good,units,true);if(!cost.available)return out;
 std::set<uint32_t> visited{view.place.system};std::vector<uint32_t> frontier{view.place.system};
 for(unsigned depth=1;depth<=3;++depth){
  std::vector<uint32_t> next;
  for(auto from:frontier)for(auto id:catalog_.star(from).wormholes)if(visited.insert(id).second){
   next.push_back(id);int64_t best=0;XPlace where;
   for(const auto& [wid,w]:catalog_.star(id).worlds)for(const auto& p:venues(id,wid)){
    const auto sale=quote(p,good,units,false);const auto profit=sale.total-cost.total-int64_t(depth)*2000;
    if(sale.available&&profit>best){best=profit;where=p;}}
   if(best>0)out.push_back({where,best,depth});
  }
  frontier=std::move(next);
 }
 std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.profit>b.profit;});if(out.size()>3)out.resize(3);return out;
}
ActivityBody XalariaEconomy::portBody(const XPlace& p,double seconds) const{
 if(p.city||!validPlace(p))throw std::invalid_argument("No orbital port");
 const auto planet=universe_.describe(catalog_.address(p.system).child(Kind::Planet,p.world));
 auto b=sampleActivity(planet,0,seconds);const auto ordinal=p.station>=100000?p.station-100000+3:p.station;
 const auto rotation=glm::angleAxis(std::fmod(ordinal*2.399963229728653,2*std::numbers::pi),glm::dvec3(0,1,0));
 b.position=rotation*b.position;b.orientation=rotation*b.orientation;b.id={planet.address,p.station>=100000?70000+p.station-100000:60000+p.station};
 b.name=placeName(p);b.status="BERTH OPEN / "+std::string(xFactionName(faction(p)));b.economicBrief="F4 MARKET / DOCK TO TRADE";
 b.tint=faction(p)==XFaction::Trade?glm::vec3(.9,.72,.3):faction(p)==XFaction::Nature?glm::vec3(.3,.85,.6):glm::vec3(.5,.75,1);
 return b;
}
std::vector<ActivityBody> XalariaEconomy::orbitalBodies(const Node& n,double seconds) const{
 std::vector<ActivityBody> out;if(n.address.kind()!=Kind::Planet)return out;
 for(const auto& p:venues(uint32_t(n.address.parent().id()),n.address.id()))if(!p.city)out.push_back(portBody(p,seconds));
 return out;
}
bool XalariaEconomy::travelToPort(const XPlace& p,Navigation& nav,Pilot& pilot){
 if(!validPlace(p)||p.city)return failure("CHOOSE AN ORBITAL STATION");
 if(currentSystem_!=p.system||currentWorld_!=p.world||!nearOrbit_)return failure("FLY TO THIS WORLD FIRST");
 const auto body=portBody(p);const auto berth=dockingBerth(body);if(!berth)return failure("THE BERTH IS CLOSED");
 FlightWaypoint point;point.reference=body.id.home;point.metres=berth->metres;point.velocity=berth->velocity;point.standOff=0;point.label=body.name;point.berth=berth;
 if(point.reference!=nav.anchor)nav.select(point.reference);pilot.navigateWaypoint(point);view.message="AUTOMATIC DOCKING / "+body.name;return true;
}
void XalariaEconomy::sync(const Navigation& nav,const Pilot& pilot){
 auto sys=nav.anchor;while(sys.kind()>Kind::System)sys=sys.parent();if(sys.kind()==Kind::System)currentSystem_=uint32_t(sys.id());
 auto world=planetOf(nav.anchor);currentWorld_.reset();nearOrbit_=false;landed_=pilot.ship().landed;
 if(world.kind()==Kind::Planet){
  currentWorld_=world.id();const auto n=universe_.describe(world),frame=universe_.describe(nav.anchor);
  const auto eye=universe_.pointRelative(nav.anchor,nav.cameraPosition()*std::pow(10.,nav.logDistance-frame.frame.logRadius),world,0);
  nearOrbit_=glm::length(eye)<=std::pow(10.,n.frame.logRadius)*6;
  if(nearOrbit_&&visited_.insert({currentSystem_,world.id()}).second)technologyXP_+=2;
 }
 const auto previous=docked_;docked_.reset();
 if(pilot.instruments().docking.docked&&currentWorld_){
  const auto frame=universe_.describe(nav.anchor);
  const auto eye=universe_.pointRelative(nav.anchor,nav.cameraPosition()*std::pow(10.,nav.logDistance-frame.frame.logRadius),catalog_.address(currentSystem_).child(Kind::Planet,*currentWorld_),0);
  double nearest=100;
  for(const auto& p:venues(currentSystem_,*currentWorld_))if(!p.city){const auto berth=dockingBerth(portBody(p));if(berth){const auto distance=glm::length(eye-berth->metres);if(distance<nearest){nearest=distance;docked_=p;}}}
 }
 if(docked_&&docked_!=previous&&faction(*docked_)==XFaction::Trade&&goodwillDue()){
  const auto due=std::min(goodwillDue(),moneyLimit-credits_);credits_+=due;rebatePoints_=goodwill();view.message="TRADE GOODWILL PAID / "+xMoney(due);
 }
}
namespace { constexpr double wormholeFuel = 8.; }
bool XalariaEconomy::beginVoyage(bool interstellar){
 if(voyageBudget_>0)return true;
 const double cost=interstellar?wormholeFuel:1.;if(fuel_<cost)return failure("NOT ENOUGH FUEL / DOCK AND REFILL");
 voyageBudget_=cost;voyageUsed_=0;return true;
}
bool XalariaEconomy::payWormholeFuel(){
 if(fuel_<wormholeFuel)return failure("NOT ENOUGH FUEL / DOCK AND REFILL");
 endVoyage();
 fuel_-=wormholeFuel;
 return true;
}
void XalariaEconomy::voyageProgress(double fraction){
 fraction=std::clamp(fraction,0.,1.);const auto wanted=voyageBudget_*fraction;
 if(wanted>voyageUsed_){fuel_=std::max(0.,fuel_-(wanted-voyageUsed_));voyageUsed_=wanted;}
}
void XalariaEconomy::endVoyage(bool completed){if(completed)voyageProgress(1);voyageBudget_=voyageUsed_=0;}
void XalariaEconomy::settleDay(int64_t dayNumber){
 constexpr unsigned defence[3][9]={{0,8,12,18,27,38,50,62,73},{18,27,38,50,62,73,82,88,92},{50,62,73,82,88,92,95,97,98}};
 for(auto& a:assets_){
  if(a.state==XAssetState::Frame){if(dayNumber*86400-a.lastFed>=7*86400)a.state=XAssetState::Lost;continue;}
  if(a.state!=XAssetState::Active||a.lastPaidDay>=dayNumber)continue;
  a.lastPaidDay=dayNumber;const auto income=dailyIncome(a);a.till=std::min(moneyLimit,a.till+income);
  if(a.hall)a.culturalTill=std::min(moneyLimit,a.culturalTill+(5*200000+40*16000)/20);
  if(a.gallery&&a.artwork.size()>=3){int64_t value=5*200000+40*22000;for(const auto& art:a.artwork)if(art.placedDay<dayNumber)value+=art.value;a.culturalTill=std::min(moneyLimit,a.culturalTill+value/20);}
  Random r{mix64(uint64_t(a.id)^mix64(uint64_t(dayNumber)))};
  if(a.kind==XAssetKind::Farm){
   if(!a.guards&&r.bits(0)%7==0){a.state=XAssetState::Lost;a.till=0;continue;}
   if(a.till>income*2&&r.unit(1)<.18){
    const bool stopped=a.guards&&r.unit(2)<.75;if(a.guards)--a.guards;if(!stopped)a.till/=2;
   }
  }else if(a.kind==XAssetKind::Station&&a.till>income&&r.unit(1)<std::clamp(double(a.till)/std::max<int64_t>(1,income*16),.02,.65)){
   const unsigned guards=std::min(8u,a.guards+(a.tradeFlag?6u:0u));
   if(r.unit(2)*100>=defence[std::min(2u,a.lasers)][guards])a.till/=2;
   if(a.guards&&r.unit(3)<.18)--a.guards;
  }
 }
 for(auto& c:contracts_)if(c.state==XContract::State::Active&&dayNumber>c.deadline)c.state=XContract::State::Expired;
}
void XalariaEconomy::advance(double dt){
 if(!std::isfinite(dt)||dt<0||dt>315360000)return;
 const auto old=day();seconds_+=dt*60.;energy_=std::min(20.,energy_+dt*.6);miningTimer_=std::max(0.,miningTimer_-dt);
 for(int64_t d=old+1;d<=day();++d)settleDay(d);
}
void XalariaEconomy::useManualFuel(double dt,double thrust,bool warp){
 if(!std::isfinite(dt)||!std::isfinite(thrust)||dt<0||dt>.5)return;
 fuel_=std::max(0.,fuel_-dt*(std::clamp(thrust,0.,1.)*.004+(warp?.04:0.)));
}
void XalariaEconomy::catchUp(int64_t now){
 if(now<0)return;if(wallTime_>0&&now>wallTime_)advance(double(std::min<int64_t>(now-wallTime_,315360000)));wallTime_=std::max(wallTime_,now);
}
std::string XalariaEconomy::serialize(int64_t unixTime) const{
 std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(17);
 s<<"XALARIA_ECONOMY 1 "<<catalog_.fingerprint<<' '<<credits_<<' '<<tradeSpend_<<' '<<rebatePoints_<<' '<<technologyXP_<<' '<<cultureXP_<<' '
  <<capacity_<<' '<<shipTier_<<' '<<fuel_<<' '<<fuelCapacity_<<' '<<energy_<<' '<<tool_<<' '<<precision_<<' '<<fillingTool_<<' '<<seconds_<<' '<<miningTimer_<<' '
  <<voyageBudget_<<' '<<voyageUsed_<<' '<<(unixTime?unixTime:wallTime_)<<' '<<randomCounter_<<' '<<nextArt_<<' '<<nextAsset_<<'\n';
 for(auto n:cargo_)s<<n<<' ';s<<'\n'<<artwork_.size()<<'\n';for(const auto& a:artwork_)s<<a.id<<' '<<a.value<<' '<<a.placedDay<<'\n';
 s<<markets_.size()<<'\n';for(const auto& [p,m]:markets_){writePlace(s,p);s<<' '<<m.cash<<' '<<m.restockedDay;for(auto n:m.stock)s<<' '<<n;for(auto n:m.target)s<<' '<<n;s<<'\n';}
 s<<assets_.size()<<'\n';for(const auto& a:assets_){
  s<<a.id<<' '<<int(a.kind)<<' '<<int(a.state)<<' '<<a.system<<' '<<a.world<<' '<<a.level<<' '<<a.nanoNeeded<<' '<<a.metalNeeded<<' '<<a.seedNeeded<<' '
   <<a.till<<' '<<a.culturalTill<<' '<<a.lastFed<<' '<<a.lastPaidDay<<' '<<a.guards<<' '<<a.lasers<<' '<<a.guardCity<<' '<<a.tradeFlag<<' '<<a.fuelStation<<' '<<a.hall<<' '<<a.gallery;
  for(auto n:a.stored)s<<' '<<n;s<<' '<<a.artwork.size();for(const auto& art:a.artwork)s<<' '<<art.id<<' '<<art.value<<' '<<art.placedDay;s<<'\n';
 }
 s<<deposits_.size()<<'\n';for(const auto& [id,d]:deposits_)s<<d.system<<' '<<d.world<<' '<<d.index<<' '<<int(d.good)<<' '<<d.total<<' '<<d.left<<' '<<d.restored<<' '<<d.filled<<'\n';
 s<<contracts_.size()<<'\n';for(const auto& c:contracts_){s<<c.id<<' ';writePlace(s,c.origin);s<<' ';writePlace(s,c.destination);s<<' '<<c.deadline<<' '<<c.reward<<' '<<c.units<<' '<<int(c.state)<<'\n';}
 s<<known_.size()<<'\n';for(const auto& [p,n]:known_)s<<p.first<<' '<<p.second<<' '<<n<<'\n';
 s<<surveyed_.size()<<'\n';for(const auto& p:surveyed_)s<<p.first<<' '<<p.second<<'\n';
 s<<visited_.size()<<'\n';for(const auto& p:visited_)s<<p.first<<' '<<p.second<<'\n';return s.str();
}
bool XalariaEconomy::restore(std::string_view text){
 try{
  XalariaEconomy state(universe_);std::istringstream s{std::string(text)};s.imbue(std::locale::classic());std::string tag,version;int format;
  if(!(s>>tag>>format>>version)||tag!="XALARIA_ECONOMY"||format!=1||version!=catalog_.fingerprint)return false;
  if(!(s>>state.credits_>>state.tradeSpend_>>state.rebatePoints_>>state.technologyXP_>>state.cultureXP_>>state.capacity_>>state.shipTier_>>state.fuel_>>state.fuelCapacity_>>state.energy_>>state.tool_>>state.precision_>>state.fillingTool_>>state.seconds_>>state.miningTimer_>>state.voyageBudget_>>state.voyageUsed_>>state.wallTime_>>state.randomCounter_>>state.nextArt_>>state.nextAsset_))return false;
  const auto validMoney=[](int64_t n){return n>=0&&n<=moneyLimit;};
  if(!validMoney(state.credits_)||!validMoney(state.tradeSpend_)||state.rebatePoints_<0||state.rebatePoints_>state.goodwill()||state.technologyXP_<0||state.cultureXP_<0||state.shipTier_>6||state.capacity_>600||state.capacity_<30||!std::isfinite(state.fuel_)||!std::isfinite(state.fuelCapacity_)||state.fuel_<0||state.fuel_>state.fuelCapacity_||state.fuelCapacity_>400||!std::isfinite(state.energy_)||state.energy_<0||state.energy_>20||!std::isfinite(state.seconds_)||state.seconds_<0||state.seconds_>1e12||!std::isfinite(state.miningTimer_)||state.miningTimer_<0||!std::isfinite(state.voyageBudget_)||state.voyageBudget_<0||state.voyageBudget_>8||!std::isfinite(state.voyageUsed_)||state.voyageUsed_<0||state.voyageUsed_>state.voyageBudget_||state.nextAsset_>100000||state.nextArt_>10000000)return false;
  constexpr std::array<unsigned,7> holds{30,60,120,200,300,450,600};constexpr std::array<double,7> tanks{80,100,120,160,200,280,400};
  if(state.capacity_!=holds[state.shipTier_]||state.fuelCapacity_!=tanks[state.shipTier_]||state.wallTime_<0||state.nextAsset_<1||state.nextArt_<1)return false;
  const auto readStock=[&](XStock& stock){for(auto& n:stock)if(!(s>>n)||n<0||n>1000000)return false;return true;};
  const auto readArt=[&](XArtwork& art){return bool(s>>art.id>>art.value>>art.placedDay)&&validMoney(art.value)&&art.placedDay>=0&&art.placedDay<=state.day();};
  if(!readStock(state.cargo_))return false;size_t count;
  if(!(s>>count)||count>600)return false;for(size_t i=0;i<count;++i){XArtwork a;if(!readArt(a))return false;state.artwork_.push_back(a);}
  if(!(s>>count)||count>100000)return false;
  for(size_t i=0;i<count;++i){XPlace p;XMarket m;if(!readPlace(s,p)||!(s>>m.cash>>m.restockedDay)||!validMoney(m.cash)||m.restockedDay<0||m.restockedDay>state.day()||!readStock(m.stock)||!readStock(m.target)||state.markets_.contains(p))return false;state.markets_[p]=m;}
  if(!(s>>count)||count>128)return false;std::set<uint32_t> assetIds;
  for(size_t i=0;i<count;++i){
   XAsset a;int kind,phase;size_t artCount;
   if(!(s>>a.id>>kind>>phase>>a.system>>a.world>>a.level>>a.nanoNeeded>>a.metalNeeded>>a.seedNeeded>>a.till>>a.culturalTill>>a.lastFed>>a.lastPaidDay>>a.guards>>a.lasers>>a.guardCity>>a.tradeFlag>>a.fuelStation>>a.hall>>a.gallery)||kind<0||kind>2||phase<0||phase>2||!assetIds.insert(a.id).second||a.id>=state.nextAsset_||!catalog_.stars.contains(a.system)||!catalog_.star(a.system).worlds.contains(a.world)||a.level<1||a.level>22||a.nanoNeeded<0||a.nanoNeeded>1400||a.metalNeeded<0||a.metalNeeded>440||a.seedNeeded<0||a.seedNeeded>150||!validMoney(a.till)||!validMoney(a.culturalTill)||a.lastFed<0||a.lastPaidDay<0||a.lastPaidDay>state.day()||a.guards>9||a.lasers>2||(a.guards&&(!catalog_.star(a.system).worlds.contains(a.guardCity)||!catalog_.star(a.system).worlds.at(a.guardCity).communities))||!readStock(a.stored)||!(s>>artCount)||artCount>1000)return false;
   a.kind=XAssetKind(kind);a.state=XAssetState(phase);for(size_t j=0;j<artCount;++j){XArtwork art;if(!readArt(art))return false;a.artwork.push_back(art);}state.assets_.push_back(std::move(a));
  }
  for(const auto& [p,m]:state.markets_)if(!state.validPlace(p))return false;
  if(!(s>>count)||count>100000)return false;
  for(size_t i=0;i<count;++i){XDeposit d;unsigned g;if(!(s>>d.system>>d.world>>d.index>>g>>d.total>>d.left>>d.restored>>d.filled)||g>=xGoodCount||!catalog_.stars.contains(d.system)||!catalog_.star(d.system).worlds.contains(d.world)||d.index>=catalog_.star(d.system).worlds.at(d.world).mines||d.total<1||d.total>20||d.left<0||d.left>d.total||d.restored<0||d.restored>d.total-d.left||(!d.filled&&d.restored))return false;d.good=XGood(g);state.deposits_[{d.system,d.world,d.index}]=d;}
  if(!(s>>count)||count>1000)return false;std::set<uint64_t> contractIds;
  for(size_t i=0;i<count;++i){XContract c;int phase;if(!(s>>c.id)||!readPlace(s,c.origin)||!readPlace(s,c.destination)||!(s>>c.deadline>>c.reward>>c.units>>phase)||phase<0||phase>2||!state.validPlace(c.origin)||!state.validPlace(c.destination)||c.deadline<0||!validMoney(c.reward)||c.units<1||c.units>30||!contractIds.insert(c.id).second)return false;c.state=XContract::State(phase);state.contracts_.push_back(c);}
  if(!(s>>count)||count>50000)return false;for(size_t i=0;i<count;++i){uint32_t id;uint64_t w;unsigned n;if(!(s>>id>>w>>n)||n>7||!catalog_.stars.contains(id)||!catalog_.star(id).worlds.contains(w))return false;state.known_[{id,w}]=n;}
  const auto places=[&](auto& set){size_t n;if(!(s>>n)||n>50000)return false;for(size_t i=0;i<n;++i){uint32_t id;uint64_t w;if(!(s>>id>>w)||!catalog_.stars.contains(id)||!catalog_.star(id).worlds.contains(w))return false;set.insert({id,w});}return true;};
  if(!places(state.surveyed_)||!places(state.visited_)||state.cargoUnits()>state.capacity_)return false;std::string extra;if(s>>extra)return false;
  credits_=state.credits_;tradeSpend_=state.tradeSpend_;rebatePoints_=state.rebatePoints_;technologyXP_=state.technologyXP_;cultureXP_=state.cultureXP_;
  cargo_=state.cargo_;capacity_=state.capacity_;shipTier_=state.shipTier_;fuel_=state.fuel_;fuelCapacity_=state.fuelCapacity_;energy_=state.energy_;tool_=state.tool_;precision_=state.precision_;
  fillingTool_=state.fillingTool_;seconds_=state.seconds_;miningTimer_=state.miningTimer_;voyageBudget_=state.voyageBudget_;voyageUsed_=state.voyageUsed_;wallTime_=state.wallTime_;randomCounter_=state.randomCounter_;nextArt_=state.nextArt_;nextAsset_=state.nextAsset_;
  markets_=std::move(state.markets_);assets_=std::move(state.assets_);artwork_=std::move(state.artwork_);contracts_=std::move(state.contracts_);deposits_=std::move(state.deposits_);known_=std::move(state.known_);surveyed_=std::move(state.surveyed_);visited_=std::move(state.visited_);
  view.message="ECONOMY RESTORED / YOUR CARGO AND HOLDINGS ARE INTACT";return true;
 }catch(const std::exception&){return false;}
}
} // namespace infinity
