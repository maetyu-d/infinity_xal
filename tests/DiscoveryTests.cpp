#include "infinity/Discovery.hpp"
#include "infinity/Cockpit.hpp"
#include "../src/DiscoveryScenario.hpp"
#include <chrono>
#include <iostream>
#include <set>
using namespace infinity;
namespace {
unsigned checks=0;
void check(bool result,const char* why){++checks;if(!result)throw std::runtime_error(why);}
struct Hunt {
    Universe u;
    Address system=Address{}.child(Kind::Galaxy,0).child(Kind::System,9);
    std::shared_ptr<const DiscoveryPage> catalog=generateDiscoveryPage(u,system);
    std::shared_ptr<DiscoveryPage> page=std::make_shared<DiscoveryPage>();
    DiscoveryController receiver;
    ReceiverObserver observer;
    Hunt(){
        check(bool(catalog),"No procedural signal catalog");
        auto source=std::find_if(catalog->sources.begin(),catalog->sources.end(),[](const auto& s){return bool(s.relic);});
        check(source!=catalog->sources.end(),"No procedural archaeology on solid worlds");
        page->system=system;page->sources.push_back(*source);receiver.page(page);
        observer.reference=source->id.planet;observer.seconds=100;
        const auto up=glm::normalize(source->metres),side=tangentFrame(up)*glm::dvec3(1,0,0);
        observer.position=source->metres+up*20000.-side*12000.;aim();
    }
    const SignalSource& source()const{return page->sources.front();}
    void aim(){const auto eye=u.pointRelative(observer.reference,observer.position/std::pow(10.,u.describe(observer.reference).frame.logRadius),source().id.planet,0);
        observer.forward=glm::inverse(u.relative(observer.reference,source().id.planet,0).orientation)*glm::normalize(source().metres-eye);}
    void tick(unsigned count,bool listen=true){for(unsigned i=0;i<count;++i){observer.seconds+=.1;receiver.update(u,observer,.1,listen,1);}}
    void tune(){receiver.tune(source().frequency-receiver.view().frequency);}
};
}
int main(){try{
    Hunt h;const auto& source=h.source();const auto& site=*source.relic;
    check(h.catalog->sources.size()<=10&&h.catalog->bytes()<256*1024,"Radio catalog exceeded bounded memory");
    auto planet=h.u.describe(source.id.planet);const auto fingerprint=descriptorFingerprint(planet);
    auto again=generateSignal(h.u,planet,1);
    check(again&&again->seed==source.seed&&again->descriptor==source.descriptor&&again->metres==source.metres,"Signal source changed across regeneration");
    check(again->relic->cause==site.cause&&again->relic->ageYears==site.ageYears&&again->relic->chronicle==site.chronicle,"Archaeology history is not deterministic");
    for(size_t i=0;i<site.blocks.size();++i)check(again->relic->blocks[i].metres==site.blocks[i].metres&&again->relic->blocks[i].dimensions==site.blocks[i].dimensions,"Ruin geometry replaced itself");
    check(hasSolidSurface(planet)&&planet.geology,"Ruin was generated on nonlandable gas");
    const auto dryHeight=glm::length(site.metres)-source.planetRadius;
    check(planet.geology->liquidWater<.08||dryHeight>2,"Ruin was generated under open water");
    std::set<RuinCause> causes;unsigned realSites=0,gas=0;
    for(const auto& a:h.u.children(h.u.describe(h.system),256)){
        auto p=h.u.describe(a);
        if(!hasSolidSurface(p)){++gas;check(!generateSignal(h.u,p,1),"Gas giant received an archaeological surface");continue;}
        if(auto s=generateSignal(h.u,p,1)){++realSites;causes.insert(s->relic->cause);}
        if(realSites>=8&&gas)break;
    }
    check(realSites>=2&&causes.size()>1,"Generated histories lack environmental variety");
    std::atomic<bool> cancelled{true};check(!generateDiscoveryPage(h.u,h.system,&cancelled),"Cancelled generation kept producing sites");
    check(!h.receiver.target(),"Receiver leaked exact destination before measurements");
    {DiscoveryController idle;idle.page(h.page);for(unsigned i=0;i<50;++i)idle.update(h.u,h.observer,.1,false);check(idle.records().empty(),"Unattended radio filled discovery journal");}
    h.receiver.tune(300);h.tick(70);
    check(h.receiver.view().decode==0,"Wrong carrier frequency decoded a source");h.tune();
    h.observer.field=true;h.tick(60);check(h.receiver.view().decode==0,"Warp saturation allowed precision listening");h.observer.field=false;
    h.observer.position=-source.metres*1.05;h.aim();h.tick(60);
    check(h.receiver.view().occluded&&h.receiver.view().decode==0,"Radio read through the host planet");
    const auto up=glm::normalize(source.metres),side=tangentFrame(up)*glm::dvec3(1,0,0);
    h.observer.position=source.metres+up*20000.-side*12000.;h.aim();h.tick(70);
    check(h.receiver.view().decoded&&!h.receiver.view().located,"Carrier decoding prematurely localized the source");
    h.receiver.recordBearing();check(h.receiver.record()->count==1&&!h.receiver.target(),"One bearing supplied a range");
    h.receiver.recordBearing();check(h.receiver.record()->count==1,"Stationary listener manufactured a baseline");
    h.observer.position=source.metres+(h.observer.position-source.metres)*1.5;h.aim();h.tick(1,false);h.receiver.recordBearing();
    check(!h.receiver.record()->located,"Collinear bearings manufactured a range");
    auto save=h.receiver.serialize(h.u.seed());DiscoveryController resumed;
    check(resumed.restore(save,h.u)&&resumed.serialize(h.u.seed())==save,"Signal bearings did not survive exact restart");resumed.page(h.page);h.receiver=std::move(resumed);
    h.observer.position=source.metres+up*20000.+side*12000.;h.aim();h.tick(1,false);h.receiver.recordBearing();
    check(h.receiver.record()->located&&h.receiver.target(),"Separated bearings failed to triangulate");
    check(glm::length(h.receiver.record()->estimate-source.metres)<=h.receiver.record()->uncertainty,"Reported search uncertainty excluded the true source");
    const auto& first=site.blocks.front();const auto a=first.metres+first.orientation*glm::dvec3(-30,1,0),b=first.metres+first.orientation*glm::dvec3(30,1,0);
    check(discoveryCollision(h.u,*h.page,source.id.planet,a,b,1).has_value(),"Visible ruin walls have no swept collision");
    check(!discoveryCollision(h.u,*h.page,source.id.planet,a+up*300.,b+up*300.,1),"Ruin collision blocks empty sky");
    for(unsigned evidence=0;evidence<3;++evidence){
        h.receiver.evidence(evidence);const auto& e=site.evidence[evidence];
        h.observer.reference=e.address;h.observer.position=evidence?glm::dvec3(0,0,std::pow(10.,h.u.describe(e.address).frame.logRadius)*2):e.metres+up*15.;
        h.observer.size=1;h.aim();h.tick(50);
        if(evidence)check(!(h.receiver.record()->facts&(1u<<evidence)),"Full-size ship bypassed microscopic inspection");
        h.observer.size=e.requiredSize;h.aim();h.tick(50);
        check(h.receiver.record()->facts&(1u<<evidence),"Physical evidence reading did not complete");
        const auto state=h.receiver.serialize(h.u.seed());DiscoveryController reload;
        check(reload.restore(state,h.u)&&reload.serialize(h.u.seed())==state,"Archaeological evidence changed after restart");reload.page(h.page);h.receiver=std::move(reload);
    }
    check(site.evidence[2].specimen.kind()==Kind::Lattice,"Microscopic reading lacks a canonical lattice source");
    const auto mineral=material(h.u.describe(site.evidence[2].specimen).environment.mineral).name;
    check(site.evidence[2].finding.find(mineral)!=std::string::npos,"Diffraction invented an unrelated mineral");
    while(h.receiver.view().theory==unsigned(site.cause))h.receiver.hypothesis();
    check(h.receiver.conclude().starts_with("HYPOTHESIS")&&!h.receiver.record()->solved,"Incorrect chronology paid out");
    while(h.receiver.view().theory!=unsigned(site.cause))h.receiver.hypothesis();
    check(h.receiver.conclude().starts_with("HISTORY RECONSTRUCTED")&&h.receiver.view().discoveries==1,"Consistent evidence did not reconstruct history");
    h.receiver.conclude();check(h.receiver.view().discoveries==1,"Repeated conclusion duplicated discovery progress");
    h.receiver.toggleMonitor();const auto final=h.receiver.serialize(h.u.seed());DiscoveryController restored;
    check(restored.restore(final,h.u)&&restored.monitoring(),"Pinned receiver did not persist");
    check(!restored.restore(final+" corrupt",h.u)&&restored.serialize(h.u.seed())==final,"Corrupt save changed live discovery state");
    check(!restored.restore(final,Universe(42)),"Discovery record migrated to the wrong universe");
    {const auto next=std::find_if(h.catalog->sources.begin(),h.catalog->sources.end(),[&](const auto& s){return s.relic&&s.id!=source.id;});
        check(next!=h.catalog->sources.end(),"Learning fixture has no second archive");
        auto other=std::make_shared<DiscoveryPage>();other->system=h.system;other->sources.push_back(*next);
        DiscoveryController novice,expert;check(expert.restore(final,h.u),"Learned pattern did not restore");
        novice.page(other);expert.page(other);novice.tune(next->frequency-novice.view().frequency);expert.tune(next->frequency-expert.view().frequency);
        ReceiverObserver probe;probe.reference=next->id.planet;probe.position=next->metres+glm::normalize(next->metres)*20000.;probe.forward=glm::normalize(next->metres-probe.position);
        for(unsigned i=0;i<15;++i){probe.seconds+=.1;novice.update(h.u,probe,.1,true);expert.update(h.u,probe,.1,true);}
        check(expert.view().decode>novice.view().decode*1.05,"Recovered history did not improve archive recognition");}
    check(descriptorFingerprint(h.u.describe(planet.address))==fingerprint,"Discovery generation modified underlying planet descriptors");
    {auto travel=h.receiver;travel.page({});ReceiverObserver galactic;galactic.reference=Address{}.child(Kind::Galaxy,0);
        travel.update(h.u,galactic,.1,false);const auto view=travel.view();
        check(!view.located&&!view.decoded&&!travel.target()&&view.status.starts_with("ENTER A STAR SYSTEM"),"Galactic travel displayed a stale local fix");}
    {
        DiscoveryCache cache;const auto start=std::chrono::steady_clock::now();auto initial=cache.request(h.u,h.system);
        const auto requestMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        check(!initial,"Radio request synchronously generated a catalog");cache.wait();auto ready=cache.request(h.u,h.system);
        check(ready&&ready->sources.size()==h.catalog->sources.size(),"Background radio catalog disagreed with synchronous generation");
        for(unsigned i=0;i<5;++i){cache.request(h.u,Address{}.child(Kind::Galaxy,0).child(Kind::System,i));cache.wait();}
        check(cache.bytes()<1024*1024,"Radio page eviction exceeded its budget");
        std::cout<<"DISCOVERY_PERF request_ms="<<requestMs<<" page_bytes="<<h.catalog->bytes()<<" cache_bytes="<<cache.bytes()<<'\n';
    }
    const auto views=discoveryProofViews(h.u);
    for(const auto& view:views)for(double aspect:{1.,16./9,2.4}){
        FlightInstruments t;t.discovery=std::make_shared<DiscoveryView>(view);const auto hud=cockpitOverlay(t,aspect,"");
        check(cockpitTextConflicts(hud).empty(),"Signal/archaeology console has overlapping text");
        for(const auto& run:hud.textRuns)check(run.bounds.x>=0&&run.bounds.y>=0&&run.bounds.x+run.bounds.w<=900*aspect+.01&&run.bounds.y+run.bounds.h<=900,"Signal console clipped text outside display");
    }
    std::cout<<"DISCOVERY_PASS checks="<<checks<<" tuning_bearings_terrain_materials_chronology_reload=passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"DISCOVERY_FAIL "<<e.what()<<'\n';return 1;}}
