#include "infinity/Cockpit.hpp"
#include "infinity/Xalaria.hpp"
#include "infinity/XalariaEconomy.hpp"
#include "infinity/CrewMinimap.hpp"
#include "infinity/MicroView.hpp"
#include "infinity/CockpitContext.hpp"
#include "infinity/Economy.hpp"
#include "infinity/ExpeditionPlanner.hpp"
#include "infinity/OrbitalDebris.hpp"
#include "infinity/SurfaceRecon.hpp"
#include "infinity/VesselSystems.hpp"
#include "infinity/Discovery.hpp"
#include "infinity/ShipSignals.hpp"
#include "infinity/Targeting.hpp"
#include "infinity/CombatAwareness.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <map>
#include <numbers>

namespace infinity {
namespace {
const glm::vec3 ink{.018, .035, .052}, muted{.43, .60, .66}, ice{.49, .85, .93}, mint{.44, .93, .74},
    amber{1, .65, .28}, pearl{.84, .92, .91}, trafficInk{.90, .61, .76};
const glm::vec3 danger{1,.30,.22};
void triangle(Mesh& m, glm::dvec3 a, glm::dvec3 b, glm::dvec3 c, glm::vec3 color, float alpha = 1) {
    const auto first = uint32_t(m.vertices.size());
    for (auto p : {a, b, c})
        m.vertices.push_back({float(p.x), float(p.y), float(p.z), rgba(color, alpha)});
    m.indices.insert(m.indices.end(), {first, first + 1, first + 2});
}
void rect(Mesh& m, double x, double y, double w, double h, glm::vec3 c, float alpha = 1) {
    triangle(m, {x, y, 0}, {x + w, y, 0}, {x, y + h, 0}, c, alpha);
    triangle(m, {x + w, y, 0}, {x + w, y + h, 0}, {x, y + h, 0}, c, alpha);
}
void line(Mesh& m, double x, double y, double a, double b, glm::vec3 c) {
    addLine(m, {x, y, 0}, {a, b, 0}, c);
}
// Small native-resolution instrument type. No font library or atlas allocation.
const std::map<char, std::string_view> glyphs{
    {'A', "01110100011000111111100011000110001"}, {'B', "11110100011000111110100011000111110"},
    {'C', "01111100001000010000100001000001111"}, {'D', "11110100011000110001100011000111110"},
    {'E', "11111100001000011110100001000011111"}, {'F', "11111100001000011110100001000010000"},
    {'G', "01111100001000010111100011000101111"}, {'H', "10001100011000111111100011000110001"},
    {'I', "11111001000010000100001000010011111"}, {'J', "00111000100001000010000101001001100"},
    {'K', "10001100101010011000101001001010001"}, {'L', "10000100001000010000100001000011111"},
    {'M', "10001110111010110101100011000110001"}, {'N', "10001110011010110011100011000110001"},
    {'O', "01110100011000110001100011000101110"}, {'P', "11110100011000111110100001000010000"},
    {'Q', "01110100011000110001101011001001101"}, {'R', "11110100011000111110101001001010001"},
    {'S', "01111100001000001110000010000111110"}, {'T', "11111001000010000100001000010000100"},
    {'U', "10001100011000110001100011000101110"}, {'V', "10001100011000110001100010101000100"},
    {'W', "10001100011000110101101011101110001"}, {'X', "10001100010101000100010101000110001"},
    {'Y', "10001100010101000100001000010000100"}, {'Z', "11111000010001000100010001000011111"},
    {'0', "01110100011001110101110011000101110"}, {'1', "00100011000010000100001000010001110"},
    {'2', "01110100010000100010001000100011111"}, {'3', "11110000010000101110000010000111110"},
    {'4', "00010001100101010010111110001000010"}, {'5', "11111100001000011110000010000111110"},
    {'6', "01110100001000011110100011000101110"}, {'7', "11111000010001000100010000100001000"},
    {'8', "01110100011000101110100011000101110"}, {'9', "01110100011000101111000010000101110"},
    {'/', "00001000010001000100010001000010000"}, {'-', "00000000000000011111000000000000000"},
    {'+', "00000001000010011111001000010000000"}, {',', "00000000000000000000001100010001000"},
    {'>', "10000010000010000010001000100010000"}, {'!', "00100001000010000100001000000000100"},
    {'.', "00000000000000000000000000011000110"}, {':', "00000011000110000000011000110000000"},
    {'%', "11001110100001000100010001001100011"}, {'m', "00000000001101010101101011010110101"},
    {'k', "10000100001001010100110001010010010"}, {'n', "00000000001111010001100011000110001"},
    {'p', "00000000001111010001111101000010000"}, {'u', "00000000001000110001100011001101101"},
    {'s', "00000000000111110000011100000111110"}, {'e', "00000000000111010001111111000001111"}};
void text(CockpitOverlay& h, std::string_view s, double x, double y, double size, glm::vec3 color,
          size_t maxChars = 100, bool literal = false) {
    auto& m=h.lettering;
    const auto firstVertex=m.vertices.size(),firstIndex=m.indices.size();
    const double firstX=x;
    const double pixel = size / 7.;
    size_t count = 0;
    for (char c : s) {
        if (count++ >= maxChars)
            break;
        if (!literal && c >= 'a' && c <= 'z')
            c = char(c - 'a' + 'A');
        if (auto it = glyphs.find(c); it != glyphs.end())
            for (int row = 0; row < 7; row++)
                for (int col = 0; col < 5;) {
                    if (it->second[size_t(row * 5 + col)] != '1') {
                        ++col;
                        continue;
                    }
                    const int start = col++;
                    while (col < 5 && it->second[size_t(row * 5 + col)] == '1')
                        ++col;
                    rect(m, x + start * pixel, y + row * pixel, (col - start) * pixel, pixel, color);
                }
        x += pixel * 6;
    }
    if(m.vertices.size()!=firstVertex){
        const auto length=std::min(maxChars,s.size());
        h.textRuns.push_back({{firstX,y,std::max(0.,length*pixel*6-pixel),size},firstVertex,
            m.vertices.size()-firstVertex,firstIndex,m.indices.size()-firstIndex,std::string(s.substr(0,length))});
    }
}
std::string elide(std::string_view value, double width, double size) {
    const size_t count = size_t(std::max(0., std::floor(width / (size * 6 / 7))));
    if (value.size() <= count)
        return std::string(value);
    return count > 3 ? std::string(value.substr(0, count - 3)) + "..." : std::string{};
}
void fitted(CockpitOverlay& h, std::string_view s, double x, double y, double width, double size, glm::vec3 color,
            bool literal = false) {
    text(h, elide(s, width, size), x, y, size, color, 200, literal);
}
void occlude(CockpitOverlay& h,CockpitRect panel) {
    std::erase_if(h.contactHits,[&](const auto& hit){return hit.bounds.overlaps(panel);});
    // Panels and text are submitted in separate batches. Remove covered
    // underlay runs before adding modal content, including whole words at edges.
    auto& mesh=h.lettering;size_t vertices=0,indices=0,runs=0;
    for(auto run:h.textRuns){
        if(run.bounds.overlaps(panel))continue;
        for(size_t i=0;i<run.vertexCount;++i)mesh.vertices[vertices+i]=mesh.vertices[run.firstVertex+i];
        for(size_t i=0;i<run.indexCount;++i)mesh.indices[indices+i]=uint32_t(mesh.indices[run.firstIndex+i]-run.firstVertex+vertices);
        run.firstVertex=vertices;run.firstIndex=indices;vertices+=run.vertexCount;indices+=run.indexCount;
        h.textRuns[runs++]=std::move(run);
    }
    mesh.vertices.resize(vertices);mesh.indices.resize(indices);h.textRuns.resize(runs);
    auto& strokes=h.strokes;vertices=indices=0;
    for(size_t i=0;i+1<strokes.indices.size();i+=2){
        const auto a=strokes.vertices[strokes.indices[i]],b=strokes.vertices[strokes.indices[i+1]];
        const CockpitRect bounds{std::min(a.x,b.x),std::min(a.y,b.y),std::max(.01,double(std::abs(a.x-b.x))),std::max(.01,double(std::abs(a.y-b.y)))};
        if(bounds.overlaps(panel))continue;
        strokes.vertices[vertices]=a;strokes.vertices[vertices+1]=b;
        strokes.indices[indices++]=uint32_t(vertices++);strokes.indices[indices++]=uint32_t(vertices++);
    }
    strokes.vertices.resize(vertices);strokes.indices.resize(indices);
}
std::vector<std::string> wrapped(std::string_view message,double width,double size,size_t maximum=3) {
    std::vector<std::string> lines;
    const auto columns=size_t(std::max(1.,std::floor(width/(size*6/7))));
    while(!message.empty()&&lines.size()<maximum){
        while(!message.empty()&&message.front()==' ')message.remove_prefix(1);
        if(message.empty())break;
        if(lines.size()+1==maximum){lines.push_back(elide(message,width,size));break;}
        auto end=std::min(columns,message.size());
        if(end<message.size())if(const auto space=message.substr(0,end).find_last_of(' ');space!=std::string_view::npos&&space>columns/3)end=space;
        lines.emplace_back(message.substr(0,end));message.remove_prefix(end);
    }
    return lines;
}
void card(CockpitOverlay&,CockpitRect,glm::vec3);
void card(CockpitOverlay& h, CockpitRect r, glm::vec3 tint) {
    h.hitRegions.push_back(r);
    rect(h.panels, r.x, r.y, r.w, r.h, ink, .94f);
    rect(h.panels, r.x, r.y, 2, r.h, tint * .55f);
    line(h.strokes, r.x + 2, r.y, r.x + r.w, r.y, tint * .38f);
    line(h.strokes, r.x + 2, r.y + r.h, r.x + r.w, r.y + r.h, tint * .15f);
}
void diamond(Mesh& m, glm::dvec2 p, double radius, glm::vec3 color) {
    for (int i = 0; i < 4; ++i) {
        const double a = i * 1.570796326795, b = (i + 1) * 1.570796326795;
        line(m, p.x + std::cos(a) * radius, p.y + std::sin(a) * radius, p.x + std::cos(b) * radius,
             p.y + std::sin(b) * radius, color);
    }
}
void bar(Mesh& m, double x, double y, double w, double value, glm::vec3 color) {
    rect(m, x, y, w, 3, muted * .22f);
    rect(m, x, y, w * std::clamp(value, 0., 1.), 3, color);
}
} // namespace
std::string flightQuantity(double v, bool speed) {
    char b[64];
    const double a = std::abs(v);
    double divisor = 1;
    const char* suffix = "m";
    if (a >= 9.4607304725808e15) {
        divisor = 9.4607304725808e15;
        suffix = "LY";
    } else if (a >= 1.495978707e10) {
        divisor = 1.495978707e11;
        suffix = "AU";
    } else if (a >= 1000) {
        divisor = 1000;
        suffix = "km";
    } else if (a > 0 && a < 1e-9) {
        divisor = 1e-12;
        suffix = "pm";
    } else if (a > 0 && a < 1e-6) {
        divisor = 1e-9;
        suffix = "nm";
    } else if (a > 0 && a < .001) {
        divisor = 1e-6;
        suffix = "um";
    } else if (a > 0 && a < 1) {
        divisor = .001;
        suffix = "mm";
    }
    const double n = v / divisor;
    if (std::abs(n) > 99999)
        std::snprintf(b, sizeof(b), "%.2e %s%s", n, suffix, speed ? "/s" : "");
    else
        std::snprintf(b, sizeof(b), std::abs(n) < 100 ? "%.1f %s%s" : "%.0f %s%s", n, suffix,
                      speed ? "/s" : "");
    return b;
}
bool CockpitRect::contains(glm::dvec2 p) const {
    return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
}
bool CockpitRect::overlaps(const CockpitRect& r) const {
    return x < r.x + r.w && x + w > r.x && y < r.y + r.h && y + h > r.y;
}
std::vector<std::pair<size_t,size_t>> cockpitTextConflicts(const CockpitOverlay& h) {
    std::vector<std::pair<size_t,size_t>> result;
    for(size_t i=0;i<h.textRuns.size();++i)
        for(size_t j=0;j<i;++j)
            if(h.textRuns[i].bounds.overlaps(h.textRuns[j].bounds))result.emplace_back(j,i);
    return result;
}
void maskCockpitMarkers(Mesh& markers, const CockpitViewport& viewport, const CockpitOverlay& h) {
    size_t vertices = 0, indices = 0;
    for (size_t i = 0; i + 1 < markers.indices.size(); i += 2) {
        const auto a = markers.vertices[markers.indices[i]], b = markers.vertices[markers.indices[i + 1]];
        const auto from = viewport.toCanvas({a.x, a.y}), to = viewport.toCanvas({b.x, b.y});
        const CockpitRect bounds{std::min(from.x, to.x) - 2, std::min(from.y, to.y) - 2,
                                 std::abs(from.x - to.x) + 4, std::abs(from.y - to.y) + 4};
        if (std::any_of(h.hitRegions.begin(), h.hitRegions.end(), [&](auto r) { return bounds.overlaps(r); }) ||
            std::any_of(h.worldLabels.begin(), h.worldLabels.end(), [&](auto r) { return bounds.overlaps(r); }) ||
            std::any_of(h.textRuns.begin(), h.textRuns.end(), [&](const auto& r) { return bounds.overlaps(r.bounds); }))
            continue;
        markers.vertices[vertices] = a;
        markers.vertices[vertices + 1] = b;
        markers.indices[indices++] = uint32_t(vertices++);
        markers.indices[indices++] = uint32_t(vertices++);
    }
    markers.vertices.resize(vertices);
    markers.indices.resize(indices);
}
CockpitViewport::CockpitViewport(int w, int h, CockpitRect safe) {
    w = std::max(1, w);
    h = std::max(1, h);
    const double halfW =
        std::min(w * .5 - std::max(0., safe.x), std::min(double(w), safe.x + safe.w) - w * .5);
    const double halfH =
        std::min(h * .5 - std::max(0., safe.y), std::min(double(h), safe.y + safe.h) - h * .5);
    width = std::clamp(int(std::floor(halfW * 2)), 1, w);
    height = std::clamp(int(std::floor(halfH * 2)), 1, h);
    if ((w - width) % 2 && width > 1)
        --width;
    // The 900-unit design needs at least a square canvas. Fit uniformly in
    // tall windows rather than creating negative instrument/label widths.
    height = std::min(height, width);
    if ((h - height) % 2 && height > 1)
        --height;
    x = (w - width) / 2;
    y = (h - height) / 2;
    projectionScale = double(h) / height;
}
glm::dvec2 CockpitViewport::toCanvas(glm::dvec2 p) const {
    return (p - glm::dvec2(x, y)) * (900. / height);
}
glm::dvec2 CockpitViewport::toPixels(glm::dvec2 p) const {
    return p * (height / 900.) + glm::dvec2(x, y);
}
CockpitLayout::CockpitLayout(double aspect)
    : width(900 * aspect), traffic{24, 176, 274, 263}, celestial{width - 310, 176, 286, 331},
      life{width - 310, 550, 286, 48}, navigation{24, 758, std::clamp((width - 48) * .31, 240., 430.), 124},
      flight{}, scope{}, systems{width - 24 - std::clamp((width - 48) * .22, 185., 290.), 758,
                                 std::clamp((width - 48) * .22, 185., 290.), 124},
      windscreen{traffic.x + traffic.w + 18, 166, celestial.x - traffic.x - traffic.w - 36, 472} {
    const double mid = systems.x - navigation.x - navigation.w - 36;
    flight = {navigation.x + navigation.w + 12, 758, mid * .48, 124};
    scope = {flight.x + flight.w + 12, 758, mid * .52, 124};
}
CrewPanelLayout::CrewPanelLayout(double aspect) {
    const double width = 900 * aspect, pw = std::min(960., width - 48), map = pw * .35;
    panel = {(width - pw) * .5, 168, pw, 578};
    for (size_t i = 0; i < crewRoomCount; ++i) {
        auto p = crewRoomPosition(CrewStation(i));
        if (i >= 5)
            p.x = i == 5 ? -2 : 2;
        rooms[i] = {panel.x + map * .5 + p.x * map * .105 - map * .2, panel.y + 154 + p.y * 23, map * .4, 56};
    }
    for (size_t i = 0; i < maxShipCrew; ++i)
        roster[i] = {panel.x + map + 12, panel.y + 66 + i * 31, pw - map - 28, 29};
}
CrewFlightLayout::CrewFlightLayout(double aspect) {
    const double width = 900 * aspect;
    panel = {24, 176, std::clamp(width * .5 - 64, 160., 316.), 570};
    automatic = {panel.x + panel.w - 72, panel.y + 8, 60, 18};
    weapon = {std::max(width * .5 - 158, panel.x + panel.w + 12), 665, 316, 80};
    const double noticeWidth = std::clamp(width - 656, 24., 650.);
    notice = {(width - noticeWidth) * .5, 126, noticeWidth, 29};
    const auto col = (panel.w - 30) * .5;
    for (size_t i = 0; i < maxShipCrew; ++i)
        roster[i] = {panel.x + 10, panel.y + 68 + i * 27, panel.w - 20, 25};
    for (size_t i = 0; i < crewRoomCount; ++i)
        rooms[i] = {panel.x + 10 + (i % 2) * (col + 10), panel.y + 321 + (i / 2) * 22, col, 20};
    for (size_t i = 0; i < actions.size(); ++i)
        actions[i] = {panel.x + 10 + (i % 2) * (col + 10), panel.y + 416 + (i / 2) * 26, col, 24};
    const auto gunWidth = (panel.w - 40) / maxWeaponMounts;
    for (unsigned i = 0; i < maxWeaponMounts; ++i)
        guns[i] = {panel.x + 10 + i * (gunWidth + 10), panel.y + 286, gunWidth, 18};
}
CockpitRect weaponStationCard(double aspect, bool flightConsole) {
    const auto weapon =
        flightConsole ? CrewFlightLayout(aspect).weapon : CockpitRect{450 * aspect - 158, 665, 316, 80};
    return {weapon.x, 603, 316, 50};
}
PlannerLayout::PlannerLayout(double aspect,bool expanded,const ExpeditionPlan* view) {
    const double w=900*aspect,pw=std::min(1040.,w-48);
    panel={(w-pw)*.5,112,pw,expanded?634.:576.};
    const double lw=(pw-60)*.43;
    left={panel.x+18,180,lw,438};right={left.x+lw+24,180,pw-60-lw,438};
    for(unsigned i=0;i<offers.size();++i)offers[i]={left.x,181+i*25.,left.w,23};
    for(unsigned i=0;i<minus.size();++i){minus[i]={right.x+right.w-58,182+i*28.,24,23};plus[i]={right.x+right.w-24,182+i*28.,24,23};}
    section={left.x,expanded?332.:267.,left.w,expanded?174.:239.};composition={left.x,512,left.w,7};
    route={panel.x+18,574,pw-36,54};
    const double slot=(right.w-16)/3;
    for(unsigned i=0;i<bays.size();++i)bays[i]={right.x+(i==0?1:i==1?0:2)*(slot+8),389,slot,79};
    for(unsigned i=0;i<steps.size();++i)steps[i]={route.x+i*route.w/10,route.y,route.w/10,route.h};
    const double bw=(pw-64)/3;
    for(unsigned i=0;i<actions.size();++i)actions[i]={panel.x+18+(i%3)*(bw+14),638+(i/3)*40.,bw,28};
    details={panel.x+pw-130,154,112,22};close={panel.x+pw-106,expanded?710.:670.,88,16};
    if(!expanded){
        const double compactWidth=std::min(660.,w-64);
        panel={(w-compactWidth)*.5,250,compactWidth,318};
        left={panel.x+24,panel.y+48,panel.w-48,148};right=left;
        section=composition={0,0,0,0};bays.fill({0,0,0,0});minus.fill({0,0,0,0});plus.fill({0,0,0,0});
        offers.fill({0,0,0,0});offers[0]={left.x,panel.y+45,left.w,47};
        const auto summary=view?summarizePlan(*view):PlannerSummary{};
        const auto lines=view?std::max(size_t(1),wrapped(summary.instruction,left.w,10,3).size()):size_t(3);
        body={left.x,panel.y+105,left.w,double(lines)*13};
        double cursor=body.y+body.h+14;
        debrief={left.x,cursor,left.w,0};
        if(view&&view->debrief){debrief.h=view->accounting.recorded?58:24;cursor+=debrief.h+8;}
        status={left.x,cursor,left.w,9};
        if(view&&(!summary.context.empty()||(view->visual.shipMetres<.012&&view->stage!=PlanStage::Loading)))cursor+=20;
        receipt={left.x,cursor,left.w,0};
        if(view&&(view->busy||(!view->message.empty()&&!(view->debrief&&view->message.starts_with("CAVERN ASSAY COMPLETE"))))){const auto rows=wrapped(view->busy?"AWAITING SAVED RECEIPT":view->message,left.w,8.5,2).size();
            receipt.h=rows*12;cursor+=receipt.h+14;}
        route={left.x,cursor,left.w,4};cursor+=22;
        for(unsigned i=0;i<steps.size();++i)steps[i]={route.x+i*route.w/10,route.y,route.w/10-4,route.h};
        actions.fill({0,0,0,0});actions[0]={left.x,cursor,left.w-126,34};
        details={panel.x+panel.w-138,cursor,114,34};cursor+=50;
        footerY=cursor;close={panel.x+panel.w-108,footerY-3,84,16};panel.h=footerY-panel.y+22;
    }
}
namespace {
std::optional<size_t> contactRow(glm::dvec2 p, CockpitRect r, size_t count, size_t capacity) {
    if (!r.contains(p) || p.y < 205 || p.y >= 205 + 34 * std::min(count, capacity))
        return {};
    return size_t((p.y - 205) / 34);
}
} // namespace
std::optional<size_t> CockpitLayout::trafficRow(glm::dvec2 p, size_t count) const {
    return contactRow(p, traffic, count, 6);
}
std::optional<size_t> CockpitLayout::celestialRow(glm::dvec2 p, size_t count) const {
    return contactRow(p, celestial, count, 8);
}
std::vector<size_t> cockpitActivityRows(std::span<const ActivityContact> contacts) {
    std::vector<size_t> rows;
    for (size_t i = 0; i < contacts.size() && rows.size() < 6; ++i)
        if (!isOrbitalDebris(contacts[i].body.id) || contacts[i].selected)
            rows.push_back(i);
    for (size_t i = 0; i < contacts.size(); ++i)
        if (contacts[i].selected && std::find(rows.begin(), rows.end(), i) == rows.end()) {
            rows.back() = i;
            break;
        }
    // Keep a known port readable among nearby freighters, while K cycling and
    // the contact vector retain their original order and identities.
    std::optional<size_t> nearest;
    for (size_t i = 0; i < contacts.size(); ++i)
        if ((contacts[i].body.kind == ActivityKind::Harbour ||
             contacts[i].body.kind == ActivityKind::SurfacePort) &&
            !contacts[i].body.economicBrief.empty() &&
            (!nearest || contacts[i].distance < contacts[*nearest].distance))
            nearest = i;
    if (nearest && rows.size() > 1 && std::find(rows.begin(), rows.end(), *nearest) == rows.end()) {
        auto slot = rows.size() - 1;
        if (contacts[rows[slot]].selected)
            --slot;
        rows[slot] = *nearest;
    }
    return rows;
}
std::vector<size_t> cockpitCelestialRows(std::span<const CelestialContact> contacts) {
    std::vector<size_t> rows;
    for (unsigned priority = 0; priority < 3; priority++)
        for (size_t i = 0; i < contacts.size() && rows.size() < 8; i++) {
            const auto& c = contacts[i];
            const unsigned rank = c.selected ? 0 : hasSurfaceLife(c) ? 1 : 2;
            if (rank == priority)
                rows.push_back(i);
        }
    return rows;
}
std::optional<Address> nextLivingWorld(std::span<const CelestialContact> contacts,
                                       const std::optional<Address>& current) {
    std::vector<Address> worlds;
    for (const auto& c : contacts)
        if (hasSurfaceLife(c))
            worlds.push_back(c.address);
    if (worlds.empty())
        return {};
    std::sort(worlds.begin(), worlds.end());
    const auto next = current ? std::upper_bound(worlds.begin(), worlds.end(), *current) : worlds.begin();
    return next == worlds.end() ? worlds.front() : *next;
}
CockpitTargetMarker cockpitTargetMarker(glm::dvec3 d, double aspect, double projectionScale) {
    CockpitLayout l(aspect);
    const glm::dvec2 center{l.width * .5, 450};
    CockpitTargetMarker result;
    result.point = center;
    const double length = glm::length(d);
    if (!std::isfinite(length) || length < 1e-30)
        return result;
    d /= length;
    result.aft = d.z >= 0;
    glm::dvec2 offset{d.x, -d.y};
    if (!result.aft) {
        const auto projected = center + offset * (450 / .7002075 / -d.z * projectionScale);
        if (l.windscreen.contains(projected)) {
            result.point = projected;
            return result;
        }
    }
    result.offscreen = true;
    if (glm::length(offset) < 1e-12)
        offset = {0, 1};
    offset = glm::normalize(offset);
    result.direction = offset;
    const double limitX =
        (offset.x < 0 ? center.x - l.windscreen.x : l.windscreen.x + l.windscreen.w - center.x) - 12;
    const double limitY =
        (offset.y < 0 ? center.y - l.windscreen.y : l.windscreen.y + l.windscreen.h - center.y) - 12;
    const double distance =
        std::min(limitX / std::max(1e-12, std::abs(offset.x)), limitY / std::max(1e-12, std::abs(offset.y)));
    result.point = center + offset * distance;
    return result;
}
CockpitRect cockpitContactsSummary(){return {24,144,274,120};}
CockpitRect cockpitLifeBadge(double aspect,bool expanded){return expanded?CockpitRect{900*aspect-106,186,68,18}:CockpitRect{196,153,86,18};}
static std::vector<std::pair<std::string,glm::vec3>> computerDetails(const std::vector<CockpitAlert>& alerts,const FlightInstruments& t,const CrewView* crew,double detailWidth){
    std::vector<std::pair<std::string,glm::vec3>> details;
    auto detailOrder=alerts;
    std::stable_partition(detailOrder.begin(),detailOrder.end(),[](const auto& a){return a.key=="notice";});
    for(const auto& a:detailOrder){
        details.emplace_back(a.title,a.level==CockpitAlertLevel::Critical?danger:a.level==CockpitAlertLevel::Warning?amber:ice);
        const auto content=a.detail.empty()?a.action:a.detail;
        for(const auto& line:wrapped(content,detailWidth-28,9,12))details.emplace_back(line,muted);
    }
    if(!t.systemFault.empty())details.emplace_back(t.systemFault,ice);
    if(crew)for(const auto& line:wrapped(crew->summary,detailWidth-28,9,3))details.emplace_back(line,ice);
    if(!t.crewFire.empty())for(const auto& line:wrapped(t.crewFire,detailWidth-28,9,3))details.emplace_back(line,muted);
    details.emplace_back("FLIGHT "+std::to_string(int(std::min(1e9,t.flightSeconds)))+"S / "+flightQuantity(t.travelMetres),muted);
    if(!t.weather.empty())for(const auto& line:wrapped(t.weather+" / "+t.weatherDetail,detailWidth-28,9,4))details.emplace_back(line,muted);
    return details;
}
static CockpitRect computerPanel(double aspect,size_t rows){
    const double w=900*aspect;const bool wide=w>=1200;
    const double width=wide?std::clamp(w-656.,240.,650.):w-48;
    return {(w-width)*.5,wide?252.:624.,width,42+std::min(size_t(wide?25:6),rows)*13.};
}
static void drawCrewMinimap(CockpitOverlay& h,const CrewView& crew,double aspect) {
    const auto map=crewMinimap(crew,aspect);const auto p=map.panel;
    card(h,p,ice);
    const auto living=std::count_if(crew.members.begin(),crew.members.end(),[](const auto& m){return m.health>0;});
    fitted(h,"CREW DECK / "+std::to_string(living),p.x+12,p.y+11,p.w-90,10,ice);
    fitted(h,"F9 / C",p.x+p.w-66,p.y+12,54,8.5,muted);
    const glm::dvec2 centre(p.x+p.w*.5,p.y+112);
    constexpr std::array<glm::dvec2,10> hull{{{0,-83},{57,-57},{126,-30},{126,45},{73,81},
        {-73,81},{-126,45},{-126,-30},{-57,-57},{0,-83}}};
    for(size_t i=1;i<hull.size();++i){const auto a=centre+hull[i-1],b=centre+hull[i];
        line(h.strokes,a.x,a.y,b.x,b.y,ice*.35f);}
    const auto midpoint=[](CockpitRect r){return glm::dvec2(r.x+r.w*.5,r.y+r.h*.5);};
    for(size_t edge=0;edge<crewCorridors().size();++edge) {
        const auto [from,to]=crewCorridors()[edge];
        const auto a=midpoint(map.rooms[from]),b=midpoint(map.rooms[to]);
        const bool closed=crew.closedDoors&(1u<<edge);
        line(h.strokes,a.x,a.y,b.x,b.y,closed?danger*.45f:muted*.5f);
        if(closed){const auto at=(a+b)*.5,along=glm::normalize(b-a),side=glm::dvec2(-along.y,along.x)*4.;
            line(h.strokes,at.x-side.x,at.y-side.y,at.x+side.x,at.y+side.y,danger);}
    }
    for(size_t i=0;i<crewRoomCount;++i) {
        const auto r=map.rooms[i];const auto& room=crew.rooms[i];
        const bool unsafe=room.fire>1||room.oxygen<25||room.breach>1||room.integrity<70;
        const auto colour=room.fire>1?danger:unsafe?amber:ice*.65f;
        rect(h.panels,r.x,r.y,r.w,r.h,unsafe?colour*.14f:ink,unsafe?.95f:1.f);
        for(const auto segment:std::array<std::pair<glm::dvec2,glm::dvec2>,4>{{
            {{r.x,r.y},{r.x+r.w,r.y}},{{r.x+r.w,r.y},{r.x+r.w,r.y+r.h}},
            {{r.x+r.w,r.y+r.h},{r.x,r.y+r.h}},{{r.x,r.y+r.h},{r.x,r.y}}}})
            line(h.strokes,segment.first.x,segment.first.y,segment.second.x,segment.second.y,colour*.6f);
        const CockpitRect label{r.x+5,r.y+4,40,7.5};
        bool clear=true;
        for(size_t j=0;j<map.count;++j)if(label.overlaps({map.markers[j].label.x-7,map.markers[j].label.y-4,14,9})){clear=false;break;}
        if(clear)fitted(h,crewRoomCode(CrewStation(i)),label.x,label.y,label.w,label.h,colour);
        if(unsafe){const auto x=r.x+r.w-9,y=r.y+8;
            line(h.strokes,x,y-3,x,y+1,colour);rect(h.panels,x-.7,y+3,1.4,1.4,colour);}
    }
    for(size_t i=0;i<map.count;++i) {
        const auto& dot=map.markers[i];
        const auto color=dot.condition==CrewMapCondition::Lost?muted:dot.condition==CrewMapCondition::Incapacitated?danger:
                         dot.condition==CrewMapCondition::Injured?amber:dot.moving?ice:dot.resting?trafficInk:mint;
        if(dot.moving){const auto delta=dot.destination-dot.position;
            if(glm::length(delta)>1e-6){const auto direction=glm::normalize(delta),side=glm::dvec2(-direction.y,direction.x);
                line(h.strokes,dot.position.x,dot.position.y,dot.destination.x,dot.destination.y,color*.30f);
                const auto tip=dot.position+direction*10.,a=tip-direction*4.+side*2.5,b=tip-direction*4.-side*2.5;
                line(h.strokes,a.x,a.y,tip.x,tip.y,color);line(h.strokes,b.x,b.y,tip.x,tip.y,color);}}
        if(glm::length(dot.label-dot.position)>1){line(h.strokes,dot.position.x,dot.position.y,dot.label.x,dot.label.y,color*.7f);
            rect(h.panels,dot.position.x-1.5,dot.position.y-1.5,3,3,color);}
        rect(h.panels,dot.label.x-7,dot.label.y-4,14,9,ink);
        if(dot.roster==crew.selected&&crew.flightOpen)diamond(h.strokes,dot.label,9,pearl);
        text(h,std::to_string(dot.roster+1),dot.label.x-3.2,dot.label.y-3.75,7.5,color);
        if(dot.condition==CrewMapCondition::Injured||dot.condition==CrewMapCondition::Incapacitated){
            line(h.strokes,dot.label.x+5,dot.label.y-3,dot.label.x+5,dot.label.y, color);
            rect(h.panels,dot.label.x+4.5,dot.label.y+2,1,1,color);}
        if(dot.condition==CrewMapCondition::Lost){line(h.strokes,dot.label.x-5,dot.label.y+5,dot.label.x+5,dot.label.y-5,color);}
    }
    if(crew.selected<map.count) {
        const auto& m=crew.members[crew.selected];const auto& dot=map.markers[crew.selected];
        const auto place=crew.selected<crew.tasks.size()&&crew.tasks[crew.selected].status=="AWAY PARTY"?std::string("OUT"):std::string(crewRoomCode(dot.room))+(dot.moving?" > "+std::string(crewRoomCode(dot.next)):std::string{});
        const auto name=elide(std::to_string(crew.selected+1)+" "+m.name,p.w-24-(place.size()+3)*8.7*6/7,8.7);
        fitted(h,name+" / "+place,p.x+12,p.y+203,p.w-24,8.7,pearl);
    } else fitted(h,"NO CREW ABOARD",p.x+12,p.y+203,p.w-24,8.7,muted);
    fitted(h,"> MOVING / OUT AWAY / ! HURT",p.x+12,p.y+220,p.w-24,8,muted);
}
static void drawEngineering(CockpitOverlay& h,const CrewView& crew,double aspect) {
    const EngineeringLayout layout(aspect);const auto p=layout.panel;const auto& e=crew.engineering;
    const auto heat=e.heat>.92?danger:e.heat>.72?amber:ice;
    card(h,p,e.critical?amber:ice);
    fitted(h,(crew.flightOpen?std::string(crewRoomCode(crew.station)):std::string("POWER"))+" / "+std::string(powerPresetName(e.preset))+" / "+std::to_string(int(e.load*100))+"%",p.x+12,p.y+9,170,9,ice);
    fitted(h,"HEAT "+std::to_string(int(e.heat*100))+"%",p.x+193,p.y+9,81,9,heat);
    bar(h.panels,p.x+12,p.y+25,120,std::min(1.,e.load),ice);bar(h.panels,p.x+154,p.y+25,120,std::min(1.,e.heat),heat);
    fitted(h,"FUEL"+std::to_string(int(e.stores[0]))+" / H2O"+std::to_string(int(e.stores[1]))+" / O2 "+std::to_string(int(e.stores[2]))+" / BAT"+std::to_string(int(e.reserve*100))+"%",p.x+12,p.y+31,p.w-24,7.5,e.brownout?amber:muted);
    for(size_t i=0;i<4;++i){const auto r=layout.presets[i];card(h,r,unsigned(e.preset)==i?mint:ice*.5f);
        fitted(h,powerPresetName(PowerPreset(i)),r.x+4,r.y+4,r.w-8,7.4,unsigned(e.preset)==i?mint:ice);}
    constexpr std::array<std::string_view,4> labels{"ISOLATE","OPEN","FLUSH","LOAD"};
    for(size_t i=0;i<4;++i){const auto r=layout.actions[i];card(h,r,i<2&&!crew.flightOpen?muted:ice);
        fitted(h,labels[i],r.x+4,r.y+4,r.w-8,7.2,i==2?heat:ice);}
}
static void drawAlertChannel(CockpitOverlay& h,const std::vector<CockpitAlert>& alerts,double aspect,bool expanded,bool probe,bool compact,const FlightInstruments& t,const CrewView* crew){
    if(alerts.empty()&&!expanded)return;
    const double w=900*aspect,width=w<900?std::max(24.,w-48):std::clamp(w-656.,240.,650.);
    if(!alerts.empty()){
    const auto& primary=alerts.front();const auto color=primary.level==CockpitAlertLevel::Critical?danger:primary.level==CockpitAlertLevel::Warning?amber:ice;
    const double size=11;const auto heading=wrapped(primary.title,width-28,size,compact?1:3);
    std::string body=primary.action;
    if(body.empty()&&!primary.detail.empty()){
        body=primary.detail;if(body.starts_with(primary.title))body.erase(0,primary.title.size());
        while(body.starts_with(" / "))body.erase(0,3);
    }
    if(body.size()>size_t(std::max(30.,(width-28)*7/60)))body="CTRL-TAB MESSAGE DETAILS";
    auto lines=wrapped(body,width-28,9.5,compact?1:2);
    const auto receipt=compact||primary.key=="notice"?alerts.end():std::find_if(alerts.begin(),alerts.end(),[](const auto& a){return a.key=="notice";});
    const double height=20+heading.size()*15+lines.size()*13+(receipt!=alerts.end()?17:0)+(!compact&&alerts.size()>1?14:0);
    const CockpitRect panel{(w-width)*.5,probe?132.:w<900?170.:64.,width,height};
    occlude(h,panel);card(h,panel,color);h.popups.push_back({panel,primary.level==CockpitAlertLevel::Critical});
    std::erase_if(h.worldLabels,[&](const auto& r){return r.overlaps(panel);});
    double y=panel.y+10;
    for(const auto& line:heading){fitted(h,line,panel.x+14,y,panel.w-28,size,color);y+=15;}
    for(const auto& line:lines){fitted(h,line,panel.x+14,y,panel.w-28,9.5,ice);y+=13;}
    if(receipt!=alerts.end()){fitted(h,receipt->title,panel.x+14,y+3,panel.w-28,9.5,mint);y+=17;}
    if(!compact&&alerts.size()>1)fitted(h,"+"+std::to_string(alerts.size()-1)+" / CTRL-TAB DETAILS",panel.x+14,y+3,panel.w-28,8.5,muted);
    }
    if(expanded){
        const bool wide=w>=1200;
        const double detailWidth=wide?std::clamp(w-656.,240.,650.):w-48;
        const double x=(w-detailWidth)*.5;
        const auto details=computerDetails(alerts,t,crew,detailWidth);
        const auto count=std::min(size_t(wide?25:6),details.size());
        const auto detail=computerPanel(aspect,details.size());occlude(h,detail);card(h,detail,ice);
        fitted(h,"FLIGHT COMPUTER / "+std::to_string(alerts.size())+" ALERTS",x+14,detail.y+14,detailWidth-28,10.5,pearl);
        for(size_t i=0;i<count;++i)fitted(h,details[i].first,x+14,detail.y+40+i*13,detailWidth-28,9,details[i].second);
    }
}
static void drawContactSummary(CockpitOverlay& h,double aspect,std::span<const CelestialContact> contacts,std::span<const ActivityContact> activity){
    const auto panel=cockpitContactsSummary();card(h,panel,ice);
    fitted(h,"CONTACTS "+std::to_string(contacts.size()+activity.size()),38,155,150,10,ice);
    h.contactHits.push_back({{24,144,166,28},{},{},true,false});
    const auto living=std::count_if(contacts.begin(),contacts.end(),hasSurfaceLife);
    const auto badge=cockpitLifeBadge(aspect,false);
    if(living){rect(h.panels,badge.x,badge.y,badge.w,badge.h,mint*.14f);
        fitted(h,std::to_string(living)+" LIFE",badge.x+6,badge.y+4,badge.w-12,9.5,mint);
        h.contactHits.push_back({badge,{},{},false,true});}
    else fitted(h,"CTRL-TAB",badge.x,badge.y+4,badge.w,9,muted);
    const auto rows=cockpitSummaryRows(contacts,activity);
    for(size_t i=0;i<rows.size();++i){const auto row=rows[i];const auto y=181+i*25.;
        const auto name=row.activity?activity[row.index].body.name:contacts[row.index].label;
        const auto range=row.activity?activity[row.index].distance:contacts[row.index].distanceMetres;
        const bool selected=row.activity?activity[row.index].selected:contacts[row.index].selected;
        const auto color=row.activity?(activity[row.index].body.hostile?danger:selected?ice:pearl):hasSurfaceLife(contacts[row.index])?mint:ice;
        if(selected)rect(h.panels,30,y-4,262,23,ice*.12f,.9f);
        fitted(h,name,38,y,159,10.5,color);
        fitted(h,flightQuantity(range),201,y+1,82,9.5,muted,true);
        CockpitContactHit hit{{30,y-4,262,23},{},{},false,false};
        if(row.activity)hit.activity=activity[row.index].body.id;else hit.world=contacts[row.index].address;
        h.contactHits.push_back(std::move(hit));
    }
    if(rows.empty())fitted(h,"NO LOCAL CONTACTS / CTRL-TAB",38,190,246,9,muted);
}
CockpitRect surfaceReconPanel(double aspect){const double width=std::min(640.,900*aspect-48);return {(900*aspect-width)*.5,aspect<1?230.:200.,width,430};}
CockpitRect surfaceReconMapRect(double aspect){const auto p=surfaceReconPanel(aspect);return {p.x+16,p.y+48,p.w-32,176};}
static void drawSurfaceRecon(CockpitOverlay& h,const SurfaceReconView& v,double aspect){
    const auto panel=surfaceReconPanel(aspect),area=surfaceReconMapRect(aspect);
    occlude(h,panel);card(h,panel,ice);
    fitted(h,"SURFACE RECONNAISSANCE",panel.x+16,panel.y+14,panel.w-32,13,pearl);
    rect(h.panels,area.x,area.y,area.w,area.h,{.018,.028,.05},1);
    if(!v.map){fitted(h,v.status,area.x+12,area.y+70,area.w-24,10,ice);}
    else {
        const auto& map=*v.map;
        auto project=[&](glm::dvec2 p){return glm::dvec2(area.x+(p.x/map.radius+1)*.5*area.w,area.y+(p.y/map.radius+1)*.5*area.h);};
        auto point=[&](glm::dvec3 p){return project(reconPosition(map,p));};
        double low=1e30,high=-1e30;for(const auto& c:map.cells){low=std::min(low,double(c.height));high=std::max(high,double(c.height));}
        const double contourStep=std::max(20.,std::ceil((high-low)/240.)*20.);
        for(unsigned z=0;z+1<reconGrid;++z)for(unsigned x=0;x+1<reconGrid;++x){const auto& c=map.cells[z*reconGrid+x];
            const double elevation=(c.height-low)/std::max(15.,high-low);
            auto tint=glm::mix(glm::vec3(.055,.035,.13),glm::vec3(.18,.45,.43),float(elevation));
            tint*=float(1-std::min(.65,double(c.slope)));
            if(!c.dry)tint={.035,.09,.23};
            const auto a=project({-map.radius+x*map.radius*2/(reconGrid-1),-map.radius+z*map.radius*2/(reconGrid-1)});
            const double width=area.w/(reconGrid-1),height=area.h/(reconGrid-1);
            rect(h.panels,a.x,a.y,width+.1,height+.1,tint,.94f);
            const std::array<double,4> heights{c.height,map.cells[z*reconGrid+x+1].height,map.cells[(z+1)*reconGrid+x+1].height,map.cells[(z+1)*reconGrid+x].height};
            const std::array<glm::dvec2,4> corners{a,a+glm::dvec2(width,0),a+glm::dvec2(width,height),a+glm::dvec2(0,height)};
            const auto [minimum,maximum]=std::minmax_element(heights.begin(),heights.end());
            for(double level=std::ceil(*minimum/contourStep)*contourStep;level<=*maximum;level+=contourStep){
                std::array<glm::dvec2,4> hits;unsigned count=0;
                for(unsigned edge=0;edge<4;++edge){const unsigned next=(edge+1)%4;
                    if((heights[edge]<level)!=(heights[next]<level))hits[count++]=glm::mix(corners[edge],corners[next],(level-heights[edge])/(heights[next]-heights[edge]));}
                for(unsigned i=0;i+1<count;i+=2)line(h.strokes,hits[i].x,hits[i].y,hits[i+1].x,hits[i+1].y,ice*.38f);
            }
        }
        for(const auto& w:v.weather)if(w.severity>.3){const auto p=project(w.point);
            const double width=area.w/5,height=area.h/5;
            const double left=std::max(area.x,p.x-width*.5),top=std::max(area.y,p.y-height*.5);
            rect(h.panels,left,top,std::max(0.,std::min(area.x+area.w,left+width)-left),std::max(0.,std::min(area.y+area.h,top+height)-top),amber,float(w.severity*.20));}
        const auto ship=project(glm::clamp(v.ship,glm::dvec2(-map.radius),glm::dvec2(map.radius)));
        if(const auto selected=v.target()){const auto p=point(selected->metres);line(h.strokes,ship.x,ship.y,p.x,p.y,ice*.7f);}
        for(const auto& target:v.points){const auto p=point(target.metres);const bool selected=target.id==v.selected;
            const auto tint=target.kind==ReconKind::Incident?(target.phase==RecoveryPhase::Complete?mint:amber):target.kind==ReconKind::Assay?mint:target.kind==ReconKind::Estimate?trafficInk:ice;
            const double size=selected?7:4;
            diamond(h.strokes,p,size,tint);
            if(target.kind==ReconKind::Estimate){line(h.strokes,p.x-3,p.y,p.x+3,p.y,tint);}
            else if(target.kind==ReconKind::Assay){line(h.strokes,p.x-3,p.y,p.x+3,p.y,tint);line(h.strokes,p.x,p.y-3,p.x,p.y+3,tint);}
            else if(target.kind==ReconKind::Entrance){rect(h.panels,p.x-2,p.y-2,4,4,ice);}
            else if(target.kind==ReconKind::Incident){line(h.strokes,p.x,p.y-2,p.x,p.y+2,tint);}
            if(selected)diamond(h.strokes,p,11,tint*.65f);
        }
        auto heading=glm::dvec2(v.heading.x*area.w,v.heading.y*area.h);heading=glm::normalize(heading);
        const auto side=glm::dvec2(-heading.y,heading.x);
        triangle(h.panels,{ship+heading*7.,0},{ship-heading*4.+side*4.,0},{ship-heading*4.-side*4.,0},mint);
    }
    fitted(h,v.loading?v.status:v.status+" / SIGNAL "+std::to_string(int(v.scanQuality*100))+"%",panel.x+16,panel.y+234,panel.w-32,8,ice);
    const auto selected=v.target();fitted(h,selected?selected->name:"NO LOCAL RETURNS",panel.x+16,panel.y+254,panel.w-32,11,selected&&selected->kind==ReconKind::Incident?amber:pearl);
    const auto detail=wrapped(selected?selected->detail:"APPROACH SOLID TERRAIN / F5 CLOSE",panel.w-32,8.5);
    for(size_t i=0;i<std::min(size_t(2),detail.size());++i)fitted(h,detail[i],panel.x+16,panel.y+273+i*13,panel.w-32,8.5,ice);
    fitted(h,v.navigator,panel.x+16,panel.y+306,panel.w-32,8.5,v.navWatching?mint:muted);
    fitted(h,v.engineer,panel.x+16,panel.y+323,panel.w-32,8.5,v.engineerWatching?mint:muted);
    fitted(h,v.action,panel.x+16,panel.y+349,panel.w-32,9,amber);
    if(v.workProgress>0)bar(h.panels,panel.x+16,panel.y+369,panel.w-32,v.workProgress,mint);
    fitted(h,"CLICK MAP / SHIFT-F5 NEXT / F4 ROUTE / 0 RESPOND",panel.x+16,panel.y+389,panel.w-32,8,ice);
    fitted(h,"CTRL-F5 NAV / ALT-F5 ENGINEER / F5 CLOSE",panel.x+16,panel.y+407,panel.w-32,8,muted);
}
static void drawVessel(CockpitOverlay& h, const VesselView& v, double aspect) {
    const CockpitRect p{450*aspect-390,150,780,500};
    occlude(h,p); card(h,p,ice);
    fitted(h,"CONTACT OPERATIONS",p.x+18,p.y+18,p.w-36,18,pearl);
    fitted(h,v.name,p.x+18,p.y+48,p.w-36,12,mint);
    fitted(h,v.scanStatus,p.x+18,p.y+72,p.w-36,10,v.confirmed?mint:amber);
    bar(h.panels,p.x+18,p.y+92,p.w-36,v.scan,ice);
    fitted(h,v.confirmed?"INTERNAL DECK / LIVE FAULTS":"HULL LAYOUT / INTERNALS UNCONFIRMED",p.x+18,p.y+112,310,9,muted);
    const glm::dvec2 centre{p.x+164,p.y+258};
    auto point=[&](unsigned i){return centre+v.deck.rooms[i]*glm::dvec2(60,46);};
    for(auto [a,b]:v.deck.doors) {const auto x=point(a),y=point(b);line(h.strokes,x.x,x.y,y.x,y.y,muted*.6f);}
    for(unsigned i=0;i<crewRoomCount;++i){const auto c=point(i);const auto& r=v.rooms[i];
        const CockpitRect room{c.x-33,c.y-18,66,36};
        const auto color=v.confirmed&&(r.fire>15||r.oxygen<18)?amber:v.deployed&&v.partyRoom==i?mint:ice;
        card(h,room,color);fitted(h,crewRoomCode(CrewStation(i)),room.x+8,room.y+6,50,8,color);
        fitted(h,!v.confirmed?"?":r.fire>15?"FIRE":r.oxygen<18?"AIR LOW":std::to_string(int(r.integrity))+"%",room.x+8,room.y+21,50,7.5,color);
    }
    if(v.deployed){const auto at=point(v.partyRoom);diamond(h.strokes,{at.x,at.y+26},4,mint);}
    const double x=p.x+345,w=p.w-365;
    fitted(h,v.confirmed?"ENGINEERING TELEMETRY":"FOCUS SCAN / HOLD F12",x,p.y+115,w,11,ice);
    if(v.confirmed){
        fitted(h,"HEAT "+std::to_string(int(v.heat*100))+"% / LOAD "+std::to_string(int(v.power*100))+"%",x,p.y+142,w,10,v.heat>.72?amber:mint);
        fitted(h,"FUEL "+std::to_string(int(v.fuel))+" KG / AIR "+std::to_string(int(v.air))+" KG",x,p.y+163,w,10,ice);
        fitted(h,"ENG "+std::to_string(int(v.systems[1]))+" / RCS "+std::to_string(int(v.systems[2]))+" / LIFE "+std::to_string(int(v.systems[6])),x,p.y+184,w,10,ice);
    }else fitted(h,"NO INTERNAL READINGS",x,p.y+150,w,10,muted);
    fitted(h,v.status,x,p.y+222,w,10,v.accessible?mint:amber);
    fitted(h,v.deployed?"AWAY / "+v.party:"AUTOMATIC CREW SELECTION",x,p.y+250,w,10,mint);
    fitted(h,v.taskStatus,x,p.y+274,w,10,ice);
    if(v.deployed)bar(h.panels,x,p.y+298,w,v.progress,mint);
    fitted(h,v.supplies,p.x+18,p.y+379,p.w-36,9,muted);
    fitted(h,"1 REPAIR / 2 EVACUATE / 3 EQUIPMENT / 4 RECALL",p.x+18,p.y+411,p.w-36,11,mint);
    fitted(h,"KEEP WITHIN 120 M / MATCH SPEED BELOW 3 M/S / FULL SIZE",p.x+18,p.y+439,p.w-36,9,ice);
    fitted(h,"CTRL-0 OR ESC CLOSE / F9 CREW",p.x+18,p.y+472,p.w-36,9,muted);
}
#include "DiscoveryCockpit.hpp"
#include "TargetingCockpit.hpp"
#include "AwarenessCockpit.hpp"
Mesh cockpitMesh(double aspect) {
    Mesh m;
    auto point = [&](glm::dvec2 p, double depth) {
        return glm::dvec3((p.x * 2 - 1) * aspect * .7002075 * depth, (1 - p.y * 2) * .7002075 * depth,
                          -depth);
    };
    auto tri = [&](glm::dvec3 a, glm::dvec3 b, glm::dvec3 c, glm::vec3 col) {
        auto n = glm::normalize(glm::cross(b - a, c - a));
        if (glm::dot(n, -(a + b + c)) < 0)
            n = -n;
        const float diffuse =
            float(.48 + .52 * std::max(0., glm::dot(n, glm::normalize(glm::dvec3(.6, .7, 1)))));
        triangle(m, a, b, c, col * diffuse);
    };
    auto panel = [&](std::initializer_list<glm::dvec2> points, glm::vec3 color, double front = .76,
                     double bevel = .10) {
        std::vector<glm::dvec2> outer(points), inner;
        glm::dvec2 center{};
        for (auto p : outer)
            center += p;
        center /= outer.size();
        for (auto p : outer)
            inner.push_back(glm::mix(p, center, bevel));
        auto depth = [&](glm::dvec2 p) { return front / (1 + (p.y - .74) * .28); };
        for (size_t i = 1; i + 1 < inner.size(); i++)
            tri(point(inner[0], depth(inner[0])), point(inner[i], depth(inner[i])),
                point(inner[i + 1], depth(inner[i + 1])), color);
        for (size_t i = 0; i < outer.size(); i++) {
            const size_t j = (i + 1) % outer.size();
            auto a = point(inner[i], depth(inner[i])), b = point(inner[j], depth(inner[j]));
            auto c = point(outer[i], depth(outer[i]) + .035), d = point(outer[j], depth(outer[j]) + .035);
            tri(a, c, b, color * 1.5f + glm::vec3(.04f));
            tri(b, c, d, color * 1.5f + glm::vec3(.04f));
            auto e = point(outer[i], depth(outer[i]) + .18), f = point(outer[j], depth(outer[j]) + .18);
            tri(c, e, d, ink);
            tri(d, e, f, ink);
        }
    };
    const glm::vec3 titanium{.12, .075, .060}, dark{.030, .020, .018}, copper{.30, .105, .045};
    constexpr double top = .052, deck = .842;
    panel({{0, top - .05}, {.24, top - .025}, {.21, top + .009}, {0, top + .035}}, titanium, .86);
    panel({{1, top - .05}, {.76, top - .025}, {.79, top + .009}, {1, top + .035}}, titanium, .86);
    panel({{0, top}, {.032, top + .032}, {.085, deck - .045}, {.054, deck + .03}, {0, deck - .005}}, titanium,
          .7, .16);
    panel({{1, top}, {.968, top + .032}, {.915, deck - .045}, {.946, deck + .03}, {1, deck - .005}}, titanium,
          .7, .16);
    panel({{0, deck - .005}, {.13, deck - .055}, {.28, deck + .025}, {0, 1}}, titanium, .65);
    panel({{1, deck - .005}, {.87, deck - .055}, {.72, deck + .025}, {1, 1}}, titanium, .65);
    panel({{.13, deck}, {.87, deck}, {1, 1}, {0, 1}}, dark, .68, .035);
    panel({{.11, deck - .041}, {.14, deck - .043}, {.232, deck + .005}, {.216, deck + .011}}, copper, .62,
          .22);
    panel({{.89, deck - .041}, {.86, deck - .043}, {.768, deck + .005}, {.784, deck + .011}}, copper, .62,
          .22);
    for (int side : {0, 1})
        for (int i = 0; i < 6; i++) {
            double x = side ? 1 - .018 - i * .012 : .018 + i * .012;
            panel({{x, deck - .016}, {x + .006, deck - .016}, {x + .006, deck - .012}, {x, deck - .012}},
                  i == 5 ? amber : ice * .7f, .58, 0);
        }
    for (double y : {.25, .43, .61})
        for (int side : {0, 1}) {
            double x = .023 + (y - top) * .077;
            if (side)
                x = 1 - x;
            panel({{x - .003, y - .004}, {x + .003, y - .004}, {x + .003, y + .004}, {x - .003, y + .004}},
                  muted, .66, .35);
        }
    m.compact();
    return m;
}

namespace {
enum class CabinMaterial { Painted, Polymer, Brushed, Bronze, Display };
struct CabinFinish {
    glm::vec3 colour;
    CabinMaterial material;
    float vertexAlpha(float emission) const {
        // Five bits for emissive intensity and the high bits for the finish.
        return (float(material)*32.f + std::round(std::clamp(emission,0.f,1.f)*31.f)) / 255.f;
    }
};
const glm::vec3 cabinLamp{.87f,.79f,.63f}, cabinSignal{.24f,.78f,.68f};
const CabinFinish cabinFrame{{.15f,.177f,.192f},CabinMaterial::Painted},
    cabinLiner{{.077f,.096f,.104f},CabinMaterial::Painted},
    cabinRubber{{.029f,.034f,.037f},CabinMaterial::Polymer},
    cabinTrim{{.32f,.36f,.38f},CabinMaterial::Brushed},
    cabinCopper{{.54f,.29f,.126f},CabinMaterial::Bronze},
    cabinScreen{{.014f,.031f,.032f},CabinMaterial::Display},
    cabinWarmLight{cabinLamp,CabinMaterial::Display},
    cabinStatusLight{cabinSignal,CabinMaterial::Display};

constexpr std::array<glm::dvec3,4> cabinCeiling{{{.82,.54,-1.46},{1.01,.57,-.71},
                                                {1.01,.57,.61},{.79,.51,1.15}}};
constexpr std::array<glm::dvec3,4> cabinSill{{{.92,-.36,-1.47},{1.05,-.32,-.71},
                                             {1.05,-.32,.61},{.79,-.37,1.15}}};

glm::dvec3 cabinSide(glm::dvec3 point, double side) {
    return {side*point.x, point.y, point.z};
}

struct CabinSurface {
    glm::dvec3 origin, right, down;
    glm::dvec3 normal() const { return glm::normalize(glm::cross(down, right)); }
    glm::dvec3 point(double u, double v) const { return origin + u * right + v * down; }
};

// Hull-local metres from the pilot's eye. The openings, instruments and
// consoles retain their positions when the window aspect or head angle changes.
const CabinSurface driveScreen{{-.39,-.352,-1.25}, {.78,0,0}, {0,-.160,.179}};
const CabinSurface portScreen{{-.907,-.322,-.27}, {0,0,-.53}, {.18,-.157,0}};
const CabinSurface starboardScreen{{.907,-.322,-.80}, {0,0,.53}, {-.18,-.157,0}};
const CabinSurface overheadScreen{{-.167,.472,.122}, {.334,0,0}, {0,0,-.35}};
const glm::dvec3 yokePivot{0,-.640,-.888};

struct CabinBuilder {
    Mesh mesh;

    void face(glm::dvec3 a, glm::dvec3 b, glm::dvec3 c, glm::dvec3 d, const CabinFinish& finish,
              float emission = 0) {
        const float alpha = finish.vertexAlpha(emission);
        triangle(mesh, a, b, c, finish.colour, alpha);
        triangle(mesh, a, c, d, finish.colour, alpha);
    }

    void beam(glm::dvec3 from, glm::dvec3 to, double width, double height, const CabinFinish& finish,
              float emission = 0) {
        const auto axis = glm::normalize(to - from);
        const auto up = std::abs(axis.y) > .95 ? glm::dvec3(0,0,1) : glm::dvec3(0,1,0);
        const auto across = glm::normalize(glm::cross(axis, up)) * (width * .5);
        const auto vertical = glm::normalize(glm::cross(across, axis)) * (height * .5);
        constexpr std::array<glm::dvec2,8> outline{{{-1,-.72},{-.72,-1},{.72,-1},{1,-.72},
                                                  {1,.72},{.72,1},{-.72,1},{-1,.72}}};
        std::array<glm::dvec3,outline.size()> corners;
        for (size_t i = 0; i < corners.size(); ++i)
            corners[i] = outline[i].x * across + outline[i].y * vertical;
        for (size_t i = 0; i < corners.size(); ++i) {
            const auto a = corners[i], b = corners[(i + 1) % corners.size()];
            face(from + a, to + a, to + b, from + b, finish, emission);
            triangle(mesh, from, from + a, from + b, finish.colour, finish.vertexAlpha(emission));
            triangle(mesh, to, to + b, to + a, finish.colour, finish.vertexAlpha(emission));
        }
    }

    void instrument(const CabinSurface& surface) {
        const auto at = [&](double x, double y) { return surface.point(x, y); };
        const auto a=at(0,0), b=at(1,0), c=at(1,1), d=at(0,1);
        const auto rim = surface.normal() * .014;
        const auto backing = surface.normal() * .095;
        face(a,b,c,d,cabinScreen);
        face(a,b,b-backing,a-backing,cabinFrame);
        face(b,c,c-backing,b-backing,cabinFrame);
        face(c,d,d-backing,c-backing,cabinFrame);
        face(d,a,a-backing,d-backing,cabinFrame);
        beam(a+rim,b+rim,.052,.036,cabinTrim);
        beam(d+rim,c+rim,.061,.045,cabinFrame);
        beam(a+rim,d+rim,.043,.041,cabinFrame);
        beam(b+rim,c+rim,.043,.041,cabinFrame);
    }
};
} // namespace

Mesh xalariaCockpitMesh() {
    CabinBuilder cabin;
    const auto& ceiling = cabinCeiling;
    const auto& sill = cabinSill;

    for (double side : {-1.,1.}) {
        const auto point = [side](double x, double y, double z) { return glm::dvec3(side*x,y,z); };
        for (size_t i = 0; i + 1 < sill.size(); ++i) {
            const auto a=cabinSide(sill[i],side), b=cabinSide(sill[i+1],side);
            const auto c=cabinSide(ceiling[i],side), d=cabinSide(ceiling[i+1],side);
            cabin.face(a,b,point(.80,-.82,b.z),point(.80,-.82,a.z),cabinLiner);
            cabin.face(c,d,point(.49,.69,d.z),point(.49,.69,c.z),cabinLiner);
            cabin.beam(a,b,.112,.12,cabinFrame);
            cabin.beam(c,d,.106,.12,cabinFrame);
            cabin.beam(a+point(-.046,.041,0),b+point(-.046,.041,0),.019,.015,cabinCopper);
            cabin.beam(c+point(-.054,-.044,0),d+point(-.054,-.044,0),.014,.014,cabinTrim);
            cabin.beam(a+point(-.020,.061,0),b+point(-.020,.061,0),.031,.022,cabinRubber);
            cabin.beam(c+point(-.020,-.061,0),d+point(-.020,-.061,0),.027,.022,cabinRubber);
            cabin.beam(a+point(-.14,-.41,0),b+point(-.14,-.41,0),.064,.075,cabinFrame);
        }
        for (size_t i : {0U,2U,3U})
            cabin.beam(cabinSide(sill[i],side),cabinSide(ceiling[i],side),.108,.117,cabinFrame);
        const auto a = point(1.05,-.32,-.045), b = point(1.01,.57,.12);
        cabin.beam(a,b,.059,.065,cabinFrame);
        cabin.beam(a+point(-.035,.02,0),b+point(-.035,-.02,0),.012,.022,cabinTrim);
        for (double height : {.20,.39,.65,.82}) {
            const auto fastener = glm::mix(a,b,height) + point(-.034,0,-.023);
            cabin.beam(fastener,fastener+point(-.009,0,0),.014,.014,cabinTrim);
        }
        cabin.beam(point(.93,.43,-.58),point(.93,.43,-.19),.014,.018,cabinWarmLight,.72f);
        cabin.beam(point(.93,.43,.24),point(.93,.43,.48),.014,.018,cabinWarmLight,.72f);

        // Angled side consoles sit below the windows and wrap around the seat.
        cabin.face(point(.995,-.307,-1.06),point(.995,-.307,.49),point(.665,-.544,.47),
                   point(.625,-.544,-.98),cabinFrame);
        cabin.face(point(.625,-.544,-.98),point(.665,-.544,.47),point(.74,-.70,.48),
                   point(.75,-.70,-.98),cabinLiner);
        cabin.beam(point(.625,-.542,-.99),point(.665,-.542,.48),.069,.065,cabinTrim);
        cabin.beam(point(.718,-.626,-.82),point(.714,-.626,.41),.033,.019,cabinRubber);
        cabin.beam(point(.696,-.603,-.24),point(.693,-.603,.26),.012,.013,cabinStatusLight,.48f);
        for (double z : {-.875,-.605,-.335,.335})
            cabin.beam(point(.641,-.508,z-.008),point(.642,-.508,z+.008),.021,.015,cabinCopper);
        cabin.instrument(side < 0 ? portScreen : starboardScreen);

        for (int i = 0; i < 5; ++i) {
            const double z = -.99 + .037*i;
            cabin.beam(point(.912,-.35,z),point(.747,-.48,z),.010,.016,cabinRubber);
        }
        for (int i = 0; i < 4; ++i) {
            const double z = -.22 + .043*i;
            cabin.beam(point(.884,-.36,z),point(.849,-.392,z),.023,.018,cabinRubber);
            cabin.beam(point(.882,-.351,z),point(.852,-.38,z),.013,.011,
                       i == 0 ? cabinCopper : cabinStatusLight,i == 0 ? .35f : .62f);
        }
        if (side < 0) {
            cabin.beam(point(.802,-.444,-.015),point(.802,-.444,.415),.19,.065,cabinRubber);
            for (double x : {.763,.831})
                cabin.beam(point(x,-.401,.013),point(x,-.401,.377),.016,.021,cabinTrim);
            for (int stop = 0; stop <= 5; ++stop) {
                const double z = .056 + stop*.054;
                cabin.beam(point(.711,-.408,z),point(.737,-.407,z),.009,.010,
                           stop == 5 ? cabinCopper : cabinWarmLight,.40f);
            }
        } else {
            cabin.beam(point(.799,-.442,-.006),point(.799,-.442,.400),.237,.072,cabinRubber);
            for (int row = 0; row < 3; ++row)
                for (int key = 0; key < 8; ++key) {
                    const double x = .718 + .049*row, z = .025 + .043*key;
                    cabin.beam(point(x,-.398+.027*row,z),point(x,-.398+.027*row,z+.029),
                               .032,.022,key == 7 ? cabinCopper : cabinTrim);
                }
        }

        // Rack panels and cable conduits make the quarter view part of the hull.
        cabin.beam(point(.86,.17,.76),point(.86,.17,1.045),.252,.50,cabinLiner);
        cabin.beam(point(.726,-.025,.773),point(.726,-.025,1.016),.047,.041,cabinTrim);
        for (int i = 0; i < 6; ++i) {
            const double y = .052 + i*.044;
            cabin.beam(point(.73,y,.786),point(.73,y,1.019),.02,.018,cabinRubber);
        }
        cabin.beam(point(.74,.19,.68),point(.74,.37,.70),.036,.042,cabinCopper);
        cabin.beam(point(.59,.587,-1.03),point(.59,.587,.73),.027,.022,cabinWarmLight,.65f);
    }

    for (size_t i = 0; i + 1 < ceiling.size(); ++i) {
        const double a=ceiling[i].z, b=ceiling[i+1].z;
        cabin.face({-.49,.69,a},{.49,.69,a},{.49,.69,b},{-.49,.69,b},cabinLiner);
        cabin.face({-.80,-.82,a},{.80,-.82,a},{.80,-.82,b},{-.80,-.82,b},cabinRubber);
    }
    cabin.beam({-.82,.54,-1.46},{.82,.54,-1.46},.138,.148,cabinFrame);
    cabin.face({-.82,.54,-1.46},{.82,.54,-1.46},{.49,.69,-1.46},{-.49,.69,-1.46},cabinLiner);
    cabin.beam({-.84,.522,-1.376},{.84,.522,-1.376},.022,.027,cabinTrim);
    cabin.beam({-.92,-.355,-1.48},{.92,-.355,-1.48},.15,.113,cabinFrame);
    cabin.beam({-.83,.464,-1.417},{.83,.464,-1.417},.028,.026,cabinRubber);
    cabin.beam({-.90,-.294,-1.437},{.90,-.294,-1.437},.030,.024,cabinRubber);
    for (double x : {-.66,-.44,.44,.66}) {
        cabin.beam({x,.531,-1.398},{x,.531,-1.388},.018,.018,cabinTrim);
        cabin.beam({x,-.355,-1.414},{x,-.355,-1.399},.018,.018,cabinTrim);
    }
    cabin.beam({0,.607,-1.25},{0,.607,.75},.388,.135,cabinFrame);
    cabin.beam({0,.52,-.54},{0,.52,.31},.341,.071,cabinRubber);
    cabin.instrument(overheadScreen);
    for (double z : {-.476,-.361,.247})
        cabin.beam({-.115,.472,z},{.115,.472,z},.031,.019,cabinWarmLight,.69f);
    for (double z : {-.73,.54})
        cabin.beam({-.64,.613,z},{.64,.613,z},.106,.097,cabinFrame);
    for (double x : {-.143,.143})
        for (double z : {-.44,.30})
            cabin.beam({x-.01,.482,z},{x+.01,.482,z},.021,.018,cabinCopper);

    // Central dashboard, recessed pedals, and the pilot's hand controls.
    cabin.face({-.94,-.344,-1.48},{.94,-.344,-1.48},{.68,-.566,-.98},{-.68,-.566,-.98},cabinFrame);
    cabin.face({-.67,-.565,-.98},{.67,-.565,-.98},{.55,-.72,-1.20},{-.55,-.72,-1.20},cabinLiner);
    cabin.face({-.92,-.36,-1.47},{.92,-.36,-1.47},{.8,-.82,-1.47},{-.8,-.82,-1.47},cabinLiner);
    cabin.beam({-.68,-.556,-.985},{.68,-.556,-.985},.062,.063,cabinTrim);
    cabin.instrument(driveScreen);
    for (double side : {-1.,1.}) {
        cabin.beam({side*.61,-.405,-1.29},{side*.52,-.523,-1.079},.176,.065,cabinRubber);
        for (int i = 0; i < 4; ++i) {
            const double z = -1.263 + i*.046;
            cabin.beam({side*.52,-.367-i*.024,z},{side*.61,-.367-i*.024,z},.018,.013,
                       i == 0 ? cabinCopper : cabinWarmLight,.45f);
        }
        cabin.beam({side*.24,-.793,-1.36},{side*.24,-.755,-1.08},.18,.072,cabinFrame);
        for (int i = 0; i < 4; ++i) {
            const double z = -1.31 + i*.048;
            cabin.beam({side*.17,-.749,z},{side*.31,-.749,z},.018,.017,cabinRubber);
        }
        cabin.beam({side*.30,-.778,-1.05},{side*.30,-.778,.32},.049,.038,cabinFrame);
        for (int i = 0; i < 8; ++i) {
            const double z = -.99 + i*.089;
            cabin.beam({side*.061,-.807,z},{side*.259,-.807,z},.039,.018,cabinLiner);
        }
    }
    cabin.beam({0,-.708,-1.11},yokePivot,.162,.161,cabinFrame);
    cabin.beam({0,-.661,-.956},{0,-.623,-.862},.143,.121,cabinRubber);

    // Rear pressure bulkhead, access hatch, and seat under the camera.
    cabin.face({-.79,.51,1.15},{.79,.51,1.15},{.80,-.82,1.15},{-.80,-.82,1.15},cabinLiner);
    cabin.face({-.79,.51,1.15},{.79,.51,1.15},{.49,.69,1.15},{-.49,.69,1.15},cabinLiner);
    cabin.beam({0,-.031,1.04},{0,-.031,1.149},.99,1.31,cabinFrame);
    cabin.beam({0,-.031,.988},{0,-.031,1.03},.83,1.14,cabinTrim);
    cabin.beam({0,-.031,.965},{0,-.031,.986},.749,1.06,cabinLiner);
    cabin.beam({0,.37,.942},{0,-.46,.942},.053,.023,cabinRubber);
    for (double x : {-.113,.113})
        cabin.beam({x,-.054,.906},{x,-.054,.977},.055,.063,cabinFrame);
    cabin.beam({-.113,-.054,.89},{.113,-.054,.89},.046,.047,cabinCopper);
    for (double y : {-.383,.244})
        cabin.beam({-.35,y,.95},{-.255,y,.935},.058,.087,cabinFrame);
    cabin.beam({-.19,.35,.952},{.19,.35,.952},.028,.033,cabinStatusLight,.61f);
    cabin.beam({0,-.713,-.205},{0,-.713,.46},.576,.142,cabinFrame);
    cabin.beam({0,-.611,-.232},{0,-.611,.431},.514,.104,cabinRubber);
    cabin.beam({0,-.605,.424},{0,-.349,.55},.498,.125,cabinRubber);
    for (double side : {-1.,1.}) {
        cabin.beam({side*.271,-.567,-.16},{side*.271,-.541,.365},.063,.101,cabinLiner);
        cabin.beam({side*.217,-.558,-.182},{side*.217,-.558,.412},.018,.014,cabinTrim);
    }
    for (double z : {-.10,.02,.14,.26,.38})
        cabin.beam({-.205,-.558,z},{.205,-.558,z},.011,.010,cabinRubber);

    cabin.mesh.compact();
    return std::move(cabin.mesh);
}

Mesh xalariaCockpitGlazing() {
    Mesh glass;
    const auto pane = [&](glm::dvec3 a, glm::dvec3 b, glm::dvec3 c, glm::dvec3 d, glm::vec3 tint) {
        triangle(glass,a,b,c,tint);
        triangle(glass,a,c,d,tint);
    };
    pane(cabinSide(cabinSill.front(),-1),cabinSill.front(),cabinCeiling.front(),
         cabinSide(cabinCeiling.front(),-1),{.49,.67,.73});
    for (double side : {-1.,1.})
        for (size_t i = 0; i+1 < cabinSill.size(); ++i)
            pane(cabinSide(cabinSill[i],side),cabinSide(cabinSill[i+1],side),
                 cabinSide(cabinCeiling[i+1],side),cabinSide(cabinCeiling[i],side),{.43,.60,.64});
    glass.compact();
    return glass;
}

Mesh xalariaCockpitYoke() {
    CabinBuilder yoke;
    yoke.beam(yokePivot,{0,-.554,-.68},.111,.108,cabinTrim);
    yoke.beam({0,-.552,-.73},{0,-.552,-.626},.222,.119,cabinFrame);
    yoke.beam({-.056,-.528,-.615},{.056,-.528,-.615},.019,.020,cabinStatusLight,.67f);
    for (double side : {-1.,1.}) {
        yoke.beam({side*.085,-.551,-.685},{side*.222,-.519,-.645},.073,.075,cabinTrim);
        yoke.beam({side*.222,-.519,-.645},{side*.245,-.386,-.673},.072,.069,cabinRubber);
        yoke.beam({side*.245,-.402,-.674},{side*.188,-.391,-.708},.052,.047,cabinFrame);
        yoke.beam({side*.214,-.377,-.697},{side*.190,-.375,-.715},.027,.015,cabinCopper,.34f);
        for (int groove = 0; groove < 4; ++groove) {
            const double y = -.503 + groove*.023;
            const double x = side*(.222 + groove*.004);
            const double z = -.611 - groove*.004;
            yoke.beam({x-.028,y,z},{x+.028,y,z},.010,.012,cabinFrame);
        }
    }
    yoke.mesh.compact();
    return std::move(yoke.mesh);
}

Mesh xalariaCockpitThrottle() {
    CabinBuilder throttle;
    throttle.beam({-.798,-.397,.289},{-.798,-.397,.343},.089,.047,cabinTrim);
    throttle.beam({-.798,-.396,.317},{-.783,-.273,.300},.044,.052,cabinFrame);
    throttle.beam({-.868,-.272,.300},{-.712,-.272,.300},.068,.062,cabinCopper);
    throttle.beam({-.852,-.243,.305},{-.728,-.243,.305},.014,.013,cabinWarmLight,.60f);
    throttle.mesh.compact();
    return std::move(throttle.mesh);
}

CockpitControlPoses xalariaCockpitControlPoses(const FlightInstruments& instruments) {
    const auto deflection = glm::clamp(instruments.localAngularVelocity * glm::dvec3(.22,.16,.21),
                                       glm::dvec3(-.34), glm::dvec3(.34));
    const auto yaw = glm::angleAxis(deflection.y, glm::dvec3(0,1,0));
    const auto pitch = glm::angleAxis(deflection.x, glm::dvec3(1,0,0));
    const auto roll = glm::angleAxis(deflection.z, glm::dvec3(0,0,1));
    const auto pivot = glm::vec3(yokePivot);
    const glm::mat4 identity(1);
    return {
        glm::translate(identity,pivot) * glm::mat4_cast(glm::quat(yaw*pitch*roll)) * glm::translate(identity,-pivot),
        glm::translate(identity,glm::vec3(0,0,-.265f*float(std::clamp(instruments.throttle,0.,1.))))
    };
}

Mesh xalariaCockpitDisplays(const FlightInstruments& instruments, double fuel, double fuelCapacity) {
    Mesh displays;
    const auto addScreen = [&](const CabinSurface& surface, const CockpitOverlay& screen, double height = 128.) {
        const auto offset = surface.normal() * .003;
        for (const auto* layer : {&screen.panels, &screen.lettering}) {
            const auto first = uint32_t(displays.vertices.size());
            for (const auto& vertex : layer->vertices) {
                const auto p = surface.point(vertex.x / 320., vertex.y / height) + offset;
                displays.vertices.push_back({float(p.x),float(p.y),float(p.z),vertex.color});
            }
            for (auto index : layer->indices)
                displays.indices.push_back(first + index);
        }
    };
    const glm::vec3 grid{.11,.36,.34};
    const auto divider = [grid](CockpitOverlay& screen, double y) {
        rect(screen.panels,27,y,266,1.5,grid);
    };
    const auto gauge = [](CockpitOverlay& screen, double y, double fraction, glm::vec3 colour) {
        for (int i = 0; i < 16; ++i) {
            const double x = 27 + i*16.8;
            const double fill = std::clamp(fraction*16-i,0.,1.);
            if (fill > 0)
                rect(screen.panels,x,y,13*fill,7,colour);
            if (fill < 1)
                rect(screen.panels,x+13*fill,y,13*(1-fill),7,{.075,.16,.16});
        }
    };
    const auto quantity = [](CockpitOverlay& screen, double value, double x, double y, double width,
                             double size, glm::vec3 colour, bool speed = false) {
        const auto label = flightQuantity(value,speed);
        text(screen,label,x,y,std::min(size,width*7/(6*label.size())),colour,label.size(),true);
    };

    CockpitOverlay drive;
    text(drive,"VELOCITY",27,21,13,cabinSignal);
    const bool aboveSurface = instruments.altitude >= 0;
    text(drive,aboveSurface ? "ALTITUDE" : "STOP DIST",202,21,11,cabinSignal);
    divider(drive,42);
    quantity(drive,instruments.speed,27,56,153,19,cabinLamp,true);
    quantity(drive,aboveSurface ? instruments.altitude : instruments.stoppingDistance,202,58,92,13,cabinLamp);
    rect(drive.panels,188,50,1.5,34,grid);
    fitted(drive,instruments.drive,27,93,260,12,cabinSignal);
    addScreen(driveScreen, drive);

    CockpitOverlay port;
    text(port,"THROTTLE",27,21,13,cabinSignal);
    text(port,std::to_string(int(std::round(instruments.throttle*100)))+"%",214,19,16,cabinLamp);
    gauge(port,42,instruments.throttle,amber);
    divider(port,61);
    const double fuelFraction = fuelCapacity > 0 ? fuel / fuelCapacity : 0.;
    const auto fuelColour = fuelFraction <= .10 ? danger : cabinSignal;
    char fuelReadout[32];
    std::snprintf(fuelReadout,sizeof(fuelReadout),"%.1f / %.0f",fuel,fuelCapacity);
    text(port,"FUEL",27,75,13,fuelColour);
    fitted(port,fuelReadout,172,74,121,14,cabinLamp);
    gauge(port,97,fuelFraction,fuelColour);
    addScreen(portScreen, port);

    CockpitOverlay starboard;
    const std::string_view name = instruments.targetLabel.empty() ? instruments.destination : instruments.targetLabel;
    const bool hasTarget = instruments.targetVisible && glm::length(instruments.localTarget) > 1e-30;
    const bool aft = hasTarget && instruments.localTarget.z > 0;
    constexpr std::string_view wormholePrefix = "WORMHOLE TO ";
    const bool wormhole = hasTarget && name.starts_with(wormholePrefix);
    text(starboard,wormhole ? "WORMHOLE" : "NAVIGATION",27,21,12,cabinSignal);
    rect(starboard.panels,27,40,155,1.5,grid);
    const auto target = wormhole ? name.substr(wormholePrefix.size()) : name.substr(0,name.find(" / "));
    fitted(starboard,hasTarget ? target : "NO TARGET",27,51,154,12,cabinLamp);
    if (hasTarget) {
        quantity(starboard,instruments.range,27,69,114,11,cabinSignal);
        if (aft)
            text(starboard,"AFT",148,69,11,amber);
    }
    text(starboard,"HULL "+std::to_string(int(std::round(instruments.hull)))+"%",27,94,13,
         instruments.hull > 30 ? cabinSignal : danger);

    // The scope is in ship axes, so looking around the cabin leaves the bearing fixed.
    const glm::dvec2 centre{244,64};
    for (double radius : {17.,39.}) {
        for (int tick = 0; tick < 28; ++tick) {
            const double angle = tick * (2*std::numbers::pi / 28);
            const double next = (tick+1) * (2*std::numbers::pi / 28);
            const glm::dvec2 a(std::cos(angle),std::sin(angle)), b(std::cos(next),std::sin(next));
            const auto p = glm::dvec3(centre+radius*a,0), q = glm::dvec3(centre+radius*b,0);
            const auto u = glm::dvec3(centre+(radius-1.3)*a,0), v = glm::dvec3(centre+(radius-1.3)*b,0);
            triangle(starboard.panels,p,q,u,grid);
            triangle(starboard.panels,u,q,v,grid);
        }
    }
    rect(starboard.panels,centre.x-40,centre.y-.55,80,1.1,grid);
    rect(starboard.panels,centre.x-.55,centre.y-40,1.1,80,grid);
    if (hasTarget) {
        const auto direction = glm::normalize(instruments.localTarget);
        const glm::dvec2 bearing(direction.x,-direction.y);
        const auto heading = glm::length(bearing) > 1e-8 ? glm::normalize(bearing) : glm::dvec2(0,1);
        const double polar = std::acos(std::clamp(-direction.z,-1.,1.));
        const auto p = centre + heading*(34*polar/std::numbers::pi);
        const auto colour = aft ? amber : cabinLamp;
        const glm::dvec3 top(p.x,p.y-5,0), bottom(p.x,p.y+5,0);
        triangle(starboard.lettering,top,{p.x-5,p.y,0},bottom,colour);
        triangle(starboard.lettering,top,bottom,{p.x+5,p.y,0},colour);
    }
    addScreen(starboardScreen, starboard);

    CockpitOverlay overhead;
    const auto lamp = [&](int row, std::string_view label, std::string_view value, glm::vec3 colour) {
        const double y = 44 + row*80;
        rect(overhead.panels,34,y,252,59,{.04,.11,.11});
        rect(overhead.panels,29,y,5,59,colour);
        text(overhead,label,47,y+20,19,cabinLamp);
        fitted(overhead,value,169,y+19,110,21,colour);
    };
    lamp(0,"GEAR",instruments.gear ? "DOWN" : "UP",instruments.gear ? cabinSignal : muted);
    lamp(1,"ASSIST",instruments.assist ? "ON" : "OFF",instruments.assist ? cabinSignal : amber);
    const bool departing = instruments.docking.phase == DockingPhase::Departure;
    lamp(2,"DOCK",instruments.docking.docked ? "LOCKED" : departing ? "DEPART" :
         instruments.docking.active ? "APPR" : "FREE",instruments.docking.docked ? cabinSignal :
         instruments.docking.active || departing ? amber : muted);
    addScreen(overheadScreen, overhead,320.);

    displays.compact();
    return displays;
}

CockpitOverlay cockpitOverlay(const FlightInstruments& t,double aspect,std::string_view mode,std::string_view notice,
                              std::span<const CelestialContact> contacts,std::span<const ActivityContact> activity,
                              bool help,double projectionScale,const MarketView* market,glm::dquat head,const CrewView* crew) {
    return cockpitOverlay(t,aspect,mode,notice,contacts,activity,help,projectionScale,market,head,crew,nullptr);
}
CockpitOverlay cockpitOverlay(const FlightInstruments& t, double aspect, std::string_view mode,
                              std::string_view notice, std::span<const CelestialContact> contacts,
                              std::span<const ActivityContact> activity, bool help, double projectionScale,
                              const MarketView* market, glm::dquat headInShip, const CrewView* crew,const ExpeditionPlan* planner,const MicroView* micro) {
    CockpitOverlay h;
    std::optional<CockpitRect> economicPanel;
    std::optional<CockpitRect> crewCommandPanel;
    std::optional<CockpitRect> stationPanel;
    std::optional<CockpitRect> traversePanel;
    const CockpitLayout l(aspect);
    const double w = l.width, center = w * .5;
    const bool hybrid=mode=="XALARIA";
    if(hybrid&&help){
        rect(h.panels,12,12,w-24,876,{.025,.04,.065});
        text(h,"INFINITY / XALARIA",40,48,25,pearl);
        text(h,"FLIGHT AND EXPLORATION",40,103,16,mint);
        const std::array<std::pair<const char*,const char*>,15> rows{{
            {"F2","OPEN OR CLOSE THE WORMHOLE ATLAS"},
            {"ATLAS","TYPE TO SEARCH / TAB NEXT RESULT / CLICK STAR OR WORLD"},
            {"ENTER","FLY THE SELECTED WORMHOLE ROUTE OR LOCAL WORLD"},
            {"F3 / SPACE","GALAXY-SYSTEM / PAUSE OR RESUME JOURNEY"},
            {"W A S D","FORWARD / LEFT / BACK / RIGHT THRUST"},
            {"Q / E","DOWN / UP THRUST"},
            {"ARROWS / Z V","PITCH AND YAW / ROLL"},
            {"C","CAPTURE OR RELEASE MOUSE STEERING"},
            {"HOLD SHIFT + MOUSE","LOOK AROUND THE COCKPIT / RELEASE TO CENTRE"},
            {"LEFT OPTION / CTRL","BOOST / PRECISE THRUST"},
            {"B / X","BRAKE AND CANCEL COURSE / TOGGLE CRUISE"},
            {"Y / J","NEXT PLANET / FLY TO SELECTED PLANET"},
            {"G / L","LANDING GEAR / AUTOMATIC LANDING"},
            {"CTRL-TAB","SHOW OR HIDE CONTACTS"},
            {"F1 / ESC","CLOSE THIS HELP / RELEASE MOUSE OR EXIT"}
        }};
        double y=158;for(const auto& [key,action]:rows){
            fitted(h,key,40,y,230,12,ice);fitted(h,action,290,y,w-330,12,pearl);y+=39;
        }
        fitted(h,"PUBLIC CATALOGUE SNAPSHOT / INFINITY REAL-TIME FLIGHT",40,790,w-80,12,muted);
        return h;
    }

    const auto eyeFromShip = glm::inverse(headInShip);
    const auto aimPoint =
        cockpitTargetMarker(eyeFromShip * (t.gunAim * glm::dvec3(0, 0, -1)), aspect, projectionScale).point;
    const double ratio = std::max(1e-12, t.hullMetres / 12);
    const bool specimen = t.phase == "SPECIMEN INSPECTION";
    const auto focus=cockpitFocus(t);
    const bool microActive=micro&&micro->active&&t.shrinking&&focus.mode==CockpitMode::Surface;
    const bool docking=focus.mode==CockpitMode::Docking;
    const auto landing=t.landing.phase;
    const bool surface=!focus.warp&&(focus.mode==CockpitMode::Landing||focus.mode==CockpitMode::Surface)&&t.altitude>=0;
    const bool modal=help||(market&&market->open)||(crew&&crew->open)||(planner&&planner->open)||(t.vessel&&t.vessel->open)||(t.discovery&&t.discovery->open);
    const bool reconOpen=t.recon&&t.recon->open&&!modal&&!(crew&&crew->flightOpen);
    const bool flightCommands=crew&&crew->flightOpen&&!modal;
    const bool catalogues=t.contactsExpanded&&!modal&&!reconOpen;
    const bool missileAlert=glm::length(t.localMissile)>.5;
    const auto living=std::count_if(contacts.begin(),contacts.end(),hasSurfaceLife);
    const auto alerts=cockpitAlerts(t,crew,notice);
    auto finish=[&](CockpitOverlay view){
        drawAlertChannel(view,alerts,aspect,catalogues&&!flightCommands,focus.mode==CockpitMode::Probe&&!(planner&&planner->open),modal,t,crew);
        return view;
    };
    const auto population = [](uint64_t people) {
        char value[40];
        if (people >= 1000000)
            std::snprintf(value, sizeof(value), "%.1fM", double(people) / 1000000);
        else if (people >= 1000)
            std::snprintf(value, sizeof(value), "%.1fK", double(people) / 1000);
        else
            std::snprintf(value, sizeof(value), "%llu", (unsigned long long)people);
        return std::string(value);
    };
    auto label = [&](std::string_view s, double x, double y, double width, glm::vec3 color = muted) {
        fitted(h, s, x, y, width, 10.5, color);
    };
    auto quantity = [&](std::string_view s, double x, double y, double width, double size = 14,
                        glm::vec3 color = pearl) {
        // Unit case is intentional: mm and m must remain distinguishable.
        size = std::min(size, width * 7 / (6 * std::max(size_t(1), s.size())));
        fitted(h, s, x, y, width, size, color, true);
    };
    if (t.phase == "MINING PROBE CAMERA" && !(planner&&planner->open)) {
        card(h,{24,28,w-48,94},ice);
        quantity("MINING PROBE / LIVE FEED",38,43,w-76,18,pearl);
        label(t.exploration[0],38,79,w-76,amber);
        if(planner&&planner->tracking)fitted(h,planner->compact,38,101,w-76,8.5,mint);
        card(h,{24,772,w-48,102},ice);
        quantity(t.exploration[1],38,788,w-76,17,mint);
        label(t.exploration[2],38,821,w-76,pearl);
        label(t.exploration[3],38,849,w-76,ice);
        for (unsigned k=0;k<4;++k) {
            const double a=k*1.5707963267948966;
            line(h.strokes,center+std::cos(a)*9,450+std::sin(a)*9,
                           center+std::cos(a)*21,450+std::sin(a)*21,ice);
        }
        return finish(std::move(h));
    }
    char value[96];
    rect(h.panels,0,0,w,46,ink,.98f);h.hitRegions.push_back({0,0,w,46});
    text(h,"INFINITY",26,15,17.5,pearl);
    fitted(h,specimen?focus.title:microActive?(micro->atomic?"ATOMIC FLIGHT":micro->address.kind()==Kind::Grain?"MINERAL FLIGHT":"MICRO FLIGHT"):focus.title,center-160,17,320,13,focus.mode==CockpitMode::Combat?danger:ice);
    const auto headerHint=focus.mode==CockpitMode::Combat&&!t.crewFire.empty()?t.crewFire.substr(0,t.crewFire.find(" / "))+" / 6 7 8":t.contactsExpanded?"CTRL-TAB CLOSE CONTACTS":"CTRL-TAB CONTACTS";
    fitted(h,headerHint,w-264,20,240,8.5,muted);
    line(h.strokes,24,45,w-24,45,muted*.3f);
    const bool changingScale=t.shrinking||std::abs(t.scaleRate)>.02;
    const CockpitRect scale{24,64,274,changingScale?76.:58.};card(h,scale,ice);
    fitted(h,specimen?"SPECIMEN / SHIP PARKED":t.shrinking?"SHRINK FIELD":"SHIP / FULL SIZE",38,75,246,9,muted);
    quantity(flightQuantity(t.hullMetres),38,94,246,21,pearl);
    if(changingScale){
        const auto note=t.shrinking?"FRAGILE X"+std::to_string(int(std::lround(shipDamageMultiplier(ratio))))+" / TARGET "+flightQuantity(t.targetHullMetres):
            "VIEW "+flightQuantity(std::pow(10.,t.logMetres))+(t.scaleRate<0?" / REFINING":" / WIDENING");
        quantity(note,38,123,246,9.5,t.shrinking?amber:ice);
    }
    const bool showClearance=(t.altitude>=0||microActive)&&!focus.warp&&(focus.mode==CockpitMode::Approach||focus.mode==CockpitMode::Landing||focus.mode==CockpitMode::Surface);
    if(showClearance){
        card(h,{w-310,64,286,92},ice);
        label(microActive?(micro->atomic?"LOCAL ATOM GAP":"SURFACE ESTIMATE"):t.phase=="CAVERN EXPLORATION"?"WALL CLEARANCE":t.shrinking?"PARENT TERRAIN / AGL":"ALTITUDE / AGL",w-296,76,258);
        const auto proximity=microActive?(!micro->proximityKnown?"SAMPLING...":micro->insideMaterial?"IN MATERIAL":
            (micro->atomic?std::string{}:std::string("~ "))+flightQuantity(micro->proximityMetres)):flightQuantity(t.landed?0.:std::max(0.,t.altitude));
        quantity(proximity,w-296,96,258,28,microActive&&micro->insideMaterial?amber:pearl);
        quantity(microActive?(micro->atomic?"TO HULL / LOCAL RESOLVED ATOMS":"APPROXIMATE MATERIAL PROXIMITY"):
            std::string(t.descent<0?"UP ":"DOWN ")+flightQuantity(std::abs(t.descent),true)+" / DRIFT "+flightQuantity(t.drift,true),w-296,135,258,9.5,ice);
    }
    const bool weatherUseful=!catalogues&&!focus.warp&&!t.weather.empty()&&(focus.mode==CockpitMode::Surface||focus.mode==CockpitMode::Landing||t.weatherSeverity>.35||t.environmentalHeat>.2||t.electricalCharge>.2);
    if(weatherUseful){
        const CockpitRect weather{w-310,showClearance?164.:64.,286,t.weatherDetail.empty()?42.:58.};card(h,weather,ice);
        const auto lines=wrapped(t.weather,258,9.5,2);
        for(size_t i=0;i<lines.size();++i)fitted(h,lines[i],w-296,weather.y+10+i*13,258,9.5,ice);
        if(!t.weatherDetail.empty())fitted(h,t.weatherDetail,w-296,weather.y+weather.h-15,258,8.5,muted);
    }
    const auto& nav=l.navigation;const auto& flight=l.flight;const auto& scope=l.scope;const auto& systems=l.systems;
    for(const auto panel:{nav,flight,scope,systems})card(h,panel,ice);
    label(focus.mode==CockpitMode::Combat?"TARGET":focus.mode==CockpitMode::Surface?"WORKSITE":"DESTINATION",nav.x+14,nav.y+12,nav.w-28,ice);
    const auto destination=microActive?micro->name:cockpitDestination(t);
    const double nameSize=destination.size()*18*6/7.<=nav.w-28?18.:13.;
    const auto destinationLines=wrapped(destination,nav.w-28,nameSize,2);
    for(size_t i=0;i<destinationLines.size();++i)fitted(h,destinationLines[i],nav.x+14,nav.y+34+i*17,nav.w-28,nameSize,pearl);
    const auto range=microActive?"FIELD "+flightQuantity(micro->fieldMetres):t.targetVisible||t.navigating?flightQuantity(t.range):std::string("SELECT WITH Y / K");
    const auto eta=!microActive&&t.etaSeconds>=0&&t.etaSeconds<10000?" / ETA "+std::to_string(int(std::ceil(t.etaSeconds)))+"S":"";
    quantity(range+eta,nav.x+14,nav.y+73,nav.w-28,12,ice);
    const auto action=hybrid?(t.destination.starts_with("WORMHOLE TO ")?"W / FLY THROUGH THE RING":"J FLY TO WORLD / F2 ATLAS"):focus.nextAction;
    fitted(h,action,nav.x+14,nav.y+105,nav.w-28,10.5,ice);

    const bool warp=focus.warp;
    label(warp?"WARP VELOCITY":t.relativeMotion?"RELATIVE SPEED":"VELOCITY",flight.x+14,flight.y+12,flight.w-28,ice);
    constexpr double lightSpeed=299792458.;
    auto velocity=flightQuantity(t.relativeMotion&&!warp?t.relativeSpeed:t.speed,true);
    if(warp&&t.speed>=lightSpeed*.1){const double c=t.speed/lightSpeed;std::snprintf(value,sizeof(value),c<10000?"%.1f C":"%.2e C",c);velocity=value;}
    quantity(velocity,flight.x+14,flight.y+34,flight.w-28,30,pearl);
    const auto brakeTime=t.brakingSeconds<10000?std::to_string(int(std::ceil(t.brakingSeconds)))+"S":"--";
    quantity(warp?"LIMIT "+flightQuantity(t.driveSpeed,true):"STOP "+flightQuantity(t.stoppingDistance)+" / "+brakeTime,
             flight.x+14,flight.y+76,flight.w-28,10.5,ice);
    std::snprintf(value,sizeof(value),"%s / %.1fG",t.assist?"ASSIST":"INERTIAL",std::min(999.,t.properG));
    fitted(h,warp?(t.warp==WarpPhase::Drop?"RETURNING TO NORMAL SPACE":"N DROP / B ABORT"):value,flight.x+14,flight.y+105,flight.w-28,9.5,muted);

    const double radarX=scope.x+scope.w*.5,radarY=scope.y+73;
    const double radarRadius=std::min(90.,(scope.w-28)*.5);
    const bool landingDirector=focus.mode==CockpitMode::Landing;
    const bool dockingDirector=focus.mode==CockpitMode::Docking;
    label(microActive?"REFERENCE CELL":dockingDirector?"DOCKING DIRECTOR":landingDirector?"LANDING DIRECTOR":focus.mode==CockpitMode::Combat?"TACTICAL SCOPE":focus.mode==CockpitMode::Surface?"LOCAL SURVEY":"NAVIGATION SCOPE",scope.x+14,scope.y+12,scope.w-28,ice);
    if(!microActive)for(double r:{.5,1.})for(int i=0;i<48;++i){const double a=i*6.283185307/48,b=(i+1)*6.283185307/48;
        line(h.strokes,radarX+std::cos(a)*radarRadius*r,radarY+std::sin(a)*23*r,radarX+std::cos(b)*radarRadius*r,radarY+std::sin(b)*23*r,muted*float(r==1?.65:.22));}
    auto scopePoint=[&](glm::dvec3 p,glm::vec3 color,bool selected){
        const auto base=glm::dvec2(radarX+p.x*radarRadius*.85,radarY+p.z*20);const auto tip=base-glm::dvec2(0,p.y*17);
        line(h.strokes,base.x,base.y,tip.x,tip.y,color*.5f);diamond(h.strokes,tip,selected?5.:2.5,color);
    };
    if(microActive){
        fitted(h,"SCHEMATIC / "+micro->phase,scope.x+14,scope.y+32,scope.w-28,8.5,muted);
        const auto cellRotation=glm::angleAxis(-.42,glm::dvec3(1,0,0))*glm::angleAxis(.62,glm::dvec3(0,1,0));
        const auto projectCell=[&](glm::vec3 p){const auto q=cellRotation*glm::dvec3(p);
            return glm::dvec2(radarX+q.x*30,radarY-q.y*27);};
        for(unsigned i=0;i<8;++i)for(unsigned axis=0;axis<3;++axis)if(!(i&(1u<<axis))){
            const auto a=projectCell(micro->cellCorners[i]),b=projectCell(micro->cellCorners[i|(1u<<axis)]);
            line(h.strokes,a.x,a.y,b.x,b.y,ice*.55f);}
        for(const auto& site:micro->cellSites)diamond(h.strokes,projectCell(site.position),2.5,elementColor(site.element));
        fitted(h,micro->atomic?(micro->atomsKnown?std::to_string(micro->atoms)+" ATOMS / "+std::to_string(micro->bonds)+" LINKS":"LOCAL ATOMS LOADING"):
            "CELL "+flightQuantity(micro->cellMetres),scope.x+14,scope.y+105,scope.w-28,9,ice);
    }else if(dockingDirector){const auto& d=t.docking;const bool aligned=d.heading<.045&&std::abs(d.roll)<.06;
        diamond(h.strokes,{radarX+std::clamp(d.offset.x/std::max(1.,d.width),-1.,1.)*radarRadius*.78,radarY-std::clamp(d.offset.y/std::max(1.,d.width*.52),-1.,1.)*21},5,aligned?mint:ice);
        const double angle=std::clamp(d.roll,-1.4,1.4);line(h.strokes,radarX-19*std::cos(angle),radarY+19*std::sin(angle),radarX+19*std::cos(angle),radarY-19*std::sin(angle),ice);
        fitted(h,d.docked?"CLAMPS SECURE":dockingPhaseName(d.phase),scope.x+14,scope.y+32,scope.w-28,11,mint);
        quantity("LATERAL "+flightQuantity(glm::length(glm::dvec2(d.offset)))+" / "+flightQuantity(d.closing,true),scope.x+14,scope.y+105,scope.w-28,9.5,ice);
    }else if(landingDirector){
        const double shift=std::clamp(t.pitch*38,-18.,18.);const bool aligned=t.safe&&(!t.landing.body||glm::length(t.landing.localOffset)<6*ratio);
        for(int side:{-1,1}){const double a=side*12.,b=side*radarRadius*.8;line(h.strokes,radarX+a*std::cos(t.roll),radarY+shift-a*.28*std::sin(t.roll),radarX+b*std::cos(t.roll),radarY+shift-b*.28*std::sin(t.roll),aligned?mint:ice);}
        diamond(h.strokes,{radarX,radarY},4,pearl);
        if(t.landing.body&&!t.landed){const double scale=std::max(12*ratio,std::max(0.,t.altitude)*.9);const auto offset=t.landing.localOffset/scale;
            diamond(h.strokes,{radarX+std::clamp(offset.x,-1.,1.)*radarRadius*.82,radarY-std::clamp(offset.y,-1.,1.)*21},8,aligned?mint:ice);}
        fitted(h,t.landed?"TOUCHDOWN SECURE":landing==LandingPhase::Flare?"FLARE":aligned?"ALIGNED":"MATCH THE HORIZON",scope.x+14,scope.y+32,scope.w-28,10.5,ice);
        std::snprintf(value,sizeof(value),"ROLL %.0f / PITCH %.0f",t.roll*57.2957795,t.pitch*57.2957795);
        fitted(h,value,scope.x+14,scope.y+105,scope.w-28,9.5,muted);
    }else{
        size_t contextDots=0;
        for(const auto& c:activity)if(!c.occluded&&(c.selected||c.body.hostile||contextDots++<18))scopePoint(c.localDirection,c.body.hostile?danger:c.selected?ice:muted*.6f,c.selected);
        contextDots=0;for(const auto& c:contacts)if(!c.occluded&&!c.selected&&contextDots++<8)scopePoint(c.localDirection,hasSurfaceLife(c)?mint*.6f:muted*.45f,false);
        if(t.targetVisible&&glm::length(t.localTarget)>1e-30)scopePoint(glm::normalize(t.localTarget),ice,true);
        line(h.strokes,radarX,radarY-5,radarX-4,radarY+4,mint);line(h.strokes,radarX-4,radarY+4,radarX+4,radarY+4,mint);line(h.strokes,radarX+4,radarY+4,radarX,radarY-5,mint);
        fitted(h,warp?(t.warp==WarpPhase::Drop?"DESTINATION REVEAL":t.warpDestination?"DESTINATION LOCKED":"MANUAL COURSE"):
            focus.mode==CockpitMode::Surface?(t.shrinking?"MICRO / NANO":"F5 TERRAIN MAP"):t.targetVisible?(t.localTarget.z>=0?"DESTINATION AFT":"DESTINATION AHEAD"):"FORWARD",scope.x+14,scope.y+32,scope.w-28,10,ice);
        if(warp&&t.warpExitSeconds>=0)quantity(t.warpExitSeconds>86400?"IN TRANSIT":"DROP ~"+std::to_string(int(std::ceil(t.warpExitSeconds)))+"S",scope.x+14,scope.y+105,scope.w-28,10.5,mint);
        else fitted(h,focus.mode==CockpitMode::Combat?"H SELECT / SPACE FIRE":focus.mode==CockpitMode::Surface?"F12 SCAN / F11 JOURNAL":"CTRL-TAB CONTACTS",scope.x+14,scope.y+105,scope.w-28,9,muted);
    }
    const double gaugeWidth=(systems.w-42)*.5;
    for(int i=0;i<2;++i){const double x=systems.x+14+i*(gaugeWidth+14);
        label(i?"HULL":"THROTTLE",x,systems.y+12,gaugeWidth,ice);
        std::snprintf(value,sizeof(value),"%.0f%%",i?t.hull:t.throttle*100);
        quantity(value,x,systems.y+34,gaugeWidth,29,i&&t.hull<25?danger:pearl);
        bar(h.panels,x,systems.y+73,gaugeWidth,i?t.hull/100:t.throttle,i?(t.hull<25?danger:mint):ice);
    }
    if(market)quantity(std::to_string(market->credits/100)+" CR / "+std::to_string(market->cargoKg)+" KG",systems.x+14,systems.y+90,systems.w-28,9.5,muted);
    const auto systemHint=focus.mode==CockpitMode::Landing||focus.mode==CockpitMode::Surface?(t.gear?"G GEAR DOWN":"G GEAR UP"):"F9 CREW / F8 DECK";
    fitted(h,hybrid?"B BRAKE / X CRUISE":systemHint,systems.x+14,systems.y+108,systems.w-28,9.5,ice);
    rect(h.panels,0,884,w,16,ink);h.hitRegions.push_back({0,884,w,16});
    fitted(h,"F1 HELP",26,888,90,8.5,muted);
    fitted(h,hybrid?"F2 ATLAS / Y NEXT WORLD / J NAVIGATE":"F2 TRADE / F3 PLAN / F5 MAP / F9 CREW",std::max(130.,center-220),888,std::max(100.,w-440),8,muted);
    fitted(h,hybrid?"CTRL-TAB CONTACTS":"CTRL-TAB CONTACTS / TAB HUD",w-245,888,221,8,muted);
    if(t.discovery&&(t.discovery->monitoring||t.discovery->calls||t.discovery->ships)&&!t.discovery->open&&!modal&&!reconOpen&&!t.contactsExpanded&&!(crew&&crew->flightOpen)&&!microActive&&!specimen)drawDiscoveryMonitor(h,*t.discovery);
    if(t.awareness&&t.awareness->active&&!modal&&!reconOpen&&!t.contactsExpanded&&!flightCommands&&!microActive&&!specimen){
        const bool radio=t.discovery&&(t.discovery->monitoring||t.discovery->calls||t.discovery->ships);
        drawDefenceCard(h,*t.awareness,radio?334:276);
    }
    if(crewMinimapVisible(t,crew,modal||reconOpen)){drawCrewMinimap(h,*crew,aspect);drawEngineering(h,*crew,aspect);}
    if(planner&&planner->tracking&&!planner->open&&!help&&!(crew&&(crew->open||crew->flightOpen))&&!(market&&market->open)){
        const CockpitRect progress{center-205,646,410,22};card(h,progress,ice);fitted(h,planner->compact,progress.x+12,progress.y+7,progress.w-24,8.5,mint);
    }
    if(planner&&planner->open) {
        const auto& plan=*planner;const PlannerLayout layout(aspect,plan.details,&plan);const auto p=layout.panel,a=layout.left,b=layout.right;
        occlude(h,p);card(h,p,mint);drawPlannerDiagrams(h,plan,layout);
        if(!plan.details){
            const auto summary=summarizePlan(plan);
            fitted(h,"EXPEDITION PLANNER",p.x+24,p.y+19,p.w-230,12,pearl);
            const auto phase=(summary.step?std::to_string(summary.step)+"/10 / ":"")+std::string(planStageName(plan.stage));
            fitted(h,phase,p.x+p.w-204,p.y+22,180,8.7,mint);
            const auto destination=plan.returnToPort||plan.stage==PlanStage::Deliver||plan.stage==PlanStage::Complete?plan.issuerName:plan.title;
            fitted(h,plan.contract?destination:plan.stage==PlanStage::Loading?"FINDING LOCAL CONTRACTS":"NO LOCAL ASSAY OFFERS",p.x+60,p.y+53,p.w-84,16,pearl);
            fitted(h,plan.reward,p.x+60,p.y+80,p.w-84,8.4,muted);
            const auto lines=wrapped(summary.instruction,p.w-48,10,3);
            for(size_t i=0;i<lines.size();++i)fitted(h,lines[i],layout.body.x,layout.body.y+i*13,layout.body.w,10,ice);
            if(plan.debrief){const auto& invoice=plan.accounting;const auto r=layout.debrief;
                if(invoice.recorded){
                    const auto net=invoice.netCash();
                    fitted(h,"NET CASH / "+crewMoney(net)+" CR",r.x,r.y,r.w,13,net>=0?mint:amber);
                    fitted(h,"TOOLS "+crewMoney(invoice.equipment)+" / PROBES "+crewMoney(invoice.ammunition)+" / PAID REPAIRS "+crewMoney(invoice.repairs)+" CR",r.x,r.y+21,r.w,8.2,muted);
                    fitted(h,std::to_string(invoice.probesUsed)+" PROBES USED / "+std::to_string(invoice.extractedKg)+" KG MINED / RETURN HULL "+std::to_string(int(invoice.returnHull))+"%",r.x,r.y+37,r.w,8.2,invoice.returnHull<invoice.departureHull?amber:ice);
                }else fitted(h,"COST HISTORY UNAVAILABLE FOR THIS OLDER EXPEDITION",r.x,r.y,r.w,8.3,muted);
            }
            auto context=summary.context;bool warning=summary.warning;
            if(plan.visual.shipMetres<.012&&plan.stage!=PlanStage::Loading){context="SHIP / "+flightQuantity(plan.visual.shipMetres)+" / FRAGILE";warning=true;}
            fitted(h,context,layout.status.x,layout.status.y,layout.status.w,8.8,warning?amber:mint);
            card(h,layout.actions[0],plan.primaryEnabled?mint:muted);
            const auto action=plan.primary==PlannerAction::Resume?std::string("RETURN TO FLIGHT"):plan.primaryLabel;
            fitted(h,action+" / ENTER",layout.actions[0].x+12,layout.actions[0].y+12,layout.actions[0].w-24,9,plan.primaryEnabled?pearl:muted);
            card(h,layout.details,ice);fitted(h,"DETAILS / D",layout.details.x+12,layout.details.y+12,layout.details.w-24,8.3,ice);
            const auto receipt=wrapped(plan.busy?"AWAITING SAVED RECEIPT":plan.debrief&&plan.message.starts_with("CAVERN ASSAY COMPLETE")?"":plan.message,p.w-48,8.5,2);
            for(size_t i=0;i<receipt.size();++i)fitted(h,receipt[i],layout.receipt.x,layout.receipt.y+i*12,layout.receipt.w,8.5,amber);
            fitted(h,"UP/DOWN CONTRACT / F4 TARGET",p.x+24,layout.footerY,p.w-150,7.3,muted);
            fitted(h,"ESC CLOSE",layout.close.x,layout.close.y+3,layout.close.w,7.3,muted);
            return finish(std::move(h));
        }
        fitted(h,"EXPEDITION PLANNER",p.x+18,128,p.w-290,17.5,pearl);
        fitted(h,planStageName(plan.stage),p.x+p.w-252,131,234,10.5,mint);
        fitted(h,plan.details?"UP-DOWN CONTRACT / B CAVERN / N NEW SITE":"UP / DOWN CONTRACT",p.x+18,157,p.w-170,8,muted);
        card(h,layout.details,ice);fitted(h,plan.details?"SUMMARY / D":"DETAILS / D",layout.details.x+9,layout.details.y+7,layout.details.w-18,8,ice);
        fitted(h,"CLOSE / ESC",layout.close.x,layout.close.y+3,layout.close.w,7.5,muted);
        for(size_t row=0;row<(plan.details?layout.offers.size():size_t(1))&&plan.firstOffer+row<plan.offers.size();++row){
            const size_t i=plan.firstOffer+row;const auto& offer=plan.offers[i];const auto r=layout.offers[row];
            const bool selected=i==plan.selected;card(h,r,selected?mint:muted);
            const auto phase=offer.phase==SurveyPhase::Open?"OPEN":offer.phase==SurveyPhase::Accepted?"ACTIVE":offer.phase==SurveyPhase::Complete?"PAID":"CLOSED";
            fitted(h,std::string(selected?"+ ":"  ")+phase+" / "+offer.title,r.x+6,r.y+7,r.w-12,8.5,selected?pearl:muted);
        }
        fitted(h,"WORLD / "+plan.title,a.x+25,plan.details?293:237,a.w-25,10.5,mint);
        fitted(h,plan.geology,a.x,plan.details?315:253,plan.details?a.w*.58:a.w,8.2,pearl);
        if(plan.details)fitted(h,plan.conditions,a.x+a.w*.60,315,a.w*.40,8.2,ice);
        const auto& visual=plan.visual;
        const double diagramY=layout.section.y;
        fitted(h,visual.hasBore?"SAVED EXCAVATION":"PLANNED SECTION",a.x+9,diagramY+8,a.w*.64,8,visual.hasBore?mint:muted);
        fitted(h,"SCHEMATIC",a.x+a.w*.73,diagramY+8,a.w*.24,7.2,muted);
        const double keyX=a.x+a.w*.71,keyWidth=a.w*.27;
        fitted(h,std::to_string(int(std::lround(visual.boreDepth)))+" M TARGET",keyX,diagramY+57,keyWidth,8,ice);
        fitted(h,visual.hasBore?std::to_string(int(std::lround(visual.reached)))+" M CUT":"UNSCANNED",keyX,diagramY+77,keyWidth,8,visual.hasBore?pearl:muted);
        if(visual.chamberReady){const auto& deposit=visual.deposits[std::min(plan.find,cavernFindCount-1)];
            fitted(h,deposit.grade?"ASSAY "+std::to_string(deposit.grade):"UNSCANNED",keyX,diagramY+99,keyWidth,8,deposit.grade?mint:muted);
            if(plan.details||deposit.marked)fitted(h,deposit.marked?"MARKED":"UNMARKED",keyX,diagramY+119,keyWidth,8,deposit.marked?mint:muted);
        }else if(visual.hasBore){
            fitted(h,"HEAT "+std::to_string(int(std::lround(visual.heat*100)))+"%",keyX,diagramY+99,keyWidth,8,visual.heat>.65?amber:ice);
            if(plan.details||visual.phase==ProbePhase::Paused)fitted(h,probePhaseName(visual.phase),keyX,diagramY+119,keyWidth,7.5,ice);
        }
        const auto shipSize=flightQuantity(visual.shipMetres)+(visual.shipMetres<.012?" / FRAGILE":visual.shipMetres>=11.88?" / FULL SIZE":" / SHRUNK");
        fitted(h,"SHIP / "+shipSize,a.x+9,488,a.w*.62,7.7,visual.shipMetres<.012?amber:mint);
        fitted(h,visual.hasBore?"LOCAL RECORD":"BULK ESTIMATE",a.x+a.w*.64,488,a.w*.34,7.2,muted);
        const std::array materialNames{"ICE","ORE","SIL","CAR"};
        const std::array<glm::vec3,4> materialColors{ice,amber,trafficInk,muted};
        if(plan.details)for(unsigned i=0;i<materialNames.size();++i)fitted(h,std::string(materialNames[i])+" "+std::to_string(int(std::lround(visual.composition[i]*100)))+"%",a.x+i*a.w/4,523,a.w/4-4,7.3,materialColors[i]);
        else fitted(h,"BULK MATERIALS / ESTIMATE",a.x,525,a.w,7.3,muted);
        fitted(h,plan.reward,a.x,plan.details?541:213,a.w,8.8,pearl);
        if(plan.details)fitted(h,"ISSUER / "+plan.issuerName,a.x,555,a.w,7.3,muted);
        const std::array names{"DEPTH / ARROWS","FEED / SHIFT-ARROWS","BAY / CTRL-ARROWS"};
        const std::array values{std::to_string(int(plan.depth))+" M",std::to_string(int(std::lround(plan.feed*100)))+"%",std::string(weaponMountName(plan.bay))};
        if(plan.details)for(size_t i=0;i<names.size();++i){
            fitted(h,names[i],b.x,189+i*28,b.w-174,8.2,ice);
            fitted(h,values[i],b.x+b.w-166,188+i*28,100,9.5,pearl);
            for(const auto r:{layout.minus[i],layout.plus[i]})card(h,r,ice);
            fitted(h,"-",layout.minus[i].x+8,layout.minus[i].y+7,14,9,ice);
            fitted(h,"+",layout.plus[i].x+8,layout.plus[i].y+7,14,9,ice);
        }
        fitted(h,"NEXT",b.x,plan.details?285:234,b.w,9,mint);
        fitted(h,plan.objective,b.x,plan.details?307:256,b.w,plan.details?11.5:13,pearl);
        const auto instructions=wrapped(plan.instruction,b.w,8.6,3);
        for(size_t i=0;i<instructions.size();++i)fitted(h,instructions[i],b.x,(plan.details?330:283)+i*12,b.w,8.6,ice);
        fitted(h,"SHIP / TOOL STATIONS",b.x,373,b.w*.46,8,ice);
        for(unsigned i=0;i<layout.bays.size();++i){const auto r=layout.bays[i];const auto& station=visual.bays[i];
            const std::array bayNames{"NOSE","PORT","STARBOARD"};
            fitted(h,bayNames[i],r.x+7,r.y+5,r.w-17,7.3,i==plan.bay?ice:muted);
            const auto tool=station.tool==PlannerTool::Probe?"PROBE / "+std::to_string(station.rounds):station.tool==PlannerTool::Miner?"MINER "+std::to_string(station.level):station.tool==PlannerTool::Gun?"GUN "+std::string(weaponGrade(station.level)):"EMPTY BAY";
            fitted(h,tool,r.x+7,r.y+51,r.w-14,7.9,station.tool==PlannerTool::Empty?muted:pearl);
            const bool staffed=station.operatorReadiness>=(station.tool==PlannerTool::Probe?.25:.0001);
            fitted(h,station.tool==PlannerTool::Empty?"UNFITTED":station.captain?"CAPTAIN":staffed?"CREW READY":"NEEDS CREW",r.x+20,r.y+65,r.w-28,6.8,staffed?mint:amber);
        }
        auto ready=[&](unsigned i){return i<plan.checks.size()&&plan.checks[i].ready;};const double half=(b.w-8)*.5;
        if(plan.details){fitted(h,ready(3)?"OPERATORS READY":"ASSIGN CREW / F9",b.x+18,481,half-22,7.8,ready(3)?mint:amber);
            fitted(h,ready(4)?"SAMPLE SPACE / OK":"HOLD FULL",b.x+half+26,481,half-22,7.8,ready(4)?mint:amber);
            fitted(h,ready(5)?"FULL SIZE / HULL OK":"CHECK HULL / SIZE",b.x+18,504,half-22,7.8,ready(5)?mint:amber);
            fitted(h,ready(6)?"NAV / ENG READY":"NAV / ENG OPTIONAL",b.x+half+26,504,half-22,7.8,ready(6)?mint:muted);
        }else{const std::array labels{"CREW","HOLD","SHIP","NAV"};const double width=(b.w-24)/4;
            for(unsigned i=0;i<labels.size();++i)fitted(h,labels[i],b.x+i*(width+8)+17,481,width-21,7.5,ready(i+3)?mint:i==3?muted:amber);}
        fitted(h,plan.stage==PlanStage::Complete?"SAMPLE DELIVERED":std::to_string(int(visual.sampleKg))+" / 10 KG SAMPLE ABOARD",b.x+124,530,b.w-124,8,mint);
        fitted(h,plan.details?"R / SUGGESTED "+std::to_string(int(plan.suggestedDepth))+" M / "+std::to_string(int(std::lround(plan.suggestedFeed*100)))+"% FEED":"NEXT PROBE / "+std::to_string(int(plan.depth))+" M / "+std::to_string(int(std::lround(plan.feed*100)))+"% FEED",b.x,552,b.w,8,ice);
        const std::array routeNames{"PREPARE","TRAVEL","DRILL","ENTER","SCAN","SHRINK","MARK","REGROW","MINE","RETURN"};
        for(unsigned i=0;i<routeNames.size();++i){const auto r=layout.steps[i];const auto& step=plan.steps[i];const double width=std::string_view(routeNames[i]).size()*8*6/7.;
            fitted(h,routeNames[i],r.x+(r.w-width)*.5,615,r.w-4,8,step.done?mint:step.active?pearl:muted);}
        const std::array buttons{plan.primaryLabel,std::string("TARGET OBJECTIVE / F4"),std::string("SUPPLIES / F2"),
            std::string("CREW / F9"),std::string(plan.tracking?"HIDE TRACKER / T":"TRACK CONTRACT / T"),std::string("USE SUGGESTED / R")};
        for(size_t i=0;i<buttons.size();++i){const auto r=layout.actions[i];if(r.w<=0)continue;const bool enabled=i?true:plan.primaryEnabled;
            card(h,r,enabled?i?ice:mint:muted);fitted(h,buttons[i],r.x+10,r.y+10,r.w-20,8.5,enabled?pearl:muted);}
        fitted(h,plan.busy?"AWAITING FLIGHT COMPUTER / SAVED RECEIPT":plan.message,p.x+18,plan.details?728:670,p.w-150,8.3,amber);
        return finish(std::move(h));
    }
    if (crew && crew->open) {
        const CrewPanelLayout layout(aspect);
        const auto p = layout.panel;
        const double map = p.w * .35, rx = p.x + map + 20, rw = p.w - map - 40;
        occlude(h,p);card(h, p, mint);
        fitted(h, "SHIP CREW / STATIONS AND DAMAGE CONTROL", p.x + 18, p.y + 15, p.w - 36, 15,
               pearl);
        fitted(h, crew->summary + " / AUTO " + (crew->autoDamageControl ? "ON" : "OFF"), p.x + 18,
               p.y + 39, p.w - 36, 9.5, ice);
        auto portEdge = [](const CockpitRect& r, glm::dvec2 toward) {
            const glm::dvec2 centre{r.x + r.w * .5, r.y + 48};
            const auto d = toward - centre;
            const double tx = std::abs(d.x) > 1e-9 ? ((d.x > 0 ? r.x + r.w : r.x) - centre.x) / d.x : 1e10;
            const double ty = std::abs(d.y) > 1e-9 ? ((d.y > 0 ? r.y + r.h : r.y) - centre.y) / d.y : 1e10;
            return centre + d * std::min(tx, ty);
        };
        for (auto [a, b] : crewCorridors()) {
            const auto ra = layout.rooms[a], rb = layout.rooms[b];
            const auto x = portEdge(ra, {rb.x + rb.w * .5, rb.y + 48}),
                       y = portEdge(rb, {ra.x + ra.w * .5, ra.y + 48});
            line(h.strokes, x.x, x.y, y.x, y.y, muted * .7f);
        }
        for (size_t i = 0; i < crewRoomCount; ++i) {
            const auto r = layout.rooms[i];
            const auto& room = crew->rooms[i];
            const auto colour = room.fire > 1         ? glm::vec3(1, .3, .12)
                                : room.oxygen < 25    ? amber
                                : room.integrity < 70 ? trafficInk
                                                      : ice;
            card(h, r, crew->station == CrewStation(i) ? mint : colour);
            fitted(h, crewStationName(CrewStation(i)), r.x + 5, r.y + 6, r.w - 10, 8, pearl);
            fitted(h,
                   "SYS " + std::to_string(int(room.integrity)) + " O2 " + std::to_string(int(room.oxygen)),
                   r.x + 5, r.y + 20, r.w - 10, 7.6, colour);
            fitted(h,
                   room.vented
                       ? "VENT OPEN"
                       : "F" + std::to_string(int(room.fire)) + " B" + std::to_string(int(room.breach)),
                   r.x + 5, r.y + 33, r.w - 10, 7.6, colour);
        }
        for (size_t i = 0; i < crew->members.size(); ++i) {
            const auto& m = crew->members[i];
            const auto row = layout.roster[i];
            const auto c = m.health <= 0 ? muted : m.health < 40 ? amber : i == crew->selected ? mint : pearl;
            if (i == crew->selected)
                rect(h.panels, row.x, row.y, row.w, row.h, mint * .16f);
            fitted(h, m.name + " / " + std::string(crewDutyName(m)), row.x + 6, row.y + 4,
                   row.w - 12, 9, c);
            const std::string state = m.health <= 0   ? "LOST"
                                      : m.travel > 0  ? "MOVING"
                                      : m.health < 20 ? "INCAPACITATED"
                                                      : std::string(crewOrderName(m.order));
            fitted(h,
                   state + " / HP " + std::to_string(int(m.health)) + " FAT " +
                       std::to_string(int(m.fatigue)) + " MOR " + std::to_string(int(m.morale)),
                   row.x + 6, row.y + 17, row.w - 12, 8, ice);
            const auto from = layout.rooms[size_t(m.room)], to = layout.rooms[size_t(m.walkTo)];
            const double blend = m.travelDuration > 0 && m.travel > 0 ? 1 - m.travel / m.travelDuration : 0;
            const double x = glm::mix(from.x + from.w * .5, to.x + to.w * .5, blend) + double(i % 3) * 5 - 5;
            const double y = glm::mix(from.y + 48, to.y + 48, blend);
            rect(h.panels, x, y, 4, 4, c);
        }
        fitted(h, "TARGET " + std::string(crewStationName(crew->station)), rx, p.y + 323, rw, 10,
               mint);
        fitted(h, "TRAIT / B N W E L M RANKS: " + crew->selectedSkill, rx, p.y + 342, rw, 8.5, ice);
        fitted(h,
               "STORES / " + std::to_string(int(crew->spares)) + " SPARES / " +
                   std::to_string(int(crew->medical)) + " MEDICAL",
               p.x + 18, p.y + 365, p.w - 36, 9.5, pearl);
        fitted(h, crew->port, p.x + 18, p.y + 385, p.w - 36, 8.5, crew->connected ? mint : muted);
        fitted(h, crew->recruit, p.x + 18, p.y + 403, p.w - 36, 8.5, ice);
        const bool trading = !crew->sell.empty();
        if (trading)
            fitted(h, crew->sell, p.x + 18, p.y + 421, p.w - 36, 8.5, mint);
        fitted(h, crew->supply, p.x + 18, p.y + (trading ? 439 : 421), p.w - 36, 8.5, ice);
        fitted(h, crew->care, p.x + 18, p.y + (trading ? 457 : 439), p.w - 36, 8.5, ice);
        fitted(h, crew->pending ? "PORT ORDER PENDING" : crew->message, p.x + 18,
               p.y + (trading ? 478 : 459), p.w - 36, 9, amber);
        if (!crew->events.empty() && !trading)
            fitted(h, crew->events.back(), p.x + 18, p.y + 478, p.w - 36, 8.5, trafficInk);
        fitted(h, "CLICK CREW THEN ROOM / UP-DOWN CREW / LEFT-RIGHT ROOM / ENTER ASSIGN", p.x + 18,
               p.y + 502, p.w - 36, 8.3, pearl);
        fitted(h, "R REPAIR / F FIRE / M AID / T REST / V VENT / D RECALL / A AUTO / CTRL1-3 GUN",
               p.x + 18, p.y + 521, p.w - 36, 8.3, ice);
        fitted(h,
               "PORT: H BUY / BRACKETS CYCLE / BACKSPACE SELL / SHIFT-BACKSPACE RELEASE / F2 MARKET",
               p.x + 18, p.y + 540, p.w - 36, 8.3, mint);
        fitted(h, "F8 OR ESC CLOSE / FLIGHT CONTINUES / MANUAL HELM SUSPENDED", p.x + 18, p.y + 559,
               p.w - 36, 8.3, muted);
        return finish(std::move(h));
    }
    const bool weaponUseful=focus.mode==CockpitMode::Combat||docking||(focus.mode==CockpitMode::Surface&&t.explorationActive)||!t.stationStatus.empty()||!t.recoveryStatus.empty();
    if (!t.weapon.empty()&&!modal&&!reconOpen&&weaponUseful) {
        if (!t.stationStatus.empty()) {
            const auto station = weaponStationCard(aspect, flightCommands);
            stationPanel = station;
            card(h, station, mint);
            fitted(h, t.stationStatus, station.x + 12, 612, 292, 8.5, mint);
            fitted(h,
                   t.reliefHelm.empty()
                       ? "HELM REMAINS YOURS UNTIL RELIEF ARRIVES"
                       : "HELM " + t.reliefHelm + " / FIT " + std::to_string(int(t.helmQuality * 100)) + "%",
                   station.x + 12, 626, 292, 8.5, ice);
            fitted(h,
                   t.playerGun < maxWeaponMounts ? (t.helmOrder.empty() ? "CREW / COURSE HOLD" : t.helmOrder)
                                                 : "F10 CANCEL / KEEP FLYING DURING HANDOVER",
                   station.x + 12, 640, 292, 8.5, pearl);
            if (t.playerGun < maxWeaponMounts) {
                traversePanel = CockpitRect{center - 85, 493, 170, 96};
                const auto tint = t.playerGun == 1 ? glm::vec3(.75, .55, 1) : mint;
                const double cx = center, cy = 555, radius = 32;
                const double rad = std::numbers::pi / 180.;
                for (double angle = t.gunMinYaw; angle < t.gunMaxYaw; angle += 5) {
                    const double a = angle * rad, b = std::min(angle + 5, t.gunMaxYaw) * rad;
                    line(h.strokes, cx + std::sin(a) * radius, cy - std::cos(a) * radius,
                         cx + std::sin(b) * radius, cy - std::cos(b) * radius, tint * .65f);
                }
                line(h.strokes, cx, cy + 5, cx, cy - 40, muted * .6f);
                line(h.strokes, cx, cy, cx + std::sin(t.gunYaw * rad) * radius,
                     cy - std::cos(t.gunYaw * rad) * radius, tint);
                const bool stop =
                    t.gunYaw < t.gunMinYaw + 2 || t.gunYaw > t.gunMaxYaw - 2 || std::abs(t.gunPitch) > 68;
                fitted(h, stop ? "TRAVERSE STOP" : "GUN ARC / NOSE UP", center - 80, 501, 160, 8,
                       stop ? amber : tint);
                fitted(h,
                       "AZ " + std::to_string(int(t.gunYaw)) + " / EL " + std::to_string(int(t.gunPitch)),
                       center - 80, 578, 160, 8, tint);
                fitted(h,
                       flightCommands ? "CTRL F1-5: COURSE RANGE CIRCLE EVADE RETREAT"
                                      : "1 COURSE  2 RANGE  3 CIRCLE  4 EVADE  5 RETREAT",
                       station.x + 2, 657, 312, 7, ice);
            }
        }
        const CockpitRect armament =
            flightCommands ? CrewFlightLayout(aspect).weapon : CockpitRect{center - 158, 665, 316, 80};
        const auto danger = glm::vec3(1, .3, .2);
        const bool environmentalWarning = t.explorationActive && t.weatherSeverity > .5;
        card(h, armament, t.incomingFire > .2 ? danger : t.weaponHeat > 80 || environmentalWarning ? amber : mint);
        if (t.explorationActive) {
            for (size_t i = 0; i < t.exploration.size(); ++i)
                fitted(h, t.exploration[i], armament.x + 12, 675 + i * 18, 292, i == 0 ? 9.5 : 8.5,
                       environmentalWarning && i >= 2 ? amber : i == 0 ? pearl : ice);
        } else if (docking) {
            const auto& d = t.docking;
            fitted(h, dockingPhaseName(d.phase), armament.x + 12, 675, 292, 9.5, d.docked ? mint : pearl);
            quantity("BERTH " + flightQuantity(d.range) + " / " + flightQuantity(std::abs(d.closing), true),
                     armament.x + 12, 693, 292, 8.8, ice);
            const double lateral = glm::length(glm::dvec2(d.offset));
            quantity("LATERAL " + flightQuantity(lateral) + (d.closing < -.2 ? " / OPENING" : " / CLOSING"),
                     armament.x + 12, 711, 292, 8.8, lateral < 2 ? mint : ice);
            if (d.phase == DockingPhase::Capture)
                bar(h.panels, armament.x + 12, 734, 292, d.capture, mint);
            else
                fitted(h, d.docked ? "WELCOME ABOARD / F2 SERVICES / ENTER UNDOCK"
                                   : !d.active ? "MANUAL / L SOFT CAPTURE / J AUTO DOCK"
                                              : "ARRIVAL > ALIGN > GLIDE > CAPTURE / B TAKE HELM",
                       armament.x + 12, 729, 292, 8, mint);
        } else {
            fitted(h,
                   t.weapon + (t.weaponBattery ? "" : " / HEAT " + std::to_string(int(t.weaponHeat)) + "%"),
                   armament.x + 12, 675, 292, 9.5, pearl);
            fitted(h, t.weaponStatus+(t.targeting&&t.targeting->target&&!t.systemTarget.empty()?" / "+t.systemTarget:""), armament.x + 12, 693, 292, 8.8,
                   t.weaponHeat > 80 ? amber : ice);
            if (t.pulseHeat)
                bar(h.panels, armament.x + 12, 705, 292, *t.pulseHeat / 100.,
                    *t.pulseHeat > 80 ? amber : ice);
            if(t.targeting&&t.targeting->target)drawTargetingReadout(h,*t.targeting,armament);
            else{
            const auto target =
                std::find_if(activity.begin(), activity.end(), [](const auto& c) { return c.selected; });
            if (target != activity.end() && isOrbitalDebris(target->body.id))
                fitted(h, "ORBITAL FRAGMENT / J MATCH COURSE", armament.x + 12, 711, 292, 8.8, ice);
            else if (target != activity.end())
                fitted(h,
                       t.systemTarget.empty()
                           ? "TARGET HULL " + std::to_string(int(std::ceil(target->body.hull))) + "% / " +
                                 flightQuantity(target->distance)
                           : t.systemTarget,
                       armament.x + 12, 711, 292, 8.8, target->body.hostile ? danger : ice);
            else
                fitted(h, "H NEXT HOSTILE / ALT-H CROSSHAIR", armament.x + 12, 711, 292, 8.8, muted);
            fitted(
                h,
                t.recoveryStatus.empty()
                    ? (t.combatStatus.empty() ? "F12 SCAN / F11 JOURNAL / CTRL-SPACE MINE" : t.combatStatus)
                    : t.recoveryStatus,
                armament.x + 12, 729, 292, 8.8, t.hostileContacts ? danger : muted);
            }
        }
        if (!t.combatMission.empty())
            fitted(h, t.combatMission, armament.x + 4, 750, 308, 8, mint);
    }
    if (market && market->open) {
        const auto& m = *market;
        const CockpitRect panel{center - std::min(790., w - 60) * .5, 166, std::min(790., w - 60), 574};
        occlude(h,panel);card(h, panel, m.connected ? mint : amber);
        h.hitRegions.push_back(panel);
        label("F2 CLOSE / F5 " +
                  std::string(m.page == MarketPage::Goods         ? "GOODS"
                              : m.page == MarketPage::Routes      ? "TRADE ROUTES"
                              : m.page == MarketPage::Industry    ? "INDUSTRY"
                              : m.page == MarketPage::Jobs        ? "JOBS"
                              : m.page == MarketPage::Outcomes    ? "OUTCOMES"
                              : m.page == MarketPage::Weapons     ? "WEAPONS"
                              : m.page == MarketPage::Crew        ? "CREW MARKET"
                              : m.page == MarketPage::Survey      ? "SURVEY AND EQUIPMENT"
                              : m.page == MarketPage::Journal     ? "SURVEY JOURNAL"
                              : m.page == MarketPage::Expeditions ? "MATERIAL EXPEDITIONS"
                              : m.page == MarketPage::Incidents   ? "INCIDENTS"
                                                                  : "SERVICES") +
                  " / ECON " + std::to_string(m.tick * 5 / 60) + "H",
              panel.x + 18, panel.y + 16, panel.w - 36, mint);
        quantity(m.ready ? m.site : "LOCAL ECONOMY", panel.x + 18, panel.y + 39, panel.w - 36, 14, pearl);
        label(m.page == MarketPage::Incidents ? m.role : m.role + " / " +
                  (m.page == MarketPage::Outcomes || m.page == MarketPage::Journal ? "SAVED REPORT"
                   : m.connected                                                   ? "TRADE LINK"
                                 : "REMOTE / APPROACH AND STOP"),
              panel.x + 18, panel.y + 64, panel.w - 36, m.connected ? mint : amber);
        quantity("CREDITS " + std::to_string(m.credits / 100) + " / CARGO " + std::to_string(m.cargoKg) +
                     "/" + std::to_string(m.capacityKg) + " KG / " + std::to_string(m.cargoLitres) + "/" +
                     std::to_string(m.capacityLitres) + " L" +
                     (m.activeJob.empty() ? "" : " / " + m.activeJob),
                 panel.x + 18, panel.y + 86, panel.w - 36, 10.5, ice);
        if (m.page == MarketPage::Goods) {
            label("GOOD                 STOCK   FREE     BUY CR     SELL CR", panel.x + 18, panel.y + 112,
                  panel.w - 36, muted);
            for (size_t g = 0; g < goodCount; g++) {
                const double y = panel.y + 125 + g * 17;
                if (g == m.selected)
                    rect(h.panels, panel.x + 12, y - 3, panel.w - 24, 16, mint * .18f, .95f);
                fitted(h, m.rows[g], panel.x + 18, y, panel.w - 36, 9.2,
                       g == m.selected ? pearl : ice);
            }
            label("ORDER " + std::to_string(m.quantity) + " KG / UP-DOWN GOODS / LEFT-RIGHT SIZE",
                  panel.x + 18, panel.y + 472, panel.w - 36, mint);
            label("ENTER BUY / BACKSPACE SELL / F3 CONTRACT / F4 TARGET", panel.x + 18, panel.y + 492,
                  panel.w - 36, pearl);
            fitted(h, m.summary.empty() ? m.contract : m.summary, panel.x + 18, panel.y + 512,
                   panel.w - 36, 8.4, amber);
        } else if (m.page == MarketPage::Journal || m.page == MarketPage::Survey ||
                   m.page == MarketPage::Expeditions) {
            const bool journal = m.page == MarketPage::Journal;
            const bool expedition = m.page == MarketPage::Expeditions;
            fitted(h,
                   expedition ? "F4 DESTINATION / SHIFT-F4 COLONY / ENTER ACTION / SHIFT-F11 EXIT PROBE"
                   : journal  ? "UP-DOWN RECORDS / F4 VISIT / ENTER SELL DATA"
                              : "UP-DOWN EQUIPMENT OR CONTRACT / LEFT-RIGHT MOUNT",
                   panel.x + 18, panel.y + 112, panel.w - 36, 9, mint);
            for (size_t i = 0; i < m.detail.size(); ++i)
                fitted(h, m.detail[i], panel.x + 18, panel.y + 140 + i * (journal ? 25 : 18),
                       panel.w - 36, i == 0 ? 9.4 : 8.4, i % 2 ? ice : pearl);
            fitted(h, m.summary, panel.x + 18, panel.y + 472, panel.w - 36, 8.4, mint);
            fitted(h,
                   expedition ? "ENTER DIAGNOSE, OPEN LAB OR FIT REPAIR / BACKSPACE CANCEL"
                   : journal ? "F11 CLOSE / F12 SCAN / CTRL-SPACE EXTRACT"
                           : "ENTER FIT OR ACCEPT-CLAIM / BACKSPACE CANCEL / F4 TARGET",
                   panel.x + 18, panel.y + 492, panel.w - 36, 9, pearl);
            fitted(h, expedition ? "SEALED CORE / SAME SOURCE GEOMETRY / PARKED SHIP / SAVED COLONY OUTCOME"
                                         : "SCAN AT ANY SCALE / EXTRACTION AT FULL SIZE / DEPLETION PERSISTS",
                   panel.x + 18, panel.y + 512, panel.w - 36, 8.4, amber);
        } else if (m.page == MarketPage::Crew) {
            fitted(h, m.summary, panel.x + 18, panel.y + 112, panel.w - 36, 9.5, mint);
            const size_t first = m.selected >= 12 ? m.selected - 11 : 0;
            for (size_t row = first; row < std::min(first + 12, m.crewRows.size()); ++row) {
                const auto& offer = m.crewRows[row];
                const auto& person = offer.person;
                const double y = panel.y + 135 + (row - first) * 25;
                if (row == m.selected)
                    rect(h.panels, panel.x + 12, y - 3, panel.w - 24, 24, mint * .16f);
                const auto skill = std::max_element(person.experience.begin(), person.experience.end());
                const auto specialty = CrewStation(skill - person.experience.begin());
                fitted(h,
                       std::string(offer.buy ? "BUY  " : "SELL ") + person.name + " / " +
                           std::string(crewStationName(specialty)) + " / " +
                           crewMoney(offer.buy ? offer.quote.buyCents : offer.quote.sellCents) + " CR",
                       panel.x + 18, y, panel.w - 36, 9, row == m.selected ? pearl : ice);
                fitted(h,
                       "HP " + std::to_string(int(person.health)) + " / FAT " +
                           std::to_string(int(person.fatigue)) + " / MOR " +
                           std::to_string(int(person.morale)) + " / " + offer.quote.status,
                       panel.x + 18, y + 12, panel.w - 36, 8, row == m.selected ? mint : muted);
            }
            fitted(h, "ENTER BUY PORT CREW / BACKSPACE SELL ABOARD CREW", panel.x + 18,
                   panel.y + 461, panel.w - 36, 9, pearl);
            fitted(h, "SHIFT-BACKSPACE RELEASE / UP-DOWN SELECT / F8 CREW STATIONS", panel.x + 18,
                   panel.y + 480, panel.w - 36, 8.5, ice);
        } else if (m.page == MarketPage::Incidents) {
            for (size_t i = 0; i < std::min<size_t>(6, m.detail.size()); ++i)
                fitted(h, m.detail[i], panel.x + 18, panel.y + 125 + i * 25, panel.w - 36,
                       i == 0 ? 11 : 9.4, i == 0 ? pearl : i == 2 ? amber : ice);
            if (m.detail.size() >= 9) {
                label("RESPONSE / LEFT-RIGHT SELECT", panel.x + 18, panel.y + 286, panel.w - 36, mint);
                const std::array<std::string_view, 3> choices{"RESCUE", "RELAY", "RELIEF"};
                for (size_t i = 0; i < choices.size(); ++i) {
                    const double y = panel.y + 310 + i * 30;
                    const bool selected = m.summary == choices[i];
                    if (selected)
                        rect(h.panels, panel.x + 12, y - 5, panel.w - 24, 23, mint * .15f, .95f);
                    fitted(h, m.detail[6 + i], panel.x + 18, y, panel.w - 36, 9.4, selected ? pearl : muted);
                }
                fitted(h, m.plan, panel.x + 18, panel.y + 413, panel.w - 36, 9, pearl);
                fitted(h, m.planTiming, panel.x + 18, panel.y + 432, panel.w - 36, 8.8, amber);
            }
            label("UP-DOWN INCIDENT / F4 TARGET / J APPROACH / ENTER ACTION", panel.x + 18, panel.y + 492,
                  panel.w - 36, pearl);
            label("I JOBS / F5 OTHER MARKET PAGES / F2 CLOSE", panel.x + 18, panel.y + 512,
                  panel.w - 36, ice);
        } else if (m.page == MarketPage::Outcomes) {
            label("DELIVERY REPORT / UP-DOWN HISTORY / BEFORE / LAST OBSERVED", panel.x + 18, panel.y + 112,
                  panel.w - 36, mint);
            for (size_t row = 0; row < m.detail.size(); ++row)
                fitted(h, m.detail[row], panel.x + 18, panel.y + 142 + row * 25, panel.w - 36,
                       row == 0 ? 10 : 8.8, row < 2 ? pearl : ice);
            fitted(h, m.summary, panel.x + 18, panel.y + 472, panel.w - 36, 8.4, mint);
            label("F4 TARGET REPORT PORT / J APPROACH / F5 MARKET PAGES", panel.x + 18, panel.y + 492,
                  panel.w - 36, pearl);
            label("PAYMENTS ARE FINAL / HEALTH AND PRODUCTION RECOVER OVER TIME", panel.x + 18, panel.y + 512,
                  panel.w - 36, amber);
        } else {
            const bool industry = m.page == MarketPage::Industry;
            const bool jobs = m.page == MarketPage::Jobs;
            const bool services = m.page == MarketPage::Services;
            const bool weapons = m.page == MarketPage::Weapons;
            label(weapons
                      ? std::string(weaponMountName(m.weaponMount)) + " / LEFT-RIGHT MOUNT / UP-DOWN WEAPON"
                  : services ? "PORT SERVICES / REPAIRS / HOLD / RESCUE HANDOVER"
                  : jobs     ? "FUNDED JOBS / UP-DOWN SELECT / ONE HAUL + ONE SECURITY"
                  : industry ? "EXISTING FACILITY REFITS / DELIVER CARGO TO BUILD"
                             : "AFFORDABLE LOCAL OPPORTUNITIES / UP-DOWN SELECT",
                  panel.x + 18, panel.y + 112, panel.w - 36, mint);
            const size_t group = industry || services || weapons ? 5 : 2;
            const double rowHeight = industry || services || weapons ? 21 : 13;
            // Keep complete offers together and follow the selected offer.
            // Longer job boards must never run into the plan or action footer.
            const double bodyEnd = jobs ? 435 : 454;
            const size_t capacity = (size_t((bodyEnd - 150) / rowHeight) + 1) / group * group;
            const size_t first = m.detail.size() > capacity
                                     ? std::min(m.selected * group / capacity * capacity,
                                                (m.detail.size() - 1) / capacity * capacity)
                                     : 0;
            const size_t end = std::min(m.detail.size(), first + capacity);
            for (size_t row = first; row < end; row++) {
                const auto y = panel.y + 140 + (row - first) * rowHeight;
                const bool selected = row / group == m.selected;
                if (selected && row % group == 0)
                    rect(h.panels, panel.x + 12, y - 4, panel.w - 24, group * rowHeight - 3, mint * .15f,
                         .95f);
                fitted(h, m.detail[row], panel.x + 18, y, panel.w - 36,
                       row % group == 0 ? 9.4 : 8.4, selected ? (row % group == 0 ? pearl : ice) : muted);
            }
            if (m.detail.size() > capacity)
                fitted(h, "ROWS " + std::to_string(first + 1) + "-" + std::to_string(end) + " / " +
                              std::to_string(m.detail.size()) + " / UP-DOWN SELECT",
                       panel.x + 18, panel.y + bodyEnd, panel.w - 36, 8, muted);
            if (jobs) {
                fitted(h, m.plan, panel.x + 18, panel.y + 451, panel.w - 36, 8.4, ice);
                fitted(h, m.planTiming, panel.x + 18, panel.y + 465, panel.w - 36, 8.4, amber);
            }
            fitted(h, m.summary, panel.x + 18, panel.y + (jobs ? 480 : 472), panel.w - 36, 8.4,
                   mint);
            label(weapons    ? "ENTER FIT / UPGRADE / BACKSPACE SELL SELECTED MOUNT / F4 PORT"
                  : services ? "ENTER SERVICE OR DISEMBARK / F4 TARGET PORT / J APPROACH"
                  : jobs     ? "ENTER ACCEPT / DELIVER / CLAIM / F4 TARGET / J APPROACH"
                  : industry ? "ENTER FUND / DELIVER PARTS / BACKSPACE COLLECT DIVIDENDS"
                             : "ENTER TARGET SOURCE / F4 TARGET BUYER / J APPROACH",
                  panel.x + 18, panel.y + 492, panel.w - 36, pearl);
            label(weapons    ? "SPACE LINKED FIRE / F9 CTRL1-3 ASSIGNS GUNNERS / INDEPENDENT HEAT"
                  : services ? "REPAIRS USE PORT STOCK / CARGO UPGRADES KEEP SEALED CONSIGNMENTS"
                  : jobs     ? "I INCIDENTS / BACKSPACE CANCEL HAUL OR SECURITY / SHIFT-F4 HAUL SOURCE"
                  : industry ? "DELETE CANCEL PROJECT / UNUSED FEES AND PARTS RETURNED"
                             : "QUOTES INCLUDE SPREAD / MARGINAL PRICES / NO PRICE GUARANTEE",
                  panel.x + 18, panel.y + 512, panel.w - 36, amber);
        }
        fitted(h, m.bottleneck, panel.x + 18, panel.y + 532, panel.w - 36, 8.4, muted);
        fitted(h, m.message, panel.x + 18, panel.y + 551, panel.w - 36, 8.4, ice);
        return finish(std::move(h));
    }
    if (help) {
        const CockpitRect guide{center-std::min(1040.,w-48)*.5,112,std::min(1040.,w-48),634};
        occlude(h,guide);card(h, guide, mint);
        fitted(h, "FLIGHT CONTROLS", guide.x + 22, guide.y + 22, guide.w - 44, 17.5, pearl);
        const std::pair<const char*, const char*> controls[]{
            {"W / S", "THROTTLE"},
            {"A / D   Q / E", "STRAFE / LIFT"},
            {"C / MOUSE / ARROWS", "HELM / LOOK"},
            {"U / O  ALT+A / D", "ROLL"},
            {"B / X", "RECOVER / ASSIST"},
            {"Y / SHIFT-Y", "PLANETS / MOONS"},
            {"CTRL-Y", "LIFE-DETECTED WORLDS"},
            {"K / I / J", "TRAFFIC / HAIL / APPROACH-DOCK"},
            {"SHIFT-K / SHIFT-I", "SURFACE PORTS / ARRIVAL CONTACTS"},
            {"ALT-K / ALT-SHIFT-K", "TRACK DEBRIS / LANDABLE FRAGMENTS"},
            {"L / SHIFT-L / G / ENTER", "LAND / ACCEPT RISK / GEAR / LAUNCH"},
            {"N / CTRL-N", "WARP / WARP TO TARGET"},
            {"Z / , / .", "SHRINK / SMALLER / LARGER"},
            {"SHIFT-Z / V", "FULL SIZE / EXPLORER"},
            {"F6 / TAB", "TRAFFIC / HUD"},
            {"CTRL-TAB", "CONTACT CATALOGUES / FLIGHT MESSAGES"},
            {"F7", "COCKPIT MOTION"},
            {"F5 / CTRL-F5 / ALT-F5", "SURFACE MAP / NAV ASSESS / ENGINEER WATCH"},
            {"ON SURFACE MAP", "SHIFT-F5 NEXT / F4 ROUTE / HOLD 0 FIELD WORK"},
            {"F8", "CREW / STATIONS / DAMAGE CONTROL"},
            {"F9", "IN-FLIGHT CREW ORDERS / HELM LIVE"},
            {"F10", "SWAP WITH READY GUNNER / RETURN TO HELM"},
            {"F11 / F12", "JOURNAL / SCAN / CTRL-F12 SEAM / SHIFT-J ENTRY"},
            {"CTRL-0", "CONTACT SCAN / BOARD / REPAIR / EVACUATE"},
            {"CTRL-F11", "RADIO / V SHIPS / S SCAN / X DIRECTIONAL JAM"},
            {"HOLD SEMICOLON", "JAM DECODED SHIP CARRIER WHILE FLYING"},
            {"ALT-F12", "CYCLE WORLD GEOLOGY LANDMARKS"},
            {"CTRL+SPACE / ALT+CTRL+SPACE", "MINING HEAD / CAREFUL CAVERN CUT"},
            {"SPACE", "FIRE FITTED WEAPON OR PROBE LAUNCHER"},
            {"CTRL-P / ALT-P", "LAUNCH PROBE / LIVE CAMERA"},
            {"SHIFT-P / CTRL-J", "PROBE BAY / EXCAVATION ENTRANCE"},
            {"ALT-[ / ]", "PROBE DEPTH PRESET / CAMERA [ ] DEPTH"},
            {"PROBE CAMERA", "P PAUSE / - + FEED / R ABORT / ENTER CHARGE"},
            {"CAVERN / ALT-0 / HOLD 0", "SHIFT-F12 DEPOSIT / BRACE / RECOVER ORE + KIT"},
            {"H / SHIFT-H / ALT-H", "NEXT / PREVIOUS / CROSSHAIR HOSTILE"},
            {"ALT-SHIFT-H", "TARGET IDENTIFIED ATTACKER"},
            {"BACKSLASH / SHIFT-BACKSLASH", "DECOY BURST / CREW AUTO DECOYS"},
            {"CTRL-BACKSLASH", "BUILD DECOY: 1KG ALLOY + 1KG ELECTRONICS"},
            {"CTRL-H / 9 / SHIFT-9", "CLEAR LOCK / NEXT / PREVIOUS COMPONENT"},
            {"6 / 7 / 8", "GUNNERS HOLD / DEFEND / FOCUS"},
            {"F2 / MARKET F5", "MARKET / JOBS / SERVICES / DELIVERY REPORTS"},
            {"F3", "EXPEDITION PLANNER / CONTRACT / PREPARE / TRACK"},
            {"F1 / ESC", "CLOSE GUIDE / RELEASE HELM"}};
        for (size_t i = 0; i < std::size(controls); ++i) {
            const auto y=guide.y+65+i*std::min(14.,(guide.h-92)/std::size(controls));
            fitted(h,controls[i].first,guide.x+22,y,guide.w*.30-22,12,ice);
            fitted(h,controls[i].second,guide.x+guide.w*.34,y,guide.w*.64-22,12,pearl);
        }
        return finish(std::move(h));
    }

    if (flightCommands) {
        const CrewFlightLayout layout(aspect);
        const auto p = layout.panel;
        crewCommandPanel = p;
        occlude(h,p);card(h, p, mint);
        fitted(h, "CREW / F9 ORDERS", p.x + 12, p.y + 12, p.w - 94, 12, pearl);
        card(h, layout.automatic, crew->autoDamageControl ? mint : amber);
        fitted(h, crew->autoDamageControl ? "AUTO ON" : "AUTO OFF", layout.automatic.x + 5,
               layout.automatic.y + 5, layout.automatic.w - 10, 7, crew->autoDamageControl ? mint : amber);
        fitted(h, crew->alert, p.x + 12, p.y + 33, p.w - 24, 8.2,
               crew->summary.find("EMERGENCY") != std::string::npos ? amber : ice);
        fitted(h,
               "CREW " +
                   std::to_string(std::count_if(crew->members.begin(), crew->members.end(),
                                                [](const auto& m) { return m.health > 0; })) +
                   "/8 / SPARES " + std::to_string(int(crew->spares)) + " / MED " +
                   std::to_string(int(crew->medical)),
               p.x + 12, p.y + 50, p.w - 24, 8.2, mint);
        for (size_t i = 0; i < crew->members.size() && i < maxShipCrew; ++i) {
            const auto& m = crew->members[i];
            const auto r = layout.roster[i];
            const auto task = i < crew->tasks.size() ? crew->tasks[i] : CrewTaskView{};
            const auto colour = m.health <= 0         ? muted
                                : task.blocked        ? amber
                                : i == crew->selected ? mint
                                                      : ice;
            if (i == crew->selected)
                rect(h.panels, r.x, r.y, r.w, r.h, mint * .16f);
            fitted(h,
                   std::to_string(i + 1) + " " + m.name + " / " +
                       std::string(m.id == crew->reliefHelmId ? "RELIEF HELM" : crewDutyName(m)),
                   r.x + 4, r.y + 3, r.w - 8, 8.6, colour);
            fitted(h,
                   task.status + " / HP " + std::to_string(int(m.health)) + " FAT " +
                       std::to_string(int(m.fatigue)),
                   r.x + 4, r.y + 15, r.w - 8, 7.5, colour);
        }
        constexpr std::array<std::string_view, 3> mountNames{"NOSE", "PORT", "STBD"};
        for (unsigned i = 0; i < maxWeaponMounts; ++i) {
            const auto r = layout.guns[i];
            const auto colour = !crew->weaponLevels[i] ? muted : crew->gunReadiness[i] > 0 ? mint : amber;
            card(h, r, colour);
            fitted(h,
                   std::to_string(i + 1) + " " + std::string(mountNames[i]) + " " +
                       std::string(weaponGrade(crew->weaponLevels[i])),
                   r.x + 4, r.y + 5, r.w - 8, 7, colour);
        }
        const auto detail =
            crew->selected < crew->tasks.size() ? crew->tasks[crew->selected] : CrewTaskView{};
        std::string selectedDetail =
            "ROOM " + std::string(crewStationName(crew->station)) + " / " + detail.detail;
        if (crew->selected < crew->members.size()) {
            const auto& m = crew->members[crew->selected];
            if (m.duty == CrewStation::Weapons && m.weaponMount < maxWeaponMounts)
                selectedDetail =
                    std::string(weaponMountName(m.weaponMount)) + " / " + crew->gunStatus[m.weaponMount];
        }
        fitted(h, selectedDetail, p.x + 12, p.y + 307, p.w - 24, 8, detail.blocked ? amber : mint);
        for (size_t i = 0; i < crewRoomCount; ++i) {
            const auto r = layout.rooms[i];
            const auto& room = crew->rooms[i];
            const auto colour = room.fire > 1 || room.oxygen < 25 ? amber
                                : crew->station == CrewStation(i) ? mint
                                                                  : ice;
            card(h, r, colour);
            fitted(h, std::to_string(i + 1) + " " + std::string(crewStationName(CrewStation(i))),
                   r.x + 4, r.y + 6, r.w - 8, 7.4, colour);
        }
        constexpr std::array<std::string_view, 8> labels{"ASSIGN",        "REPAIR",   "FIGHT FIRE",
                                                         "FIRST AID",     "REST",     "RETURN TO DUTY",
                                                         "BEST STATIONS", "EMERGENCY"};
        for (size_t i = 0; i < labels.size(); ++i) {
            const auto r = layout.actions[i];
            card(h, r, i == 7 ? amber : ice);
            fitted(h, labels[i], r.x + 5, r.y + 8, r.w - 10, 8.2, i == 7 ? amber : pearl);
        }
        fitted(h, crew->message.empty() ? "MANUAL HELM AND WEAPONS REMAIN LIVE" : crew->message,
               p.x + 12, p.y + 529, p.w - 24, 7.8, amber);
        fitted(h, "1-8 CREW / ALT 1-7 ROOM / C CURSOR", p.x + 12, p.y + 547, p.w - 24, 7.6, ice);
        fitted(h, "CTRL1-3 MAN GUN / F10 SWAP / F9 CLOSE", p.x + 12, p.y + 560, p.w - 24, 7.4,
               muted);
    }
    if(!modal&&!flightCommands&&!reconOpen&&!catalogues){
        if(microActive){const auto panel=cockpitContactsSummary();card(h,panel,ice);
            fitted(h,"LOCAL MATERIAL",38,155,160,10,ice);fitted(h,"CTRL-TAB",213,155,71,8.5,muted);
            quantity(micro->mineral+" / "+micro->formula,38,181,246,12,pearl);
            fitted(h,micro->phase,38,207,246,10,mint);
            quantity("CELL "+flightQuantity(micro->cellMetres)+" / FIELD "+flightQuantity(micro->fieldMetres),38,240,246,9.5,ice);
            h.contactHits.push_back({panel,{},{},true,false});
        }else drawContactSummary(h,aspect,contacts,activity);
    }
    if (catalogues && !flightCommands && !activity.empty()) {
        card(h, l.traffic, trafficInk);
        label("PORTS / TRAFFIC", 38, 188, 160, trafficInk);
        h.contactHits.push_back({{24,176,274,28},{},{},true,false});
        label("K / I", 238, 188, 56, muted);
        const auto rows = cockpitActivityRows(activity);
        for (size_t row = 0; row < rows.size(); ++row) {
            const auto& c = activity[rows[row]];
            const double y = 211 + row * 34;
            h.contactHits.push_back({{28,y-5,266,33},{},c.body.id,false,false});
            if (c.selected)
                rect(h.panels, 28, y - 5, 266, 33, amber * .16f, .8f);
            fitted(h, (c.selected ? "+ " : "  ") + c.body.name, 36, y, 246, 10.5,
                   c.body.hostile ? glm::vec3(1, .3, .2)
                   : c.selected   ? amber
                   : c.occluded   ? muted
                                  : pearl);
            auto status = c.body.economicBrief.empty() ? c.body.status.substr(0, c.body.status.find(" / "))
                                                       : c.body.economicBrief;
            if (c.occluded)
                status = "OCCULTED";
            quantity(flightQuantity(c.distance) + " / " + status, 52, y + 16, 234, 9,
                     c.selected || c.body.economicAlert > .5f ? amber : muted);
        }
        const auto fragments = std::count_if(activity.begin(), activity.end(),
                                             [](const auto& c) { return isOrbitalDebris(c.body.id); });
        label(fragments ? std::to_string(fragments) + " DEBRIS / ALT-K SCAN"
                        : std::to_string(activity.size()) + " CONTACTS / K SELECT",
              38, 421, 246, fragments ? ice : muted);
        const ActivityContact* report = nullptr;
        for (const auto& contact : activity)
            if (contact.selected && !contact.body.economicBrief.empty()) {
                report = &contact;
                break;
            }
        if (report) {
            const auto& body = report->body;
            const auto colour = body.economicAlert > .9f   ? glm::vec3(1, .42, .25)
                                : body.economicAlert > .5f ? amber
                                                           : mint;
            economicPanel = CockpitRect{24, 450, 274, 166};
            card(h, *economicPanel, colour);
            h.hitRegions.push_back(*economicPanel);
            label(body.kind == ActivityKind::Freighter ? "FREIGHT MANIFEST" : "PORT STATUS", 38, 462, 246,
                  colour);
            fitted(h, body.name, 38, 482, 246, 11, pearl);
            fitted(h, body.economicBrief, 38, 502, 246, 9.4, colour);
            for (size_t i = 0; i < body.economicReport.size(); ++i)
                fitted(h, body.economicReport[i], 38, 521 + i * 18, 246, 8.4, ice);
            label(body.kind == ActivityKind::Freighter ? "I HAIL / J MATCH COURSE"
                                                       : "I HAIL / J APPROACH / F2 MARKET",
                  38, 598, 246, muted);
        }
    }
    if (catalogues && !contacts.empty()) {
        card(h, l.celestial, ice);
        const auto moons =
            std::count_if(contacts.begin(), contacts.end(), [](const auto& c) { return c.moon; });
        const auto planets = std::count_if(contacts.begin(), contacts.end(), [](const auto& c) {
            return c.address.kind() == Kind::Planet && !c.moon;
        });
        label("SYSTEM " + std::to_string(planets) + "P / " + std::to_string(moons) + "M", w - 296, 188, 180,
              ice);
        const auto rows = cockpitCelestialRows(contacts);
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto& c = contacts[rows[i]];
            const double x = w - 296, y = 211 + i * 34;
            h.contactHits.push_back({{w-306,y-5,278,33},c.address,{},false,false});
            const bool inhabited = hasSurfaceLife(c);
            if (c.selected)
                rect(h.panels, w - 306, y - 5, 278, 33, amber * .16f, .8f);
            else if (inhabited)
                rect(h.panels, w - 306, y - 5, 278, 33, mint * .10f, .9f);
            const auto color = c.selected ? amber : inhabited ? mint : c.occluded ? muted : pearl;
            fitted(h, (c.selected ? "+ " : "  ") + c.label, x, y, inhabited ? 207 : 258, 10.5,
                   color);
            if (inhabited) {
                rect(h.panels, x + 214, y - 1, 44, 13, mint * .20f);
                fitted(h, "LIFE", x + 219, y + 1, 37, 9, mint);
            }
            const auto direction = c.occluded ? "HIDDEN" : c.localDirection.z < 0 ? "AHEAD" : "AFT";
            const auto status =
                inhabited ? population(c.population) + " PEOPLE" + (c.occluded ? " / HIDDEN" : "")
                : c.life == LifeDetection::Scanning                                 ? "SCANNING"
                : c.life == LifeDetection::None && c.address.kind() == Kind::Planet ? "NO SURFACE LIFE"
                                                                                    : direction;
            quantity(flightQuantity(c.distanceMetres) + " / " + status, x + 17, y + 16, 243, 9,
                     inhabited    ? mint
                     : c.selected ? amber
                                  : muted);
        }
        label("CTRL-Y LIFE / Y WORLDS", w - 296, 490, 258, living ? mint : muted);
        const auto badge=cockpitLifeBadge(aspect,true);
        if(living){rect(h.panels,badge.x,badge.y,badge.w,badge.h,mint*.14f);
            fitted(h,std::to_string(living)+" LIFE",badge.x+4,badge.y+4,badge.w-8,9,mint);
            h.contactHits.push_back({badge,{},{},false,true});}

    }

    if(reconOpen)h.hitRegions.push_back(surfaceReconPanel(aspect));
    if(catalogues&&!flightCommands){
        const double detailWidth=w>=1200?std::clamp(w-656.,240.,650.):w-48;
        h.hitRegions.push_back(computerPanel(aspect,computerDetails(alerts,t,crew,detailWidth).size()));
    }
    // Readable world labels reserve rectangles, avoid all fixed panels and the
    // reticle, and cap at three secondary labels. Geometry and picking stay full.
    if (!t.systemTarget.empty() && glm::length(t.localSystemTarget) > 1e-12 && !(market && market->open) &&
        !help) {
        const auto marker =
            cockpitTargetMarker(glm::inverse(headInShip) * t.localSystemTarget, aspect, projectionScale);
        if (!marker.offscreen && !marker.aft)
            diamond(h.strokes, marker.point, 7, amber);
    }
    const CockpitRect aim{aimPoint.x - 42, aimPoint.y - 27, 84, 54};
    std::optional<CockpitTargetMarker> missileMarker;
    CockpitRect missileBounds{};
    if (missileAlert) {
        const auto marker = cockpitTargetMarker(eyeFromShip * t.localMissile, aspect, projectionScale);
        missileBounds = {marker.point.x - 16, marker.point.y - 16, 32, 32};
        if (std::none_of(h.hitRegions.begin(), h.hitRegions.end(),
                         [&](const auto& panel) { return panel.overlaps(missileBounds); }) &&
            std::none_of(h.textRuns.begin(), h.textRuns.end(),
                         [&](const auto& run) { return run.bounds.overlaps(missileBounds); }))
            missileMarker = marker;
    }
    auto worldLabel = [&](glm::dvec2 anchor, std::string_view name, std::string_view range, glm::vec3 color) {
        const double width = std::min(184., l.windscreen.w - 12);
        for (int placement : {0, 1, 2, 3}) {
            const int side = placement == 1 ? -1 : 1;
            CockpitRect r{side > 0 ? anchor.x + 18 : anchor.x - width - 18,
                          anchor.y + (placement == 2   ? 32
                                      : placement == 3 ? -68
                                                       : -16),
                          width, t.contactsExpanded?39.:23.};
            r.x = std::clamp(r.x, l.windscreen.x, l.windscreen.x + l.windscreen.w - width);
            r.y = std::clamp(r.y, l.windscreen.y, l.windscreen.y + l.windscreen.h - r.h);
            if (r.overlaps(aim) || (missileMarker && r.overlaps(missileBounds)) ||
                std::any_of(h.hitRegions.begin(),h.hitRegions.end(),[&](const auto& panel){return r.overlaps(panel);}) ||
                std::any_of(h.textRuns.begin(),h.textRuns.end(),[&](const auto& run){return r.overlaps(run.bounds);}) ||
                (economicPanel && r.overlaps(*economicPanel)) ||
                (crewCommandPanel && r.overlaps(*crewCommandPanel)) ||
                (stationPanel && r.overlaps(*stationPanel)) ||
                (traversePanel && r.overlaps(*traversePanel)) ||
                std::any_of(h.worldLabels.begin(), h.worldLabels.end(),
                            [&](auto other) { return r.overlaps(other); }))
                continue;
            h.worldLabels.push_back(r);
            rect(h.panels, r.x, r.y, r.w, r.h, ink, .94f);
            line(h.strokes, anchor.x, anchor.y, side > 0 ? r.x : r.x + r.w, r.y + 18, color * .5f);
            fitted(h, name, r.x + 7, r.y + 6, r.w - 14, 9, color);
            if(t.contactsExpanded)quantity(range, r.x + 7, r.y + 22, r.w - 14, 9, muted);
            return true;
        }
        return false;
    };
    if(t.targeting&&t.targeting->target&&!modal&&!help&&!reconOpen&&
       t.targeting->state!=TargetLock::Disabled&&glm::length(t.targeting->direction)>1e-9){
        const auto& v=*t.targeting;const auto color=targetingColor(v);
        auto marker=cockpitTargetMarker(eyeFromShip*v.direction,aspect,projectionScale);
        auto clear=[&](CockpitRect r){
            return std::none_of(h.hitRegions.begin(),h.hitRegions.end(),[&](const auto& p){return p.overlaps(r);})&&
                   std::none_of(h.textRuns.begin(),h.textRuns.end(),[&](const auto& t){return t.bounds.overlaps(r);});
        };
        if(marker.offscreen&&!clear({marker.point.x-22,marker.point.y-22,44,44}))
            marker.point=aimPoint+marker.direction*68.;
        const auto p=marker.point;
        if(clear({p.x-23,p.y-23,46,46})){
            if(marker.offscreen){const auto d=marker.direction,side=glm::dvec2(-d.y,d.x);
                line(h.strokes,p.x,p.y,p.x-d.x*13+side.x*7,p.y-d.y*13+side.y*7,color);
                line(h.strokes,p.x,p.y,p.x-d.x*13-side.x*7,p.y-d.y*13-side.y*7,color);
            }else{
                for(int side:{-1,1}){line(h.strokes,p.x+side*14,p.y-14,p.x+side*21,p.y-14,color);
                    line(h.strokes,p.x+side*21,p.y-14,p.x+side*21,p.y+14,color);
                    line(h.strokes,p.x+side*21,p.y+14,p.x+side*14,p.y+14,color);}
                for(unsigned i=0;i<40;++i){const double a=i*2*std::numbers::pi/40,b=(i+.65)*2*std::numbers::pi/40;
                    const auto tint=i<40*v.progress?color:muted*.3f;
                    line(h.strokes,p.x+27*std::sin(a),p.y-27*std::cos(a),p.x+27*std::sin(b),p.y-27*std::cos(b),tint);}
            }
            h.worldLabels.push_back({p.x-29,p.y-29,58,58});
            if(v.missile&&v.interceptValid&&!marker.offscreen){
                const auto lead=cockpitTargetMarker(eyeFromShip*v.intercept,aspect,projectionScale);
                if(!lead.offscreen&&!lead.aft&&clear({lead.point.x-8,lead.point.y-8,16,16})){
                    diamond(h.strokes,lead.point,5,mint);
                    h.worldLabels.push_back({lead.point.x-9,lead.point.y-9,18,18});
                    const auto d=lead.point-p;
                    if(glm::length(d)>35)for(unsigned i=2;i<8;i+=2){const auto a=p+d*(i/10.),b=p+d*((i+1)/10.);line(h.strokes,a.x,a.y,b.x,b.y,mint*.6f);}
                }
            }
            worldLabel(p,v.name+(marker.aft?" / AFT":""),flightQuantity(v.range),color);
        }
    }
    if(t.awareness&&t.awareness->active&&!modal&&!help&&!reconOpen){
        unsigned marked=0;
        for(const auto& cue:t.awareness->cues){
            const auto bearing=eyeFromShip*cue.direction;
            const auto tint=(cue.phase==ThreatPhase::Locking?amber:danger)*float(std::clamp(cue.strength,.1,1.));
            if(cue.phase==ThreatPhase::Hit){
                const double angle=std::abs(bearing.y)>.8?(bearing.y>0?0:std::numbers::pi):std::atan2(bearing.x,-bearing.z);
                for(unsigned i=0;i<8;++i){const double a=angle-.27+i*.0675,b=a+.045;
                    line(h.strokes,aimPoint.x+68*std::sin(a),aimPoint.y-68*std::cos(a),aimPoint.x+68*std::sin(b),aimPoint.y-68*std::cos(b),tint);}
            }
            if(cue.phase==ThreatPhase::Tracking||marked>=3)continue;
            auto marker=cockpitTargetMarker(bearing,aspect,projectionScale);
            if(marker.offscreen)marker.point=aimPoint+marker.direction*87.;
            const auto p=marker.point;const CockpitRect bounds{p.x-40,p.y-25,80,30};
            if(std::any_of(h.hitRegions.begin(),h.hitRegions.end(),[&](const auto& r){return r.overlaps(bounds);})||
               std::any_of(h.textRuns.begin(),h.textRuns.end(),[&](const auto& r){return r.bounds.overlaps(bounds);}))continue;
            for(int side:{-1,1}){line(h.strokes,p.x+side*32,p.y-20,p.x+side*38,p.y-13,tint);
                line(h.strokes,p.x+side*38,p.y-13,p.x+side*32,p.y-6,tint);}
            h.worldLabels.push_back(bounds);++marked;
            const bool selected=t.targeting&&t.targeting->target&&cue.source==t.targeting->target;
            if(marked==1&&!selected)worldLabel(p,cue.name+" / "+(cue.phase==ThreatPhase::Locking?"LOCKING":cue.phase==ThreatPhase::Missile?"MISSILE":cue.phase==ThreatPhase::Hit?"HIT":"FIRING"),"",tint);
        }
    }
    if (t.targetVisible && !(t.targeting&&t.targeting->target&&t.targetLabel==t.targeting->name)) {
        const auto marker = cockpitTargetMarker(eyeFromShip * (t.warpDestination ? t.localWarpDestination : t.localTarget),
                                                 aspect, projectionScale);
        const auto p = marker.point;
        const bool footprint = t.landing.body && !t.landed;
        const auto tint = footprint ? mint : ice;
        if (marker.offscreen) {
            const auto d = marker.direction, across = glm::dvec2(-d.y, d.x);
            line(h.strokes, p.x, p.y, p.x - d.x * 12 + across.x * 6, p.y - d.y * 12 + across.y * 6, tint);
            line(h.strokes, p.x, p.y, p.x - d.x * 12 - across.x * 6, p.y - d.y * 12 - across.y * 6, tint);
        } else {
            for (int side : {-1, 1}) {
                line(h.strokes, p.x + side * 10, p.y - 12, p.x + side * 17, p.y - 12, tint);
                line(h.strokes, p.x + side * 17, p.y - 12, p.x + side * 17, p.y + 12, tint);
                line(h.strokes, p.x + side * 17, p.y + 12, p.x + side * 10, p.y + 12, tint);
            }
        }
        if (!footprint) {
            const auto lifeTarget = std::find_if(contacts.begin(), contacts.end(),
                                                 [](const auto& c) { return c.selected && hasSurfaceLife(c); });
            worldLabel(p,
                       t.warpDestination ? (t.warpReveal > .05 ? "DESTINATION / RESOLVING" : "WARP DESTINATION")
                       : !t.targetLabel.empty() ? t.targetLabel
                       : lifeTarget != contacts.end() ? (marker.aft ? "LIFE DETECTED / AFT" : "LIFE DETECTED")
                       : marker.aft                 ? "TARGET AFT"
                                                    : "TARGET",
                       flightQuantity(t.range), t.targetLabel.empty()&&lifeTarget != contacts.end() ? mint : ice);
        }
    }
    if (missileMarker) {
        const auto& marker = *missileMarker;
        const auto p = marker.point;
        constexpr glm::vec3 alert(1, .42f, .23f);
        if (marker.offscreen) {
            const auto d = marker.direction, across = glm::dvec2(-d.y, d.x);
            for (double side : {-1., 1.}) {
                const auto tail = p - d * 13. + across * (side * 7.);
                line(h.strokes, p.x, p.y, tail.x, tail.y, alert);
            }
        } else {
            diamond(h.strokes, p, 16., alert);
        }
    }
    for (int side : {-1, 1}) {
        line(h.strokes, aimPoint.x + side * 9, aimPoint.y, aimPoint.x + side * 23, aimPoint.y, ice);
        line(h.strokes, aimPoint.x + side * 23, aimPoint.y, aimPoint.x + side * 23, aimPoint.y + 4, ice);
    }
    rect(h.panels, aimPoint.x - 1, aimPoint.y - 1, 2, 2, pearl);
    const auto motion = eyeFromShip * (t.relativeMotion ? t.localRelativeVelocity : t.localVelocity);
    for (int sign : {1, -1}) {
        const auto d = motion * double(sign);
        if (glm::length(d) < 1e-15 || -d.z < glm::length(d) * .12)
            continue;
        const glm::dvec2 p{center + d.x / -d.z * 450 / .7002075 * projectionScale,
                           450 - d.y / -d.z * 450 / .7002075 * projectionScale};
        if (!l.windscreen.contains(p))
            continue;
        const auto colour = sign > 0 ? mint : amber;
        for (int i = 0; i < 24; i++) {
            const double a = i * 6.283185307 / 24, b = (i + 1) * 6.283185307 / 24;
            line(h.strokes, p.x + std::cos(a) * 7, p.y + std::sin(a) * 7, p.x + std::cos(b) * 7,
                 p.y + std::sin(b) * 7, colour);
        }
        if (sign > 0) {
            line(h.strokes, p.x - 16, p.y, p.x - 9, p.y, colour);
            line(h.strokes, p.x + 9, p.y, p.x + 16, p.y, colour);
        } else {
            line(h.strokes, p.x - 5, p.y - 5, p.x + 5, p.y + 5, colour);
            line(h.strokes, p.x - 5, p.y + 5, p.x + 5, p.y - 5, colour);
        }
    }
    size_t secondary = 0;
    if (catalogues && t.phase != "CAVERN EXPLORATION") {
        auto marker = [&](glm::dvec3 d, std::string_view name, double range, glm::vec3 color) {
            if (d.z >= -.01 || secondary >= 3)
                return;
            const glm::dvec2 p{center + d.x / -d.z * 450 / .7002075 * projectionScale,
                               450 - d.y / -d.z * 450 / .7002075 * projectionScale};
            if (!l.windscreen.contains(p) || aim.contains(p))
                return;
            if (worldLabel(p, name, flightQuantity(range), color)) {
                diamond(h.strokes, p, 4, color);
                ++secondary;
            }
        };
        for (const auto& c : contacts)
            if (!c.selected && !c.occluded && hasSurfaceLife(c))
                marker(c.localDirection, "LIFE / " + c.label, c.distanceMetres, mint);
        for (const auto& c : activity)
            if (!c.selected && !c.occluded && c.body.kind == ActivityKind::Harbour && c.distance < 2e7)
                marker(c.localDirection, c.body.name, c.distance,
                       c.body.economicAlert > .5f ? amber : trafficInk);
        for (const auto& c : contacts)
            if (!c.selected && !c.occluded && !hasSurfaceLife(c) && c.angularRadius < .008)
                marker(c.localDirection, c.label, c.distanceMetres, ice);
    }
    if (surface) {
        // Local terrain normal drives the horizon; it is unrelated to galaxy up.
        const auto headUp = eyeFromShip * t.localUp;
        const double viewRoll =
            t.roll + std::atan2(headUp.x, headUp.y) - std::atan2(t.localUp.x, t.localUp.y);
        const double viewPitch = t.pitch + std::asin(std::clamp(-headUp.z, -1., 1.)) -
                                 std::asin(std::clamp(-t.localUp.z, -1., 1.));
        const double cs = std::cos(viewRoll), sn = std::sin(viewRoll);
        for (int pitch : {-20, -10, 0, 10, 20}) {
            const double y = (viewPitch - pitch * .01745329252) * 220;
            if (std::abs(y) > 140)
                continue;
            for (int side : {-1, 1}) {
                const double a = side * (pitch ? 57. : 46.), b = side * (pitch ? 83. : 108.);
                line(h.strokes, center + a * cs + y * sn, 450 - a * sn + y * cs, center + b * cs + y * sn,
                     450 - b * sn + y * cs, pitch ? muted * .5f : mint * .7f);
            }
        }
    }
    if(reconOpen)drawSurfaceRecon(h,*t.recon,aspect);
    if(t.vessel&&t.vessel->open)drawVessel(h,*t.vessel,aspect);
    if(t.discovery&&t.discovery->open)drawDiscovery(h,*t.discovery,aspect);
    return finish(std::move(h));
}
namespace {
using MapRect=XalariaMapRect;
void mapLine(CockpitOverlay& h,MapRect r,glm::dvec2 a,glm::dvec2 b,glm::vec3 color,bool arrow=false){
    const auto delta=b-a;double lo=0,hi=1;
    const auto clip=[&](double p,double q){
        if(std::abs(p)<1e-12)return q>=0;
        const double t=q/p;if(p<0)lo=std::max(lo,t);else hi=std::min(hi,t);return lo<=hi;
    };
    if(!clip(-delta.x,a.x-r.x)||!clip(delta.x,r.x+r.w-a.x)||!clip(-delta.y,a.y-r.y)||!clip(delta.y,r.y+r.h-a.y))return;
    b=a+delta*hi;a+=delta*lo;line(h.strokes,a.x,a.y,b.x,b.y,color);
    if(arrow&&glm::length(b-a)>24){
        const auto d=glm::normalize(b-a),p=a+(b-a)*.62,n=glm::dvec2(-d.y,d.x);
        for(int side:{-1,1})line(h.strokes,p.x,p.y,p.x-d.x*5+n.x*3*side,p.y-d.y*5+n.y*3*side,color);
    }
}
void mapCircle(CockpitOverlay& h,MapRect r,glm::dvec2 p,double radius,glm::vec3 color){
    for(int n=0;n<40;++n){const double a=n*2*std::numbers::pi/40,b=(n+1)*2*std::numbers::pi/40;
        mapLine(h,r,p+glm::dvec2(std::cos(a),std::sin(a))*radius,p+glm::dvec2(std::cos(b),std::sin(b))*radius,color);}
}
void mapButton(CockpitOverlay& h,MapRect r,std::string_view name,bool selected=false,bool enabled=true){
    rect(h.panels,r.x,r.y,r.w,r.h,selected?glm::vec3(.065,.18,.18):glm::vec3(.040,.073,.102));
    const auto color=enabled?(selected?mint:ice):muted*.6f;
    line(h.strokes,r.x,r.y+r.h,r.x+r.w,r.y+r.h,color*.5f);
    fitted(h,name,r.x+12,r.y+(r.h-11)/2,r.w-24,11,color);
}
void mapLabel(CockpitOverlay& h,MapRect r,std::string_view name,glm::dvec2 p,glm::vec3 color,double size=10){
    const double width=std::min(150.,(name.size()+.5)*size*6/7);
    CockpitRect box{p.x,p.y,width,size};
    if(p.x<r.x+3||p.y<r.y+3||p.x+width>r.x+r.w-3||p.y+size>r.y+r.h-3)return;
    for(const auto& existing:h.textRuns)if(existing.bounds.overlaps(box))return;
    fitted(h,name,p.x,p.y,width,size,color);
}
void routeLine(CockpitOverlay& h,const XalariaMapView& v,MapRect r,uint32_t from,uint32_t to,double aspect,glm::vec3 color){
    const double grid=v.catalog->grid,scale=r.h/v.span;
    const glm::dvec2 ga{double(from%v.catalog->grid),double(from/v.catalog->grid)},gb{double(to%v.catalog->grid),double(to/v.catalog->grid)};
    auto delta=gb-ga;for(int k=0;k<2;++k)delta[k]-=std::round(delta[k]/grid)*grid;
    const auto a=xalariaMapPoint(v,from,aspect),b=xalariaMapPoint(v,to,aspect),virtualB=a+delta*scale;
    mapLine(h,r,a,virtualB,color,true);
    if(glm::length(virtualB-b)>1)mapLine(h,r,a+(b-virtualB),b,color,true);
}
void liveMarker(CockpitOverlay& h,MapRect r,glm::dvec2 p){
    p=glm::clamp(p,glm::dvec2(r.x+9,r.y+9),glm::dvec2(r.x+r.w-9,r.y+r.h-9));
    const glm::dvec2 a{p.x,p.y-7},b{p.x-5,p.y+5},c{p.x+5,p.y+5};
    triangle(h.panels,{a,0},{b,0},{c,0},mint);
    mapCircle(h,r,p,12,mint*.6f);
}
void galaxyChart(CockpitOverlay& h,const XalariaMapView& v,double aspect){
    const auto r=XalariaMapLayout(aspect).chart;const auto& c=*v.catalog;
    for(const auto& [seed,s]:c.stars){
        const auto a=xalariaMapPoint(v,seed,aspect);if(!r.contains(a))continue;
        for(auto to:s.wormholes){
            if(seed>to&&std::find(c.star(to).wormholes.begin(),c.star(to).wormholes.end(),seed)!=c.star(to).wormholes.end())continue;
            auto b=xalariaMapPoint(v,to,aspect);
            const auto raw=glm::dvec2(double(to%c.grid)-double(seed%c.grid),double(to/c.grid)-double(seed/c.grid));
            if(std::abs(raw.x)>c.grid/2.||std::abs(raw.y)>c.grid/2.)continue;
            mapLine(h,r,a,b,seed==v.selected||seed==v.current?glm::vec3(.14,.31,.36):glm::vec3(.045,.11,.15),v.span<45);
        }
    }
    for(size_t n=1;n<v.route.size();++n)routeLine(h,v,r,v.route[n-1],v.route[n],aspect,amber*.65f);
    for(size_t n=1;n<v.activeRoute.size();++n)routeLine(h,v,r,v.activeRoute[n-1],v.activeRoute[n],aspect,n<=v.activeLeg?muted*.5f:mint);
    for(const auto& [seed,s]:c.stars){const auto p=xalariaMapPoint(v,seed,aspect);if(!r.contains(p))continue;
        const double size=v.span<50?3.5:1.8;
        rect(h.panels,p.x-size/2,p.y-size/2,size,size,stellarColor(s.temperature));}
    mapCircle(h,r,xalariaMapPoint(v,v.selected,aspect),9,amber);
    if(!v.activeRoute.empty())mapCircle(h,r,xalariaMapPoint(v,v.destination,aspect),12,mint);
    mapLabel(h,r,c.star(v.selected).name,xalariaMapPoint(v,v.selected,aspect)+glm::dvec2(13,-18),amber,12);
    mapLabel(h,r,c.star(v.current).name,xalariaMapPoint(v,v.current,aspect)+glm::dvec2(13,8),mint,11);
    if(v.span<55)for(const auto& [seed,s]:c.stars)mapLabel(h,r,s.name,xalariaMapPoint(v,seed,aspect)+glm::dvec2(7,7),muted,9);
    liveMarker(h,r,xalariaMapPoint(v,v.ship,aspect));
}
void systemChart(CockpitOverlay& h,const XalariaMapView& v,double aspect){
    const auto r=XalariaMapLayout(aspect).chart;const auto& s=v.catalog->star(v.selected);
    const glm::dvec2 centre{r.x+r.w/2,r.y+r.h/2};
    rect(h.panels,centre.x-6,centre.y-6,12,12,stellarColor(s.temperature));
    mapCircle(h,r,centre,16,stellarColor(s.temperature)*.6f);
    for(const auto& [id,w]:s.worlds){const auto p=xalariaSystemPoint(v,id,aspect);
        if(w.primary<0)mapCircle(h,r,centre,glm::length(p-centre),{.065,.13,.18});
        else mapLine(h,r,xalariaSystemPoint(v,uint64_t(w.primary),aspect),p,muted*.4f);
        const double size=w.primary<0?8:4;
        rect(h.panels,p.x-size/2,p.y-size/2,size,size,w.life?mint:w.composition==0?glm::vec3(.65,.65,.87):ice);
        if(w.ringOuter>w.ringInner)mapCircle(h,r,p,8,amber*.65f);
        if(v.selectedWorld==id)mapCircle(h,r,p,13,amber);
        if(v.destination==v.selected&&v.activeWorld==id&&!v.activeRoute.empty())mapCircle(h,r,p,16,mint);
    }
    if(v.selectedWorld)mapLabel(h,r,s.worlds.at(*v.selectedWorld).name,xalariaSystemPoint(v,*v.selectedWorld,aspect)+glm::dvec2(16,-19),amber,12);
    for(const auto& [id,w]:s.worlds)mapLabel(h,r,w.name,xalariaSystemPoint(v,id,aspect)+glm::dvec2(9,9),w.life?mint:pearl);
    if(v.current==v.selected)liveMarker(h,r,xalariaSystemShipPoint(v,aspect));
}
}
CockpitOverlay xalariaMapOverlay(const XalariaMapView& v,double aspect){
    CockpitOverlay h;const auto& c=*v.catalog;const XalariaMapLayout l(aspect);const auto r=l.chart;const double rx=l.details.x,rw=l.details.w;
    rect(h.panels,8,8,l.width-16,884,{.018,.032,.05});
    text(h,"INFINITY / NAVIGATION",24,27,24,pearl);
    const auto liveRegion = c.region(v.current);
    fitted(h,"LIVE / "+c.star(v.current).name+" / "+std::string(xalariaLookName(liveRegion.look)),
           24,61,l.width-48,12,mint);
    mapButton(h,l.galaxy,"GALAXY",v.mode==XalariaMapView::Mode::Galaxy);
    mapButton(h,l.system,"SYSTEM",v.mode==XalariaMapView::Mode::System);
    mapButton(h,l.locate,"LOCATE");
    mapButton(h,l.fitRoute,"FIT ROUTE");
    mapButton(h,l.fitGalaxy,"OVERVIEW");
    mapButton(h,l.economy,"TRADE F4");
    fitted(h,v.mode==XalariaMapView::Mode::Galaxy?"WORMHOLE NETWORK / DRAG TO PAN / SCROLL TO ZOOM":"ORBITAL SCHEMATIC / SELECT PLANETS AND MOONS / NOT TO SCALE",r.x,131,r.w,10,muted);
    rect(h.panels,r.x,r.y,r.w,r.h,{.006,.014,.025});
    if(v.mode==XalariaMapView::Mode::Galaxy){
        const double step=v.span>100?20:v.span>40?10:v.span>15?5:1;
        const auto lo=xalariaMapGrid(v,{r.x,r.y},aspect),hi=xalariaMapGrid(v,{r.x+r.w,r.y+r.h},aspect);
        for(double x=std::ceil(lo.x/step)*step;x<hi.x;x+=step){const double px=r.x+r.w/2+(x-v.centre.x)*r.h/v.span;mapLine(h,r,{px,r.y},{px,r.y+r.h},{.028,.065,.09});}
        for(double y=std::ceil(lo.y/step)*step;y<hi.y;y+=step){const double py=r.y+r.h/2+(y-v.centre.y)*r.h/v.span;mapLine(h,r,{r.x,py},{r.x+r.w,py},{.028,.065,.09});}
        galaxyChart(h,v,aspect);
    }else systemChart(h,v,aspect);
    rect(h.panels,l.search.x,l.search.y,l.search.w,l.search.h,v.searchFocused?glm::vec3(.065,.13,.17):glm::vec3(.035,.07,.10));
    fitted(h,v.query.empty()?"SEARCH STAR OR WORLD / TYPE":v.query,l.search.x+10,l.search.y+10,l.search.w-20,12,ice);
    fitted(h,v.mode==XalariaMapView::Mode::Galaxy?"RUST PLAN / AMBER ACTIVE COURSE / TRIANGLE SHIP":"RUST SELECTION / AMBER ARRIVAL / TRIANGLE SHIP",r.x,715,r.w,9,muted);
    const auto& nearby=c.star(v.current).wormholes;
    const auto& results=v.query.empty()?nearby:v.matches;
    const size_t first=v.query.empty()?0:(v.matchIndex/4)*4;
    for(size_t i=first;i<std::min(results.size(),first+4);++i){
        const double y=l.results.y+(i-first)*24;
        if(!v.query.empty()&&i==v.matchIndex)rect(h.panels,l.results.x,y,l.results.w,22,{.07,.15,.17});
        fitted(h,c.star(results[i]).name,l.results.x+8,y+5,l.results.w-70,11,results[i]==v.selected?amber:pearl);
        const bool manual=v.wormhole&&v.wormhole->destination==results[i];
        fitted(h,v.query.empty()?(manual?"MANUAL":"LINK"):std::to_string(i+1),l.results.x+l.results.w-55,y+5,48,9,muted);
    }
    if(results.empty())fitted(h,v.query.empty()?"NO OUTBOUND WORMHOLES":"NO MATCHES",rx,185,rw,12,amber);
    if(!v.query.empty()&&!results.empty())fitted(h,"TAB / SHIFT-TAB / RESULT "+std::to_string(v.matchIndex+1)+" OF "+std::to_string(results.size()),rx,276,rw,8.5,muted);
    const auto& s=c.star(v.selected);
    fitted(h,s.name,rx,294,rw,21,amber);
    fitted(h,"CLASS "+std::string("OBAFGKM").substr(s.spectralClass,1)+" / TECH "+std::to_string(s.technology)+" / "+std::to_string(s.worlds.size())+" WORLDS",rx,329,rw,11,ice);
    fitted(h,v.current==v.selected?"CURRENT SYSTEM":v.route.empty()?"UNREACHABLE BY WORMHOLE":std::to_string(v.route.size()-1)+" HOPS FROM SHIP",rx,355,rw,11,v.route.empty()?amber:mint);
    const auto region = c.region(v.selected);
    fitted(h,"REGION "+std::to_string(region.x)+","+std::to_string(region.y)+" / "+
           std::string(xalariaLookName(region.look)),rx,372,rw,9,ice);
    fitted(h,"WORLDS / CLICK A DESTINATION",rx,386,rw,11,muted);
    size_t index=0,row=0;
    for(const auto& [id,w]:s.worlds){
        if(index++<v.worldScroll)continue;if(row==l.worldRows)break;
        const double y=l.worldY+row++*l.worldStep;
        if(v.selectedWorld==id)rect(h.panels,rx-4,y-5,rw+8,24,{.065,.14,.17});
        fitted(h,(w.primary>=0?"  ":"")+w.name,rx,y,rw*.64,11,v.selectedWorld==id?amber:pearl);
        fitted(h,std::to_string(int(w.temperature))+" K"+(w.life?" LIFE":""),rx+rw*.66,y,rw*.34,9,w.life?mint:muted);
    }
    if(s.worlds.size()>l.worldRows)fitted(h,"PGUP / PGDN / "+std::to_string(v.worldScroll+1)+"-"+std::to_string(std::min(s.worlds.size(),size_t(v.worldScroll+l.worldRows))),rx,635,rw,9,muted);
    if(v.selectedWorld){const auto& w=s.worlds.at(*v.selectedWorld);
        fitted(h,(w.composition==0?"GAS":"SOLID")+std::string(" / RADIUS ")+flightQuantity(w.radius),rx,659,rw,10,ice);
        fitted(h,std::to_string(w.communities)+" COMMUNITIES / "+std::to_string(w.stations)+" STATIONS",rx,681,rw,10,muted);
    }
    const std::string destination=v.activeRoute.empty()?(v.selectedWorld?s.worlds.at(*v.selectedWorld).name:c.star(v.selected).name):
        v.activeWorld?c.star(v.destination).worlds.at(*v.activeWorld).name:c.star(v.destination).name;
    std::string state=v.paused?"PAUSED":v.arrived?"ARRIVED":v.finalApproach?"FINAL APPROACH":v.flying?"IN FLIGHT":"PLANNED";
    fitted(h,state+" / "+destination,24,731,l.width-48,14,v.paused?amber:mint);
    std::string detail;
    if(v.flying||v.paused||v.arrived){
        const size_t hops=v.activeRoute.size()>1?v.activeRoute.size()-1:0;
        detail=(hops?"LEG "+std::to_string(std::min(v.activeLeg+1,hops))+" OF "+std::to_string(hops):"LOCAL FLIGHT")+
            " / "+flightQuantity(v.range)+" / "+std::to_string(int(v.progress*100))+"%";
        if(v.eta>=0&&v.eta<86400)detail+=" / ETA "+std::to_string(int(std::ceil(v.eta)))+"S";
    }else detail=v.route.empty()?"NO WORMHOLE CONNECTION":v.selectedWorld?"SELECTED / "+s.worlds.at(*v.selectedWorld).name:std::to_string(v.route.size()-1)+" HOPS / SELECT A WORLD FOR A PRECISE ARRIVAL";
    fitted(h,detail,24,760,l.width-48,11,ice);
    rect(h.panels,24,785,l.width-48,3,{.05,.10,.13});
    if(v.flying||v.paused||v.arrived)rect(h.panels,24,785,(l.width-48)*v.progress,3,v.paused?amber:mint);
    mapButton(h,l.fly,v.current==v.selected?"FLY TO WORLD":"FLY PLANNED ROUTE",true,!v.route.empty()&&(v.query.empty()||!v.matches.empty())&&(v.current!=v.selected||v.selectedWorld.has_value()));
    mapButton(h,l.pause,v.paused?"RESUME JOURNEY":"PAUSE JOURNEY",false,v.flying||v.paused);
    mapButton(h,l.cancel,"CANCEL",false,v.flying||v.paused);
    if(v.wormhole&&v.wormhole->destination==v.selected)
        mapButton(h,l.wormhole,"TARGET WORMHOLE",false);
    fitted(h,v.message,24,855,l.width-48,11,amber);
    fitted(h,"F2 FLIGHT / F3 GALAXY-SYSTEM / ENTER FLY / SPACE PAUSE-RESUME / CTRL-F SEARCH",24,877,l.width-48,9,muted);
    h.hitRegions.push_back({8,8,l.width-16,884});return h;
}
void xalariaFlightOverlay(CockpitOverlay& h,const XalariaMapView& v,double aspect){
    const double width=900*aspect;const CockpitRect box{width-342,145,318,182};
    occlude(h,box);rect(h.panels,box.x,box.y,box.w,box.h,{.016,.038,.055},.96f);
    const auto& c=*v.catalog;const double x=box.x+12,w=box.w-24;
    fitted(h,v.wormhole?"F2 ATLAS / H WORMHOLE":"NAVIGATION / F2 ATLAS",x,box.y+12,w,11,ice);
    const std::string state=v.paused?"PAUSED / ":v.finalApproach?"APPROACH / ":v.flying?"COURSE / ":v.arrived?"ARRIVED / ":"PLAN / ";
    const auto to=v.activeRoute.empty()?v.selected:v.destination;
    const auto world=v.activeRoute.empty()?v.selectedWorld:v.activeWorld;
    fitted(h,state+(world?c.star(to).worlds.at(*world).name:c.star(to).name),x,box.y+37,w,12,v.paused?amber:mint);
    fitted(h,"HERE / "+c.star(v.current).name+" / "+std::string(xalariaLookName(c.region(v.current).look)),
           x,box.y+61,w,10,pearl);
    const MapRect diagram{x,box.y+88,w,43};
    const auto project=[&](glm::dvec2 p){auto d=p-v.ship;return glm::dvec2(diagram.x+diagram.w*.5,diagram.y+diagram.h*.5)+d*8.;};
    for(const auto& [id,s]:c.stars){const auto p=project({double(id%c.grid),double(id/c.grid)});if(diagram.contains(p))rect(h.panels,p.x-1,p.y-1,2,2,muted);}
    const auto& path=!v.activeRoute.empty()?v.activeRoute:v.route;
    for(size_t n=1;n<path.size();++n)mapLine(h,diagram,project({double(path[n-1]%c.grid),double(path[n-1]/c.grid)}),project({double(path[n]%c.grid),double(path[n]/c.grid)}),mint);
    liveMarker(h,diagram,{diagram.x+diagram.w*.5,diagram.y+diagram.h*.5});
    const auto hops=v.activeRoute.size()>1?v.activeRoute.size()-1:0;
    std::string info=v.activeRoute.empty()?(v.route.empty()?"NO WORMHOLE ROUTE":std::to_string(v.route.size()-1)+" HOPS PLANNED"):
        (hops?"LEG "+std::to_string(std::min(v.activeLeg+1,hops))+"/"+std::to_string(hops)+" / ":"LOCAL / ")+flightQuantity(v.range)+" / "+std::to_string(int(v.progress*100))+"%";
    fitted(h,info,x,box.y+145,w,10,muted);
    h.hitRegions.push_back(box);
}

XEconomyLayout::XEconomyLayout(double aspect):width(900*aspect){
 const double split=width*.59;
 for(size_t i=0;i<5;++i)tabs[i]={24+(width-48)/5*i,142,(width-48)/5-8,38};
 rows={24,290,split-48,384};details={split,290,width-split-24,384};
 buy={24,700,(rows.w-12)/2,38};sell={buy.x+buy.w+12,700,buy.w,38};
 minus={24,751,44,36};plus={190,751,44,36};
 travel={split,222,details.w,42};venue={24,222,rows.w,42};
 action={split,700,details.w,38};secondary={split,751,details.w,36};tertiary={split,800,details.w,36};
}
void xalariaEconomyHud(CockpitOverlay& h,const XalariaEconomy& e,double aspect,bool map){
 const double w=900*aspect;const CockpitRect b{w-342,map?14.:342.,318,map?60.:68.};occlude(h,b);
 rect(h.panels,b.x,b.y,b.w,b.h,ink,.96);
 fitted(h,xMoney(e.credits())+" / F4 ECONOMY",b.x+12,b.y+12,b.w-24,11,mint);
 fitted(h,"FUEL "+std::to_string(int(e.fuel()))+"/"+std::to_string(int(e.fuelCapacity()))+
  " / HOLD "+std::to_string(e.cargoUnits())+"/"+std::to_string(e.holdCapacity()),b.x+12,b.y+39,b.w-24,10,pearl);
 h.hitRegions.push_back(b);
}
CockpitOverlay xalariaEconomyOverlay(const XalariaEconomy& e,double aspect){
 CockpitOverlay h;const XEconomyLayout l(aspect);const auto& v=e.view;
 rect(h.panels,8,8,l.width-16,884,ink,.99f);
 fitted(h,"INFINITY / XALARIA",24,28,l.width-48,13,ice);
 fitted(h,"TRADE, INDUSTRY AND CULTURE",24,58,l.width-48,21,pearl);
 fitted(h,xMoney(e.credits())+" / HOLD "+std::to_string(e.cargoUnits())+"/"+std::to_string(e.holdCapacity())+
  " / FUEL "+std::to_string(int(e.fuel()))+"/"+std::to_string(int(e.fuelCapacity()))+
  " / ENERGY "+std::to_string(int(e.energy()*5))+"%",24,101,l.width-48,12,mint);
 constexpr std::array<std::string_view,5> tabs{"1 MARKET","2 HOLDINGS","3 MINING","4 SERVICES","5 CHARTERS"};
 for(size_t i=0;i<5;++i)mapButton(h,l.tabs[i],tabs[i],size_t(v.page)==i);
 const bool local=e.canTrade(v.place);const auto list=e.venues(v.place.system,v.place.world);
 fitted(h,e.placeName(v.place),24,197,l.width-48,13,ice);
 fitted(h,xFactionName(e.faction(v.place)),l.details.x,276,l.details.w,9,muted);
 mapButton(h,l.venue,list.size()>1?"CHANGE STATION / CITY":"LOCAL STATION / CITY");
 mapButton(h,l.travel,local?"UNDOCK / LAUNCH":"TRAVEL AND DOCK",!local);
 const auto detail=[&](std::string_view s,unsigned row,glm::vec3 color=pearl){fitted(h,s,l.details.x+12,l.details.y+row*31,l.details.w-24,11,color);};
 const auto row=[&](std::string_view name,size_t n,bool selected,glm::vec3 color=pearl){
  if(n>=12)return;const double y=l.rows.y+n*32;if(selected)rect(h.panels,l.rows.x,y-7,l.rows.w,30,{.07,.18,.20});
  fitted(h,name,l.rows.x+10,y,l.rows.w-20,11,selected?mint:color);
 };
 if(v.page==XEconomyView::Page::Market){
  fitted(h,"COMMODITY",34,276,l.rows.w*.44,10,muted);
  fitted(h,"BUY / SELL GC",l.rows.x+l.rows.w*.48,276,l.rows.w*.31,10,muted);
  fitted(h,"STOCK / HOLD",l.rows.x+l.rows.w*.82,276,l.rows.w*.18-10,9,muted);
  for(size_t i=0;i<xGoodCount;++i){
   const auto good=XGood(i);const auto buy=e.quote(v.place,good,1,true),sell=e.quote(v.place,good,1,false);
   const double y=l.rows.y+i*32;if(v.good==i)rect(h.panels,l.rows.x,y-7,l.rows.w,30,{.07,.18,.20});
   fitted(h,xGoodInfo(good).name,34,y,l.rows.w*.43,10,v.good==i?mint:pearl);
   fitted(h,(buy.available?xMoney(buy.total):"-")+" / "+(sell.available?xMoney(sell.total):"-"),
    l.rows.x+l.rows.w*.48,y,l.rows.w*.32,9,ice);
   fitted(h,std::to_string(e.stock(v.place,good))+" / "+std::to_string(e.cargo()[i]),
    l.rows.x+l.rows.w*.82,y,l.rows.w*.18-10,10,pearl);
  }
  const auto good=XGood(v.good);const auto buy=e.quote(v.place,good,v.quantity,true),sell=e.quote(v.place,good,v.quantity,false);
  detail(xGoodInfo(good).name,0,mint);detail(local?"MARKET ACCESS OPEN":"REMOTE QUOTE / TRAVEL TO TRADE",2,local?mint:amber);
  detail("QUANTITY "+std::to_string(v.quantity),4);
  detail("BUY TOTAL "+(buy.available?xMoney(buy.total):"UNAVAILABLE"),5);
  detail("SELL TOTAL "+(sell.available?xMoney(sell.total):"UNAVAILABLE"),6);
  detail("TOTALS INCLUDE LOCAL FEES",7,muted);detail("GOODWILL "+std::to_string(e.goodwill()),9);
  detail("NEXT TRADE REBATE "+xMoney(e.goodwillDue()),10);
  mapButton(h,l.buy,"BUY CARGO / B",true,local&&buy.available);
  mapButton(h,l.sell,"SELL CARGO / S",false,local&&sell.available);
  mapButton(h,l.action,"FIND PROFITABLE ROUTE");
  mapButton(h,l.secondary,"REFILL FUEL / R");
  mapButton(h,l.tertiary,"DELIVER PARCELS");
 }else if(v.page==XEconomyView::Page::Holdings){
  const size_t first=(v.asset/12)*12;
  for(size_t i=first;i<e.assets().size()&&i<first+12;++i){const auto& a=e.assets()[i];
   row(std::to_string(a.id)+" / "+(a.kind==XAssetKind::Station?"STATION":a.kind==XAssetKind::Vault?"VAULT":"FARM")+
    " / "+(a.state==XAssetState::Frame?"FRAME":a.state==XAssetState::Active?"ACTIVE":"LOST"),i-first,i==v.asset);
  }
  if(v.asset<e.assets().size()){
   const auto& a=e.assets()[v.asset];detail("HOLDING "+std::to_string(a.id)+" / LEVEL "+std::to_string(a.level),0,mint);
   detail("NEEDS "+std::to_string(a.nanoNeeded)+" NANO / "+std::to_string(a.metalNeeded)+" METAL",2);
   detail("SEED "+std::to_string(a.seedNeeded)+" / GUARDS "+std::to_string(a.guards),3);
   detail("INCOME "+xMoney(e.dailyIncome(a))+" PER DAY",5);detail("TILL "+xMoney(a.till+a.culturalTill),6);
   detail("H HALL / G GALLERY / O DONATE ART",8,muted);detail("T TRADE FLAG / U VAULT FUEL",9,muted);
   detail("V STORE / R RETRIEVE / LEFT-RIGHT CARGO",10,muted);
   detail(std::string(xGoodInfo(XGood(v.good)).name)+" / "+std::to_string(v.quantity)+" UNITS",11,ice);
  }else{detail("BUILD YOUR FIRST HOLDING",0,mint);detail("SERVICES: STATIONS, VAULTS, FARMS",2);detail("CARRY MATERIALS TO THE WORLD",4);detail("FEED A FRAME WITHIN SEVEN DAYS",5);}
  mapButton(h,l.buy,"PREVIOUS HOLDING");mapButton(h,l.sell,"NEXT HOLDING");
  mapButton(h,l.action,"FEED MATERIALS");mapButton(h,l.secondary,"COLLECT TILLS");mapButton(h,l.tertiary,"ARRANGE CITY GUARDS");
 }else if(v.page==XEconomyView::Page::Mining){
  std::vector<const XDeposit*> deposits;
  for(const auto& [id,d]:e.deposits())if(d.system==v.place.system&&d.world==v.place.world)deposits.push_back(&d);
  const size_t first=(v.deposit/12)*12;
  for(size_t i=first;i<deposits.size()&&i<first+12;++i){const auto& d=*deposits[i];
   row(std::to_string(d.index+1)+" / "+std::string(xGoodInfo(d.good).name)+" / "+(d.filled?"RESTORED":std::to_string(d.left)+" LEFT"),i-first,i==v.deposit);
  }
  detail("SURVEY AND EXTRACT FINITE RESOURCES",0,mint);
  detail(e.landed()?"SHIP ON THE SURFACE":"LAND TO SURVEY OR MINE",2,e.landed()?mint:amber);
  detail("TECHNOLOGY LEVEL "+std::to_string(e.technology()),4);
  detail("TOOL ENERGY "+std::to_string(int(e.energy()*5))+"%",5);
  detail("STATION SERVICES SELL MINING TOOLS",7,muted);
  detail("FILLING RESTORES AND CLOSES A DEPOSIT",9,muted);
  mapButton(h,l.buy,"PREVIOUS DEPOSIT");mapButton(h,l.sell,"NEXT DEPOSIT");
  mapButton(h,l.action,"SURVEY WORLD");mapButton(h,l.secondary,"MINE SELECTED DEPOSIT");mapButton(h,l.tertiary,"RESTORE SELECTED DEPOSIT");
 }else if(v.page==XEconomyView::Page::Services){
  constexpr std::array<std::string_view,11> services{
   "REFILL FUEL TANK","TREMBLE MINING TOOL / 1500 GC","PRECISION TOOL / 6000 GC","EXCHANGE FOR LARGER TRADER",
   "BUY AUTHENTIC ORNAMENT","VISIT CITY CULTURAL CENTRE","SEED ORBITAL STATION","SEED PRIVATE VAULT","CLEAR FARM FIELD","SURVEY LOCAL WORLD","MINE FILLING TOOL / 100 GC"};
  for(size_t i=0;i<services.size();++i)row(services[i],i,v.good==i);
  detail("TECHNOLOGY "+std::to_string(e.technology())+" / CULTURE "+std::to_string(e.culture()),0,mint);
  detail("STATION ADVANCEMENT "+std::to_string(v.buildLevel),2);
  detail("BUILD CHANCE "+std::to_string(XalariaEconomy::constructionChance(v.buildLevel,e.technology()))+"%",3);
  detail("SEED: "+std::to_string(5+v.buildLevel)+" NANOMITES",5);
  detail("TOTAL: "+std::to_string(XalariaEconomy::stationNanomites(v.buildLevel))+" NANOMITES",6);
  detail("METAL: "+std::to_string(v.buildLevel*20)+" STRUCTURAL UNITS",7);
  detail("STATION INCOME "+xMoney(XalariaEconomy::stationIncome(v.buildLevel))+"/DAY",9);
  detail("VAULT: 7 NANO + 40 METAL",10,muted);
  mapButton(h,l.buy,"LOWER ADVANCEMENT");mapButton(h,l.sell,"HIGHER ADVANCEMENT");
  mapButton(h,l.action,"USE SELECTED SERVICE / ENTER");
 }else{
  const auto offer=e.offer(v.place);row("TODAY'S CHARTER / "+xMoney(offer.reward),0,v.good==0);
  for(size_t i=0;i<e.contracts().size()&&i<10;++i){const auto& c=e.contracts()[e.contracts().size()-1-i];
   row((c.state==XContract::State::Active?"IN HOLD":c.state==XContract::State::Delivered?"DELIVERED":"EXPIRED")+std::string(" / ")+xMoney(c.reward),i+1,v.good==i+1);
  }
  const XContract* c=&offer;
  if(v.good>0&&v.good<=e.contracts().size())c=&e.contracts()[e.contracts().size()-v.good];
  detail(c->units?e.placeName(c->destination):"NO DELIVERY CHARTER TODAY",0,mint);
  detail("SEALED CARGO "+std::to_string(c->units)+" UNITS",2);detail("REWARD "+xMoney(c->reward),4);
  detail("DEADLINE: DAY "+std::to_string(c->deadline),5);
  detail("DOCK AT THE DESTINATION TO DELIVER",7,muted);detail("PARCELS USE YOUR SHIP'S CARGO HOLD",9,muted);
  mapButton(h,l.action,"ACCEPT TODAY'S CHARTER");mapButton(h,l.secondary,"DELIVER PARCELS");mapButton(h,l.tertiary,"PLOT DELIVERY DESTINATION");
 }
 if(v.page==XEconomyView::Page::Market){
  mapButton(h,l.minus,"-");mapButton(h,l.plus,"+");fitted(h,std::to_string(v.quantity)+" UNITS",79,762,103,12,pearl);
 }
 fitted(h,v.message,24,853,l.width-48,11,amber);
 fitted(h,"F4 CLOSE / F2 ATLAS / ARROWS SELECT / ENTER ACT / D TRAVEL / DAY "+std::to_string(e.day())+
  " / ECONOMY TIME 60X",24,878,l.width-48,9,muted);
 h.hitRegions.push_back({8,8,l.width-16,884});return h;
}

} // namespace infinity
