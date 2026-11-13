#pragma once
#include "infinity/Cockpit.hpp"

namespace infinity {
double atlasTextWidth(std::string_view, double size);
void atlasText(CockpitOverlay&, std::string_view, double x, double y, double width, double size,
               glm::vec3 colour);
}
