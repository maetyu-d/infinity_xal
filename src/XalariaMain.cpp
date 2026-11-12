#include "infinity/XalariaNavigation.hpp"
#include "infinity/XalariaEconomy.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/Renderer.hpp"
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
        unsigned frames=0;bool explicitData=false,fresh=false,smoke=false,flightView=false,journeyProof=false,atlasProof=false,economyProof=false,checkpointProof=false,sunsetProof=false,reflections=true,spaceGlass=true,spaceProof=false,lensEffects=true;uint32_t start=14940;
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
            else if(a=="--help"){std::cout<<"Infinity / Xalaria 0.12.1\nF4 economy. F2 map/flight. F3 galaxy/system. Type to search; Tab cycles results. Click worlds; Enter flies. Space pauses/resumes a course.\nMap: drag pan, scroll zoom, Home locate ship, Page Up/Down world list.\nFlight: WASD and Q/E thrust, arrows turn, U/O or V/Z roll left/right, hold R to level, C mouse steering, hold Shift and move mouse to look around, left Option/Alt boost, B cancel/brake, X cruise, H target manual wormhole, J fly selected world, Y next world, L land, G gear, F1 help.\n--catalog FILE --data DIR --start SEED --fresh --frames N --capture-dir DIR --flight-view --journey-test --atlas-test --economy-test\n";return 0;}
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
        std::string navigationSave,shipSave,routeSave,economySave,portSave;
        if(!fresh){
            const auto checkpoint=read(save/"session.txt");
            if(!checkpoint.empty()){
                std::istringstream in(checkpoint);std::string tag,fingerprint,extra;int version;
                if(!(in>>tag>>version>>fingerprint>>std::quoted(navigationSave)>>std::quoted(shipSave)>>std::quoted(routeSave)>>std::quoted(economySave)>>std::quoted(portSave))||
                    tag!="XALARIA_SESSION"||version!=1||fingerprint!=catalog->fingerprint||(in>>extra))
                    throw std::runtime_error("The saved session could not be read; it has been preserved");
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
        economy.catchUp(unixNow());economy.sync(nav,pilot);
        XalariaNavigation course(universe,nav,pilot);course.setEconomy(&economy);auto& map=course.view;
        ActivityState activity;
        std::optional<XPlace> pendingPort,serviceDestination;
        if(!fresh){std::istringstream savedPort(portSave);std::string fingerprint;XPlace p;
            if(savedPort>>fingerprint>>p.system>>p.world>>p.station>>p.city;fingerprint==catalog->fingerprint&&catalog->stars.contains(p.system)&&catalog->star(p.system).worlds.contains(p.world)){
                const auto list=economy.venues(p.system,p.world);if(std::find(list.begin(),list.end(),p)!=list.end())pendingPort=p;
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
        if(!fresh&&!routeSave.empty()&&!course.restore(routeSave))throw std::runtime_error("The saved route could not be read; it has been preserved");map.open=!flightView&&!economyProof;
        if(journeyProof){course.selectSystem(catalog->star(map.current).wormholes.at(0));if(!course.fly())throw std::runtime_error("Test departure refused");}
        if(atlasProof){course.selectSystem(catalog->star(map.current).wormholes.at(0));xalariaFitRoute(map,1.6);}
        SDL_SetAppMetadata("Infinity / Xalaria","0.12.1","org.infinity.xalaria");
        if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS))throw std::runtime_error(SDL_GetError());
        window=SDL_CreateWindow("INFINITY / XALARIA",1440,900,SDL_WINDOW_RESIZABLE);
        if(!window)throw std::runtime_error(SDL_GetError());SDL_SetWindowMinimumSize(window,960,640);SDL_SyncWindow(window);
        {
            Renderer renderer(window,universe);renderer.setSceneReflections(reflections);renderer.setSpaceGlass(spaceGlass);renderer.setLensEffects(lensEffects);renderer.setXalariaMap(&map);renderer.setXalariaEconomy(&economy);renderer.setActivity(&activity);
            int pw,ph;SDL_GetWindowSizeInPixels(window,&pw,&ph);renderer.resize(pw,ph);
            bool running=true,looking=false,searching=false,contactsExpanded=false,dragging=false,moved=false,querySelected=false,swallowSpace=false;
            bool cockpitLookActive = false;
            glm::dvec2 cockpitLook{};
            glm::dvec2 mouse{},dragStart{},dragCentre{},cursor{};unsigned frame=0,arrivedFrame=0,manualArrivals=0;
            auto last=std::chrono::steady_clock::now();
            if(!captures.empty())std::filesystem::create_directories(captures);
            const auto syncMouseMode = [&]() { SDL_SetWindowRelativeMouseMode(window, looking || cockpitLookActive); };
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
            const auto saveFlight=[&](){
                auto port=pendingPort?pendingPort:economy.dockedPlace()?economy.dockedPlace():pilot.instruments().docking.active?serviceDestination:std::nullopt;
                const auto portText=port?catalog->fingerprint+" "+std::to_string(port->system)+" "+std::to_string(port->world)+" "+std::to_string(port->station)+" "+std::to_string(port->city):"";
                std::ostringstream state;
                state<<"XALARIA_SESSION 1 "<<catalog->fingerprint<<' '<<std::quoted(nav.serialize())<<' '<<std::quoted(pilot.serialize())<<' '
                    <<std::quoted(course.serialize())<<' '<<std::quoted(economy.serialize(unixNow()))<<' '<<std::quoted(portText);
                write(save/"session.txt",state.str());
            };
            const auto aspect=[&](){return renderer.cockpitViewport().aspect();};
            const auto canvas=[&](double x,double y){
                int ww,wh;SDL_GetWindowSize(window,&ww,&wh);
                return renderer.cockpitViewport().toCanvas({x*pw/ww,y*ph/wh});
            };
            const auto toggleMap=[&](){
                economy.view.open=false;map.open=!map.open;releaseMouse();renderer.closeCockpitHelp();
                if(map.open&&map.mode==XalariaMapView::Mode::Galaxy&&!xalariaMapContains(xalariaMapPoint(map,map.ship,aspect()),aspect()))map.centre=map.ship;
            };
            const auto targetWormhole=[&](){
                if(course.targetWormhole()){
                    pendingPort.reset();serviceDestination.reset();
                    releaseMouse();renderer.closeCockpitHelp();
                }
            };
            const auto openEconomy=[&](){
                if(economy.view.open){economy.view.open=false;return;}
                const auto system=map.open?map.selected:economy.currentSystem();
                const auto world=map.open?map.selectedWorld:economy.currentWorld();
                economy.show(system,world.value_or(catalog->star(system).worlds.begin()->first));
                if(economy.dockedPlace()&&economy.dockedPlace()->system==system&&economy.dockedPlace()->world==economy.view.place.world)economy.view.place=*economy.dockedPlace();
                map.searchFocused=false;releaseMouse();renderer.closeCockpitHelp();
            };
            const auto plotPort=[&](const XPlace& p){
                course.search("");course.selectSystem(p.system);course.selectWorld(p.world);
                map.mode=XalariaMapView::Mode::System;map.open=true;economy.view.open=false;
            };
            const auto travelMarket=[&](){
                const auto p=economy.view.place;
                if(economy.canTrade(p)){pendingPort.reset();pilot.launch();economy.view.open=false;map.open=false;return;}
                const auto list=economy.venues(p.system,p.world);
                if(std::find(list.begin(),list.end(),p)==list.end()){economy.view.message="THIS WORLD HAS NO MARKET / CHOOSE ANOTHER WORLD";return;}
                const bool departing=economy.landed()||economy.dockedPlace().has_value();
                pendingPort=p;serviceDestination=p;if(departing)pilot.launch();else course.cancel();
                if(economy.currentSystem()==p.system&&economy.currentWorld()==p.world&&economy.nearOrbit()){
                    if(p.city){if(pilot.land())pendingPort.reset();}
                    else if(economy.travelToPort(p,nav,pilot)){activity.select(economy.portBody(p).id);pendingPort.reset();}
                }else{
                    
                    course.search("");course.selectSystem(p.system);course.selectWorld(p.world);course.fly();
                }
                economy.view.open=false;map.open=false;releaseMouse();
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
                else{if(action==0)economy.acceptContract();else if(action==1)economy.deliverContracts();
                    else{auto c=economy.offer(v.place);if(v.good>0&&v.good<=economy.contracts().size())c=economy.contracts()[economy.contracts().size()-v.good];if(c.units)plotPort(c.destination);}}
            };
            const auto selectEconomyRow=[&](int delta){
                auto& v=economy.view;using Page=XEconomyView::Page;
                if(v.page==Page::Holdings)v.asset=size_t(std::clamp(int64_t(v.asset)+delta,int64_t(0),int64_t(economy.assets().empty()?0:economy.assets().size()-1)));
                else if(v.page==Page::Mining){size_t count=0;for(const auto& [id,d]:economy.deposits())if(d.system==v.place.system&&d.world==v.place.world)++count;
                    v.deposit=size_t(std::clamp(int64_t(v.deposit)+delta,int64_t(0),int64_t(count?count-1:0)));}
                else v.good=size_t(std::clamp(int(v.good)+delta,0,v.page==Page::Market?11:v.page==Page::Services?10:int(std::min<size_t>(10,economy.contracts().size()))));
            };
            const auto economyPick=[&](glm::dvec2 point){
                const XEconomyLayout l(aspect());auto& v=economy.view;using Page=XEconomyView::Page;
                for(size_t i=0;i<5;++i)if(l.tabs[i].contains(point)){v.page=Page(i);v.good=0;return;}
                if(l.travel.contains(point)){travelMarket();return;}
                if(l.venue.contains(point)){economy.cycleVenue();return;}
                if(l.rows.contains(point)){size_t row=std::min<size_t>(11,size_t((point.y-l.rows.y+7)/32));
                    if(v.page==Page::Holdings){if((v.asset/12)*12+row<economy.assets().size())v.asset=(v.asset/12)*12+row;}
                    else if(v.page==Page::Mining)v.deposit=(v.deposit/12)*12+row;
                    else v.good=std::min(row,v.page==Page::Services?size_t(10):size_t(11));return;}
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
            const auto pick=[&](glm::dvec2 point,bool twice){
                const XalariaMapLayout l(aspect());
                if(XalariaMapRect{900*aspect()-342,14,318,60}.contains(point)||l.economy.contains(point)){openEconomy();return;}
                if(l.galaxy.contains(point)){map.mode=XalariaMapView::Mode::Galaxy;return;}
                if(l.system.contains(point)){map.mode=XalariaMapView::Mode::System;return;}
                if(l.locate.contains(point)){course.search("");course.selectSystem(map.current);if(map.currentWorld)course.selectWorld(*map.currentWorld);map.centre=map.ship;map.span=26;return;}
                if(l.fitRoute.contains(point)){map.mode=XalariaMapView::Mode::Galaxy;xalariaFitRoute(map,aspect());return;}
                if(l.fitGalaxy.contains(point)){map.mode=XalariaMapView::Mode::Galaxy;map.centre={100,100};map.span=210;return;}
                if(l.fly.contains(point)){pendingPort.reset();serviceDestination.reset();course.fly();return;}
                if(l.pause.contains(point)){if(map.paused)course.resume();else course.pause();return;}
                if(l.cancel.contains(point)){course.cancel();return;}
                if(map.wormhole&&map.wormhole->destination==map.selected&&l.wormhole.contains(point)){targetWormhole();return;}
                if(l.search.contains(point)){map.searchFocused=true;return;}
                map.searchFocused=false;
                if(l.results.contains(point)){
                    const auto& ids=map.query.empty()?catalog->star(map.current).wormholes:map.matches;
                    const size_t first=map.query.empty()?0:(map.matchIndex/4)*4;
                    const size_t row=size_t((point.y-l.results.y)/24)+first;
                    if(row<ids.size()){if(!map.query.empty()){map.matchIndex=row;course.nextMatch(0);}else course.selectSystem(ids[row]);previewVisible();}
                    return;
                }
                if(point.x>=l.details.x&&point.x<l.details.x+l.details.w&&point.y>=l.worldY-5&&point.y<l.worldY+l.worldRows*l.worldStep-5){
                    const size_t index=size_t((point.y-(l.worldY-5))/l.worldStep)+map.worldScroll;
                    const auto& worlds=catalog->star(map.selected).worlds;if(index<worlds.size()){auto it=worlds.begin();std::advance(it,index);course.search("");course.selectWorld(it->first);map.mode=XalariaMapView::Mode::System;if(twice)course.fly();}return;
                }
                if(map.mode==XalariaMapView::Mode::Galaxy){
                    if(const auto id=xalariaPickStar(map,point,aspect())){course.search("");course.selectSystem(*id);if(twice)map.mode=XalariaMapView::Mode::System;}
                }else if(const auto id=xalariaPickWorld(map,point,aspect())){course.search("");course.selectWorld(*id);if(twice)course.fly();}
            };
            while(running&&(!frames||frame<frames)){
                const auto now=std::chrono::steady_clock::now();
                const double dt=smoke?(journeyProof?.1:1./60):std::clamp(std::chrono::duration<double>(now-last).count(),.001,.05);last=now;
                SDL_Event e;
                while(SDL_PollEvent(&e)){
                    // Capture runs use their scripted helm. Desktop typing or
                    // a held key must not cancel an automated flight proof.
                    if(smoke&&e.type!=SDL_EVENT_QUIT&&e.type!=SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED&&e.type!=SDL_EVENT_WINDOW_FOCUS_LOST)continue;
                    if(e.type==SDL_EVENT_QUIT)running=false;
                    else if(e.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED){SDL_GetWindowSizeInPixels(window,&pw,&ph);renderer.resize(pw,ph);}
                    else if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST)releaseMouse();
                    else if(e.type==SDL_EVENT_TEXT_INPUT&&map.open&&!economy.view.open){
                        if(swallowSpace&&std::string_view(e.text.text)==" "){swallowSpace=false;continue;}swallowSpace=false;
                        map.searchFocused=true;course.search((querySelected?"":map.query)+std::string(e.text.text));querySelected=false;
                        if(!map.matches.empty())previewVisible();
                    }else if(e.type==SDL_EVENT_MOUSE_MOTION){
                        cursor=canvas(e.motion.x,e.motion.y);
                        if (!map.open && !economy.view.open) {
                            const glm::dvec2 delta(e.motion.xrel, e.motion.yrel);
                            if (cockpitLookActive) {
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
                        if(economy.view.open){selectEconomyRow(e.wheel.y>0?-1:1);}
                        else if(map.open){
                            const auto l=XalariaMapLayout(aspect());
                            if(cursor.x>=l.details.x&&cursor.y>=l.worldY&&cursor.y<660){
                                const auto count=catalog->star(map.selected).worlds.size();const auto maximum=count>l.worldRows?count-l.worldRows:0;
                                map.worldScroll=unsigned(std::clamp(long(map.worldScroll)-long(std::round(e.wheel.y)),0L,long(maximum)));
                            }else if(map.mode==XalariaMapView::Mode::Galaxy)xalariaZoomMap(map,e.wheel.y,l.chart.contains(cursor)?cursor:glm::dvec2(l.chart.x+l.chart.w/2,l.chart.y+l.chart.h/2),aspect());
                        }else pilot.adjustDrive(e.wheel.y);
                    }else if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN&&e.button.button==SDL_BUTTON_LEFT&&!looking&&!cockpitLookActive){
                        cursor=canvas(e.button.x,e.button.y);
                        if(map.open&&!economy.view.open){dragStart=cursor;dragCentre=map.centre;dragging=xalariaMapContains(cursor,aspect());moved=false;}
                    }else if(e.type==SDL_EVENT_MOUSE_BUTTON_UP&&e.button.button==SDL_BUTTON_LEFT&&!looking&&!cockpitLookActive){
                        cursor=canvas(e.button.x,e.button.y);
                        if(economy.view.open){economyPick(cursor);}
                        else if(map.open){if(!moved)pick(cursor,e.button.clicks>=2);dragging=false;moved=false;}
                        else{
                            const XalariaMapRect badge{900*aspect()-342,145,318,182};
                            if(badge.contains(cursor)){toggleMap();continue;}
                            if(XalariaMapRect{900*aspect()-342,342,318,68}.contains(cursor)){openEconomy();continue;}
                            int ww,wh;SDL_GetWindowSize(window,&ww,&wh);
                            if(auto picked=renderer.pick(e.button.x*pw/ww,e.button.y*ph/wh);picked&&picked->kind()==Kind::Planet){
                                course.cancel();if(*picked!=nav.anchor)pilot.selectPlanet(*picked);course.selectFromFlight(*picked);
                            }
                        }
                    }else if(e.type==SDL_EVENT_KEY_UP){
                        if((e.key.scancode==SDL_SCANCODE_LSHIFT||e.key.scancode==SDL_SCANCODE_RSHIFT)&&!(e.key.mod&SDL_KMOD_SHIFT))
                            setCockpitLookActive(false);
                    }else if(e.type==SDL_EVENT_KEY_DOWN&&!e.key.repeat){
                        const auto key=e.key.scancode;const bool control=e.key.mod&(SDL_KMOD_CTRL|SDL_KMOD_GUI);
                        if(key==SDL_SCANCODE_F4)openEconomy();
                        else if(key==SDL_SCANCODE_F2)toggleMap();
                        else if(key==SDL_SCANCODE_F3){economy.view.open=false;map.mode=map.mode==XalariaMapView::Mode::Galaxy?XalariaMapView::Mode::System:XalariaMapView::Mode::Galaxy;if(!map.open)toggleMap();}
                        else if(key==SDL_SCANCODE_ESCAPE){if(economy.view.open)economy.view.open=false;else if(looking||cockpitLookActive)releaseMouse();else if(map.open)map.open=false;else running=false;}
                        else if(economy.view.open){
                            auto& v=economy.view;
                            if(key>=SDL_SCANCODE_1&&key<=SDL_SCANCODE_5){v.page=XEconomyView::Page(key-SDL_SCANCODE_1);v.good=0;}
                            else if(key==SDL_SCANCODE_LEFT&&v.page==XEconomyView::Page::Holdings)v.good=(v.good+11)%12;
                            else if(key==SDL_SCANCODE_RIGHT&&v.page==XEconomyView::Page::Holdings)v.good=(v.good+1)%12;
                            else if(key==SDL_SCANCODE_UP)selectEconomyRow(-1);else if(key==SDL_SCANCODE_DOWN)selectEconomyRow(1);
                            else if(key==SDL_SCANCODE_LEFTBRACKET)v.quantity=std::max<int64_t>(1,v.quantity-1);
                            else if(key==SDL_SCANCODE_RIGHTBRACKET)v.quantity=std::min<int64_t>(600,v.quantity+1);
                            else if(key==SDL_SCANCODE_D)travelMarket();
                            else if(key==SDL_SCANCODE_RETURN)economyAction(0);
                            else if(key==SDL_SCANCODE_B&&v.page==XEconomyView::Page::Market)economy.buy(XGood(v.good),v.quantity);
                            else if(key==SDL_SCANCODE_S&&v.page==XEconomyView::Page::Market)economy.sell(XGood(v.good),v.quantity);
                            else if(v.page==XEconomyView::Page::Holdings){
                                const auto id=selectedAsset();
                                if(key==SDL_SCANCODE_H)economy.buildHall(id,false);else if(key==SDL_SCANCODE_G)economy.buildHall(id,true);
                                else if(key==SDL_SCANCODE_O)economy.donateArtwork(id);else if(key==SDL_SCANCODE_T)economy.flag(id);
                                else if(key==SDL_SCANCODE_U)economy.buildFuelStation(id);
                                else if(key==SDL_SCANCODE_V)economy.store(id,XGood(v.good),v.quantity,false);
                                else if(key==SDL_SCANCODE_R)economy.store(id,XGood(v.good),v.quantity,true);
                            }else if(key==SDL_SCANCODE_R)economy.refuel();
                        }
                        else if(key==SDL_SCANCODE_SPACE&&(map.flying||map.paused)){swallowSpace=true;if(map.paused)course.resume();else course.pause();}
                        else if(key==SDL_SCANCODE_TAB&&!map.open&&control)contactsExpanded=!contactsExpanded;
                        else if(map.open){
                            if(control&&key==SDL_SCANCODE_F){map.searchFocused=true;querySelected=true;}
                            else if(control&&key==SDL_SCANCODE_A){map.searchFocused=true;querySelected=true;}
                            else if(key==SDL_SCANCODE_BACKSPACE){auto q=map.query;if(querySelected||control)q.clear();else eraseCharacter(q);course.search(q);querySelected=false;if(!map.matches.empty())previewVisible();}
                            else if(key==SDL_SCANCODE_TAB){course.nextMatch((e.key.mod&SDL_KMOD_SHIFT)?-1:1);if(!map.matches.empty())previewVisible();}
                            else if(key==SDL_SCANCODE_HOME){map.centre=map.ship;map.span=26;}
                            else if(key==SDL_SCANCODE_LEFT)map.centre.x-=map.span*.12;
                            else if(key==SDL_SCANCODE_RIGHT)map.centre.x+=map.span*.12;
                            else if(key==SDL_SCANCODE_UP)map.centre.y-=map.span*.12;
                            else if(key==SDL_SCANCODE_DOWN)map.centre.y+=map.span*.12;
                            else if(key==SDL_SCANCODE_PAGEUP)map.worldScroll=map.worldScroll>5?map.worldScroll-5:0;
                            else if(key==SDL_SCANCODE_PAGEDOWN){const auto count=catalog->star(map.selected).worlds.size();const auto rows=XalariaMapLayout(aspect()).worldRows;map.worldScroll=std::min(unsigned(count>rows?count-rows:0),map.worldScroll+5);}
                            else if(key==SDL_SCANCODE_RETURN){pendingPort.reset();serviceDestination.reset();course.fly();}
                        }else{
                            if(key==SDL_SCANCODE_C){looking=!looking;mouse={};syncMouseMode();}
                            else if(key==SDL_SCANCODE_LSHIFT||key==SDL_SCANCODE_RSHIFT)setCockpitLookActive(true);
                            else if(key==SDL_SCANCODE_F1)renderer.toggleCockpitHelp();
                            else if(key==SDL_SCANCODE_H)targetWormhole();
                            else if(key==SDL_SCANCODE_B){pendingPort.reset();serviceDestination.reset();course.cancel();}
                            else if(key==SDL_SCANCODE_X){const bool wasCruise=pilot.ship().cruise;course.cancel();if(!wasCruise)pilot.toggleCruise();}
                            else if(key==SDL_SCANCODE_F){const bool wasAssist=pilot.ship().assist;course.cancel();if(wasAssist)pilot.toggleAssist();}
                            else if(key==SDL_SCANCODE_Y){course.cancel();pilot.selectNextPlanet();if(nav.selected)course.selectFromFlight(*nav.selected);}
                            else if(key==SDL_SCANCODE_J){if(map.selectedWorld)course.fly();else if(nav.selected&&nav.selected->kind()==Kind::Planet){course.selectFromFlight(*nav.selected);course.fly();}}
                            else if(key==SDL_SCANCODE_G)pilot.toggleGear();
                            else if(key==SDL_SCANCODE_L){course.cancel();pilot.land();}
                        }
                    }
                }
                if(map.open&&!economy.view.open&&!searching){SDL_StartTextInput(window);searching=true;}else if((!map.open||economy.view.open)&&searching){SDL_StopTextInput(window);searching=false;}
                HelmInput input;
                if(!smoke&&!map.open&&!economy.view.open&&(SDL_GetWindowFlags(window)&SDL_WINDOW_INPUT_FOCUS)){
                    const auto* keys=SDL_GetKeyboardState(nullptr);
                    input.throttle=double(keys[SDL_SCANCODE_W])-keys[SDL_SCANCODE_S];input.strafe=double(keys[SDL_SCANCODE_D])-keys[SDL_SCANCODE_A];
                    input.lift=double(keys[SDL_SCANCODE_E])-keys[SDL_SCANCODE_Q];input.yaw=double(keys[SDL_SCANCODE_RIGHT])-keys[SDL_SCANCODE_LEFT];
                    input.pitch=double(keys[SDL_SCANCODE_DOWN])-keys[SDL_SCANCODE_UP];
                    const bool bankLeft = keys[SDL_SCANCODE_U] || keys[SDL_SCANCODE_V];
                    const bool bankRight = keys[SDL_SCANCODE_O] || keys[SDL_SCANCODE_Z];
                    input.roll = double(bankRight) - bankLeft;
                    if (keys[SDL_SCANCODE_R] && !bankLeft && !bankRight)
                        input.roll = course.levelRollInput();
                    input.boost=keys[SDL_SCANCODE_LALT];input.precision=keys[SDL_SCANCODE_LCTRL];
                    if(map.flying&&(input.throttle||input.strafe||input.lift||input.yaw||input.pitch||input.roll))course.pause();
                }
                cache.beginFrame();flight.observeGalaxy(renderer.galaxySamples());
                const bool crossed=course.advanceFlight(resolveHelmInput(input,mouse,dt),dt,renderer.submittedGeometry(nav.anchor));mouse={};
                if(crossed){++manualArrivals;pendingPort.reset();serviceDestination.reset();}
                economy.useManualFuel(dt,std::min(1.,std::abs(input.throttle)+std::abs(input.strafe)+std::abs(input.lift)),pilot.ship().cruise&&!map.flying);
                pilot.setSystemAuthority(economy.fuel()>.001?1.:.15,1.);
                economy.advance(dt);economy.sync(nav,pilot);pilot.setCargoMass(economy.cargoKg());activity.seconds=pilot.ship().flightSeconds;
                if(crossed&&!smoke)saveFlight();
                if(pendingPort){
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
                renderer.setCockpitLook(cockpitLook);
                renderer.setCockpit(&instruments);renderer.render(nav,cache,true,1/dt,0,map.message,looking);
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
