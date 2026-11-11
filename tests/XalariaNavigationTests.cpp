#include "infinity/XalariaNavigation.hpp"
#include "infinity/Cockpit.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace infinity;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
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
        for(const auto* overlay:{&galaxy,&system})for(const auto& text:overlay->textRuns)
            check(text.bounds.x>=0&&text.bounds.y>=0&&text.bounds.x+text.bounds.w<=layout.width+.01&&text.bounds.y+text.bounds.h<=900,"Navigation text outside viewport");
        v.mode=XalariaMapView::Mode::Galaxy;
    }
    restored.course.cancel();
    restored.course.selectFromFlight(c->address(destination).child(Kind::Planet,0));
    const auto target=restored.course.instruments();
    check(target.targetLabel==c->star(destination).worlds.at(0).name&&target.targetVisible&&target.range>0,"Planned map world did not reach the cockpit target");
    check(!restored.course.view.flying&&!restored.course.view.paused&&restored.course.view.activeRoute.empty(),"Cancel retained an active journey");
    std::cout<<"PASS immutable routes, two-hop flight, frozen final world, pause/resume, checkpoint recovery, live frame position, cockpit/world selection, map clicks and cursor-anchored zoom\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
