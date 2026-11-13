#include "infinity/ShipInterior.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinity {
namespace {
using namespace shipDeck;
constexpr double playerRadius = .127;
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
    DeckArea{cargoDoorHalf,cargoFront-.08,cargoHalf,cargoFront+.08}
};

bool collides(glm::dvec2 point, const DeckArea& area) {
    const auto nearest = glm::clamp(point,glm::dvec2(area.left,area.front),glm::dvec2(area.right,area.back));
    return glm::length(point-nearest) < playerRadius;
}

bool canStand(glm::dvec2 point) {
    constexpr double r = playerRadius, diagonal = r*.707106781;
    constexpr std::array<glm::dvec2,8> boundary{{{-r,0},{r,0},{0,-r},{0,r},
        {-diagonal,-diagonal},{diagonal,-diagonal},{diagonal,diagonal},{-diagonal,diagonal}}};
    for (const auto offset : boundary)
        if (std::none_of(walkable.begin(),walkable.end(),[&](const auto& area){return area.contains(point+offset);}))
            return false;
    return std::none_of(walls.begin(),walls.end(),[&](const auto& area){return collides(point,area);}) &&
           std::none_of(cargoCrates.begin(),cargoCrates.end(),[&](const auto& area){return collides(point,area);});
}
}

std::array<DeckCrewPose,3> shipDeckCrew(double seconds) {
    using namespace shipDeck;
    // Keep the crew at their work areas. The cargo specialist works off
    // the centreline, leaving a clear path between the hatch and aft wall.
    return {{
        {{-1.15,deckFloor,3.13}, .23+.14*std::sin(seconds*.59), 1.65,
          std::sin(seconds*1.37), {.77f,.46f,.24f}},
        {{1.13,deckFloor,3.03}, -1.53+.10*std::sin(seconds*.73), 1.77,
          std::sin(seconds*1.84+.9), {.30f,.66f,.78f}},
        {{.75,deckFloor,6.43+.085*std::sin(seconds*.54)}, -2.18+.17*std::sin(seconds*.51), 1.83,
          std::sin(seconds*1.52+1.7), {.40f,.78f,.65f}}
    }};
}

bool ShipInterior::seatInReach() const {
    return walking_ && glm::length(position_-glm::dvec2(0,.24)) < .87 && position_.y < .93;
}

bool ShipInterior::useSeat() {
    if (walking_ && !seatInReach())
        return false;
    walking_ = !walking_;
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

void ShipInterior::step(glm::dvec2 input, double seconds, bool fast, double flightSeconds) {
    if (seconds <= 0)
        return;
    const auto moveHatch=[&](double interval) {
        const bool near=walking_ && std::abs(position_.y-hatch)<.89;
        hatchOpening_=std::clamp(hatchOpening_+(near?interval:-interval)*4.6,0.,1.);
    };
    if (walking_ && input != glm::dvec2(0)) {
        input /= std::max(1.,glm::length(input));
        const double sinYaw=std::sin(angles_.x),cosYaw=std::cos(angles_.x);
        const glm::dvec2 right(cosYaw,sinYaw),forward(sinYaw,-cosYaw);
        const auto movement=(right*input.x+forward*input.y)*seconds*(fast?2.45:1.58);
        const unsigned samples=std::max(1u,unsigned(std::ceil(glm::length(movement)/.055)));
        const auto delta=movement/double(samples);
        const auto crew=shipDeckCrew(flightSeconds);
        const auto clear=[&](glm::dvec2 next) {
            if (std::abs(next.y-hatch)<.19 &&
                std::abs(next.x)+playerRadius>hatchHalf*smoothstep(0.,1.,hatchOpening_))
                return false;
            constexpr double crewClearance=.36;
            for (const auto& member : crew) {
                const glm::dvec2 feet(member.feet.x,member.feet.z);
                const double distance=glm::length(next-feet);
                if (distance<crewClearance && distance<glm::length(position_-feet))
                    return false;
            }
            return canStand(next);
        };
        for (unsigned i=0;i<samples;++i) {
            moveHatch(seconds/double(samples));
            auto next=position_+glm::dvec2(delta.x,0);
            if (clear(next))
                position_=next;
            next=position_+glm::dvec2(0,delta.y);
            if (clear(next))
                position_=next;
        }
    } else
        moveHatch(seconds);
}

glm::dquat ShipInterior::orientation() const {
    return glm::angleAxis(-angles_.x,glm::dvec3(0,1,0))*glm::angleAxis(-angles_.y,glm::dvec3(1,0,0));
}

glm::dvec3 ShipInterior::eye() const {
    const double height=std::lerp(.295,.405,smoothstep(1.12,1.68,position_.y));
    return walking_ ? glm::dvec3(position_.x,height,position_.y) : glm::dvec3(0);
}

std::string_view ShipInterior::room() const {
    using namespace shipDeck;
    if (position_.y < hatch) return "Cockpit";
    if (position_.y >= cargoFront) return "Cargo bay";
    if (position_.x < -passageHalf) return "Crew quarters";
    if (position_.x > passageHalf) return "Systems alcove";
    return "Main passage";
}
} // namespace infinity
