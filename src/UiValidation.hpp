#pragma once
#include "WindowSizing.hpp"
#include "DiscoveryScenario.hpp"
#include "SignalGameplayScenario.hpp"
#include "TargetingScenario.hpp"
#include "ThreatScenario.hpp"
#include "ShipSignalScenario.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/CrewMinimap.hpp"
#include "infinity/Economy.hpp"
#include "infinity/Renderer.hpp"
#include "infinity/NpcCombat.hpp"
#include <filesystem>
#include <stdexcept>

namespace infinity {
class UiValidation {
  public:
    UiValidation(SDL_Window* window, Navigation& nav, Pilot& pilot)
        : window_(window), nav_(nav), pilot_(pilot) {
        SDL_Rect usable;
        const auto b = windowBorders(window);
        int x, y, w, h;
        if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &usable) ||
            !SDL_GetWindowPosition(window, &x, &y) || !SDL_GetWindowSize(window, &w, &h))
            throw std::runtime_error("Unable to inspect native window work area");
        if (x - b.left < usable.x || y - b.top < usable.y || x + w + b.right > usable.x + usable.w ||
            y + h + b.bottom > usable.y + usable.h)
            throw std::runtime_error("Startup window decorations exceed the display work area");
        std::cout << "UI_STARTUP window_and_decorations=inside_work_area\n";
        pilot_.startInSystem();
        camera_ = nav_.serialize();
        // Exact framebuffer sizes below are fixture requests, not interactive
        // minimum sizes. Production startup fitting above remains exercised.
        SDL_SetWindowMinimumSize(window_, 1, 1);
        ShipCrew shipCrew(584);
        crew_ = shipCrew.view(0, CrewStation::Bridge);
        crew_.members.resize(maxShipCrew);
        for (size_t i=0;i<crew_.members.size();++i) {
            auto& member=crew_.members[i];member.id=800+i;
            member.name = "LIEUTENANT LONG SURNAME";
            member.room=member.walkTo=member.target=CrewStation(i%crewRoomCount);
        }
        crew_.tasks.resize(maxShipCrew);
        for (auto& task : crew_.tasks) {
            task.status = "REPAIRING";
            task.detail = "MAINTAINING LIFE SUPPORT";
        }
        crew_.summary = "CREW EMERGENCY / ENGINE DAMAGE / RELIEF HELM MOVING";
        crew_.alert = "ENGINE ROOM FIRE / REPAIRS IN PROGRESS";
        crew_.message = "DAMAGE CONTROL ORDER CONFIRMED";
        market_.connected = market_.ready = true;
        market_.page = MarketPage::Jobs;
        market_.site = "SURFACE EQUIPMENT PORT";
        market_.role = "COLONY JOB BOARD";
        market_.selected = 14;
        market_.summary = "CARGO FULL / CREW READY";
        market_.message = "TRANSACTION COMPLETE / EQUIPMENT READY";
        market_.plan = "DELIVER TO ORBITAL HARBOUR";
        market_.planTiming = "ETA 1:20 / DEADLINE 5:00";
        for (unsigned i = 0; i < 80; ++i)
            market_.detail.push_back("JOB ROW " + std::to_string(i) + " / FUNDED COLONY DELIVERY / LONG DESCRIPTION");
    }
    void advance(Renderer& renderer) {
        if (placed_ || finished())
            return;
        const auto& c = cases_[stage_];
        if(c.fixture==16){
            Universe u;auto page=generateDiscoveryPage(u,Address{}.child(Kind::Galaxy,0).child(Kind::System,9));
            const SignalSource* chosen=nullptr;double best=-2;
            for(const auto& source:page->sources)if(source.relic){
                const auto sun=glm::normalize(u.relative(source.id.planet.parent(),source.id.planet,0).position);
                const auto light=glm::dot(sun,glm::normalize(source.relic->metres));
                if(light>best){best=light;chosen=&source;}
            }
            if(!chosen)throw std::runtime_error("Ruin surface fixture has no generated site");
            auto scene=std::make_shared<DiscoveryPage>();scene->system=page->system;scene->sources.push_back(*chosen);ruinPage_=scene;
            const auto& site=*chosen->relic;const auto axes=tangentFrame(glm::normalize(site.metres));
            const auto eye=site.metres+axes*glm::dvec3(130,140,160);
            nav_.anchor=chosen->id.planet;nav_.selected.reset();nav_.logDistance=nav_.targetLogDistance=2.6;
            nav_.orientation=glm::quatLookAtRH(glm::normalize(site.metres-eye),glm::normalize(site.metres));
            nav_.offset=eye/std::pow(10.,nav_.logDistance)-nav_.orientation*glm::dvec3(0,0,1);nav_.transitioning=false;
            if(!nav_.restore(nav_.serialize()))throw std::runtime_error("Ruin fixture camera setup failed");
            camera_=nav_.serialize();pilot_.synchronizeFrame();
        }
        const double density = std::max(1.f, SDL_GetWindowPixelDensity(window_));
        SDL_SetWindowSize(window_, int(std::lround(c.w / density)), int(std::lround(c.h / density)));
        SDL_SyncWindow(window_);
        int w, h;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        if (w != c.w || h != c.h)
            throw std::runtime_error("UI fixture did not receive its requested framebuffer size");
        renderer.resize(w, h);
        renderer.setCockpitValidation(true);
        renderer.setMode(c.mode);
        if (help_ != c.help) {
            renderer.toggleCockpitHelp();
            help_ = c.help;
        }
        placed_ = true;
        stable_ = 0;
    }
    void apply(FlightInstruments& t, Renderer& renderer) {
        t.contactsExpanded=true;
        const auto fixture = cases_[stage_].fixture;
        if (!fixture)
            return;
        if(fixture==16){
            auto view=std::make_shared<DiscoveryView>();view->page=ruinPage_;t.discovery=view;t.contactsExpanded=false;
            t.phase="RELIC APPROACH";t.altitude=140;t.weather="CLEAR";t.weatherSeverity=0;
            crew_.open=crew_.flightOpen=market_.open=false;crew_.rooms.fill(CrewRoom{});crew_.alert.clear();
            renderer.setCrew(&crew_);renderer.setEconomy(nullptr,&market_);return;
        }
        if(fixture>=27){
            if(!shipSignalViews_)shipSignalViews_=shipSignalProofViews(Universe{});
            auto view=std::make_shared<DiscoveryView>((*shipSignalViews_)[fixture==27?0:fixture==28?1:2]);
            view->open=fixture!=30;view->monitoring=fixture==30;
            t.discovery=std::move(view);t.vessel.reset();t.recon.reset();t.targeting.reset();t.awareness.reset();t.contactsExpanded=false;
            if(fixture==30){if(!threatViews_)threatViews_=threatProofViews(Universe{});t.awareness=std::make_shared<CombatAwarenessView>((*threatViews_)[0]);}
            crew_.open=crew_.flightOpen=market_.open=false;renderer.setCrew(&crew_);renderer.setEconomy(nullptr,&market_);return;
        }
        if(fixture>=23){
            if(!threatViews_)threatViews_=threatProofViews(Universe{});
            const unsigned index=fixture==23?0:fixture==24?1:fixture==25?2:3;
            t.awareness=std::make_shared<CombatAwarenessView>((*threatViews_)[index]);
            t.discovery.reset();t.vessel.reset();t.recon.reset();t.targeting.reset();t.contactsExpanded=false;
            t.hostileContacts=1;t.weapon="PULSE LASER I";t.weaponStatus="READY";t.explorationActive=false;
            if(fixture==25){if(!targetingViews_)targetingViews_=targetingProofViews(Universe{});t.targeting=std::make_shared<TargetingView>((*targetingViews_)[1]);}
            t.localMissile=fixture==25?glm::normalize(glm::dvec3(.2,0,-1)):glm::dvec3(0);
            t.incomingFire=fixture==24?1:0;t.weaponHeat=0;t.targetVisible=false;t.navigating=false;
            crew_.open=crew_.flightOpen=market_.open=false;crew_.rooms.fill(CrewRoom{});crew_.alert.clear();
            renderer.setCrew(&crew_);renderer.setEconomy(nullptr,&market_);return;
        }
        if(fixture>=19){
            if(!targetingViews_)targetingViews_=targetingProofViews(Universe{});
            const unsigned index=fixture==19?0:fixture==20?1:fixture==21?2:3;
            t.targeting=std::make_shared<TargetingView>((*targetingViews_)[index]);
            t.discovery.reset();t.vessel.reset();t.recon.reset();t.contactsExpanded=false;
            t.phase="COMBAT";t.hostileContacts=2;t.weapon="GUIDED MISSILE";t.weaponStatus="READY";
            t.weaponHeat=0;t.pulseHeat.reset();t.explorationActive=false;t.targetVisible=false;t.navigating=false;t.systemTarget.clear();
            t.combatMission.clear();t.incomingFire=0;crew_.open=crew_.flightOpen=market_.open=false;
            crew_.rooms.fill(CrewRoom{});crew_.alert.clear();
            renderer.setCrew(&crew_);renderer.setEconomy(nullptr,&market_);return;
        }
        if(fixture>=17){
            if(!signalView_)signalView_=signalGameplayProofView(Universe{});
            auto view=std::make_shared<DiscoveryView>(*signalView_);view->open=fixture!=18;
            if(fixture==18){view->monitoring=false;view->calls=2;}
            t.discovery=std::move(view);t.contactsExpanded=false;crew_.open=crew_.flightOpen=market_.open=false;
            renderer.setCrew(&crew_);renderer.setEconomy(nullptr,&market_);return;
        }
        if(fixture>=11){
            if(!discoveryViews_)discoveryViews_=discoveryProofViews(Universe{});
            const unsigned index=fixture==11||fixture==15?0:fixture==12?1:2;
            auto view=std::make_shared<DiscoveryView>((*discoveryViews_)[index]);
            view->open=fixture!=15;view->monitoring=fixture==15;view->journal=fixture==14;
            t.discovery=std::move(view);t.contactsExpanded=false;crew_.open=crew_.flightOpen=market_.open=false;
            renderer.setCrew(&crew_);renderer.setEconomy(nullptr,&market_);return;
        }
        if(fixture>=10){
            Universe u;const auto home=Address{}.child(Kind::Galaxy,0).child(Kind::System,9).child(Kind::Planet,0);
            auto body=sampleRaider(u,home,100);body.velocity={};body.systems[1]=0;body.hostile=false;
            VesselEngineering engineering;engineering.advance(body,.2,0,.25);
            NpcPlayer player;player.reference=home;player.position=body.position+glm::dvec3(0,0,body.radius+80);player.forward={0,0,-1};player.seconds=100;
            VesselOperations operations;ShipCrew party(u.seed());
            for(unsigned i=0;i<70;++i){player.seconds+=.1;operations.observe(u,player,body,&engineering,.1,true,1,std::span(&body,1));}
            operations.begin(VesselTask::Evacuate,body,&engineering,party);
            for(unsigned i=0;i<60;++i){player.seconds+=.1;operations.observe(u,player,body,&engineering,.1,false,1,std::span(&body,1));operations.advance(u,player,body,&engineering,party,.1,std::span(&body,1));}
            t.vessel=std::make_shared<VesselView>(operations.view(body,&engineering,true));t.contactsExpanded=false;
            crew_.open=crew_.flightOpen=market_.open=false;
            renderer.setCrew(&crew_);renderer.setEconomy(nullptr,&market_);return;
        }
        if(fixture>=6) {
            t.contactsExpanded=false;t.phase="PLANETARY FLIGHT";
            t.altitude=8500;t.weather="CLEAR / 281K";t.weatherDetail="WIND 8M/S";
            crew_.open=false;crew_.flightOpen=fixture==8;market_.open=false;crew_.selected=2;
            crew_.rooms[3].fire=40;crew_.rooms[4].oxygen=18;
            ShipPlant plant;PlantDemand demand;demand.throttle=.4;
            if(fixture==8){plant.heat=.94;plant.stores={8,6,3};plant.reserve=.2;demand.reactor=.2;demand.cooling=.1;demand.drill=1;demand.weapons=1;crew_.closedDoors=1;}
            else crew_.closedDoors=0;
            plant.advance(demand,.25);crew_.engineering=plant.readout();
            for(size_t i=0;i<crew_.members.size();++i){auto& member=crew_.members[i];
                member.room=member.walkTo=CrewStation(fixture==9?3:i%crewRoomCount);member.travel=0;member.health=i==4?40:100;}
            if(fixture!=9){auto& member=crew_.members[2];member.room=CrewStation::Weapons;
                member.walkTo=member.target=CrewStation::LifeSupport;member.travelDuration=10;member.travel=fixture==7?2.5:7.5;}
            renderer.setCrew(&crew_);renderer.setEconomy(nullptr,&market_);return;
        }
        // Presentation-only stress data; the navigation and ship simulation
        // continue to use their real state, checked in observe().
        t.weapon = "MINING PROBE LAUNCHER";
        t.explorationActive = true;
        t.exploration = {"MINING PROBE / NOSE", "SIX ROUNDS / CTRL-P LAUNCH", "SOLID TERRAIN / SAVED CAVERN",
                         "ALT-P CAMERA / CTRL-J ENTRANCE"};
        t.altitude = 35;
        t.gear = t.growthBlocked = true;
        t.descent = 100;
        t.stoppingDistance = 5000;
        t.crewFire = "GUNNERS HOLD FIRE / NEUTRAL SHIPS NEARBY";
        t.targetVisible = true;
        t.localTarget = {0, 0, -1};
        t.range = 4000;
        crew_.flightOpen = fixture == 2;
        crew_.open = fixture == 3;
        market_.open = fixture == 4;
        if (fixture == 2) {
            t.stationStatus = "CAPTAIN AT PORT GUN / CREW RELIEF PILOT ACTIVE";
            t.playerGun = 1;
            t.reliefHelm = "LIEUTENANT LONG SURNAME";
        }
        if (fixture == 5) {
            t.phase = "MINING PROBE CAMERA";
            t.exploration = {"BORING / CUTTER ACTIVE", "DEPTH 80M / TARGET 80M",
                             "CUTTER 100 RPM / LOAD 80% / TIP HEAT 50%", "ESC RETURNS TO SHIP / ALT-P CAMERA"};
        }
        renderer.setCrew(&crew_);
        renderer.setEconomy(nullptr, &market_);
    }
    std::string_view notice() const {
        return cases_[stage_].fixture
                   ? "TRANSACTION COMPLETE / MINING PROBE LAUNCHER FITTED / SIX ROUNDS AVAILABLE / NEW EQUIPMENT READY"
                   : "";
    }
    void observe(const FrameStats& stats, RegionCache& cache, Renderer& renderer,
                 const std::string& directory) {
        if (captured_) {
            if (hold_) {
                --hold_;
                return;
            }
            captured_ = false;
            placed_ = false;
            if (++stage_ == std::size(cases_))
                std::cout << "PASS: native UI fit / " << std::size(cases_) << " layouts and popups / world, traffic, crew and discovery hit testing / "
                             "camera unchanged\n";
            return;
        }
        const auto gen = cache.stats();
        stable_ = stats.visibleObjects && !gen.pending && !gen.queued && !stats.lodTransitions &&
                          !stats.uploadDeferred && !stats.weatherPending && (cases_[stage_].fixture!=16||stats.surfaceBuildings>=8)
                      ? stable_ + 1
                      : 0;
        if (stable_ < 10)
            return;
        const auto& c = cases_[stage_];
        const auto v = renderer.cockpitViewport();
        const CockpitLayout layout(v.aspect());
        if (nav_.serialize() != camera_ || v.x < 0 || v.y < 0 || v.x + v.width > c.w || v.y + v.height > c.h)
            throw std::runtime_error("UI resize changed the camera or clipped the HUD viewport");
        const auto worlds = renderer.celestialContacts();
        const auto ships = renderer.activityContacts();
        if (c.fixture!=16&&(worlds.empty() || ships.empty()))
            throw std::runtime_error("UI fixture has no real contacts to test");
        const auto worldPoint = v.toPixels({layout.celestial.x + 24, 217});
        const auto shipPoint = v.toPixels({layout.traffic.x + 24, 217});
        const auto world = renderer.pick(worldPoint.x, worldPoint.y);
        const auto ship = renderer.pickActivity(shipPoint.x, shipPoint.y);
        if(c.fixture==16){
            if(stats.surfaceBuildings<8)throw std::runtime_error("Generated archaeological blocks were not drawn on the planet");
            std::cout<<"ARCHAEOLOGY_SURFACE visible_blocks="<<stats.surfaceBuildings<<" same_camera=PASS\n";
        }else if(c.fixture>=27){
            const auto point=v.toPixels(c.fixture==30?glm::dvec2(40,289):glm::dvec2(450*v.aspect(),400));
            if(renderer.pick(point.x,point.y)||renderer.pickActivity(point.x,point.y))throw std::runtime_error("Ship signal instrument allowed click-through");
        }else if(c.fixture>=19){
            const auto instrument=v.toPixels({450*v.aspect(),725});
            if(renderer.pick(instrument.x,instrument.y)||renderer.pickActivity(instrument.x,instrument.y))throw std::runtime_error("Targeting instrument allows click-through");
        }else if(c.fixture>=11){
            if(c.fixture==15||c.fixture==18){const auto point=v.toPixels({40,289});
                if(renderer.pick(point.x,point.y)||renderer.pickActivity(point.x,point.y))throw std::runtime_error("Pinned receiver allows click-through");
                const auto crew=v.toPixels({900*v.aspect()-160,360});
                if(!renderer.crewMinimapHit(crew.x,crew.y))throw std::runtime_error("Pinned radio hides live crew access");
            }else{
                const auto centre=v.toPixels({450*v.aspect(),400});
                const auto crew=v.toPixels({900*v.aspect()-160,360});
                if(renderer.pick(centre.x,centre.y)||renderer.pickActivity(centre.x,centre.y)||renderer.crewMinimapHit(crew.x,crew.y))throw std::runtime_error("Discovery console allowed click-through or hidden map picks");
            }
        }else if(c.fixture>=10){
            const auto centre=v.toPixels({450*v.aspect(),400});
            const auto hidden=v.toPixels({900*v.aspect()-160,360});
            if(renderer.pick(centre.x,centre.y)||renderer.pickActivity(centre.x,centre.y)||renderer.crewMinimapHit(hidden.x,hidden.y))
                throw std::runtime_error("Contact console allowed selection through modal or hidden crew map");
        }else if(c.fixture>=6) {
            const auto map=crewMinimap(crew_,v.aspect());
            for(size_t i=0;i<map.count;++i){const auto point=v.toPixels(map.markers[i].label);
                const auto canvas=v.toCanvas(point);
                if(map.pickCrew(canvas)!=map.markers[i].id||!renderer.crewMinimapHit(point.x,point.y)||renderer.pick(point.x,point.y)||renderer.pickActivity(point.x,point.y))
                    throw std::runtime_error("Native minimap selected the wrong crew identity or a world behind the map");}
            const EngineeringLayout engineering(v.aspect());
            for(const auto button:engineering.presets){const auto point=v.toPixels({button.x+button.w*.5,button.y+button.h*.5});
                if(!renderer.engineeringHit(point.x,point.y)||renderer.pick(point.x,point.y)||renderer.pickActivity(point.x,point.y))
                    throw std::runtime_error("Native engineering controls select a world behind the cockpit");}
        } else if (help_ || c.fixture >= 3) {
            if (world || ship)
                throw std::runtime_error("Controls guide allowed world selection through its panels");
            const auto hidden=v.toPixels({900*v.aspect()-160,360});
            if(renderer.crewMinimapHit(hidden.x,hidden.y))throw std::runtime_error("Hidden minimap remained clickable under a modal or remote feed");
            const auto engineering=v.toPixels({900*v.aspect()-160,550});
            if(renderer.engineeringHit(engineering.x,engineering.y))throw std::runtime_error("Hidden engineering controls remained clickable");
        } else if (world != worlds[cockpitCelestialRows(worlds).front()].address ||
                   (c.fixture == 2 ? bool(ship) : ship != ships[cockpitActivityRows(ships).front()].body.id))
            throw std::runtime_error("Resized/Retina contact rows select the wrong world or craft");
        const auto blocked = v.toPixels({40, 80});
        if (renderer.pick(blocked.x, blocked.y) || renderer.pickActivity(blocked.x, blocked.y))
            throw std::runtime_error("Clicking an instrument selected an object behind the HUD");
        renderer.screenshot((std::filesystem::path(directory) / (std::string(c.name) + ".ppm")).string());
        std::cout << "UI_FIT case=" << c.name << " pixels=" << c.w << ',' << c.h << " viewport=" << v.x << ','
                  << v.y << ',' << v.width << ',' << v.height << " projection=" << v.projectionScale
                  << " hit_testing=PASS camera=unchanged text_overlap=0\n";
        captured_ = true;
        hold_ = 6;
    }
    bool finished() const {
        return stage_ == std::size(cases_);
    }

  private:
    struct Case {
        const char* name;
        int w, h;
        RenderMode mode;
        bool help;
        unsigned fixture;
    };
    static constexpr Case cases_[] = {{"small", 800, 500, RenderMode::Hyperion, false, 0},
                                      {"square", 600, 600, RenderMode::Hyperion, false, 0},
                                      {"portrait", 600, 900, RenderMode::Hyperion, false, 0},
                                      {"retina", 1920, 1200, RenderMode::Hyperion, false, 0},
                                      {"wide", 1440, 900, RenderMode::Hyperion, false, 0},
                                      {"controls", 800, 500, RenderMode::Hyperion, true, 0},
                                      {"crowded", 800, 500, RenderMode::Hyperion, false, 1},
                                      {"station", 600, 600, RenderMode::Hyperion, false, 2},
                                      {"crew", 600, 900, RenderMode::Hyperion, false, 3},
                                      {"market", 800, 500, RenderMode::Hyperion, false, 4},
                                      {"probe", 800, 500, RenderMode::Hyperion, false, 5},
                                      {"probe_portrait", 600, 900, RenderMode::Hyperion, false, 5},
                                      {"crewmap_wide",1440,900,RenderMode::Hyperion,false,6},
                                      {"crewmap_portrait",600,900,RenderMode::Hyperion,false,7},
                                      {"crewmap_orders",960,600,RenderMode::Hyperion,false,8},
                                      {"crewmap_crowded",800,500,RenderMode::Hyperion,false,9},
                                      {"vessel_wide",1440,900,RenderMode::Hyperion,false,10},
                                      {"vessel_portrait",600,900,RenderMode::Hyperion,false,10},
                                      {"receiver_wide",1440,900,RenderMode::Hyperion,false,11},
                                      {"receiver_portrait",600,900,RenderMode::Hyperion,false,11},
                                      {"archaeology_wide",1440,900,RenderMode::Hyperion,false,12},
                                      {"archaeology_portrait",600,900,RenderMode::Hyperion,false,12},
                                      {"archaeology_history",960,600,RenderMode::Hyperion,false,13},
                                      {"discovery_journal",800,500,RenderMode::Hyperion,false,14},
                                      {"radio_monitor",800,500,RenderMode::Hyperion,false,15},
                                      {"radio_call_wide",1440,900,RenderMode::Hyperion,false,17},
                                      {"radio_call_portrait",600,900,RenderMode::Hyperion,false,17},
                                      {"radio_alert",800,500,RenderMode::Hyperion,false,18},
                                      {"target_acquiring",1440,900,RenderMode::Hyperion,false,19},
                                      {"target_locked",1440,900,RenderMode::Hyperion,false,20},
                                      {"target_portrait",600,900,RenderMode::Hyperion,false,20},
                                      {"target_blocked",960,600,RenderMode::Hyperion,false,21},
                                      {"target_offscreen",800,500,RenderMode::Hyperion,false,22},
                                      {"threat_locking",1440,900,RenderMode::Hyperion,false,23},
                                      {"threat_portrait",600,900,RenderMode::Hyperion,false,23},
                                      {"threat_directional_hit",960,600,RenderMode::Hyperion,false,24},
                                      {"threat_missile",1440,900,RenderMode::Hyperion,false,25},
                                      {"threat_decoy",800,500,RenderMode::Hyperion,false,26},
                                      {"ship_emitter",1440,900,RenderMode::Hyperion,false,27},
                                      {"ship_emitter_portrait",600,900,RenderMode::Hyperion,false,27},
                                      {"ship_scan",960,600,RenderMode::Hyperion,false,28},
                                      {"ship_jam",1440,900,RenderMode::Hyperion,false,29},
                                      {"ship_jam_monitor",800,500,RenderMode::Hyperion,false,30},
                                      {"relic_surface",1440,900,RenderMode::Hyperion,false,16}};
    SDL_Window* window_;
    Navigation& nav_;
    Pilot& pilot_;
    std::string camera_;
    CrewView crew_;
    std::optional<std::array<DiscoveryView,3>> discoveryViews_;
    std::optional<DiscoveryView> signalView_;
    std::optional<std::array<TargetingView,4>> targetingViews_;
    std::optional<std::array<CombatAwarenessView,4>> threatViews_;
    std::optional<std::array<DiscoveryView,3>> shipSignalViews_;
    std::shared_ptr<const DiscoveryPage> ruinPage_;
    MarketView market_;
    size_t stage_ = 0;
    unsigned stable_ = 0, hold_ = 0;
    bool placed_ = false, captured_ = false, help_ = false;
};
} // namespace infinity
