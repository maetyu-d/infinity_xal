#pragma once
#include "XalariaTraffic.hpp"
#include <map>

namespace infinity {
class XalariaCombat;
class XalariaNavigation;
struct CombatPlayer;
struct StoryContext;
inline constexpr uint32_t convoyActivityTag=0x10000000u;
inline bool isConvoyActor(const ActivityId& id){return (id.slot&0xf0000000u)==convoyActivityTag&&(id.slot&15u)<6;}
inline constexpr double convoyPeriod=1800.;
struct ConvoyKey {uint32_t service;uint64_t cycle;auto operator<=>(const ConvoyKey&)const=default;};
enum class ConvoyDuty : unsigned {None,Escort,Pirate};
enum class ConvoyPhase : unsigned {Loading,Outbound,Inbound,Delivered,Stranded,Lost};
struct ConvoyPlan {
    uint32_t service;
    XPlace origin,destination;
    std::vector<uint32_t> route;
    XFaction faction,rival;
    XGood good;
    int64_t units;
    double offset;
};
struct ConvoyStage {
    Address frame;
    glm::dvec3 position{},velocity{};
    glm::dquat orientation{1,0,0,0};
    uint32_t system;
    size_t leg;
    double localSeconds;
    ConvoyPhase phase;
};
struct ConvoyRecord {
    bool known=false,physical=false,delivered=false,claimed=false,betrayed=false,background=false;
    ConvoyDuty duty=ConvoyDuty::None;
    double joined=0.,accompanied=0.,haltedAt=-1.,lastPhysical=0.;
    int64_t remaining=0,stolen=0;
    unsigned dead=0;
    Address frame;
    uint64_t npcShots=0,npcHits=0,playerHits=0;
};
class XalariaConvoys {
  public:
    XalariaConvoys(const Universe&,XalariaEconomy&,XalariaCombat&);
    bool open=false,following=false;
    size_t row=0;
    std::string message="Hail a convoy, hear its distress call, or dock for local departures.";
    const std::map<uint32_t,ConvoyPlan>& plans()const{return plans_;}
    const ConvoyPlan& plan(ConvoyKey)const;
    const ConvoyRecord* record(ConvoyKey)const;
    const std::map<ConvoyKey,ConvoyRecord>& records()const{return records_;}
    std::vector<ConvoyKey> known()const;
    std::optional<ConvoyKey> selected()const;
    std::optional<ConvoyKey> tracked()const{return tracked_;}
    void step(int);
    double departure(ConvoyKey)const;
    double arrival(ConvoyKey)const;
    double seconds()const{return seconds_;}
    bool factionBattle(ConvoyKey)const;
    ConvoyStage stage(ConvoyKey,double)const;
    XalariaWormholeMouth mouth(uint32_t,uint32_t)const;
    std::optional<XPlace> port(uint32_t)const;
    std::optional<ConvoyKey> identity(const ActivityId&,double)const;
    bool validIdentity(const ActivityId&)const;
    ActivityId id(ConvoyKey,unsigned,const Address&)const;
    std::optional<XalariaTrafficContact> contact(const ActivityId&,double)const;
    std::vector<ActivityBody> bodies(const Address&,double)const;
    std::vector<ActivityBody> localBodies(uint32_t,double)const;
    bool discover(ConvoyKey);
    bool hear(const ActivityId&,double);
    bool join(ConvoyKey,ConvoyDuty,const CombatPlayer&);
    bool report();
    bool follow(Navigation&,Pilot&,XalariaNavigation&);
    std::optional<FlightWaypoint> waypoint(const CombatPlayer&)const;
    void advance(const CombatPlayer&,double);
    void simulate(const ActivityBody&,std::span<const ActivityBody>,const CombatPlayer&,double,const Geometry*);
    std::optional<ActivityId> localTarget(const ActivityBody&,std::span<const ActivityBody>,const CombatPlayer&)const;
    void struck(const ActivityBody&,double seconds,bool playerShot);
    bool seize(const ActivityId&,const StoryContext&);
    int64_t prizeCargo(const ActivityId&)const;
    bool raider(const ActivityId&)const;
    bool lawfulTarget(const ActivityId&)const;
    bool hostile(const ActivityId&)const;
    int standing(XFaction f)const{return standing_[size_t(f)];}
    double marketFactor(const XPlace&,XGood)const;
    std::string radio(const ActivityId&,TrafficRequest,double)const;
    std::string summary(ConvoyKey,double)const;
    std::string serialize()const;
    bool restore(std::string_view,double seconds);
  private:
    friend class XalariaStrategy;
    const Universe& universe_;XalariaEconomy& economy_;XalariaCombat& combat_;
    std::map<uint32_t,ConvoyPlan> plans_;
    std::map<uint32_t,std::vector<uint32_t>> through_;
    std::map<uint32_t,XPlace> ports_;
    mutable std::map<uint32_t,XalariaWormholeMouth> mouths_;
    std::map<ConvoyKey,ConvoyRecord> records_;
    std::array<int,5> standing_{};
    std::optional<ConvoyKey> tracked_;
    double seconds_=0.,lastNotice_=-30.;
    std::vector<ConvoyKey> localKeys(uint32_t,double)const;
    ConvoyRecord& activate(ConvoyKey,double);
    ActivityBody sample(ConvoyKey,unsigned,double)const;
    void prepare(ConvoyKey,double);
    void finish(ConvoyKey,double);
    void reputation(XFaction,int);
    int side(ConvoyKey,const ActivityBody&)const;
};
std::string_view convoyPhaseName(ConvoyPhase);
struct XalariaConvoyLayout {
    double width;XalariaMapRect close,list,escort,pirate,follow,hail,report;
    explicit XalariaConvoyLayout(double aspect);
};
CockpitOverlay xalariaConvoyOverlay(const XalariaConvoys&,const XalariaEconomy&,double aspect,double seconds);
void xalariaConvoyHud(CockpitOverlay&,const XalariaConvoys&,double aspect,double seconds);
} // namespace infinity
