#include "infinity/Xalaria.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/Planetology.hpp"
#include "infinity/Piloting.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace infinity;
void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
int main(int argc,char** argv){
 try{
    require(argc==2,"Catalogue path required");
    auto cat=std::make_shared<XalariaCatalog>(argv[1]);
    require(cat->fingerprint=="5923cc4f","Unexpected public snapshot");
    require(cat->stars.size()==7710&&cat->worldCount==48081&&cat->lifeCount==11254,"Snapshot counts differ");
    Universe u(0x58414c4152494101ULL,cat);
    auto galaxy=Address{}.child(Kind::Galaxy,0);
    require(u.children(u.describe(Address{})).size()==1,"Synthetic galaxies added");
    require(std::abs(std::pow(10.,u.describe(galaxy).frame.logRadius)/(110e15)-1)<1e-12,"Public map scale changed");
    auto all=u.children(u.describe(galaxy),10000);require(all.size()==cat->stars.size(),"Galaxy has synthetic/missing stars");
    size_t checked=0,worlds=0;
    for(const auto& [id,s]:cat->stars){
        auto a=cat->address(id);const auto n=u.describe(a);
        require(n.seed==seedOf(a,u.seed()),"Engine cache identity changed");
        require(n.label()==s.name,"Star name mismatch");
        require(std::abs(n.stellarRadiusMetres/s.radius-1)<1e-12,"Star radius units");
        require(n.temperatureK==s.temperature,"Star temperature mismatch");
        auto addresses=u.children(n,1000);require(addresses.size()==s.worlds.size(),"Lost planets or moons");
        worlds+=addresses.size();
        for(auto target:s.wormholes)require(cat->stars.contains(target),"Dangling wormhole");
        if((checked++%31)!=0)continue;
        for(const auto& address:addresses){
            const auto w=u.describe(address);const auto& source=cat->world(address);
            require(w.seed==seedOf(address,u.seed()),"World cache identity changed");
            require(w.label()==source.name,"World name mismatch");
            require(std::abs(std::pow(10.,w.frame.logRadius)/source.radius-1)<1e-12,"World radius units");
            require(w.environment.temperature==std::max(2.725,source.temperature),"World temperature mismatch");
            require(hasSolidSurface(w)==(source.composition==1),"Solid and gas classification differ");
            require(std::isfinite(glm::length(w.frame.position)),"Nonfinite world position");
            const auto traits=planetTraits(w);require(traits.rings.innerRadius==source.ringInner&&traits.rings.outerRadius==source.ringOuter,"Ring bounds differ");
            require(isMoon(address)==(source.primary>=0),"Moon identity mismatch");
            if(source.primary>=0)require(w.satelliteOf==uint64_t(source.primary),"Moon parent mismatch");
        }
    }
    require(worlds==48081,"World total changed");
    auto from=14940u;const auto to=cat->star(from).wormholes.front();
    const auto route=cat->route(from,to);require(route==std::vector<uint32_t>({from,to}),"Direct link was not shortest");
    require(cat->route(from,from)==std::vector<uint32_t>{from},"Identity route");
    require(cat->route(63,from).empty(),"Isolated star should be disconnected");
    const auto matches=cat->search("oylanhai");require(std::find(matches.begin(),matches.end(),from)!=matches.end(),"Search case folding");
    for(const auto target:{66u,10000u,25000u}){
        if(!cat->stars.contains(target))continue;
        auto path=cat->route(from,target);
        for(size_t i=1;i<path.size();++i){const auto& links=cat->star(path[i-1]).wormholes;
            require(std::find(links.begin(),links.end(),path[i])!=links.end(),"Route invented an edge");}
    }
    XalariaMapView view;view.catalog=cat.get();view.current=from;view.selected=to;view.route=route;
    auto overlay=xalariaMapOverlay(view,1.6);
    require(!overlay.panels.vertices.empty()&&!overlay.strokes.vertices.empty(),"Native map missing");
    for(const auto& run:overlay.textRuns)require(run.bounds.x>=0&&run.bounds.y>=0&&run.bounds.x+run.bounds.w<=1440&&run.bounds.y+run.bounds.h<=900,"Map text outside canvas");
    Navigation nav(u);auto planet=cat->address(from).child(Kind::Planet,0);auto n=u.describe(planet);
    nav.changeFrame(planet);nav.setRenderScale(n.frame.logRadius);nav.offset={0,0,1.5};
    RegionCache cache(u);FlightController controller(u,nav,cache);Pilot pilot(u,nav,controller);
    auto before=nav.cameraPosition();
    for(int i=0;i<120;++i){HelmInput input;input.throttle=1;pilot.update(input,1./60);}
    require(pilot.ship().travelMetres>0&&glm::length(nav.cameraPosition()-before)>0,"Real-time flight did not move");
    require(pilot.warpTo(cat->address(to)),"Flight rejected reachable target");
    auto saved=pilot.serialize();require(pilot.restore(saved),"Flight save did not restore");
    GeometryGenerator generator(u);auto samples=generator.generate(u.describe(galaxy),0);
    require(samples->particles.size()==cat->stars.size(),"Galaxy render invented stars");
    controller.observeGalaxy(samples);
    require(pilot.warpTo(cat->address(to).child(Kind::Planet,0)),"Wormhole planet route refused");
    const auto beforeArrival=pilot.instruments().warpArrivalSequence;
    for(int i=0;i<12000&&pilot.instruments().warpArrivalSequence==beforeArrival;++i){
        cache.beginFrame();pilot.update({},.1);cache.finishFrame();
    }
    require(pilot.instruments().warpArrivalSequence>beforeArrival,"Real-time interstellar route did not arrive");
    auto final=nav.anchor;while(final.kind()>Kind::System)final=final.parent();
    require(final.kind()==Kind::System&&final.id()==to,"Arrival was in the wrong system");

    std::cout<<"ARRIVAL flight_seconds="<<pilot.ship().flightSeconds<<" travel_m="<<pilot.ship().travelMetres<<" system="<<final.id()<<"\n";
    std::cout<<"PASS catalogue=7710 stars/48081 worlds; descriptor samples; names; moon identities; directed routes; map bounds; real flight; save restore; complete interstellar arrival\n";
    return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
