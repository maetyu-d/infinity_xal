#include "infinity/XalariaCombat.hpp"
#include "infinity/XalariaNavigation.hpp"
#include "infinity/XalariaStories.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/ShipInterior.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <iomanip>
using namespace infinity;
void check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
struct Fixture {
 std::shared_ptr<const XalariaCatalog> catalog;Universe universe;Navigation nav;RegionCache cache;FlightController controller;Pilot pilot;XalariaEconomy economy;XalariaNavigation course;XalariaCombat combat;
 explicit Fixture(std::shared_ptr<const XalariaCatalog> c):catalog(c),universe(0x58414c4152494101ULL,c),nav(universe),cache(universe),controller(universe,nav,cache),pilot(universe,nav,controller),economy(universe),course(universe,nav,pilot),combat(universe,economy,pilot){economy.setCombat(&combat);course.setEconomy(&economy);}
 CombatPlayer player()const{CombatPlayer p;p.frame=nav.anchor;p.position=nav.cameraPosition()*std::pow(10.,nav.logDistance);p.orientation=nav.orientation;p.forward=nav.orientation*glm::dvec3(0,0,-1);p.velocity=pilot.ship().velocity;p.seconds=pilot.ship().flightSeconds;
  p.sheltered=pilot.instruments().docking.docked||pilot.ship().landed;p.transit=pilot.instruments().warp!=WarpPhase::Off||pilot.ship().cruise||pilot.ship().shrinking;p.weaponsInhibited=pilot.instruments().docking.active;
  p.gate=course.view.wormhole;if(pilot.clampedHarbour())p.clamped=*pilot.clampedHarbour();return p;}
 bool tick(double dt=.1,const HelmInput& input={}){
  if(combat.convoys().following&&!pilot.instruments().docking.active&&pilot.instruments().warp==WarpPhase::Off){
   if(auto point=combat.convoys().waypoint(player())){
    if(point->label=="CONVOY WORMHOLE CROSSING"&&combat.convoys().tracked()){
     const auto& route=combat.convoys().plan(*combat.convoys().tracked()).route;const auto here=std::find(route.begin(),route.end(),economy.currentSystem());
     if(here!=route.end()&&here+1!=route.end()&&(!course.view.wormhole||course.view.wormhole->destination!=*(here+1)))course.selectSystem(*(here+1));
    }
    if(!pilot.instruments().navigating)pilot.navigateWaypoint(*point);else pilot.trackWaypoint(*point);
   }
  }
  combat.advance(player(),dt);cache.beginFrame();const bool crossed=course.advanceFlight(input,dt);economy.advance(dt);economy.sync(nav,pilot);cache.finishFrame();return crossed;
 }
 void dock(XPlace p){pilot.brake();pilot.clearWaypoint();const auto berth=*dockingBerth(economy.portBody(p));nav.changeFrame(berth.id.home);nav.setRenderScale(3.);nav.orientation=berth.orientation;nav.offset=berth.metres/1000.-nav.orientation*glm::dvec3(0,0,1);pilot.synchronizeFrame();pilot.resetMotion();economy.sync(nav,pilot);economy.show(p.system,p.world);economy.view.place=p;
  check(economy.travelToPort(p,nav,pilot),"Docking request failed");for(int i=0;i<100&&!economy.dockedPlace();++i)tick();check(economy.canTrade(p),"Physical docking failed");}
 bool shoot(ActivityId id,CombatWeapon weapon){const auto c=xalariaTrafficContact(economy,id,player().gate,pilot.ship().flightSeconds);if(!c||c->body.disabled)return false;
  const auto at=universe.pointRelative(c->body.id.home,c->body.position/std::pow(10.,universe.describe(c->body.id.home).frame.logRadius),nav.anchor,0.);
  const auto eye=player().position;const auto direction=glm::normalize(at-eye);nav.orientation=glm::quatLookAtRH(direction,std::abs(direction.y)>.99?glm::dvec3(0,0,1):glm::dvec3(0,1,0));nav.offset=eye/std::pow(10.,nav.logDistance)-nav.orientation*glm::dvec3(0,0,1);
  combat.armed=true;combat.weapon=weapon;combat.target=id;return combat.fire(player());
 }
 void capture(const std::filesystem::path& root,std::string name){if(root.empty())return;const auto folder=root/name;std::filesystem::create_directories(folder);std::ostringstream port;
  if(pilot.clampedHarbour()&&isXalariaTraffic(*pilot.clampedHarbour()))port<<"VESSEL "<<pilot.clampedHarbour()->home.str()<<' '<<pilot.clampedHarbour()->slot<<" clamped";
  else if(economy.dockedPlace()){const auto p=*economy.dockedPlace();port<<catalog->fingerprint<<' '<<p.system<<' '<<p.world<<' '<<p.station<<" 0";}
  std::ofstream out(folder/"session.txt");XalariaStories stories(economy,*catalog);
  out<<"XALARIA_SESSION 4 "<<catalog->fingerprint<<' '<<std::quoted(nav.serialize())<<' '<<std::quoted(pilot.serialize())<<' '<<std::quoted(course.serialize())<<' '<<std::quoted(economy.serialize())<<' '<<std::quoted(port.str())<<' '<<std::quoted(economy.serializeTraffic(pilot.ship().flightSeconds))<<' '<<std::quoted(stories.serialize())<<' '<<std::quoted(combat.serialize())<<' '<<std::quoted(combat.convoys().serialize());
 }
};
void universeCoverage(std::shared_ptr<const XalariaCatalog> cat){
 Fixture f(cat);const auto& plans=f.combat.convoys().plans();std::set<unsigned> regions;std::set<uint32_t> systems;unsigned withoutWorlds=0,clashes=0;
 check(f.combat.convoys().known().empty(),"Fresh Operations leaks undiscovered convoys");
 for(const auto& [service,p]:plans){
  const auto reg=cat->region(service);regions.insert(reg.x+20*reg.y);check(p.origin!=p.destination||p.route.size()==1,"Freight loops back despite alternative ports");
  for(size_t i=0;i<p.route.size();++i){systems.insert(p.route[i]);if(cat->star(p.route[i]).worlds.empty())++withoutWorlds;if(i)check(std::find(cat->star(p.route[i-1]).wormholes.begin(),cat->star(p.route[i-1]).wormholes.end(),p.route[i])!=cat->star(p.route[i-1]).wormholes.end(),"Convoy invented a wormhole edge");}
  ConvoyKey key{service,0};const auto time=f.combat.convoys().departure(key)+52.;const auto stage=f.combat.convoys().stage(key,time);
  const auto actor=f.combat.convoys().id(key,0,stage.frame);const auto c=f.combat.convoys().contact(actor,time);
  check(c&&c->body.kind==ActivityKind::Freighter&&xalariaVesselBerth(c->body),"Generated convoy is not a real boardable traffic body");
  check(f.combat.convoys().validIdentity(actor),"Convoy identity failed its route-frame validation");clashes+=f.combat.convoys().factionBattle(key);
 }
 check(regions.size()==82&&plans.size()>6000&&clashes>1000&&withoutWorlds>0,"Convoy generation misses populated regions, faction battles or worldless transit");
 const auto& route=plans.at(eunhiaStation.system);check(route.origin==eunhiaStation&&route.destination==phehmyarStation&&route.good==XGood::Nickel&&route.units==12,"Opening freight route does not carry Eunhia nickel to Phehmyar");
 std::cout<<"PASS universe: "<<plans.size()<<" services / "<<systems.size()<<" systems / "<<regions.size()<<" regions / "<<clashes<<" faction interdictions / worldless legs "<<withoutWorlds<<'\n';
}
void backgroundServices(std::shared_ptr<const XalariaCatalog> cat){
 Fixture f(cat);auto& cv=f.combat.convoys();const ConvoyKey key{eunhiaStation.system,0};
 const auto before=f.economy.stock(phehmyarStation,XGood::Nickel),source=f.economy.stock(eunhiaStation,XGood::Nickel),wallet=f.economy.credits();
 CombatPlayer observer;const double time=cv.arrival(key)+1.;const auto at=cv.stage(key,time);
 observer.frame=at.frame;observer.position=at.position+glm::dvec3(50000.,0,0);observer.seconds=time;
 cv.advance(observer,.1);const auto* r=cv.record(key);
 check(r&&r->background&&(r->delivered||cv.stage(key,time).phase==ConvoyPhase::Lost),"An unobserved arrival bypassed its background battle outcome");
 int64_t delivered=0;for(const auto& [k,record]:cv.records())if(cv.plan(k).destination==phehmyarStation&&cv.plan(k).good==XGood::Nickel&&record.delivered)delivered+=record.remaining;
 int64_t loaded=0,removed=0;for(const auto& [k,record]:cv.records()){const auto& plan=cv.plan(k);if(plan.origin==eunhiaStation&&plan.good==XGood::Nickel)loaded+=plan.units;if(plan.origin==phehmyarStation&&plan.good==XGood::Nickel)removed+=plan.units;}
 check(f.economy.stock(phehmyarStation,XGood::Nickel)==before+delivered-removed&&f.economy.stock(eunhiaStation,XGood::Nickel)==source-loaded,"A finite departure or one-time arrival did not change the shared station inventory");
 const auto after=f.economy.stock(phehmyarStation,XGood::Nickel);observer.seconds+=.1;cv.advance(observer,.1);
 check(f.economy.stock(phehmyarStation,XGood::Nickel)==after&&!cv.report()&&f.economy.credits()==wallet&&r->accompanied==0.,"Background shipping duplicated a delivery or granted unearned escort pay");
 std::cout<<"PASS background services: seeded offscreen outcome, finite wholesale lot, one-time market delivery, shared inventory debited, no unearned reward\n";
}
void npcBattle(std::shared_ptr<const XalariaCatalog> cat){
 Fixture f(cat);const auto& plans=f.combat.convoys().plans();const auto pick=std::find_if(plans.begin(),plans.end(),[&](const auto& p){return f.combat.convoys().factionBattle({p.first,0})&&p.second.offset<80.;});
 check(pick!=plans.end(),"No faction battle fixture");f.dock(pick->second.origin);
 const ConvoyKey key{pick->first,0};for(unsigned i=0;i<1500&&f.pilot.ship().flightSeconds<f.combat.convoys().departure(key)+91.;++i)f.tick();
 const auto* r=f.combat.convoys().record(key);check(r&&r->npcShots>0&&r->npcHits>0,"Factions never exchanged physical weapon hits");
 const auto survivors=f.combat.convoys().localBodies(f.economy.currentSystem(),f.pilot.ship().flightSeconds);
 check(r->dead||std::any_of(survivors.begin(),survivors.end(),[](auto& b){return b.hull<100.;}),"NPC battle caused no hull damage");
 check(f.combat.victories()==0&&f.combat.bountyDue()==0&&f.pilot.ship().hull==100.,"NPC fighting awarded player kills or damaged a safely clamped player");
 std::cout<<"PASS faction battle: "<<r->npcShots<<" physical NPC shots / "<<r->npcHits<<" hits without player firing\n";
}
void escortRun(std::shared_ptr<const XalariaCatalog> cat,const std::filesystem::path& captures){
 Fixture f(cat);f.dock(eunhiaStation);check(f.combat.refit(CombatFit::Gunship),"Escort Gunship refit failed");auto& cv=f.combat.convoys();const ConvoyKey key{eunhiaStation.system,0};
 check(cv.record(key)&&cv.record(key)->known,"Physical departure board did not discover its convoy");check(cv.join(key,ConvoyDuty::Escort,f.player()),cv.message);f.capture(captures,"escort-departure");
 f.pilot.launch();check(cv.follow(f.nav,f.pilot,f.course),"Convoy helm assist failed to start");unsigned crossed=0;bool fired=false;
 for(unsigned i=0;i<5000&&(!cv.record(key)->delivered||f.economy.currentSystem()!=phehmyarStation.system);++i){
  const auto time=f.pilot.ship().flightSeconds;const auto pose=cv.stage(key,time);
  if(time>cv.departure(key)+47.&&time<cv.departure(key)+100.&&!f.pilot.instruments().docking.active){
   for(unsigned member:{3u,4u}){const auto id=cv.id(key,member,pose.frame);if(auto c=cv.contact(id,time);c&&c->body.hull>0&&c->body.systems[size_t(ShipSystem::Engines)]>0){fired|=f.shoot(id,CombatWeapon::Pulse);break;}}
  }
  crossed+=f.tick();
  if(f.pilot.ship().hull<=0){const auto r=cv.record(key);const auto pose=cv.stage(key,f.pilot.ship().flightSeconds);const auto c=cv.contact(cv.id(key,0,pose.frame),f.pilot.ship().flightSeconds);std::cerr<<"ESCORT DEATH time="<<f.pilot.ship().flightSeconds<<" attacker="<<f.combat.incomingFire<<" collision="<<f.pilot.trafficContact()<<" playerRange="<<(c?glm::distance(f.player().position,c->body.position):-1.)<<" npc="<<r->npcShots<<" hits="<<r->npcHits<<" wanted="<<f.combat.wanted(eunhiaStation.system)<<" betrayed="<<r->betrayed<<" accomp="<<r->accompanied<<" shots="<<f.combat.shots()<<'\n';throw std::runtime_error("Escort pilot destroyed");}
 }
 const auto* record=cv.record(key);
 if(!record->delivered||f.economy.currentSystem()!=phehmyarStation.system)std::cerr<<"ESCORT time="<<f.pilot.ship().flightSeconds<<" system="<<f.economy.currentSystem()<<" hull="<<f.pilot.ship().hull<<" accompanied="<<record->accompanied<<" phase="<<int(cv.stage(key,f.pilot.ship().flightSeconds).phase)<<" nav="<<f.pilot.instruments().destination<<" range="<<f.pilot.instruments().range<<'\n';
 check(crossed==1&&record->delivered&&record->remaining==12&&fired&&record->npcHits>0&&record->playerHits>0,"Escort failed to defend a real interstellar nickel delivery");
 check(record->accompanied>=45.&&!record->betrayed&&f.combat.wanted(eunhiaStation.system)==0,"Escort presence or lawful targeting incorrect");
 cv.following=false;f.pilot.clearWaypoint();check(f.economy.travelToPort(phehmyarStation,f.nav,f.pilot),"Phehmyar refused escorted arrival");
 for(unsigned i=0;i<5000&&!f.economy.dockedPlace();++i)f.tick();
 check(f.economy.dockedPlace()==phehmyarStation,"Escort pilot did not physically berth at Phehmyar");const auto wallet=f.economy.credits();check(cv.report()&&f.economy.credits()==wallet+260000&&cv.standing(cv.plan(key).faction)==5,cv.message);
 check(!cv.report(),"Escort payment could be claimed twice");XalariaStories contacts(f.economy,*cat);check(contacts.trust(cv.plan(key).faction)==2,"Completed escort did not earn faction contacts");f.capture(captures,"escort-paid");
 std::cout<<"PASS Eunhia -> Phehmyar escort: manual wormhole crossing, 12 nickel delivered, "<<record->accompanied<<" s accompanying, 2600 GC paid once\n";
}
void pirateRun(std::shared_ptr<const XalariaCatalog> cat,const std::filesystem::path& captures){
 Fixture f(cat);f.dock(eunhiaStation);check(f.combat.refit(CombatFit::Gunship),"Pirate refit failed");auto& cv=f.combat.convoys();const ConvoyKey key{eunhiaStation.system,0};
 check(cv.join(key,ConvoyDuty::Pirate,f.player()),cv.message);f.pilot.launch();check(cv.follow(f.nav,f.pilot,f.course),"Pirate shadowing failed");
 bool disabled=false;
 for(unsigned i=0;i<1600&&!disabled;++i){
  const auto time=f.pilot.ship().flightSeconds;const auto pose=cv.stage(key,time);
  if(time>cv.departure(key)+35.&&!f.pilot.instruments().docking.active){
   bool escort=false;for(unsigned member:{1u,2u,5u}){const auto id=cv.id(key,member,pose.frame);if(auto c=cv.contact(id,time);c&&c->body.hull>0&&glm::distance(f.player().position,c->body.position)<6500.){f.shoot(id,CombatWeapon::Pulse);escort=true;break;}}
   if(!escort){const auto id=cv.id(key,0,pose.frame);f.shoot(id,CombatWeapon::Ion);disabled=f.combat.shieldsLowered(id);}
  }
  f.tick();if(f.pilot.ship().hull<=0)throw std::runtime_error("Pirate pilot destroyed before capturing the convoy");
 }
 check(disabled,"Pirate failed to disable the freighter without destroying its cargo");cv.following=false;
 const auto pose=cv.stage(key,f.pilot.ship().flightSeconds);const auto carrier=cv.id(key,0,pose.frame);f.combat.target=carrier;check(f.combat.demand(carrier,f.player()),f.combat.message);
 const auto point=xalariaVesselApproach(f.economy,carrier,f.player().gate,f.pilot.ship().flightSeconds);check(point&&f.pilot.dockManually(*point)&&f.pilot.dock(),"Surrendered convoy refused docking");
 for(unsigned i=0;i<3000&&!f.pilot.clampedHarbour();++i){const auto update=xalariaVesselApproach(f.economy,carrier,f.player().gate,f.pilot.ship().flightSeconds);check(bool(update),"Captured collar lost its pose");f.pilot.trackWaypoint(*update);f.tick();}
 check(f.pilot.clampedHarbour()&&*f.pilot.clampedHarbour()==carrier,"Pirate never secured the real freighter clamps");f.capture(captures,"pirate-clamped");
 check(!f.combat.seize(carrier,{}),"Convoy cargo could be stolen remotely");
 // The rendered proof separately walks both airlocks and invokes the real E control.
 StoryContext aboard{.system=eunhiaStation.system,.orbit=eunhiaStation.world,.boardedVessel=carrier};
 check(f.combat.seize(carrier,aboard)&&f.economy.cargo()[size_t(XGood::Nickel)]==12&&f.combat.stolen()[size_t(XGood::Nickel)]==12,"Pirate did not receive the exact finite convoy manifest");
 check(!f.combat.seize(carrier,aboard)&&!cv.report()&&f.combat.wanted(eunhiaStation.system)>0&&cv.standing(cv.plan(key).faction)<0,"Pirate received duplicate loot, escort pay or no consequences");
 const auto saved=cv.serialize();const auto warfare=f.combat.serialize();XalariaCombat restored(f.universe,f.economy,f.pilot);
 check(restored.restore(warfare,f.pilot.ship().flightSeconds)&&restored.convoys().restore(saved,f.pilot.ship().flightSeconds),"Convoy piracy state did not restore");
 check(restored.convoys().record(key)->remaining==0&&restored.prizeCargo(carrier)==0,"Reload replenished a stolen manifest");
 const auto before=restored.convoys().serialize();check(!restored.convoys().restore(saved+" garbage",f.pilot.ship().flightSeconds)&&restored.convoys().serialize()==before,"Malformed convoy save mutated good state");
 f.capture(captures,"pirate-prize");
 for(double aspect:{1.,4./3.,16./9.,21./9.}){check(cockpitTextConflicts(xalariaConvoyOverlay(cv,f.economy,aspect,f.pilot.ship().flightSeconds)).empty(),"Operations screen has overlapping text");CockpitOverlay hud;xalariaConvoyHud(hud,cv,aspect,f.pilot.ship().flightSeconds);check(cockpitTextConflicts(hud).empty(),"Convoy duty HUD overlaps");}
 std::cout<<"PASS Eunhia -> Phehmyar pirate: defeated escorts, disabled freighter, physical docking, 12 stolen nickel, finite loot, warrant, faction loss and reload\n";
}
int main(int argc,char** argv){try{check(argc>=2,"Catalogue required");auto catalog=std::make_shared<XalariaCatalog>(std::filesystem::path(argv[1]));const std::filesystem::path captures=argc>=3?argv[2]:"";
 universeCoverage(catalog);backgroundServices(catalog);npcBattle(catalog);escortRun(catalog,captures);pirateRun(catalog,captures);
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
