#include "infinity/XalariaNavigation.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/ShipInterior.hpp"
#include "infinity/WarpTravel.hpp"
#include <algorithm>
#include <numbers>
#include <iostream>
#include <stdexcept>
using namespace infinity;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
void checkWarpOptics() {
    WarpOptics optics;
    constexpr double step=1./60.;
    auto previous=optics.update(false,false,0.,.21,step);
    for(int i=0;i<180;++i){
        const auto frame=optics.update(true,true,1e9,.92,step);
        check(frame.closure>=previous.closure&&frame.closure-previous.closure<.05&&
              frame.light>=previous.light&&frame.light-previous.light<.05,"Warp entry flashed or reversed");
        previous=frame;
    }
    check(previous.light>.99&&std::abs(previous.bore-.92)<.0001,"Warp did not settle around the destination");
    for(int i=0;i<180;++i){
        const auto frame=optics.update(false,true,0.,.21,step);
        check(frame.closure<=previous.closure&&previous.closure-frame.closure<.04&&
              frame.light<=previous.light&&previous.light-frame.light<.04&&std::abs(frame.bore-.92)<.0001,
              "Warp exit reset the opening or brightness");
        previous=frame;
    }
    check(previous.light<.004&&previous.closure<.004,"Guided warp field did not fade out");
    WarpOptics rapid;
    for(bool engaged : {true,false,true,false,true,false})
        for(int i=0;i<9;++i){
            const auto frame=rapid.update(engaged,false,0.,.21,step);
            check(std::abs(frame.light-previous.light)<.05&&std::abs(frame.closure-previous.closure)<.05,
                  "Rapid manual warp toggle caused a visual jump");
            previous=frame;
        }
    WarpOptics fast,slow;
    WarpOpticsSample a,b;
    for(int i=0;i<30;++i)a=slow.update(true,false,1e7,.21,1./30.);
    for(int i=0;i<120;++i)b=fast.update(true,false,1e7,.21,1./120.);
    check(std::abs(a.light-b.light)<1e-10&&std::abs(a.closure-b.closure)<1e-10,
          "Warp timing changed with the frame rate");
}
void checkFlightHandoffs() {
    FlightHudStability hud;
    check(!hud.showMarkers() && hud.opacity()==0,"Flight HUD should settle on startup");
    hud.update(.12,0,true);
    check(!hud.showMarkers() && hud.opacity()>0 && hud.opacity()<1,"Flight instruments should fade in");
    hud.update(.30,glm::radians(60.),true);
    check(!hud.showMarkers(),"Destination marker appeared during a fast turn");
    hud.update(.23,0,true);
    check(!hud.showMarkers(),"Destination marker appeared before the view settled");
    hud.update(.02,0,true);
    check(hud.showMarkers() && hud.opacity()==1,"Destination marker did not return");
    hud.update(.08,glm::radians(22.),true);
    check(hud.showMarkers(),"Destination marker flickered near the turning threshold");
    hud.update(.01,0,false);
    check(!hud.showMarkers() && hud.opacity()==0,"Leaving flight should reset the HUD");

    DeckHudLabel room;
    room.update("Cargo bay",.12);
    check(room.text()=="Cargo bay" && room.settled(),"Cargo location did not settle");
    for (int crossing=0;crossing<4;++crossing) {
        room.update("Airlock",.045);
        check(room.text()=="Cargo bay" && room.opacity()==1.f && !room.settled(),
              "Room label reacted to a brief doorway crossing");
        room.update("Cargo bay",.045);
    }
    room.update("Airlock",.12);
    check(room.text()=="Cargo bay" && room.opacity()>0.f && room.opacity()<1.f,
          "Persistent room change did not fade the old location");
    room.update("Airlock",.06);
    check(room.text()=="Airlock" && room.opacity()==0.f,"Room label did not swap at zero opacity");
    room.update("Airlock",.12);
    check(room.settled() && room.opacity()==1.f,"New room label did not fade in");
    room.update("Docking bridge",.04);
    room.update("Airlock",.034);
    check(!room.settled(),"A prompt could flash again while stepping across the same threshold");

    FlightInstruments instruments;
    instruments.targetVisible=true;
    instruments.localTarget={.5,.2,-1};
    instruments.targetLabel=instruments.destination="Ziksa";
    const auto settled=cockpitOverlay(instruments,1.6,"XALARIA");
    const auto turning=cockpitOverlay(instruments,1.6,"XALARIA",{}, {}, {},false,1,nullptr,
                                      {1,0,0,0},nullptr,nullptr,nullptr,false);
    check(settled.strokes.vertices.size()>turning.strokes.vertices.size(),"Stable flight lost its direction cue");
    check(settled.worldLabels.empty() && turning.worldLabels.empty(),"Destination name should stay on the instruments");
    const auto names=[](const CockpitOverlay& h) {
        return std::count_if(h.textRuns.begin(),h.textRuns.end(),[](const auto& run){return run.value=="Ziksa";});
    };
    check(names(settled)>0 && names(settled)==names(turning),"Turning should keep the fixed destination readout");

    // Ordinary flight must keep the ship card steady while the terrain changes render scale.
    instruments.logMetres=3.5;
    for (double rate : {-4., -.019, .019, 4.}) {
        instruments.scaleRate=rate;
        const auto terrainHud=cockpitOverlay(instruments,1.6,"XALARIA");
        check(std::any_of(terrainHud.hitRegions.begin(),terrainHud.hitRegions.end(),[](const auto& panel) {
            return panel.x==24. && panel.y==64. && panel.h==58.;
        }),"Terrain refinement resized the ship card");
        check(std::none_of(terrainHud.textRuns.begin(),terrainHud.textRuns.end(),[](const auto& run) {
            return run.value.starts_with("VIEW ");
        }),"Terrain refinement exposed a render-scale readout in flight");
    }

    ShipInterior deck;
    const CockpitMotionPose seated{};
    check(deck.useSeat(),"Could not stand from the helm");
    const CockpitMotionPose standing{deck.eye(),{},deck.orientation()};
    SeatTransition seat;
    seat.begin(seated,true);
    check(seat.active() && seat.pose(standing).translation==seated.translation,"Standing snapped the camera");
    seat.update(.18);
    const auto rising=seat.pose(standing);
    check(rising.translation.x>0 && rising.translation.x<standing.translation.x &&
          rising.translation.y>standing.translation.y*.5,"Camera should rise while clearing the seat");
    seat.update(.20);
    check(!seat.active() && seat.pose(standing).translation==standing.translation,"Standing missed its final pose");
    seat.begin(standing,false);
    seat.update(.18);
    const auto lowering=seat.pose(seated);
    check(lowering.translation.x<standing.translation.x && lowering.translation.y>0 &&
          std::abs(glm::length(lowering.orientation)-1.)<1e-10,"Seating should ease back to the helm");
    seat.update(.20);
    check(!seat.active() && seat.pose(seated).orientation==seated.orientation,"Seating missed its final orientation");
}
void checkCrewPosition(glm::dvec2 position) {
    using namespace shipDeck;
    // Allow for the full sprite width, including the arm reaching out at a station.
    constexpr double clearance=.46;
    const auto clear=[&](const DeckArea& area) {
        return glm::length(position-glm::clamp(position,glm::dvec2(area.left,area.front),
                                               glm::dvec2(area.right,area.back)))>=clearance;
    };
    check(position.y>=hatch+clearance&&position.y<=cargoBack-clearance,"Crew crossed an end bulkhead");
    for (const auto& crate : cargoCrates)
        check(clear(crate),"Crew silhouette crossed a cargo crate");
    check(clear(bunk)&&clear(systems),"Crew silhouette crossed a room fixture");
    check(clear({-.83,1.41,-.55,2.03})&&clear({1.48,2.05,1.63,3.15}),
          "Crew silhouette crossed a display");
    for (double side : {-1.,1.}) {
        const double x=side*passageHalf;
        check(clear({x-.035,1.71,x+.035,quartersDoorFront+.04})&&
              clear({x-.035,quartersDoorBack-.04,x+.035,cargoFront}),
              "Crew silhouette crossed a passage wall or doorway frame");
        const double cargoEdge=side*cargoDoorHalf, hull=side*cargoHalf;
        check(clear({std::min(cargoEdge,hull)-.036,cargoFront-.04,
                     std::max(cargoEdge,hull)+.036,cargoFront+.04}),
              "Crew silhouette crossed the cargo bulkhead");
        const double left=std::min(x,side*quartersWall),right=std::max(x,side*quartersWall);
        check(clear({left,quartersFront,right,quartersFront})&&clear({left,quartersBack,right,quartersBack}),
              "Crew silhouette crossed a room bulkhead");
    }
}
void checkCrewRounds() {
    ShipInterior deck;
    std::array<unsigned,3> visits{};
    std::array<bool,3> walked{};
    auto previous=deck.crew();
    double elapsed=0;
    for (unsigned tick=0;elapsed<150.;++tick) {
        const double dt=tick%97==0 ? .28 : 1./60;
        elapsed+=dt;
        deck.step({},dt,false,{true,true,elapsed>60.});
        const auto& crew=deck.crew();
        unsigned moving=0;
        for (size_t i=0;i<crew.size();++i) {
            const auto& member=crew[i];
            visits[i]|=1u<<unsigned(member.station);
            moving+=member.moving;
            if (member.moving)
                walked[i]|=member.stride!=previous[i].stride;
            check(glm::length(member.position-previous[i].position)<=.953*dt+1e-9,
                  "Crew teleported between deck stations");
            checkCrewPosition(member.position);
            for (size_t j=0;j<i;++j)
                check(glm::length(crew[j].position-member.position)>.24,"Crew paths crossed another crew member");
        }
        check(moving<=1,"Crew should yield the passage to one transfer at a time");
        previous=crew;
    }
    const auto visited=[&](size_t i,DeckStation station) {return visits[i]&(1u<<unsigned(station));};
    check(walked[0]&&walked[1]&&walked[2],"Every crew member should walk a round");
    check(visited(0,DeckStation::Navigation)&&visited(1,DeckStation::Systems)&&visited(2,DeckStation::Cargo),
          "Crew did not visit the station for their current assignment");
}
void checkShipDeck() {
    ShipInterior deck;
    check(!deck.walking() && deck.eye()==glm::dvec3(0) && !deck.cargoTerminalInReach(),"Player should load at the helm");
    check(deck.useSeat() && deck.seatInReach(),"Could not stand beside the seat");
    deck.look({(std::numbers::pi-2.68)/.0028,0});
    deck.step({1,0},.466/1.58);
    check(deck.position().x>.42,"The player walked through the pilot's seat");
    deck.step({0,1},.31);
    deck.step({1,0},deck.position().x/1.58);
    check(std::abs(deck.position().x)<.001 && deck.hatchOpening()>.99,"Could not line up with the open hatch");
    const auto aft=[&](double z){deck.step({0,1},(z-deck.position().y)/1.58);};
    const auto across=[&](double x){
        const auto amount=x-deck.position().x;
        deck.step({-std::copysign(1.,amount),0},std::abs(amount)/1.58);
    };
    aft(2.60);
    check(deck.room()=="Main passage" && !deck.useSeat(),"Hatch passage or seat reach failed");
    for (unsigned i=0;i<90;++i) {
        deck.step({1,0},1./60);
        for (const auto& member : deck.crew())
            checkCrewPosition(member.position);
    }
    check(deck.room()=="Main passage","Player walked through crew in the doorway");
    const auto stopped=deck.crew().front().position;
    deck.step({},.35);
    check(deck.crew().front().position==stopped,"Crew walked through the player in a doorway");
    deck.step({-1,0},.55/1.58);
    deck.step({0,-1},.38/1.58);
    for (unsigned i=0;i<90 && deck.room()!="Crew quarters";++i)
        deck.step({1,0},1./60);
    check(deck.room()=="Crew quarters","Port quarters are inaccessible");
    check(deck.crew().front().position.x>stopped.x,"Crew did not resume once the doorway cleared");
    across(-3.);
    check(deck.position().x>shipDeck::bunk.right,"Bunk did not block walking");
    aft(2.42);
    across(-.48);
    aft(2.92);
    across(-.48);
    deck.step({},.65);
    aft(3.04);
    for (unsigned i=0;i<360 && deck.room()!="Systems alcove";++i)
        deck.step({-1,0},1./60);
    check(deck.room()=="Systems alcove","Starboard systems alcove is inaccessible");
    across(0);
    aft(5.65);
    // Leave the aisle open if the cargo crew is approaching head-on.
    if (deck.position().y<5.63) {
        across(-.43);
        deck.step({},1.2);
        across(0);
        aft(5.65);
    }
    check(deck.room()=="Cargo bay" && std::abs(deck.position().y-5.65)<.02,"Cargo aisle is inaccessible");
    check(!deck.cargoTerminalInReach(),"Opened the terminal from across the hold");
    across(-3.);
    check(deck.position().x>shipDeck::cargoCrates[2].right,"Cargo crate did not block walking");
    across(0);
    aft(12.);
    check(deck.position().y<shipDeck::cargoScreenZ && deck.cargoTerminalInReach(),
          "Aft console should block walking and be usable from the aisle");
    deck.look({std::numbers::pi/.0028,0});
    check(!deck.cargoTerminalInReach(),"Terminal interacted through the player's back");
    deck.look({std::numbers::pi/.0028,0});
    for (unsigned i=0;i<3 && !deck.seatInReach();++i) {
        const double before=deck.position().y;
        deck.step({0,-1},(before-.83)/1.58);
        if (deck.seatInReach())
            break;
        const double side=deck.position().y>shipDeck::cargoFront ? -.43 : .42;
        across(side);
        deck.step({},1.2);
        if (deck.position().y>1.96)
            deck.step({0,-1},.28/1.58);
        across(0);
    }
    check(deck.seatInReach() && deck.useSeat() && !deck.walking(),"Could not walk back and sit at the helm");
    check(deck.eye()==glm::dvec3(0),"Sitting did not restore the seated camera");
}
void checkBoardingDeck(StationRole role) {
    using namespace stationDeck;
    ShipInterior deck;
    check(deck.useSeat() && deck.departureClear(),"Could not start a walk to the airlock");
    deck.look({(std::numbers::pi-2.68)/.0028,-.075/.0028});
    deck.step({0,1},.46/1.58);
    deck.step({1,0},deck.position().x/1.58);
    deck.step({0,1},(centre-deck.position().y)/1.58);
    deck.look({std::numbers::pi/2/.0028,0});
    deck.step({0,1},1.05/1.58);
    check(std::abs(deck.position().y-centre)<.02 && deck.boardingInteraction()==BoardingInteraction::ShipDoor,
          "Could not reach the port airlock from the cargo aisle");
    deck.useAirlock(BoardingInteraction::ShipDoor);
    deck.step({0,1},1.);
    check(deck.position().x>shipDoor && deck.departureClear(),"Airlock opened in flight");
    deck.setStation(StationKey{23+unsigned(role),role});
    const auto& layout=*deck.station();
    check(deck.boardingInteraction()==BoardingInteraction::ShipDoor,"Closed airlock became unusable at contact distance");
    deck.useAirlock(BoardingInteraction::ShipDoor);
    check(!deck.departureClear(),"Opening the airlock failed to lock departure");
    deck.step({},.05);
    check(deck.airlockMotion()==AirlockMotion::OpeningShip && deck.shipDoorOpening()>0. &&
          deck.shipDoorOpening()<.05,"Ship door did not ease away from the seal");
    deck.step({},.50);
    check(deck.shipDoorOpening()==1.&&deck.bridgeDoorOpening()==0.,"Incorrect pressure door opened");
    deck.step({0,1},(-3.05-deck.position().x)/-1.58);
    check(deck.room()=="Airlock"&&deck.boardingInteraction()==BoardingInteraction::BridgeDoor,
          "Could not reach the outer pressure door");
    deck.useAirlock(BoardingInteraction::BridgeDoor);
    deck.step({},.23);
    check(deck.airlockMotion()==AirlockMotion::SealingShip && deck.shipDoorOpening()>0.&&
          deck.bridgeDoorOpening()==0.,"Both pressure doors opened during the cycle");
    auto singleStep=deck;
    singleStep.step({},.38);
    for (int i=0;i<38;++i)
        deck.step({},.01);
    check(deck.airlockMotion()==AirlockMotion::OpeningBridge && deck.shipDoorOpening()==0. &&
          deck.bridgeDoorOpening()>0.&&deck.bridgeDoorOpening()<.30&&
          std::abs(deck.bridgeDoorOpening()-singleStep.bridgeDoorOpening())<1e-10,
          "Pressure handoff ran faster when a frame crossed the seal");
    deck.step({},.42);
    check(deck.shipDoorOpening()==0.&&deck.bridgeDoorOpening()==1.,"Outer door did not open after sealing the ship");
    const auto along=[&](double x) {
        const double gap=x-deck.position().x;
        deck.step({0,gap<0.?1.:-1.},std::abs(gap)/1.58);
    };
    for (size_t i=1;i<layout.modules.size();++i) {
        const auto& room=layout.modules[i];
        const double centreX=(room.floor.left+room.floor.right)*.5;
        along(centreX);
        check(deck.room()==stationModuleName(room.kind),"Generated room is unreachable or has the wrong name");
        check(deck.aboardDockside()==(room.kind!=StationModuleKind::Bridge),
              "Dockside contact activated before crossing the bridge");
        for (const auto& annex : layout.annexes) {
            if (annex.parent!=i) continue;
            const double blockedX=annex.room.floor.left+.29;
            along(blockedX);
            deck.step({-annex.side,0},3.);
            check(deck.room()==stationModuleName(room.kind),"Player crossed a solid wall beside a side room doorway");
            deck.step({annex.side,0},std::abs(deck.position().y-centre)/1.58);

            along((annex.doorwayLeft+annex.doorwayRight)*.5);
            deck.step({-annex.side,0},5.);
            check(deck.room()==stationModuleName(annex.room.kind)&&
                  std::abs(deck.position().y-annex.outsideWall())<.31&&
                  annex.side*(deck.position().y-annex.outsideWall())<0.,
                  "Could not enter the side room or stop at its wall or window");
            check(deck.boardingInteraction()==BoardingInteraction::None,"Freight could be collected through a side room wall");
            if (annex.panoramic()) {
                const double aisle=(annex.doorwayLeft+annex.doorwayRight)*.5;
                for (const auto& fixture : annex.room.fixtures) {
                    const double gap=(fixture.front+fixture.back)*.5-deck.position().y;
                    deck.step({gap<0.?1.:-1.,0},std::abs(gap)/1.58);
                    const double fixtureX=(fixture.left+fixture.right)*.5;
                    const double edge=fixtureX<aisle?fixture.right:fixture.left;
                    along(fixtureX);
                    const double distance=(deck.position().x-edge)*(fixtureX<aisle?1.:-1.);
                    check(distance>.12 && distance<.19,"Lounge furnishing failed to block movement at its footprint");
                    along(aisle);
                }
            }
            deck.step({annex.side,0},std::abs(deck.position().y-centre)/1.58);
            check(deck.room()==stationModuleName(room.kind)&&std::abs(deck.position().y-centre)<.02,
                  "Could not return to the main corridor through the side room doorway");
        }
    }
    const auto& freightPad=layout.freightPad;
    const double loadingX=freightPad.right+1.05;
    deck.step({0,1},(loadingX-deck.position().x)/-1.58);
    check(deck.room()=="Station loading bay"&&deck.boardingInteraction()==BoardingInteraction::Freight&&
          !deck.useSeat()&&!deck.departureClear(),"Could not walk across the bridge to the freight pad");
    deck.step({0,1},2.);
    check(deck.position().x>freightPad.right,"Walked through the freight pad");
    deck.look({std::numbers::pi/.0028,0});
    deck.step({0,1},(-2.30-deck.position().x)/1.58);
    check(deck.room()=="Airlock"&&deck.boardingInteraction()==BoardingInteraction::ShipDoor,
          "Could not walk back through the bridge");
    deck.useAirlock(BoardingInteraction::ShipDoor);
    deck.step({},1.);
    auto occupiedDoor=deck;
    occupiedDoor.step({0,1},(shipDoor+.22-occupiedDoor.position().x)/1.58);
    occupiedDoor.look({std::numbers::pi/.0028,0});
    check(occupiedDoor.boardingInteraction()==BoardingInteraction::ShipDoor,
          "Could not request a seal from just inside the ship");
    occupiedDoor.useAirlock(BoardingInteraction::ShipDoor);
    occupiedDoor.step({0,1},.085,true);
    occupiedDoor.step({},.5);
    check(occupiedDoor.shipDoorOpening()>=doorClear && occupiedDoor.bridgeDoorOpening()==0.,
          "A sealing door moved into an occupied threshold");
    occupiedDoor.step({0,-1},.5);
    occupiedDoor.step({},.5);
    check(occupiedDoor.departureClear(),"The door did not finish sealing once the threshold was clear");
    deck.step({0,1},(-.8-deck.position().x)/1.58);
    check(deck.room()=="Cargo bay"&&!deck.departureClear(),"Return did not keep the airlock interlocked");
    deck.look({std::numbers::pi/.0028,0});
    deck.useAirlock(BoardingInteraction::ShipDoor);
    deck.step({},.55);
    check(deck.departureClear(),"Sealed airlock still blocked departure");
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
    checkFlightHandoffs();
    checkWarpOptics();
    checkShipDeck();
    for (auto role : {StationRole::Freight,StationRole::Habitat,StationRole::Research,StationRole::Industrial,StationRole::Vault})
        checkBoardingDeck(role);
    checkCrewRounds();
    for(const auto& [id,star]:c->stars){
        if(!star.surveyStar)continue;
        Flight tour(c);
        tour.course.search(star.name);
        check(tour.course.view.selected==id&&tour.course.fly(),"Atlas refused the survey course");
        for(int frame=0;frame<22000&&tour.course.phase()!=XalariaNavigation::Phase::Arrived;++frame)
            tour.step();
        check(tour.course.phase()==XalariaNavigation::Phase::Arrived&&tour.course.view.current==id&&
              tour.course.view.message.find("F6 STAR SCOPE")!=std::string::npos,
              "Survey course did not complete at its viewing world");
    }
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
