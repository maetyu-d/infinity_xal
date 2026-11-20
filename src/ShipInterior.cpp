#include "infinity/ShipInterior.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinity {
namespace {
using namespace shipDeck;
constexpr double playerRadius = .127;
constexpr double crewClearance = .31;
// Clears the widest silhouette across all poses, including the working reach.
constexpr double crewRadius = .47;
constexpr double crewStep = .025;
constexpr std::array walkable{
    DeckArea{-.626,-.53,.626,.91},
    DeckArea{-.77,.65,.77,hatch},
    DeckArea{-hatchHalf,.90,hatchHalf,1.40},
    DeckArea{-passageHalf,1.33,passageHalf,cargoFront+.06},
    DeckArea{-quartersWall,quartersFront,-passageHalf,quartersBack},
    DeckArea{passageHalf,quartersFront,quartersWall,quartersBack},
    DeckArea{-cargoHalf,cargoFront+.06,cargoHalf,cargoBack}
};
constexpr std::array walls{
    seat,bunk,systems,
    DeckArea{-passageHalf-.035,quartersFront,-passageHalf+.035,quartersDoorFront},
    DeckArea{-passageHalf-.035,quartersDoorBack,-passageHalf+.035,quartersBack},
    DeckArea{passageHalf-.035,quartersFront,passageHalf+.035,quartersDoorFront},
    DeckArea{passageHalf-.035,quartersDoorBack,passageHalf+.035,quartersBack},
    DeckArea{-cargoHalf,cargoFront-.08,-cargoDoorHalf,cargoFront+.08},
    DeckArea{cargoDoorHalf,cargoFront-.08,cargoHalf,cargoFront+.08},
    DeckArea{-.67,cargoScreenZ-.025,.67,cargoBack}
};

constexpr std::array crewFixtures{
    DeckArea{-.83,1.41,-.55,2.03},    // Navigation terminal and its wall trim
    DeckArea{1.48,2.05,1.63,3.15},   // Systems monitor and cooling pipes
    DeckArea{-passageHalf-.035,quartersDoorBack-.04,-passageHalf+.035,quartersDoorBack},
    DeckArea{passageHalf-.035,quartersDoorBack-.04,passageHalf+.035,quartersDoorBack},
    DeckArea{-cargoHalf,cargoFront-.04,-cargoDoorHalf+.036,cargoFront+.04},
    DeckArea{cargoDoorHalf-.036,cargoFront-.04,cargoHalf,cargoFront+.04}
};

bool collides(glm::dvec2 point, const DeckArea& area, double radius) {
    const auto nearest = glm::clamp(point,glm::dvec2(area.left,area.front),glm::dvec2(area.right,area.back));
    const auto offset = point-nearest;
    return glm::dot(offset,offset) < radius*radius;
}

bool canStand(glm::dvec2 point, double radius, double hatchOpening, const StationLayout* station = nullptr,
              double shipDoorOpening = 0, double bridgeDoorOpening = 0) {
    if (std::abs(point.y-hatch)<.19 && std::abs(point.x)+radius>hatchHalf*smoothstep(0.,1.,hatchOpening))
        return false;
    if (station) {
        const auto blockedDoor = [&](double x,double opening) {
            const DeckArea door{x-.055,stationDeck::centre-stationDeck::doorHalf,
                                x+.055,stationDeck::centre+stationDeck::doorHalf};
            return opening<stationDeck::doorClear && collides(point,door,radius);
        };
        if (blockedDoor(stationDeck::shipDoor,shipDoorOpening) || blockedDoor(stationDeck::bridgeDoor,bridgeDoorOpening))
            return false;
        const auto hitsFixture=[&](const StationModule& module) {
            return point.x>=module.floor.left-radius && point.x<=module.floor.right+radius &&
                   std::any_of(module.fixtures.begin(),module.fixtures.end(),
                               [&](const auto& fixture){return collides(point,fixture,radius);});
        };
        if (std::any_of(station->modules.begin(),station->modules.end(),hitsFixture))
            return false;
        for (const auto& annex : station->annexes) {
            const auto wall=annex.insideWall();
            const auto& area=annex.room.floor;
            const DeckArea left{area.left,wall-.058,annex.doorwayLeft,wall+.058};
            const DeckArea right{annex.doorwayRight,wall-.058,area.right,wall+.058};
            if (hitsFixture(annex.room) || collides(point,left,radius) || collides(point,right,radius))
                return false;
        }
    }
    constexpr double diagonal = .707106781;
    constexpr std::array<glm::dvec2,8> boundary{{{-1,0},{1,0},{0,-1},{0,1},
        {-diagonal,-diagonal},{diagonal,-diagonal},{-diagonal,diagonal},{diagonal,diagonal}}};
    for (const auto direction : boundary) {
        const auto fits = [&](const DeckArea& area){return area.contains(point+direction*radius);};
        if (std::none_of(walkable.begin(),walkable.end(),fits) &&
            (!station || !station->roomAt(point+direction*radius)))
            return false;
    }
    const auto blocked = [&](const DeckArea& area){return collides(point,area,radius);};
    return std::none_of(walls.begin(),walls.end(),blocked) &&
           std::none_of(cargoCrates.begin(),cargoCrates.end(),blocked);
}

bool crewCanStand(glm::dvec2 point, double hatchOpening) {
    return canStand(point,crewRadius,hatchOpening) &&
           std::none_of(crewFixtures.begin(),crewFixtures.end(),
                        [&](const auto& area){return collides(point,area,crewRadius);});
}

struct CrewStation {
    glm::dvec2 position;
    double yaw;
};
constexpr std::array<CrewStation,4> stations{{
    {{-1.03,2.66},1.50},
    {{.99,2.68},-1.57},
    {{.38,5.04},-1.42},
    {{-.07,1.93},1.57},
}};
}

ShipInterior::ShipInterior() {
    crew_={{
        {stations[0].position,DeckStation::Quarters,stations[0].yaw,1.66,0,0,{.89f,.52f,.28f}},
        {stations[1].position,DeckStation::Systems,stations[1].yaw,1.76,0,1.4,{.37f,.72f,.88f}},
        {stations[2].position,DeckStation::Cargo,stations[2].yaw,1.84,0,2.2,{.45f,.85f,.68f}},
    }};
    dwell_={2.2,5.4,7.0};
}

void ShipInterior::updateCrew(double seconds, DeckOperations operations) {
    constexpr std::array<std::array<DeckStation,3>,3> rounds{{
        {DeckStation::Navigation,DeckStation::Quarters,DeckStation::Cargo},
        {DeckStation::Systems,DeckStation::Navigation,DeckStation::Quarters},
        {DeckStation::Cargo,DeckStation::Systems,DeckStation::Quarters},
    }};
    for (size_t i=0;i<crew_.size();++i) {
        dwell_[i]-=seconds;
        crew_[i].taskPhase=std::fmod(crew_[i].taskPhase+seconds*(.91+.21*i),2*std::numbers::pi);
    }
    if (travelling_==crew_.size()) {
        for (size_t i=0;i<crew_.size();++i) {
            if (dwell_[i]>0)
                continue;
            const auto current=crew_[i].station;
            const auto priority=rounds[i][0];
            const bool onDuty=i==0?operations.courseActive:i==1?operations.damaged:operations.loaded;
            for (unsigned offset=0;offset<rounds[i].size();++offset) {
                const auto stop=(nextStop_[i]+offset)%rounds[i].size();
                const auto choice=rounds[i][stop];
                if (choice==current || std::any_of(crew_.begin(),crew_.end(),[&](const auto& member){
                        return member.station==choice;
                    }))
                    continue;
                // During an active assignment the next stop is the duty
                // station as soon as it is free. Off-duty rounds still visit it.
                if (onDuty && current!=priority && choice!=priority)
                    continue;
                const auto& from=stations[size_t(current)];
                const auto& to=stations[size_t(choice)];
                // Approach openings squarely from the aisle centre.
                waypoints_={glm::dvec2(0,from.position.y),glm::dvec2(0,to.position.y),to.position};
                waypoint_=0;
                destination_=choice;
                travelling_=i;
                nextStop_[i]=unsigned((stop+1)%rounds[i].size());
                break;
            }
            if (travelling_!=crew_.size())
                break;
        }
    }
    if (travelling_==crew_.size())
        return;

    auto& member=crew_[travelling_];
    const auto approachingPlayer=[&](glm::dvec2 point) {
        if (!walking_)
            return false;
        const auto distanceToPlayer=glm::length(point-position_);
        return distanceToPlayer<crewClearance+.04 &&
               distanceToPlayer<glm::length(member.position-position_);
    };
    double remaining=seconds*(.86+.046*travelling_);
    member.moving=false;
    while (remaining>0 && waypoint_<waypoints_.size()) {
        const auto delta=waypoints_[waypoint_]-member.position;
        const double distance=glm::length(delta);
        if (distance==0.) {
            ++waypoint_;
            continue;
        }
        auto direction=delta/distance;
        const double advance=std::min({remaining,distance,crewStep});
        auto next=advance==distance ? waypoints_[waypoint_] : member.position+direction*advance;
        if (approachingPlayer(next)) {
            // Yield in the passage. Wait in doorways until the opening is clear.
            if (waypoint_!=1 || member.position.y>=cargoFront-.55 || std::abs(direction.y)<.75)
                break;
            direction={position_.x<=0?1.:-1.,0};
            next=member.position+direction*advance;
            if (std::abs(next.x)>.28 || approachingPlayer(next))
                break;
        }
        // Check the middle as well as the end of each short step when the
        // clearance circle grazes a doorframe or a fixture corner.
        if (!crewCanStand((member.position+next)*.5,hatchOpening_) || !crewCanStand(next,hatchOpening_))
            break;
        const double heading=std::atan2(-direction.x,-direction.y);
        member.yaw+=std::remainder(heading-member.yaw,2*std::numbers::pi)*std::min(1.,seconds*11.);
        member.position=next;
        if (member.position==waypoints_[waypoint_])
            ++waypoint_;
        member.stride=std::fmod(member.stride+advance/.87,1.);
        member.moving=true;
        remaining-=advance;
    }
    if (waypoint_==waypoints_.size()) {
        member.station=destination_;
        member.yaw=stations[size_t(destination_)].yaw;
        member.moving=false;
        const bool work=travelling_==0?operations.courseActive:travelling_==1?operations.damaged:operations.loaded;
        dwell_[travelling_]=(work&&destination_==rounds[travelling_][0]?10.5:4.2)+travelling_*1.25;
        travelling_=crew_.size();
    }
}

bool ShipInterior::seatInReach() const {
    return walking_ && glm::length(position_-glm::dvec2(0,.24)) < .87 && position_.y < hatch-.23;
}

bool ShipInterior::cargoTerminalInReach() const {
    if (!walking_ || std::abs(position_.x) > .67 || position_.y < cargoScreenZ-1.55 ||
        position_.y >= cargoScreenZ)
        return false;
    const auto forward = orientation()*glm::dvec3(0,0,-1);
    if (forward.z <= 0.)
        return false;
    const double distance = (cargoScreenZ-position_.y)/forward.z;
    const auto hit = eye()+forward*distance;
    // Include the bezel and the panel immediately above the screen.
    return distance <= 1.55 && std::abs(hit.x) <= cargoScreenHalf+.045 &&
           hit.y >= cargoScreenTop-cargoScreenHeight-.045 && hit.y <= cargoScreenTop+.10;
}

void ShipInterior::setStation(std::optional<StationKey> key) {
    if (key ? station_ && station_->key==*key : !station_)
        return;
    if (station_ && position_.x < stationDeck::shipDoor+.20)
        position_ = {-1.12,stationDeck::centre};
    airlockSide_ = AirlockSide::Sealed;
    shipDoorTravel_ = bridgeDoorTravel_ = 0.;
    if (key) station_=generateStation(*key);
    else station_.reset();
}

bool ShipInterior::departureClear() const {
    return position_.x >= stationDeck::shipDoor && airlockSide_ == AirlockSide::Sealed &&
           shipDoorTravel_ == 0. && bridgeDoorTravel_ == 0.;
}

bool ShipInterior::aboardDockside() const {
    const auto* room=walking_&&station_?station_->roomAt(position_):nullptr;
    return room&&room->kind!=StationModuleKind::Airlock&&room->kind!=StationModuleKind::Bridge;
}

BoardingInteraction ShipInterior::boardingInteraction() const {
    if (!walking_)
        return BoardingInteraction::None;
    const auto forward = orientation()*glm::dvec3(0,0,-1);
    const auto faces = [&](double x,double half,double reach,double low,double high) {
        if (std::abs(forward.x)<.10)
            return false;
        const double distance = (x-position_.x)/forward.x;
        const auto hit = eye()+forward*distance;
        return distance>0. && distance<=reach && std::abs(hit.z-stationDeck::centre)<=half &&
               hit.y>=low && hit.y<=high;
    };
    const auto facesDoor = [&](double x,double opening){
        return std::abs(position_.x-x)>(opening>=stationDeck::doorClear?.21:.05) &&
               faces(x,stationDeck::doorHalf+.06,1.28,deckFloor,stationDeck::doorTop+.16);
    };
    if (facesDoor(stationDeck::shipDoor,shipDoorOpening()))
        return BoardingInteraction::ShipDoor;
    if (station_ && facesDoor(stationDeck::bridgeDoor,bridgeDoorOpening()))
        return BoardingInteraction::BridgeDoor;
    if (station_ && station_->roomAt(position_)==&station_->freightBay() &&
        faces(station_->freightPad.right,.78,1.72,-.96,1.10))
        return BoardingInteraction::Freight;
    return BoardingInteraction::None;
}

void ShipInterior::useAirlock(BoardingInteraction control) {
    if (!station_ || control != boardingInteraction())
        return;
    if (control == BoardingInteraction::ShipDoor) {
        const bool closingFromShip = position_.x > stationDeck::shipDoor && airlockSide_ == AirlockSide::Ship;
        airlockSide_ = closingFromShip ? AirlockSide::Sealed : AirlockSide::Ship;
    } else if (control == BoardingInteraction::BridgeDoor) {
        airlockSide_ = AirlockSide::Bridge;
    }
}

AirlockMotion ShipInterior::airlockMotion() const {
    if (!station_)
        return AirlockMotion::Holding;
    if (airlockSide_!=AirlockSide::Ship && shipDoorTravel_>0.)
        return AirlockMotion::SealingShip;
    if (airlockSide_!=AirlockSide::Bridge && bridgeDoorTravel_>0.)
        return AirlockMotion::SealingBridge;
    if (airlockSide_==AirlockSide::Ship && shipDoorTravel_<1.)
        return AirlockMotion::OpeningShip;
    if (airlockSide_==AirlockSide::Bridge && bridgeDoorTravel_<1.)
        return AirlockMotion::OpeningBridge;
    return AirlockMotion::Holding;
}

void ShipInterior::updateAirlock(double seconds) {
    double travel=seconds*2.15;
    // The second leaf only receives the time left over after the seal.
    const auto seal=[&](double x,double& position) {
        if (position==0. || travel==0.)
            return;
        const bool occupied=walking_ && std::abs(position_.x-x)<playerRadius+.056 &&
                            std::abs(position_.y-stationDeck::centre)<stationDeck::doorHalf+playerRadius;
        if (occupied) {
            travel=0.;
            return;
        }
        const double used=std::min(travel,position);
        position-=used;
        travel-=used;
    };
    if (airlockSide_!=AirlockSide::Ship)
        seal(stationDeck::shipDoor,shipDoorTravel_);
    if (airlockSide_!=AirlockSide::Bridge)
        seal(stationDeck::bridgeDoor,bridgeDoorTravel_);
    if (airlockSide_==AirlockSide::Ship && bridgeDoorTravel_==0.)
        shipDoorTravel_=std::min(1.,shipDoorTravel_+travel);
    if (airlockSide_==AirlockSide::Bridge && shipDoorTravel_==0.)
        bridgeDoorTravel_=std::min(1.,bridgeDoorTravel_+travel);
}

bool ShipInterior::useSeat() {
    if (walking_ && !seatInReach())
        return false;
    walking_ = !walking_;
    if (!walking_)
        airlockSide_ = AirlockSide::Sealed;
    position_ = walking_ ? glm::dvec2(.466,.37) : glm::dvec2(0);
    angles_ = walking_ ? glm::dvec2(2.68,.075) : glm::dvec2(0);
    return true;
}

void ShipInterior::look(glm::dvec2 mouseDelta) {
    if (!walking_)
        return;
    angles_ += mouseDelta * .0028;
    angles_.x = std::remainder(angles_.x,2*std::numbers::pi);
    angles_.y = std::clamp(angles_.y,-1.44,1.44);
}

void ShipInterior::step(glm::dvec2 input, double seconds, bool fast, DeckOperations operations) {
    if (seconds <= 0)
        return;
    const auto moveHatches=[&](double interval) {
        const bool near=walking_ && std::abs(position_.y-hatch)<.89;
        hatchOpening_=std::clamp(hatchOpening_+(near?interval:-interval)*4.6,0.,1.);
        updateAirlock(interval);
    };
    if (walking_ && input != glm::dvec2(0)) {
        input /= std::max(1.,glm::length(input));
        const double sinYaw=std::sin(angles_.x),cosYaw=std::cos(angles_.x);
        const glm::dvec2 right(cosYaw,sinYaw),forward(sinYaw,-cosYaw);
        const auto movement=(right*input.x+forward*input.y)*seconds*(fast?2.45:1.58);
        const unsigned samples=std::max(1u,unsigned(std::ceil(glm::length(movement)/.055)));
        const auto delta=movement/double(samples);
        const auto clear=[&](glm::dvec2 next) {
            if (!canStand(next,playerRadius,hatchOpening_,station(),shipDoorOpening(),bridgeDoorOpening()))
                return false;
            for (const auto& member : crew_) {
                const auto feet=member.position;
                const double distance=glm::length(next-feet);
                if (distance<crewClearance && distance<glm::length(position_-feet))
                    return false;
            }
            return true;
        };
        const double interval=seconds/double(samples);
        for (unsigned i=0;i<samples;++i) {
            moveHatches(interval);
            updateCrew(interval,operations);
            auto next=position_+glm::dvec2(delta.x,0);
            if (clear(next))
                position_=next;
            next=position_+glm::dvec2(0,delta.y);
            if (clear(next))
                position_=next;
        }
    } else {
        moveHatches(seconds);
        updateCrew(seconds,operations);
    }
}

glm::dquat ShipInterior::orientation() const {
    return glm::angleAxis(-angles_.x,glm::dvec3(0,1,0))*glm::angleAxis(-angles_.y,glm::dvec3(1,0,0));
}

glm::dvec3 ShipInterior::eye() const {
    const double height=std::lerp(.295,.405,smoothstep(1.12,1.68,position_.y));
    return walking_ ? glm::dvec3(position_.x,height,position_.y) : glm::dvec3(0);
}

namespace {
constexpr double seatTransitionSeconds = .36;
}

void SeatTransition::begin(const CockpitMotionPose& from, bool standing) {
    from_ = from;
    remaining_ = seatTransitionSeconds;
    standing_ = standing;
}

void SeatTransition::update(double seconds) {
    remaining_ = std::max(0.,remaining_-std::max(0.,seconds));
}

CockpitMotionPose SeatTransition::pose(const CockpitMotionPose& destination) const {
    if (!active())
        return destination;
    const double progress = 1.-remaining_/seatTransitionSeconds;
    const double turn = smoothstep(0.,1.,progress);
    auto result = destination;
    result.orientation = glm::normalize(glm::slerp(from_.orientation,destination.orientation,turn));
    result.angles = glm::mix(from_.angles,destination.angles,turn);
    // Rise before sliding past the armrest, or move across before lowering into the seat.
    const double slide = standing_ ? smoothstep(.18,1.,progress) : smoothstep(0.,.80,progress);
    const double rise = standing_ ? smoothstep(0.,.77,progress) : smoothstep(.23,1.,progress);
    result.translation = glm::mix(from_.translation,destination.translation,slide);
    result.translation.y = std::lerp(from_.translation.y,destination.translation.y,rise);
    return result;
}

std::string_view ShipInterior::room() const {
    using namespace shipDeck;
    if (position_.x < stationDeck::shipDoor && station_)
        if (const auto* module=station_->roomAt(position_))
            return station_->key.vessel&&module->kind==StationModuleKind::Freight?
                "Transfer bay":stationModuleName(module->kind);
    if (position_.y < hatch) return "Cockpit";
    if (position_.y >= cargoFront) return "Cargo bay";
    if (position_.x < -passageHalf) return "Crew quarters";
    if (position_.x > passageHalf) return "Systems alcove";
    return "Main passage";
}
} // namespace infinity
