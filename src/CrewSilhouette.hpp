#pragma once
#include <cstdint>
#include <vector>

namespace infinity {
inline constexpr unsigned crewSpriteWidth=144, crewSpriteHeight=256;
inline constexpr unsigned crewSpriteDirections=8, crewSpriteFrames=12;
inline constexpr double crewSpriteTexelsPerMetre=122., crewSpriteGroundMargin=14.;
std::vector<uint8_t> crewSilhouettePixels();
}
