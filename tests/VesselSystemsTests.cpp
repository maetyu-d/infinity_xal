#include "infinity/Cockpit.hpp"
#include "infinity/CrewMinimap.hpp"
#include "infinity/Economy.hpp"
#include "infinity/NpcCombat.hpp"
#include "infinity/VesselSystems.hpp"
#include <iostream>
#include <iomanip>
#include <queue>
#include <set>
#include <sstream>
using namespace infinity;
namespace {
unsigned checks = 0;
void check(bool pass, const char* message) { ++checks; if (!pass) throw std::runtime_error(message); }
struct Fixture {
    Universe universe;
    Address home = Address{}.child(Kind::Galaxy,0).child(Kind::System,9).child(Kind::Planet,0);
    ActivityBody raw = sampleRaider(universe,home,100);
    NpcPlayer player;
    ShipCrew crew{universe.seed()};
    WeaponSystem weapons;
    NpcCombat combat;
    VesselOperations operations;
    Fixture() {
        raw.velocity={}; raw.hull=65;raw.systems[0]=65;
        raw.systems[size_t(ShipSystem::Engines)]=0;
        player.reference=home;player.seconds=100;
        player.position=raw.position+glm::dvec3(0,0,raw.radius+80);player.forward={0,0,-1};
        check(weapons.secureVessel(raw,100),"Could not secure test vessel");
        combat.update(universe,player,std::span(&raw,1),weapons,.1);
    }
    ActivityBody body() const {auto b=raw;combat.apply(b);weapons.apply(b);return b;}
    void scan(double seconds=6) {
        for(unsigned i=0;i<unsigned(seconds*10);++i){player.seconds+=.1;auto b=body();operations.observe(universe,player,b,combat.engineering(b.id),.1,true,crew.efficiency(CrewStation::Navigation),std::span(&b,1));}
    }
    std::optional<VesselTaskResult> step() {
        player.seconds+=.1;auto b=body();auto* e=combat.engineering(b.id);
        operations.observe(universe,player,b,e,.1,false,1,std::span(&b,1));
        return operations.advance(universe,player,b,e,crew,.1,std::span(&b,1));
    }
};
std::shared_ptr<EconomicNetwork> network(const Fixture& f) {
    auto n=std::make_shared<EconomicNetwork>();n->system=f.home.parent();n->universeSeed=f.universe.seed();n->fingerprint=640081;
    EconomicSite s;s.id={f.home,0};s.orbital=true;s.name="FIELD OFFICE";s.initial.fill(1000);s.workers=s.power=20;s.capacityKg=1000000;n->sites.push_back(s);
    return n;
}
EconomicCommand command(const Fixture& f,const EconomyPlayer& p,EconomicAction action) {
    EconomicCommand c;c.sequence=p.lastCommand+1;c.port=f.raw.id;c.combatTarget=f.raw.id;c.action=action;
    c.wreck=f.weapons.damage().front();c.combatPresent=true;c.targetDistance=80;c.relativeSpeed=0;c.crewAboard=6;
    c.impactSeconds=f.player.seconds;c.workSeconds=12;c.engineering=100;c.crewId=4;return c;
}
void audit(const Economy& e) {
    for(size_t g=0;g<goodCount;++g)check(e.initialTotal()[g]+e.externalGoodsDelta()[g]==e.inventoryTotal()[g],"Field operations violated finite goods ledger");
    Economy restored(std::make_shared<EconomicNetwork>(e.network()));
    check(restored.restore(e.serialize())&&restored.serialize()==e.serialize(),"Field ledger reload differs");
}
}
int main(){try{
    Fixture f;
    const auto canonical=f.raw;
    std::set<uint64_t> layouts;std::set<unsigned> complements;
    unsigned generated=0;
    for(auto slot:activitySlots(f.universe.describe(f.home))){
        auto b=sampleActivity(f.universe.describe(f.home),slot,100);
        if(b.kind!=ActivityKind::Freighter&&b.kind!=ActivityKind::Ferry&&b.kind!=ActivityKind::Survey&&b.kind!=ActivityKind::Patrol)continue;
        ++generated;auto p=vesselProfile(b);auto q=vesselProfile(b);
        check(p.fingerprint==q.fingerprint&&p.rooms==q.rooms&&p.doors==q.doors,"NPC deck is not deterministic");
        std::set<unsigned> reached{0};for(unsigned iteration=0;iteration<7;++iteration)for(auto [a,c]:p.doors){if(reached.contains(a))reached.insert(c);if(reached.contains(c))reached.insert(a);}
        check(reached.size()==7,"Generated deck disconnected");layouts.insert(p.fingerprint);complements.insert(p.complement);
    }
    check(layouts.size()==generated&&generated>5,"Distinct vessel identities received duplicate deck seeds");
    auto prof=vesselProfile(f.raw);auto freight=f.raw;freight.kind=ActivityKind::Freighter;auto cargo=vesselProfile(freight);
    check(prof.reactor!=cargo.reactor&&prof.endurance!=cargo.endurance,"Roles lack engineering specialisation");
    {
        auto physical=sampleActivity(f.universe.describe(f.home),100,100);
        physical.hull=65;physical.systems[1]=0;WeaponSystem ledger;
        check(ledger.secureVessel(physical,100),"Intact adrift freighter failed to register");
        auto dispatch=physical;dispatch.disabled=true;dispatch.hull=0;ledger.apply(dispatch);
        check(!dispatch.disabled&&dispatch.hull==65&&dispatch.systems[1]==0,"Dispatch interruption destroyed an intact physical hull");
        ledger.fieldRepair(physical.id,101);dispatch.disabled=true;dispatch.hull=0;ledger.apply(dispatch);
        check(!dispatch.disabled&&dispatch.hull==65&&dispatch.systems[1]>=60,"Economic dispatch flag overrode completed physical repair");
    }
    {
        auto hostile=sampleRaider(f.universe,f.home,100);hostile.position=f.raw.position;hostile.velocity={};
        auto pilot=f.player;pilot.position=hostile.position+glm::dvec3(0,0,1600);pilot.forward={0,1,0};
        NpcCombat hot;WeaponSystem clean;hot.update(f.universe,pilot,std::span(&hostile,1),clean,.1);
        const auto oldEngineering=hot.engineering(hostile.id)->serialize();
        auto thermal=*hot.engineering(hostile.id);thermal.plant.heat=1;thermal.plant.stores[1]=0;thermal.plant.refresh();
        auto saved=hot.serialize(f.universe.seed());std::ostringstream oldToken,newToken;oldToken<<std::quoted(oldEngineering);newToken<<std::quoted(thermal.serialize());
        auto position=saved.find(oldToken.str());check(position!=std::string::npos,"NPC thermal fixture not found");saved.replace(position,oldToken.str().size(),newToken.str());
        check(hot.restore(saved,f.universe),"NPC thermal fault did not restore");
        pilot.seconds+=.1;hot.update(f.universe,pilot,std::span(&hostile,1),clean,.1);
        check(hot.agents().at(hostile.id).mode==NpcMode::Retreat&&hot.agents().at(hostile.id).shots==0,"Hot NPC failed to shed guns and retreat");
    }
    {
        auto target=sampleRaider(f.universe,f.home,100);target.velocity={};
        WeaponSystem ion;ion.setLevel(ionDisruptor);WeaponRay ray;
        ray.reference=f.home;ray.origin=target.position+glm::dvec3(0,0,500);ray.direction={0,0,-1};ray.seconds=100;
        check(ion.fire(f.universe,ray,std::span(&target,1))&&ion.ionDisabled(target.id),"Ion outage fixture failed");
        NpcCombat director;auto player=f.player;player.position=ray.origin;
        director.update(f.universe,player,std::span(&target,1),ion,.1);
        const auto* systems=director.engineering(target.id);
        check(systems&&!systems->surrendered&&systems->rooms[3].integrity==100&&systems->rooms[3].fire==0,"Temporary ion jam caused permanent surrender or physical fire");
        for(unsigned i=0;i<24;++i){ion.advance(1);player.seconds+=1;director.update(f.universe,player,std::span(&target,1),ion,.1);}
        check(!ion.ionDisabled(target.id)&&!director.engineering(target.id)->surrendered,"Temporary ion outage failed to recover");
    }
    auto* eng=f.combat.engineering(f.raw.id);
    check(eng&&eng->initialized&&eng->rooms[3].integrity==0&&eng->surrendered,"Subsystem damage failed to reach NPC compartments and surrender");
    const auto state=f.combat.serialize(f.universe.seed());NpcCombat loaded;
    check(loaded.restore(state,f.universe)&&loaded.serialize(f.universe.seed())==state,"NPC engineering exact restart failed");
    auto empty=f.player;empty.position+=glm::dvec3(1e8,0,0);
    for(unsigned i=0;i<20;++i)f.combat.update(f.universe,empty,{},f.weapons,.1);
    check(f.combat.engineering(f.raw.id)->plant.serialize()==eng->plant.serialize(),"Leaving view refuelled an NPC");
    check(f.raw.id==canonical.id&&f.raw.position==canonical.position&&f.raw.seed==canonical.seed,"Vessel simulation mutated canonical identity or coordinates");
    auto b=f.body();
    check(f.operations.begin(VesselTask::Repair,b,eng,f.crew).find("SCAN")!=std::string::npos,"Boarding bypassed scanning");
    f.scan();check(f.operations.confirmed(b.id,f.player.seconds),"Focused scan never confirmed");
    auto locked=f.operations.view(b,eng,true);
    check(locked.confirmed&&locked.systems[1]==0&&locked.heat==eng->plant.heat,"Scan telemetry does not reflect actual damage");
    f.player.seconds+=46;f.operations.observe(f.universe,f.player,b,eng,.1,false,1,{});
    check(!f.operations.confirmed(b.id,f.player.seconds),"Expired internal lock remained actionable");
    f.scan();
    auto blocker=b;blocker.id.slot=201;blocker.position=(b.position+f.player.position)*.5;blocker.radius=8;
    VesselOperations blocked;std::array obstacles{b,blocker};
    for(unsigned i=0;i<100;++i){f.player.seconds+=.1;blocked.observe(f.universe,f.player,b,eng,.1,true,1,obstacles);}
    check(!blocked.confirmed(b.id,f.player.seconds),"Scanner read through another hull");
    f.scan();
    f.player.velocity={4,0,0};f.operations.observe(f.universe,f.player,b,eng,.1,true,1,std::span(&b,1));
    check(f.operations.begin(VesselTask::Repair,b,eng,f.crew).find("VELOCITY")!=std::string::npos,"Moving ship allowed crew transfer");
    f.player.velocity={};f.scan();
    auto start=f.operations.begin(VesselTask::Repair,b,eng,f.crew);
    check(f.operations.party()!=0&&f.crew.away()==f.operations.party(),"Field work did not reserve actual crew");
    auto partyIndex=size_t(std::find_if(f.crew.members().begin(),f.crew.members().end(),[&](const auto& m){return m.id==f.operations.party();})-f.crew.members().begin());
    const auto crewView=f.crew.view(partyIndex);const auto map=crewMinimap(crewView,16./9);
    check(crewView.tasks[partyIndex].status=="AWAY PARTY","Crew orders hid deployed person's status");
    for(const auto& room:map.rooms)check(!room.contains(map.markers[partyIndex].position),"Away crew still displayed inside own ship");
    const auto engineerBefore=f.crew.efficiency(CrewStation::Engineering);
    check(engineerBefore==0,"Away engineer simultaneously staffed player reactor");
    std::set<unsigned> rooms;std::optional<VesselTaskResult> result;
    for(unsigned i=0;i<900&&!result;++i){
        result=f.step();auto v=f.operations.view(f.body(),f.combat.engineering(b.id),true);rooms.insert(v.partyRoom);
        if(i==25){VesselOperations restored;auto saved=f.operations.serialize(f.universe.seed());check(restored.restore(saved,f.universe)&&restored.serialize(f.universe.seed())==saved,"Away party did not persist deck location");f.operations=std::move(restored);
            ShipCrew crewRestored(f.universe.seed());check(crewRestored.restore(f.crew.serialize()),"Away party's own crew record failed reload");f.crew=std::move(crewRestored);}
    }
    check(result&&result->task==VesselTask::Repair&&rooms.contains(3)&&rooms.size()>1,"Party did not walk deck and complete engineering work");
    check(f.step().has_value(),"Busy wallet could lose a completed field task");
    const double awayHealth=f.crew.members()[partyIndex].health;
    f.crew.order(5,CrewOrder::Aid,CrewStation::Engineering);
    for(unsigned i=0;i<50;++i)f.crew.advance(.25);
    check(f.crew.members()[partyIndex].health==awayHealth,"Onboard medic healed crew on another vessel");
    auto n=network(f);Economy economy(n);economy.advanceTo(100,128);EconomyPlayer wallet;
    for(auto g:{Good::Alloy,Good::Electronics,Good::Propellant,Good::Water,Good::Oxygen})wallet.cargo[size_t(g)]=10;
    auto registerOrder=command(f,wallet,EconomicAction::RegisterWreck);
    check(economy.execute(registerOrder,wallet).starts_with("WRECK REGISTERED"),"Boarding did not register finite manifest");
    auto survivor=vesselSurvivor(f.universe.seed(),b.id,b.systems[size_t(ShipSystem::LifeSupport)]);
    check(bool(economy.wrecks().front().survivor)==bool(survivor)&&(!survivor||economy.wrecks().front().survivor->id==survivor->id),"Boarding and old recovery generated different survivors");
    auto repair=command(f,wallet,EconomicAction::FieldRepairVessel);auto before=wallet.cargo;
    check(economy.execute(repair,wallet).starts_with("FIELD REPAIR COMPLETE"),"Verified field repair rejected");
    check(before[size_t(Good::Alloy)]-wallet.cargo[size_t(Good::Alloy)]==2&&before[size_t(Good::Water)]-wallet.cargo[size_t(Good::Water)]==2,"Repair failed to consume actual kit");
    auto paid=wallet.cargo;repair.sequence=wallet.lastCommand+1;economy.execute(repair,wallet);check(wallet.cargo==paid,"Receipt charged repair twice");audit(economy);
    auto old=f.body();f.combat.rejoin(f.raw,old);f.weapons.fieldRepair(b.id,repair.impactSeconds);f.combat.fieldService(b.id,repair.impactSeconds);
    auto repaired=f.body();check(repaired.position==old.position&&repaired.id==old.id&&repaired.systems[1]>=60,"Repair teleported hull or failed to restart propulsion");
    auto distantRoute=f.raw;distantRoute.position+=glm::dvec3(1e7,0,0);
    const auto fuelBefore=f.combat.engineering(b.id)->plant.stores[0];
    f.combat.update(f.universe,f.player,std::span(&distantRoute,1),f.weapons,.1);
    check(f.combat.engineering(b.id)->plant.stores[0]<fuelBefore,"Nearby moored ship stopped simulation when its old route moved away");
    f.operations.acknowledge(true);for(unsigned i=0;i<600&&f.operations.target();++i)f.step();check(f.crew.away()==0&&!f.operations.target(),"Party did not return to own ship");
    check(f.crew.efficiency(CrewStation::Engineering)>0,"Returned engineer never resumed duty");
    const auto serviced=f.combat.engineering(b.id)->plant.serialize();f.combat.fieldService(b.id,repair.impactSeconds);check(serviced==f.combat.engineering(b.id)->plant.serialize(),"Receipt refilled NPC twice");
    WeaponRay ray;ray.reference=f.home;ray.origin=f.player.position;
    ray.direction=glm::normalize(repaired.position+repaired.orientation*(systemOffset(ShipSystem::Engines)*repaired.radius)-ray.origin);
    ray.targetSystem=ShipSystem::Engines;ray.seconds=f.player.seconds+2;
    f.weapons.advance(2);check(f.weapons.fire(f.universe,ray,std::span(&repaired,1)),"Later subsystem damage fixture failed");
    auto hit=f.weapons.damage().front();check(hit.systems[1]<60,"Later hit did not damage repaired engine");f.weapons.fieldRepair(b.id,repair.impactSeconds);
    check(f.weapons.damage().front().systems==hit.systems,"Old field receipt healed later subsystem damage");
    auto salvage=command(f,wallet,EconomicAction::RecoverWreck);salvage.serviceUnits=2;
    economy.execute(salvage,wallet);check(wallet.rescued.empty(),"Equipment mission silently abducted survivor");auto cargoAfter=wallet.cargo;
    salvage.sequence=wallet.lastCommand+1;economy.execute(salvage,wallet);check(wallet.cargo==cargoAfter,"Repeated boarding minted equipment");
    auto rescue=command(f,wallet,EconomicAction::RecoverWreck);rescue.serviceUnits=1;economy.execute(rescue,wallet);
    check(wallet.rescued.size()==size_t(bool(survivor)),"Evacuation failed to transfer named finite survivor");
    rescue.sequence=wallet.lastCommand+1;economy.execute(rescue,wallet);check(wallet.rescued.size()==size_t(bool(survivor)),"Survivor duplicated on second evacuation");audit(economy);
    // Read-only console must fit portrait and wide windows without hiding controls.
    for(double aspect:{1.,16./9,2.4}){
        FlightInstruments t;t.vessel=std::make_shared<VesselView>(locked);
        const auto overlay=cockpitOverlay(t,aspect,"");
        check(cockpitTextConflicts(overlay).empty(),"Contact console text overlaps");
        for(const auto& text:overlay.textRuns)check(text.bounds.x>=0&&text.bounds.y>=0&&text.bounds.x+text.bounds.w<=900*aspect+.01&&text.bounds.y+text.bounds.h<=900,"Contact console text outside screen");
    }
    VesselOperations bad;auto good=f.operations.serialize(f.universe.seed());check(bad.restore(good,f.universe)&&!bad.restore(good+" invalid",f.universe)&&bad.serialize(f.universe.seed())==good,"Invalid mission save changed state");
    std::cout<<"VESSEL_SYSTEMS_PASS checks="<<checks<<" deck_scan_board_repair_rescue_salvage_reload=passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"VESSEL_SYSTEMS_FAIL "<<e.what()<<'\n';return 1;}}
