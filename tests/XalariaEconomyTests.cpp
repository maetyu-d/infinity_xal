#include "infinity/XalariaEconomy.hpp"
#include "infinity/XalariaNavigation.hpp"
#include "infinity/Cockpit.hpp"
#include <fstream>
#include <iomanip>
#include <limits>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cmath>
using namespace infinity;
void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
struct Fixture {
 std::shared_ptr<const XalariaCatalog> catalog;Universe universe;Navigation nav;RegionCache cache;FlightController controller;Pilot pilot;XalariaEconomy economy;XalariaNavigation course;
 explicit Fixture(std::shared_ptr<const XalariaCatalog> c):catalog(c),universe(0x58414c4152494101ULL,c),nav(universe),cache(universe),controller(universe,nav,cache),pilot(universe,nav,controller),economy(universe),course(universe,nav,pilot){course.setEconomy(&economy);}
 bool tick(double dt=.1,const HelmInput& input={}){
  cache.beginFrame();const bool crossed=course.advanceFlight(input,dt);economy.advance(dt);
  economy.sync(nav,pilot);pilot.setCargoMass(economy.cargoKg());cache.finishFrame();return crossed;
 }
 void orbit(uint32_t system,uint64_t world){
  pilot.brake();pilot.clearWaypoint();auto address=catalog->address(system).child(Kind::Planet,world);nav.changeFrame(address);nav.setRenderScale(universe.describe(address).frame.logRadius);
  nav.orientation={1,0,0,0};nav.offset={0,0,1.25};pilot.synchronizeFrame();pilot.resetMotion();economy.sync(nav,pilot);course.update(0);economy.show(system,world);
 }
 void dock(XPlace p){
  pilot.brake();pilot.clearWaypoint();const auto b=*dockingBerth(economy.portBody(p));nav.changeFrame(b.id.home);nav.setRenderScale(universe.describe(b.id.home).frame.logRadius);
  nav.orientation=b.orientation;nav.offset=b.metres/std::pow(10.,nav.logDistance)-nav.orientation*glm::dvec3(0,0,1);
  pilot.synchronizeFrame();pilot.resetMotion();economy.sync(nav,pilot);economy.show(p.system,p.world);economy.view.place=p;
  check(!economy.canTrade(p),"Proximity alone allowed orbital trading");check(economy.travelToPort(p,nav,pilot),"Docking request failed");
  for(int i=0;i<80&&!economy.dockedPlace();++i)tick();
  check(economy.canTrade(p),"Physical berth did not enable the market");
 }
 void land(uint32_t system,uint64_t wid){
  orbit(system,wid);const auto p=universe.describe(catalog->address(system).child(Kind::Planet,wid));const double r=std::pow(10.,p.frame.logRadius);
  bool accepted=false;
  for(unsigned i=0;i<160&&!accepted;++i){
   const auto dir=glm::normalize(glm::dvec3(std::sin(i*1.7)+.2,std::cos(i*2.1)+.1,.5));
   const auto normal=universe.terrainNormal(p,dir);if(glm::dot(normal,dir)<.995)continue;
   const auto up=normal;const auto forward=glm::normalize(glm::cross(up,glm::dvec3(.2,.3,1)));
   nav.orientation=glm::quatLookAtRH(forward,up);nav.offset=dir*(1+(universe.terrainHeight(p,dir)+2)/r)-nav.orientation*glm::dvec3(0,0,1);
   pilot.synchronizeFrame();pilot.resetMotion();accepted=pilot.landAt(p.address,dir,true);
  }
  check(accepted,"No safe test landing site");
  for(int i=0;i<1500&&!pilot.ship().landed;++i)tick();
  check(pilot.ship().landed,"Controlled landing did not complete");economy.sync(nav,pilot);
 }
 // Save fixtures provide materials and experience; no grant API exists in gameplay.
 void provision(XStock cargo={},bool tools=true){
  auto text=economy.serialize();const auto first=text.find('\n'),second=text.find('\n',first+1);
  std::istringstream in(text.substr(0,first));std::vector<std::string> t;std::string word;while(in>>word)t.push_back(word);
  t[3]="10000000000";t[6]="50000";t[7]="50000";t[8]="600";t[9]="6";t[10]="400";t[11]="400";t[12]="20";t[13]=tools?"1":"0";t[14]=tools?"1":"0";t[15]=tools?"1":"0";
  std::ostringstream out;for(const auto& s:t)out<<s<<' ';out<<'\n';for(auto n:cargo)out<<n<<' ';out<<text.substr(second);
  check(economy.restore(out.str()),"Provisioned save rejected");economy.sync(nav,pilot);
 }
};
void manualCargoRun(std::shared_ptr<const XalariaCatalog> catalog, const std::filesystem::path& captures) {
 Fixture f(std::move(catalog));
 constexpr XPlace departure{14940,0,0,false}, arrival{14538,1,0,false};
 const auto checkpoint=[&](std::string_view name){
  if(captures.empty())return;
  const auto dir=captures/name;
  std::filesystem::create_directories(dir);
  std::ostringstream port;
  if(f.economy.dockedPlace()){
   const auto& p=*f.economy.dockedPlace();
   port<<f.catalog->fingerprint<<' '<<p.system<<' '<<p.world<<' '<<p.station<<' '<<p.city;
  }
  std::ofstream out(dir/"session.txt");
  out<<"XALARIA_SESSION 1 "<<f.catalog->fingerprint<<' '
     <<std::quoted(f.nav.serialize())<<' '<<std::quoted(f.pilot.serialize())<<' '
     <<std::quoted(f.course.serialize())<<' '<<std::quoted(f.economy.serialize())<<' '<<std::quoted(port.str())<<'\n';
  out.close();check(bool(out),"Could not save manual cargo capture");
 };
 f.dock(departure);
 check(f.course.view.wormhole&&f.course.view.wormhole->destination==arrival.system,"Oylanhai mouth unavailable");
 check(f.course.targetWormhole()&&f.economy.canTrade(departure),"Targeting the mouth released the berth");
 constexpr int64_t units=4;
 XGood good=XGood::Iron;
 int64_t spread=std::numeric_limits<int64_t>::min();
 for(unsigned i=0;i<unsigned(XGood::Count);++i){
  const auto candidate=XGood(i);
  const auto buy=f.economy.quote(departure,candidate,units,true);
  const auto sell=f.economy.quote(arrival,candidate,units,false);
  if(buy.available&&sell.available&&sell.total-buy.total>spread){good=candidate;spread=sell.total-buy.total;}
 }
 const auto startingCredits=f.economy.credits();
 const auto purchase=f.economy.quote(departure,good,units,true);
 check(f.economy.buy(good,units),"Eunhia purchase failed");
 check(f.economy.credits()==startingCredits-purchase.total&&f.economy.cargoUnits()==units,"Incorrect purchase ledger");
 checkpoint("departure");
 f.pilot.launch();
 for(int i=0;i<5000&&f.pilot.instruments().docking.active;++i)f.tick();
 check(!f.pilot.instruments().docking.active&&!f.economy.dockedPlace(),"Eunhia departure did not clear the berth");
 check(f.course.instruments().targetLabel=="WORMHOLE TO Ziksa","Manual bearing unavailable after departure");
 const auto startDistance=f.pilot.ship().travelMetres;
 bool crossed=false,captured=false;
 const HelmInput forward{.throttle=1};
 for(int i=0;i<3200&&!crossed;++i){
  if(!captured&&f.course.instruments().range<4700.){
   checkpoint("approach");captured=true;
  }
  const double fuel=f.economy.fuel();
  crossed=f.tick(.1,forward);
  if(crossed)check(std::abs(f.economy.fuel()-(fuel-8.))<1e-7,"Manual crossing did not charge exactly eight fuel");
  f.economy.useManualFuel(.1,1.,false);
 }
 check(crossed&&f.economy.currentSystem()==arrival.system&&f.course.view.current==arrival.system,"Manual helm did not reach Ziksa");
 check(f.pilot.instruments().warpArrivalSequence==0&&f.pilot.ship().travelMetres>startDistance+5000.,"Crossing used guided warp or skipped the approach");
 check(f.course.view.selectedWorld==arrival.world&&f.course.view.wormhole&&f.course.view.wormhole->destination==departure.system,
       "Exit failed to select Phehmyar and expose the return mouth");
 check(f.economy.cargoUnits()==units&&!f.economy.canTrade(arrival),"Cargo or berth state changed during transit");
 checkpoint("arrival");
 f.economy.show(arrival.system,arrival.world);f.economy.view.place=arrival;
 check(f.economy.travelToPort(arrival,f.nav,f.pilot),"Phehmyar docking request failed");
 for(int i=0;i<16000&&!f.economy.dockedPlace();++i)f.tick();
 check(f.economy.dockedPlace()==arrival&&f.economy.canTrade(arrival),"Cargo did not reach a physical Phehmyar berth");
 const auto stock=f.economy.stock(arrival,good),wallet=f.economy.credits();
 const auto sale=f.economy.quote(arrival,good,units,false);
 check(f.economy.sell(good,units),"Phehmyar sale failed");
 check(f.economy.cargoUnits()==0&&f.economy.stock(arrival,good)==stock+units&&f.economy.credits()==wallet+sale.total,
       "Incorrect sale ledger");
 checkpoint("delivered");
 std::cout<<"MANUAL_CARGO origin="<<f.economy.placeName(departure)<<" destination="<<f.economy.placeName(arrival)
          <<" units="<<units<<" good="<<xGoodInfo(good).name<<" paid_cents="<<purchase.total<<" received_cents="<<sale.total
          <<" net_trade_cents="<<sale.total-purchase.total<<" gate_fuel=8 remaining_fuel="<<f.economy.fuel()
          <<" hull="<<f.pilot.ship().hull<<" flight_seconds="<<f.pilot.ship().flightSeconds<<'\n';
}
void manualWormholeClearance(std::shared_ptr<const XalariaCatalog> catalog) {
 Fixture f(std::move(catalog));
 f.orbit(14940,0);
 check(f.course.view.wormhole.has_value(),"Missing manual mouth");
 const auto mouth=*f.course.view.wormhole;
 const double units=std::pow(10.,f.universe.describe(mouth.reference).frame.logRadius);
 const auto prepare=[&](double lateral,double depth){
  f.nav.setRenderScale(std::log10(units));
  f.nav.orientation=depth>0?mouth.orientation:mouth.orientation*glm::angleAxis(std::acos(-1.),glm::dvec3(0,1,0));
  f.nav.offset=(mouth.metres+mouth.orientation*glm::dvec3(lateral,0,depth))/units-
               f.nav.orientation*glm::dvec3(0,0,1);
  f.pilot.resetMotion();
  f.pilot.setVelocity(f.nav.orientation*glm::dvec3(0,0,-600.));
 };
 const auto crossedPlane=[&](){
  const auto eye=f.nav.cameraPosition()*std::pow(10.,f.nav.logDistance);
  return glm::dot(eye-mouth.metres,mouth.orientation*glm::dvec3(0,0,1))<0;
 };
 const double fuel=f.economy.fuel();
 prepare(mouth.radius+50,10.);
 check(!f.tick()&&crossedPlane()&&f.economy.currentSystem()==14940&&f.economy.fuel()==fuel,
       "Passing outside the opening triggered transit or consumed fuel");
 while(f.economy.payWormholeFuel()){}
 prepare(0.,10.);
 check(!f.tick()&&crossedPlane()&&f.economy.currentSystem()==14940&&f.economy.fuel()<8.,
       "An unfuelled ship crossed the wormhole");
 Fixture reverse(f.catalog);
 reverse.orbit(14940,0);
 reverse.nav.orientation=mouth.orientation*glm::angleAxis(std::acos(-1.),glm::dvec3(0,1,0));
 reverse.nav.offset=(mouth.metres+mouth.orientation*glm::dvec3(0,0,-10.))/units-
                    reverse.nav.orientation*glm::dvec3(0,0,1);
 reverse.pilot.resetMotion();
 reverse.pilot.setVelocity(reverse.nav.orientation*glm::dvec3(0,0,-600.));
 check(reverse.tick()&&reverse.economy.currentSystem()==14538&&reverse.economy.fuel()==fuel-8.,
       "Approach from the opposite side of the ring failed");
 check(!reverse.tick()&&reverse.economy.currentSystem()==14538,"The exit retriggered on the next frame");
}
int main(int argc,char** argv){try{
 check(argc==2||argc==3,"Catalogue required");auto c=std::make_shared<XalariaCatalog>(argv[1]);
 manualCargoRun(c,argc==3?std::filesystem::path(argv[2]):std::filesystem::path{});
 manualWormholeClearance(c);
 Fixture f(c);
 XPlace home{14940,0,0,false};f.orbit(home.system,home.world);
 auto before=f.economy.serialize();check(!f.economy.buy(XGood::Iron,1),"Remote purchase succeeded");check(!f.economy.sell(XGood::Iron,1),"Remote sale succeeded");check(before==f.economy.serialize(),"Rejected remote transaction mutated state");
 f.dock(home);check(!f.economy.buy(XGood::Iron,0)&&!f.economy.buy(XGood::Iron,-1),"Invalid purchase accepted");
 const auto credits=f.economy.credits(),stock=f.economy.stock(home,XGood::Iron);const auto price=f.economy.quote(home,XGood::Iron,10,true);
 check(f.economy.buy(XGood::Iron,10),"Valid purchase failed");check(f.economy.credits()==credits-price.total&&f.economy.cargoUnits()==10&&f.economy.stock(home,XGood::Iron)==stock-10,"Purchase did not conserve cargo or credits");
 const auto sale=f.economy.quote(home,XGood::Iron,10,false);check(sale.total<price.total,"Local buy/sell creates free money");
 check(f.economy.sell(XGood::Iron,10),"Sale failed");check(f.economy.cargoUnits()==0&&f.economy.stock(home,XGood::Iron)==stock&&f.economy.credits()<credits,"Round trip changed stock or generated money");
 check(!f.economy.buy(XGood::Iron,31),"Cargo overflow accepted");check(!f.economy.sell(XGood::Count,1),"Invalid good accepted");
 auto offered=f.economy.offer(home);check(offered.units>0,"No source charter");check(f.economy.acceptContract(),"Charter acceptance failed");const auto sealed=f.economy.cargoUnits();
 check(!f.economy.acceptContract()&&f.economy.cargoUnits()==sealed,"Duplicate charter loaded cargo");
 check(!f.economy.deliverContracts(),"Remote delivery paid");check(!f.economy.sell(XGood::Iron,sealed),"Sealed cargo sold as metal");
 // Real departure, physical wormhole flight, and arrival to the same destination as the charter.
 f.pilot.launch();for(int i=0;i<5000&&f.pilot.instruments().docking.active;++i)f.tick();
 f.course.selectSystem(offered.destination.system);f.course.selectWorld(offered.destination.world);check(f.course.fly(),"Fuelled flight failed");
 const auto initialFuel=f.economy.fuel();
 for(int i=0;i<20000&&f.pilot.instruments().warpArrivalSequence==0;++i)f.tick();
 check(f.pilot.instruments().warpArrivalSequence==1&&f.economy.currentSystem()==offered.destination.system,"Paid cargo did not fly to its destination");
 check(std::abs(f.economy.fuel()-(initialFuel-8))<1e-6,"Wormhole fuel did not settle exactly once");
 check(f.economy.cargoUnits()==sealed,"Flight lost its sealed cargo");
 // Set up the exact berth to isolate physical clamp and delivery authorisation after the flight.
 f.course.cancel();if(offered.destination.city)f.land(offered.destination.system,offered.destination.world);else f.dock(offered.destination);
 f.economy.view.place=offered.destination;const auto wallet=f.economy.credits();
 check(f.economy.deliverContracts()&&f.economy.credits()==wallet+offered.reward,"Charter did not pay at destination");
 check(!f.economy.deliverContracts()&&f.economy.cargoUnits()==0,"Charter paid twice or retained parcel");
 // Fuel reservation cannot be reset by pause/resume or serialization.
 check(f.economy.beginVoyage(true),"Fuel reservation failed");f.economy.voyageProgress(.4);const auto partial=f.economy.fuel();
 check(f.economy.beginVoyage(true),"Fuel resume failed");f.economy.voyageProgress(.4);check(f.economy.fuel()==partial,"Resume burned the first section again");
 XalariaEconomy recovered(f.universe);check(recovered.restore(f.economy.serialize()),"Economy did not restore");
 recovered.voyageProgress(1);check(std::abs(recovered.fuel()-(partial-4.8))<1e-7,"Saved fuel progress was reset");recovered.endVoyage();
 f.economy.endVoyage();
 auto saved=f.economy.serialize();check(recovered.restore(saved)&&recovered.serialize()==saved,"Economy save is not lossless");
 auto bad=saved;bad.replace(bad.find(c->fingerprint),c->fingerprint.size(),"foreign");check(!recovered.restore(bad)&&recovered.serialize()==saved,"Foreign save mutated economy");
 check(!recovered.restore(saved.substr(0,saved.size()/2))&&recovered.serialize()==saved,"Truncated save mutated economy");
 for(int i=0;i<60;++i){check(recovered.beginVoyage(true)||recovered.fuel()<8,"Fuel unexpectedly refused");recovered.endVoyage(true);}
 check(recovered.fuel()<8&&!recovered.beginVoyage(true),"Empty tank permits a wormhole");
 // Expiry frees sealed hold space, and a manual drive cannot fly on a dry tank.
 XalariaEconomy expired(f.universe);Fixture exp(c);exp.dock(home);check(exp.economy.acceptContract(),"Expiry charter setup failed");
 exp.economy.advance(8*86400/60.);check(exp.economy.cargoUnits()==0&&exp.economy.contracts().back().state==XContract::State::Expired,"Overdue cargo did not expire");
 const auto manualFuel=expired.fuel();expired.useManualFuel(.5,1,true);check(expired.fuel()<manualFuel,"Manual flight has no fuel cost");
 // Guaranteed, high-technology construction fixture at an orbit with free station slots.
 uint32_t buildSystem=0;uint64_t buildWorld=0,farmWorld=0;uint32_t farmSystem=0;
 for(const auto& [id,s]:c->stars)for(const auto& [wid,w]:s.worlds){
  if(!buildSystem&&w.stations<3&&w.communities){buildSystem=id;buildWorld=wid;}
  if(!farmSystem&&w.composition==1&&w.temperature>250&&w.temperature<325&&w.mines&&w.communities){farmSystem=id;farmWorld=wid;}
 }
 check(buildSystem&&farmSystem,"Missing build/mining world");
 Fixture b(c);b.orbit(buildSystem,buildWorld);XStock kit{};kit[size_t(XGood::Nanomites)]=50;kit[size_t(XGood::Iron)]=100;b.provision(kit);
 check(XalariaEconomy::stationNanomites(1)==14&&XalariaEconomy::stationNanomites(22)==1400&&XalariaEconomy::stationIncome(22)==21700000,"Published station anchors changed");
 check(XalariaEconomy::constructionChance(1,7)==100&&XalariaEconomy::constructionChance(22,1)==0,"Seeding matrix changed");
 check(b.economy.seedAsset(XAssetKind::Station,1),"Guaranteed station failed");const auto station=b.economy.assets().back().id;
 check(b.economy.feedAsset(station),"Station feed failed");check(b.economy.assets().back().state==XAssetState::Active,"Full station did not complete");
 b.economy.advance(20);check(b.economy.seedAsset(XAssetKind::Vault),"Guaranteed vault failed");const auto vault=b.economy.assets().back().id;check(b.economy.feedAsset(vault),"Vault did not complete");
 check(!b.economy.seedAsset(XAssetKind::Vault),"Second private vault in same orbit accepted");
 const XPlace vaultPort{buildSystem,buildWorld,100000+vault,false};b.dock(vaultPort);const auto iron=b.economy.cargo()[size_t(XGood::Iron)];
 check(b.economy.store(vault,XGood::Iron,10,false)&&b.economy.store(vault,XGood::Iron,10,true),"Vault round trip failed");
 check(b.economy.cargo()[size_t(XGood::Iron)]==iron,"Vault storage generated cargo");
 check(b.economy.buildFuelStation(vault),"Vault fuel build failed");
 const auto balance=b.economy.credits();b.economy.advance(86400/60.);check(b.economy.credits()==balance,"Offline income went directly to wallet");
 check(b.economy.collect(vault)&&b.economy.credits()==balance+20000,"Vault daily till incorrect");
 check(b.economy.collect(vault)&&b.economy.credits()==balance+20000,"Till collected twice");
 // Cultural halls need actual construction materials and three authentic donated pieces.
 b.dock({buildSystem,buildWorld,100000+station,false});XStock culturalKit{};
 culturalKit[size_t(XGood::Nanomites)]=10;culturalKit[size_t(XGood::Marble)]=40;culturalKit[size_t(XGood::Granite)]=40;b.provision(culturalKit);
 check(b.economy.buildHall(station,false)&&b.economy.buildHall(station,true),"Cultural construction failed");
 check(!b.economy.buildHall(station,true),"Gallery built twice");
 for(int i=0;i<3;++i){check(b.economy.buyArtwork()&&b.economy.donateArtwork(station),"Authentic gallery donation failed");}
 b.economy.advance(86400/60.);check(b.economy.assets().front().culturalTill>0,"Cultural income missing");
 // Daily simulation must agree with one equivalent offline interval, including raids.
 XalariaEconomy batch(b.universe),incremental(b.universe);check(batch.restore(b.economy.serialize())&&incremental.restore(b.economy.serialize()),"Asset snapshot failed");
 batch.advance(12*86400/60.);for(int d=0;d<12;++d)incremental.advance(86400/60.);
 check(batch.serialize()==incremental.serialize(),"Offline catch-up changes daily settlement outcome");
 check(batch.restore(b.economy.serialize(1000)),"Timestamp save failed");batch.catchUp(2440);auto once=batch.serialize();batch.catchUp(2440);check(batch.serialize()==once,"Offline catch-up double counted income");
 b.orbit(buildSystem,buildWorld);b.provision(kit);check(b.economy.seedAsset(XAssetKind::Station,1)||c->star(buildSystem).worlds.at(buildWorld).stations>=2,"Frame fixture failed");
 const auto frame=b.economy.assets().back().id;b.economy.advance(8*86400/60.);check(b.economy.assets().back().id!=frame||b.economy.assets().back().state==XAssetState::Lost,"Unfed frame did not expire");
 // Finite deposits and farms share physical landed presence and cargo.
 Fixture m(c);m.land(farmSystem,farmWorld);XStock seeds{};seeds[size_t(XGood::Seed)]=150;m.provision(seeds);
 check(m.economy.survey(),"Landed survey failed");check(!m.economy.survey(),"Duplicate survey accepted");
 check(!m.economy.deposits().empty(),"Source mine count did not produce deposits");
 const auto dep=m.economy.deposits().begin()->second;check(dep.total>=1&&dep.total<=20,"Deposit size outside public bounds");
 const auto minedBefore=m.economy.cargoUnits();
 for(int i=0;i<500&&m.economy.deposits().begin()->second.left;++i){m.economy.mine(dep.index);m.economy.advance(3);}
 check(m.economy.deposits().begin()->second.left==0&&m.economy.cargoUnits()==minedBefore+dep.total,"Finite extraction lost units or created infinite material");
 check(!m.economy.mine(dep.index),"Exhausted deposit yielded ore");m.economy.advance(3);check(m.economy.mine(dep.index,true),"Restoration failed");check(!m.economy.mine(dep.index),"Restored deposit reopened");
 check(m.economy.seedAsset(XAssetKind::Farm),"Farm clearing failed");check(!m.economy.seedAsset(XAssetKind::Farm),"Two unfinished fields accepted");
 const auto farm=m.economy.assets().back().id;check(m.economy.feedAsset(farm)&&m.economy.assets().back().state==XAssetState::Active,"Farm feeding failed");
 // City prices remain non-arbitrageable after learning an aesthetic.
 m.economy.view.place={farmSystem,farmWorld,0,true};check(m.economy.canTrade(m.economy.view.place),"Landed city inaccessible");
 check(m.economy.studyCulture(),"Culture visit with CL13 failed");
 check(m.economy.aesthetics(farmSystem,farmWorld)==1,"Culture was not remembered");check(m.economy.guard(farm),"Farm city guards failed");
 const auto q=m.economy.quote(m.economy.view.place,XGood::Iron,1,true),r=m.economy.quote(m.economy.view.place,XGood::Iron,1,false);
 check(q.total>r.total,"Culture discount permits local arbitrage");
 for(double aspect:{1.,1.25,1.6,2.2})for(unsigned page=0;page<5;++page){
  m.economy.view.page=XEconomyView::Page(page);m.economy.view.good=0;
  const auto overlay=xalariaEconomyOverlay(m.economy,aspect);
  for(const auto& t:overlay.textRuns)check(t.bounds.x>=0&&t.bounds.y>=0&&t.bounds.x+t.bounds.w<=900*aspect+.01&&t.bounds.y+t.bounds.h<=900,"Economy text escaped viewport");
  check(cockpitTextConflicts(overlay).empty(),"Economy labels overlap");
 }
 for(double aspect:{1.,1.6,2.2}){
  f.course.view.open=true;auto hud=xalariaMapOverlay(f.course.view,aspect);xalariaEconomyHud(hud,f.economy,aspect,true);
  check(cockpitTextConflicts(hud).empty(),"Economy HUD overlaps map labels");
 }
 std::cout<<"PASS docking access, conserved trading, cargo limits, sealed contracts, physical wormhole haul, fuel, corruption-safe saves, construction, storage, daily and offline income, finite mining, farms, culture, guards and native UI bounds\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
