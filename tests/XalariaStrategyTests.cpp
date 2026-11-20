#include "infinity/XalariaCombat.hpp"
#include "infinity/XalariaNavigation.hpp"
#include "infinity/XalariaStories.hpp"
#include "infinity/Cockpit.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <set>
using namespace infinity;
void check(bool x,const std::string& s){if(!x)throw std::runtime_error(s);}
struct Fixture {
 std::shared_ptr<const XalariaCatalog> catalog;Universe universe;Navigation nav;RegionCache cache;FlightController controller;Pilot pilot;XalariaEconomy economy;XalariaNavigation course;XalariaCombat combat;double seconds=0.;
 Fixture(std::shared_ptr<const XalariaCatalog> c):catalog(c),universe(0x58414c4152494101ULL,c),nav(universe),cache(universe),controller(universe,nav,cache),pilot(universe,nav,controller),economy(universe),course(universe,nav,pilot),combat(universe,economy,pilot){economy.setCombat(&combat);course.setEconomy(&economy);}
 XalariaStrategy& strategy(){return combat.strategy();}
 void dock(XPlace p){pilot.brake();pilot.clearWaypoint();const auto b=*dockingBerth(economy.portBody(p));nav.changeFrame(b.id.home);nav.setRenderScale(3.);nav.orientation=b.orientation;nav.offset=b.metres/1000.-nav.orientation*glm::dvec3(0,0,1);pilot.synchronizeFrame();pilot.resetMotion();economy.sync(nav,pilot);economy.show(p.system,p.world);economy.view.place=p;check(economy.travelToPort(p,nav,pilot),"Friendly docking denied");for(int i=0;i<100&&!economy.dockedPlace();++i){cache.beginFrame();course.advanceFlight({},.1);economy.advance(.1);economy.sync(nav,pilot);cache.finishFrame();}check(economy.canTrade(p),"Physical docking failed");seconds=pilot.ship().flightSeconds;}
 CombatPlayer observer(double at)const{CombatPlayer p;p.frame=catalog->address(eunhiaStation.system);p.position={1e10,0,0};p.seconds=at;p.sheltered=true;return p;}
 void advance(double end){while(seconds<end){double dt=std::min(1.,end-seconds);auto p=observer(seconds);strategy().advance(p,dt);seconds+=dt;}}
 ActivityId freighter(XPlace p){const auto station=*economy.orbitalStationId(p);for(unsigned i=3;i<8;++i){ActivityId id{station.home,trafficActivityTag|(station.slot<<4)|i};if(xalariaTrafficContact(economy,id,{},seconds)->body.kind==ActivityKind::Freighter)return id;}throw std::runtime_error("No freighter");}
 CombatPlayer aim(ActivityId id){const auto b=xalariaTrafficContact(economy,id,{},seconds)->body;const auto side=b.orientation*glm::dvec3(1,0,0);CombatPlayer p;p.frame=id.home;p.position=b.position+side*700.;p.forward=-side;p.orientation=glm::quatLookAtRH(p.forward,b.orientation*glm::dvec3(0,1,0));p.seconds=seconds;return p;}
 void cool(CombatPlayer p){for(int i=0;i<11;++i){p.seconds=seconds;combat.advance(p,.1);seconds+=.1;}}
 void disable(ActivityId id){combat.armed=true;combat.weapon=CombatWeapon::Ion;for(int i=0;i<8&&!combat.shieldsLowered(id);++i){auto p=aim(id);check(combat.fire(p),"Ion fire failed");cool(p);}check(combat.shieldsLowered(id),"Actual ion rays failed to disable drives");check(combat.demand(id,aim(id)),combat.message);}
 void snapshot(const std::filesystem::path& folder){if(folder.empty())return;std::filesystem::create_directories(folder);std::ostringstream port;if(economy.dockedPlace()){auto p=*economy.dockedPlace();port<<catalog->fingerprint<<' '<<p.system<<' '<<p.world<<' '<<p.station<<" 0";}XalariaStories stories(economy,*catalog);std::istringstream header(strategy().serialize());std::string tag,fingerprint;unsigned version;double strategyTime;header>>tag>>version>>fingerprint>>strategyTime;
  auto pilotText=pilot.serialize();std::istringstream fields(pilotText);std::ostringstream aligned;std::string token;for(unsigned i=0;fields>>token;++i){if(i)aligned<<' ';if(i==32)aligned<<std::setprecision(17)<<strategyTime;else aligned<<token;}pilotText=aligned.str();std::ofstream out(folder/"session.txt");out<<"XALARIA_SESSION 5 "<<catalog->fingerprint<<' '<<std::quoted(nav.serialize())<<' '<<std::quoted(pilotText)<<' '<<std::quoted(course.serialize())<<' '<<std::quoted(economy.serialize())<<' '<<std::quoted(port.str())<<' '<<std::quoted(economy.serializeTraffic(seconds))<<' '<<std::quoted(stories.serialize())<<' '<<std::quoted(combat.serialize())<<' '<<std::quoted(combat.convoys().serialize())<<' '<<std::quoted(strategy().serialize());}
};
void supply(std::shared_ptr<const XalariaCatalog> c){
 Fixture f(c);auto& s=f.strategy();std::set<unsigned> regions;size_t ports=0;for(const auto& [id,star]:c->stars)for(const auto& [world,w]:star.worlds)ports+=w.stations;
 for(const auto& [p,station]:s.stations()){const auto r=c->region(p.system);regions.insert(r.x+20*r.y);check(station.market.stock[1]>=0,"Invalid stock");}
 check(s.stations().size()==ports&&regions.size()==82,"Strategic depots miss public stations or regions");check(s.knowledge().empty()&&s.reports().empty(),"Fresh game leaks remote intelligence");
 const ConvoyKey key{eunhiaStation.system,0};auto before=f.economy.stock(eunhiaStation,XGood::Nickel),dest=f.economy.stock(phehmyarStation,XGood::Nickel);auto n=s.allocate(key);check(n==12&&f.economy.stock(eunhiaStation,XGood::Nickel)==before-n,"Departure did not debit actual nickel");check(s.allocate(key)==n&&f.economy.stock(eunhiaStation,XGood::Nickel)==before-n,"Manifest loaded twice");s.manifestChanged(key,n-3);s.complete(key,n-3);s.complete(key,n);check(f.economy.stock(phehmyarStation,XGood::Nickel)==dest+n-3,"Delivery cloned stolen cargo or settled twice");
 f.combat.convoys().discover(key);check(f.combat.convoys().record(key)->delivered&&f.combat.convoys().record(key)->remaining==n-3,"Late observation recreated an old manifest");
 const auto* lot=s.shipment(key);check(lot&&lot->settled&&!lot->lost,"Shipment history missing");
 auto saved=s.serialize();XalariaCombat loaded(f.universe,f.economy,f.pilot);check(loaded.strategy().restore(saved,f.seconds)&&loaded.strategy().serialize()==saved,"Early market edits were not saved");
 std::cout<<"PASS supply: "<<ports<<" finite depots in 82 regions, single debit/credit, stolen cargo excluded, late observation stable\n";
}
void industryWar(std::shared_ptr<const XalariaCatalog> c){
 Fixture f(c);auto& s=f.strategy();XPlace refinery{};int64_t ore=0;for(const auto& [p,station]:s.stations())if(station.industry==0&&station.market.stock[9]>0){refinery=p;ore=station.ore;break;}
 f.advance(900.);check(s.station(refinery)->ore<ore&&s.station(refinery)->production>0,"Industry did not consume finite ore and inputs");check(!s.campaigns().empty(),"Universe factions never committed expeditions");check(s.knowledge().empty(),"Background war revealed undiscovered universe");
 const auto fronts=s.campaigns().size();f.advance(4500.);unsigned active=0,won=0;for(const auto& [id,front]:s.campaigns()){active+=!front.finished;won+=front.victory;check(s.station(front.origin)&&s.station(front.target),"Front has no physical station");if(front.victory)check(s.station(front.target)->owner==front.attacker||s.station(front.target)->dirty,"Station surrender has no owner");}
 check(won>0&&s.wrecks().size()>0,"Campaigns never change ownership or leave displaced crews");
 auto save=s.serialize();XalariaCombat restored(f.universe,f.economy,f.pilot);check(restored.strategy().restore(save,f.seconds)&&restored.strategy().serialize()==save,"Universe production, wars or shipments failed roundtrip");for(auto bad:{save+" junk",save.substr(0,save.size()/2),std::string("XALARIA_STRATEGY 1 wrong 0")})check(!restored.strategy().restore(bad,f.seconds)&&restored.strategy().serialize()==save,"Malformed strategy changed live state");
 std::cout<<"PASS industry / war: "<<fronts<<" opening expeditions, "<<active<<" active fronts, "<<won<<" surrenders, finite ore and transactional save\n";
}
void fleet(std::shared_ptr<const XalariaCatalog> c,const std::filesystem::path& proof){
 Fixture f(c);f.dock(eunhiaStation);f.economy.rewardDispatch(10000000,0,0);check(f.combat.refit(CombatFit::Corsair),f.combat.message);auto id=f.freighter(eunhiaStation);f.disable(id);const auto original=xalariaTrafficContact(f.economy,id,{},f.seconds)->body;auto& s=f.strategy();
 check(!s.capture({}),"Remote hull capture allowed");StoryContext aboard{.system=eunhiaStation.system,.orbit=eunhiaStation.world,.boardedVessel=id};check(s.capture(aboard),s.message);check(!s.capture(aboard)&&s.fleet().size()==1,"Prize cloned");
 check(!s.order(1,FleetOrder::Trade,phehmyarStation.system),"Uncrewed ship accepted orders");check(s.hire(1,aboard),s.message);auto own=s.contact(id,f.seconds);check(own&&own->body.seed==original.seed&&own->body.kind==original.kind&&glm::distance(own->body.position,original.position)<1e-7,"Captured hull changed shape or teleported");
 auto p=f.aim(id);auto motions=nearbyXalariaTraffic(f.universe,f.economy,p.frame,p.position,{},eunhiaStation.system,nullptr,f.seconds,.1);check(std::count_if(motions.begin(),motions.end(),[&](const auto& m){return m.start.id==id;})==1,"Captured hull has duplicate collision bodies");
 check(s.order(1,FleetOrder::Follow,eunhiaStation.system),s.message);auto start=s.contact(id,f.seconds)->body.position;auto clamp=p;clamp.clamped=id;s.advance(clamp,.1);check(s.contact(id,f.seconds+.1)->body.position==start,"Captain moved with occupied boarding collar");
 const auto wallet=f.economy.credits(),iron=f.economy.stock(eunhiaStation,XGood::Iron);check(s.purchase(ActivityKind::Freighter),s.message);check(f.economy.credits()==wallet-650000&&f.economy.stock(eunhiaStation,XGood::Iron)==iron-10,"Purchased hull has no cost in credits and metal");check(s.hire(2,{}),s.message);check(s.purchase(ActivityKind::Patrol)&&s.hire(3,{}),s.message);check(s.pledge(),s.message);
 for(double a:{1.,4./3.,16./9.,21./9.})for(unsigned page=0;page<3;++page){s.page=page;auto hud=xalariaStrategyOverlay(s,f.economy,a);check(cockpitTextConflicts(hud).empty(),"Fleet screen text overlaps");}s.page=0;
 // A captain must physically complete a route before settling its cargo.
 const auto cargo=s.fleet().at(1).cargo;check(s.order(1,FleetOrder::Trade,phehmyarStation.system),s.message);const auto arrival=s.fleet().at(1).arrival;auto enroute=s.bodies(eunhiaStation.system,f.seconds+5.1);check(!enroute.empty(),"Fleet absent from departure sky");
 f.advance(arrival+1.);check(s.fleet().at(1).missions>0&&s.knowledge().contains(phehmyarStation.system),"Captain never arrived or reported");
 check(s.order(2,FleetOrder::Escort,phehmyarStation.system),s.message);const auto escort=*s.fleet().at(2).escort;const auto eta=s.fleet().at(2).arrival;const auto before=f.economy.credits();f.advance(eta+1.);const auto* lot=s.shipment(escort);check(lot&&lot->settled&&!lot->lost&&s.fleet().at(2).missions==1&&s.fleet().at(2).order==FleetOrder::Hold,"Commissioned escort did not deliver its actual scheduled lot");check(f.economy.credits()>before,"Surviving escort was not paid");auto after=f.economy.credits();f.advance(eta+2.);check(f.economy.credits()==after,"Escort paid twice");
 const auto save=s.serialize();XalariaCombat resumed(f.universe,f.economy,f.pilot);check(resumed.restore(f.combat.serialize(),f.seconds+.11)&&resumed.strategy().restore(save,f.seconds+.11)&&resumed.strategy().serialize()==save,"Fleet, captain or voyage lost on reload");
 // Snapshot a real docked game with commissioned hulls, without changing the player's profile.
 f.dock(eunhiaStation);f.strategy().order(3,FleetOrder::Hold,eunhiaStation.system);f.snapshot(proof);
 std::cout<<"PASS fleet: physical ion/capture, identical prize, one collider, captain/repair, purchase inputs, trade voyage, scheduled escort, UI layout, save\n";
}
void salvage(std::shared_ptr<const XalariaCatalog> c){
 Fixture f(c);f.dock(eunhiaStation);f.economy.rewardDispatch(10000000,0,0);check(f.combat.refit(CombatFit::Gunship),f.combat.message);auto id=f.freighter(eunhiaStation);f.combat.armed=true;f.combat.weapon=CombatWeapon::Pulse;
 for(int shot=0;shot<45;++shot){auto b=xalariaTrafficContact(f.economy,id,{},f.seconds);if(!b||b->body.hull<=0)break;auto p=f.aim(id);if(f.combat.fire(p))f.cool(p);}
 auto& s=f.strategy();check(s.wrecks().contains(id),"Destroyed ship left no persistent wreck");auto p=f.aim(id);p.transit=false;p.sheltered=false;check(s.salvage(id,p),s.message);check(!s.salvage(id,p),"Wreck yielded infinite salvage");check(s.salvage(id,p,true)&&!s.recruits().empty(),"Actual lifeboats did not yield survivors");check(!s.salvage(id,p,true),"Survivors cloned");
 auto save=s.serialize();XalariaCombat resumed(f.universe,f.economy,f.pilot);check(resumed.strategy().restore(save,f.seconds+.11)&&resumed.strategy().serialize()==save,"Stripped wreck or rescue lost on reload");
 std::cout<<"PASS consequences: actual weapons leave wreck, salvage and survivors recover once, persistent stripped state\n";
}
void strandedCargo(std::shared_ptr<const XalariaCatalog> c){
 Fixture f(c);f.dock(eunhiaStation);check(f.combat.refit(CombatFit::Corsair),f.combat.message);auto& cv=f.combat.convoys();const ConvoyKey key{eunhiaStation.system,0};cv.discover(key);const auto pose=cv.stage(key,f.seconds);const auto id=cv.id(key,0,pose.frame);f.disable(id);const auto units=cv.record(key)->remaining;check(units>0&&cv.record(key)->haltedAt>=0,"No live stranded manifest");
 f.advance(905.);check(cv.record(key)->remaining==units&&!f.strategy().shipment(key)->settled,"Strategic time discarded cargo aboard a live disabled freighter");
 StoryContext aboard{.system=eunhiaStation.system,.orbit=eunhiaStation.world,.boardedVessel=id};check(f.combat.seize(id,aboard)&&f.economy.cargo()[size_t(XGood::Nickel)]==units,"Extended boarding lost the physical freight");check(!f.combat.seize(id,aboard),"Stranded manifest duplicated");
 std::cout<<"PASS long boarding: disabled freighter retains its finite nickel across three strategic turns, then transfers once\n";
}
void conquest(std::shared_ptr<const XalariaCatalog> c){
 Fixture f(c);f.dock(eunhiaStation);f.economy.rewardDispatch(20000000,0,0);check(f.combat.refit(CombatFit::Corsair),f.combat.message);auto& s=f.strategy();check(s.pledge(),s.message);
 for(unsigned id=1;id<=4;++id){check(s.purchase(ActivityKind::Patrol)&&s.hire(id,{}),s.message);check(s.order(id,FleetOrder::Blockade,phehmyarStation.system),s.message);}
 f.advance(250.);bool campaign=false;for(const auto& [id,front]:s.campaigns())campaign|=front.player&&front.target==phehmyarStation;check(campaign,"Arrived warships never began a blockade");
 auto bodies=s.bodies(phehmyarStation.system,f.seconds);check(s.interdicts(bodies.front().id,{eunhiaStation.system,0}),"Blockade does not recognize incoming Eunhia freight");std::set<ActivityId> ids;for(const auto& b:bodies)check(ids.insert(b.id).second,"Duplicate campaign/fleet/wreck hull");
 check(std::any_of(bodies.begin(),bodies.end(),[](const auto& b){return (b.id.slot&0xf0000000u)==campaignActivityTag;}),"Blockade has no physical defenders");
 const auto enemy=std::find_if(bodies.begin(),bodies.end(),[](const auto& b){return (b.id.slot&0xf0000000u)==campaignActivityTag&&(b.id.slot&7)>=4;})->id;
 f.combat.armed=true;f.combat.weapon=CombatWeapon::Ion;
 for(unsigned shot=0;shot<8&&!f.combat.shieldsLowered(enemy);++shot){check(f.combat.fire(f.aim(enemy)),"War prize ion ray failed");for(unsigned tick=0;tick<11;++tick){f.combat.advance(f.observer(f.seconds),.1);f.seconds+=.1;}}
 check(f.combat.shieldsLowered(enemy),"Station defender never lost its drives");auto disabled=s.contact(enemy,f.seconds)->body;auto future=s.contact(enemy,f.seconds+1.);check(future&&glm::distance(disabled.position,future->body.position)<1e-6,"Disabled defender kept circling under power");
 StoryContext aboard{.system=phehmyarStation.system,.orbit=phehmyarStation.world,.boardedVessel=enemy};check(s.capture(aboard)&&f.combat.wanted(phehmyarStation.system)==0&&!s.fleet().at(5).contraband,"Lawful wartime capture created a crime or failed to register the prize");

 // Move a witnessed battle through the normal combat loop: guns use the same ray traces as the player.
 CombatPlayer p;p.frame=bodies.front().id.home;p.position={1e9,0,0};p.seconds=f.seconds;p.sheltered=true;bool observedShots=false;for(unsigned step=0;step<80;++step){p.seconds=f.seconds;f.combat.advance(p,.1);observedShots|=!f.combat.effects().shots().empty();f.seconds+=.1;}check(observedShots,"Visible station squadrons never fired physical weapon rays");
 bool physical=false;for(const auto& [id,front]:s.campaigns())physical|=front.player&&front.physicalAt>=249.;check(physical,"Nearby station defence never entered physical combat");
 f.advance(2100.);check(s.controls(phehmyarStation),"Sustained fleet blockade failed to capture its actual station");check(f.economy.faction(phehmyarStation)==s.allegiance(),"Station ownership did not affect game access/faction");
 unsigned contraband=0;for(const auto& [id,ship]:s.fleet())for(auto n:ship.cargo)contraband+=unsigned(n);check(contraband>0,"Blockade never intercepted an actual finite supply lot");
 check(s.station(phehmyarStation)->treasury>0,"Captured station collected no finite traffic fees");f.dock(phehmyarStation);auto balance=f.economy.credits();const auto fees=s.station(phehmyarStation)->treasury;check(s.collectTaxes()&&f.economy.credits()==balance+fees&&!s.collectTaxes(),"Station fees could not be collected once at its berth");
 std::cout<<"PASS conquest: four commissioned warships, physical station defenders, finite supply interdiction, surrender and ownership\n";
}
int main(int argc,char** argv){try{auto c=std::make_shared<XalariaCatalog>(argv[1]);supply(c);industryWar(c);fleet(c,argc>2?argv[2]:std::filesystem::path{});salvage(c);strandedCargo(c);conquest(c);return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
