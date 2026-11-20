#pragma once
#include "XalariaTraffic.hpp"
#include "Weapons.hpp"
#include "XalariaConvoys.hpp"
#include "ShipExplosion.hpp"
#include "XalariaStrategy.hpp"
#include <map>

namespace infinity {
struct StoryContext;
enum class CombatFit : unsigned { Civilian, Corsair, Gunship };
enum class CombatWeapon : unsigned { Pulse, Ion, Torpedo };
enum class CombatMode : unsigned { Circuit, Attack, Flee, Surrendered, Disabled, Wreck, Return, Skirmish };
struct CombatPlayer {
    Address frame;
    glm::dvec3 position{},velocity{},forward{0,0,-1};
    glm::dquat orientation{1,0,0,0};
    double seconds=0.;
    bool sheltered=false,transit=false,weaponsInhibited=false;
    std::optional<ActivityId> clamped;
    std::optional<XalariaWormholeMouth> gate;
};
struct CombatShip {
    double hull=100.,engines=100.,aggroUntil=0.,cooldown=0.;
    CombatMode mode=CombatMode::Circuit;
    glm::dvec3 previous{},position{},velocity{};
    glm::dquat previousOrientation{1,0,0,0},orientation{1,0,0,0};
    double from=0.,until=0.;
    bool moving=false,bountyRecorded=false,threatened=false;
    int64_t cargoLeft=-1;
};
inline bool isCombatActor(const ActivityId& id){return (id.slot&0xe0000000u)==combatActivityTag&&(id.slot&15u)<3;}
ActivityId trafficStation(const ActivityId&);
std::string_view combatFitName(CombatFit);
std::string_view combatModeName(CombatMode);

class XalariaCombat {
  public:
    XalariaCombat(const Universe&,XalariaEconomy&,Pilot&);
    XalariaStrategy& strategy(){return strategy_;}
    const XalariaStrategy& strategy()const{return strategy_;}
    XalariaConvoys& convoys(){return convoys_;}
    const XalariaConvoys& convoys()const{return convoys_;}
    bool open=false,armed=false;
    CombatWeapon weapon=CombatWeapon::Pulse;
    std::optional<ActivityId> target;
    std::string incomingFire;double incomingAt=-10.;
    std::string message="Fit a Corsair or Gunship at a station. M arms weapons; hold Space to fire.";
    CombatFit fit()const{return fit_;}
    double heat()const{return heat_;}
    unsigned torpedoes()const{return torpedoes_;}
    uint64_t shots()const{return shots_;}
    uint64_t victories()const{return victories_;}
    int64_t bountyDue()const{return bountyDue_;}
    int64_t wanted(uint32_t system)const;
    int64_t localWarrant()const{return wanted(economy_.currentSystem());}
    const XStock& stolen()const{return stolen_;}
    const WeaponSystem& effects()const{return effects_;}
    std::span<const ShipExplosion> explosions()const{return explosions_;}
    bool refit(CombatFit);
    bool service();
    bool recover(Navigation&);
    double playerHull()const{return pilot_.ship().hull;}
    int64_t prizeCargo(const ActivityId&)const;
    std::optional<XalariaTrafficContact> selected()const;
    bool claimBounty();
    bool permits(const XPlace&)const;
    bool maySell(const XPlace&,XGood,int64_t)const;
    void cargoSold(XGood,int64_t,int64_t before);
    bool demand(const ActivityId&,const CombatPlayer&);
    bool seize(const ActivityId&,const StoryContext&);
    bool mayDock(const ActivityBody&)const;
    bool fire(const CombatPlayer&,const Geometry* =nullptr);
    void advance(const CombatPlayer&,double dt,const Geometry* =nullptr);
    void apply(ActivityBody&,double seconds)const;
    bool shieldsLowered(const ActivityId&)const;
    std::optional<XalariaTrafficContact> contact(const ActivityId&,double seconds)const;
    std::vector<ActivityBody> actors(const XPlace&,double seconds)const;
    std::vector<TrafficMotion> collisionMotions(const Address&,glm::dvec3 eye,uint32_t system,double seconds,double dt)const;
    std::vector<ActivityBody> localBodies(const CombatPlayer&)const;
    std::string serialize()const;
    bool restore(std::string_view,double seconds);
    void annotate(FlightInstruments&)const;
  private:
    friend class XalariaConvoys;
    friend class XalariaStrategy;
    const Universe& universe_;XalariaEconomy& economy_;Pilot& pilot_;
    XalariaConvoys convoys_;
    XalariaStrategy strategy_;
    bool outlaw(const ActivityId&)const;
    CombatFit fit_=CombatFit::Civilian;
    double heat_=0.,cooldown_=0.,lastSeconds_=0.;
    bool overheated_=false;
    unsigned torpedoes_=0;
    uint64_t shots_=0,victories_=0,repairs_=0;
    int64_t bountyDue_=0;
    XStock stolen_{};
    std::map<ActivityId,CombatShip> ships_;
    std::map<uint32_t,int64_t> warrants_;
    WeaponSystem effects_;
    std::vector<ShipExplosion> explosions_;
    struct Torpedo {Address frame;glm::dvec3 position,direction;ActivityId target;double age=0.;};
    std::vector<Torpedo> missiles_;
    std::optional<XalariaWormholeMouth> lastGate_;
    ActivityBody canonical(const ActivityId&,double seconds)const;
    CombatShip* state(const ActivityBody&,double seconds);
    void crime(const ActivityId&,int64_t points);
    void hit(ActivityBody,double load,bool ion,double seconds,glm::dvec3 impact,bool playerShot=true);
    void configurePilot();
};
struct XalariaCombatLayout {
    double width;
    XalariaMapRect close,civilian,corsair,gunship,arm,pulse,ion,torpedo,demand,seize,claim,service,board;
    explicit XalariaCombatLayout(double aspect);
};
CockpitOverlay xalariaCombatOverlay(const XalariaCombat&,const XalariaEconomy&,double aspect);
void xalariaCombatHud(CockpitOverlay&,const XalariaCombat&,double aspect,const ActivityContact* contact=nullptr,double projectionScale=1.);
struct CabinDisplay;
CabinDisplay xalariaPrizeDisplay(const StationLayout&,const XalariaTrafficContact&,const XalariaCombat&);
} // namespace infinity
