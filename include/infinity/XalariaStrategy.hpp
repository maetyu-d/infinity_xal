#pragma once
#include "XalariaConvoys.hpp"
#include <deque>
#include <set>
namespace infinity {
class XalariaCombat;
struct CombatPlayer;
struct StoryContext;
inline constexpr uint32_t campaignActivityTag=0x60000000u, fleetActivityTag=0x70000000u;
inline bool isStrategyActor(const ActivityId& id){return (id.slot&0xf0000000u)==campaignActivityTag||(id.slot&0xf0000000u)==fleetActivityTag;}
inline constexpr double strategicTurn=300.;
enum class FleetOrder : unsigned {Hold,Follow,Trade,Escort,Blockade,Salvage};
std::string_view fleetOrderName(FleetOrder);
struct StrategicStation {
 XPlace place;XFaction owner=XFaction::Independent;XMarket market;
 unsigned garrison=4,industry=0;bool playerOwned=false,dirty=false;
 int64_t treasury=0,ore=24000;uint64_t production=0;double shortage=0.;
};
struct StrategicShipment {int64_t units=0;bool settled=false,lost=false;};
struct FactionCampaign {
 uint32_t id=0;XPlace origin,target;XFaction attacker=XFaction::Rebel;
 unsigned force=0,dead=0;double departure=0,arrival=0,physicalAt=-100.,siege=0.;
 bool finished=false,victory=false,player=false;uint32_t fleet=0;
};
struct FleetShip {
 uint32_t id=0;uint64_t seed=0;std::optional<ActivityId> source;XPlace origin,destination;
 std::string name,captain;ActivityKind kind=ActivityKind::Freighter;
 FleetOrder order=FleetOrder::Hold;std::vector<uint32_t> route;
 double departure=0,arrival=0,hull=100.,engines=100.,radius=48.;
 XStock cargo{};int64_t costBasis=0;bool contraband=false,underway=false,lost=false,launched=false;
 unsigned loyalty=50,missions=0;std::optional<ConvoyKey> escort;glm::dvec3 mooring{},velocity{};glm::dquat axes{1,0,0,0};Address frame;
};
struct StrategicWreck {ActivityBody body;int64_t salvage=0;unsigned survivors=0;double created=0.;bool known=false;};
struct StrategicReport {uint64_t id=0;uint32_t system=0;double seconds=0.;std::string text;};
class XalariaStrategy {
 public:
 XalariaStrategy(const Universe&,XalariaEconomy&,XalariaCombat&);
 std::string destinationLabel="F2 Atlas";double time()const{return clock_;}
 bool open=false;unsigned page=0;size_t row=0;std::string message="Dock, hail ships, scan nearby activity or board a vessel to earn intelligence.";
 const auto& stations()const{return stations_;}const auto& fleet()const{return fleet_;}
 const auto& campaigns()const{return campaigns_;}const auto& wrecks()const{return wrecks_;}
 const auto& reports()const{return reports_;}const auto& knowledge()const{return knowledge_;}
 const auto& recruits()const{return recruits_;}XFaction allegiance()const{return allegiance_;}
 int trust(XFaction f)const{return trust_[size_t(f)];}
 const StrategicStation* station(const XPlace&)const;XMarket* market(const XPlace&)const;
 void adoptMarkets();bool controls(const XPlace&)const;
 double marketFactor(const XPlace&,XGood)const;
 const StrategicShipment* shipment(ConvoyKey key)const{const auto it=shipments_.find(key);return it==shipments_.end()?nullptr:&it->second;}
 int64_t allocate(ConvoyKey);void complete(ConvoyKey,int64_t,bool lost=false);
 void manifestChanged(ConvoyKey,int64_t);void noticeDelivery(ConvoyKey,bool escort);
 bool escorted(ConvoyKey)const;bool interdicts(const ActivityId&,ConvoyKey)const;
 void advance(const CombatPlayer&,double);
 bool discover(uint32_t,std::string_view reason);bool scan(const CombatPlayer&);bool informer();
 bool pledge();bool capture(const StoryContext&);bool hire(uint32_t,const StoryContext&);
 bool purchase(ActivityKind);bool order(uint32_t,FleetOrder,uint32_t destination);
 bool salvage(const ActivityId&,const CombatPlayer&,bool rescue=false);bool collectTaxes();
 bool ownsSource(const ActivityId&)const;bool suppresses(const ActivityId&)const;
 bool validIdentity(const ActivityId&)const;
 std::optional<XalariaTrafficContact> contact(const ActivityId&,double)const;
 std::vector<ActivityBody> bodies(uint32_t,double)const;
 void struck(const ActivityBody&,double,bool playerShot,bool destroyed);
 void simulate(const ActivityBody&,std::span<const ActivityBody>,const CombatPlayer&,double,const Geometry*);
 bool lawfulTarget(const ActivityId&)const;
 void tax(const XPlace&,int64_t);
 std::string advisory(const XPlace&)const;std::string radio(const ActivityId&)const;
 std::string serialize()const;bool restore(std::string_view,double);
 void step(int);
 private:
 const Universe& universe_;XalariaEconomy& economy_;XalariaCombat& combat_;
 mutable std::map<XPlace,StrategicStation> stations_;
 std::map<ConvoyKey,StrategicShipment> shipments_;
 std::map<uint32_t,FleetShip> fleet_;std::map<uint32_t,FactionCampaign> campaigns_;
 std::map<ActivityId,StrategicWreck> wrecks_;std::set<uint32_t> knowledge_;
 std::deque<StrategicReport> reports_;std::vector<std::string> recruits_;
 std::array<int,5> trust_{};XFaction allegiance_=XFaction::Independent;
 bool adoptedMarkets_=false;double clock_=0.,turn_=0.;uint32_t nextFleet_=1,nextCampaign_=1;uint64_t nextReport_=1;
 void addStation(const XPlace&);void tick(double);void advanceFleet(const CombatPlayer&,double);
 void report(uint32_t,std::string);std::vector<uint32_t> route(uint32_t,uint32_t)const;
 ActivityBody fleetBody(const FleetShip&,double)const;ActivityBody campaignBody(const FactionCampaign&,unsigned,double)const;
 XFaction side(const ActivityBody&)const;bool fail(std::string);
 void arrive(FleetShip&);bool depart(FleetShip&,XPlace,double);void launchCampaign(XPlace,XPlace,XFaction,unsigned,double,uint32_t=0);
};
struct XalariaStrategyLayout {
 double width;XalariaMapRect close,list;std::array<XalariaMapRect,3> tabs;std::array<XalariaMapRect,4> acquire;std::array<XalariaMapRect,6> orders,intel;
 explicit XalariaStrategyLayout(double aspect);
};
CockpitOverlay xalariaStrategyOverlay(const XalariaStrategy&,const XalariaEconomy&,double aspect);
}
