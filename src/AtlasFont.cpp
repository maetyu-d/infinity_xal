#include "AtlasFont.hpp"
#include "AtlasFontData.hpp"

namespace infinity {
namespace {
const atlasFontData::Glyph& glyph(unsigned char c) {
    return atlasFontData::glyphs[(c >= 32 && c < 127 ? c : '?') - 32];
}
bool continuation(unsigned char c) { return (c & 0xc0) == 0x80; }
double advance(unsigned char c, double size) { return glyph(c).advance64 * size / (64 * atlasFontData::em); }
}
double atlasTextWidth(std::string_view label, double size) {
    double width = 0;
    for (unsigned char c : label)
        if (!continuation(c))
            width += advance(c, size);
    return width;
}
void atlasText(CockpitOverlay& overlay, std::string_view label, double x, double y, double width,
               double size, glm::vec3 colour) {
    std::string fitted;
    if (atlasTextWidth(label, size) > width) {
        const double available = width - atlasTextWidth("...", size);
        double used = 0;
        for (unsigned char c : label) {
            if (continuation(c))
                continue;
            used += advance(c, size);
            if (used > available)
                break;
            fitted += c < 127 ? char(c) : '?';
        }
        if (fitted.empty())
            return;
        fitted += "...";
        label = fitted;
    }
    if (label.empty())
        return;
    const float scale = float(size / atlasFontData::em);
    const float baseline = float(y + size * .79);
    const auto tint = rgba(colour);
    overlay.textRuns.push_back({{x, y, atlasTextWidth(label, size), size}, 0, 0, 0, 0, std::string(label)});
    for (unsigned char c : label) {
        if (continuation(c))
            continue;
        const auto& g = glyph(c);
        if (g.w) {
            const float left = float(x) + g.left * scale;
            const float top = baseline + g.top * scale;
            overlay.atlasGlyphs.push_back({left, top, left + g.w * scale, top + g.h * scale,
                float(g.x) / atlasFontData::width, float(g.y) / atlasFontData::height,
                float(g.x + g.w) / atlasFontData::width, float(g.y + g.h) / atlasFontData::height, tint});
        }
        x += advance(c, size);
    }
}
} // namespace infinity
