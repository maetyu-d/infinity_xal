#include "infinity/XalariaCombat.hpp"
#include "infinity/XalariaNavigation.hpp"
#include <set>
#include "infinity/XalariaStories.hpp"
#include "infinity/Cockpit.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
using namespace infinity;
void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
struct Fixture {
 std::shared_ptr<const XalariaCatalog> catalog; Universe universe; Navigation nav; RegionCache cache; FlightController controller;
 Pilot pilot; XalariaEconomy economy; XalariaNavigation course; XalariaCombat combat; double seconds=0.;
 Fixture(std::shared_ptr<const XalariaCatalog> c):catalog(c),universe(0x58414c4152494101ULL,c),nav(universe),cache(universe),controller(universe,nav,cache),pilot(universe,nav,controller),economy(universe),course(universe,nav,pilot),combat(universe,economy,pilot){course.setEconomy(&economy);economy.setCombat(&combat);}
 void dock(XPlace p){
  pilot.brake();pilot.clearWaypoint();pilot.releaseWaypointControl();
  const auto b=*dockingBerth(economy.portBody(p));nav.changeFrame(b.id.home);nav.setRenderScale(3.);nav.orientation=b.orientation;
  nav.offset=b.metres/1000.-nav.orientation*glm::dvec3(0,0,1);pilot.synchronizeFrame();pilot.resetMotion();economy.sync(nav,pilot);economy.show(p.system,p.world);economy.view.place=p;
  check(economy.travelToPort(p,nav,pilot),"Friendly docking denied");
  for(int i=0;i<100&&!economy.dockedPlace();++i){cache.beginFrame();course.advanceFlight({},.1);economy.advance(.1);economy.sync(nav,pilot);cache.finishFrame();}
  check(economy.canTrade(p),"Docking did not reach the market");seconds=pilot.ship().flightSeconds;
 }
 CombatPlayer aim(ActivityId id,double range=700.){
  auto body=xalariaTrafficContact(economy,id,{},seconds)->body;
  auto forward=body.orientation*glm::dvec3(0,0,-1);
  // Remain clear of the station and shoot toward the target's broadside.
  auto side=body.orientation*glm::dvec3(1,0,0);
  CombatPlayer p;p.frame=id.home;p.position=body.position+side*range;p.forward=-side;p.seconds=seconds;
  p.orientation=glm::quatLookAtRH(p.forward,body.orientation*glm::dvec3(0,1,0));return p;
 }
 void cool(CombatPlayer p,double duration){for(int i=0;i<int(duration*10);++i){p.seconds=seconds;combat.advance(p,.1);seconds+=.1;}}
 ActivityId freighter(XPlace p){auto station=*economy.orbitalStationId(p);for(unsigned lane=3;lane<8;++lane){ActivityId id{station.home,trafficActivityTag|(station.slot<<4)|lane};if(xalariaTrafficContact(economy,id,{},seconds)->body.kind==ActivityKind::Freighter)return id;}throw std::runtime_error("No freighter");}
};
XPlace findPort(const Fixture& f,bool haven,bool raider=false){
 for(const auto& [system,star]:f.catalog->stars)for(const auto& [world,r]:star.worlds)if(r.stations){
  XPlace p{system,world,0,false};auto faction=f.economy.faction(p);bool fence=faction==XFaction::Rebel||faction==XFaction::Independent;
  if(fence!=haven)continue;
  if(raider){const auto bodies=f.combat.actors(p,0.);if(std::none_of(bodies.begin(),bodies.end(),[](auto& b){return b.kind==ActivityKind::Raider;}))continue;}
  return p;
 }throw std::runtime_error("No suitable port");
}
void piracy(std::shared_ptr<const XalariaCatalog> catalog){
 Fixture f(catalog);const auto law=findPort(f,false),haven=findPort(f,true);
 check(!f.combat.refit(CombatFit::Corsair),"Refit allowed away from a berth");
 f.dock(law);const auto wallet=f.economy.credits();check(f.combat.refit(CombatFit::Corsair),f.combat.message);
 check(f.economy.credits()==wallet-350000&&f.combat.torpedoes()==4,"Corsair price or ammunition wrong");
 const auto id=f.freighter(law);f.combat.target=id;auto p=f.aim(id);
 check(!f.combat.fire(p),"Weapons fired while safe");f.combat.armed=true;f.combat.weapon=CombatWeapon::Ion;
 auto protectedPlayer=p;protectedPlayer.weaponsInhibited=true;check(!f.combat.fire(protectedPlayer),"Fired while walking");
 // Real ray traces: shields take the initial ion load; later hits disable engines without destroying the hull.
 for(unsigned shot=0;shot<6&&!f.combat.shieldsLowered(id);++shot){p=f.aim(id);check(f.combat.fire(p),"Ion failed to fire");
  const auto contact=xalariaTrafficContact(f.economy,id,{},f.seconds);check(contact->body.hull==100.,"Ion damaged the prize hull");
  if(shot==0)check(contact->shield.fraction<1.,"Weapon missed the actual target or bypassed its shield");
  f.cool(p,1.1);
 }
 check(f.combat.shieldsLowered(id),"Ion did not disable engines");p=f.aim(id);check(f.combat.demand(id,p),f.combat.message);
 check(std::none_of(f.combat.explosions().begin(),f.combat.explosions().end(),[&](const auto& e){return e.source==id;}),"Ion disabling exploded the boardable prize");
 const auto captive=xalariaTrafficContact(f.economy,id,{},f.seconds);check(f.combat.mayDock(captive->body)&&xalariaVesselBerth(captive->body),"Disabled ship cannot be boarded");
 check(xalariaTrafficReply(*catalog,f.economy,*captive,TrafficRequest::Dock,0.).find("unlocked")!=std::string::npos,"Surrendered radio refused collar");
 check(!f.combat.seize(id,{}),"Remote seizure allowed");
 StoryContext aboard{.system=law.system,.orbit=law.world,.boardedVessel=id};const auto cargo=f.economy.cargoUnits();
 check(f.combat.seize(id,aboard)&&f.economy.cargoUnits()>cargo,f.combat.message);
 check(!f.combat.seize(id,aboard),"Prize cargo could be farmed");
 const auto good=XGood(captive->body.seed%7);check(f.combat.stolen()[size_t(good)]>0&&!f.combat.maySell(law,good,1),"Stolen goods accepted by a lawful market");
 check(f.combat.maySell(haven,good,1)&&!f.combat.permits(law)&&f.combat.permits(haven),"Warrant access or fence policy wrong");
 // Shared trajectory must agree for radio, weapons and collision sweeps, including halfway through a tick.
 f.cool(p,4.);p=f.aim(id);f.combat.advance(p,.1);
 const auto motions=nearbyXalariaTraffic(f.universe,f.economy,p.frame,p.position,{},law.system,nullptr,f.seconds,.1);
 const auto motion=std::find_if(motions.begin(),motions.end(),[&](auto& m){return m.start.id==id;});
 check(motion!=motions.end(),"Captured freighter absent from collision sweep");
 const auto next=xalariaTrafficContact(f.economy,id,{},f.seconds+.1);
 check(glm::distance(motion->end,next->body.position)<1e-7,"Docking and collision positions diverged");
 const auto saved=f.combat.serialize();XalariaCombat restored(f.universe,f.economy,f.pilot);
 const bool accepted=restored.restore(saved,f.seconds+.1);check(accepted&&restored.serialize()==saved,"Combat save lost prizes, warrant or damage");
 check(!restored.restore(saved+" garbage",f.seconds+.1)&&restored.serialize()==saved,"Invalid save mutated combat state");
 f.pilot.resetMotion();f.economy.sync(f.nav,f.pilot);
 f.economy.setCombat(&restored);check(!f.economy.dockedPlace()&&restored.claimBounty()&&restored.wanted(law.system)==0,
       "Security control refused remote settlement while station access was barred");
 f.economy.setCombat(&f.combat);f.dock(haven);const auto units=f.economy.cargo()[size_t(good)];
 check(f.economy.sell(good,units)&&f.combat.stolen()[size_t(good)]==0,"Fence sale failed to clear stolen ledger");
 const auto fine=f.combat.wanted(law.system);check(fine>0,"Piracy produced no local warrant");
 check(f.combat.refit(CombatFit::Gunship)&&f.combat.wanted(law.system)==fine,"Refitting erased crime history");
 for(double aspect:{1.,4./3.,16./9.,21./9.}){
  auto overlay=xalariaCombatOverlay(f.combat,f.economy,aspect);check(cockpitTextConflicts(overlay).empty(),"Armament menu text overlaps");
  CockpitOverlay hud;xalariaCombatHud(hud,f.combat,aspect);check(cockpitTextConflicts(hud).empty(),"Combat HUD text overlaps");
 }
 const auto originalWarrant=f.combat.wanted(law.system);
 f.pilot.resetMotion();f.pilot.applyWeaponDamage(1000.,{1,0,0});f.pilot.applyWeaponDamage(1000.,{1,0,0});
 check(f.pilot.ship().hull==0.&&f.combat.recover(f.nav)&&f.pilot.ship().hull==100.&&f.economy.cargoUnits()==0&&
       f.combat.wanted(law.system)==originalWarrant,"Emergency recovery lost warrant history or failed to restore the ship");
 std::cout<<"PASS piracy: ray-traced ion, shields, surrender, radio, shared collision pose, boarding-only loot, finite prize, fences, warrants and persistence\n";
}
void patrol(std::shared_ptr<const XalariaCatalog> catalog){
 Fixture f(catalog);const auto law=findPort(f,false,true);f.dock(law);check(f.combat.refit(CombatFit::Gunship),f.combat.message);
 const auto actors=f.combat.actors(law,f.seconds);const auto id=std::find_if(actors.begin(),actors.end(),[](auto& b){return b.kind==ActivityKind::Raider;})->id;
 f.combat.target=id;f.combat.armed=true;f.combat.weapon=CombatWeapon::Ion;auto p=f.aim(id);
 for(unsigned shot=0;shot<7&&!f.combat.shieldsLowered(id);++shot){p=f.aim(id);check(f.combat.fire(p),"Anti-raider ion failed");f.cool(p,1.1);}
 check(f.combat.shieldsLowered(id)&&f.combat.victories()==1&&f.combat.bountyDue()>=120000&&f.combat.wanted(law.system)==0,"Lawful raider engagement did not award exactly one voucher");
 const auto voucher=f.combat.bountyDue();p=f.aim(id);check(f.combat.fire(p),"Disabled raider shot failed");check(f.combat.bountyDue()==voucher,"Duplicate bounty awarded");
 check(f.combat.claimBounty()&&f.combat.bountyDue()==0,"Lawful station refused patrol voucher");
 f.combat.weapon=CombatWeapon::Torpedo;f.cool(p,1.1);p=f.aim(id);const auto ammo=f.combat.torpedoes();check(f.combat.fire(p)&&f.combat.torpedoes()==ammo-1,"Torpedo launch did not consume ammunition");
 const auto mid=f.combat.serialize();XalariaCombat restored(f.universe,f.economy,f.pilot);check(restored.restore(mid,f.seconds+.1)&&restored.serialize()==mid,"In-flight torpedo did not survive save");
 f.cool(p,3.);const auto hull=xalariaTrafficContact(f.economy,id,{},f.seconds)->body.hull;check(hull<100.,"Torpedo never hit the disabled raider");
 f.combat.weapon=CombatWeapon::Pulse;
 for(unsigned i=0;i<6&&xalariaTrafficContact(f.economy,id,{},f.seconds)->body.hull>0;++i){
  p=f.aim(id);f.combat.fire(p);
  if(xalariaTrafficContact(f.economy,id,{},f.seconds)->body.hull>0)f.cool(p,1.1);
 }
 const auto bursts=[&](){return std::count_if(f.combat.explosions().begin(),f.combat.explosions().end(),[&](const auto& e){return e.source==id;});};
 check(xalariaTrafficContact(f.economy,id,{},f.seconds)->body.hull==0.&&bursts()==1,"Fatal hull damage must emit exactly one destruction blast");
 const auto destroyed=xalariaTrafficContact(f.economy,id,{},f.seconds)->body;
 const auto blast=std::find_if(f.combat.explosions().begin(),f.combat.explosions().end(),[&](const auto& e){return e.source==id;});
 check(blast!=f.combat.explosions().end()&&blast->propellant==shipPropellant(destroyed),"Fatal blast lost the actual ship's propellant family");
 const auto death=f.combat.serialize();XalariaCombat wreckReload(f.universe,f.economy,f.pilot);
 check(wreckReload.restore(death,f.seconds)&&wreckReload.explosions().empty(),"Reloading a wreck replayed its explosion");
 f.cool(p,1.1);p=f.aim(id);f.combat.fire(p);check(bursts()==1,"Repeated fire on a wreck duplicated its destruction blast");
 f.cool(p,11.);check(bursts()==0,"Destruction particles did not expire");
 // Fitted shield and residual armour apply to the player's hull, and persist through the legacy Pilot format.
 f.pilot.resetMotion();const auto beforeDamage=f.pilot.ship().hull;
 check(f.pilot.shield().raised,"Fitted shield failed to deploy in flight");f.pilot.applyWeaponDamage(90.,{1,0,0});
 check(std::abs(f.pilot.ship().hull-(beforeDamage-6.5))<1e-6,"Heavy shield/armour did not protect the player");
 const auto hullBefore=f.pilot.ship().hull;const auto pilotSave=f.pilot.serialize(),combatSave=f.combat.serialize();
 check(f.pilot.restore(pilotSave)&&f.combat.restore(combatSave,f.seconds+.1)&&std::abs(f.pilot.ship().hull-hullBefore)<1e-6,"Fitted ship failed save restore");
 f.dock(law);check(f.combat.service()&&f.pilot.ship().hull==100.&&f.combat.torpedoes()==10,"Paid service failed");
 std::cout<<"PASS warship: raider combat, one-time lawful voucher, torpedo impact/persistence, heavy shield/armour and repair/rearm\n";
}
void gateRecovery(std::shared_ptr<const XalariaCatalog> catalog){
 Fixture f(catalog);f.dock(eunhiaStation);check(f.combat.refit(CombatFit::Corsair),"Gate test refit failed");
 const auto mouth=f.course.view.wormhole;check(bool(mouth),"No gate service fixture");
 const auto station=*f.economy.orbitalStationId(eunhiaStation);ActivityId id{station.home,trafficActivityTag|(station.slot<<4)|8u};
 f.combat.armed=true;f.combat.weapon=CombatWeapon::Ion;CombatPlayer p;p.frame=id.home;p.gate=mouth;
 for(unsigned i=0;i<5&&!f.combat.shieldsLowered(id);++i){
  const auto body=xalariaTrafficContact(f.economy,id,mouth,f.seconds)->body;const auto side=body.orientation*glm::dvec3(1,0,0);
  p.position=body.position+side*500.;p.forward=-side;p.orientation=glm::quatLookAtRH(p.forward,body.orientation*glm::dvec3(0,1,0));p.seconds=f.seconds;
  check(f.combat.fire(p),"Gate service ion trace failed");f.cool(p,1.1);
 }
 check(f.combat.shieldsLowered(id),"Gate service ship never disabled");
 check(f.combat.seize(id,{.system=eunhiaStation.system,.orbit=eunhiaStation.world,.boardedVessel=id}),"Captured gate-service hold lost its reference frame");
 f.seconds+=601.;p.seconds=f.seconds;
 const auto before=xalariaTrafficContact(f.economy,id,mouth,f.seconds)->body;f.combat.advance(p,.1);
 const auto after=xalariaTrafficContact(f.economy,id,mouth,f.seconds+.1)->body;
 check(std::isfinite(glm::length(after.position))&&glm::distance(before.position,after.position)<=glm::length(before.velocity)*.1+.4,"Rejoining gate service snapped or used an orbital lane index");
 std::cout<<"PASS gate service: disabled ferry boarding and continuous route recovery use the wormhole frame\n";
}
void explosionPresentation(){
 ShipExplosion blast;blast.radius=32.;blast.seed=0x4558504c4f53494fULL;
 for(double age:{0.,.03,.1,.5,1.,2.,4.,7.,9.9,10.}){
  blast.age=age;const auto image=sampleShipExplosion(blast);
  check(image.count<=image.particles.size(),"Explosion exceeded its particle budget");
  for(size_t i=0;i<image.count;++i){const auto& p=image.particles[i];
   check(std::isfinite(glm::length(p.offset))&&std::isfinite(glm::length(p.velocity))&&std::isfinite(p.radius)&&p.radius>=0.&&p.opacity>=0.&&p.opacity<=1.,"Invalid destruction particle");}
  if(age==10.)check(image.count==0&&image.flash==0.&&image.shock==0.,"Finished blast still emits light");
 }
 blast.age=1.;const auto one=sampleShipExplosion(blast),repeat=sampleShipExplosion(blast);blast.radius*=2.;const auto large=sampleShipExplosion(blast);
 check(one.shockRadius>32.*10.&&one.haloRadius>32.*8.&&one.count==repeat.count,"Destruction blast is too small or changes between frames");
 std::set<unsigned> octants;
 for(size_t i=0;i<one.count;++i){
  check(glm::distance(one.particles[i].offset,repeat.particles[i].offset)==0.,"Explosion geometry flickers on repeated sampling");
  check(glm::distance(one.particles[i].offset*2.,large.particles[i].offset)<1e-8,"Blast does not scale with the destroyed ship");
  if(one.particles[i].kind!=ExplosionParticleKind::Fire){const auto p=one.particles[i].offset;octants.insert((p.x>0?1:0)|(p.y>0?2:0)|(p.z>0?4:0));}
 }
 check(octants.size()==8,"Debris is confined to a plane or has a gravity direction");
 // Count complete populations away from birth/death boundaries: every
 // component gets the requested 50% increase, including clean-fuel residue.
 for(unsigned fuel=0;fuel<4;++fuel){
  blast.radius=32.;blast.age=3.;blast.propellant=ShipPropellant(fuel);
  const auto image=sampleShipExplosion(blast);
  std::array<size_t,4> counts{};
  for(size_t i=0;i<image.count;++i)++counts[size_t(image.particles[i].kind)];
  check(counts[0]==54&&counts[1]==60&&counts[2]==48&&counts[3]==(fuel<2?36:fuel==2?6:9),
        "Explosion particle families did not increase by 50 percent");
  check(std::abs(image.shockRadius-32.*1.20*(1.15+11.5*3.))<1e-8&&
        std::abs(image.coreRadius-32.*1.20*(.8+3.2*(1.-std::exp(-18.))))<1e-8,
        "Explosion visual dimensions did not increase by 20 percent");
 }
 std::set<ShipPropellant> fuels;
 ActivityBody ship;ship.kind=ActivityKind::Patrol;
 for(uint64_t seed=0;seed<256;++seed){ship.seed=seed;const auto fuel=shipPropellant(ship);fuels.insert(fuel);
  auto moved=ship;moved.position={1e12,2e12,-1e12};moved.orientation=glm::angleAxis(1.9,glm::normalize(glm::dvec3(1,2,3)));
  moved.tint={.01,.95,.02};moved.hull=0.;moved.disabled=true;moved.id.slot+=900;
  check(shipPropellant(moved)==fuel,"Ship fuel changes with frame, lighting, motion or damage");
 }
 check(fuels.size()==4,"Procedural ships do not cover all four propellant families");
 std::array<float,4> opacity{};
 for(unsigned fuel=0;fuel<4;++fuel){
  blast.radius=32.;blast.age=1.5;blast.propellant=ShipPropellant(fuel);const auto image=sampleShipExplosion(blast);
  check(image.count<=image.particles.size(),"Fuel cloud exceeded the particle budget");
  check((fuel<2?image.haloColour.r>image.haloColour.b*2.:image.haloColour.b>image.haloColour.r*2.),"Warm or blue-violet fuel palette is incorrect");
  bool gas=false;
  for(size_t i=0;i<image.count;++i)if(image.particles[i].kind==ExplosionParticleKind::Vapour){
   const auto& p=image.particles[i];gas=true;opacity[fuel]+=p.opacity;
   if(fuel==0)check(p.colour.r>p.colour.b*4.&&p.colour.r>p.colour.g*2.,"NTO release is not rust brown");
   if(fuel==1)check(std::max({p.colour.r,p.colour.g,p.colour.b})<.1,"Kerosene soot is luminous instead of dark");
  }
  check(gas,"Fuel profile has no expanding released-gas cloud");
 }
 check(opacity[0]>opacity[2]*10.&&opacity[1]>opacity[3]*4.,"Clean fuels produce the same dense smoke as dirty fuels");
 std::cout<<"PASS explosions: one fatal event, ion-safe boarding, deterministic 3D volume, ship-scale blast, all-direction debris and bounded lifetime\n";
 std::cout<<"PASS propellants: persistent ship families, orange-brown NTO, sooty kerosene, blue-violet clean fuel and sparse clean residue\n";
}
int main(int argc,char** argv){try{
 check(argc==2,"Catalogue path required");auto catalog=std::make_shared<XalariaCatalog>(XalariaCatalog(argv[1]));
 explosionPresentation();piracy(catalog);patrol(catalog);gateRecovery(catalog);Fixture f(catalog);std::set<unsigned> regions;size_t ports=0;
 for(const auto& [system,star]:catalog->stars)for(const auto& [world,r]:star.worlds)if(r.stations){XPlace p{system,world,0,false};const auto ships=f.combat.actors(p,0.);check(!ships.empty(),"Missing system patrol");
  for(const auto& b:ships){check(isXalariaTraffic(b.id)&&xalariaTrafficContact(f.economy,b.id,{},0.)&&xalariaVesselBerth(b),"Procedural warship missing shared traffic or boarding collar");}
  const auto region=catalog->region(system);regions.insert(region.x+20*region.y);++ports;
 }
 check(regions.size()==82,"Combat coverage missing a region");std::cout<<"PASS universe coverage: "<<ports<<" orbital harbours across "<<regions.size()<<" regions\n";
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;} }
