#include "infinity/XalariaNavigation.hpp"
#include "infinity/XalariaEconomy.hpp"
#include "infinity/XalariaTraffic.hpp"
#include "infinity/XalariaStories.hpp"
#include "infinity/XalariaCombat.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/Renderer.hpp"
#include "infinity/ShipInterior.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <oneapi/tbb/global_control.h>
using namespace infinity;
namespace {
std::string read(const std::filesystem::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
void write(const std::filesystem::path& p,const std::string& s){
    auto t=p;t+=".tmp";{std::ofstream f(t);f<<s;if(!f)throw std::runtime_error("Could not save flight");}std::filesystem::rename(t,p);
}
Address homeWorld(const XalariaCatalog& c,uint32_t id){
    const auto& s=c.star(id);
    for(const auto& [wid,w]:s.worlds)if(w.primary<0&&w.stations)return c.address(id).child(Kind::Planet,wid);
    for(const auto& [wid,w]:s.worlds)if(w.primary<0&&w.composition==1&&w.life)return c.address(id).child(Kind::Planet,wid);
    for(const auto& [wid,w]:s.worlds)if(w.primary<0)return c.address(id).child(Kind::Planet,wid);
    return c.address(id);
}
void eraseCharacter(std::string& s){if(s.empty())return;size_t i=s.size()-1;while(i&&(static_cast<unsigned char>(s[i])&0xc0)==0x80)--i;s.resize(i);}
}
int main(int argc,char** argv){
    SDL_Window* window=nullptr;
    try{
        const auto executable=std::filesystem::weakly_canonical(std::filesystem::absolute(argv[0]));
        const bool bundle=executable.parent_path().filename()=="MacOS";
        std::filesystem::path catalogue=bundle?executable.parent_path().parent_path()/"Resources/catalog.tsv":std::filesystem::path(XALARIA_DATA_DIR)/"catalog.tsv";
        std::filesystem::path save=executable.parent_path()/"xalaria-save",captures;
        unsigned frames=0;bool explicitData=false,fresh=false,smoke=false,flightView=false,journeyProof=false,atlasProof=false,economyProof=false,checkpointProof=false,fleetProof=false,sunsetProof=false,reflections=true,spaceGlass=true,spaceProof=false,lensEffects=true;uint32_t start=14940;
        for(int i=1;i<argc;++i){
            const std::string a=argv[i];auto value=[&](){if(++i==argc)throw std::runtime_error("Missing option value");return std::string(argv[i]);};
            if(a=="--catalog")catalogue=value();else if(a=="--data"){save=value();explicitData=true;}
            else if(a=="--frames")frames=unsigned(std::stoul(value()));else if(a=="--capture-dir")captures=value();
            else if(a=="--start")start=uint32_t(std::stoul(value()));else if(a=="--fresh")fresh=true;
            else if(a=="--smoke-test")smoke=true;else if(a=="--flight-view")flightView=true;
            else if(a=="--journey-test")journeyProof=true;else if(a=="--atlas-test")atlasProof=true;
            else if(a=="--no-reflections")reflections=false;
            else if(a=="--no-lens-effects")lensEffects=false;
            else if(a=="--no-glass-space")spaceGlass=false;
            else if(a=="--space-test"){spaceProof=true;flightView=true;}
            else if(a=="--sunset-test"){sunsetProof=true;flightView=true;}
            else if(a=="--economy-test")economyProof=true;else if(a=="--checkpoint-test")checkpointProof=true;
            else if(a=="--fleet-test")fleetProof=true;
            else if(a=="--help"){std::cout<<"Infinity / Xalaria 0.68.0\nF4 economy. In economy: M manual dock, D auto dock. During manual docking: J auto dock, Ctrl fine thrust. Enter undocks.\nF2 map/flight. F3 galaxy/system. Type to search; Tab cycles results. Click worlds; Enter flies. Space pauses/resumes a course.\nMap: drag pan, scroll zoom, Home locate ship, Page Up/Down world list.\nFlight: WASD and Q/E thrust, arrows turn, U/O or V/Z roll left/right, hold R to level, C mouse steering, hold Shift and move mouse to look around, left Option/Alt boost, B cancel/brake, X cruise, H target manual wormhole, K/Shift-K cycle crews, I hail, 7 request ship docking in radio panel, J fly selected world, Y next world, L land, G gear, F1 help.\nShip interior: F5 stand up, WASD walk, mouse or arrows look, Shift walk faster. E sits at the helm or opens the aft cargo terminal while docked.\nBoarding: click a station to open its market. At any clamped orbital station, E at each airlock door, then E at the sealed freight in the loading bay. Use the same airlock when clamped to a crewed ship, then collect the courier parcel in its transfer bay.\nN scans a tracked investigation or discovers a new lead in your orbit. F7 opens discovered Dispatch leads. T tracks a lead; Tab cycles Nearby, News, Local leads, Follow-ups, Journal and Discoveries. Page Up/Down browses your discoveries. B opens the case file; at the aft terminal, B opens case files and E keeps the docked market. Enter opens the assignment or next chapter; 1 or 2 files a decision at the site.\nF9 opens discovered convoy operations. E accepts escort, P marks a prize, F follows with helm assist, I hails, and R reports at the destination berth. Escort pay requires at least 45 seconds accompanying a surviving delivery.\nF8 opens ship fitting, cargo demands, prize cargo, patrol vouchers and repair/rearm. M arms weapons in flight; hold Space to fire; T cycles visible ships; 1/2/3 select pulse, ion or torpedoes while radio is closed. Ion disables engines for boarding. E in a captured ship transfer bay seizes cargo.\nF10 opens Fleet Command: C claim a boarded disabled hull, H commission captain, B/W buy freighter/warship. Orders 0-5 use the atlas destination. Tab switches earned intelligence and campaigns.\nF6 opens the stellar survey view in Rhyvane, Nereth, Veyru and Cindrel.\n--catalog FILE --data DIR --start SEED --fresh --frames N --capture-dir DIR --flight-view --journey-test --atlas-test --economy-test --fleet-test\n";return 0;}
            else throw std::runtime_error("Unknown option: "+a);
        }
        if(!explicitData&&bundle){char* pref=SDL_GetPrefPath("Infinity","Xalaria");if(!pref)throw std::runtime_error(SDL_GetError());save=pref;SDL_free(pref);}
        auto catalog=std::make_shared<XalariaCatalog>(catalogue);catalog->star(start);
        oneapi::tbb::global_control workers(oneapi::tbb::global_control::max_allowed_parallelism,3);
        Universe universe(0x58414c4152494101ULL,catalog);Navigation nav(universe);
        const auto home=homeWorld(*catalog,start),body=home;const auto descriptor=universe.describe(body);
        nav.changeFrame(home);nav.setRenderScale(descriptor.frame.logRadius);
        const auto direction=glm::normalize(glm::dvec3(.3,.4,1));nav.orientation=glm::quatLookAtRH(-direction,glm::dvec3(0,1,0));
        nav.offset=direction*2.25-nav.orientation*glm::dvec3(0,0,1);nav.selected.reset();
        RegionCache cache(universe);FlightController flight(universe,nav,cache);Pilot pilot(universe,nav,flight);
        std::filesystem::create_directories(save);
        std::string navigationSave,shipSave,routeSave,economySave,portSave,trafficSave,storySave,combatSave,convoySave,strategySave;
        if(!fresh){
            const auto checkpoint=read(save/"session.txt");
            if(!checkpoint.empty()){
                std::istringstream in(checkpoint);std::string tag,fingerprint,extra;int version;
                if(!(in>>tag>>version>>fingerprint>>std::quoted(navigationSave)>>std::quoted(shipSave)>>std::quoted(routeSave)>>std::quoted(economySave)>>std::quoted(portSave))||
                    tag!="XALARIA_SESSION"||(version!=1&&version!=2&&version!=3&&version!=4&&version!=5)||fingerprint!=catalog->fingerprint)
                    throw std::runtime_error("The saved session could not be read; it has been preserved");
                in>>std::ws;
                if(version>=2){
                    if(in.peek()!='"'||!(in>>std::quoted(trafficSave)))
                        throw std::runtime_error("Saved traffic could not be read; the session has been preserved");
                    in>>std::ws;
                    if(in.peek()!='"'||!(in>>std::quoted(storySave))||storySave.empty())
                        throw std::runtime_error("The dispatch journal could not be read; the session has been preserved");
                    in>>std::ws;
                    if(version>=3&&in.peek()!='"')throw std::runtime_error("The saved armament record is missing");
                    if(version>=3&&(!(in>>std::quoted(combatSave))||combatSave.empty()))throw std::runtime_error("The saved armament could not be read");
                    if(version>=4){in>>std::ws;if(in.peek()!='"'||!(in>>std::quoted(convoySave))||convoySave.empty())throw std::runtime_error("Saved convoy operations could not be read");}
                    if(version>=5){in>>std::ws;if(in.peek()!='"'||!(in>>std::quoted(strategySave))||strategySave.empty())throw std::runtime_error("Saved fleet and faction simulation could not be read");}
                    if(in>>extra)throw std::runtime_error("Unexpected saved session fields");
                }else if(in.peek()!=std::char_traits<char>::eof()&&
                         (in.peek()!='"'||!(in>>std::quoted(trafficSave))||(in>>extra)))
                    throw std::runtime_error("Saved traffic could not be read; the session has been preserved");
            }else if(read(save/"fingerprint.txt")==catalog->fingerprint){
                navigationSave=read(save/"navigation.txt");shipSave=read(save/"ship.txt");routeSave=read(save/"route.txt");
                economySave=read(save/"economy.txt");portSave=read(save/"port.txt");
            }
            if(!navigationSave.empty()&&(!nav.restore(navigationSave)||!pilot.restore(shipSave)))throw std::runtime_error("The saved flight could not be read; it has been preserved");
        }
        if(sunsetProof){
            // Native capture fixture at the real world's terminator, oriented
            // toward its actual star. It changes only the isolated test profile.
            const auto planet=universe.describe(home);
            const double radius=std::pow(10.,planet.frame.logRadius);
            const auto light=glm::normalize(universe.relative(catalog->address(start),home,0).position);
            const auto axis=std::abs(light.y)<.85?glm::dvec3(0,1,0):glm::dvec3(1,0,0);
            auto up=glm::normalize(glm::cross(light,axis));
            up=glm::normalize(up+light*.006);
            const auto forward=glm::normalize(light-up*glm::dot(light,up)-up*.045);
            nav.changeFrame(home);nav.setRenderScale(planet.frame.logRadius);
            nav.orientation=glm::quatLookAtRH(forward,up);
            nav.offset=up*(1+(universe.terrainHeight(planet,up)+12000)/radius)-nav.orientation*glm::dvec3(0,0,1);
            nav.selected.reset();pilot.resetMotion();
        }
        if(spaceProof){
            nav.changeFrame(catalog->address(start));nav.setRenderScale(13);
            const glm::dvec3 eye(.7,.25,2.8);
            nav.orientation=glm::quatLookAtRH(glm::normalize(-eye),glm::dvec3(0,1,0));
            nav.offset=eye-nav.orientation*glm::dvec3(0,0,1);nav.selected.reset();pilot.resetMotion();
        }
        pilot.synchronizeFrame();XalariaEconomy economy(universe);
        const auto unixNow=[](){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();};
        if(!fresh&&!economySave.empty()&&!economy.restore(economySave))throw std::runtime_error("Economy save could not be read; the previous save has been preserved");
        XalariaStories stories(economy,*catalog);
        if(!stories.restore(storySave))
            throw std::runtime_error("The dispatch journal could not be read; the session has been preserved");
        stories.advance(pilot.ship().flightSeconds);
        economy.setStories(&stories);
        XalariaCombat combat(universe,economy,pilot);
        if(!combat.restore(combatSave,pilot.ship().flightSeconds))throw std::runtime_error("The saved armament could not be read; your session has been preserved");
        economy.setCombat(&combat);auto& convoys=combat.convoys();
        if(!economy.restoreTraffic(trafficSave,pilot.ship().flightSeconds))
            throw std::runtime_error("Saved traffic could not be read; the session has been preserved");

        if(!convoys.restore(convoySave,pilot.ship().flightSeconds))throw std::runtime_error("Saved convoy operations could not be read; your session has been preserved");
        auto& strategy=combat.strategy();
        if(!strategy.restore(strategySave,pilot.ship().flightSeconds))throw std::runtime_error("Saved fleet and factions could not be read; the session has been preserved");
        economy.catchUp(unixNow());economy.sync(nav,pilot);
        XalariaNavigation course(universe,nav,pilot);course.setEconomy(&economy);auto& map=course.view;
        ActivityState activity;
        activity.seconds=pilot.ship().flightSeconds;
        std::optional<XPlace> pendingPort,serviceDestination;
        std::optional<ActivityId> savedVessel;
        std::string savedVesselMode;
        bool resumeManualDock=false;
        if(!fresh){std::istringstream savedPort(portSave);std::string fingerprint;XPlace p;
            savedPort>>fingerprint;
            if(fingerprint=="VESSEL"){
                std::string home;
                uint32_t slot;
                if(savedPort>>home>>slot>>savedVesselMode){
                    try{
                        const ActivityId id{Address::parse(home),slot};
                        if(isXalariaTraffic(id))savedVessel=id;
                    }catch(const std::exception&){}
                }
            }else if(fingerprint==catalog->fingerprint&&savedPort>>p.system>>p.world>>p.station>>p.city&&
                     catalog->stars.contains(p.system)&&catalog->star(p.system).worlds.contains(p.world)){
                const auto list=economy.venues(p.system,p.world);
                if(std::find(list.begin(),list.end(),p)!=list.end()){
                    pendingPort=p;
                    std::string mode;
                    resumeManualDock=bool(savedPort>>mode)&&mode=="manual";
                }
            }
        }
        if(economyProof){
            for(const auto& [wid,w]:catalog->star(start).worlds)if(w.stations){
                const XPlace p{start,wid,0,false};const auto berth=*dockingBerth(economy.portBody(p));
                nav.changeFrame(berth.id.home);nav.setRenderScale(universe.describe(berth.id.home).frame.logRadius);
                nav.orientation=berth.orientation;nav.offset=berth.metres/std::pow(10.,nav.logDistance)-nav.orientation*glm::dvec3(0,0,1);
                pilot.synchronizeFrame();economy.sync(nav,pilot);economy.show(start,wid);economy.travelToPort(p,nav,pilot);break;
            }
        }
        if(!fresh&&!routeSave.empty()&&!course.restore(routeSave))throw std::runtime_error("The saved route could not be read; it has been preserved");
        map.open=navigationSave.empty()&&!flightView&&!economyProof;
        if(journeyProof){course.selectSystem(catalog->star(map.current).wormholes.at(0));if(!course.fly())throw std::runtime_error("Test departure refused");}
        if(atlasProof){course.selectSystem(catalog->star(map.current).wormholes.at(0));xalariaFitRoute(map,1.6);}
        SDL_SetAppMetadata("Infinity / Xalaria","0.68.0","org.infinity.xalaria");
        if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS))throw std::runtime_error(SDL_GetError());
        window=SDL_CreateWindow("INFINITY / XALARIA",1440,900,SDL_WINDOW_RESIZABLE);
        if(!window)throw std::runtime_error(SDL_GetError());SDL_SetWindowMinimumSize(window,960,640);SDL_SyncWindow(window);
        {
            Renderer renderer(window,universe);renderer.setSceneReflections(reflections);renderer.setSpaceGlass(spaceGlass);renderer.setLensEffects(lensEffects);renderer.setXalariaMap(&map);renderer.setXalariaEconomy(&economy);renderer.setActivity(&activity);renderer.setXalariaCombat(&combat);renderer.setWeapons(&combat.effects());
            XalariaRadioView radio;
            renderer.setXalariaRadio(&radio);
            std::optional<ActivityId> trafficHold;
            std::string trafficHoldLabel,trafficHoldCallsign;
            TrafficHoldProgress holdProgress;
            int pw,ph;SDL_GetWindowSizeInPixels(window,&pw,&ph);renderer.resize(pw,ph);
            bool running=true,looking=false,searching=false,contactsExpanded=false,dragging=false,moved=false,querySelected=false,swallowSpace=false;
            bool cockpitLookActive = false;
            bool interiorMouse = true, interiorHelp = false;
            ShipInterior interior;
            renderer.setShipInterior(&interior);
            std::string freightNotice;
            double freightNoticeTime = 0.;
            glm::dvec2 cockpitLook{};
            std::array<bool,SDL_SCANCODE_COUNT> heldAcrossMode{};
            const auto maskHeldKeys = [&]() {
                int count=0;
                const auto* keys=SDL_GetKeyboardState(&count);
                std::copy_n(keys,std::min(heldAcrossMode.size(),size_t(count)),heldAcrossMode.begin());
            };
            SDL_PumpEvents();
            maskHeldKeys();
            glm::dvec2 mouse{},dragStart{},dragCentre{},cursor{};unsigned frame=0,arrivedFrame=0,manualArrivals=0;
            auto last=std::chrono::steady_clock::now();
            if(!captures.empty())std::filesystem::create_directories(captures);
            const auto syncMouseMode = [&]() {
                const bool focus=SDL_GetWindowFlags(window)&SDL_WINDOW_INPUT_FOCUS;
                const bool deck=interior.walking() && interiorMouse && !interiorHelp;
                const bool capture=focus && !strategy.open&&!combat.open&&!convoys.open && !map.open && !map.observingStar && !economy.view.open && !stories.open && !renderer.seatTransitionActive() &&
                                   (looking || cockpitLookActive || deck);
                if (SDL_GetWindowRelativeMouseMode(window)!=capture && SDL_SetWindowRelativeMouseMode(window,capture))
                    SDL_FlushEvent(SDL_EVENT_MOUSE_MOTION);
            };
            const auto setCockpitLookActive = [&](bool active) {
                if (cockpitLookActive == active) return;
                cockpitLookActive = active;
                cockpitLook = mouse = {};
                syncMouseMode();
            };
            const auto releaseMouse = [&]() {
                looking = cockpitLookActive = dragging = false;
                cockpitLook = mouse = {};
                syncMouseMode();
            };
            const auto useSeat = [&]() {
                if (renderer.seatTransitionActive() || !interior.useSeat())
                    return;
                renderer.beginSeatTransition(interior.walking());
                maskHeldKeys();
                interiorMouse=true;
                interiorHelp=false;
                contactsExpanded=false;
                renderer.closeCockpitHelp();
                releaseMouse();
            };
            const auto saveFlight=[&](){
                auto port=pendingPort?pendingPort:economy.dockedPlace()?economy.dockedPlace():pilot.instruments().docking.active?serviceDestination:std::nullopt;
                auto portText=port?catalog->fingerprint+" "+std::to_string(port->system)+" "+std::to_string(port->world)+" "+std::to_string(port->station)+" "+std::to_string(port->city):"";
                if(port&&(resumeManualDock||manualDocking(pilot.instruments().docking.phase)))portText+=" manual";
                if(radio.dockingTarget&&pilot.instruments().docking.active&&
                   pilot.instruments().docking.phase!=DockingPhase::Departure){
                    const auto& id=*radio.dockingTarget;
                    const auto phase=pilot.instruments().docking.phase;
                    const auto mode=phase==DockingPhase::Docked?"clamped":manualDocking(phase)?"manual":"auto";
                    portText="VESSEL "+id.home.str()+" "+std::to_string(id.slot)+" "+mode;
                }
                std::ostringstream state;
                state<<"XALARIA_SESSION 5 "<<catalog->fingerprint<<' '<<std::quoted(nav.serialize())<<' '<<std::quoted(pilot.serialize())<<' '
                    <<std::quoted(course.serialize())<<' '<<std::quoted(economy.serialize(unixNow()))<<' '<<std::quoted(portText);
                const auto contacts=economy.serializeTraffic(pilot.ship().flightSeconds);
                state<<' '<<std::quoted(contacts)<<' '<<std::quoted(stories.serialize())<<' '<<std::quoted(combat.serialize())<<' '<<std::quoted(convoys.serialize())<<' '<<std::quoted(strategy.serialize());
                write(save/"session.txt",state.str());
            };
            const auto aspect=[&](){return renderer.cockpitViewport().aspect();};
            const auto canvas=[&](double x,double y){
                int ww,wh;SDL_GetWindowSize(window,&ww,&wh);
                return renderer.cockpitViewport().toCanvas({x*pw/ww,y*ph/wh});
            };
            const auto closeEconomy=[&](){
                economy.view.open=false;
                economy.view.cargoTerminal=false;
                maskHeldKeys();
            };
            const auto toggleMap=[&](){
                strategy.open=false;combat.open=false;convoys.open=false;
                map.observingStar=false;closeEconomy();map.open=!map.open;releaseMouse();renderer.closeCockpitHelp();
                if(map.open&&map.mode==XalariaMapView::Mode::Galaxy&&!xalariaMapContains(xalariaMapPoint(map,map.ship,aspect()),aspect()))map.centre=map.ship;
            };
            const auto targetWormhole=[&](){
                convoys.following=false;
                if(course.targetWormhole()){
                    if(manualDocking(pilot.instruments().docking.phase)){
                        pilot.clearWaypoint();activity.clearSelection();
                        radio.dockingTarget.reset();
                    }
                    pendingPort.reset();serviceDestination.reset();
                    releaseMouse();renderer.closeCockpitHelp();
                }
            };
            const auto openEconomy=[&](){
                strategy.open=false;combat.open=false;convoys.open=false;
                map.observingStar=false;
                if(economy.view.open){closeEconomy();return;}
                maskHeldKeys();
                const auto system=map.open?map.selected:economy.currentSystem();
                const auto world=map.open?map.selectedWorld:economy.currentWorld();
                economy.show(system,world.value_or(catalog->star(system).worlds.begin()->first));
                if(economy.dockedPlace()&&economy.dockedPlace()->system==system&&economy.dockedPlace()->world==economy.view.place.world)economy.view.place=*economy.dockedPlace();
                map.searchFocused=false;releaseMouse();renderer.closeCockpitHelp();
            };
            const auto contactOnSensors=[&](const ActivityId& id){
                const auto contacts=renderer.activityContacts();
                return std::any_of(contacts.begin(),contacts.end(),[&](const auto& c){
                    return c.body.id==id&&!c.occluded&&c.distance<trafficRadioRange;
                });
            };
            const auto distressFor=[&](const ActivityId& id)->std::optional<StoryKey>{
                const auto key=stories.distressAt(economy.currentSystem(),activity.seconds);
                if(key&&stories.vessel(stories.generate(*key))==id)return key;
                return {};
            };
            std::optional<StoryKey> lastDistress;
            std::optional<std::pair<ActivityId,StoryKey>> heardLead;
            const auto radioRequest=[&](TrafficRequest request){
                radio.open=true;
                radio.recoveryNotice.reset();
                renderer.closeCockpitHelp();
                const auto contact=radio.selected&&contactOnSensors(*radio.selected)?
                    xalariaTrafficContact(economy,*radio.selected,map.wormhole,activity.seconds):std::nullopt;
                if(!contact){
                    radio.activity=radio.selected?"SIGNAL UNAVAILABLE":"NO CONTACT SELECTED";
                    radio.reply="Press K to select a crew in radio range.";
                    radio.shield={};
                    radio.freight.reset();
                    return;
                }
                strategy.discover(contact->port.system,"hailed crew and flight plan");
                radio.callsign=contact->body.name;
                radio.activity=contact->body.status;
                radio.shield=economy.trafficShield(contact->body,activity.seconds,
                    contact->phase==TrafficPhase::AtBerth||
                    (radio.dockingTarget==contact->body.id&&pilot.instruments().docking.active));
                if(isConvoyActor(contact->body.id))convoys.hear(contact->body.id,activity.seconds);
                const auto learned=!isConvoyActor(contact->body.id)&&combat.mayDock(contact->body)&&!combat.shieldsLowered(contact->body.id)&&
                    (request==TrafficRequest::Hail||request==TrafficRequest::Advisory||request==TrafficRequest::FreightWork)?
                    stories.hear(*contact,activity.seconds):std::nullopt;
                const auto tip=learned?" We have a lead for you: "+stories.generate(*learned).headline+". F7 opens your discoveries.":"";
                if(learned)heardLead=std::pair{contact->body.id,*learned};
                if(learned&&!smoke)saveFlight();
                if(request==TrafficRequest::PortMarket){
                    const auto& port=contact->port;
                    economy.show(port.system,port.world);
                    economy.view.place=port;
                    radio.open=false;
                    releaseMouse();maskHeldKeys();
                    return;
                }
                if(!isConvoyActor(contact->body.id)&&(request==TrafficRequest::FreightWork||request==TrafficRequest::FreightDispatch)){
                    const auto previous=radio.freight;
                    radio.freight=xalariaTrafficFreight(economy,contact->port);
                    radio.reply=xalariaFreightBrief(economy,contact->port,radio.freight)+tip;
                    if(request==TrafficRequest::FreightWork||!previous||!radio.freight||
                       previous->charter.id!=radio.freight->charter.id||previous->aboard!=radio.freight->aboard)
                        return;
                    const auto& lead=*radio.freight;
                    const auto& port=lead.nextPort();
                    economy.show(port.system,port.world);
                    economy.view.place=port;
                    economy.view.page=XEconomyView::Page::Contracts;
                    if(lead.aboard){
                        const auto& journal=economy.contracts();
                        const auto found=std::find_if(journal.rbegin(),journal.rend(),[&](const auto& c){
                            return c.id==lead.charter.id;
                        });
                        const auto row=size_t(found-journal.rbegin())+1;
                        if(row<=std::min<size_t>(10,journal.size()))economy.view.good=row;
                        economy.view.message=lead.atPort?"PARCEL READY TO DELIVER":"PARCEL ABOARD / D TRAVEL TO DELIVERY";
                    }else{
                        economy.view.message=lead.atPort?"COLLECT SEALED FREIGHT FROM THE LOADING BAY":
                                                            "SEALED FREIGHT / D TRAVEL TO THE PICKUP PORT";
                    }
                    radio.open=false;
                    releaseMouse();maskHeldKeys();
                    return;
                }
                const auto pose=universe.relative(nav.anchor,contact->body.id.home,0.);
                const auto eye=pose.position+pose.orientation*(nav.cameraPosition()*std::pow(10.,nav.logDistance));
                const auto range=contact->body.position-eye;
                const double length=glm::length(range);
                const double closing=length>.01?
                    glm::dot(pose.orientation*pilot.ship().velocity-contact->body.velocity,range/length):0.;
                radio.reply=xalariaTrafficReply(*catalog,economy,*contact,request,closing);
                if(request==TrafficRequest::Hail||request==TrafficRequest::Advisory||request==TrafficRequest::FlightPlan){
                    const auto dispatch=economy.trafficAdvisory(contact->port,activity.seconds);
                    if(!dispatch.empty())radio.reply+=" Local traffic advisory: "+dispatch+".";
                }
                if(request==TrafficRequest::Hail&&!isConvoyActor(contact->body.id)&&!combat.shieldsLowered(contact->body.id)&&combat.mayDock(contact->body)&&distressFor(contact->body.id)){
                    radio.activity="DISTRESS CHANNEL";
                    radio.reply="We have an emergency on the crew deck. F7 opens the report. Ask for docking clearance on this channel.";
                }
                radio.reply+=tip;
                if(contact->dockingHold>0.&&(request!=TrafficRequest::Dock||xalariaVesselBerth(contact->body)))
                    radio.recoveryNotice=contact->body.id;
                if(request==TrafficRequest::Dock){
                    convoys.following=false;
                    const auto point=xalariaVesselApproach(economy,contact->body.id,map.wormhole,activity.seconds);
                    if(!point)return;
                    const auto& helm=pilot.instruments();
                    if(radio.dockingTarget==contact->body.id&&helm.docking.active){
                        radio.reply=helm.docking.docked?"Clamps secure. Use the aft airlock to cross to our transfer bay.":
                                                        "Your approach is cleared. Follow the lights, or press J for automatic docking.";
                        return;
                    }
                    if(interior.walking()||helm.docking.active||helm.landed||helm.warp!=WarpPhase::Off||
                       helm.shrinking||helm.hull<=0.||helm.speed>1000.||length>20000.){
                        radio.reply="Bring the helm within 20 km in normal flight and slow below 1 km/s for clearance.";
                        return;
                    }
                    if(trafficHold){
                        pilot.releaseWaypointControl();
                        trafficHold.reset();
                    }
                    if(!pilot.dockManually(*point))return;
                    course.cancel(false);
                    pendingPort.reset();serviceDestination.reset();
                    radio.dockingTarget=contact->body.id;
                    map.message="DOCKING CLEARED WITH "+contact->body.name;
                    if(!economy.charterClaimed(xalariaVesselParcel(economy,*contact).id))
                        map.message+=" / COURIER PARCEL AVAILABLE";
                    radio.open=false;
                    releaseMouse();maskHeldKeys();
                    return;
                }
                if(request!=TrafficRequest::Rendezvous)return;
                const auto point=xalariaTrafficRendezvous(economy,*contact);
                if(!point)return;
                const auto& helm=pilot.instruments();
                if(interior.walking()||helm.docking.phase!=DockingPhase::None||helm.landed||
                   helm.warp!=WarpPhase::Off||helm.shrinking||helm.hull<=0.||helm.speed>1000.||
                   (helm.altitude>=0.&&helm.altitude<10000.)){
                    radio.reply="Rendezvous needs a free helm and normal flight clear of the surface. Slow below 1 km/s.";
                    return;
                }
                course.cancel();
                pendingPort.reset();serviceDestination.reset();
                pilot.navigateWaypoint(*point);
                trafficHold=contact->body.id;
                trafficHoldLabel=point->label;
                trafficHoldCallsign=contact->body.name;
                holdProgress={};
                map.message=point->label+" / B TAKE HELM";
            };
            const auto cycleTraffic=[&](bool backwards){
                const auto previous=radio.selected;
                std::vector<ActivityId> choices;
                for(const auto& contact:renderer.activityContacts())
                    if(isXalariaTraffic(contact.body.id)&&!contact.occluded)
                        choices.push_back(contact.body.id);
                if(choices.empty()){
                    radio.selected.reset();radio.callsign.clear();
                }else {
                    std::sort(choices.begin(),choices.end());
                    const auto here=radio.selected?std::lower_bound(choices.begin(),choices.end(),*radio.selected)
                                                   :choices.end();
                    const bool found=here!=choices.end()&&*here==*radio.selected;
                    const auto index=found?size_t(here-choices.begin()):choices.size();
                    radio.selected=choices[backwards?(found?(index+choices.size()-1)%choices.size():choices.size()-1)
                                                      :(found?(index+1)%choices.size():0)];
                }
                if(radio.selected!=previous)radio.freight.reset();
                radioRequest(TrafficRequest::Hail);
            };
            const auto plotPort=[&](const XPlace& p){
                course.search("");course.selectSystem(p.system);course.selectWorld(p.world);
                map.mode=XalariaMapView::Mode::System;map.open=true;closeEconomy();
            };
            const auto departureClear=[&](){
                if(interior.departureClear())return true;
                map.message=economy.view.message="DEPARTURE INTERLOCK / RETURN TO THE SHIP AND SEAL THE AIRLOCK";
                return false;
            };
            const auto flyCourse=[&](){
                convoys.following=false;
                if(!departureClear())return;
                const auto phase=pilot.instruments().docking.phase;
                if(radio.dockingTarget&&phase!=DockingPhase::Docked&&phase!=DockingPhase::Departure){
                    radio.dockingTarget.reset();
                    pilot.releaseWaypointControl();
                }
                pendingPort.reset();serviceDestination.reset();course.fly();
            };
            const auto toggleCoursePause=[&](){
                if(map.paused){if(departureClear())course.resume();}
                else course.pause();
            };
            const auto travelMarket=[&](){
                convoys.following=false;
                if(!departureClear())return;
                const auto p=economy.view.place;
                if(economy.canTrade(p)){pendingPort.reset();pilot.launch();economy.view.open=false;map.open=false;return;}
                const auto list=economy.venues(p.system,p.world);
                if(std::find(list.begin(),list.end(),p)==list.end()){economy.view.message="THIS WORLD HAS NO MARKET / CHOOSE ANOTHER WORLD";return;}
                const bool departing=economy.landed()||pilot.instruments().docking.docked;
                pendingPort=p;serviceDestination=p;
                if(departing)pilot.launch();
                else {
                    if(radio.dockingTarget){
                        radio.dockingTarget.reset();
                        pilot.releaseWaypointControl();
                    }
                    course.cancel();
                }
                if(economy.currentSystem()==p.system&&economy.currentWorld()==p.world&&economy.nearOrbit()){
                    if(!departing){
                        if(p.city){if(pilot.land())pendingPort.reset();}
                        else if(economy.travelToPort(p,nav,pilot)){activity.select(economy.portBody(p).id);pendingPort.reset();}
                    }
                }else{
                    
                    course.search("");course.selectSystem(p.system);course.selectWorld(p.world);course.fly();
                }
                economy.view.open=false;map.open=false;releaseMouse();
            };
            const auto startManualDock=[&](const XPlace& p){
                convoys.following=false;
                if(!economy.travelToPort(p,nav,pilot,true))return false;
                radio.dockingTarget.reset();
                course.cancel(false);
                map.message=economy.view.message;
                serviceDestination=p;
                pendingPort.reset();
                resumeManualDock=false;
                activity.select(economy.portBody(p).id);
                economy.view.open=false;map.open=false;
                maskHeldKeys();releaseMouse();
                return true;
            };
            if(resumeManualDock&&pendingPort){
                const auto p=*pendingPort;
                if(!startManualDock(p)){pendingPort.reset();resumeManualDock=false;}
            }
            if(pendingPort&&!pendingPort->city&&economy.nearOrbit()&&
               economy.currentSystem()==pendingPort->system&&economy.currentWorld()==pendingPort->world&&
               course.phase()!=XalariaNavigation::Phase::Transit){
                const auto p=*pendingPort;
                if(economy.travelToPort(p,nav,pilot)){
                    serviceDestination=p;pendingPort.reset();activity.select(economy.portBody(p).id);
                    economy.sync(nav,pilot);
                }
            }
            if(savedVessel&&catalog->address(map.current).ancestorOf(savedVessel->home)){
                if(const auto point=xalariaVesselApproach(economy,*savedVessel,map.wormhole,activity.seconds)){
                    if(savedVesselMode=="manual"){
                        if(pilot.dockManually(*point))radio.selected=radio.dockingTarget=*savedVessel;
                    }else if(savedVesselMode=="clamped"||savedVesselMode=="auto"){
                        pilot.navigateWaypoint(*point);
                        if(pilot.instruments().docking.active)radio.selected=radio.dockingTarget=*savedVessel;
                    }
                }
            }
            const auto clampedVessel=[&]()->std::optional<XalariaTrafficContact>{
                const auto* id=pilot.clampedHarbour();
                if(!id||!isXalariaTraffic(*id))return {};
                return xalariaTrafficContact(economy,*id,map.wormhole,activity.seconds);
            };
            const auto syncBoarding=[&](){
                const auto vessel=clampedVessel();
                interior.setStation(vessel?std::optional{xalariaVesselInterior(vessel->body)}:economy.dockedStationKey());
            };
            syncBoarding();
            const auto atStorySite=[&](){
                StoryContext context;
                context.system=economy.currentSystem();
                const auto& helm=pilot.instruments();
                const bool normal=helm.warp==WarpPhase::Off&&!helm.shrinking&&!helm.landed&&helm.hull>0.;
                if(normal&&economy.nearOrbit())context.orbit=economy.currentWorld();
                context.stellarOrbit=normal&&catalog->address(context.system).ancestorOf(nav.anchor);
                if(helm.docking.docked&&interior.aboardDockside()){
                    context.boardedStation=economy.dockedPlace();
                    if(const auto vessel=clampedVessel())context.boardedVessel=vessel->body.id;
                }
                return context;
            };
            const auto combatPlayer=[&](){
                const auto& helm=pilot.instruments();CombatPlayer player;
                player.frame=nav.anchor;player.position=nav.cameraPosition()*std::pow(10.,nav.logDistance);
                player.velocity=pilot.ship().velocity;player.orientation=nav.orientation;
                player.forward=nav.orientation*glm::dvec3(0,0,-1);player.seconds=pilot.ship().flightSeconds;
                player.sheltered=helm.docking.docked||helm.landed;
                player.transit=helm.warp!=WarpPhase::Off||pilot.ship().cruise||helm.shrinking||std::abs(pilot.ship().logSize)>.01||helm.hull<=0;
                player.weaponsInhibited=interior.walking()||renderer.seatTransitionActive()||helm.docking.active;
                if(pilot.clampedHarbour())player.clamped=*pilot.clampedHarbour();
                player.gate=map.wormhole;return player;
            };
            const auto closeStories=[&](){
                stories.open=false;
                maskHeldKeys();
                releaseMouse();
            };
            const auto openStories=[&](){
                strategy.open=false;combat.open=false;convoys.open=false;
                stories.evidenceOpen=false;
                const bool fromAtlas=map.open;
                const auto distress=!fromAtlas&&radio.open&&radio.selected?distressFor(*radio.selected):std::nullopt;
                stories.show(fromAtlas?map.selected:map.current,activity.seconds);
                stories.showPage(StoryPage::Nearby,activity.seconds);
                if(!fromAtlas&&stories.tracked()&&(!distress||distress==stories.tracked()))
                    stories.showPage(StoryPage::Journal,activity.seconds);
                if(distress&&distress!=stories.tracked()&&stories.known(*distress))
                    stories.openReport(*distress,activity.seconds);
                if(!fromAtlas&&radio.open&&heardLead&&radio.selected==heardLead->first)
                    stories.openReport(heardLead->second,activity.seconds);
                map.observingStar=false;
                closeEconomy();
                radio.open=false;
                renderer.closeCockpitHelp();
                releaseMouse();
            };
            const auto scanInvestigation=[&](){
                if(!stories.tracked()||!stories.investigation(*stories.tracked()))return false;
                const auto plan=stories.investigationPlan(*stories.tracked());
                const auto eye=universe.pointRelative(nav.anchor,nav.cameraPosition()*
                    std::pow(10.,nav.logDistance-universe.describe(nav.anchor).frame.logRadius),plan.frame,0.);
                const auto pose=universe.relative(nav.anchor,plan.frame,0.);
                const bool found=stories.scanEvidence(atStorySite(),plan.frame,eye,
                    pose.orientation*pilot.ship().velocity,activity.seconds);
                if(found&&!interior.walking())nav.setRenderScale(std::min(nav.logDistance,3.));
                if(found&&!smoke)saveFlight();
                if(!found)map.message="INVESTIGATION / "+stories.investigationTask(*stories.tracked());
                return found;
            };
            const auto openCaseFiles=[&](){
                openStories();
                if(stories.tracked())stories.openReport(*stories.tracked(),activity.seconds);
                else stories.showPage(StoryPage::Archive,activity.seconds);
                stories.evidenceOpen=true;
            };
            const auto nearbyStory=[&](const XalariaStory& story,const StoryContext& context){
                return context.system==story.system&&story.world&&context.orbit==story.world;
            };
            const auto storyRouteLabel=[&](const XalariaStory& story,const StoryContext& context)->std::string_view {
                if(stories.result(story.key))return stories.continuation(story.key)?"Enter  Next chapter":"";
                if(!stories.actionable(story.key))return {};
                if(!story.key.chapter&&stories.investigation(story.key)&&!stories.evidenceComplete(story.key)){
                    if(stories.investigationWaypoint(story.key))return "Enter  Approach search area";
                    if((story.site==StorySite::Station&&context.boardedStation==story.port)||
                       (story.site==StorySite::Vessel&&context.boardedVessel==stories.vessel(story)))
                        return "Enter  Take witness statement";
                }
                if(stories.ready(story,context))return {};
                if(story.port&&nearbyStory(story,context)){
                    if(story.site==StorySite::Vessel) {
                        const auto vessel=stories.vessel(story);
                        if(vessel&&pilot.instruments().docking.docked&&radio.dockingTarget==vessel)
                            return "Enter  Boarding help";
                        return vessel&&contactOnSensors(*vessel)?"Enter  Hail freighter":"Enter  Find freighter";
                    }
                    return economy.dockedPlace()==story.port?"Enter  Boarding help":"Enter  Dock at station";
                }
                return "Enter  Plot in atlas";
            };
            const auto storyRoute=[&](){
                const auto* story=stories.selected();
                if(!story)return;
                if(const auto next=stories.continuation(story->key)){
                    stories.openReport(*next,activity.seconds);
                    return;
                }
                const auto context=atStorySite();
                if(!story->key.chapter&&stories.actionable(story->key)&&stories.investigation(story->key)&&
                   !stories.evidenceComplete(story->key)){
                    if(stories.tracked()!=story->key){stories.track(story->key);if(!smoke)saveFlight();}
                    const auto state=stories.investigation(story->key);
                    const bool witness=(story->site==StorySite::Station&&context.boardedStation==story->port)||
                        (story->site==StorySite::Vessel&&context.boardedVessel==stories.vessel(*story));
                    if(state->evidence==7&&witness){scanInvestigation();stories.evidenceOpen=true;return;}
                    if(const auto point=stories.investigationWaypoint(story->key);point&&context.system==story->system&&
                       (!story->world||context.orbit==story->world)){
                        if(interior.walking()||pilot.instruments().docking.active){
                            stories.message="Return to the helm and clear the berth to search.";return;
                        }
                        trafficHold.reset();pendingPort.reset();serviceDestination.reset();course.cancel();
                        if(nav.anchor!=point->reference)nav.select(point->reference);
                        pilot.navigateWaypoint(*point);map.message=point->label;
                        closeStories();map.open=false;return;
                    }
                }
                if(!stories.actionable(story->key)||stories.ready(*story,context))return;
                if(story->port&&nearbyStory(*story,context)){
                    if(story->site==StorySite::Station){
                        if(economy.dockedPlace()==story->port){
                            stories.message="F5 stand up. Use E at both airlock doors and walk into the station.";
                            return;
                        }
                        if(!departureClear()){stories.message=map.message;return;}
                        economy.view.place=*story->port;
                        closeStories();
                        travelMarket();
                        if(serviceDestination==story->port)
                            map.message="DISPATCH APPROACH / "+economy.placeName(*story->port);
                        return;
                    }
                    const auto id=stories.vessel(*story);
                    if(id&&pilot.instruments().docking.docked&&radio.dockingTarget==id){
                        stories.message="F5 stand up. Use E at both airlock doors and cross to the freighter.";
                        return;
                    }
                    if(id&&contactOnSensors(*id)){
                        closeStories();
                        map.open=false;
                        radio.selected=id;
                        radio.freight.reset();
                        radioRequest(TrafficRequest::Hail);
                        return;
                    }
                    if(interior.walking()||pilot.instruments().docking.active){
                        stories.message="Return to the helm and clear the current berth to approach.";
                        return;
                    }
                    const auto contact=id?xalariaTrafficContact(economy,*id,map.wormhole,activity.seconds):std::nullopt;
                    auto hold=contact?xalariaTrafficRendezvous(economy,*contact):std::nullopt;
                    if(!hold){stories.message="No safe approach available. Hail when the vessel comes into range.";return;}
                    hold->velocity={};
                    hold->label="DISTRESS CONTACT / "+contact->body.name;
                    trafficHold.reset();
                    pendingPort.reset();serviceDestination.reset();
                    course.cancel();
                    pilot.navigateWaypoint(*hold);
                    map.message="APPROACHING DISTRESS SIGNAL / REOPEN F7 TO HAIL";
                    closeStories();
                    map.open=false;
                    return;
                }
                course.search("");
                course.selectSystem(story->system);
                if(story->world)course.selectWorld(*story->world);
                map.mode=XalariaMapView::Mode::System;
                map.open=true;
                map.message=story->site==StorySite::Station&&map.wormhole&&map.wormhole->destination==story->system
                    ?"DISPATCH LEAD / TARGET WORMHOLE FOR STATION APPROACH"
                    :"DISPATCH LEAD / SELECT FLY ROUTE TO DEPART";
                closeStories();
            };
            const auto trackStory=[&](){
                if(const auto* story=stories.selected();story&&stories.track(story->key)&&!smoke)saveFlight();
            };
            const auto resolveStory=[&](unsigned choice){
                if(const auto* story=stories.selected()){
                    if(stories.resolve(story->key,atStorySite(),choice,economy)){
                        if(!smoke)saveFlight();
                    }else if(!stories.result(story->key)){
                        stories.message=!stories.actionable(story->key)?"The response window has closed. Read the outcome in News.":
                            stories.tracked()!=story->key?"Track this lead first":!stories.evidenceComplete(story->key)?
                            stories.investigationTask(story->key):"Reach the assignment site to file the report.";
                    }
                }
            };
            const auto closeCombat=[&](){strategy.open=false;combat.open=false;convoys.open=false;maskHeldKeys();releaseMouse();};
            const auto openCombat=[&](){
                closeStories();closeEconomy();map.open=false;radio.open=false;
                if(radio.selected)combat.target=radio.selected;
                combat.open=true;maskHeldKeys();releaseMouse();renderer.closeCockpitHelp();
            };
            const auto cycleCombatTarget=[&](){
                std::vector<const ActivityContact*> targets;
                for(const auto& contact:renderer.activityContacts())
                    if(isXalariaTraffic(contact.body.id)&&!contact.occluded&&!contact.body.disabled&&contact.distance<100000.)targets.push_back(&contact);
                std::stable_sort(targets.begin(),targets.end(),[](const auto* a,const auto* b){return std::tuple(!a->body.hostile,a->body.id)<std::tuple(!b->body.hostile,b->body.id);});
                if(targets.empty()){combat.target.reset();return;}
                const auto found=std::find_if(targets.begin(),targets.end(),[&](const auto* item){return combat.target==item->body.id;});
                const auto index=found==targets.end()?0:(size_t(found-targets.begin())+1)%targets.size();
                combat.target=targets[index]->body.id;radio.selected=combat.target;
            };
            const auto combatAction=[&](unsigned action){
                bool changed=false;
                if(action<3)changed=combat.refit(CombatFit(action));
                else if(action==3)combat.armed=!combat.armed;
                else if(action<7)combat.weapon=CombatWeapon(action-4);
                else if(action==7&&combat.target)changed=combat.demand(*combat.target,combatPlayer());
                else if(action==8&&combat.target)changed=combat.seize(*combat.target,atStorySite());
                else if(action==9)changed=combat.claimBounty();
                else if(action==10){
                    if(pilot.ship().hull<=0){
                        course.cancel();pendingPort.reset();serviceDestination.reset();radio.dockingTarget.reset();
                        changed=combat.recover(nav);
                        if(changed){interior=ShipInterior{};renderer.cutPlayerTrail();course.update(0.);serviceDestination=economy.view.place;}
                    }else changed=combat.service();
                }else if(action==11&&combat.target){
                    radio.selected=combat.target;closeCombat();radioRequest(TrafficRequest::Dock);
                }
                if(changed&&!smoke)saveFlight();
            };
            const auto pickCombat=[&](glm::dvec2 point){
                const XalariaCombatLayout layout(aspect());
                if(layout.close.contains(point)){closeCombat();return;}
                const std::array buttons{layout.civilian,layout.corsair,layout.gunship,layout.arm,layout.pulse,layout.ion,
                    layout.torpedo,layout.demand,layout.seize,layout.claim,layout.service,layout.board};
                for(unsigned i=0;i<buttons.size();++i)if(buttons[i].contains(point)){combatAction(i);break;}
            };
            const auto strategyAction=[&](SDL_Scancode key){
                uint32_t selected=0;if(!strategy.fleet().empty()){auto it=strategy.fleet().begin();std::advance(it,std::min(strategy.row,strategy.fleet().size()-1));selected=it->first;}
                if(key==SDL_SCANCODE_C){if(strategy.capture(atStorySite())){strategy.page=0;strategy.row=strategy.fleet().size()-1;}}
                else if(key==SDL_SCANCODE_H)strategy.hire(selected,atStorySite());
                else if(key==SDL_SCANCODE_B){if(strategy.purchase(ActivityKind::Freighter)){strategy.page=0;strategy.row=strategy.fleet().size()-1;}}
                else if(key==SDL_SCANCODE_W){if(strategy.purchase(ActivityKind::Patrol)){strategy.page=0;strategy.row=strategy.fleet().size()-1;}}
                else if(key==SDL_SCANCODE_N)strategy.scan(combatPlayer());
                else if(key==SDL_SCANCODE_I)strategy.informer();
                else if(key==SDL_SCANCODE_A)strategy.pledge();
                else if(key==SDL_SCANCODE_T)strategy.collectTaxes();
                else if((key==SDL_SCANCODE_S||key==SDL_SCANCODE_R)&&(combat.target||radio.selected))strategy.salvage(combat.target?*combat.target:*radio.selected,combatPlayer(),key==SDL_SCANCODE_R);
                else if(key==SDL_SCANCODE_0)strategy.order(selected,FleetOrder::Hold,map.selected);
                else if(key>=SDL_SCANCODE_1&&key<=SDL_SCANCODE_5)strategy.order(selected,FleetOrder(1+key-SDL_SCANCODE_1),map.selected);
                if(!smoke)saveFlight();
            };
            const auto closeConvoys=[&](){convoys.open=false;maskHeldKeys();releaseMouse();};
            const auto openConvoys=[&](){closeStories();closeEconomy();map.open=false;radio.open=false;combat.open=false;convoys.open=true;maskHeldKeys();releaseMouse();renderer.closeCockpitHelp();};
            const auto convoyAction=[&](unsigned action){
                const auto key=convoys.selected();if(!key)return;bool changed=false;
                if(action<2)changed=convoys.join(*key,action==0?ConvoyDuty::Escort:ConvoyDuty::Pirate,combatPlayer());
                else if(action==2){
                    if(!departureClear())return;
                    const auto pose=convoys.stage(*key,activity.seconds);
                    if(economy.dockedPlace())pilot.launch();
                    changed=convoys.follow(nav,pilot,course);if(changed)closeConvoys();
                    (void)pose;
                }else if(action==3){
                    const auto pose=convoys.stage(*key,activity.seconds);radio.selected=convoys.id(*key,0,pose.frame);combat.target=radio.selected;
                    closeConvoys();radioRequest(TrafficRequest::Hail);
                }else if(action==4){if(convoys.tracked()==key)changed=convoys.report();else convoys.message="Select your tracked escort to file its report.";}
                if(changed&&!smoke)saveFlight();
            };
            const auto pickConvoy=[&](glm::dvec2 point){
                const XalariaConvoyLayout layout(aspect());if(layout.close.contains(point)){closeConvoys();return;}
                const auto known=convoys.known();
                if(layout.list.contains(point)){const auto index=(convoys.row/6)*6+size_t((point.y-layout.list.y)/78.);if(index<known.size())convoys.row=index;return;}
                const std::array buttons{layout.escort,layout.pirate,layout.follow,layout.hail,layout.report};
                for(unsigned i=0;i<buttons.size();++i)if(buttons[i].contains(point)){convoyAction(i);return;}
            };
            const auto pickStory=[&](glm::dvec2 point){
                const XalariaStoryLayout layout(aspect());
                if(layout.close.contains(point))closeStories();
                else if(layout.nearby.contains(point))stories.showPage(StoryPage::Nearby,activity.seconds);
                else if(layout.news.contains(point))stories.showPage(StoryPage::News,activity.seconds);
                else if(layout.feed.contains(point))stories.showPage(StoryPage::Feed,activity.seconds);
                else if(layout.followUps.contains(point))stories.showPage(StoryPage::FollowUps,activity.seconds);
                else if(layout.journal.contains(point))stories.showPage(StoryPage::Journal,activity.seconds);
                else if(layout.archive.contains(point))stories.showPage(StoryPage::Archive,activity.seconds);
                else if(stories.sheetCount()>1&&layout.previous.contains(point))stories.changeSheet(-1,activity.seconds);
                else if(stories.sheetCount()>1&&layout.next.contains(point))stories.changeSheet(1,activity.seconds);
                else if(const auto row=layout.pickRow(point,stories.entries().size()))stories.select(*row);
                else if(layout.track.contains(point))trackStory();
                else if(layout.route.contains(point))storyRoute();
                else if(layout.primary.contains(point))resolveStory(0);
                else if(layout.secondary.contains(point))resolveStory(1);
            };
            const auto selectedAsset=[&]()->uint32_t{return economy.view.asset<economy.assets().size()?economy.assets()[economy.view.asset].id:0;};
            const auto selectedDeposit=[&]()->unsigned{
                size_t i=0;for(const auto& [id,d]:economy.deposits())if(d.system==economy.view.place.system&&d.world==economy.view.place.world){if(i++==economy.view.deposit)return d.index;}
                return UINT32_MAX;
            };
            const auto service=[&](){
                switch(economy.view.good){
                    case 0:economy.refuel();break;case 1:economy.buyTool();break;case 2:economy.buyTool(true);break;
                    case 3:economy.upgradeHold();break;case 4:economy.buyArtwork();break;case 5:economy.studyCulture();break;
                    case 6:economy.seedAsset(XAssetKind::Station,economy.view.buildLevel);break;
                    case 7:economy.seedAsset(XAssetKind::Vault);break;case 8:economy.seedAsset(XAssetKind::Farm);break;
                    case 9:economy.survey();break;case 10:economy.buyFillingTool();break;
                }
            };
            const auto economyAction=[&](int action){
                auto& v=economy.view;using Page=XEconomyView::Page;
                if(v.page==Page::Market){
                    if(action==0){
                        auto routes=economy.opportunities(XGood(v.good),v.quantity);
                        if(routes.empty())v.message="NO PROFITABLE TRADE WITHIN THREE WORMHOLES / TRY OTHER CARGO";
                        else{const auto best=routes.front();plotPort(best.destination);map.message="TRADE PREVIEW / "+std::string(xGoodInfo(XGood(v.good)).name)+" / ESTIMATED NET "+xMoney(best.profit)+" / "+std::to_string(best.hops)+" HOPS";}
                    }else if(action==1)economy.refuel();else economy.deliverContracts();
                }else if(v.page==Page::Services)service();
                else if(v.page==Page::Holdings){if(action==0)economy.feedAsset(selectedAsset());else if(action==1)economy.collect(selectedAsset());else economy.guard(selectedAsset());}
                else if(v.page==Page::Mining){if(action==0)economy.survey();else economy.mine(selectedDeposit(),action==2);}
                else{
                    if(action==0){
                        if(economy.acceptContract()&&v.cargoTerminal)v.good=1;
                    }else if(action==1)economy.deliverContracts();
                    else{
                        const auto& charters=economy.contracts();
                        const auto charter=v.good>0&&v.good<=charters.size()?charters[charters.size()-v.good]:economy.offer(v.place);
                        if(charter.units)plotPort(charter.destination);
                    }
                }
            };
            const auto selectEconomyRow=[&](int delta){
                auto& v=economy.view;using Page=XEconomyView::Page;
                if(v.page==Page::Holdings)v.asset=size_t(std::clamp(int64_t(v.asset)+delta,int64_t(0),int64_t(economy.assets().empty()?0:economy.assets().size()-1)));
                else if(v.page==Page::Mining){size_t count=0;for(const auto& [id,d]:economy.deposits())if(d.system==v.place.system&&d.world==v.place.world)++count;
                    v.deposit=size_t(std::clamp(int64_t(v.deposit)+delta,int64_t(0),int64_t(count?count-1:0)));}
                else v.good=size_t(std::clamp(int(v.good)+delta,0,v.page==Page::Market?11:v.page==Page::Services?10:int(std::min<size_t>(10,economy.contracts().size()))));
            };
            const auto economyPick=[&](glm::dvec2 point){
                auto& v=economy.view;using Page=XEconomyView::Page;
                const XEconomyLayout l(aspect(),v.cargoTerminal);
                for(size_t i=0;i<l.tabs.size();++i)if(l.tabs[i].w>0&&l.tabs[i].contains(point)){
                    v.page=Page(i);v.good=0;return;
                }
                if(v.cargoTerminal){
                    if(l.travel.contains(point)){closeEconomy();return;}
                }else{
                    if(l.manualDock.contains(point)&&!v.place.city&&!economy.canTrade(v.place)){startManualDock(v.place);return;}
                    if(l.travel.contains(point)){travelMarket();return;}
                    if(l.venue.contains(point)){economy.cycleVenue();return;}
                }
                if(l.rows.contains(point)){size_t row=std::min<size_t>(11,size_t((point.y-l.rows.y+7)/32));
                    if(v.page==Page::Holdings){if((v.asset/12)*12+row<economy.assets().size())v.asset=(v.asset/12)*12+row;}
                    else if(v.page==Page::Mining)v.deposit=(v.deposit/12)*12+row;
                    else if(v.page!=Page::Contracts||row<=std::min<size_t>(10,economy.contracts().size()))v.good=std::min(row,v.page==Page::Services?size_t(10):size_t(11));return;}
                if(l.minus.contains(point)){v.quantity=std::max<int64_t>(1,v.quantity-1);return;}
                if(l.plus.contains(point)){v.quantity=std::min<int64_t>(600,v.quantity+1);return;}
                if(l.buy.contains(point)||l.sell.contains(point)){
                    const bool right=l.sell.contains(point);
                    if(v.page==Page::Market){if(right)economy.sell(XGood(v.good),v.quantity);else economy.buy(XGood(v.good),v.quantity);}
                    else if(v.page==Page::Services)v.buildLevel=unsigned(std::clamp(int(v.buildLevel)+(right?1:-1),1,22));
                    else selectEconomyRow(right?1:-1);return;
                }
                if(l.action.contains(point))economyAction(0);else if(l.secondary.contains(point))economyAction(1);else if(l.tertiary.contains(point))economyAction(2);
            };
            const auto previewVisible=[&](){map.centre={double(map.selected%catalog->grid),double(map.selected/catalog->grid)};};
            const auto locateShip=[&](){
                if(map.mode==XalariaMapView::Mode::System){
                    course.search("");
                    course.selectSystem(map.current);
                    if(map.currentWorld)course.selectWorld(*map.currentWorld);
                }
                map.centre=map.ship;
                map.span=26;
            };
            const auto pick=[&](glm::dvec2 point,bool twice){
                const XalariaMapLayout l(aspect());
                const bool galaxy=map.mode==XalariaMapView::Mode::Galaxy;
                if(l.close.contains(point)){toggleMap();return;}
                if(l.dispatch.contains(point)){openStories();return;}
                if(l.economy.contains(point)){openEconomy();return;}
                if(l.galaxy.contains(point)){map.mode=XalariaMapView::Mode::Galaxy;return;}
                if(l.system.contains(point)){map.mode=XalariaMapView::Mode::System;return;}
                if((galaxy?l.locate:l.links).contains(point)){locateShip();return;}
                if(galaxy&&l.fitRoute.contains(point)){xalariaFitRoute(map,aspect());return;}
                if(galaxy&&l.fitGalaxy.contains(point)){map.centre={100,100};map.span=210;return;}
                if(galaxy&&l.links.contains(point)){map.showLinks=!map.showLinks;return;}
                if(l.fly.contains(point)){flyCourse();return;}
                if((map.flying||map.paused)&&l.pause.contains(point)){toggleCoursePause();return;}
                if((map.flying||map.paused)&&l.cancel.contains(point)){course.cancel();return;}
                if(map.wormhole&&map.wormhole->destination==map.selected&&l.wormhole.contains(point)){targetWormhole();return;}
                if(!map.query.empty()&&l.clearSearch.contains(point)){course.search("");querySelected=false;map.searchFocused=true;return;}
                if(l.search.contains(point)){map.searchFocused=true;return;}
                map.searchFocused=false;
                if(!map.query.empty()&&l.results.contains(point)){
                    const size_t first=(map.matchIndex/l.resultRows)*l.resultRows;
                    const size_t row=first+size_t((point.y-l.results.y)/l.resultStep);
                    if(row<map.matches.size()){map.matchIndex=row;course.nextMatch(0);previewVisible();}
                    return;
                }
                if(map.query.empty()&&galaxy&&l.nearby.contains(point)){
                    const auto& ids=catalog->star(map.current).wormholes;
                    const size_t row=size_t((point.y-l.nearby.y)/l.worldStep);
                    if(row<ids.size()){course.selectSystem(ids[row]);previewVisible();if(twice)map.mode=XalariaMapView::Mode::System;}
                    return;
                }
                if(map.query.empty()&&!galaxy&&l.worlds.contains(point)){
                    const size_t index=size_t((point.y-l.worlds.y)/l.worldStep)+map.worldScroll;
                    const auto& worlds=catalog->star(map.selected).worlds;if(index<worlds.size()){auto it=worlds.begin();std::advance(it,index);course.search("");course.selectWorld(it->first);map.mode=XalariaMapView::Mode::System;if(twice)flyCourse();}return;
                }
                if(galaxy){
                    if(const auto id=xalariaPickStar(map,point,aspect())){course.search("");course.selectSystem(*id);if(twice)map.mode=XalariaMapView::Mode::System;}
                }else if(const auto id=xalariaPickWorld(map,point,aspect())){course.search("");course.selectWorld(*id);if(twice)flyCourse();}
            };
            while(running&&(!frames||frame<frames)){
                const auto now=std::chrono::steady_clock::now();
                const double dt=smoke?(journeyProof?.1:1./60):std::clamp(std::chrono::duration<double>(now-last).count(),.001,.05);last=now;
                SDL_Event e;
                while(SDL_PollEvent(&e)){
                    // Capture runs use their scripted helm. Desktop typing or
                    // a held key must not cancel an automated flight proof.
                    if(smoke&&e.type!=SDL_EVENT_QUIT&&e.type!=SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED&&
                       e.type!=SDL_EVENT_WINDOW_FOCUS_LOST&&e.type!=SDL_EVENT_WINDOW_FOCUS_GAINED)continue;
                    if(e.type==SDL_EVENT_QUIT)running=false;
                    else if(e.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED){SDL_GetWindowSizeInPixels(window,&pw,&ph);renderer.resize(pw,ph);}
                    else if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST){maskHeldKeys();releaseMouse();}
                    else if(e.type==SDL_EVENT_WINDOW_FOCUS_GAINED)maskHeldKeys();
                    else if(e.type==SDL_EVENT_TEXT_INPUT&&map.open&&!economy.view.open&&!stories.open){
                        if(swallowSpace&&std::string_view(e.text.text)==" "){swallowSpace=false;continue;}swallowSpace=false;
                        map.searchFocused=true;course.search((querySelected?"":map.query)+std::string(e.text.text));querySelected=false;
                        if(!map.matches.empty())previewVisible();
                    }else if(e.type==SDL_EVENT_MOUSE_MOTION){
                        if(map.observingStar)continue;
                        cursor=canvas(e.motion.x,e.motion.y);
                        if (!map.open && !economy.view.open && !stories.open && !renderer.seatTransitionActive() &&
                            SDL_GetWindowRelativeMouseMode(window)) {
                            const glm::dvec2 delta(e.motion.xrel, e.motion.yrel);
                            if (interior.walking()) {
                                if (interiorMouse && !interiorHelp)
                                    interior.look(delta);
                            } else if (cockpitLookActive) {
                                cockpitLook += delta * .0028;
                                cockpitLook.x = std::remainder(cockpitLook.x, 2 * std::numbers::pi);
                                cockpitLook.y = std::clamp(cockpitLook.y, glm::radians(-78.), glm::radians(78.));
                            } else if (looking) {
                                mouse += delta;
                            }
                        }
                        if(dragging&&map.open&&map.mode==XalariaMapView::Mode::Galaxy){
                            if(glm::length(cursor-dragStart)>4)moved=true;
                            if(moved)map.centre=dragCentre-(cursor-dragStart)*(map.span/XalariaMapLayout(aspect()).chart.h);
                        }
                    }else if(e.type==SDL_EVENT_MOUSE_WHEEL){
                        if(map.observingStar)continue;
                        if(strategy.open){if(e.wheel.y!=0.)strategy.step(e.wheel.y>0?-1:1);}
                        else if(convoys.open){if(e.wheel.y!=0.)convoys.step(e.wheel.y>0?-1:1);}
                        else if(stories.open){if(e.wheel.y!=0.)stories.step(e.wheel.y>0?-1:1);}
                        else if(economy.view.open){selectEconomyRow(e.wheel.y>0?-1:1);}
                        else if(map.open){
                            const auto l=XalariaMapLayout(aspect());
                            if(map.mode==XalariaMapView::Mode::System&&map.query.empty()&&l.worlds.contains(cursor)){
                                const auto count=catalog->star(map.selected).worlds.size();const auto maximum=count>l.worldRows?count-l.worldRows:0;
                                map.worldScroll=unsigned(std::clamp(long(map.worldScroll)-long(std::round(e.wheel.y)),0L,long(maximum)));
                            }else if(map.mode==XalariaMapView::Mode::Galaxy&&l.chart.contains(cursor))xalariaZoomMap(map,e.wheel.y,cursor,aspect());
                        }else if(!interior.walking()&&!renderer.seatTransitionActive())pilot.adjustDrive(e.wheel.y);
                    }else if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN&&e.button.button==SDL_BUTTON_LEFT&&!looking&&!cockpitLookActive){
                        if(map.observingStar)continue;
                        cursor=canvas(e.button.x,e.button.y);
                        if(map.open&&!economy.view.open&&!stories.open){dragStart=cursor;dragCentre=map.centre;dragging=xalariaMapContains(cursor,aspect());moved=false;}
                    }else if(e.type==SDL_EVENT_MOUSE_BUTTON_UP&&e.button.button==SDL_BUTTON_LEFT&&!looking&&!cockpitLookActive){
                        if(map.observingStar)continue;
                        cursor=canvas(e.button.x,e.button.y);
                        if(strategy.open){
                            const XalariaStrategyLayout l(aspect());
                            if(l.close.contains(cursor)){strategy.open=false;maskHeldKeys();releaseMouse();}
                            for(unsigned i=0;i<3;++i)if(l.tabs[i].contains(cursor)){strategy.page=i;strategy.row=0;}
                            if(l.list.contains(cursor))strategy.row=(strategy.row/7)*7+size_t((cursor.y-l.list.y)/40.);
                            constexpr std::array acquire{SDL_SCANCODE_C,SDL_SCANCODE_H,SDL_SCANCODE_B,SDL_SCANCODE_W};
                            constexpr std::array orders{SDL_SCANCODE_0,SDL_SCANCODE_1,SDL_SCANCODE_2,SDL_SCANCODE_3,SDL_SCANCODE_4,SDL_SCANCODE_5};
                            constexpr std::array intel{SDL_SCANCODE_N,SDL_SCANCODE_I,SDL_SCANCODE_A,SDL_SCANCODE_S,SDL_SCANCODE_R,SDL_SCANCODE_T};
                            for(unsigned i=0;i<4;++i)if(l.acquire[i].contains(cursor))strategyAction(acquire[i]);
                            for(unsigned i=0;i<6;++i){if(strategy.page==0&&l.orders[i].contains(cursor))strategyAction(orders[i]);if(l.intel[i].contains(cursor))strategyAction(intel[i]);}
                        }
                        else if(convoys.open){pickConvoy(cursor);}
                        else if(combat.open){pickCombat(cursor);}
                        else if(stories.open){pickStory(cursor);}
                        else if(economy.view.open){economyPick(cursor);}
                        else if(map.open){if(!moved)pick(cursor,e.button.clicks>=2);dragging=false;moved=false;}
                        else if(renderer.seatTransitionActive())continue;
                        else if(radio.open&&XalariaRadioLayout(aspect()).panel.contains(cursor)){
                            const XalariaRadioLayout layout(aspect());
                            if(layout.close.contains(cursor))radio.open=false;
                            else if(radio.selected)
                                for(size_t i=0;i<layout.actions.size();++i)
                                    if(layout.actions[i].contains(cursor)&&
                                       (i!=size_t(TrafficRequest::FreightDispatch)||radio.freight)){
                                        radioRequest(TrafficRequest(i));break;
                                    }
                        }
                        else if(interior.walking()){
                            if(!interiorHelp){interiorMouse=true;syncMouseMode();}
                        }else{
                            const XalariaMapRect badge{900*aspect()-342,145,318,182};
                            if(badge.contains(cursor)){toggleMap();continue;}
                            if(XalariaMapRect{900*aspect()-342,342,318,68}.contains(cursor)){openEconomy();continue;}
                            int ww,wh;SDL_GetWindowSize(window,&ww,&wh);
                            const double x=e.button.x*pw/ww,y=e.button.y*ph/wh;
                            if(renderer.contactsToggleHit(x,y)){contactsExpanded=!contactsExpanded;continue;}
                            const auto target=renderer.pickActivity(x,y);
                            if(target&&isXalariaTraffic(*target)){
                                if(radio.selected!=target)radio.freight.reset();
                                radio.selected=*target;
                                radioRequest(TrafficRequest::Hail);
                                continue;
                            }
                            if(pilot.instruments().docking.docked||manualDocking(pilot.instruments().docking.phase))continue;
                            if(target)
                                if(const auto port=economy.orbitalPlace(*target)){
                                    openEconomy();economy.view.place=*port;continue;
                                }
                            if(auto picked=renderer.pick(x,y);picked&&picked->kind()==Kind::Planet){
                                course.cancel();if(*picked!=nav.anchor)pilot.selectPlanet(*picked);course.selectFromFlight(*picked);
                            }
                        }
                    }else if(e.type==SDL_EVENT_KEY_UP){
                        heldAcrossMode[e.key.scancode]=false;
                        if((e.key.scancode==SDL_SCANCODE_LSHIFT||e.key.scancode==SDL_SCANCODE_RSHIFT)&&!(e.key.mod&SDL_KMOD_SHIFT))
                            setCockpitLookActive(false);
                    }else if(e.type==SDL_EVENT_KEY_DOWN&&!e.key.repeat){
                        const auto key=e.key.scancode;const bool control=e.key.mod&(SDL_KMOD_CTRL|SDL_KMOD_GUI);
                        if (renderer.seatTransitionActive())
                            heldAcrossMode[key]=true;
                        else if (heldAcrossMode[key])
                            continue;
                        if(key==SDL_SCANCODE_F10){
                            const bool was=strategy.open;closeConvoys();closeCombat();closeStories();closeEconomy();map.open=false;radio.open=false;strategy.open=!was;maskHeldKeys();releaseMouse();
                        }else if(strategy.open){
                            if(key==SDL_SCANCODE_ESCAPE){strategy.open=false;maskHeldKeys();releaseMouse();}
                            else if(key==SDL_SCANCODE_TAB){strategy.page=(strategy.page+1)%3;strategy.row=0;}
                            else if(key==SDL_SCANCODE_UP)strategy.step(-1);
                            else if(key==SDL_SCANCODE_DOWN)strategy.step(1);
                            else if(key==SDL_SCANCODE_PAGEUP)strategy.step(-7);
                            else if(key==SDL_SCANCODE_PAGEDOWN)strategy.step(7);
                            else strategyAction(key);
                        }else if(key==SDL_SCANCODE_F9){
                            if(convoys.open)closeConvoys();else if(!renderer.seatTransitionActive())openConvoys();
                        }else if(convoys.open){
                            if(key==SDL_SCANCODE_ESCAPE)closeConvoys();
                            else if(key==SDL_SCANCODE_UP)convoys.step(-1);
                            else if(key==SDL_SCANCODE_DOWN)convoys.step(1);
                            else if(key==SDL_SCANCODE_E)convoyAction(0);
                            else if(key==SDL_SCANCODE_P)convoyAction(1);
                            else if(key==SDL_SCANCODE_F||key==SDL_SCANCODE_RETURN)convoyAction(2);
                            else if(key==SDL_SCANCODE_I)convoyAction(3);
                            else if(key==SDL_SCANCODE_R)convoyAction(4);
                            else if(key==SDL_SCANCODE_F8){closeConvoys();openCombat();}
                        }else if(key==SDL_SCANCODE_F8){
                            if(combat.open)closeCombat();else if(!renderer.seatTransitionActive())openCombat();
                        }else if(combat.open){
                            if(key==SDL_SCANCODE_ESCAPE)closeCombat();
                            else if(key==SDL_SCANCODE_M)combatAction(3);
                            else if(key>=SDL_SCANCODE_1&&key<=SDL_SCANCODE_3)combatAction(4+key-SDL_SCANCODE_1);
                            else if(key==SDL_SCANCODE_T)cycleCombatTarget();
                        }else if(key==SDL_SCANCODE_F7){
                            if(stories.open)closeStories();
                            else if(!renderer.seatTransitionActive())openStories();
                        }else if(stories.open){
                            if(key==SDL_SCANCODE_ESCAPE)closeStories();
                            else if(key==SDL_SCANCODE_UP)stories.step(-1);
                            else if(key==SDL_SCANCODE_DOWN)stories.step(1);
                            else if(key==SDL_SCANCODE_TAB)stories.nextPage(activity.seconds);
                            else if(key==SDL_SCANCODE_PAGEUP)stories.changeSheet(-1,activity.seconds);
                            else if(key==SDL_SCANCODE_PAGEDOWN)stories.changeSheet(1,activity.seconds);
                            else if(key==SDL_SCANCODE_T)trackStory();
                            else if(key==SDL_SCANCODE_B)stories.evidenceOpen=!stories.evidenceOpen;
                            else if(key==SDL_SCANCODE_N)scanInvestigation();
                            else if(key==SDL_SCANCODE_RETURN)storyRoute();
                            else if(key==SDL_SCANCODE_1)resolveStory(0);
                            else if(key==SDL_SCANCODE_2)resolveStory(1);
                            else if(key==SDL_SCANCODE_F2){map.open=true;closeStories();}
                        }else if(key==SDL_SCANCODE_F4)openEconomy();
                        else if(key==SDL_SCANCODE_F2)toggleMap();
                        else if(key==SDL_SCANCODE_F3){closeEconomy();map.mode=map.mode==XalariaMapView::Mode::Galaxy?XalariaMapView::Mode::System:XalariaMapView::Mode::Galaxy;if(!map.open)toggleMap();}
                        else if(key==SDL_SCANCODE_ESCAPE){
                            if(map.observingStar){map.observingStar=false;maskHeldKeys();}
                            else if(economy.view.open)closeEconomy();
                            else if(map.open){map.open=false;maskHeldKeys();}
                            else if(radio.open)radio.open=false;
                            else if(interior.walking()){
                                interiorMouse=false;interiorHelp=false;renderer.closeCockpitHelp();releaseMouse();
                            }else if(looking||cockpitLookActive)releaseMouse();
                            else running=false;
                        }
                        else if(key==SDL_SCANCODE_F6&&!map.open&&!economy.view.open&&!interior.walking()&&
                                !renderer.seatTransitionActive()&&
                                (map.observingStar||(!map.flying&&catalog->star(map.current).surveyStar&&
                                  catalog->address(map.current).ancestorOf(nav.anchor)))){
                            map.observingStar=!map.observingStar;
                            maskHeldKeys();releaseMouse();renderer.closeCockpitHelp();
                        }
                        else if(map.observingStar)continue;
                        else if(economy.view.open){
                            auto& v=economy.view;using Page=XEconomyView::Page;
                            if(key>=SDL_SCANCODE_1&&key<=SDL_SCANCODE_5){
                                const auto page=Page(key-SDL_SCANCODE_1);
                                if(!v.cargoTerminal||page==Page::Market||page==Page::Contracts){v.page=page;v.good=0;}
                            }
                            else if(key==SDL_SCANCODE_LEFT&&v.page==XEconomyView::Page::Holdings)v.good=(v.good+11)%12;
                            else if(key==SDL_SCANCODE_RIGHT&&v.page==XEconomyView::Page::Holdings)v.good=(v.good+1)%12;
                            else if(key==SDL_SCANCODE_UP)selectEconomyRow(-1);else if(key==SDL_SCANCODE_DOWN)selectEconomyRow(1);
                            else if(key==SDL_SCANCODE_LEFTBRACKET)v.quantity=std::max<int64_t>(1,v.quantity-1);
                            else if(key==SDL_SCANCODE_RIGHTBRACKET)v.quantity=std::min<int64_t>(600,v.quantity+1);
                            else if(key==SDL_SCANCODE_D&&!v.cargoTerminal)travelMarket();
                            else if(key==SDL_SCANCODE_M&&!v.cargoTerminal&&!v.place.city&&!economy.canTrade(v.place))startManualDock(v.place);
                            else if(key==SDL_SCANCODE_RETURN)economyAction(0);
                            else if(key==SDL_SCANCODE_B&&v.page==Page::Market)economy.buy(XGood(v.good),v.quantity);
                            else if(key==SDL_SCANCODE_S&&v.page==Page::Market)economy.sell(XGood(v.good),v.quantity);
                            else if(v.page==XEconomyView::Page::Holdings){
                                const auto id=selectedAsset();
                                if(key==SDL_SCANCODE_H)economy.buildHall(id,false);else if(key==SDL_SCANCODE_G)economy.buildHall(id,true);
                                else if(key==SDL_SCANCODE_O)economy.donateArtwork(id);else if(key==SDL_SCANCODE_T)economy.flag(id);
                                else if(key==SDL_SCANCODE_U)economy.buildFuelStation(id);
                                else if(key==SDL_SCANCODE_V)economy.store(id,XGood(v.good),v.quantity,false);
                                else if(key==SDL_SCANCODE_R)economy.store(id,XGood(v.good),v.quantity,true);
                            }else if(key==SDL_SCANCODE_R)economy.refuel();
                        }
                        else if(key==SDL_SCANCODE_SPACE&&(map.flying||map.paused)){swallowSpace=true;toggleCoursePause();}
                        else if(key==SDL_SCANCODE_TAB&&!map.open&&!interior.walking()&&
                                !renderer.seatTransitionActive()&&control)contactsExpanded=!contactsExpanded;
                        else if(map.open){
                            if(control&&key==SDL_SCANCODE_F){map.searchFocused=true;querySelected=true;}
                            else if(control&&key==SDL_SCANCODE_A){map.searchFocused=true;querySelected=true;}
                            else if(key==SDL_SCANCODE_BACKSPACE){auto q=map.query;if(querySelected||control)q.clear();else eraseCharacter(q);course.search(q);querySelected=false;if(!map.matches.empty())previewVisible();}
                            else if(key==SDL_SCANCODE_TAB){course.nextMatch((e.key.mod&SDL_KMOD_SHIFT)?-1:1);if(!map.matches.empty())previewVisible();}
                            else if(key==SDL_SCANCODE_HOME)locateShip();
                            else if(key==SDL_SCANCODE_LEFT)map.centre.x-=map.span*.12;
                            else if(key==SDL_SCANCODE_RIGHT)map.centre.x+=map.span*.12;
                            else if(key==SDL_SCANCODE_UP)map.centre.y-=map.span*.12;
                            else if(key==SDL_SCANCODE_DOWN)map.centre.y+=map.span*.12;
                            else if(key==SDL_SCANCODE_PAGEUP)map.worldScroll=map.worldScroll>5?map.worldScroll-5:0;
                            else if(key==SDL_SCANCODE_PAGEDOWN){const auto count=catalog->star(map.selected).worlds.size();const auto rows=XalariaMapLayout(aspect()).worldRows;map.worldScroll=std::min(unsigned(count>rows?count-rows:0),map.worldScroll+5);}
                            else if(key==SDL_SCANCODE_RETURN)flyCourse();
                        }else if(renderer.seatTransitionActive()){
                            continue;
                        }else if(key==SDL_SCANCODE_K){
                            cycleTraffic(e.key.mod&SDL_KMOD_SHIFT);
                        }else if(key==SDL_SCANCODE_I){
                            if(radio.selected)radioRequest(TrafficRequest::Hail);
                            else cycleTraffic(false);
                        }else if(radio.open&&radio.selected&&key>=SDL_SCANCODE_1&&key<=SDL_SCANCODE_7){
                            radioRequest(TrafficRequest(1+key-SDL_SCANCODE_1));
                        }else if(interior.walking()){
                            if(key==SDL_SCANCODE_B&&interior.cargoTerminalInReach())openCaseFiles();
                            else if(key==SDL_SCANCODE_N&&!interiorHelp)scanInvestigation();
                            else if(key==SDL_SCANCODE_E&&!interiorHelp){
                                if(interior.seatInReach())useSeat();
                                else if(interior.cargoTerminalInReach()&&economy.openCargoTerminal()){
                                    maskHeldKeys();releaseMouse();
                                }else{
                                    const auto action=interior.boardingInteraction();
                                    if(action==BoardingInteraction::Freight){
                                        const auto vessel=clampedVessel();
                                        const bool prize=vessel&&combat.shieldsLowered(vessel->body.id);
                                        const bool collected=prize?combat.seize(vessel->body.id,atStorySite()):vessel?economy.collectTrafficFreight(*vessel,pilot):
                                                                    economy.collectLoadingBayFreight();
                                        freightNotice=prize?combat.message:economy.view.message;
                                        freightNoticeTime=6.;
                                        if(collected)saveFlight();
                                    }else if(action!=BoardingInteraction::None&&pilot.instruments().docking.docked){
                                        interior.useAirlock(action);
                                    }
                                }
                            }
                            else if(key==SDL_SCANCODE_F1){radio.open=false;interiorHelp=!interiorHelp;renderer.toggleCockpitHelp();syncMouseMode();}
                        }else{
                            if(key==SDL_SCANCODE_F5){useSeat();continue;}
                            if(key==SDL_SCANCODE_M){combat.armed=!combat.armed;continue;}
                            if(key==SDL_SCANCODE_T){cycleCombatTarget();continue;}
                            if(!radio.open&&key>=SDL_SCANCODE_1&&key<=SDL_SCANCODE_3){combat.weapon=CombatWeapon(key-SDL_SCANCODE_1);continue;}
                            if(key==SDL_SCANCODE_N){
                                if(stories.tracked()&&stories.investigation(*stories.tracked())&&
                                   !stories.evidenceComplete(*stories.tracked()))scanInvestigation();
                                else if(stories.survey(atStorySite(),activity.seconds)){
                                    if(!smoke)saveFlight();
                                }else map.message="SURVEY / No new lead here. Hail crews or explore another world.";
                                continue;
                            }
                            if(key==SDL_SCANCODE_C){looking=!looking;mouse={};syncMouseMode();}
                            else if(key==SDL_SCANCODE_LSHIFT||key==SDL_SCANCODE_RSHIFT)setCockpitLookActive(true);
                            else if(key==SDL_SCANCODE_F1){radio.open=false;renderer.toggleCockpitHelp();}
                            else if(key==SDL_SCANCODE_H)targetWormhole();
                            else if(pilot.instruments().docking.docked){if(key==SDL_SCANCODE_RETURN&&departureClear())pilot.launch();}
                            else if(key==SDL_SCANCODE_B){
                                const bool manual=manualDocking(pilot.instruments().docking.phase);
                                if(trafficHold){
                                    pilot.releaseWaypointControl();
                                    trafficHold.reset();
                                }
                                pendingPort.reset();serviceDestination.reset();course.cancel();
                                if(manual||radio.dockingTarget){pilot.releaseWaypointControl();activity.clearSelection();radio.dockingTarget.reset();}
                            }
                            else if(key==SDL_SCANCODE_X){const bool wasCruise=pilot.ship().cruise;course.cancel();if(!wasCruise)pilot.toggleCruise();}
                            else if(key==SDL_SCANCODE_F){const bool wasAssist=pilot.ship().assist;course.cancel();if(wasAssist)pilot.toggleAssist();}
                            else if(key==SDL_SCANCODE_Y){course.cancel();pilot.selectNextPlanet();if(nav.selected)course.selectFromFlight(*nav.selected);}
                            else if(key==SDL_SCANCODE_J){
                                if(manualDocking(pilot.instruments().docking.phase)){
                                    if(pilot.dock())map.message="AUTOMATIC DOCKING / APPROACH ACTIVE";
                                }else if(map.selectedWorld)flyCourse();
                                else if(nav.selected&&nav.selected->kind()==Kind::Planet){course.selectFromFlight(*nav.selected);flyCourse();}
                            }
                            else if(key==SDL_SCANCODE_G)pilot.toggleGear();
                            else if(key==SDL_SCANCODE_L){course.cancel();pilot.land();}
                        }
                    }
                }
                if(interiorHelp&&(map.open||economy.view.open)){
                    interiorHelp=false;
                    renderer.closeCockpitHelp();
                }
                syncMouseMode();
                if(map.open&&!economy.view.open&&!stories.open&&!searching){SDL_StartTextInput(window);searching=true;}
                else if((!map.open||economy.view.open||stories.open)&&searching){SDL_StopTextInput(window);searching=false;}
                HelmInput input;bool firing=false;
                glm::dvec2 steps{};
                bool fastWalk=false;
                if(!smoke&&!strategy.open&&!combat.open&&!convoys.open&&!map.open&&!map.observingStar&&!economy.view.open&&!stories.open&&!renderer.seatTransitionActive()&&
                   (SDL_GetWindowFlags(window)&SDL_WINDOW_INPUT_FOCUS)){
                    const auto* state=SDL_GetKeyboardState(nullptr);
                    const auto pressed=[&](SDL_Scancode key){return state[key]&&!heldAcrossMode[key];};
                    firing=!interior.walking()&&!radio.open&&!cockpitLookActive&&pressed(SDL_SCANCODE_SPACE)&&!swallowSpace;
                    if(interior.walking()){
                        if(!interiorHelp){
                            steps={double(pressed(SDL_SCANCODE_D))-pressed(SDL_SCANCODE_A),
                                   double(pressed(SDL_SCANCODE_W))-pressed(SDL_SCANCODE_S)};
                            fastWalk=pressed(SDL_SCANCODE_LSHIFT)||pressed(SDL_SCANCODE_RSHIFT);
                            interior.look(glm::dvec2(double(pressed(SDL_SCANCODE_RIGHT))-pressed(SDL_SCANCODE_LEFT),
                                                     double(pressed(SDL_SCANCODE_DOWN))-pressed(SDL_SCANCODE_UP))*dt*615.);
                        }
                    }else{
                        input.throttle=double(pressed(SDL_SCANCODE_W))-pressed(SDL_SCANCODE_S);
                        input.strafe=double(pressed(SDL_SCANCODE_D))-pressed(SDL_SCANCODE_A);
                        input.lift=double(pressed(SDL_SCANCODE_E))-pressed(SDL_SCANCODE_Q);
                        input.yaw=double(pressed(SDL_SCANCODE_RIGHT))-pressed(SDL_SCANCODE_LEFT);
                        input.pitch=double(pressed(SDL_SCANCODE_DOWN))-pressed(SDL_SCANCODE_UP);
                        const bool bankLeft = pressed(SDL_SCANCODE_U) || pressed(SDL_SCANCODE_V);
                        const bool bankRight = pressed(SDL_SCANCODE_O) || pressed(SDL_SCANCODE_Z);
                        input.roll = double(bankRight) - bankLeft;
                        if (pressed(SDL_SCANCODE_R) && !bankLeft && !bankRight)
                            input.roll = course.levelRollInput();
                        input.boost=pressed(SDL_SCANCODE_LALT);input.precision=pressed(SDL_SCANCODE_LCTRL);
                    }
                }
                if (!interior.walking() && !renderer.seatTransitionActive() && !map.open &&
                    !economy.view.open && !stories.open && !strategy.open&&!combat.open&&!convoys.open && !cockpitLookActive)
                    input=resolveHelmInput(input,mouse,dt);
                mouse={};
                const bool manualHelm=input.throttle||input.strafe||input.lift||input.yaw||input.pitch||input.roll;
                if(map.flying&&manualHelm)course.pause();
                if(manualHelm||input.brake)convoys.following=false;
                if(convoys.following&&!pilot.instruments().docking.active&&pilot.instruments().warp==WarpPhase::Off){
                    if(const auto point=convoys.waypoint(combatPlayer())){
                        if(point->label=="CONVOY WORMHOLE CROSSING"&&convoys.tracked()){
                            const auto& route=convoys.plan(*convoys.tracked()).route;
                            const auto here=std::find(route.begin(),route.end(),economy.currentSystem());
                            if(here!=route.end()&&here+1!=route.end()&&(!map.wormhole||map.wormhole->destination!=*(here+1)))course.selectSystem(*(here+1));
                        }
                        if(!pilot.instruments().navigating)pilot.navigateWaypoint(*point);else pilot.trackWaypoint(*point);
                    }
                }
                if(trafficHold){
                    const auto& helm=pilot.instruments();
                    const bool matching=helm.navigating&&helm.destination==trafficHoldLabel&&!pendingPort&&
                                        helm.warp==WarpPhase::Off&&!helm.docking.active;
                    if(!matching||manualHelm||input.brake) {
                        if(helm.destination==trafficHoldLabel)pilot.releaseWaypointControl();
                        trafficHold.reset();
                        if(!map.flying&&(map.message.starts_with(trafficHoldLabel)||
                                        map.message.starts_with("COURSE MATCHED WITH ")))
                            map.message="RENDEZVOUS ENDED / MANUAL FLIGHT";
                    }else {
                        const auto contact=contactOnSensors(*trafficHold)?
                            xalariaTrafficContact(economy,*trafficHold,map.wormhole,activity.seconds):std::nullopt;
                        const auto point=contact?xalariaTrafficRendezvous(economy,*contact):std::nullopt;
                        if(point)pilot.trackWaypoint(*point);
                        else {
                            pilot.brake();pilot.releaseWaypointControl();trafficHold.reset();
                            map.message="RENDEZVOUS ABORTED / CONTACT LOST";
                        }
                    }
                }
                const auto combatFrame=combatPlayer();
                combat.advance(combatFrame,dt,renderer.submittedGeometry(nav.anchor).get());
                if(firing&&combat.fire(combatFrame,renderer.submittedGeometry(nav.anchor).get()))
                    nav.setRenderScale(std::min(nav.logDistance,3.5));
                if(pilot.ship().hull<=0){
                    input={};input.brake=true;
                    if(map.flying||pilot.instruments().navigating){course.cancel();pilot.releaseWaypointControl();radio.dockingTarget.reset();}
                    map.message="SHIP DISABLED / F8 EMERGENCY RECOVERY";
                }
                cache.beginFrame();flight.observeGalaxy(renderer.galaxySamples());
                if(radio.dockingTarget&&pilot.instruments().docking.active){
                    if(const auto point=xalariaVesselApproach(economy,*radio.dockingTarget,map.wormhole,activity.seconds))
                        pilot.trackWaypoint(*point);
                    else {
                        pilot.releaseWaypointControl();
                        radio.dockingTarget.reset();
                        map.message="DOCKING ENDED / CONTACT LOST";
                    }
                }
                const bool crossed=course.advanceFlight(input,dt,renderer.submittedGeometry(nav.anchor));
                activity.seconds=pilot.ship().flightSeconds;
                if(const auto& waveOff=course.trafficWaveOff())
                    if(const auto contact=xalariaTrafficContact(economy,*waveOff,map.wormhole,activity.seconds)){
                        radio.selected=radio.recoveryNotice=*waveOff;
                        radio.callsign=contact->body.name;
                        radio.activity=contact->body.status;
                        radio.shield=contact->shield;
                        radio.reply=xalariaTrafficReply(*catalog,economy,*contact,TrafficRequest::Hail,0.);
                        radio.freight.reset();
                        radio.open=true;
                        renderer.closeCockpitHelp();
                    }
                if(radio.recoveryNotice){
                    if(!radio.open||radio.selected!=radio.recoveryNotice)radio.recoveryNotice.reset();
                    else if(economy.trafficDockingHold(*radio.recoveryNotice,activity.seconds)<=0.){
                        const auto contact=xalariaTrafficContact(economy,*radio.recoveryNotice,map.wormhole,activity.seconds);
                        radio.reply=contact&&xalariaVesselBerth(contact->body)?
                            "We are steady and the collar checks out. Request docking clearance when ready.":
                            "We are steady and back on course.";
                        radio.recoveryNotice.reset();
                    }
                }
                if(radio.dockingTarget){
                    if(pilot.instruments().docking.docked){
                        if(const auto point=xalariaVesselApproach(economy,*radio.dockingTarget,map.wormhole,activity.seconds)){
                            pilot.trackWaypoint(*point);
                            pilot.alignMovingClamp();
                        }
                    }else if(!pilot.instruments().docking.active){
                        radio.dockingTarget.reset();
                        pilot.releaseWaypointControl();
                    }
                }
                if(trafficHold){
                    const auto& helm=pilot.instruments();
                    const bool wasMatched=holdProgress.matched();
                    holdProgress.update(helm.range,helm.relativeSpeed,dt);
                    if(wasMatched!=holdProgress.matched()){
                        if(holdProgress.matched())map.message="COURSE MATCHED WITH "+trafficHoldCallsign;
                        else map.message=trafficHoldLabel+" / B TAKE HELM";
                    }
                    radio.rendezvous=radio.selected==trafficHold?
                        std::string(holdProgress.matched()?"Course matched":"Matching course")+"  /  "+
                        flightQuantity(helm.range)+" to hold  /  "+flightQuantity(helm.relativeSpeed,true)+" relative":
                        std::string{};
                }else radio.rendezvous.clear();
                interior.step(steps,dt,fastWalk,{map.flying,economy.cargoUnits()>0,pilot.ship().hull<88.});
                if(crossed){renderer.cutPlayerTrail();++manualArrivals;pendingPort.reset();serviceDestination.reset();}
                economy.useManualFuel(dt,std::min(1.,std::abs(input.throttle)+std::abs(input.strafe)+std::abs(input.lift)),pilot.ship().cruise&&!map.flying);
                pilot.setSystemAuthority(economy.fuel()>.001?1.:.15,1.);
                economy.advance(dt);economy.sync(nav,pilot);pilot.setCargoMass(economy.cargoKg());
                if(economy.view.open&&economy.view.cargoTerminal&&!economy.canTrade(economy.view.place))
                    closeEconomy();
                if((crossed||course.trafficWaveOff())&&!smoke)saveFlight();
                syncBoarding();
                if(stories.observe(atStorySite(),activity.seconds,dt)&&!smoke)saveFlight();
                if(const auto notice=stories.takeDiscoveryNotice();!notice.empty())map.message=notice;
                freightNoticeTime=std::max(0.,freightNoticeTime-dt);
                if(pendingPort&&interior.departureClear()&&!pilot.instruments().docking.active){
                    if(economy.currentSystem()==pendingPort->system&&economy.currentWorld()==pendingPort->world&&economy.nearOrbit()&&course.phase()!=XalariaNavigation::Phase::Transit){
                        const auto p=*pendingPort;serviceDestination=p;course.cancel();
                        if(p.city){if(pilot.land())pendingPort.reset();}
                        else if(economy.travelToPort(p,nav,pilot)){activity.select(economy.portBody(p).id);pendingPort.reset();}
                    }else if(course.phase()==XalariaNavigation::Phase::Paused&&!pilot.ship().landed&&!pilot.instruments().docking.active&&economy.fuel()>=(pendingPort->system==economy.currentSystem()?1.:8.))course.resume();
                }
                if(economyProof){
                    if(frame==130){if(!economy.dockedPlace())throw std::runtime_error("Economy native docking failed");if(!economy.buy(XGood::Iron,4))throw std::runtime_error(economy.view.message);}
                    if(frame==145&&!captures.empty())renderer.screenshot((captures/"economy-market.ppm").string());
                    if(frame==160)economy.view.page=XEconomyView::Page::Services;
                    if(frame==185&&!captures.empty())renderer.screenshot((captures/"economy-services.ppm").string());
                    if(frame==200)economy.view.page=XEconomyView::Page::Holdings;
                    if(frame==225&&!captures.empty())renderer.screenshot((captures/"economy-holdings.ppm").string());
                    if(frame==240)economy.view.page=XEconomyView::Page::Contracts;
                    if(frame==265&&!captures.empty())renderer.screenshot((captures/"economy-charters.ppm").string());
                }
                if(atlasProof){
                    if(frame==20){course.fly();map.open=true;}
                    if(frame==50)course.pause();
                    if(frame==60&&!captures.empty())renderer.screenshot((captures/"galaxy.ppm").string());
                    if(frame==70){course.resume();map.mode=XalariaMapView::Mode::System;course.selectWorld(catalog->star(map.selected).worlds.begin()->first);}
                    if(frame==110&&!captures.empty())renderer.screenshot((captures/"system.ppm").string());
                    if(frame==125)map.open=false;
                    if(frame==160&&!captures.empty())renderer.screenshot((captures/"cockpit-map.ppm").string());
                }
                auto instruments=course.instruments();instruments.contactsExpanded=contactsExpanded;
                if(freightNoticeTime>0.&&interior.walking())instruments.stationStatus=freightNotice;
                renderer.setCockpitLook(cockpitLook);
                renderer.setXalariaShield(pilot.shield());
                stories.refresh(activity.seconds);
                if(const auto distress=stories.distressAt(economy.currentSystem(),activity.seconds);distress!=lastDistress){
                    lastDistress=distress;
                    renderer.setXalariaDistress(distress?stories.vessel(stories.generate(*distress)):std::nullopt);
                }
                {
                    const auto context=atStorySite();
                    const auto* story=stories.selected();
                    renderer.setXalariaStories(&stories,context,story?storyRouteLabel(*story,context):std::string_view{});
                }
                strategy.destinationLabel=catalog->star(map.selected).name;
                if(fleetProof){
                    map.open=false;economy.view.open=false;stories.open=combat.open=convoys.open=radio.open=false;
                    strategy.open=frame<210;strategy.page=frame<80?0:frame<145?1:2;
                    if(!captures.empty()&&(frame==60||frame==120||frame==180)){
                        const auto name=frame==60?"fleet-command.ppm":frame==120?"fleet-intelligence.ppm":"fleet-campaigns.ppm";
                        renderer.screenshot((captures/name).string());
                    }
                }
                renderer.setCockpit(&instruments);renderer.render(nav,cache,true,1/dt,0,map.message,looking);
                if(radio.open&&radio.selected){
                    const auto contacts=renderer.activityContacts();
                    const auto live=std::find_if(contacts.begin(),contacts.end(),[&](const auto& c){
                        return c.body.id==*radio.selected&&!c.occluded;
                    });
                    radio.activity=live==contacts.end()?"SIGNAL UNAVAILABLE":live->body.status;
                    radio.shield=live==contacts.end()?ShieldReadout{}:
                        economy.trafficShield(live->body,activity.seconds,radio.dockingTarget==live->body.id&&
                                                pilot.instruments().docking.active);
                }
                if(!cache.stats().error.empty())throw std::runtime_error(cache.stats().error);
                if(journeyProof&&frame%300==0)std::cout<<"JOURNEY seconds="<<pilot.ship().flightSeconds<<" range="<<map.range<<" system="<<map.current<<std::endl;
                if(journeyProof&&pilot.instruments().warpArrivalSequence>0){
                    if(!arrivedFrame){arrivedFrame=frame;if(!captures.empty())renderer.screenshot((captures/"arrival.ppm").string());}
                    if(frame>arrivedFrame+5)running=false;
                }
                if(!captures.empty()&&!atlasProof&&!economyProof&&(frame==90||frame+3==frames))renderer.screenshot((captures/(map.open?"atlas.ppm":"flight.ppm")).string());
                if(frame&&frame%600==0&&!smoke)saveFlight();++frame;
            }
            if(!smoke||checkpointProof)saveFlight();
            std::cout<<"XALARIA_NATIVE frames="<<frame<<" stars="<<catalog->stars.size()<<" worlds="<<catalog->worldCount<<" system="<<map.current<<" travel_m="<<pilot.ship().travelMetres<<" credits_cents="<<economy.credits()<<" cargo_units="<<economy.cargoUnits()<<" docked="<<bool(economy.dockedPlace())<<" arrivals="<<pilot.instruments().warpArrivalSequence<<" manual_arrivals="<<manualArrivals<<"\n";
            if(journeyProof&&pilot.instruments().warpArrivalSequence==0)throw std::runtime_error("Wormhole journey did not complete");
        }
        SDL_DestroyWindow(window);SDL_Quit();return 0;
    }catch(const std::exception& e){std::cerr<<"Infinity / Xalaria: "<<e.what()<<'\n';if(window)SDL_DestroyWindow(window);SDL_Quit();return 1;}
}
