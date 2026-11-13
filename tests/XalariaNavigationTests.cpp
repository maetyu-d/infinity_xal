#include "infinity/XalariaNavigation.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/ShipInterior.hpp"
#include <algorithm>
#include <numbers>
#include <iostream>
#include <stdexcept>
using namespace infinity;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
void checkShipDeck() {
    ShipInterior deck;
    check(!deck.walking() && deck.eye()==glm::dvec3(0),"Player should load at the helm");
    check(deck.useSeat() && deck.seatInReach(),"Could not stand beside the seat");
    deck.look({(std::numbers::pi-2.68)/.0028,0});
    deck.step({1,0},.466/1.58);
    check(deck.position().x>.42,"The player walked through the pilot's seat");
    deck.step({0,1},.31);
    deck.step({1,0},deck.position().x/1.58);
    check(std::abs(deck.position().x)<.001 && deck.hatchOpening()>.99,"Could not line up with the open hatch");
    const auto aft=[&](double z){deck.step({0,1},(z-deck.position().y)/1.58);};
    const auto forward=[&](double z){deck.step({0,-1},(deck.position().y-z)/1.58);};
    const auto across=[&](double x){
        const auto amount=x-deck.position().x;
        deck.step({-std::copysign(1.,amount),0},std::abs(amount)/1.58);
    };
    aft(2.60);
    check(deck.room()=="Main passage" && !deck.useSeat(),"Hatch passage or seat reach failed");
    across(-1.14);
    check(deck.room()=="Crew quarters","Port quarters are inaccessible");
    across(-3.);
    check(deck.position().x>shipDeck::bunk.right,"Bunk did not block walking");
    across(1.14);
    check(deck.room()=="Systems alcove","Starboard systems alcove is inaccessible");
    across(0);
    aft(5.65);
    check(deck.room()=="Cargo bay","Cargo bay is inaccessible");
    across(-3.);
    check(deck.position().x>shipDeck::cargoCrates[2].right,"Cargo crate did not block walking");
    across(0);
    aft(12.);
    check(deck.position().y<shipDeck::cargoBack,"Player escaped through the aft bulkhead");
    forward(.67);
    check(deck.seatInReach() && deck.useSeat() && !deck.walking(),"Could not walk back and sit at the helm");
    check(deck.eye()==glm::dvec3(0),"Sitting did not restore the seated camera");
}
struct Flight {
    Universe universe;
    Navigation nav;
    RegionCache cache;
    FlightController controller;
    Pilot pilot;
    XalariaNavigation course;
    explicit Flight(std::shared_ptr<const XalariaCatalog> c):universe(0x58414c4152494101ULL,c),nav(universe),cache(universe),controller(universe,nav,cache),pilot(universe,nav,controller),course(universe,nav,pilot){
        auto world=c->address(14940).child(Kind::Planet,0);const auto p=universe.describe(world);
        nav.changeFrame(world);nav.setRenderScale(p.frame.logRadius);nav.offset={0,0,1.5};pilot.synchronizeFrame();course.update(0);course.selectSystem(14940);
        GeometryGenerator generator(universe);controller.observeGalaxy(generator.generate(universe.describe(c->address(14940).parent()),0));
    }
    void step(){cache.beginFrame();pilot.update({},.1);course.update(.1);cache.finishFrame();}
};
int main(int argc,char** argv){try{
    check(argc==2,"Catalogue required");auto c=std::make_shared<XalariaCatalog>(argv[1]);Flight f(c);
    checkShipDeck();
    {
        Flight banking(c);
        banking.nav.changeFrame(c->address(14940));
        banking.nav.setRenderScale(13);
        banking.nav.offset={0,0,2};
        banking.nav.orientation=glm::angleAxis(1.38,glm::dvec3(0,0,-1));
        banking.pilot.resetMotion();
        banking.pilot.synchronizeFrame();
        banking.course.update(0);
        check(banking.course.levelRollInput()<-.6,"Level control turned an existing right bank further right");
        check(!xalariaBankAngle({0,0,1}),"Vertical flight has an ambiguous bank reference");
        const auto forward=banking.nav.orientation*glm::dvec3(0,0,-1);
        for (int frame=0;frame<300;++frame) {
            HelmInput input;
            input.roll=banking.course.levelRollInput();
            banking.course.advanceFlight(input,1./60);
        }
        const auto bank=xalariaBankAngle(banking.course.instruments().localUp);
        check(bank&&std::abs(*bank)<.045,"Level control did not recover from a right bank");
        check(glm::length(banking.nav.orientation*glm::dvec3(0,0,-1)-forward)<1e-7,
              "Roll leveling changed the forward bearing");
    }
    const auto first=c->star(14940).wormholes.front();uint32_t destination=0;
    for(auto id:c->star(first).wormholes)if(c->route(14940,id).size()==3){destination=id;break;}
    check(destination!=0,"No two-hop fixture");
    f.course.selectSystem(destination);f.course.selectWorld(0);
    const auto committed=c->route(14940,destination);check(f.course.fly(),"Could not start route");
    const auto firstTarget=f.pilot.instruments().warpDestination;check(firstTarget.has_value(),"Flight target missing");
    f.course.selectSystem(63);f.course.selectWorld(c->star(63).worlds.begin()->first);
    check(f.course.view.route.empty(),"Isolated preview should be unreachable");
    check(f.course.view.activeRoute==committed&&f.course.view.destination==destination&&f.course.view.activeWorld==0,"Browsing changed the committed destination");
    check(f.pilot.instruments().warpDestination==firstTarget,"Browsing retargeted the helm");
    const auto eye=f.nav.cameraPosition();f.course.pause();
    check(f.course.view.paused&&f.nav.cameraPosition()==eye,"Pause teleported or lost course");
    const auto checkpoint=f.course.serialize();
    Flight restored(c);check(restored.nav.restore(f.nav.serialize()),"Nav restore failed");check(restored.pilot.restore(f.pilot.serialize()),"Ship restore failed");
    check(restored.course.restore(checkpoint),"Journey restore failed");
    check(restored.course.view.paused&&restored.course.view.activeRoute==committed&&restored.course.view.activeWorld==0,"Saved journey lost its route or world");
    auto corrupted=checkpoint;corrupted.replace(corrupted.find(c->fingerprint),c->fingerprint.size(),"foreign!");
    const auto before=restored.course.serialize();check(!restored.course.restore(corrupted)&&restored.course.serialize()==before,"Foreign checkpoint changed state");
    check(restored.course.resume(),"Cannot resume paused route");
    for(int n=0;n<18000&&restored.pilot.instruments().warpArrivalSequence<2;++n)restored.step();
    check(restored.pilot.instruments().warpArrivalSequence==2,"Two-hop route did not finish");
    check(restored.course.view.current==destination,"Route arrived in wrong system");
    check(restored.pilot.instruments().warpDestination==c->address(destination).child(Kind::Planet,0),"Browsing changed the final world during the second hop");
    check(restored.course.phase()==XalariaNavigation::Phase::FinalApproach,"Final approach not distinguished from transit");
    const auto position=restored.course.view.ship;
    restored.nav.changeFrame(restored.nav.anchor.parent());restored.pilot.synchronizeFrame();restored.course.update(0);
    check(glm::length(position-restored.course.view.ship)<1e-8,"Live map position jumps during reference change");
    restored.course.selectFromFlight(c->address(destination).child(Kind::Planet,0));
    check(restored.course.view.selected==destination&&restored.course.view.selectedWorld==0&&restored.course.view.mode==XalariaMapView::Mode::System,"Cockpit selection failed to open corresponding world");
    const auto name=c->star(66).worlds.rbegin()->second.name;
    restored.course.search(name);
    check(restored.course.view.selectedWorld.has_value(),"World search only selected its star");
    restored.course.search("no such world or star here");check(restored.course.view.matches.empty(),"No-match search did not clear results");
    auto v=restored.course.view;v.mode=XalariaMapView::Mode::Galaxy;v.selected=14940;v.current=14940;v.centre={140,74};v.span=25;v.selectedWorld=0;
    for(double aspect:{1.,1.25,1.6,2.2}){
        const XalariaMapLayout layout(aspect);
        const auto at=xalariaMapPoint(v,14940,aspect);check(xalariaPickStar(v,at,aspect)==14940,"Star click mismatch");
        const auto grid=xalariaMapGrid(v,at,aspect);check(glm::length(grid-glm::dvec2(140,74))<1e-9,"Map projection inversion");
        auto zoom=v;const auto point=at+glm::dvec2(29,21);const auto anchor=xalariaMapGrid(zoom,point,aspect);
        xalariaZoomMap(zoom,3,point,aspect);check(glm::length(anchor-xalariaMapGrid(zoom,point,aspect))<1e-9,"Zoom moved the cursor anchor");
        auto galaxy=xalariaMapOverlay(v,aspect);v.mode=XalariaMapView::Mode::System;
        auto system=xalariaMapOverlay(v,aspect);
        const auto wp=xalariaSystemPoint(v,0,aspect);check(xalariaPickWorld(v,wp,aspect)==0,"World click mismatch");
        const auto checkLayout=[&](const CockpitOverlay& overlay) {
            check(!overlay.atlasGlyphs.empty(),"Atlas lettering missing");
            check(cockpitTextConflicts(overlay).empty(),"Atlas text overlaps");
            for(const auto& text:overlay.textRuns)
                check(text.bounds.x>=0&&text.bounds.y>=0&&text.bounds.x+text.bounds.w<=layout.width+.01&&
                      text.bounds.y+text.bounds.h<=900,"Navigation text outside viewport");
        };
        checkLayout(galaxy);
        checkLayout(system);
        auto search=v;
        search.query="World or star";search.matches={14940,destination};search.matchIndex=1;
        checkLayout(xalariaMapOverlay(search,aspect));
        search.matches.clear();
        checkLayout(xalariaMapOverlay(search,aspect));
        auto route=v;route.mode=XalariaMapView::Mode::Galaxy;
        route.selected=destination;route.route={14940,destination};route.activeRoute=route.route;
        route.destination=destination;route.paused=true;route.showLinks=true;
        checkLayout(xalariaMapOverlay(route,aspect));
        v.mode=XalariaMapView::Mode::Galaxy;
    }
    auto system=v;
    system.selected=destination;system.selectedWorld.reset();system.activeRoute.clear();
    const auto& systemWorlds=c->star(system.selected).worlds;
    const auto moon=std::find_if(systemWorlds.begin(),systemWorlds.end(),[](const auto& item){return item.second.primary>=0;});
    check(moon!=systemWorlds.end(),"Fixture needs a moon");
    check(!xalariaSystemShowsWorld(system,moon->first),"Unfocused moon clutters the system map");
    system.selectedWorld=uint64_t(moon->second.primary);
    check(xalariaSystemShowsWorld(system,moon->first),"Selected planet's moons are missing");
    system.selectedWorld=moon->first;
    check(xalariaPickWorld(system,xalariaSystemPoint(system,moon->first,1.6),1.6)==moon->first,
          "Selected moon cannot be picked");
    restored.course.cancel();
    restored.course.selectFromFlight(c->address(destination).child(Kind::Planet,0));
    const auto target=restored.course.instruments();
    check(target.targetLabel==c->star(destination).worlds.at(0).name&&target.targetVisible&&target.range>0,"Planned map world did not reach the cockpit target");
    check(!restored.course.view.flying&&!restored.course.view.paused&&restored.course.view.activeRoute.empty(),"Cancel retained an active journey");
    std::cout<<"PASS immutable routes, two-hop flight, frozen final world, pause/resume, checkpoint recovery, live frame position, cockpit/world selection, map clicks and cursor-anchored zoom\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
