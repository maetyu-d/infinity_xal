#pragma once
#include "Core.hpp"
#include <array>
#include <deque>

namespace infinity {
inline constexpr double shipTrailLifetime=17.;

struct ShipTrailPoint {
    Address reference;
    std::array<glm::dvec3,2> exhaust;
    double seconds;
    float intensity;
    bool begins=false;
};

class ShipTrail {
  public:
    void cut() { cut_=true; }

    void expire(double seconds) {
        while(!points_.empty()&&seconds-points_.front().seconds>=shipTrailLifetime)
            points_.pop_front();
    }

    // The newest point follows the nozzle every frame. Older knots are
    // committed at a fixed cadence, keeping the trail bounded at any FPS.
    void observe(ShipTrailPoint point) {
        expire(point.seconds);
        if(!points_.empty()&&point.seconds<points_.back().seconds){
            points_.clear();
            cut_=true;
        }
        if(points_.empty()||cut_||point.seconds-points_.back().seconds>.55){
            point.begins=true;
            points_.push_back(point);
            committed_=point.seconds;
            cut_=false;
            return;
        }
        if(points_.back().seconds==committed_) {
            if(point.seconds>committed_)points_.push_back(point);
        } else points_.back()=point;
        if(point.seconds-committed_>=.16)committed_=point.seconds;
    }

    const std::deque<ShipTrailPoint>& points() const {return points_;}
    double nozzleWidth=1.; // Metres before the trail expands.

  private:
    std::deque<ShipTrailPoint> points_;
    double committed_=0.;
    bool cut_=true;
};
} // namespace infinity
