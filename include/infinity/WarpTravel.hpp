#pragma once
#include "Core.hpp"

namespace infinity {
// Read-only optics and estimates. These never move a body or change a route.
double warpExitEstimate(double range, double dropRange, double closingSpeed, double speedLimit,
                        double clearanceOffset = -1);
double warpDestinationReveal(double range, double dropRange, double bodyRadius);
double warpTunnelAperture(double reveal, double angularRadius, double verticalFov);

struct WarpOpticsSample {
    double closure = 0, light = 0, bore = .21;
};

// Carries intensity and its rate through engagement, cancellation, and retargeting.
class WarpOptics {
  public:
    WarpOpticsSample update(bool engaged, bool guided, double speed, double bore, double seconds);

  private:
    struct Track {
        double value = 0, velocity = 0;
        void approach(double target,double response,double seconds);
    };
    Track closure_, light_, bore_{.21};
    double boreTarget_=.21;
};
} // namespace infinity
