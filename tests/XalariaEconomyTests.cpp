#include "infinity/XalariaEconomy.hpp"
#include "infinity/XalariaCombat.hpp"
#include "infinity/XalariaNavigation.hpp"
#include "infinity/XalariaTraffic.hpp"
#include "infinity/XalariaStories.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/Docking.hpp"
#include "infinity/ShipTrail.hpp"
#include <fstream>
#include <iomanip>
#include <limits>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <glm/gtc/quaternion.hpp>
using namespace infinity;
void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
struct Fixture {
 std::shared_ptr<const XalariaCatalog> catalog;Universe universe;Navigation nav;RegionCache cache;FlightController controller;Pilot pilot;XalariaEconomy economy;XalariaNavigation course;
 XalariaStories* stories=nullptr;XalariaCombat* warfare=nullptr;
 explicit Fixture(std::shared_ptr<const XalariaCatalog> c):catalog(c),universe(0x58414c4152494101ULL,c),nav(universe),cache(universe),controller(universe,nav,cache),pilot(universe,nav,controller),economy(universe),course(universe,nav,pilot){course.setEconomy(&economy);}
 bool tick(double dt=.1,const HelmInput& input={}){
  if(warfare){
   CombatPlayer player;player.frame=nav.anchor;player.position=nav.cameraPosition()*std::pow(10.,nav.logDistance);
   player.orientation=nav.orientation;player.forward=nav.orientation*glm::dvec3(0,0,-1);player.velocity=pilot.ship().velocity;
   player.seconds=pilot.ship().flightSeconds;player.gate=course.view.wormhole;
   player.sheltered=pilot.instruments().docking.docked||pilot.ship().landed;
   player.transit=pilot.instruments().warp!=WarpPhase::Off||pilot.ship().shrinking||pilot.ship().cruise;
   if(pilot.clampedHarbour())player.clamped=*pilot.clampedHarbour();warfare->advance(player,dt);
  }
  cache.beginFrame();const bool crossed=course.advanceFlight(input,dt);
  if(stories)stories->advance(pilot.ship().flightSeconds);
  economy.advance(dt);
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
void collectCaseEvidence(XalariaStories& stories,StoryKey key,double seconds) {
 if(!stories.investigation(key)||stories.evidenceComplete(key))return;
 check(stories.track(key),"Could not track the physical evidence fixture");
 const auto plan=stories.investigationPlan(key);
 const auto story=stories.generate(key);
 StoryContext context{.system=story.system,.orbit=story.world,.stellarOrbit=!story.world};
 const std::array points{plan.centre,plan.clues[0],plan.clues[1],plan.clues[2]};
 for(unsigned step=0;step<4;++step){
  if(stories.investigation(key)->evidence&(1u<<step))continue;
  if(step==3){context.boardedStation=story.port;context.boardedVessel=stories.vessel(story);}
  check(stories.scanEvidence(context,plan.frame,points[step],{},seconds),"Could not collect evidence at its actual generated location");
 }
}
void manualCargoRun(std::shared_ptr<const XalariaCatalog> catalog, const std::filesystem::path& captures) {
 Fixture f(std::move(catalog));
 XalariaCombat warfare(f.universe,f.economy,f.pilot);f.warfare=&warfare;f.economy.setCombat(&warfare);
 XalariaStories incidents(f.economy,*f.catalog);
 f.stories=&incidents;f.economy.setStories(&incidents);
 constexpr XPlace departure=eunhiaStation, arrival=phehmyarStation;
 const auto checkpoint=[&](std::string_view name){
  if(captures.empty())return;
  const auto dir=captures/name;
  std::filesystem::create_directories(dir);
  std::ostringstream port;
  if(f.economy.dockedPlace()){
   const auto& p=*f.economy.dockedPlace();
   port<<f.catalog->fingerprint<<' '<<p.system<<' '<<p.world<<' '<<p.station<<' '<<p.city;
  }else if(manualDocking(f.pilot.instruments().docking.phase)){
   port<<f.catalog->fingerprint<<' '<<arrival.system<<' '<<arrival.world<<' '<<arrival.station<<" 0 manual";
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
  if(buy.available&&buy.total<=f.economy.credits()&&sell.available&&sell.total-buy.total>spread){good=candidate;spread=sell.total-buy.total;}
 }
 const auto startingCredits=f.economy.credits();
 const auto purchase=f.economy.quote(departure,good,units,true);
 check(f.economy.buy(good,units),"Eunhia purchase failed: "+f.economy.view.message);
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
 check(!f.economy.openCargoTerminal(),"Terminal allowed trading before docking");
 check(!f.economy.collectLoadingBayFreight(),"Loading bay granted freight before docking");
 const auto pickup=xalariaTrafficFreight(f.economy,arrival);
 check(pickup&&!pickup->aboard&&!pickup->atPort&&pickup->nextPort()==arrival&&
       pickup->charter.destination==departure&&f.economy.cargoUnits()==units,
       "Remote dispatch exposed the wrong port or loaded freight without a berth");
 checkpoint("arrival");
 f.economy.show(arrival.system,arrival.world);f.economy.view.place=arrival;
 check(f.economy.travelToPort(arrival,f.nav,f.pilot,true),"Phehmyar manual docking request failed");
 const auto berth=*dockingBerth(f.economy.portBody(arrival));
 const double distanceAtRequest=f.pilot.instruments().docking.range;
 check(distanceAtRequest>berth.laneLength(),"Arrival fixture starts inside the approach lane");
 bool approachCaptured=false,finalCaptured=false,armingCaptured=false;
 for(int i=0;i<4000&&!f.economy.dockedPlace();++i){
  const auto t=f.course.instruments();const auto& d=t.docking;
  check(manualDocking(d.phase)&&!t.navigating&&!f.economy.canTrade(arrival),"Manual approach lost its berth or used autopilot");
  if(!approachCaptured&&d.range<berth.laneLength()+220&&d.heading<.15){checkpoint("manual-lane");approachCaptured=true;}
  if(!finalCaptured&&d.range<22&&d.heading<.10){checkpoint("manual-final");finalCaptured=true;}
  if(!armingCaptured&&d.phase==DockingPhase::ManualCapture){checkpoint("manual-clamps");armingCaptured=true;}
  // Fly the same rate and translation axes exposed to the player's helm.
  const auto local=glm::inverse(f.nav.orientation);
  const auto right=local*(berth.orientation*glm::dvec3(1,0,0));
  const auto up=local*(berth.orientation*glm::dvec3(0,1,0));
  const auto attitude=glm::quat_cast(glm::dmat3(right,up,-d.localForward));
  const auto rotation=attitude.w<0?-attitude:attitude;
  const auto error=glm::dvec3(rotation.x,rotation.y,rotation.z)*2.;
  const auto bound=[](double value){return std::clamp(value,-1.,1.);};
  HelmInput input{.pitch=bound(2.*error.x),.yaw=bound(-2.*error.y),.roll=bound(-1.4*error.z)};
  if(d.heading<.16&&std::abs(d.roll)<.20&&d.phase!=DockingPhase::ManualCapture){
   const double forwardSpeed=glm::length(glm::dvec2(d.offset))<std::max(1.,d.offset.z*.05)?
       std::clamp((d.offset.z-.64)*.42,-2.,120.):0.;
   const auto goal=right*std::clamp(-d.offset.x*.55,-16.,16.)+
                   up*std::clamp(-d.offset.y*.55,-16.,16.)+d.localForward*forwardSpeed;
   const auto axes=goal/manualDockingSpeed(d.range);
   input.throttle=bound(-axes.z);input.strafe=bound(axes.x);input.lift=bound(axes.y);
  }
  f.tick(.1,input);
  f.economy.useManualFuel(.1,std::min(1.,std::abs(input.throttle)+std::abs(input.strafe)+std::abs(input.lift)),false);
 }
 check(approachCaptured&&finalCaptured&&armingCaptured,"Manual approach, final or clamp dwell was skipped");
 check(f.economy.dockedPlace()==arrival&&f.economy.canTrade(arrival),"Cargo did not reach a physical Phehmyar berth");
 f.tick(.1,{.throttle=1,.yaw=1});
 check(f.economy.canTrade(arrival)&&f.pilot.instruments().docking.docked,"Helm input released secure clamps");
 const auto stock=f.economy.stock(arrival,good),wallet=f.economy.credits();
 const auto sale=f.economy.quote(arrival,good,units,false);
 f.economy.show(departure.system,departure.world);
 check(f.economy.openCargoTerminal()&&f.economy.view.open&&f.economy.view.cargoTerminal&&
       f.economy.view.place==arrival&&f.economy.view.page==XEconomyView::Page::Market&&
       f.economy.view.good==size_t(good)&&f.economy.view.quantity==units,
       "Terminal did not bind to the current berth and select cargo");
 for(double aspect:{1.5,1.6,2.2}){
  const auto screen=xalariaEconomyOverlay(f.economy,aspect);
  check(cockpitTextConflicts(screen).empty(),"Cargo terminal overlay labels overlap");
 }
 check(f.economy.sell(XGood(f.economy.view.good),f.economy.view.quantity),"Phehmyar cargo terminal sale failed");
 check(f.economy.cargoUnits()==0&&f.economy.stock(arrival,good)==stock+units&&f.economy.credits()==wallet+sale.total,
       "Incorrect sale ledger");
 checkpoint("delivered");
 const auto charter=f.economy.offer(arrival);
 const auto clampedLead=xalariaTrafficFreight(f.economy,arrival);
 check(clampedLead&&clampedLead->charter.id==charter.id&&!clampedLead->aboard&&clampedLead->atPort&&
       xalariaFreightBrief(f.economy,arrival,clampedLead).find("loading bay")!=std::string::npos,
       "A crew at the pickup port did not refer the player to the real loading bay");
 check(f.economy.dockedStationKey().has_value(),"Clamped station has no layout");
 const auto layout=generateStation(*f.economy.dockedStationKey());
 auto display=xalariaLoadingBayDisplay(layout,f.economy,charter,false);
 check(cockpitTextConflicts(display.content).empty()&&
       std::all_of(display.content.atlasGlyphs.begin(),display.content.atlasGlyphs.end(),[&](const auto& glyph){
           return glyph.x0>=0&&glyph.y0>=0&&glyph.x1<=display.size.x&&glyph.y1<=display.size.y;
       }),"Loading bay dispatch text does not fit its display");
 f.economy.show(departure.system,departure.world);
 check(charter.destination==departure&&charter.units>0&&f.economy.collectLoadingBayFreight()&&
       f.economy.contracts().back().origin==arrival&&f.economy.contracts().back().destination==departure&&
       f.economy.cargoUnits()==charter.units,"Eunhia return charter failed to load");
 check(!f.economy.collectLoadingBayFreight()&&f.economy.cargoUnits()==charter.units,"Return charter loaded twice");
 const auto dispatch=xalariaTrafficFreight(f.economy,arrival);
 const auto receiver=xalariaTrafficFreight(f.economy,departure);
 check(dispatch&&receiver&&dispatch->aboard&&receiver->aboard&&dispatch->nextPort()==departure&&
       receiver->charter.id==charter.id&&!receiver->atPort&&f.economy.contracts().size()==1,
       "Radio did not switch the collected charter from pickup to delivery");
 XalariaEconomy restored(f.universe);
 check(restored.restore(f.economy.serialize())&&restored.cargoUnits()==charter.units&&restored.charterClaimed(charter.id),
       "Reload lost the collected freight or its receipt");
 f.economy.show(arrival.system,arrival.world);
 check(!f.economy.acceptContract(),"Onboard terminal duplicated the loading bay charter");
 check(!f.economy.view.cargoTerminal,"Regular economy inherited the cargo terminal");
 Fixture relaunched(f.catalog);
 check(relaunched.nav.restore(f.nav.serialize())&&relaunched.pilot.restore(f.pilot.serialize())&&
       relaunched.economy.restore(f.economy.serialize()),"Could not restore docked freight run");
 relaunched.pilot.synchronizeFrame();
 relaunched.economy.sync(relaunched.nav,relaunched.pilot);
 check(relaunched.economy.travelToPort(arrival,relaunched.nav,relaunched.pilot)&&
       relaunched.pilot.instruments().docking.docked,"Restored freight run did not reattach the clamps");
 relaunched.course.update(0.);
 check(relaunched.course.targetWormhole(),"Return charter had no wormhole to its receiving port");
 relaunched.pilot.launch();
 relaunched.tick();
 check(relaunched.pilot.instruments().docking.phase==DockingPhase::Departure&&
       relaunched.economy.cargoUnits()==charter.units,"Saved berth departure lost the return freight");
 for(int i=0;i<5000&&relaunched.pilot.instruments().docking.active;++i)relaunched.tick();
 check(!relaunched.pilot.instruments().docking.active&&!relaunched.economy.dockedPlace()&&
       relaunched.economy.cargoUnits()==charter.units,"Could not clear Phehmyar with the return freight aboard");
 bool returned=false;
 for(int i=0;i<3200&&!returned;++i){
  returned=relaunched.tick(.1,forward);
  relaunched.economy.useManualFuel(.1,1.,false);
 }
 check(returned&&relaunched.economy.currentSystem()==departure.system&&
       relaunched.economy.cargoUnits()==charter.units&&!relaunched.economy.deliverContracts(),
       "The homebound parcel was lost or paid before docking");
 check(relaunched.economy.travelToPort(departure,relaunched.nav,relaunched.pilot),"Eunhia approach refused the parcel");
 for(int i=0;i<8000&&!relaunched.economy.dockedPlace();++i)relaunched.tick();
 const auto ready=xalariaTrafficFreight(relaunched.economy,departure);
 const auto beforePayout=relaunched.economy.credits();
 check(ready&&ready->aboard&&ready->atPort&&ready->charter.id==charter.id&&
       relaunched.economy.deliverContracts()&&relaunched.economy.credits()==beforePayout+charter.reward&&
       relaunched.economy.cargoUnits()==0&&!relaunched.economy.deliverContracts(),
       "Receiver did not pay the referred charter exactly once at its physical berth");
 XalariaStories employer(relaunched.economy,*relaunched.catalog);
 check(employer.trust(relaunched.economy.faction(charter.origin))>=1&&
       employer.trust(relaunched.economy.faction(charter.destination))>=1,
       "A delivered physical freight charter earns no story-contact trust at its endpoints");
 check(f.pilot.ship().hull>0.&&warfare.wanted(departure.system)==0&&warfare.wanted(arrival.system)==0,"Peaceful cargo run failed with armed traffic active");
 std::cout<<"MANUAL_CARGO origin="<<f.economy.placeName(departure)<<" destination="<<f.economy.placeName(arrival)
          <<" units="<<units<<" good="<<xGoodInfo(good).name<<" paid_cents="<<purchase.total<<" received_cents="<<sale.total
          <<" net_trade_cents="<<sale.total-purchase.total<<" gate_fuel=8 manual_docking=yes remaining_fuel="<<f.economy.fuel()
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
void manualDockingClearance() {
 const DockingBerth berth;
 DockingController controller;
 controller.beginManual();
 const auto good=dockingReadout(berth,{.15,.12,.7},{0,0,-.1},{1,0,0,0});
 const auto hold=[&](const DockingReadout& readout,int ticks){
  for(int i=0;i<ticks;++i)
   if(controller.updateManual(readout,.1))return true;
  return false;
 };
 for(const auto& invalid:{
     dockingReadout(berth,{1.3,0,.7},{},{1,0,0,0}),
     dockingReadout(berth,{0,0,.7},{.8,0,-.1},{1,0,0,0}),
     dockingReadout(berth,{0,0,.7},{0,0,-.8},{1,0,0,0}),
     dockingReadout(berth,{0,0,-1.2},{},{1,0,0,0}),
     dockingReadout(berth,{0,0,.7},{},glm::angleAxis(glm::radians(9.),glm::dvec3(0,1,0))),
     dockingReadout(berth,{0,0,.7},{},glm::angleAxis(glm::radians(9.),glm::dvec3(0,0,1)))})
  check(!hold(invalid,20)&&controller.phase()==DockingPhase::ManualApproach&&controller.capture()==0,
        "Unsafe manual approach armed or closed the clamps");
 check(!hold(good,12)&&controller.phase()==DockingPhase::ManualCapture,"Aligned manual approach failed to arm");
 auto drift=good;drift.offset.y=1.1;
 check(!hold(drift,1)&&controller.capture()==0,"Breaking alignment retained the dwell");
 check(!hold(good,15)&&hold(good,2)&&controller.phase()==DockingPhase::Docked,
       "Clamps did not require uninterrupted alignment");
}
void checkStationTraffic(StationKey key) {
 const DockingBerth berth{};
 std::array<bool,2> arrived{},departed{};
 constexpr double dt=.25;
 auto previous=sampleStationTraffic(key,0.);
 for(double t=dt;t<=96.;t+=dt){
  const auto traffic=sampleStationTraffic(key,t);
  for(size_t i=0;i<traffic.size();++i){
   const auto& ship=traffic[i];
   const auto p=ship.position;
   const double bounds=ship.radius*(ship.kind==ActivityKind::Ferry?1.75:1.64);
   const double laneWidth=berth.halfWidth()*(.65+.35*std::clamp(p.z/berth.laneLength(),0.,1.));
   check(std::abs(p.x)-bounds>laneWidth || p.y-bounds>berth.halfWidth()*.52,
         "Ambient traffic cut across the player docking lane");
   check(std::abs(glm::length(ship.orientation)-1.)<1e-9 &&
         std::abs(glm::dot(previous[i].orientation,ship.orientation))>.99,
         "Station traffic snapped its attitude between phases");
   const auto step=p-previous[i].position;
   check(glm::length(step)<70.*dt,"Station traffic jumped at the end of a circuit");
   if(i<stationTrafficPads.size()){
    const auto toPad=p-stationTrafficPads[i].centre;
    check(toPad.y>0.,"Station traffic went through its landing platform");
    arrived[i]|=std::abs(toPad.z)<.001 && std::abs(toPad.x)<.001 && glm::length(step)<.001;
    departed[i]|=arrived[i] && toPad.z>2. && step.z>0.;
   }
   for(size_t j=0;j<i;++j)
    check(glm::distance(traffic[j].position,p)>1.75*(ship.radius+traffic[j].radius),
          "Station traffic paths crossed at the same time");
  }
  previous=traffic;
 }
 check(std::all_of(departed.begin(),departed.end(),[](bool left){return left;}),
       "A shuttle or cargo tug did not dock and depart again");
}

void checkOrbitalTraffic(StationKey key) {
 constexpr double dt=.5;
 constexpr double throat=1050.;
 auto previous=sampleOrbitalTraffic(key,0.);
 auto previousGate=sampleWormholeTraffic(throat,0.);
 auto previousJunk=sampleSpaceJunk(key,0.);
 check(previous[0].kind==ActivityKind::Freighter && previous[1].kind==ActivityKind::Freighter &&
       previous[2].kind==ActivityKind::Patrol && previous[3].kind==ActivityKind::Ferry,
       "The orbital routes lost their convoy, escort or shuttle");
 for(double seconds=dt;seconds<=360.;seconds+=dt) {
  const auto traffic=sampleOrbitalTraffic(key,seconds);
  const auto gate=sampleWormholeTraffic(throat,seconds);
  const auto junk=sampleSpaceJunk(key,seconds);
  for(size_t i=0;i<traffic.size();++i) {
   const auto& ship=traffic[i];
   check(std::abs(ship.position.x)-ship.radius*1.8>throat*1.5,
         "Orbital traffic entered the central approach corridor");
   check(glm::distance(ship.position,previous[i].position)<365.*dt &&
         std::abs(glm::dot(ship.orientation,previous[i].orientation))>.98,
         "Orbital traffic jumped or snapped its heading");
   for(size_t j=0;j<i;++j)
    check(glm::distance(traffic[j].position,ship.position)>1.8*(traffic[j].radius+ship.radius),
          "Orbital ships overlapped");
  }
  for(size_t i=0;i<gate.size();++i) {
   const auto& ship=gate[i];
   check(std::abs(ship.position.x)-ship.radius*1.8>throat*1.6 &&
         glm::distance(ship.position,previousGate[i].position)<220.*dt &&
         std::abs(glm::dot(ship.orientation,previousGate[i].orientation))>.98,
         "Wormhole traffic entered the aperture or snapped around a turn");
   for(const auto& buoy:wormholeBuoys(throat))
    check(glm::distance(ship.position,buoy)>1.8*ship.radius+24. &&
          std::hypot(buoy.x,buoy.y)-24.>1.45*throat,"Wormhole buoy obstructed traffic");
  }
  for(size_t i=0;i<junk.size();++i) {
   const auto& piece=junk[i];
   check(piece.kind==previousJunk[i].kind && piece.position.y-piece.radius*1.8>2450. &&
         glm::distance(piece.position,previousJunk[i].position)<3.0*dt &&
         std::abs(glm::dot(piece.orientation,previousJunk[i].orientation))>.995,
         "Orbital junk jumped, changed identity, or drifted into traffic");
  }
  previous=traffic;
  previousGate=gate;
  previousJunk=junk;
 }
 for(auto kind:{SpaceJunkKind::Satellite,SpaceJunkKind::Booster,SpaceJunkKind::SolarPanel})
  check(spaceJunkMesh(kind,1).indexCount()>0,"A piece of space junk has no visible mesh");
}

void checkTrafficRadio(const std::shared_ptr<const XalariaCatalog>& catalogue) {
 TrafficHoldProgress hold;
 hold.update(30.,3.,.6);
 check(!hold.matched(),"Rendezvous acknowledged before the hold was steady");
 hold.update(44.,3.,.2);
 hold.update(25.,4.,.55);
 check(!hold.matched(),"Rendezvous ignored a brief excursion from the holding point");
 hold.update(25.,4.,.21);
 check(hold.matched(),"Steady rendezvous did not receive a course match");
 hold.update(61.,9.,.12);
 check(hold.matched(),"Rendezvous acknowledgement flickered on a small correction");
 hold.update(61.,15.,.12);
 check(!hold.matched(),"Rendezvous remained matched at excess relative speed");

 Fixture f(catalogue);
 for(const auto port:{eunhiaStation,phehmyarStation}) {
  f.orbit(port.system,port.world);
  const auto station=*f.economy.orbitalStationId(port);
  const auto berth=*dockingBerth(f.economy.portBody(port));
  const auto key=f.economy.stationDesignKey(station);
  const auto& mouth=f.course.view.wormhole;
  check(mouth.has_value(),"Traffic radio test is missing the wormhole");
  std::set<ActivityId> ids;
  std::set<std::string> names;
  const auto inspect=[&](TrafficService service,std::span<const TrafficCraft> fleet,glm::dvec3 centre,glm::dquat axes) {
   for(size_t i=0;i<fleet.size();++i) {
    const auto body=xalariaTrafficBody(station,service,i,fleet[i],centre,axes);
    check(ids.insert(body.id).second&&names.insert(body.name).second&&isXalariaTraffic(body.id)&&
          !f.economy.orbitalPlace(body.id),"Traffic identity is ambiguous with a crew or berth");
    const auto live=xalariaTrafficContact(f.economy,body.id,mouth,35.);
    const auto later=xalariaTrafficContact(f.economy,body.id,mouth,55.);
    check(live&&later&&live->body.name==later->body.name&&live->port==port&&live->service==service&&
          glm::distance(live->body.position,body.position)<1e-6&&
          glm::distance(live->body.velocity,body.velocity)<1e-6,
          "Radio did not resolve the same procedural craft that was rendered");
    const bool expectedShield=service==TrafficService::Wormhole||body.kind==ActivityKind::Patrol||
                              body.kind==ActivityKind::Survey||(body.kind==ActivityKind::Freighter&&body.radius>=40.);
    check(bool(live->shield.capacity)==expectedShield&&live->shield.raised==expectedShield&&
          (!expectedShield||live->shield.fraction==1.),"Traffic equipment does not match the craft's assignment");
    const auto plan=xalariaTrafficReply(*catalogue,f.economy,*live,TrafficRequest::FlightPlan,0.);
    const auto advisory=xalariaTrafficReply(*catalogue,f.economy,*live,TrafficRequest::Advisory,125.);
    check(plan.find(f.economy.placeName(port))!=std::string::npos&&advisory.find("closing fast")!=std::string::npos,
          "Crew plan or advisory missed the local port or closing speed");
    const auto collar=xalariaVesselApproach(f.economy,body.id,mouth,35.);
    const auto clearance=xalariaTrafficReply(*catalogue,f.economy,*live,TrafficRequest::Dock,0.);
    if(body.radius>=minimumTrafficCollarRadius){
     const auto layout=generateStation(xalariaVesselInterior(body));
     check(collar&&collar->berth->vessel()&&collar->berth->id==body.id&&
           clearance.find("Cleared")!=std::string::npos&&layout.key.vessel&&
           layout.freightBay().floor.contains({layout.freightPad.left,layout.freightPad.front})&&
           layout.freightBay().floor.contains({layout.freightPad.right,layout.freightPad.back}),
           "An eligible crewed craft lacks a usable collar or transfer bay");
    }else check(!collar&&clearance.find("small")!=std::string::npos,"A tiny craft offered boarding");
    const auto rendezvous=xalariaTrafficRendezvous(f.economy,*live);
    check(bool(rendezvous)==(service==TrafficService::Orbital),"Gate or berth craft granted an unsafe rendezvous");
    if(rendezvous) {
     check(glm::distance(rendezvous->metres,body.position-berth.orientation*glm::dvec3(0,900.,0))<1e-6&&
           rendezvous->velocity==body.velocity&&!rendezvous->berth,
           "Rendezvous stopped tracking the live holding point");
    }
   }
  };
  inspect(TrafficService::Port,sampleStationTraffic(key,35.),berth.metres,berth.orientation);
  inspect(TrafficService::Orbital,sampleOrbitalTraffic(key,35.),berth.metres,berth.orientation);
  inspect(TrafficService::Wormhole,sampleWormholeTraffic(mouth->radius,35.),mouth->metres,mouth->orientation);
  check(ids.size()==10&&!isXalariaTraffic(station),"A manned traffic slot was lost");
  for(double aspect:{.7,1.25,1.6,2.4}) {
   XalariaRadioView radio;
   radio.selected=*ids.begin();radio.callsign="Wayfarer-874";radio.activity="Gate tender / SERVICING THE WORMHOLE";
   radio.reply="The pulsing buoys mark the edges. Keep to the opening when crossing to "+catalogue->star(mouth->destination).name+".";
   radio.freight=xalariaTrafficFreight(f.economy,port);
   radio.rendezvous="Course matched  /  24 m to hold  /  4 m/s relative";
   radio.shield=ShieldCharge{}.read(34.,0.,true);
   CockpitOverlay overlay;xalariaRadioOverlay(overlay,radio,aspect);
   check(cockpitTextConflicts(overlay).empty()&&overlay.textRuns.size()>=9,"Radio text or buttons overlap");
  }
 }
}

void checkDispatch(const std::shared_ptr<const XalariaCatalog>& catalogue) {
 Fixture f(catalogue);
 XalariaStories stories(f.economy,*catalogue);
 size_t generated=0;
 for(const auto& [system,star]:catalogue->stars){
  for(unsigned kind=0;kind<unsigned(StoryKind::Count);++kind){
   const auto key=stories.latest(system,StoryKind(kind),0.);
   const auto story=stories.generate(key);
   check(story.key==key&&!story.headline.empty()&&!story.report.empty()&&!story.development.empty()&&
         !story.followUp.empty()&&story.outcomes[0]!=story.outcomes[1],"Invalid procedural dispatch");
   check(!story.world||star.worlds.contains(*story.world),"Dispatch target is outside its system");
   check(!story.port||f.economy.orbitalStationId(*story.port).has_value(),"Dispatch refers to a nonexistent berth");
   if(story.site==StorySite::Vessel)check(stories.vessel(story).has_value(),"Distress lead has no live vessel ID");
   ++generated;
  }
 }
 stories.discover({phehmyarStation.system,0,StoryKind::Distress},StoryOrigin::PreviousJourney);
 stories.show(phehmyarStation.system,0.);
 const auto distress=*stories.selected();
 const auto victim=stories.vessel(distress);
 check(distress.port==phehmyarStation&&victim,"Opening distress dispatch misses Phehmyar");
 const auto contact=xalariaTrafficContact(f.economy,*victim,{},0.);
 check(contact&&xalariaVesselBerth(contact->body)&&distress.headline.find(contact->body.name)!=std::string::npos,
       "Distress dispatch and actual ship identity disagree");
 check(stories.track(distress.key)&&stories.serialize().starts_with("XALARIA_STORIES 6 1"),"Could not pin a lead");
 XalariaStories restored(f.economy,*catalogue);
 check(restored.restore(stories.serialize())&&restored.tracked()==distress.key,"A tracked lead was lost on reload");
 restored.show(phehmyarStation.system,3000.);
 check(restored.selected()&&restored.selected()->key==distress.key&&restored.distressAt(phehmyarStation.system,3000.)==distress.key,
       "Browsing invented a new lead or lost the tracked distress vessel");
 restored.showPage(StoryPage::Journal,3000.);
 check(restored.selected()&&restored.selected()->headline==distress.headline,"The journal resampled a pinned headline");
 StoryContext context{.system=phehmyarStation.system,.orbit=phehmyarStation.world,.boardedStation=phehmyarStation,
                      .boardedVessel=std::nullopt};
 const auto balance=f.economy.credits();
 check(!restored.resolve(distress.key,context,0,f.economy)&&f.economy.credits()==balance,
       "Remote hail or station boarding falsely completed the freighter rescue");
 context.boardedVessel=victim;
 check(restored.resolve(distress.key,context,1,f.economy)&&restored.result(distress.key)==1&&
       f.economy.credits()==balance+64000,"Boarded assistance did not settle the dispatch once");
 check(!restored.resolve(distress.key,context,0,f.economy)&&f.economy.credits()==balance+64000,
       "A resolved dispatch paid out twice");
 const auto receipt=restored.serialize();
 check(!restored.restore(receipt+" invalid")&&restored.serialize()==receipt,"Malformed journal damaged live receipts");
 XalariaStories reloaded(f.economy,*catalogue);
 check(reloaded.restore(receipt)&&reloaded.result(distress.key)==1&&!reloaded.track(distress.key),
       "The saved outcome or payout guard was lost");
 check(reloaded.stage(distress.key,10000.)==StoryStage::FollowUp,"An earlier bulletin did not develop");
 for(const double aspect:{1.,4./3.,16./9.}) {
  reloaded.show(phehmyarStation.system,0.);
  for(size_t row=0;row<reloaded.entries().size();++row){
   reloaded.select(row);
   const auto overlay=xalariaStoryOverlay(reloaded,context,0.,aspect,"Enter  Plot in atlas");
   check(!overlay.atlasGlyphs.empty()&&cockpitTextConflicts(overlay).empty(),"Dispatch text overlaps");
   for(const auto& run:overlay.textRuns)
    check(run.bounds.x>=0&&run.bounds.y>=0&&run.bounds.x+run.bounds.w<=900*aspect+.01&&
          run.bounds.y+run.bounds.h<=900.01,"Dispatch text is outside the viewport");
  }
 }
 std::cout<<"DISPATCH "<<generated<<" seeded leads, journal, physical contact gates and layout checked\n";
}

void checkDispatchFollowUps(const std::shared_ptr<const XalariaCatalog>& catalogue) {
 Fixture f(catalogue);
 XalariaStories stories(f.economy,*catalogue);
 const StoryKey first{phehmyarStation.system,0,StoryKind::Distress};
 const StoryKey review{first.system,first.edition,first.kind,1};
 const StoryKey closing{first.system,first.edition,first.kind,2};
 check(!stories.track(review)&&!stories.track(closing),"Follow-up unlocked before the report was filed");
 check(stories.restore("XALARIA_STORIES 1 0 1 14538 0 0 1"),"Previous release's completed rescue would not load");
 stories.show(first.system,0.);
 stories.showPage(StoryPage::FollowUps,0.);
 check(stories.followUpCount()==1&&stories.selected()&&stories.selected()->key==review&&
       stories.selected()->port==eunhiaStation&&stories.selected()->headline.find("tug")!=std::string::npos,
       "The tow decision did not route the recorder to Eunhia");
 const auto legacy=stories.serialize();
 check(!stories.restore("XALARIA_STORIES 2 0 1 14538 0 0 1 0")&&
       !stories.restore("XALARIA_STORIES 2 1 14538 0 0 2 0")&&stories.serialize()==legacy,
       "A chapter with missing receipts passed save validation");
 check(stories.track(review),"A migrated follow-up could not be tracked");
 const auto beforeReview=stories.serialize();
 StoryContext context{.system=phehmyarStation.system,.orbit=phehmyarStation.world,
                      .boardedStation=phehmyarStation,.boardedVessel=std::nullopt};
 const auto initial=f.economy.credits();
 check(!stories.resolve(review,context,1,f.economy),"Evidence was filed at the case origin instead of Eunhia");
 context.system=eunhiaStation.system;context.orbit=eunhiaStation.world;context.boardedStation.reset();
 check(!stories.resolve(review,context,1,f.economy),"Evidence was filed from the wrong side of the airlock");
 context.boardedStation=eunhiaStation;
 check(stories.resolve(review,context,1,f.economy)&&stories.continuation(review)==closing&&
       f.economy.credits()==initial+42000,"The recorder review did not pay once and unlock the final hearing");
 stories.openReport(closing,3000.);
 check(stories.selected()&&stories.selected()->key==closing&&stories.selected()->headline.find("Public")!=std::string::npos,
       "The public evidence decision did not reach the final hearing");
 XalariaEconomy alternateLedger(f.universe);
 XalariaStories sponsor(alternateLedger,*catalogue);
 check(sponsor.restore(beforeReview)&&sponsor.resolve(review,context,0,alternateLedger),
       "Could not file the same recorder with the sponsor");
 const auto official=sponsor.generate(closing);
 check(official.headline.find("sponsor")!=std::string::npos&&official.system!=stories.selected()->system,
       "Sending the same evidence to another recipient did not change the closing chapter");
 check(stories.track(closing),"Could not track the final hearing");
 XalariaStories restored(f.economy,*catalogue);
 check(restored.restore(stories.serialize())&&restored.tracked()==closing&&restored.generate(closing).system==stories.selected()->system,
       "The pending hearing switched destination on reload");
 const auto locale=[](const XalariaStories& stories,const XalariaStory& story){
  return StoryContext{.system=story.system,.orbit=story.world,.boardedStation=story.port,
                      .boardedVessel=stories.vessel(story),.stellarOrbit=!story.world};
 };
 check(restored.resolve(closing,locale(restored,restored.generate(closing)),0,f.economy)&&
       !restored.continuation(closing)&&!restored.followUpCount(),"The final hearing did not close the case");

 for(unsigned channel=0;channel<unsigned(StoryKind::Count);++channel){
  std::array<std::string,2> reports;
  for(unsigned branch=0;branch<2;++branch){
   XalariaStories arc(f.economy,*catalogue);
   const StoryKey root{first.system,0,StoryKind(channel)};
   arc.discover(root,StoryOrigin::PreviousJourney);
   auto key=root;
   for(unsigned chapter=0;chapter<storyChapterCount;++chapter){
    const auto story=arc.generate(key);
    if(chapter==1)reports[branch]=story.report;
    check(arc.track(key)&&arc.resolve(key,locale(arc,story),branch,f.economy),
          "A generated chapter is unreachable or could not be resolved");
    if(const auto next=arc.continuation(key)){
     const auto handoff=arc.generate(*next);
     const auto path=catalogue->route(story.system,handoff.system);
     check(path.size()==2&&(!handoff.port||f.economy.orbitalStationId(*handoff.port)),
           "A handoff in the linked Ziksa cluster has no navigable destination");
     key=*next;
    }else check(chapter+1==storyChapterCount,"A procedural case stopped early");
   }
  }
  check(reports[0]!=reports[1],"Two choices led to the same follow-up report");
 }

 XalariaStories pages(f.economy,*catalogue);
 for(uint64_t edition=0;edition<9;++edition){
  const StoryKey key{first.system,edition,StoryKind::Science};
  pages.discover(key,StoryOrigin::PreviousJourney);
  check(pages.track(key)&&pages.resolve(key,locale(pages,pages.generate(key)),0,f.economy),"Could not prepare archived reports");
 }
 pages.show(first.system,0.);
 pages.showPage(StoryPage::FollowUps,0.);
 pages.changeSheet(1,0.);
 check(pages.sheetCount()==2&&pages.sheet()==1&&pages.entries().size()==3,"Older follow-ups disappeared from the browser");
 const StoryKey oldest{first.system,0,StoryKind::Science};
 pages.openReport(oldest,0.);
 check(pages.page()==StoryPage::Journal&&pages.sheet()==1&&pages.selected()->key==oldest,
       "Opening an older report did not find its journal page");
 const auto hud=xalariaStoryOverlay(pages,{},0.,4./3.,"Enter  Next chapter");
 check(cockpitTextConflicts(hud).empty(),"The paged journal footer overlaps the report controls");
 std::cout<<"DISPATCH FOLLOW-UPS branching routes, all six channels, migration and paged journal passed\n";
}

void checkStoryLibrary(const std::shared_ptr<const XalariaCatalog>& catalogue) {
 Fixture f(catalogue);
 XalariaStories archive(f.economy,*catalogue);
 archive.show(phehmyarStation.system,0.);
 archive.showPage(StoryPage::Archive,0.);
 check(archive.entries().empty()&&archive.sheetCount()==1,"A fresh player can browse undiscovered outlines");
 std::set<std::string> titles,premises;
 std::set<StoryKey> keys;
 const auto locale=[](const XalariaStories& stories,const XalariaStory& story){
  return StoryContext{.system=story.system,.orbit=story.world,.boardedStation=story.port,
                      .boardedVessel=stories.vessel(story),.stellarOrbit=!story.world};
 };
 for(unsigned page=0;page<storyVariantsPerKind;++page){
  std::vector<XalariaStory> entries;
  for(unsigned kind=0;kind<6;++kind)entries.push_back(archive.generate({phehmyarStation.system,0,StoryKind(kind),0,page+1}));
  for(const auto& story:entries){
   check(titles.insert(story.headline).second&&premises.insert(story.report).second&&keys.insert(story.key).second,
         "The story archive repeats the same incident under another identifier");
   check(story.key.variant==page+1&&story.key.chapter==0&&story.report.find('{')==std::string::npos&&
         !story.decisions[0].empty()&&story.decisions[0]!=story.decisions[1]&&
         story.outcomes[0]!=story.outcomes[1],"The incident library has incomplete or unexpanded content");
   check(!story.port||f.economy.orbitalStationId(*story.port).has_value(),"An archived lead has no valid receiving port");
   std::array<std::string,2> investigations;
   for(unsigned choice=0;choice<2;++choice){
    XalariaStories branch(f.economy,*catalogue);
    branch.discover(story.key,StoryOrigin::PreviousJourney);
    check(branch.track(story.key)&&branch.resolve(story.key,locale(branch,story),choice,f.economy),
          "An archived incident could not be completed at its target");
    const auto next=branch.continuation(story.key);
    check(next&&next->variant==story.key.variant,"An archived case lost its incident identity between chapters");
    const auto second=branch.generate(*next);
    investigations[choice]=second.development;
    check(second.headline.find(story.headline)!=std::string::npos&&
          !catalogue->route(story.system,second.system).empty(),"An archived follow-up lost its origin or route");
    check(branch.track(*next),"The archived continuation could not be tracked");
    XalariaStories restored(f.economy,*catalogue);
    check(restored.restore(branch.serialize())&&restored.tracked()==next&&
          restored.generate(*next).development==second.development&&restored.generate(*next).port==second.port,
          "Reload changed an archived case's task or destination");
    check(!restored.resolve(*next,{},1,f.economy),"An archived continuation paid out away from its site");
    check(restored.resolve(*next,locale(restored,second),1,f.economy),"The archived second chapter could not be filed");
    const auto final=restored.continuation(*next);
    check(final&&final->variant==story.key.variant&&restored.track(*final)&&
          restored.resolve(*final,locale(restored,restored.generate(*final)),0,f.economy)&&
          !restored.continuation(*final),"An archived case failed to close its final chapter");
   }
   check(investigations[0]!=investigations[1],"Different archived decisions produced the same follow-up task");
  }
  // This content fixture explicitly records each outline; gameplay never grants the library.
  for(const auto& story:entries)archive.discover(story.key,StoryOrigin::PreviousJourney);
  archive.showPage(StoryPage::Archive,0.);
  for(const double aspect:{1.,16./9.}){
   for(size_t row=0;row<entries.size();++row){
    archive.select(row);
    const auto hud=xalariaStoryOverlay(archive,{},0.,aspect,"Enter  Plot in atlas");
    check(cockpitTextConflicts(hud).empty(),"The archive page or long incident text overlaps");
    for(const auto& run:hud.textRuns)
     check(run.bounds.x>=0&&run.bounds.y>=0&&run.bounds.x+run.bounds.w<=900*aspect+.01&&
           run.bounds.y+run.bounds.h<=900.01,"The archive text leaves the viewport");
   }
  }
  archive.changeSheet(1,0.);
 }
 check(keys.size()==960&&titles.size()==960&&premises.size()==960,"The library contains fewer than 960 distinct incidents");
 check(!archive.restore("XALARIA_STORIES 3 1 14538 0 0 0 161 0"),"An invalid incident identifier entered a save");
 for(const auto system:{14940u,16134u,16142u}){
  archive.show(system,0.);
  check(archive.entries().empty(),"A remote system revealed leads just because its map was selected");
 }
 std::cout<<"STORY LIBRARY 960 unique incidents, 1920 branches, 5760 chapters, archive layouts and save stability passed\n";
}

void checkStoryDiscovery(const std::shared_ptr<const XalariaCatalog>& catalogue) {
 Fixture f(catalogue);
 XalariaStories stories(f.economy,*catalogue);
 for(const auto page:{StoryPage::Nearby,StoryPage::News,StoryPage::Feed,StoryPage::Archive,
                      StoryPage::FollowUps,StoryPage::Journal}){
  stories.show(eunhiaStation.system,0.);stories.showPage(page,0.);
  check(stories.entries().empty()&&stories.sheetCount()==1&&stories.discoveredCount()==0,
        "A fresh player can see undiscovered stories through a Dispatch tab");
  for(double aspect:{1.,4./3.,16./9.})
   check(cockpitTextConflicts(xalariaStoryOverlay(stories,{},0.,aspect,{})).empty(),
         "The empty discovery screen's instructions overlap");
 }
 stories.show(phehmyarStation.system,0.);
 const auto hidden=stories.latest(phehmyarStation.system,StoryKind::Science,0.);
 check(stories.entries().empty()&&!stories.known(hidden)&&!stories.track(hidden)&&!stories.actionable(hidden),
       "Selecting a remote map system discovers or enables an unknown assignment");
 stories.openReport(hidden,0.);
 check(stories.entries().empty()&&!stories.survey({},0.),"An unknown report or invalid scan reveals a lead");
 StoryContext orbit{.system=eunhiaStation.system,.orbit=eunhiaStation.world};
 const auto scan=stories.survey(orbit,0.);
 check(scan&&stories.discoveredCount()==1&&stories.known(*scan)&&scan->kind==StoryKind::Science&&
       scan->variant<=64&&stories.discovery(*scan)->origin==StoryOrigin::Survey&&
       !stories.survey(orbit,0.)&&!stories.survey(orbit,4000.),
       "A physical survey fails to discover one public lead or can be farmed by repeating it");
 stories.show(eunhiaStation.system,4000.);stories.showPage(StoryPage::Archive,4000.);
 check(stories.entries().size()==1&&stories.sheetCount()==1,
       "Discovering one lead reveals the rest of its channel or the library");
 stories.show(phehmyarStation.system,4000.);stories.showPage(StoryPage::News,4000.);
 check(stories.entries().empty(),"News leaks unrelated autonomous incidents");

 XalariaStories listening(f.economy,*catalogue);
 for(int i=1;i<=190;++i)listening.observe(orbit,i*.1,.1);
 check(listening.discoveredCount()==0,"Arrival instantly reveals local broadcasts");
 for(int i=191;i<=400;++i)listening.observe(orbit,i*.1,.1);
 check(listening.discoveredCount()<=1,"Listening locally reveals the entire incident pool");
 XalariaStories station(f.economy,*catalogue);
 StoryContext aboard=orbit;aboard.boardedStation=eunhiaStation;
 for(int i=1;i<=70;++i)station.observe(aboard,i*.1,.1);
 check(station.discoveredCount()==1&&station.discoveries()[0].origin==StoryOrigin::Station,
       "Crossing the station airlock and meeting its contact does not reveal a local lead");
 for(int i=71;i<=400;++i)station.observe(aboard,i*.1,.1);
 check(std::count_if(station.discoveries().begin(),station.discoveries().end(),[](const auto& item){
        return item.origin==StoryOrigin::Station;})==1,"Station re-entry repeatedly grants public leads");

 const auto first=stories.generate(*scan);
 const auto faction=first.source;
 std::optional<XPlace> host;
 for(const auto& [id,star]:catalogue->stars){
  for(const auto& [world,record]:star.worlds){
   if(record.stations){
    const XPlace port{id,world,0,false};
    if(f.economy.faction(port)==faction){host=port;break;}
   }
  }
  if(host)break;
 }
 check(host.has_value(),"No crew represents the discovered lead's faction");
 const auto ship=*xalariaOrbitalFreighter(f.economy,*host);
 const auto contact=*xalariaTrafficContact(f.economy,ship,{},4000.);
 const auto publicTip=stories.hear(contact,4000.);
 check(publicTip&&stories.trust(faction)==0&&stories.discovery(*publicTip)->origin==StoryOrigin::Conversation,
       "An unknown crew does not offer a public introduction");
 if(!publicTip->live)check(stories.stage(*publicTip,4000.)==StoryStage::FirstReport,
                          "A freshly discovered introduction is already aged into its final report");
 const auto before=stories.discoveredCount();
 for(int i=0;i<100;++i)check(!stories.hear(contact,4000.),"Repeated hails farm new story tips");
 check(stories.discoveredCount()==before,"Radio contact grants extra leads without earning trust");
 const auto site=[&](StoryKey key){
  const auto story=stories.generate(key);
  return StoryContext{story.system,story.world,story.port,stories.vessel(story),!story.world};
 };
 collectCaseEvidence(stories,*scan,4000.);
 check(stories.track(*scan)&&stories.resolve(*scan,site(*scan),0,f.economy)&&stories.trust(faction)==1,
       "Completing discovered work does not earn its sponsor's trust");
 const auto privateTip=stories.hear(contact,4000.);
 check(privateTip&&privateTip->variant>=65&&privateTip->variant<=128&&
       stories.discovery(*privateTip)->origin==StoryOrigin::TrustedContact,
       "Earned reputation fails to unlock a different private lead from the same crew");
 auto key=*stories.continuation(*scan);
 check(stories.known(key)&&stories.track(key)&&stories.resolve(key,site(key),0,f.economy),
       "The first follow-up is not earned through completing its parent");
 key=*stories.continuation(key);
 check(stories.track(key)&&stories.resolve(key,site(key),0,f.economy)&&stories.trust(faction)==2,
       "Completing a case does not earn full sponsor trust");
 const auto confidential=stories.hear(contact,4000.);
 check(confidential&&confidential->variant>=129&&
       stories.discovery(*confidential)->origin==StoryOrigin::TrustedContact,
       "A completed case fails to unlock the trusted contact's confidential work");
 const auto saved=stories.serialize();
 XalariaStories restored(f.economy,*catalogue);
 check(restored.restore(saved)&&restored.serialize()==saved&&restored.trust(faction)==2&&
       !restored.hear(contact,4000.)&&!restored.survey(orbit,4000.)&&
       restored.discoveredCount()==stories.discoveredCount(),
       "Reload forgets discoveries, contact trust, scan history or repeat-hail guards");
 check(!restored.restore(saved+" extra")&&restored.serialize()==saved,
       "Malformed discovery data damages the live journal");
 XalariaStories legacy(f.economy,*catalogue);
 check(legacy.restore("XALARIA_STORIES 4 1 14538 0 5 1 40 0 1 14538 0 5 0 40 0 0 0 0")&&
       legacy.discoveredCount()==1&&legacy.tracked()->chapter==1&&legacy.followUpCount()==1,
       "An old journal loses its active case or unlocks the full library during migration");
 check(legacy.restore("XALARIA_STORIES 5 0 0 0 1 14538 0 3 0 12 0 2 0 0")&&
       legacy.investigation({14538,0,StoryKind::Factions,0,12})&&
       !legacy.evidenceComplete({14538,0,StoryKind::Factions,0,12}),
       "An older unstarted lead does not gain the new investigation stages");
 std::cout<<"STORY DISCOVERY empty start, no atlas/news leaks, physical scans, local encounters, earned contact tiers and persistent discovery history passed\n";
}

void checkPhysicalInvestigations(const std::shared_ptr<const XalariaCatalog>& catalogue) {
 Fixture f(catalogue);
 XalariaStories planner(f.economy,*catalogue);
 std::set<std::pair<unsigned,unsigned>> regions;
 std::set<unsigned> causes;
 size_t plans=0,empty=0;
 for(const auto& [system,star]:catalogue->stars){
  const auto region=catalogue->region(system);regions.emplace(region.x,region.y);
  for(unsigned kind=0;kind<6;++kind){
   const StoryKey key{system,0,StoryKind(kind),0,unsigned((system+kind)%160)+1};
   const auto plan=planner.investigationPlan(key),same=planner.investigationPlan(key);
   const double radius=plan.frame.kind()==Kind::Planet?star.worlds.at(plan.frame.id()).radius:star.radius;
   check(plan.frame==same.frame&&plan.centre==same.centre&&plan.clues==same.clues&&
         glm::length(plan.centre)>radius*1.03,"An investigation moved on regeneration or lies inside its celestial body");
   for(const auto clue:plan.clues)
    check(glm::distance(clue,plan.centre)<plan.searchRadius&&glm::length(clue)>radius*1.03,
          "Physical evidence lies outside its search area or inside a planet/star");
   causes.insert(unsigned(plan.cause));++plans;empty+=star.worlds.empty();
  }
 }
 check(plans==catalogue->stars.size()*6&&regions.size()==82&&causes.size()==3&&empty>0,
       "Investigations do not cover every channel, region and empty star system");
 for(unsigned step=0;step<3;++step)check(investigationMesh(step,2).indexCount()>0,"An evidence prop has no physical mesh");
 bool publicConsequence=false,contradiction=false;
 for(unsigned kind=0;kind<6;++kind){
  XalariaEconomy ledger(f.universe),otherLedger(f.universe);
  XalariaStories story(ledger,*catalogue),other(otherLedger,*catalogue);
  const StoryKey key{phehmyarStation.system,0,StoryKind(kind),0,77+kind};
  const auto base=story.generate(key);
  const auto plan=story.investigationPlan(key);
  StoryContext site{base.system,base.world,base.port,story.vessel(base),!base.world};
  check(!story.investigationWaypoint(key)&&story.evidenceBodies(plan.frame).empty(),
        "An undiscovered investigation reveals physical targets");
  check(story.discover(key,StoryOrigin::Conversation)&&story.track(key),"Could not begin a discovered investigation");
  check(!story.resolve(key,site,0,ledger)&&!story.ready(story.generate(key),site)&&
        story.evidenceNotes(key)[2].starts_with("No recorder"),"A rumour can be filed or reveals its uncollected recorder's finding");
  check(!story.scanEvidence(site,plan.frame,plan.centre+glm::dvec3(6100.,0,0),{},10.),
        "A remote scan skips the physical search");
  check(story.scanEvidence(site,plan.frame,plan.centre,{},10.)&&story.investigation(key)->evidence==1,
        "The search scan did not fix a bearing");
  const auto props=story.evidenceBodies(plan.frame);
  check(props.size()>=2&&props[0].position==plan.clues[0]&&isInvestigationEvidence(props[0].id)&&
        !isXalariaTraffic(props[0].id),"The rendered evidence and its interaction location disagree");
  check(!story.scanEvidence(site,plan.frame,plan.clues[0],{26,0,0},20.)&&
        !story.scanEvidence(site,plan.frame,plan.clues[0]+glm::dvec3(181,0,0),{},20.)&&
        story.scanEvidence(site,plan.frame,plan.clues[0],{},20.),"The fragment's range or speed gate is wrong");
  const auto partial=story.serialize();
  check(other.restore(partial)&&other.serialize()==partial&&other.investigation(key)->evidence==3&&
        other.investigationPlan(key).clues==plan.clues,"Reload changes the trail or loses collected evidence");
  check(!other.restore(partial+" BAD")&&other.serialize()==partial,"Invalid evidence data damages a live case");
  check(!story.scanEvidence(site,plan.frame,plan.clues[1],{9,0,0},30.)&&
        !story.scanEvidence(site,plan.frame,plan.clues[1]+glm::dvec3(76,0,0),{},30.)&&
        story.scanEvidence(site,plan.frame,plan.clues[1],{},30.),"Recorder recovery ignores its physical range or closing speed");
  check(story.investigation(key)->evidence==7&&!story.ready(story.generate(key),site),
        "The recorder bypasses the independent-account stage");
  const auto after=story.evidenceBodies(plan.frame);
  check(std::none_of(after.begin(),after.end(),[](const auto& body){return (body.id.slot&3)==1;}),
        "A recovered recorder remains outside the ship");
  StoryContext wrong{.system=base.system,.orbit=base.world,.stellarOrbit=!base.world};
  if(base.site!=StorySite::Survey)
   check(!story.scanEvidence(wrong,plan.frame,plan.clues[2],{},40.),"A witness is interviewed from outside their airlock");
  check(story.scanEvidence(site,plan.frame,plan.clues[2],{},40.)&&story.evidenceComplete(key)&&
        story.ready(story.generate(key),site),"Physical evidence and the independent account do not unlock the finding");
  contradiction|=plan.claim!=plan.cause;
  const auto full=story.serialize();
  check(other.restore(full),"Could not prepare the alternate finding");
  const auto source=base.source;
  check(story.resolve(key,site,0,ledger)&&other.resolve(key,site,1,otherLedger)&&
        otherLedger.credits()>ledger.credits()&&!story.resolve(key,site,0,ledger),
        "Evidence choices do not affect rewards or can pay twice");
  if(source!=XFaction::Independent&&plan.cause!=InvestigationCause::Failure){
   check(story.factionStanding(source)>other.factionStanding(source)&&other.trust(XFaction::Independent)>=1,
         "Public evidence has no faction consequences");
   story.advance(490.);other.advance(490.);
   const XPlace port=base.port.value_or(XPlace{base.system,*base.world,0,false});
   check(other.marketFactor(port,XGood::Nanomites)>story.marketFactor(port,XGood::Nanomites),
         "Publishing interference has no local supply consequences");
   if(base.port){
    const auto station=*ledger.orbitalStationId(*base.port);
    check(story.trafficClock(station,490.).first!=other.trafficClock(station,490.).first&&
          other.trafficAdvisory(*base.port,490.).starts_with("INDEPENDENT EVIDENCE"),
          "The independent finding does not change physical convoy schedules or advisories");
   }
   publicConsequence=true;
  }
  story.show(key.system,500.);story.showPage(StoryPage::Archive,500.);story.evidenceOpen=true;
  for(double aspect:{1.,4./3.,16./9.})
   check(cockpitTextConflicts(xalariaStoryOverlay(story,site,500.,aspect,{})).empty(),
         "The case board's evidence or conflicting accounts overlap");
 }
 check(contradiction&&publicConsequence,"Procedural investigations lack conflicting accounts or meaningful disclosure choices");
 std::cout<<"PHYSICAL INVESTIGATIONS "<<plans<<" plans across 82 regions; all channels, empty systems, stable props, range/speed gates, witnesses, saves and consequences passed\n";
}

void checkLivingGalaxy(const std::shared_ptr<const XalariaCatalog>& catalogue) {
 Fixture f(catalogue);
 XalariaStories stories(f.economy,*catalogue);
 f.economy.setStories(&stories);
 const auto contains=[](const auto& list,StoryKey key){
  return std::any_of(list.begin(),list.end(),[&](const auto& item){return item.key==key;});
 };
 std::optional<StoryKey> supplyCase,failedCase;
 uint32_t busySystem=0;
 bool quiet=false,hub=false,success=false,failure=false,propagated=false;
 for(const auto& [system,star]:catalogue->stars){
  const auto active=stories.nearby(system,0.);
  const size_t cap=10+5*(stories.activityLevel(system)-1);
  check(active.size()<=cap,"Local incident density exceeds its hub budget");
  unsigned urgent=0;
  for(const auto& entry:active){
   const auto route=catalogue->route(system,entry.key.system);
   check(!route.empty()&&route.size()==entry.hops+1&&entry.hops<=3,
         "A nearby incident lies outside its advertised jump radius");
   const auto state=stories.incident(entry.key,0.);
   check(!stories.incident(entry.key,state.started-.1).urgent,
         "An incident transmits an urgent beacon before it starts");
   urgent+=state.urgent;
   check(!state.closed()&&state.started<=0.,"A closed or future incident entered the local leads");
   const auto ending=stories.incident(entry.key,state.endsAt+.01);
   success|=ending.phase==IncidentPhase::Completed;
   failure|=ending.phase==IncidentPhase::Failed;
   const auto story=stories.generate(entry.key);
   if(ending.phase==IncidentPhase::Failed&&!failedCase){
    XalariaEconomy probeLedger(f.universe);
    XalariaStories probe(probeLedger,*catalogue);
    probe.advance(std::max(0.,state.responseAt+1.));
    const StoryContext site{story.system,story.world,story.port,probe.vessel(story),!story.world};
    probe.discover(entry.key,StoryOrigin::Signal);
    collectCaseEvidence(probe,entry.key,std::max(0.,state.responseAt+1.));
    if(probe.track(entry.key)&&probe.resolve(entry.key,site,0,probeLedger)&&
       probe.incident(entry.key,state.endsAt+1.).phase==IncidentPhase::Completed)failedCase=entry.key;
   }
   if(!supplyCase&&story.port&&(entry.key.kind==StoryKind::Disaster||entry.key.kind==StoryKind::Conflict))
    supplyCase=entry.key;
  }
  check(urgent<=2,"The local incident pool overwhelms the player with urgent leads");
  quiet|=stories.activityLevel(system)==1&&active.size()==10;
  hub|=stories.activityLevel(system)==3&&active.size()==20;
  if(stories.activityLevel(system)==3&&active.size()==20)busySystem=system;
  if(!propagated&&!star.wormholes.empty()){
   for(double time=300.;time<1100.&&!propagated;time+=100.){
    for(const auto& entry:stories.news(system,time)){
     if(entry.hops)continue;
     const auto state=stories.incident(entry.key,time);
     const auto neighbour=star.wormholes.front();
     const auto before=stories.news(neighbour,state.decisionAt+1.);
     const auto after=stories.news(neighbour,state.decisionAt+121.);
     if(contains(stories.news(system,state.decisionAt+1.),entry.key)&&contains(after,entry.key)){
      check(!contains(before,entry.key),"News appeared next door before the report travelled there");
      propagated=true;break;
     }
    }
   }
  }
  if(quiet&&hub&&success&&failure&&supplyCase&&failedCase&&propagated)break;
 }
 check(quiet&&hub&&success&&failure&&supplyCase&&failedCase&&propagated,
       "The galaxy lacks density variation, autonomous outcomes or delayed neighbouring news");
 for(double time=0.;time<=5400.;time+=150.)
  check(stories.nearby(busySystem,time).size()>=10,
        "A busy neighbourhood runs out of incidents between scheduling cycles");
 const auto key=*failedCase;
 const auto schedule=stories.incident(key,0.);
 check(stories.incident(key,schedule.responseAt+.1).phase==IncidentPhase::Responding&&
       stories.incident(key,schedule.decisionAt+.1).phase==IncidentPhase::Escalated&&
       stories.incident(key,schedule.endsAt+.1).phase==IncidentPhase::Failed,
       "An untracked rescue or dispute does not progress to an outcome");
 const auto wallet=f.economy.credits();
 stories.advance(schedule.endsAt+1.);
 check(!stories.track(key)&&!stories.result(key)&&f.economy.credits()==wallet,
       "A closed NPC response became a playable lead or rewarded the player");
 const auto failedReport=stories.incidentReport(key,schedule.endsAt+1.);
 stories.show(eunhiaStation.system,schedule.endsAt+100.);
 check(stories.incidentReport(key,schedule.endsAt+1.)==failedReport,
       "Travelling away resampled an autonomous incident outcome");

 XalariaStories assisted(f.economy,*catalogue);
 const auto story=assisted.generate(key);
 const StoryContext site{story.system,story.world,story.port,assisted.vessel(story),!story.world};
 const double intervention=std::max(0.,schedule.responseAt+1.);
 assisted.advance(intervention);
 assisted.discover(key,StoryOrigin::Signal);
 collectCaseEvidence(assisted,key,intervention);
 check(assisted.track(key)&&!assisted.resolve(key,{},0,f.economy)&&
       assisted.resolve(key,site,0,f.economy),"The living incident bypasses or blocks its physical assignment");
 const auto saved=assisted.serialize();
 XalariaStories resumed(f.economy,*catalogue);
 check(resumed.restore(saved)&&resumed.serialize()==saved&&
       resumed.incident(key,intervention-.1).phase==stories.incident(key,intervention-.1).phase&&
       resumed.incident(key,schedule.endsAt+1.).phase==assisted.incident(key,schedule.endsAt+1.).phase&&
       resumed.incident(key,schedule.endsAt+1.).phase==IncidentPhase::Completed&&
       !resumed.resolve(key,site,1,f.economy),"Reload lost intervention timing or duplicated a reward");
 check(!resumed.restore(saved+" extra")&&resumed.serialize()==saved&&
       !resumed.restore("XALARIA_STORIES 4 0 0 -1"),"Malformed incident clocks changed a saved case");
 check(resumed.restore("XALARIA_STORIES 3 1 14538 0 5 0 40 0"),
       "An earlier variant-based tracked assignment cannot be migrated");

 const auto supply=stories.generate(*supplyCase);
 const auto state=stories.incident(*supplyCase,0.);
 const auto station=*f.economy.orbitalStationId(*supply.port);
 const double time=state.responseAt+(state.endsAt-state.responseAt)*.36;
 stories.advance(time);
 const auto quote=f.economy.quote(*supply.port,XGood::Nanomites,1,true);
 f.economy.setStories(nullptr);
 const auto base=f.economy.quote(*supply.port,XGood::Nanomites,1,true);
 f.economy.setStories(&stories);
 check(quote.total>base.total&&stories.marketFactor(*supply.port,XGood::Nanomites)>1.&&
       !stories.trafficAdvisory(*supply.port,state.responseAt+1.).empty(),
       "Relief or conflict has no effect on supply prices or flight advisories");
 const auto clock=stories.trafficClock(station,time);
 const double epsilon=.0001;
 const double rate=(stories.trafficClock(station,time+epsilon).first-
                    stories.trafficClock(station,time-epsilon).first)/(2.*epsilon);
 check(std::abs(clock.first-time)>.01&&clock.second>.2&&std::abs(clock.second-rate)<1e-5,
       "Incident traffic timing is inert, reverses ships or disagrees with velocity");
 for(const double edge:{state.responseAt,state.endsAt}){
  const auto before=stories.trafficClock(station,edge-epsilon);
  const auto after=stories.trafficClock(station,edge+epsilon);
  check(std::abs((after.first-before.first)-2.*epsilon)<1e-6&&std::abs(before.second-after.second)<1e-5,
        "An incident response teleports traffic at its boundary");
 }
 const auto vessel=*xalariaOrbitalFreighter(f.economy,*supply.port);
 const auto contact=*xalariaTrafficContact(f.economy,vessel,{},time);
 const auto end=*xalariaTrafficContact(f.economy,vessel,{},time+.02);
 const auto traffic=nearbyXalariaTraffic(f.universe,f.economy,vessel.home,contact.body.position,{},
                                     supply.system,nullptr,time,.02);
 const auto collider=std::find_if(traffic.begin(),traffic.end(),[&](const auto& motion){return motion.start.id==vessel;});
 check(collider!=traffic.end()&&glm::distance(collider->start.position,contact.body.position)<1e-6&&
       glm::distance(collider->end,end.body.position)<1e-6&&std::isfinite(glm::length(contact.body.velocity)),
       "A responding convoy's radio target and collision body follow different routes");

 stories.advance(0.);
 for(const auto& entry:stories.nearby(phehmyarStation.system,0.))stories.discover(entry.key,StoryOrigin::Signal);
 stories.show(phehmyarStation.system,0.);
 stories.showPage(StoryPage::Nearby,0.);
 check(stories.entries().size()<=5&&!stories.entries().empty(),"Nearby does not limit immediate leads to five");
 const auto pinned=stories.entries().back().key;
 stories.select(stories.entries().size()-1);stories.refresh(1.);
 check(stories.selected()->key==pinned,"A live refresh moves the selected lead under the player");
 std::set<StoryKey> browsed;
 for(size_t page=0;page<stories.sheetCount();++page){
  for(const auto& entry:stories.entries())check(browsed.insert(entry.key).second,"Nearby repeats a lead across pages");
  stories.changeSheet(1,1.);
 }
 check(browsed.size()==stories.nearbyCount(),"The wider incident pool cannot be browsed five leads at a time");
 stories.nextPage(1.);
 check(stories.page()==StoryPage::News&&stories.row==0,"Changing feeds retains the previous feed's selection");
 for(const auto page:{StoryPage::Nearby,StoryPage::News})for(double aspect:{1.,4./3.,16./9.}){
  stories.showPage(page,1000.);
  for(size_t sheet=0;sheet<stories.sheetCount();++sheet){
   for(size_t row=0;row<stories.entries().size();++row){
    stories.select(row);
    check(cockpitTextConflicts(xalariaStoryOverlay(stories,{},1000.,aspect,{})).empty(),
          "Nearby or delayed news overlaps its tabs, reports or response controls");
   }
   stories.changeSheet(1,1000.);
  }
 }
 std::cout<<"LIVING GALAXY hub density, urgent budget, NPC outcomes, delayed news, intervention clocks, prices and continuous collision-safe convoy routes passed\n";
}

void checkShipTrail(const std::shared_ptr<const XalariaCatalog>& catalogue) {
 ShipTrail trail;
 const auto home=catalogue->address(phehmyarStation.system).child(Kind::Planet,phehmyarStation.world);
 const auto observe=[&](double seconds,glm::dvec3 at){
  trail.observe({home,{at-glm::dvec3(1,0,0),at+glm::dvec3(1,0,0)},seconds,.8f});
 };
 for(int i=0;i<=1440;++i)observe(i/240.,{0,0,double(i)});
 check(trail.points().size()>26&&trail.points().size()<50&&trail.points().back().seconds==6.&&
       trail.points().back().exhaust[0].z==1440.,"Trail history lost its live exhaust tip or grew with FPS");
 const auto live=trail.points().size();
 trail.cut();
 observe(6.02,{70000.,0.,0.});
 check(trail.points().size()==live+1&&trail.points().back().begins&&trail.points().front().begins,
       "An interruption joined a new trail to the previous flight path");
 observe(7.,{70000.,0.,4.});
 check(trail.points().back().begins,"A gap in tracking left a line across unseen positions");
 trail.expire(22.);
 check(!trail.points().empty()&&trail.points().front().seconds>5.,"Age filtering removed the recent wake or retained the old wake");
 trail.expire(25.);
 check(trail.points().empty(),"The wake failed to fade after the ship left");
}

void checkTrafficCollision(const std::shared_ptr<const XalariaCatalog>& catalogue) {
 Fixture f(catalogue);
 f.orbit(phehmyarStation.system,phehmyarStation.world);
 const auto quay=*dockingBerth(f.economy.portBody(phehmyarStation));
 const auto key=f.economy.stationDesignKey(quay.id);
 const auto fleet=sampleOrbitalTraffic(key,0.);
 const auto id=xalariaTrafficBody(quay.id,TrafficService::Orbital,0,fleet[0],quay.metres,quay.orientation).id;
 const auto scheduled=[&](double seconds){
  const auto fleet=sampleOrbitalTraffic(key,seconds);
  return xalariaTrafficBody(quay.id,TrafficService::Orbital,0,fleet[0],quay.metres,quay.orientation);
 };
 const auto contact=[&]{
  return xalariaTrafficContact(f.economy,id,f.course.view.wormhole,f.pilot.ship().flightSeconds)->body;
 };
 const auto position=[&](const ActivityBody& ship,glm::dvec3 point,glm::dvec3 speed){
  f.nav.changeFrame(ship.id.home);
  f.nav.setRenderScale(3.);
  f.nav.orientation=ship.orientation;
  const auto eye=ship.position+ship.orientation*point;
  f.nav.offset=eye/1000.-f.nav.orientation*glm::dvec3(0,0,1);
  f.pilot.synchronizeFrame();f.pilot.resetMotion();
  f.pilot.setVelocity(ship.velocity+ship.orientation*speed);
 };
 const auto localEye=[&](const ActivityBody& ship){
  const auto pose=f.universe.relative(f.nav.anchor,ship.id.home,0.);
  return glm::conjugate(ship.orientation)*(pose.position+pose.orientation*
    (f.nav.cameraPosition()*std::pow(10.,f.nav.logDistance))-ship.position);
 };

 auto ship=contact();
 TrafficMotion passing{ship,ship.position+ship.orientation*glm::dvec3(0,140,0),ship.orientation};
 const auto held=ship.position+ship.orientation*glm::dvec3(0,70,0);
 const auto sweep=sweepXalariaTraffic(f.universe,id.home,std::span(&passing,1),held,held,
                                      ship.orientation,ship.orientation,2.,0.,1.,.05);
 check(sweep&&sweep->fraction>0.&&sweep->fraction<1.&&glm::dot(sweep->surfaceVelocity,sweep->normal)>100.,
       "Fast moving traffic crossed a stationary player between frame samples");

 // The approved berth is to the side of the freighter's solid hull.
 position(ship,{vesselCollarCentre(ship.radius)+6.8,0.,160.},{0,0,-7200.});
 f.tick(.05);
 check(f.pilot.trafficContact().empty()&&f.pilot.ship().hull==100.&&localEye(contact()).z<-135.,
       "The hull collider obstructed a clear pass along the docking lane");

 ship=contact();
 position(ship,{0.,.23*ship.radius+2.11,0.},{0.,-1.9,0.});
 for(int i=0;i<16&&f.pilot.trafficStrikes().empty();++i)f.tick(.02);
 check(!f.pilot.trafficStrikes().empty()&&!f.course.trafficWaveOff()&&f.pilot.ship().hull==100.,
       "A light touch did not produce a harmless traffic strike");
 const auto touch=f.pilot.ship().flightSeconds;
 const auto atTouch=contact();
 const auto sameStart=scheduled(touch);
 const auto glance=xalariaTrafficContact(f.economy,id,f.course.view.wormhole,touch+.6);
 const auto laterSchedule=scheduled(touch+.6);
 check(glm::distance(atTouch.position,sameStart.position)<1e-5&&glm::distance(atTouch.velocity,sameStart.velocity)>.08&&
       glance&&glm::distance(glance->body.position,laterSchedule.position)>.02&&glance->dockingHold==0.,
       "The other ship did not drift smoothly after a glancing hit");
 const auto raised= f.pilot.shield();
 const auto targetShield=f.economy.trafficShield(atTouch,touch);
 check(raised.raised&&raised.fraction<1.&&raised.pulse>0.&&targetShield.raised&&targetShield.fraction<1.&&targetShield.pulse>0.,
       "The impact did not spend shield charge and light both fields");
 Pilot resumed(f.universe,f.nav,f.controller);
 XalariaEconomy resumedTraffic(f.universe);
 check(resumed.restore(f.pilot.serialize())&&std::abs(resumed.shield().fraction-raised.fraction)<1e-9&&
       resumedTraffic.restoreTraffic(f.economy.serializeTraffic(touch),touch)&&
       std::abs(resumedTraffic.trafficShield(atTouch,touch).fraction-targetShield.fraction)<1e-9,
       "Loading the session refilled either ship's shield");
 position(atTouch,{260.,250.,235.},{0.,0.,0.});
 for(int i=0;i<63;++i)f.tick(.1);
 check(f.pilot.shield().fraction==1.&&f.economy.trafficShield(contact(),f.pilot.ship().flightSeconds).fraction==1.&&
       glm::distance(contact().position,scheduled(f.pilot.ship().flightSeconds).position)<1e-5,
       "The glancing response did not settle and recharge on the scheduled route");

 // Drive sideways into the physical collar while manual docking is armed.
 ship=contact();
 const double collar=vesselCollarCentre(ship.radius);
 position(ship,{collar+5.5,0.,0.},{-34.,0,0});
 const auto waypoint=xalariaVesselApproach(f.economy,id,f.course.view.wormhole,f.pilot.ship().flightSeconds);
 check(waypoint&&f.pilot.dockManually(*waypoint),"Could not arm the manual contact fixture");
 check(!f.pilot.shield().raised,"The player field stayed up after receiving docking clearance");
 for(int i=0;i<20&&f.pilot.trafficContact().empty();++i){
  f.pilot.trackWaypoint(*xalariaVesselApproach(f.economy,id,f.course.view.wormhole,f.pilot.ship().flightSeconds));
  f.tick(.02,{.strafe=-1.});
 }
 check(f.pilot.trafficContact()==ship.name&&f.pilot.ship().hull<100.&&
       !f.pilot.instruments().docking.active&&!f.pilot.clampedHarbour()&&localEye(contact()).x>collar+2.,
       "Side impact penetrated the collar or left the clamps armed");
 const double impactTime=f.pilot.ship().flightSeconds;
 const auto recovering=xalariaTrafficContact(f.economy,id,f.course.view.wormhole,impactTime);
 check(f.course.trafficWaveOff()==id&&recovering&&recovering->dockingHold>8.&&
       xalariaTrafficReply(*catalogue,f.economy,*recovering,TrafficRequest::Dock,0.).starts_with("Stand clear")&&
       !xalariaVesselApproach(f.economy,id,f.course.view.wormhole,impactTime),
       "A collar strike did not call off the approach and suspend docking");
 check(recovering->shield.fraction==1.&&recovering->shield.pulse==0.,
       "The other vessel's lowered shield absorbed a collar strike");
 const auto impaired=xalariaTrafficContact(f.economy,id,f.course.view.wormhole,impactTime+1.);
 check(impaired&&glm::distance(impaired->body.position,scheduled(impactTime+1.).position)>1.,
       "The freighter stayed on its procedural path after a hard hit");
 check(!f.economy.reactToTrafficStrike(f.pilot.trafficStrikes().front(),impactTime+.02),
       "A second impact restarted the radio alert while the crew was checking the collar");
 const auto save=f.economy.serializeTraffic(impactTime+1.);
 XalariaEconomy restored(f.universe);
 check(restored.restoreTraffic(save,impactTime+1.)&&restored.serializeTraffic(impactTime+1.)==save&&
       restored.trafficDockingHold(id,impactTime+1.)>7.&&
       !restored.restoreTraffic(save+" EXTRA",impactTime+1.)&&restored.serializeTraffic(impactTime+1.)==save,
       "Reload lost the clearance hold or damaged recovery after a malformed save");
 const auto sampled=xalariaTrafficContact(f.economy,id,f.course.view.wormhole,impactTime+1.);
 const auto motions=nearbyXalariaTraffic(f.universe,f.economy,id.home,sampled->body.position,{},phehmyarStation.system,
                                       &*f.course.view.wormhole,impactTime+1.,.04);
 const auto collider=std::find_if(motions.begin(),motions.end(),[&](const auto& item){return item.start.id==id;});
 check(collider!=motions.end()&&glm::distance(collider->start.position,sampled->body.position)<1e-6&&
       std::abs(glm::dot(collider->start.orientation,sampled->body.orientation))>.999999,
       "The recovering ship's collider did not follow its visible flight path");

 for(int i=0;i<96;++i)f.tick(.1);
 const auto fresh=xalariaVesselApproach(f.economy,id,f.course.view.wormhole,f.pilot.ship().flightSeconds);
 check(fresh&&f.economy.serializeTraffic(f.pilot.ship().flightSeconds).empty(),
       "The collar stayed closed after the freighter recovered");
 check(f.pilot.dockManually(*fresh)&&f.pilot.dock(),"Fresh clearance did not start another approach");
 check(f.pilot.instruments().docking.range>100.,"The test did not have to reacquire the recovering freighter");
 for(int i=0;i<2800&&!f.pilot.clampedHarbour();++i){
  const auto point=xalariaVesselApproach(f.economy,id,f.course.view.wormhole,f.pilot.ship().flightSeconds);
  check(point.has_value(),"Recovery disabled the renewed approach");
  f.pilot.trackWaypoint(*point);
  f.tick(.05);
 }
 const auto boarded=xalariaTrafficContact(f.economy,id,f.course.view.wormhole,f.pilot.ship().flightSeconds);
 check(f.pilot.clampedHarbour()&&*f.pilot.clampedHarbour()==id&&boarded&&f.economy.collectTrafficFreight(*boarded,f.pilot)&&
       f.economy.contracts().back().destination==phehmyarStation,
       "After the collar inspection the player could not collect the same freighter's parcel");

 ship=contact();
 position(ship,{0.,67.,0.},{0,-26500.,0});
 f.tick(.05);
 const auto struck=contact();
 const auto transform=f.universe.relative(f.nav.anchor,struck.id.home,0.).orientation;
 check(f.pilot.trafficContact()==struck.name&&f.pilot.ship().hull==0.&&localEye(struck).y>ship.radius*.23&&
       glm::dot(transform*f.pilot.ship().velocity-struck.velocity,struck.orientation*glm::dvec3(0,1,0))>=0.&&
       glm::length(f.pilot.instruments().contactImpulse)>0.,
       "High-speed impact tunneled through the freighter or omitted the impact response");
 std::cout<<"TRAFFIC_RECOVERY glancing drift, hard strike wave-off, restored hold, renewed docking and parcel passed\n";
}

void checkVesselBoarding(const std::shared_ptr<const XalariaCatalog>& catalogue,
                        const std::filesystem::path& captures) {
 Fixture f(catalogue);
 f.orbit(phehmyarStation.system,phehmyarStation.world);
 const auto station=*f.economy.orbitalStationId(phehmyarStation);
 const auto quay=*dockingBerth(f.economy.portBody(phehmyarStation));
 const auto key=f.economy.stationDesignKey(station);
 const auto fleet=sampleOrbitalTraffic(key,0.);
 auto id=xalariaTrafficBody(station,TrafficService::Orbital,0,fleet[0],quay.metres,quay.orientation).id;
 const auto locate=[&]{return *xalariaVesselApproach(f.economy,id,f.course.view.wormhole,f.pilot.ship().flightSeconds);};
 const auto checkpoint=[&](std::string_view name,std::string_view mode){
  if(captures.empty())return;
  const auto folder=captures/name;
  std::filesystem::create_directories(folder);
  const auto port="VESSEL "+id.home.str()+" "+std::to_string(id.slot)+" "+std::string(mode);
  std::ofstream out(folder/"session.txt");
  out<<"XALARIA_SESSION 1 "<<catalogue->fingerprint<<' '
     <<std::quoted(f.nav.serialize())<<' '<<std::quoted(f.pilot.serialize())<<' '
     <<std::quoted(f.course.serialize())<<' '<<std::quoted(f.economy.serialize())<<' '<<std::quoted(port)<<'\n';
  out.close();check(bool(out),"Could not save ship boarding capture");
 };
 const auto start=[&](glm::dvec3 offset){
  const auto point=locate();
  f.nav.changeFrame(point.reference);
  f.nav.setRenderScale(4.);
  f.nav.orientation=point.berth->orientation;
  const auto eye=point.metres+f.nav.orientation*offset;
  f.nav.offset=eye/10000.-f.nav.orientation*glm::dvec3(0,0,1);
  f.pilot.synchronizeFrame();f.pilot.resetMotion();f.pilot.setVelocity(point.velocity);
  check(f.pilot.dockManually(point),"Could not request the moving berth");
 };
 const auto advance=[&](const HelmInput& input={}){
  f.pilot.trackWaypoint(locate());
  f.tick(.05,input);
  if(f.pilot.clampedHarbour()){
   f.pilot.trackWaypoint(locate());
   f.pilot.alignMovingClamp();
  }
 };
 const auto contact=*xalariaTrafficContact(f.economy,id,f.course.view.wormhole,0.);
 const auto parcel=xalariaVesselParcel(f.economy,contact);
 check(!f.economy.collectTrafficFreight(contact,f.pilot),"Traffic freight was awarded in flight");
 const auto lane=*locate().berth;
 const auto approach=dockingLane(lane,31.,DockingPhase::ManualApproach,0.,1.f);
 const auto arming=dockingLane(lane,31.,DockingPhase::ManualCapture,.5,1.f);
 const auto secure=dockingLane(lane,31.,DockingPhase::Docked,1.,1.f);
 const auto sameLane=[&](const Mesh& lights){
  return approach.indices==lights.indices&&approach.vertices.size()==lights.vertices.size()&&
         std::equal(approach.vertices.begin(),approach.vertices.end(),lights.vertices.begin(),
                    [](const auto& a,const auto& b){return a.x==b.x&&a.y==b.y&&a.z==b.z;});
 };
 check(!approach.vertices.empty()&&sameLane(arming)&&sameLane(secure)&&
       approach.vertices.back().color!=arming.vertices.back().color&&arming.vertices.back().color!=secure.vertices.back().color,
       "The moving approach lane changes geometry or snaps to the clamp colour");
 start({2.,-1.,33.});
 bool armed=false;
 for(int i=0;i<3600&&!f.pilot.clampedHarbour();++i){
  f.pilot.trackWaypoint(locate());
  const auto d=f.pilot.instruments().docking;
  const auto berth=*locate().berth;
  const auto local=glm::inverse(f.nav.orientation);
  const auto right=local*(berth.orientation*glm::dvec3(1,0,0));
  const auto up=local*(berth.orientation*glm::dvec3(0,1,0));
  auto attitude=glm::quat_cast(glm::dmat3(right,up,-d.localForward));
  if(attitude.w<0)attitude=-attitude;
  const auto bound=[](double x){return std::clamp(x,-1.,1.);};
  HelmInput input{.pitch=bound(4.*attitude.x),.yaw=bound(-4.*attitude.y),.roll=bound(-2.8*attitude.z)};
  armed|=d.phase==DockingPhase::ManualCapture;
  if(d.phase!=DockingPhase::ManualCapture){
   const auto goal=right*(-d.offset.x*.65)+up*(-d.offset.y*.65)+d.localForward*((d.offset.z-.63)*.48);
   const auto axes=goal/manualDockingSpeed(d.range);
   input.throttle=bound(-axes.z);input.strafe=bound(axes.x);input.lift=bound(axes.y);
  }
  advance(input);
 }
 const auto manual=f.pilot.instruments().docking;
 if(!f.pilot.clampedHarbour())
  std::cerr<<"Vessel manual "<<manual.range<<" m, "<<manual.relativeSpeed<<" m/s, "
           <<manual.heading<<" rad, offset "<<manual.offset.x<<' '<<manual.offset.y<<' '<<manual.offset.z<<'\n';
 check(armed&&f.pilot.clampedHarbour()&&*f.pilot.clampedHarbour()==id,"Manual controls failed to capture the moving collar");
 const auto capturedAt=locate().metres;
 for(int i=0;i<60;++i)advance({.throttle=1.,.roll=1.});
 const auto moving=locate();
 const auto eyeInFrame=f.universe.relative(f.nav.anchor,moving.reference,0.);
 const auto position=eyeInFrame.position+eyeInFrame.orientation*
                     (f.nav.cameraPosition()*std::pow(10.,f.nav.logDistance));
 check(glm::distance(capturedAt,moving.metres)>12.&&glm::distance(position,moving.metres)<.01,
       "Secure clamps slipped as the vessel moved");
 checkpoint("vessel-clamped","clamped");
 check(f.economy.collectTrafficFreight(contact,f.pilot)&&f.economy.cargoUnits()==parcel.units&&
       !f.economy.canTrade(phehmyarStation)&&!f.economy.deliverContracts()&&
       !f.economy.collectTrafficFreight(contact,f.pilot),"Courier parcel was missing, duplicated or paid before landing");
 XalariaEconomy restored(f.universe);
 check(restored.restore(f.economy.serialize())&&restored.cargoUnits()==parcel.units&&restored.charterClaimed(parcel.id),
       "Reload lost the vessel parcel or pickup receipt");
 const auto depart=[&](){
  f.pilot.launch();
  check(f.pilot.instruments().docking.phase==DockingPhase::Departure,"The moving berth did not release the clamps");
  for(int i=0;i<2400&&f.pilot.instruments().docking.active;++i)advance();
  check(!f.pilot.instruments().docking.active,"Departure did not clear the moving collar");
  f.pilot.releaseWaypointControl();
 };
 depart();
 start({8.,4.,85.});
 checkpoint("vessel-manual","manual");
 check(f.pilot.dock(),"Automatic approach refused a moving collar");
 for(int i=0;i<2400&&!f.pilot.clampedHarbour();++i)advance();
 const auto automatic=f.pilot.instruments().docking;
 if(!f.pilot.clampedHarbour())
  std::cerr<<"Vessel automatic "<<dockingPhaseName(automatic.phase)<<' '<<automatic.range<<" m, "
           <<automatic.relativeSpeed<<" m/s, "<<automatic.heading<<" rad\n";
 check(f.pilot.clampedHarbour()&&*f.pilot.clampedHarbour()==id,"Automatic docking missed the moving collar");
 depart();
 check(f.economy.travelToPort(phehmyarStation,f.nav,f.pilot),"Phehmyar Station refused the courier approach");
 for(int i=0;i<8000&&!f.economy.dockedPlace();++i)f.tick();
 const auto credits=f.economy.credits();
 check(f.economy.canTrade(phehmyarStation)&&f.economy.deliverContracts()&&
       f.economy.credits()==credits+parcel.reward&&f.economy.cargoUnits()==0&&!f.economy.deliverContracts(),
       "Phehmyar did not pay the ship-to-station courier delivery exactly once");
 const auto automaticallyDockFleet=[&](TrafficService service,std::span<const TrafficCraft> traffic,
                                       glm::dvec3 centre,glm::dquat orientation){
  for(size_t i=0;i<traffic.size();++i){
   const auto body=xalariaTrafficBody(station,service,i,traffic[i],centre,orientation);
   if(!xalariaVesselBerth(body))continue;
   id=body.id;
   start({12.,5.,94.});
   check(f.pilot.dock(),"Automatic docking request refused for "+body.name);
   for(int frame=0;frame<2600&&!f.pilot.clampedHarbour();++frame)advance();
   const auto d=f.pilot.instruments().docking;
   if(!f.pilot.clampedHarbour())
    std::cerr<<"Fleet docking "<<body.name<<' '<<body.status<<' '<<dockingPhaseName(d.phase)<<' '
             <<d.range<<" m, "<<d.relativeSpeed<<" m/s, "<<d.heading<<" rad\n";
   check(f.pilot.clampedHarbour()&&*f.pilot.clampedHarbour()==id,"Automatic docking missed "+body.name);
  }
 };
 automaticallyDockFleet(TrafficService::Port,sampleStationTraffic(key,0.),quay.metres,quay.orientation);
 automaticallyDockFleet(TrafficService::Orbital,fleet,quay.metres,quay.orientation);
 const auto& mouth=*f.course.view.wormhole;
 automaticallyDockFleet(TrafficService::Wormhole,sampleWormholeTraffic(mouth.radius,0.),mouth.metres,mouth.orientation);
 std::cout<<"VESSEL_BOARDING manual and automatic clamps, moving deck and Phehmyar parcel delivery passed\n";
}

void checkModularStations(const std::shared_ptr<const XalariaCatalog>& c) {
 const auto signFits=[](const CabinDisplay& sign) {
  return !sign.content.panels.vertices.empty() && !sign.content.textRuns.empty() &&
         !sign.content.atlasGlyphs.empty() &&
         std::all_of(sign.content.textRuns.begin(),sign.content.textRuns.end(),[&](const auto& text){
          const auto& box=text.bounds;
          return !text.value.ends_with("...") && box.x>=0. && box.y>=0. &&
                 box.x+box.w<=sign.size.x && box.y+box.h<=sign.size.y;
         });
 };
 check(std::all_of(xalariaShipSigns().begin(),xalariaShipSigns().end(),signFits) &&
       std::all_of(xalariaAirlockSigns().begin(),xalariaAirlockSigns().end(),signFits),
       "A ship or airlock sign clipped its lettering");
 Fixture f(c);
 std::array<unsigned,4> roles{};
 std::set<uint64_t> uniqueStationSeeds;
 unsigned count=0;
 std::array<std::optional<StationKey>,4> samples;
 for(const auto& [system,star]:c->stars)for(const auto& [world,record]:star.worlds)
  for(unsigned n=0;n<record.stations;++n){
   const XPlace port{system,world,n,false};
   const auto id=f.economy.orbitalStationId(port);
   check(id&&f.economy.orbitalPlace(*id)==port,"Published station ID failed to round trip");
   const auto key=f.economy.stationDesignKey(*id);
   check(uniqueStationSeeds.insert(key.seed).second,"Two published stations received the same key");
   const auto plan=generateStation(key);
   check(plan.modules.front().kind==StationModuleKind::Airlock&&plan.freightBay().kind==StationModuleKind::Freight&&
         plan.modules.front().floor.right==stationDeck::shipDoor,"Station plan lost its airlock or loading bay");
   double next=stationDeck::shipDoor;
   for(const auto& room:plan.modules){
    check(room.floor.right==next&&room.floor.right>room.floor.left&&room.floor.front<stationDeck::centre&&
          room.floor.back>stationDeck::centre&&room.ceiling>.85,"Disconnected or invalid station module");
    next=room.floor.left;
   }
   check(plan.annexes.size()==(key.role==StationRole::Research?2:1),"Station lost its role-specific side rooms");
   for(const auto& annex:plan.annexes){
    const auto& host=plan.modules.at(annex.parent).floor;
    const auto& room=annex.room.floor;
    check((annex.side==1||annex.side==-1)&&annex.insideWall()==(annex.side>0?host.back:host.front)&&
          room.left>=host.left&&room.right<=host.right&&room.left<annex.doorwayLeft&&
          annex.doorwayLeft<annex.doorwayRight&&annex.doorwayRight<room.right&&
          plan.roomAt({(room.left+room.right)*.5,(room.front+room.back)*.5})==&annex.room,
          "Station side room does not match its corridor doorway");
    const double aisle=(annex.doorwayLeft+annex.doorwayRight)*.5;
    for(const auto& fixture:annex.room.fixtures)
     check(room.contains({fixture.left,fixture.front})&&room.contains({fixture.right,fixture.back})&&
           (fixture.right<aisle-.23||fixture.left>aisle+.23),
           "Generated furnishing obstructs the side-room aisle or extends outside the room");
   }
   ++roles[size_t(key.role)];++count;
   samples[size_t(key.role)]=key;
  }
 check(count==37441&&std::all_of(roles.begin(),roles.end(),[](unsigned n){return n>0;}),
       "Published station coverage or architecture families changed");
 std::set<size_t> silhouetteSizes;
 for(const auto& key:samples){
  checkStationTraffic(*key);
  checkOrbitalTraffic(*key);
  const auto plan=generateStation(*key),replayed=generateStation(*key);
  check(plan.freightPad==replayed.freightPad&&plan.modules.size()==replayed.modules.size()&&
        std::equal(plan.modules.begin(),plan.modules.end(),replayed.modules.begin(),[](const auto& a,const auto& b){
         return a.kind==b.kind&&a.floor==b.floor&&a.ceiling==b.ceiling&&a.fixtures==b.fixtures;
        }),"Station modules changed when generated again");
  const auto hull=modularStationHull(plan,2);
  const double aisleStart=plan.freightBay().floor.left+.12;
  const auto aisleRay=dockingBerthOffset+glm::dvec3(aisleStart,.42,stationDeck::centre)/orbitalStationRadius;
  const double aisleSpan=(stationDeck::shipDoor+.12-aisleStart)/orbitalStationRadius;
  const auto hullPosition=[&](size_t index){
   const auto& v=hull.vertices[hull.index(index)];
   return glm::dvec3(v.x,v.y,v.z);
  };
  for (size_t i=0;i<hull.indexCount();i+=3){
   const auto a=hullPosition(i),b=hullPosition(i+1),c=hullPosition(i+2);
   const auto n=glm::cross(b-a,c-a);
   if (std::abs(n.x)<1e-15) continue;
   const double x=glm::dot(n,a-aisleRay)/n.x;
   if (x<0. || x>aisleSpan) continue;
   const auto hit=aisleRay+glm::dvec3(x,0,0);
   const auto side=[&](glm::dvec3 u,glm::dvec3 v){return glm::dot(glm::cross(v-u,hit-u),n);};
   check(std::min({side(a,b),side(b,c),side(c,a)})<0.,
         "Station exterior blocks the view along the docking bridge or main aisle");
  }
  std::set<uint32_t> floorFinishes;
  for(const auto look:{XalariaLook::CharredGlass,XalariaLook::VerdantFog,XalariaLook::TidalBlue,XalariaLook::AcidEtch}){
   const auto cabin=xalariaDockingDeck(plan,look);
   check(cabin.indexCount()>0,"Station interior recipe produced no geometry");
   floorFinishes.insert(cabin.vertices.front().color);
  }
  check(floorFinishes.size()==4,"Station interior reused the same floor finish across environments");
  const auto lighting=xalariaStationAnnexLighting(plan);
  for(size_t i=0;i<lighting.size()/6;++i){
   const auto& room=lighting[i*6];
   if(i>=plan.annexes.size()){
    check(room==glm::vec4(0.f),"Empty annex emitted a lighting record");
    continue;
   }
   const auto& annex=plan.annexes[i];
   const auto& task=lighting[i*6+2];
   check(room.x==float(annex.room.floor.left)&&room.z==float(annex.room.floor.right),
         "Annex lighting bounds did not match the room");
   if(annex.panoramic()||annex.room.kind==StationModuleKind::Analysis){
    const auto& furniture=annex.room.fixtures[annex.panoramic()?2:0];
    check(task.w>=0.&&furniture.contains({task.x,task.z-task.w})&&
          furniture.contains({task.x,task.z+task.w})&&task.y<annex.room.ceiling,
          "Task light did not line up with its furniture");
   }else{
    check(task.w<0.,"Annex without a task lamp emitted a task light");
   }
  }
  check(plan.annexes.size()==replayed.annexes.size()&&
        std::equal(plan.annexes.begin(),plan.annexes.end(),replayed.annexes.begin(),[](const auto& a,const auto& b){
         return a.parent==b.parent&&a.side==b.side&&a.room.kind==b.room.kind&&a.room.floor==b.room.floor&&
                a.room.ceiling==b.room.ceiling&&a.room.fixtures==b.room.fixtures&&
                a.doorwayLeft==b.doorwayLeft&&a.doorwayRight==b.doorwayRight;
        }),"Side rooms changed when generated again");
  const auto glazing=stationGlazing(plan);
  const auto apertures=xalariaStationWindows(plan);
  const auto sections=std::count_if(plan.modules.begin(),plan.modules.end(),[](const auto& room){
   return room.kind==StationModuleKind::Bridge;
  });
  const auto signs=xalariaStationSigns(plan);
  check(signs.size()==2*(plan.modules.size()-sections-1)+4*plan.annexes.size() &&
        std::all_of(signs.begin(),signs.end(),signFits),
        "Generated station lost a directional plaque or clipped its lettering");
  const auto windows=std::count_if(plan.annexes.begin(),plan.annexes.end(),[](const auto& annex){return annex.panoramic();});
  const auto& firstBridge=plan.modules.at(1);
  const auto profile=bridgeGlassProfile(firstBridge);
  const glm::dvec3 eye=dockingBerthOffset+glm::dvec3(
      (firstBridge.floor.left+firstBridge.floor.right)*.5,.405,stationDeck::centre)/orbitalStationRadius;
  const auto nearestSurface=[&](glm::dvec3 origin,glm::dvec3 ray){
   const Vertex* surface=nullptr;
   double nearest=std::numeric_limits<double>::infinity();
   const auto point=[&](size_t index){
    const auto& vertex=hull.vertices[hull.index(index)];
    return glm::dvec3(vertex.x,vertex.y,vertex.z);
   };
   for(size_t i=0;i<hull.indexCount();i+=3){
    const auto a=point(i),b=point(i+1),c=point(i+2);
    const auto normal=glm::cross(b-a,c-a);
    const double toward=glm::dot(normal,ray);
    if(std::abs(toward)<1e-12)continue;
    const double distance=glm::dot(normal,a-origin)/toward;
    if(distance<=0.||distance>=nearest)continue;
    const auto hit=origin+ray*distance;
    if(glm::dot(glm::cross(b-a,hit-a),normal)>=0.&&
       glm::dot(glm::cross(c-b,hit-b),normal)>=0.&&
       glm::dot(glm::cross(a-c,hit-c),normal)>=0.){
     nearest=distance;
     surface=&hull.vertices[hull.index(i)];
    }
   }
   return surface;
  };
  check(!nearestSurface(eye,{0.,-.32,1.})&&nearestSurface(eye,{0.,-.32,-1.}),
        "Bridge did not overlook open space outboard and the service apron inboard");
  const auto* catwalk=nearestSurface(eye,{0.,-.67,-1.});
  check(catwalk && (catwalk->color>>24)<192,"Inboard bridge did not overlook exterior service hardware");
  for (const auto& pad : stationTrafficPads) {
   const auto above=dockingBerthOffset+(pad.centre+glm::dvec3(.25,2.,.70))/orbitalStationRadius;
   const auto* deck=nearestSurface(above,{0.,-1.,0.});
   check(deck && (deck->color>>24)<192,"Auxiliary berth backing intersected the visible deck plate");
  }
  for (const auto& annex : plan.annexes) {
   if (!annex.panoramic()) continue;
   const auto& room=annex.room.floor;
   const auto lookout=dockingBerthOffset+glm::dvec3((room.left+room.right)*.5,.405,
       annex.outsideWall()-annex.side*1.11)/orbitalStationRadius;
   check(!nearestSurface(lookout,{0.,0.,double(annex.side)}),"Lounge supports obstructed the horizon through its windows");
  }
  check(apertures[0].x==float(firstBridge.floor.right-sections*stationDeck::ribPitch)&&
        apertures[0].y==float(firstBridge.floor.right)&&
        apertures[2].x==float(profile.front().x)&&apertures[2].y==float(profile[1].x)&&
        apertures[2].z==float(profile[profile.size()/2].x)&&apertures[2].w==float(stationDeck::bridgeRibWidth*.5),
        "Light apertures did not follow the docking bridge");
  check((apertures[1].y>apertures[1].x)==(windows>0),"Lounge glazing and light aperture disagreed");
  check(glazing.indexCount()==(size_t(sections)*(profile.size()-1)+size_t(windows))*6&&
        std::all_of(glazing.vertices.begin(),glazing.vertices.end(),[&](const auto& vertex){
         return vertex.x<=stationDeck::bridgeDoor+.0001&&vertex.x>plan.freightBay().floor.left&&
                vertex.y>stationDeck::floor;
        }),"Docking tunnel or observation lounge lost its glazing");
  check(hull.indexCount()>0&&
        std::all_of(hull.vertices.begin(),hull.vertices.end(),[](const auto& p){
         return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
        }),"Station kit produced missing or invalid geometry");
  silhouetteSizes.insert(hull.indexCount());
 }
 check(silhouetteSizes.size()==samples.size(),"Architecture families reused the same exterior recipe");
 std::cout<<"MODULAR_STATIONS published="<<count<<" roles="<<roles[0]<<','<<roles[1]<<','<<roles[2]<<','<<roles[3]<<'\n';
}
void checkCrowdedOrbit(const std::shared_ptr<const XalariaCatalog>& c,const std::filesystem::path& captures) {
 Fixture f(c);
 const XPlace publicPort{11750,8,4,false}; // Skomyu has five published stations.
 f.dock(publicPort);
 const auto publicKey=f.economy.dockedStationKey();
 check(publicKey.has_value(),"Fifth public berth has no boarding layout");
 f.orbit(publicPort.system,publicPort.world);
 XStock materials{};
 materials[size_t(XGood::Nanomites)]=7;
 materials[size_t(XGood::Iron)]=40;
 f.provision(materials);
 check(f.economy.seedAsset(XAssetKind::Vault)&&f.economy.feedAsset(1),"Could not build the vault in a crowded orbit");
 const XPlace vault{publicPort.system,publicPort.world,100001,false};
 check(glm::length(f.economy.portBody(vault).position-f.economy.portBody(publicPort).position)>1000.,
       "A private vault shares a public station's orbit position");
 f.dock(vault);
 check(f.economy.dockedPlace()==vault&&f.economy.dockedStationKey()&&f.economy.dockedStationKey()!=publicKey&&
       f.economy.dockedStationKey()->role==StationRole::Vault,"Docking selected the wrong station in a crowded orbit");
 if(!captures.empty()){
  const auto folder=captures/"crowded-vault";
  std::filesystem::create_directories(folder);
  std::ostringstream port;
  port<<c->fingerprint<<' '<<vault.system<<' '<<vault.world<<' '<<vault.station<<" 0";
  std::ofstream out(folder/"session.txt");
  out<<"XALARIA_SESSION 1 "<<c->fingerprint<<' '
     <<std::quoted(f.nav.serialize())<<' '<<std::quoted(f.pilot.serialize())<<' '
     <<std::quoted(f.course.serialize())<<' '<<std::quoted(f.economy.serialize())<<' '<<std::quoted(port.str())<<'\n';
 }
}
int main(int argc,char** argv){try{
 check(argc==2||argc==3,"Catalogue required");auto c=std::make_shared<XalariaCatalog>(argv[1]);
 const auto captures=argc==3?std::filesystem::path(argv[2]):std::filesystem::path{};
 checkTrafficRadio(c);
 checkDispatch(c);
 checkDispatchFollowUps(c);
 checkStoryLibrary(c);
 checkStoryDiscovery(c);
 checkPhysicalInvestigations(c);
 checkLivingGalaxy(c);
 checkShipTrail(c);
 checkTrafficCollision(c);
 checkVesselBoarding(c,captures);
 checkModularStations(c);
 checkCrowdedOrbit(c,captures);
 manualDockingClearance();
 manualCargoRun(c,captures);
 manualWormholeClearance(c);
 Fixture f(c);
 XPlace home{14940,0,0,false};f.orbit(home.system,home.world);
 auto before=f.economy.serialize();check(!f.economy.buy(XGood::Iron,1),"Remote purchase succeeded");check(!f.economy.sell(XGood::Iron,1),"Remote sale succeeded");check(before==f.economy.serialize(),"Rejected remote transaction mutated state");
 f.dock(home);check(!f.economy.buy(XGood::Iron,0)&&!f.economy.buy(XGood::Iron,-1),"Invalid purchase accepted");
 const auto credits=f.economy.credits(),stock=f.economy.stock(home,XGood::Iron);const auto price=f.economy.quote(home,XGood::Iron,10,true);
 check(f.economy.buy(XGood::Iron,10),"Valid purchase failed");check(f.economy.credits()==credits-price.total&&f.economy.cargoUnits()==10&&f.economy.stock(home,XGood::Iron)==stock-10,"Purchase did not conserve cargo or credits");
 {
  const auto checkDisplayFit=[](const auto& displays) {
   for(const auto& display:displays){
    const auto& screen=display.content;
    check(!screen.atlasGlyphs.empty()&&cockpitTextConflicts(screen).empty(),"Deck display labels overlap");
    check(std::abs(glm::length(display.model[0])-glm::length(display.model[1]))<1e-6f,
          "Deck display text is stretched");
    for(const auto& glyph:screen.atlasGlyphs)
     check(glyph.x0>=6&&glyph.y0>=4&&glyph.x1<=display.size.x-6&&glyph.y1<=display.size.y-6,
           "Deck display glyph crosses the screen gutter");
   }
  };
  const auto hasText=[](const CabinDisplay& display,std::string_view value) {
   return std::any_of(display.content.textRuns.begin(),display.content.textRuns.end(),
                      [&](const auto& run){return run.value==value;});
  };
  auto map=f.course.view;
  FlightInstruments instruments;
  const auto loaded=xalariaDeckDisplays(instruments,map,f.economy);
  checkDisplayFit(loaded);
  check(hasText(loaded[2],"10 / 30")&&hasText(loaded[2],"20")&&hasText(loaded[2],"IRON")&&
        hasText(loaded[2],"E Market / B Case files"),
        "Manifest should reflect the loaded cargo and remaining capacity");
  instruments.hull=8;
  map.destination=14538;
  map.activeRoute={14940,14538};
  map.range=9.8e15;
  map.paused=true;
  XalariaEconomy empty(f.universe);
  const auto paused=xalariaDeckDisplays(instruments,map,empty);
  checkDisplayFit(paused);
  check(hasText(paused[0],c->star(map.destination).name)&&hasText(paused[0],"Paused")&&
        hasText(paused[1],"Attention")&&hasText(paused[2],"No freight on board")&&
        hasText(paused[2],"B Case files / Dock to trade"),
        "Deck displays should reflect a paused route, damaged hull, and empty hold");
  instruments.targetLabel="WORMHOLE TO "+c->star(map.destination).name;
  instruments.targetVisible=true;
  instruments.localTarget={1,1,1};
  instruments.range=map.range;
  instruments.docking.phase=DockingPhase::Departure;
  const auto cockpit=xalariaCockpitDisplays(instruments,396.8,400);
  checkDisplayFit(cockpit);
  check(hasText(cockpit[1],"396.8 / 400")&&hasText(cockpit[2],"Aft")&&
        hasText(cockpit[3],"Departing"),"Flight displays lost target or docking status");
 }
 const auto sale=f.economy.quote(home,XGood::Iron,10,false);check(sale.total<price.total,"Local buy/sell creates free money");
 check(f.economy.sell(XGood::Iron,10),"Sale failed");check(f.economy.cargoUnits()==0&&f.economy.stock(home,XGood::Iron)==stock&&f.economy.credits()<credits,"Round trip changed stock or generated money");
 check(!f.economy.buy(XGood::Iron,31),"Cargo overflow accepted");check(!f.economy.sell(XGood::Count,1),"Invalid good accepted");
 auto offered=f.economy.offer(home);check(offered.units>0,"No source charter");
 check(f.economy.collectLoadingBayFreight()&&f.economy.contracts().back().destination==offered.destination,
       "Eunhia loading bay did not use its own charter");
 const auto sealed=f.economy.cargoUnits();
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
 check(b.economy.dockedStationKey()&&b.economy.dockedStationKey()->role==StationRole::Vault,"Built vault has no boarding layout");
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
