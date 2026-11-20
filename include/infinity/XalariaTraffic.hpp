#pragma once
#include "XalariaEconomy.hpp"

namespace infinity {
enum class TrafficService { Port, Orbital, Wormhole };
enum class TrafficRequest { Hail, FlightPlan, Advisory, Rendezvous, PortMarket, FreightWork, FreightDispatch, Dock };
inline constexpr double trafficRadioRange=100000.;
inline constexpr double minimumTrafficCollarRadius=12.;
inline constexpr double vesselCollarCentre(double radius) {return 1.2*radius+1.2;}
inline constexpr double vesselCollarMountLength(double radius) {return .88*radius+1.2;}

struct XalariaTrafficContact {
    ActivityBody body;
    XPlace port;
    TrafficService service;
    TrafficPhase phase;
    std::optional<uint32_t> gateDestination;
    double dockingHold=0.;
    ShieldReadout shield;
};

struct TrafficMotion {
    ActivityBody start;
    glm::dvec3 end;
    glm::dquat endOrientation;
};

struct TrafficImpact {
    const ActivityBody* vessel;
    double fraction;
    glm::dvec3 eye, normal, surfaceVelocity, leverage;
};

struct TrafficFreightLead {
    XContract charter;
    bool aboard, atPort;
    const XPlace& nextPort() const { return aboard?charter.destination:charter.origin; }
};

class TrafficHoldProgress {
  public:
    void update(double range, double relativeSpeed, double seconds);
    bool matched() const { return matched_; }
  private:
    double steady_=0.;
    bool matched_=false;
};

struct XalariaRadioView {
    std::optional<ActivityId> selected;
    std::optional<ActivityId> dockingTarget;
    std::optional<ActivityId> recoveryNotice;
    std::optional<TrafficFreightLead> freight;
    std::string callsign, activity, reply, rendezvous;
    ShieldReadout shield;
    bool open=false;
};

bool isXalariaTraffic(const ActivityId&);
std::string xalariaTrafficCallsign(const ActivityId&);
std::optional<ActivityId> xalariaOrbitalFreighter(const XalariaEconomy&,const XPlace&);
double trafficShieldCapacity(const ActivityBody&);
ActivityBody xalariaTrafficBody(const ActivityId& station,TrafficService,size_t index,const TrafficCraft&,
                               glm::dvec3 centre,glm::dquat axes);
std::optional<XalariaTrafficContact> xalariaTrafficContact(const XalariaEconomy&,const ActivityId&,
    const std::optional<XalariaWormholeMouth>&,double seconds);
std::vector<TrafficMotion> nearbyXalariaTraffic(const Universe&,const XalariaEconomy&,const Address& frame,
    glm::dvec3 eyeMetres,glm::dvec3 velocity,uint32_t system,const XalariaWormholeMouth*,double seconds,double dt);
std::optional<TrafficImpact> sweepXalariaTraffic(const Universe&,const Address& frame,std::span<const TrafficMotion>,
    glm::dvec3 from,glm::dvec3 to,glm::dquat fromAxes,glm::dquat toAxes,double radius,
    double begin,double end,double dt);
std::string xalariaTrafficReply(const XalariaCatalog&,const XalariaEconomy&,
    const XalariaTrafficContact&,TrafficRequest,double closingMetresPerSecond);
std::optional<FlightWaypoint> xalariaTrafficRendezvous(const XalariaEconomy&,const XalariaTrafficContact&);
std::optional<TrafficFreightLead> xalariaTrafficFreight(const XalariaEconomy&,const XPlace&);
std::string xalariaFreightBrief(const XalariaEconomy&,const XPlace&,const std::optional<TrafficFreightLead>&);
std::optional<DockingBerth> xalariaVesselBerth(const ActivityBody&);
std::optional<FlightWaypoint> xalariaVesselApproach(const XalariaEconomy&,const ActivityId&,
    const std::optional<XalariaWormholeMouth>&,double seconds);
StationKey xalariaVesselInterior(const ActivityBody&);
XContract xalariaVesselParcel(const XalariaEconomy&,const XalariaTrafficContact&);
Mesh xalariaVesselCollar();
Mesh xalariaVesselCollarMount();
} // namespace infinity
