#include "infinity/Cockpit.hpp"
#include "infinity/ShipInterior.hpp"
#include "AtlasFont.hpp"
#include "infinity/Xalaria.hpp"
#include "infinity/XalariaEconomy.hpp"
#include "infinity/XalariaTraffic.hpp"
#include "infinity/XalariaStories.hpp"
#include "infinity/XalariaCombat.hpp"
#include "infinity/XalariaNavigation.hpp"
#include "infinity/StellarEvolution.hpp"
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
#include <initializer_list>
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
void line(Mesh& m, double x, double y, double a, double b, glm::vec3 c, float alpha=1.f) {
    addLine(m, {x, y, 0}, {a, b, 0}, c);
    if(alpha<1.f)
        m.vertices[m.vertices.size()-2].color=m.vertices.back().color=rgba(c,alpha);
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
XalariaRadioLayout::XalariaRadioLayout(double aspect) {
    const double w=900.*aspect;
    const double width=std::min(610.,w-48.);
    panel={(w-width)*.5,563,width,175};
    close={panel.x+width-81,571,68,18};
    const double slot=(width-24.)/5.;
    for(size_t i=0;i<5;++i)
        actions[i]={panel.x+12.+slot*i,690,slot-3.,20.};
    actions[5]={panel.x+12.,713,slot-3.,20.};
    actions[6]={panel.x+12.+slot,713,slot*1.45-3.,20.};
    actions[7]={panel.x+12.+slot*2.45,713,slot*1.30-3.,20.};
}

void xalariaRadioOverlay(CockpitOverlay& h,const XalariaRadioView& radio,double aspect) {
    const XalariaRadioLayout layout(aspect);
    const auto p=layout.panel;
    occlude(h,p);
    card(h,p,mint);
    rect(h.panels,p.x+2.,p.y+1.,p.w-2.,p.h-2.,ink);
    const double x=p.x+13.,width=p.w-26.;
    atlasText(h,"Traffic comms",x,572,width-78.,14.,mint);
    atlasText(h,"Esc  Close",layout.close.x+3,573,layout.close.w-6,12.,pearl*.75f);
    atlasText(h,radio.callsign,x,592,width*.27,18.,pearl);
    atlasText(h,radio.activity,x+width*.29,595,width*.71,13.,mint);
    std::string_view reply=radio.reply;
    for(unsigned line=0;line<3&&!reply.empty();++line) {
        size_t end=reply.size();
        if(line<2) {
            while(end>0&&atlasTextWidth(reply.substr(0,end),16.)>width) {
                const auto space=reply.rfind(' ',end-1);
                if(space==reply.npos||space==0)break;
                end=space;
            }
        }
        atlasText(h,reply.substr(0,end),x,614.+line*17.,width,16.,pearl);
        reply.remove_prefix(std::min(reply.size(),end+1));
    }
    const bool shielded=radio.shield.capacity>0.;
    const double shieldWidth=shielded?width*.34:0.;
    atlasText(h,radio.rendezvous,x,668.,width-shieldWidth-(shielded?10.:0.),14.,mint);
    if(shielded){
        const auto label=!radio.shield.raised?"Shield lowered":radio.shield.fraction<=.001?"Shield depleted":
            "Shield "+std::to_string(int(std::round(100.*radio.shield.fraction)))+"%";
        atlasText(h,label,x+width-shieldWidth,668.,shieldWidth,14.,radio.shield.raised?shieldBlue:muted);
    }
    if(radio.selected) {
        const auto& freight=radio.freight;
        const std::string_view guide=freight?(freight->aboard?"6  Delivery page":"6  Pickup page"):"";
        const std::array<std::string_view,8> names{"I  Hail","1  Flight plan","2  Advisory","3  Rendezvous",
                                                   "4  Port market","5  Freight work",guide,"7  Request dock"};
        for(size_t i=0;i<names.size();++i) {
            if(names[i].empty())continue;
            const auto r=layout.actions[i];
            rect(h.panels,r.x,r.y,r.w,r.h,ice*.18f);
            const double size=std::min(13.,(r.w-9.)/atlasTextWidth(names[i],1.));
            atlasText(h,names[i],r.x+5,r.y+4,r.w-9.,size,pearl);
        }
    }
}

void xalariaShieldHud(CockpitOverlay& h,double aspect,const ShieldReadout& shield) {
    if(shield.capacity<=0.)return;
    const auto panel=CockpitLayout(aspect).systems;
    const double room=panel.w-28.,meter=room*.34;
    const auto label=!shield.raised?"SHIELD LOWERED":shield.fraction<=.001?"SHIELD DEPLETED":
        "SHIELD "+std::to_string(int(std::round(shield.fraction*100)))+"%";
    const auto colour=shield.raised?(shield.fraction<=.001?amber:shieldBlue):muted;
    fitted(h,label,panel.x+14.,panel.y+90.,room-meter-8.,10.,colour);
    rect(h.panels,panel.x+14.+room-meter,panel.y+94.,meter,3.,muted*.34f);
    if(shield.raised&&shield.fraction>0.)
        rect(h.panels,panel.x+14.+room-meter,panel.y+94.,meter*shield.fraction,3.,colour);
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
void FlightHudStability::update(double seconds,double turnRate,bool available) {
    if (!available) {
        quietSeconds_ = 0;
        opacity_ = 0;
        return;
    }
    // Hysteresis prevents the overlay from flickering around the turn-rate threshold.
    const double limit = showMarkers() ? glm::radians(32.) : glm::radians(18.);
    quietSeconds_ = turnRate<=limit ? std::min(.24,quietSeconds_+seconds) : 0.;
    opacity_ = std::min(1.f,opacity_+float(seconds/.22));
}
void DeckHudLabel::update(std::string_view label,double seconds) {
    if (shown_.empty())
        shown_=label;
    if (label!=candidate_) {
        candidate_=label;
        heldSeconds_=0.;
    }
    const double previousHeld=heldSeconds_;
    heldSeconds_+=seconds;
    if (label==shown_) {
        opacity_=std::min(1.f,opacity_+float(seconds/.12));
        return;
    }
    const double fade=std::max(0.,heldSeconds_-std::max(previousHeld,settleSeconds));
    opacity_=std::max(0.f,opacity_-float(fade/.075));
    if (fade>0. && opacity_==0.f)
        shown_=candidate_;
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
enum class CabinMaterial {
    Painted, Polymer, Brushed, Bronze, Display, Textile, Tread, Ceramic,
    AcousticPanel, Carpet, MineralComposite, EnamelPanel, CarbonComposite
};
struct CabinFinish {
    glm::vec3 colour;
    CabinMaterial material;
    float vertexAlpha(float emission) const {
        // The high nibble selects a finish and the low nibble stores emission.
        return (float(material)*16.f + std::round(std::clamp(emission,0.f,1.f)*15.f)) / 255.f;
    }
};
const glm::vec3 cabinLamp{.87f,.79f,.63f}, cabinSignal{.24f,.78f,.68f};
const CabinFinish cabinFrame{{.113f,.152f,.178f},CabinMaterial::Painted},
    cabinLiner{{.047f,.069f,.086f},CabinMaterial::Painted},
    cabinRubber{{.029f,.034f,.037f},CabinMaterial::Polymer},
    cabinTrim{{.32f,.36f,.38f},CabinMaterial::Brushed},
    cabinCopper{{.54f,.29f,.126f},CabinMaterial::Bronze},
    cabinCeramic{{.43f,.45f,.41f},CabinMaterial::Ceramic},
    cabinUpholstery{{.17f,.21f,.22f},CabinMaterial::Textile},
    cabinScreen{{.014f,.031f,.032f},CabinMaterial::Display},
    cabinComposite{{.083f,.105f,.118f},CabinMaterial::CarbonComposite},
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
const CabinSurface navigationDeckScreen{{-.715,.547,1.998},{.113,0,-.551},{0,-.326,0}};
const CabinSurface systemsDeckScreen{{1.533,.556,2.085},{0,0,.908},{0,-.535,0}};
const CabinSurface cargoDeckScreen{{shipDeck::cargoScreenHalf,shipDeck::cargoScreenTop,shipDeck::cargoScreenZ},
                                   {-2*shipDeck::cargoScreenHalf,0,0},{0,-shipDeck::cargoScreenHeight,0}};
CabinSurface stationFreightScreen(const StationLayout& station) {
    return {{station.freightBay().floor.left+.095,1.36,stationDeck::centre+.99},
            {0,0,-1.98},{0,-1.10,0}};
}
CabinSurface stationSurveyScreen(const StationAnnex& annex) {
    const auto& area=annex.room.floor;
    const double x=(area.left+area.right)*.5;
    return {{x+annex.side*.94,1.25,annex.outsideWall()-annex.side*.096},
            {-annex.side*1.88,0,0},{0,-.95,0}};
}
constexpr double observationFore=5.09, observationAft=6.01;
constexpr double observationSill=.31, observationHead=.96, observationCorner=.116;
std::array<glm::dvec3,8> cargoWindow(double x,double margin) {
    const double front=observationFore-margin, back=observationAft+margin;
    const double low=observationSill-margin, high=observationHead+margin;
    const double chamfer=observationCorner+margin*.55;
    return {{{x,low,front+chamfer},{x,low,back-chamfer},
             {x,low+chamfer,back},{x,high-chamfer,back},
             {x,high,back-chamfer},{x,high,front+chamfer},
             {x,high-chamfer,front},{x,low+chamfer,front}}};
}
const glm::vec3 berthLight{1.f,.60f,.31f}, engineeringLight{.34f,.85f,.94f}, cargoLight{1.f,.76f,.38f};
const glm::dvec3 yokePivot{0,-.640,-.888};

struct CabinBuilder {
    Mesh mesh;
    static constexpr std::array<glm::dvec2,8> beamOutline{{
        {-1,-.72},{-.72,-1},{.72,-1},{1,-.72},{1,.72},{.72,1},{-.72,1},{-1,.72}}};
    using BeamSection = std::array<glm::dvec3,beamOutline.size()>;

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
        const auto front = beamSection(from, across, vertical), back = beamSection(to, across, vertical);
        beamSides(front, back, finish, emission);
        beamCap(from, front, finish, emission, false);
        beamCap(to, back, finish, emission, true);
    }

    void tube(glm::dvec3 from,glm::dvec3 to,double radius,const CabinFinish& finish) {
        const auto axis=glm::normalize(to-from);
        const auto reference=std::abs(axis.y)>.9?glm::dvec3(0,0,1):glm::dvec3(0,1,0);
        const auto across=glm::normalize(glm::cross(axis,reference));
        const auto vertical=glm::cross(axis,across);
        const auto ring=[&](glm::dvec3 centre,unsigned sector){
            const auto angle=sector*2*std::numbers::pi/32.;
            return centre+(across*std::cos(angle)+vertical*std::sin(angle))*radius;
        };
        for(unsigned i=0;i<32;++i){
            const auto j=(i+1)%32;
            face(ring(from,i),ring(to,i),ring(to,j),ring(from,j),finish);
            triangle(mesh,from,ring(from,j),ring(from,i),finish.colour,finish.vertexAlpha(0));
            triangle(mesh,to,ring(to,i),ring(to,j),finish.colour,finish.vertexAlpha(0));
        }
    }

    void fastener(glm::dvec3 centre, glm::dvec3 normal, double radius) {
        normal=glm::normalize(normal);
        const auto axis=std::abs(normal.y)<.9?glm::dvec3(0,1,0):glm::dvec3(1,0,0);
        const auto right=glm::normalize(glm::cross(normal,axis)),up=glm::cross(normal,right);
        const auto at=[&](unsigned i,double r,double depth){
            const double a=i*2*std::numbers::pi/8.;
            return centre+normal*depth+(right*std::cos(a)+up*std::sin(a))*r;
        };
        for(unsigned i=0;i<8;++i){
            const auto j=(i+1)%8;
            // A recessed black washer, bevel and inset face have separate depths.
            face(at(i,radius*1.38,0),at(j,radius*1.38,0),at(j,radius,.0018),at(i,radius,.0018),cabinRubber);
            face(at(i,radius,.0018),at(j,radius,.0018),at(j,radius*.71,.0039),at(i,radius*.71,.0039),cabinTrim);
            triangle(mesh,centre+normal*.0039,at(i,radius*.71,.0039),at(j,radius*.71,.0039),
                     cabinTrim.colour,cabinTrim.vertexAlpha(0));
        }
        const auto slot=centre+normal*.0042;
        face(slot-right*radius*.53-up*radius*.12,slot+right*radius*.53-up*radius*.12,
             slot+right*radius*.53+up*radius*.12,slot-right*radius*.53+up*radius*.12,cabinRubber);
    }

    void canopyRail(const std::array<glm::dvec3,4>& path, double side, glm::dvec2 offset,
                    double width, double height, const CabinFinish& finish) {
        const auto centre = [&](size_t i) { return cabinSide(path[i] + glm::dvec3(offset,0), side); };
        const auto section = [&](size_t i) {
            return beamSection(centre(i), {width * .5,0,0}, {0,height * .5,0});
        };
        auto previous = section(0);
        beamCap(centre(0), previous, finish, 0, false);
        for (size_t i = 1; i < path.size(); ++i) {
            const auto current = section(i);
            beamSides(previous, current, finish, 0);
            previous = current;
        }
        beamCap(centre(path.size()-1), previous, finish, 0, true);
    }

    static BeamSection beamSection(glm::dvec3 centre, glm::dvec3 across, glm::dvec3 vertical) {
        BeamSection section;
        for (size_t i = 0; i < section.size(); ++i)
            section[i] = centre + beamOutline[i].x * across + beamOutline[i].y * vertical;
        return section;
    }

    void beamSides(const BeamSection& front, const BeamSection& back, const CabinFinish& finish, float emission) {
        for (size_t i = 0; i < front.size(); ++i) {
            const size_t j = (i + 1) % front.size();
            face(front[i], back[i], back[j], front[j], finish, emission);
        }
    }

    void beamCap(glm::dvec3 centre, const BeamSection& section, const CabinFinish& finish, float emission, bool back) {
        for (size_t i = 0; i < section.size(); ++i) {
            const auto a = section[i], b = section[(i + 1) % section.size()];
            triangle(mesh, centre, back ? b : a, back ? a : b, finish.colour, finish.vertexAlpha(emission));
        }
    }

    void panel(const CabinSurface& surface, double bevel, double depth, const CabinFinish& faceFinish,
               const CabinFinish& edgeFinish) {
        const double u=std::min(.24,bevel/glm::length(surface.right));
        const double v=std::min(.24,bevel/glm::length(surface.down));
        const glm::dvec3 offset=surface.normal()*depth;
        const std::array<glm::dvec3,4> rim{surface.point(0,0),surface.point(1,0),
                                            surface.point(1,1),surface.point(0,1)};
        const std::array<glm::dvec3,4> top{surface.point(u,v)+offset,surface.point(1-u,v)+offset,
                                           surface.point(1-u,1-v)+offset,surface.point(u,1-v)+offset};
        face(top[0],top[1],top[2],top[3],faceFinish);
        for (size_t i=0;i<rim.size();++i) {
            const size_t next=(i+1)%rim.size();
            face(rim[i],rim[next],top[next],top[i],edgeFinish);
        }
    }

    // Turned fittings have a real recessed centre, chamfered lip and separate
    // face depths. Every part shares the hull frame, including during boost.
    void dial(const CabinSurface& surface, double u, double v, double radius, bool vent) {
        const auto normal=surface.normal(), centre=surface.point(u,v)+normal*.009;
        const auto right=glm::normalize(surface.right), down=glm::normalize(surface.down);
        const auto at=[&](unsigned i,double r,double depth) {
            const double angle=i*2*std::numbers::pi/48.;
            return centre+(right*std::cos(angle)+down*std::sin(angle))*(radius*r)+normal*depth;
        };
        const std::array<glm::dvec2,5> profile{{{1.16,0},{1.08,.005},{1.02,.014},{.85,.020},{.77,.010}}};
        for(unsigned i=0;i<48;++i){
            const auto j=(i+1)%48;
            for(size_t band=1;band<profile.size();++band){
                const auto a=profile[band-1],b=profile[band];
                face(at(i,a.x,a.y),at(j,a.x,a.y),at(j,b.x,b.y),at(i,b.x,b.y),
                     band==1?cabinRubber:band==3?cabinCopper:cabinTrim);
            }
            triangle(mesh,centre+normal*.004,at(i,.77,.004),at(j,.77,.004),
                     cabinRubber.colour,cabinRubber.vertexAlpha(0));
            if(vent && i%3==0){
                const auto a=at(i,.72,.010),b=at(i+2,.30,.014);
                beam(a,b,radius*.075,.005,cabinTrim);
            }else if(!vent){
                face(at(i,.67,.012),at(j,.67,.012),at(j,.60,.027),at(i,.60,.027),
                     i%3==0?cabinTrim:cabinComposite);
                triangle(mesh,centre+normal*.027,at(i,.60,.027),at(j,.60,.027),
                         cabinComposite.colour,cabinComposite.vertexAlpha(0));
                if(i>=5 && i<=34)
                    face(at(i,.91,.021),at(j,.91,.021),at(j,.94,.021),at(i,.94,.021),
                         i<27?cabinStatusLight:cabinWarmLight,.40f);
            }
        }
        if(vent){
            for(unsigned i=0;i<48;++i){
                const auto j=(i+1)%48;
                face(at(i,.28,.012),at(j,.28,.012),at(j,.22,.018),at(i,.22,.018),cabinCopper);
            }
        }else{
            beam(centre+normal*.029-down*radius*.17,centre+normal*.029-down*radius*.49,
                 radius*.11,.003,cabinWarmLight,.35f);
        }
    }

    void instrument(const CabinSurface& surface, bool deckTerminal = false) {
        const auto at = [&](double x, double y) { return surface.point(x, y); };
        const auto a=at(0,0), b=at(1,0), c=at(1,1), d=at(0,1);
        const auto rim = surface.normal() * .014;
        const auto backing = surface.normal() * .095;
        face(a,b,c,d,cabinScreen);
        face(a,b,b-backing,a-backing,cabinFrame);
        face(b,c,c-backing,b-backing,cabinFrame);
        face(c,d,d-backing,c-backing,cabinFrame);
        face(d,a,a-backing,d-backing,cabinFrame);
        if (deckTerminal) {
            const auto top = surface.down * -.037, bottom = -top;
            const auto side = surface.right * .024;
            beam(a+top+rim,b+top+rim,.032,.027,cabinTrim);
            beam(d+bottom+rim,c+bottom+rim,.037,.033,cabinFrame);
            beam(a+top-side+rim,d+bottom-side+rim,.040,.034,cabinFrame);
            beam(b+top+side+rim,c+bottom+side+rim,.040,.034,cabinFrame);
            for (double x : {.10,.16,.22})
                beam(at(x,1.083),at(x+.021,1.083),.012,.010,cabinStatusLight,.64f);
        } else {
            beam(a+rim,b+rim,.052,.036,cabinTrim);
            beam(d+rim,c+rim,.061,.045,cabinFrame);
            beam(a+rim,d+rim,.043,.041,cabinFrame);
            beam(b+rim,c+rim,.043,.041,cabinFrame);
        }
        for(double x:{0.,1.})for(double y:{0.,1.})
            fastener(at(x,y)+rim+surface.normal()*.022,surface.normal(),deckTerminal?.006:.007);
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
        }
        // Each run shares its cross-sections at the bends. The small metal
        // inlays sit on the inside of the ceramic, clear of the glass gasket.
        cabin.canopyRail(sill, side, {0,0}, .112,.120,cabinFrame);
        cabin.canopyRail(ceiling, side, {0,0}, .106,.120,cabinFrame);
        cabin.canopyRail(sill, side, {-.059,.037}, .036,.029,cabinCeramic);
        cabin.canopyRail(ceiling, side, {-.055,-.035}, .034,.030,cabinCeramic);
        cabin.canopyRail(sill, side, {-.080,.037}, .011,.010,cabinCopper);
        cabin.canopyRail(ceiling, side, {-.075,-.035}, .010,.010,cabinTrim);
        cabin.canopyRail(sill, side, {.017,.071}, .033,.031,cabinRubber);
        cabin.canopyRail(ceiling, side, {.017,-.071}, .031,.031,cabinRubber);
        cabin.canopyRail(sill, side, {-.14,-.41}, .064,.075,cabinFrame);
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
                   point(.625,-.544,-.98),cabinComposite);
        cabin.face(point(.625,-.544,-.98),point(.665,-.544,.47),point(.74,-.70,.48),
                   point(.75,-.70,-.98),cabinLiner);
        cabin.beam(point(.625,-.542,-.99),point(.665,-.542,.48),.069,.065,cabinTrim);
        cabin.beam(point(.718,-.626,-.82),point(.714,-.626,.41),.033,.019,cabinRubber);
        cabin.beam(point(.696,-.603,-.24),point(.693,-.603,.26),.012,.013,cabinStatusLight,.48f);
        for (double z : {-.72,-.48,.09,.31}) {
            cabin.beam(point(.736,-.685,z-.068),point(.733,-.685,z+.068),.033,.019,cabinTrim);
            cabin.beam(point(.715,-.688,z-.039),point(.715,-.688,z+.039),.011,.012,cabinCopper);
        }
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
        cabin.panel({point(.727,.394,side<0 ? 1.038 : .786),{0,0,side*.252},{0,-.104,0}},.018,.018,
                    cabinCeramic,cabinTrim);
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
    // Machined windscreen flange and its captive pressure bolts. Kept on
    // the opaque frame, with physical separation from the glazing gasket.
    for(double side:{-1.,1.}){
        cabin.beam({side*.78,.492,-1.365},{side*.78,-.316,-1.365},.026,.029,cabinTrim);
        for(double y:{-.24,-.03,.18,.40})
            cabin.fastener({side*.78,y,-1.346},{0,0,1},.009);
        cabin.beam({side*.75,-.320,-1.38},{side*.43,-.320,-1.38},.018,.016,cabinCopper);
        cabin.beam({side*.73,-.318,-1.361},{side*.49,-.318,-1.361},.004,.004,cabinStatusLight,.27f);
    }
    for (double x : {-.66,-.44,.44,.66}) {
        cabin.beam({x,.531,-1.398},{x,.531,-1.388},.018,.018,cabinTrim);
        cabin.beam({x,-.355,-1.414},{x,-.355,-1.399},.018,.018,cabinTrim);
    }
    // Segmented pressure collar: carbon inserts sit forward of the beam's
    // front face. Gaps expose the structural alloy rather than coplanar decals.
    for(int section=0;section<5;++section){
        const double x=-.735+section*.298;
        const CabinSurface plate{{x,.583,-1.354},{.278,0,0},{0,-.079,0}};
        cabin.panel(plate,.010,.008,cabinComposite,cabinCopper);
        for(double u:{.10,.90})
            cabin.fastener(plate.point(u,.51)+glm::dvec3(0,0,.013),{0,0,1},.005);
        for(int slot=0;slot<5;++slot){
            const double a=x+.091+slot*.023;
            cabin.beam({a,.551,-1.340},{a+.009,.526,-1.340},.006,.003,cabinRubber);
        }
    }
    for(double side:{-1.,1.}){
        cabin.beam({side*.29,.493,-1.351},{side*.72,.493,-1.351},.010,.009,cabinRubber);
        cabin.beam({side*.31,.491,-1.341},{side*.70,.491,-1.341},.004,.004,cabinWarmLight,.49f);
        // Split cuffs and polished pressure tubes reinforce the front corners.
        for(double y:{-.247,.067,.382}){
            cabin.panel({{side<0?-.809:.751,y+.034,-1.323},{.058,0,0},{0,-.068,0}},
                        .009,.009,cabinComposite,cabinTrim);
            cabin.fastener({side*.78,y,-1.308},{0,0,1},.006);
        }
        cabin.beam({side*.807,-.25,-1.321},{side*.807,.40,-1.321},.008,.009,cabinCopper);
        for(int bay=0;bay<3;++bay){
            const double z=-.51+bay*.53;
            const CabinSurface roof{{side<0?-.48:.28,.670,z},{.20,0,0},{0,0,-.46}};
            cabin.panel(roof,.016,.018,cabinComposite,cabinTrim);
            for(int rib=0;rib<6;++rib){
                const double line=z-.075-rib*.053;
                cabin.beam({side*.307,.648,line},{side*.451,.648,line},.013,.008,cabinRubber);
            }
        }
    }
    // Sculpted pressure manifold and telescopic corner stays. These are
    // rounded, opaque fittings with separated sleeves, collars and mounts.
    // Their soft-shadow proxies use the same hull-local dimensions.
    cabin.tube({-.72,.607,-1.285},{.72,.607,-1.285},.018,cabinCopper);
    for(double x:{-.62,-.31,0.,.31,.62}){
        cabin.tube({x-.014,.607,-1.285},{x+.014,.607,-1.285},.025,cabinTrim);
        cabin.beam({x,.607,-1.327},{x,.607,-1.312},.067,.067,cabinComposite);
    }
    for(double side:{-1.,1.}){
        const double x=side*.812;
        cabin.tube({x,-.25,-1.285},{x,.40,-1.285},.0135,cabinTrim);
        cabin.tube({x,-.25,-1.285},{x,.035,-1.285},.018,cabinRubber);
        cabin.tube({side*.72,.607,-1.285},{x,.40,-1.285},.016,cabinCopper);
        for(double y:{-.22,.08,.38}){
            const CabinSurface mount{{x-.031,y+.042,-1.319},{.062,0,0},{0,-.084,0}};
            cabin.panel(mount,.010,.017,cabinComposite,cabinCopper);
            cabin.tube({x,y-.013,-1.285},{x,y+.013,-1.285},.026,cabinTrim);
            cabin.fastener({x,y,-1.255},{0,0,1},.0045);
        }
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
    for (double x : {-.25,.25})
        cabin.beam({x,.634,-1.22},{x,.634,.66},.031,.026,cabinCopper);

    // Central dashboard, recessed pedals, and the pilot's hand controls.
    cabin.face({-.94,-.344,-1.48},{.94,-.344,-1.48},{.68,-.566,-.98},{-.68,-.566,-.98},cabinComposite);
    cabin.face({-.67,-.565,-.98},{.67,-.565,-.98},{.55,-.72,-1.20},{-.55,-.72,-1.20},cabinLiner);
    cabin.face({-.92,-.36,-1.47},{.92,-.36,-1.47},{.8,-.82,-1.47},{-.8,-.82,-1.47},cabinLiner);
    cabin.beam({-.68,-.556,-.985},{.68,-.556,-.985},.062,.063,cabinTrim);
    cabin.beam({-.59,-.568,-.952},{.59,-.568,-.952},.026,.018,cabinCeramic);
    cabin.beam({-.32,-.588,-.943},{.32,-.588,-.943},.011,.009,cabinStatusLight,.53f);
    cabin.instrument(driveScreen);
    for (double side : {-1.,1.}) {
        const CabinSurface controls{{side<0?-.677:.457,-.346,-1.454},{.22,0,0},{0,-.128,.288}};
        cabin.panel(controls,.014,.012,cabinComposite,cabinTrim);
        cabin.dial(controls,.5,.66,.067,true);
        cabin.dial(controls,.5,.205,.032,false);
        for(double u:{.10,.90})for(double v:{.10,.90})
            cabin.fastener(controls.point(u,v)+controls.normal()*.015,controls.normal(),.0045);
        for (int i=0;i<4;++i){
            const auto a=controls.point(.11+i*.21,.41)+controls.normal()*.016;
            cabin.beam(a,a+controls.normal()*.004,.010,.007,
                       i==3?cabinWarmLight:cabinStatusLight,.32f);
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

    // A real opening through the rear pressure bulkhead connects the deck.
    constexpr double hatch=shipDeck::hatch, door=shipDeck::hatchHalf;
    for (double side : {-1.,1.}) {
        const double inner=side*door,outer=side*.90;
        cabin.face({inner,-.82,hatch},{outer,-.82,hatch},{outer,.69,hatch},{inner,.69,hatch},cabinLiner);
        cabin.beam({inner+side*.036,-.82,hatch-.045},{inner+side*.036,.66,hatch-.045},.068,.12,cabinFrame);
        cabin.beam({inner+side*.030,-.65,hatch-.112},{inner+side*.030,-.18,hatch-.112},.018,.025,cabinStatusLight,.58f);
    }
    cabin.beam({-.55,.647,hatch-.041},{.55,.647,hatch-.041},.11,.105,cabinFrame);
    cabin.beam({-.43,.603,hatch-.108},{.43,.603,hatch-.108},.014,.020,cabinWarmLight,.77f);
    // Pilot's seat and foot rails.
    cabin.beam({0,-.713,-.205},{0,-.713,.46},.576,.142,cabinFrame);
    cabin.beam({0,-.611,-.232},{0,-.611,.431},.514,.104,cabinUpholstery);
    cabin.beam({0,-.605,.424},{0,-.349,.55},.498,.125,cabinUpholstery);
    for (double side : {-1.,1.}) {
        cabin.beam({side*.271,-.567,-.16},{side*.271,-.541,.365},.063,.101,cabinLiner);
        cabin.beam({side*.217,-.558,-.182},{side*.217,-.558,.412},.018,.014,cabinTrim);
    }
    for (double z : {-.10,.02,.14,.26,.38})
        cabin.beam({-.205,-.558,z},{.205,-.558,z},.011,.010,cabinRubber);

    cabin.mesh.compact();
    return std::move(cabin.mesh);
}

Mesh xalariaCockpitHatch() {
    CabinBuilder door;
    const double z=shipDeck::hatch;
    constexpr double x=shipDeck::hatchHalf*.5;
    door.beam({x,-.102,z-.030},{x,-.102,z+.021},.47,1.42,cabinFrame);
    for (double face : {-.069,.059}) {
        door.beam({x,-.123,z+face},{x,-.123,z+face+.016},.385,1.21,cabinLiner);
        door.beam({.086,-.083,z+face},{.195,-.083,z+face},.036,.048,cabinCopper);
        for (double y : {-.568,.420})
            door.beam({.062,y,z+face},{.410,y,z+face},.016,.020,cabinTrim);
        door.beam({.11,.302,z+face},{.31,.302,z+face},.014,.018,cabinStatusLight,.66f);
    }
    door.mesh.compact();
    return std::move(door.mesh);
}

Mesh xalariaShipDeck() {
    using namespace shipDeck;
    CabinBuilder deck;
    const CabinFinish deckPlate{{.041f,.061f,.073f},CabinMaterial::Polymer};
    const CabinFinish tread{{.072f,.100f,.117f},CabinMaterial::Tread};
    const CabinFinish bulkhead{{.089f,.13f,.158f},CabinMaterial::Painted};
    const CabinFinish ceiling{{.10f,.14f,.16f},CabinMaterial::Painted};
    const CabinFinish bulkheadPanel{{.27f,.32f,.34f},CabinMaterial::Painted};
    const CabinFinish frame{{.13f,.16f,.17f},CabinMaterial::Brushed};
    const CabinFinish ivory{{.39f,.40f,.35f},CabinMaterial::Ceramic};
    const CabinFinish safety{{.61f,.37f,.11f},CabinMaterial::Painted};
    const CabinFinish routeLamp{{.23f,.69f,.72f},CabinMaterial::Display};
    const CabinFinish cloth{{.20f,.30f,.28f},CabinMaterial::Textile};
    const CabinFinish berthLamp{berthLight,CabinMaterial::Display};
    const CabinFinish reactorLamp{engineeringLight,CabinMaterial::Display};
    const CabinFinish loadingLamp{cargoLight,CabinMaterial::Display};
    const auto surface = [&](double left,double right,double start,double end,double y,const CabinFinish& finish) {
        deck.face({left,y,start},{right,y,start},{right,y,end},{left,y,end},finish);
    };
    const auto side = [&](double x,double from,double to,double floor,double roof,const CabinFinish& finish) {
        deck.face({x,floor,from},{x,floor,to},{x,roof,to},{x,roof,from},finish);
    };
    const auto wall = [&](double z,double left,double right,double floor,double roof,const CabinFinish& finish) {
        deck.face({left,floor,z},{right,floor,z},{right,roof,z},{left,roof,z},finish);
    };
    const auto box = [&](const DeckArea& footprint,double base,double top,const CabinFinish& finish) {
        deck.beam({(footprint.left+footprint.right)*.5,(base+top)*.5,footprint.front},
                  {(footprint.left+footprint.right)*.5,(base+top)*.5,footprint.back},
                  footprint.right-footprint.left,top-base,finish);
    };
    const auto insetWall = [&](double x,double from,double to,double low,double high,
                               const CabinFinish& finish) {
        deck.beam({x,(low+high)*.5,from},{x,(low+high)*.5,to},.020,high-low,finish);
    };
    const auto floorPlates = [&](double from,double to,double halfWidth,unsigned count) {
        const double spacing=(to-from)/count;
        for (unsigned i=0;i<count;++i) {
            const double z=from+i*spacing;
            deck.panel({{-halfWidth,deckFloor+.003,z+.011},{2*halfWidth,0,0},{0,0,spacing-.022}},
                       .017,.003,tread,frame);
            for (double x : {-halfWidth+.055,halfWidth-.055}) {
                surface(x-.023,x+.023,z+.043,z+.054,deckFloor+.007,frame);
                surface(x-.023,x+.023,z+spacing-.054,z+spacing-.043,deckFloor+.007,frame);
            }
        }
    };
    floorPlates(1.72,cargoFront,.43,6);

    // Ramp from the bridge's raised floor to the passage.
    deck.face({-hatchHalf,bridgeFloor,hatch},{hatchHalf,bridgeFloor,hatch},
              {passageHalf,deckFloor,1.71},{-passageHalf,deckFloor,1.71},deckPlate);
    surface(-passageHalf,passageHalf,1.71,cargoFront,deckFloor,deckPlate);
    surface(-passageHalf,passageHalf,hatch,cargoFront,passageRoof,ceiling);
    for (double sign : {-1.,1.}) {
        deck.face({sign*hatchHalf,bridgeFloor,hatch},{sign*passageHalf,deckFloor,1.71},
                  {sign*passageHalf,passageRoof,1.71},{sign*hatchHalf,.69,hatch},bulkhead);
        side(sign*passageHalf,1.71,quartersDoorFront,deckFloor,passageRoof,bulkhead);
        side(sign*passageHalf,quartersDoorBack,cargoFront,deckFloor,passageRoof,bulkhead);
        side(sign*passageHalf,quartersDoorFront,quartersDoorBack,.82,passageRoof,cabinFrame);
        const auto& roomLamp = sign<0 ? berthLamp : reactorLamp;
        const double jamb=sign*passageHalf;
        deck.beam({jamb,.847,quartersDoorFront},{jamb,.847,quartersDoorBack},.070,.056,frame);
        deck.beam({jamb-sign*.038,.826,quartersDoorFront+.08},
                  {jamb-sign*.038,.826,quartersDoorBack-.08},.016,.014,roomLamp,.72f);
        for (double z : {quartersDoorFront+.065,quartersDoorBack-.065}) {
            deck.beam({jamb-sign*.036,.46,z},{jamb-sign*.036,.76,z},.012,.016,roomLamp,.57f);
            deck.beam({jamb-sign*.038,-.90,z},{jamb-sign*.038,-.69,z},.013,.018,roomLamp,.54f);
        }
        for (double z : {1.79,quartersDoorFront,quartersDoorBack,3.49}) {
            deck.beam({sign*passageHalf,deckFloor,z},{sign*passageHalf,passageRoof,z},.069,.080,cabinFrame);
        }
        deck.beam({sign*.52,deckFloor+.028,1.72},{sign*.52,deckFloor+.028,cargoFront},.025,.017,routeLamp,.48f);
        deck.beam({sign*.49,passageRoof-.044,1.45},{sign*.49,passageRoof-.044,3.55},
                  .061,.035,cabinWarmLight,.78f);
        for (const auto interval : {glm::dvec2{1.73,1.98},glm::dvec2{3.28,3.69}}) {
            const double x=sign*(passageHalf-.015);
            insetWall(x,interval.x,interval.y,-.47,.68,bulkheadPanel);
            deck.panel({{x-sign*.013,.56,sign<0 ? interval.y-.027 : interval.x+.027},
                        {0,0,sign*(interval.y-interval.x-.054)},{0,-.48,0}},
                       .022,.015,frame,cabinTrim);
            insetWall(x-sign*.024,interval.x+.044,interval.y-.044,.48,.53,ivory);
            insetWall(x-sign*.013,interval.x+.021,interval.y-.021,-.42,-.12,cabinLiner);
            for (double y : {-.34,-.28,-.22})
                deck.beam({x-sign*.026,y,interval.x+.045},{x-sign*.026,y,interval.y-.045},
                          .016,.013,frame);
            deck.beam({x-sign*.025,-.040,interval.x+.027},{x-sign*.025,-.040,interval.y-.027},
                      .014,.014,routeLamp,.56f);
        }
        for (double z : {quartersDoorFront+.12,quartersDoorBack-.12})
            deck.beam({sign*(passageHalf-.014),.89,z},{sign*(passageHalf-.014),.89,z+.09},
                      .025,.067,safety);
        const auto left=std::min(sign*passageHalf,sign*quartersWall);
        const auto right=std::max(sign*passageHalf,sign*quartersWall);
        surface(left,right,quartersFront,quartersBack,deckFloor,deckPlate);
        surface(left,right,quartersFront,quartersBack,passageRoof,ceiling);
        const auto& roomWall = sign<0 ? ivory : bulkhead;
        wall(quartersFront,left,right,deckFloor,passageRoof,roomWall);
        wall(quartersBack,left,right,deckFloor,passageRoof,roomWall);
        side(sign*quartersWall,quartersFront,quartersBack,deckFloor,passageRoof,bulkhead);
        deck.beam({sign*1.55,passageRoof-.050,2.05},{sign*1.55,passageRoof-.050,3.20},.14,.055,frame);
        deck.beam({sign*1.55,passageRoof-.085,2.13},{sign*1.55,passageRoof-.085,3.12},.072,.023,
                  roomLamp,.81f);
        deck.beam({jamb,deckFloor+.008,quartersDoorFront+.10},
                  {jamb,deckFloor+.008,quartersDoorBack-.10},.085,.012,roomLamp,.42f);
    }
    deck.face({-.49,.69,hatch},{.49,.69,hatch},{passageHalf,passageRoof,1.71},
              {-passageHalf,passageRoof,1.71},ceiling);
    for (double z : {1.86,2.33,2.80,3.27}) {
        deck.beam({-.49,.947,z},{.49,.947,z},.050,.043,frame);
        for (double x : {-.36,0.,.36})
            deck.beam({x,.921,z-.07},{x,.921,z+.07},.069,.017,cabinLiner);
    }
    for (double x : {-.13,.13})
        deck.beam({x,.937,1.72},{x,.937,3.64},.068,.044,cabinLiner);
    deck.instrument(navigationDeckScreen,true);

    // The bunk, its storage and the reading light share one recessed bay.
    const CabinFinish blanket{{.38f,.13f,.075f},CabinMaterial::Textile};
    const CabinFinish headboard{{.12f,.18f,.18f},CabinMaterial::Textile};
    const CabinFinish stitch{{.50f,.31f,.18f},CabinMaterial::Textile};
    box(bunk,deckFloor,-.67,frame);
    box({-2.01,2.03,-1.57,3.19},-.68,-.50,cloth);
    box({-1.99,2.09,-1.62,2.34},-.52,-.41,ivory);
    box({-2.00,2.66,-1.58,3.15},-.51,-.454,blanket);
    deck.beam({-1.59,-.46,2.74},{-1.99,-.46,2.74},.020,.018,ivory);
    for (double x : {-1.97,-1.62})
        deck.beam({x,-.452,2.76},{x,-.452,3.09},.010,.008,stitch);
    insetWall(-2.068,2.05,3.18,-.43,.28,headboard);
    for (double z : {2.23,2.60,2.98}) {
        deck.panel({{-2.056,.19,z+.166},{0,0,-.332},{0,-.60,0}},.043,.026,cloth,headboard);
        insetWall(-1.570,z-.155,z+.155,-.99,-.74,bulkheadPanel);
        deck.beam({-1.554,-.80,z-.068},{-1.554,-.80,z+.068},.017,.023,frame);
    }
    deck.beam({-2.018,.32,2.05},{-2.018,.32,3.18},.15,.074,frame);
    deck.beam({-1.978,.278,2.12},{-1.978,.278,3.11},.034,.018,berthLamp,.92f);
    for (double z : {2.23,2.60,2.98}) {
        insetWall(-2.04,z-.165,z+.165,.425,.818,ivory);
        insetWall(-2.024,z-.144,z+.144,.461,.781,bulkheadPanel);
        deck.beam({-2.008,.531,z-.052},{-2.008,.531,z+.052},.018,.022,cabinCopper);
    }

    // Starboard systems alcove, with separate vertical cooling pipes.
    box(systems,deckFloor,.68,cabinFrame);
    insetWall(1.614,2.05,3.24,-.91,.67,frame);
    deck.panel({{1.598,.63,2.07},{0,0,1.13},{0,-1.52,0}},.044,.026,cabinLiner,cabinTrim);
    for (double z : {2.18,2.40,2.62,2.84,3.06}) {
        deck.beam({1.601,-.83,z},{1.601,-.30,z},.092,.100,cabinTrim);
        deck.beam({1.544,-.755,z},{1.544,-.367,z},.020,.037,reactorLamp,.82f);
        for (double y : {-.80,-.70,-.43,-.34})
            deck.beam({1.566,y,z-.054},{1.566,y,z+.054},.037,.022,frame);
        deck.beam({1.578,-.87,z},{1.578,-.87,z+.085},.041,.015,cabinCopper);
    }
    deck.beam({1.557,-.24,2.07},{1.557,-.24,3.14},.070,.073,cabinCopper);
    for (double z : {2.14,3.07})
        deck.beam({1.56,-.22,z},{1.56,-.87,z},.065,.062,cabinCopper);
    insetWall(1.567,3.06,3.23,-.16,.49,bulkheadPanel);
    for (double y : {-.07,.055,.18,.305})
        deck.beam({1.548,y,3.093},{1.548,y,3.151},.019,.021,reactorLamp,.62f);
    for (int i=0;i<14;++i) {
        const double z=2.12+i*.08;
        deck.beam({1.565,.622,z},{1.565,.644,z},.025,.032,cabinTrim);
    }
    deck.instrument(systemsDeckScreen,true);

    // The cargo bay has one clear aisle and two banks of tied-down crates.
    surface(-cargoHalf,cargoHalf,cargoFront,cargoBack,deckFloor,deckPlate);
    floorPlates(cargoFront+.12,cargoBack-.16,.48,8);
    surface(-cargoHalf,cargoHalf,cargoFront,cargoBack,cargoRoof,ceiling);
    wall(cargoFront,-cargoHalf,-cargoDoorHalf,deckFloor,cargoRoof,bulkhead);
    wall(cargoFront,cargoDoorHalf,cargoHalf,deckFloor,cargoRoof,bulkhead);
    wall(cargoFront,-cargoDoorHalf,cargoDoorHalf,.90,cargoRoof,cabinFrame);
    wall(cargoBack,-cargoHalf,cargoHalf,deckFloor,cargoRoof,bulkhead);
    for (double sign : {-1.,1.}) {
        const double hull=sign*cargoHalf;
        side(hull,cargoFront,observationFore,deckFloor,cargoRoof,bulkhead);
        if (sign<0.) {
            const double from=stationDeck::centre-stationDeck::doorHalf, to=stationDeck::centre+stationDeck::doorHalf;
            side(hull,observationAft,from,deckFloor,cargoRoof,bulkhead);
            side(hull,to,cargoBack,deckFloor,cargoRoof,bulkhead);
            side(hull,from,to,stationDeck::doorTop,cargoRoof,cabinFrame);
            for (double z : {from-.028,to+.028}) {
                deck.beam({hull+.015,deckFloor,z},{hull+.015,stationDeck::doorTop,z},.092,.073,cabinTrim);
                deck.beam({hull+.065,-.45,z},{hull+.065,.62,z},.018,.015,loadingLamp,.86f);
            }
        } else {
            side(hull,observationAft,cargoBack,deckFloor,cargoRoof,bulkhead);
        }
        side(hull,observationFore,observationAft,deckFloor,observationSill,bulkhead);
        side(hull,observationFore,observationAft,observationHead,cargoRoof,bulkhead);
        const auto rim=cargoWindow(hull,.056);
        const auto bevel=cargoWindow(hull-sign*.036,.012);
        const auto glass=cargoWindow(hull-sign*.008,0);
        for (size_t i=0;i<rim.size();++i) {
            const size_t next=(i+1)%rim.size();
            deck.face(rim[i],rim[next],bevel[next],bevel[i],frame);
            deck.face(bevel[i],bevel[next],glass[next],glass[i],cabinTrim);
        }
        for (double z : {observationFore-.033,observationAft+.033}) {
            deck.beam({hull-sign*.043,.45,z},{hull-sign*.043,.79,z},.026,.024,cabinRubber);
            deck.beam({hull-sign*.059,.54,z},{hull-sign*.059,.69,z},.008,.010,reactorLamp,.68f);
        }
        deck.beam({hull-sign*.032,.244,observationFore+.14},{hull-sign*.032,.244,observationAft-.14},
                  .023,.020,cabinCopper);
        deck.beam({sign*cargoDoorHalf,deckFloor,cargoFront},{sign*cargoDoorHalf,.91,cargoFront},
                  .071,.089,cabinTrim);
        deck.beam({sign*.61,-.80,cargoFront-.035},{sign*.61,.74,cargoFront-.035},
                  .022,.024,loadingLamp,.75f);
        deck.beam({sign*.54,deckFloor+.023,3.82},{sign*.54,deckFloor+.023,7.33},
                  .032,.020,routeLamp,.57f);
        for (double z : {3.95,4.97,6.13,7.26}) {
            if (sign<0.&&z>7.) continue;
            deck.beam({sign*1.52,deckFloor,z},{sign*1.52,1.04,z},.15,.144,cabinFrame);
            deck.beam({sign*1.52,1.04,z},{sign*1.15,cargoRoof,z},.13,.114,cabinFrame);
        }
        insetWall(sign*1.628,3.90,sign<0.?6.28:7.26,-.26,.045,ivory);
        for (const auto span : {glm::dvec2{4.03,4.88},glm::dvec2{5.06,6.05},glm::dvec2{6.22,7.17}}) {
            if (sign<0.&&span.y>7.) continue;
            deck.panel({{sign*1.633,.27,sign<0?span.y:span.x},{0,0,sign*(span.y-span.x)},{0,-.61,0}},
                       .032,.019,bulkheadPanel,frame);
        }
        for (double z : {4.45,6.66}) {
            if (sign<0.&&z>6.) continue;
            insetWall(sign*1.612,z-.35,z+.35,.33,.92,bulkheadPanel);
            insetWall(sign*1.591,z-.31,z+.31,.40,.66,frame);
            for (double y : {.45,.50,.55,.60})
                deck.beam({sign*1.576,y,z-.27},{sign*1.576,y,z+.27},.015,.022,cabinRubber);
            deck.beam({sign*1.585,.73,z-.255},{sign*1.585,.73,z+.255},.022,.034,ivory);
            deck.beam({sign*1.578,.83,z-.18},{sign*1.578,.83,z+.18},.022,.020,loadingLamp,.63f);
        }
        for (double z : {4.19,5.23,6.27}) {
            deck.beam({sign*.76,cargoRoof-.045,z},{sign*.76,cargoRoof-.045,z+.52},
                      .202,.069,frame);
            deck.beam({sign*.76,cargoRoof-.084,z+.04},{sign*.76,cargoRoof-.084,z+.48},
                      .103,.023,loadingLamp,.90f);
        }
        for (int i=0;i<8;++i) {
            const double x=sign*(.66+i*.099);
            deck.beam({x,deckFloor+.015,cargoFront+.16},{x+sign*.048,deckFloor+.015,cargoFront+.25},
                      .028,.010,i%2?safety:cabinRubber);
        }
    }
    deck.beam({-.52,.884,cargoFront-.043},{.52,.884,cargoFront-.043},.026,.022,loadingLamp,.76f);
    for (double x : {-.26,.26})
        deck.beam({x,1.145,3.84},{x,1.145,7.23},.088,.065,frame);
    deck.beam({0,-.072,7.393},{0,-.072,7.331},1.47,1.55,cabinFrame);
    deck.beam({0,-.083,7.32},{0,-.083,7.288},1.29,1.36,cabinLiner);
    for (double side : {-1.,1.}) {
        const double x=side*1.08;
        deck.beam({x,-.19,7.403},{x,-.19,7.379},.57,1.58,frame);
        deck.beam({x,-.19,7.378},{x,-.19,7.363},.46,1.40,bulkheadPanel);
        for (int row=0;row<7;++row) {
            const double y=-.69+row*.122;
            deck.beam({x-.151,y,7.349},{x+.151,y,7.349},.018,.034,cabinLiner);
        }
        deck.beam({x-.14,.34,7.350},{x+.14,.34,7.350},.022,.022,loadingLamp,.71f);
    }
    deck.instrument(cargoDeckScreen,true);
    deck.beam({-.57,.681,7.307},{.57,.681,7.307},.065,.145,cabinFrame);
    for (double x : {-.62,.62}) {
        deck.beam({x,-.65,7.275},{x,.48,7.275},.059,.053,cabinTrim);
        deck.beam({x,-.27,7.24},{x,.17,7.24},.022,.023,routeLamp,.48f);
    }
    for (size_t i=0;i<cargoCrates.size();++i) {
        const auto& crate=cargoCrates[i];
        const double top=(i%3==1)?-.105:(i==2)?.15:-.31;
        const CabinFinish shell{i%2 ? glm::vec3{.32f,.38f,.37f} : glm::vec3{.55f,.35f,.16f},
                                CabinMaterial::Painted};
        box(crate,deckFloor+.055,top,shell);
        box({crate.left+.023,crate.front+.024,crate.right-.023,crate.back-.024},top-.028,top+.012,frame);
        for (double x : {crate.left+.10,crate.right-.10}) {
            deck.beam({x,top+.022,crate.front+.02},{x,top+.022,crate.back-.02},.056,.047,cabinRubber);
            for (double z : {crate.front-.018,crate.back+.018})
                deck.beam({x,deckFloor+.092,z},{x,top+.02,z},.056,.047,cabinRubber);
        }
        const double sign=crate.left<0?1.:-1.;
        const double inside=(sign>0?crate.right:crate.left)+sign*.012;
        insetWall(inside,crate.front+.08,crate.back-.08,top-.32,top-.085,frame);
        insetWall(inside+sign*.015,crate.front+.23,crate.front+.47,top-.275,top-.145,ivory);
        for (int stripe=0;stripe<5;++stripe) {
            const double z=crate.front+.25+stripe*.023;
            deck.beam({inside+sign*.027,top-.254,z},{inside+sign*.027,top-.169,z},.008,.011,cabinRubber);
        }
        deck.beam({inside,top-.13,crate.front+.15},{inside,top-.13,crate.back-.15},
                  .027,.047,cabinCopper);
        for (double z : {crate.front+.21,crate.back-.21})
            deck.beam({inside,top-.21,z},{inside,top-.31,z},.041,.035,cabinFrame);
    }
    deck.beam({-.58,.57,7.30},{.58,.57,7.30},.10,.094,cabinFrame);
    deck.beam({-.44,.57,7.25},{.44,.57,7.25},.024,.030,routeLamp,.76f);

    deck.mesh.compact();
    return std::move(deck.mesh);
}

namespace {
constexpr double annexStripInset=.31, annexStripDrop=.228, annexStripEnd=.23;

struct StationTaskLamp {
    glm::dvec3 position{};
    double halfLength=-1.;
};

StationTaskLamp stationTaskLamp(const StationAnnex& annex) {
    if (annex.panoramic() && annex.room.fixtures.size()>2) {
        const auto& table=annex.room.fixtures[2];
        return {{(table.left+table.right)*.5,-.224,(table.front+table.back)*.5},0.};
    }
    if (annex.room.kind==StationModuleKind::Analysis && !annex.room.fixtures.empty()) {
        const auto& bench=annex.room.fixtures.front();
        return {{(bench.left+bench.right)*.5+.12,.85,(bench.front+bench.back)*.5-.045},.54};
    }
    return {};
}
}

StationAnnexLighting xalariaStationAnnexLighting(const StationLayout& layout) {
    StationAnnexLighting lighting{};
    const auto bounds=[](const DeckArea& area) {
        return glm::vec4(area.left,area.front,area.right,area.back);
    };
    constexpr size_t vectorsPerAnnex=6;
    for (size_t i=0;i<std::min(layout.annexes.size(),lighting.size()/vectorsPerAnnex);++i) {
        const auto& annex=layout.annexes[i];
        const auto& room=annex.room;
        const auto lamp=stationTaskLamp(annex);
        const auto start=i*vectorsPerAnnex;
        lighting[start]=bounds(room.floor);
        lighting[start+1]={float(room.ceiling-annexStripDrop),float(annexStripInset),
                           float(annexStripEnd),annex.panoramic()?1.f:0.f};
        lighting[start+2]=glm::vec4(lamp.position,lamp.halfLength);
        for (size_t j=0;j<std::min(size_t(3),room.fixtures.size());++j) {
            auto footprint=room.fixtures[j];
            if (annex.panoramic() && j==2) {
                const double x=lamp.position.x,z=lamp.position.z;
                footprint={x-.075,z-.075,x+.075,z+.075};
            }
            lighting[start+3+j]=bounds(footprint);
        }
    }
    return lighting;
}

StationWindowLighting xalariaStationWindows(const StationLayout& layout) {
    using namespace stationDeck;
    StationWindowLighting windows{};
    const auto bridge=std::find_if(layout.modules.rbegin(),layout.modules.rend(),[](const auto& room) {
        return room.kind==StationModuleKind::Bridge;
    });
    if (bridge!=layout.modules.rend()) {
        windows[0]={float(bridge->floor.left),float(bridgeDoor),float(centre),float(bridge->floor.back-centre)};
        windows[2]={float(glassSill),float(glassShoulder),float(bridge->ceiling),float(bridgeRibWidth*.5)};
    }
    const auto panorama=std::find_if(layout.annexes.begin(),layout.annexes.end(),[](const auto& annex) {
        return annex.panoramic();
    });
    if (panorama!=layout.annexes.end())
        windows[1]={float(panorama->room.floor.left+panoramaInset),float(panorama->room.floor.right-panoramaInset),
                    float(panorama->outsideWall()),float(panorama->insideWall())};
    windows[3]={float(panoramaSill),float(panoramaHead),float(ribPitch),float(floor)};
    return windows;
}

Mesh xalariaDockingDeck(const StationLayout& layout,XalariaLook look) {
    using namespace stationDeck;
    struct Finishes {
        glm::vec3 liner, enamel, ceramic, alloy, fabric, marker, lamp;
    };
    constexpr std::array<Finishes,4> regionalFinishes{{
        {{.108f,.046f,.032f},{.30f,.083f,.066f},{.57f,.35f,.22f},{.48f,.29f,.15f},
         {.39f,.082f,.069f},{.95f,.40f,.14f},{1.f,.78f,.49f}},
        {{.041f,.093f,.066f},{.095f,.265f,.163f},{.35f,.48f,.275f},{.39f,.36f,.18f},
         {.21f,.29f,.115f},{.64f,.86f,.33f},{.94f,.91f,.62f}},
        {{.027f,.071f,.128f},{.057f,.20f,.285f},{.29f,.47f,.51f},{.24f,.38f,.46f},
         {.40f,.205f,.164f},{.21f,.81f,.89f},{.60f,.91f,1.f}},
        {{.095f,.037f,.082f},{.28f,.078f,.184f},{.56f,.31f,.37f},{.48f,.32f,.22f},
         {.43f,.117f,.272f},{1.f,.54f,.73f},{1.f,.88f,.46f}}
    }};
    const auto& colours=regionalFinishes.at(size_t(look));
    const auto accent=glm::mix(colours.marker,stationAccent(layout.key.role),.19f);
    const CabinFinish tread{colours.liner*.82f,CabinMaterial::Tread};
    const CabinFinish floorPanel{glm::mix(colours.liner,colours.enamel,.48f)*1.35f,CabinMaterial::Tread};
    const CabinFinish frame{colours.alloy,CabinMaterial::Brushed};
    const CabinFinish bulkhead{colours.enamel,CabinMaterial::EnamelPanel};
    const CabinFinish liner{colours.liner,CabinMaterial::EnamelPanel};
    const CabinFinish wallPlate{colours.ceramic,CabinMaterial::Ceramic};
    const CabinFinish worktop{glm::mix(colours.ceramic,colours.alloy,.34f),CabinMaterial::MineralComposite};
    const CabinFinish painted{glm::mix(colours.enamel,accent,.39f),CabinMaterial::Painted};
    const CabinFinish upholstery{colours.fabric,CabinMaterial::Textile};
    const CabinFinish acoustics{glm::mix(colours.liner,colours.fabric,.49f)*1.12f,CabinMaterial::AcousticPanel};
    const CabinFinish stitching{colours.fabric*.61f,CabinMaterial::Textile};
    const CabinFinish fastener{colours.alloy*.62f,CabinMaterial::Brushed};
    const CabinFinish workLight{glm::mix(colours.lamp,glm::vec3(1.f,.97f,.89f),.46f),CabinMaterial::Display};
    const CabinFinish warmLight{colours.lamp,CabinMaterial::Display};
    const CabinFinish readingLight{{1.f,.77f,.43f},CabinMaterial::Display};
    const CabinFinish inspectionLight{{.76f,.90f,1.f},CabinMaterial::Display};
    const CabinFinish railLight{accent,CabinMaterial::Display};
    const CabinFinish bridgeFrame{glm::mix(glm::vec3(.089f,.119f,.139f),colours.alloy,.16f),CabinMaterial::Painted};
    const CabinFinish bridgeEdge{glm::mix(glm::vec3(.32f,.36f,.37f),colours.alloy,.23f),CabinMaterial::Brushed};
    const CabinFinish bridgeLiner{glm::mix(glm::vec3(.040f,.061f,.072f),colours.liner,.27f),CabinMaterial::EnamelPanel};
    const CabinFinish bridgeWalk{glm::mix(glm::vec3(.127f,.145f,.15f),colours.liner,.23f),CabinMaterial::MineralComposite};
    const CabinFinish bridgeLight{glm::mix(glm::vec3(1.f,.88f,.72f),colours.lamp,.23f),CabinMaterial::Display};
    const CabinFinish safety{{1.f,.67f,.23f},CabinMaterial::Display};
    const CabinFinish plants{{.19f,.45f,.25f},CabinMaterial::Polymer};
    const CabinFinish leaves{{.32f,.58f,.21f},CabinMaterial::Polymer};
    const Random detail{layout.key.seed};
    CabinBuilder deck;
    const auto endWall=[&](double x,double from,double to,double low,double high) {
        if (to>from && high>low)
            deck.face({x,low,from},{x,high,from},{x,high,to},{x,low,to},liner);
    };
    const auto sideWall=[&](double from,double to,double z,double low,double high,const CabinFinish& material) {
        if (to>from && high>low)
            deck.face({from,low,z},{to,low,z},{to,high,z},{from,high,z},material);
    };
    const auto bridgeRib=[&](const StationModule& module,double x,double width) {
        const auto edges=bridgeGlassProfile(module,.002);
        const auto inside=bridgeGlassProfile(module,-.036);
        const double half=width*.5, inset=.009;
        for (size_t n=1;n<inside.size();++n) {
            const auto a=inside[n-1],b=inside[n];
            deck.face({x-half+inset,a},{x+half-inset,a},{x+half-inset,b},{x-half+inset,b},bridgeFrame);
            for (double side : {-1.,1.}) {
                const double edge=x+side*half, flat=edge-side*inset;
                deck.face({edge,edges[n-1]},{flat,a},{flat,b},{edge,edges[n]},bridgeEdge);
            }
        }
        for (double side : {-1.,1.}) {
            const double z=centre+side*(module.floor.back-centre-.026);
            deck.beam({x,floor+.033,z},{x,glassSill,z},width+.018,.048,bridgeFrame);
        }
    };
    const auto annexEntrance=[&](const StationAnnex& annex,double roof) {
        const double z=annex.insideWall();
        const double left=annex.doorwayLeft,right=annex.doorwayRight;
        sideWall(left,right,z,annexDoorTop,roof,liner);
        for (double x : {left,right}) {
            deck.beam({x,floor,z},{x,annexDoorTop,z},.117,.10,frame);
            for (double side : {-1.,1.})
                deck.beam({x,-.62,z+side*.069},{x,.68,z+side*.069},.018,.023,railLight,.78f);
        }
        deck.beam({left,annexDoorTop,z},{right,annexDoorTop,z},.112,.092,frame);
        for (double side : {-1.,1.}) {
            const double mark=z+side*.18;
            deck.beam({left+.08,floor+.015,mark},{right-.08,floor+.015,mark},.024,.016,railLight,.73f);
        }
    };
    const auto portal=[&](double x,double half,double height) {
        for (double z : {centre-half,centre+half}) {
            deck.beam({x,floor,z},{x,height,z},.095,.105,frame);
            deck.beam({x+.061,-.74,z},{x+.061,.60,z},.016,.022,railLight,.87f);
        }
        deck.beam({x,height,centre-half},{x,height,centre+half},.086,.094,frame);
        deck.beam({x,height+.077,centre-half},{x,height+.077,centre+half},.104,.077,liner);
        for (double z : {centre-half+.105,centre+half-.105}) {
            deck.beam({x+.062,height+.083,z-.037},{x+.062,height+.083,z+.037},.018,.023,railLight,.73f);
            deck.beam({x-.062,height+.083,z-.037},{x-.062,height+.083,z+.037},.018,.023,railLight,.73f);
        }
    };
    const auto box=[&](const DeckArea& a,double base,double top,const CabinFinish& finish) {
        const double z=(a.front+a.back)*.5;
        deck.beam({a.left,(base+top)*.5,z},{a.right,(base+top)*.5,z},a.back-a.front,top-base,finish);
    };
    const auto planter=[&](double x,double y,double z) {
        deck.beam({x,y,z},{x,y+.075,z},.232,.236,frame);
        deck.beam({x,y+.07,z},{x,y+.23,z},.278,.282,wallPlate);
        deck.beam({x,y+.217,z},{x,y+.249,z},.289,.290,painted);
        deck.beam({x,y+.245,z},{x,y+.251,z},.232,.233,liner);
        for (unsigned branch=0;branch<9;++branch) {
            const double angle=branch*2.39996323;
            const glm::dvec3 root(x,y+.244,z);
            const glm::dvec3 along(std::cos(angle),0,std::sin(angle));
            const glm::dvec3 across(along.z,0,-along.x);
            const double height=.18+detail.unit(branch+125)*.20;
            const auto tip=root+along*(.08+detail.unit(branch+146)*.15)+glm::dvec3(0,height,0);
            const auto elbow=glm::mix(root,tip,.43)+glm::dvec3(0,.039,0);
            deck.beam(root+along*.02,elbow,.011,.011,plants);
            const auto ridge=glm::mix(elbow,tip,.56)+glm::dvec3(0,.025,0);
            const auto leaf=across*(.028+detail.unit(branch+167)*.03);
            deck.face(elbow,ridge+leaf,tip,ridge,leaves);
            deck.face(elbow,ridge,tip,ridge-leaf,plants);
        }
    };
    const auto outfit=[&](const StationModule& room) {
        for (const auto& item : room.fixtures) {
            if (item==layout.freightPad) {
                box(item,floor,-.91,liner);
                for (double z : {item.front+.027,item.back-.027}) {
                    deck.beam({item.left+.07,-.903,z},{item.right-.07,-.903,z},.040,.027,frame);
                    for (double x : {item.left+.18,item.right-.20})
                        deck.beam({x-.11,-.884,z},{x+.11,-.884,z},.035,.015,safety,.85f);
                }
                for (double x : {item.left+.035,item.right-.035})
                    deck.beam({x,-.902,item.front+.063},{x,-.902,item.back-.063},.044,.037,frame);
                continue;
            }
            const double inner=item.front>centre?item.front:item.back;
            const double middle=(item.front+item.back)*.5;
            const double side=middle>centre?1.:-1.;
            const double length=item.right-item.left;
            switch (room.kind) {
            case StationModuleKind::Habitat:
                box(item,floor,-.55,bulkhead);
                deck.beam({item.left,-.50,middle},{item.right,-.50,middle},.48,.095,liner);
                deck.beam({item.left+.05,-.515,inner+side*.033},{item.right-.05,-.515,inner+side*.033},.045,.053,wallPlate);
                for (unsigned i=0;i<unsigned(length/.29);++i) {
                    const double x=item.left+.17+i*.29;
                    const double rise=.52+detail.unit(60+i)*.30;
                    deck.beam({x,-.53,middle},{x+.045,rise-.46,middle-side*.035},.026,.035,plants);
                    for (double tilt : {-.13,.13})
                        deck.face({x,-.28,middle},{x+tilt,rise-.62,middle+side*.15},
                                  {x+tilt*.68,rise-.44,middle+side*.14},{x+.023,-.22,middle},
                                  i%2?plants:leaves);
                }
                deck.beam({item.left+.15,1.18,middle},{item.right-.15,1.18,middle},.16,.065,frame);
                deck.beam({item.left+.24,1.137,middle},{item.right-.24,1.137,middle},.061,.031,warmLight,.93f);
                break;
            case StationModuleKind::Concourse:
                for (double x : {item.left+.13,item.right-.13})
                    deck.beam({x,floor+.02,middle},{x,-.68,middle},.25,.066,frame);
                deck.beam({item.left,-.665,middle},{item.right,-.665,middle},.49,.115,upholstery);
                deck.beam({item.left,-.38,middle+side*.19},{item.right,-.38,middle+side*.19},.15,.38,painted);
                for (double x=(item.left+item.right)*.5-.21;x<(item.left+item.right)*.5+.24;x+=.42)
                    deck.beam({x,-.52,inner+.03*side},{x,-.52,middle+side*.17},.035,.035,frame);
                break;
            case StationModuleKind::Laboratory:
                box(item,floor,-.42,liner);
                box(item,-.42,-.35,worktop);
                for (double x=item.left+.29;x<item.right-.26;x+=.68) {
                    const double face=inner+side*.11;
                    deck.beam({x,-.37,middle},{x,.20,middle},.31,.31,frame);
                    const CabinSurface console{{x+side*.153,.21,face},{-side*.306,0,0},{0,-.257,0}};
                    deck.panel(console,.024,.010,cabinScreen,frame);
                    for (unsigned row=0;row<4;++row)
                        deck.beam({x-.105,.165-row*.051,face-side*.017},
                                  {x-.058+.025*double((row*3+unsigned(std::abs(x)))%5),.165-row*.051,face-side*.017},
                                  .008,.009,railLight,.65f);
                    deck.beam({x-.14,-.292,middle},{x+.14,-.292,middle},.18,.092,painted);
                    deck.beam({x-.17,-.69,inner+side*.03},{x+.17,-.69,inner+side*.03},.028,.040,frame);
                }
                break;
            default: {
                const double top=room.kind==StationModuleKind::Freight?-.18:.44;
                const unsigned sections=std::max(2u,unsigned(length/.58));
                for (unsigned n=0;n<sections;++n) {
                    const double a=std::lerp(item.left,item.right,double(n)/sections);
                    const double b=std::lerp(item.left,item.right,double(n+1)/sections);
                    box({a+.017,item.front,b-.017,item.back},floor,top,n%2?frame:bulkhead);
                    deck.beam({a+.048,top-.065,middle},{b-.048,top-.065,middle},item.back-item.front-.085,.105,painted);
                    const double z=inner+side*.011;
                    for (double x : {a+.094,b-.094})
                        deck.beam({x,floor+.13,z},{x,top-.11,z},.035,.042,cabinRubber);
                    deck.beam({a+.124,top-.26,z},{b-.124,top-.26,z},.030,.040,liner);
                    deck.beam({a+.13,top-.151,z},{a+.163,top-.151,z},.019,.019,railLight,.69f);
                }
                break;
            }
            }
        }
    };
    for (size_t i=0;i<layout.modules.size();++i) {
        const auto& room=layout.modules[i];
        const auto [end,low,start,high]=room.floor;
        const bool bridge=room.kind==StationModuleKind::Bridge;
        const bool airlock=room.kind==StationModuleKind::Airlock;
        const double roof=room.ceiling;
        deck.face({end,floor,low},{end,floor,high},{start,floor,high},{start,floor,low},tread);
        if (!bridge)
            deck.face({start,roof,low},{start,roof,high},{end,roof,high},{end,roof,low},liner);
        const double wallTop=bridge?glassSill:roof;
        const std::array sideRooms{layout.annexAt(i,-1),layout.annexAt(i,1)};
        for (size_t wall=0;wall<sideRooms.size();++wall) {
            const double z=wall==0?low:high;
            if (const auto* annex=sideRooms[wall]) {
                sideWall(end,annex->doorwayLeft,z,floor,wallTop,bulkhead);
                sideWall(annex->doorwayRight,start,z,floor,wallTop,bulkhead);
                annexEntrance(*annex,roof);
            } else {
                sideWall(end,start,z,floor,wallTop,bridge?bridgeLiner:bulkhead);
            }
        }
        if (bridge) {
            bridgeRib(room,start,layout.modules[i-1].kind==StationModuleKind::Airlock?.085:bridgeRibWidth);
            for (double side : {-1.,1.}) {
                const double z=centre+side*(high-centre-.023);
                deck.beam({end,glassSill-.022,z},{start,glassSill-.022,z},.087,.043,bridgeFrame);
                deck.beam({end,glassSill,z},{start,glassSill,z},.050,.015,bridgeEdge);
                const double lightZ=centre+side*(high-centre-.065);
                deck.beam({end+.052,glassSill-.071,lightZ},{start-.052,glassSill-.071,lightZ},.012,.013,bridgeLight,.64f);
            }
        }
        if (i>0) {
            const auto& previous=layout.modules[i-1];
            const double opening=std::min(high,previous.floor.back)-centre;
            const double minimum=std::min(low,previous.floor.front), maximum=std::max(high,previous.floor.back);
            const double header=previous.kind==StationModuleKind::Airlock?doorTop:std::min(roof,previous.ceiling);
            const double top=std::max(roof,previous.ceiling);
            endWall(start,minimum,centre-opening,floor,top);
            endWall(start,centre+opening,maximum,floor,top);
            if (previous.kind==StationModuleKind::Bridge && !bridge) {
                const auto profile=bridgeGlassProfile(previous);
                for (size_t n=2;n+1<profile.size();++n) {
                    const auto a=profile[n-1],b=profile[n];
                    deck.face({start,a},{start,top,a.y},{start,top,b.y},{start,b},bridgeLiner);
                }
                bridgeRib(previous,start,.093);
            } else {
                endWall(start,centre-opening,centre+opening,header,top);
                if (previous.kind!=room.kind) portal(start,opening,header-.09);
            }
        }
        if (airlock) {
            deck.beam({end+.37,1.13,centre},{start-.38,1.13,centre},.22,.095,frame);
            deck.beam({end+.45,1.077,centre},{start-.46,1.077,centre},.086,.025,warmLight,.83f);
            for (double side : {-1.,1.}) {
                const double z=centre+side*(doorHalf-.020);
                const CabinSurface plate{{side<0?end+.33:start-.33,.47,z},
                                         {-side*(start-end-.66),0,0},{0,-.80,0}};
                deck.panel(plate,.046,.013,liner,frame);
                deck.beam({end+.40,-.48,z},{start-.40,-.48,z},.035,.041,frame);
                deck.beam({end+.48,-.47,z-side*.021},{start-.48,-.47,z-side*.021},.015,.017,safety,.57f);
            }
            continue;
        }
        for (double x=start-ribPitch*.5;x>end;x-=ribPitch) {
            const double span=std::min({ribPitch*.5,x-end,start-x});
            const double walkHalf=bridge?.48:.62;
            deck.panel({{x-span+.040,floor+.006,centre-walkHalf},{2*span-.080,0,0},{0,0,2*walkHalf}},
                       .022,.003,bridge?bridgeWalk:floorPanel,bridge?bridgeLiner:liner);
            if (!bridge) {
                deck.beam({x,roof-.10,low+.095},{x,roof-.10,high-.095},.121,.145,liner);
                deck.beam({x,roof-.167,low+.26},{x,roof-.167,high-.26},.056,.052,frame);
                for (double side : {-1.,1.}) {
                    const double z=centre+side*(high-centre-.24);
                    deck.beam({x+span,roof-.254,z},{x-span,roof-.254,z},.052,.057,frame);
                    deck.beam({x+span,roof-.275,z-side*.084},{x-span,roof-.275,z-side*.084},.029,.037,painted);
                }
            }
            for (double side : {-1.,1.}) {
                const auto wall=centre+side*(high-centre-.047);
                const auto lane=centre+side*(bridge?.56:.69);
                const double panelStart=x+side*(span-.074), panelWidth=-side*(2*span-.148);
                const auto* annex=sideRooms[side>0?1:0];
                const bool atDoor=annex&&x+span>annex->doorwayLeft-.10&&x-span<annex->doorwayRight+.10;
                if (!bridge) {
                    if (!atDoor) {
                        deck.beam({x+span-.028,floor,wall},{x+span-.028,roof-.12,wall},.122,.092,frame);
                        const bool habitat=room.kind==StationModuleKind::Habitat||room.kind==StationModuleKind::Concourse;
                        const bool service=room.kind==StationModuleKind::Workshop||room.kind==StationModuleKind::Vault;
                        const CabinSurface panel{{panelStart,roof-.37,wall},{panelWidth,0,0},{0,-(roof-.55),0}};
                        deck.panel(panel,.045,.017,service?bulkhead:habitat?acoustics:wallPlate,frame);
                        const CabinSurface skirting{{panelStart,-.31,wall},{panelWidth,0,0},{0,-.68,0}};
                        deck.panel(skirting,.031,.014,habitat?painted:liner,frame);
                        deck.beam({x-span+.075,-.211,wall},{x+span-.074,-.211,wall},.033,.046,frame);
                        deck.beam({x-span+.098,roof-.408,wall-side*.015},{x+span-.098,roof-.408,wall-side*.015},
                                  .024,.045,painted);
                        for (double slot : {-.52,-.36,.36,.52})
                            deck.beam({x+slot,roof-.34,wall-side*.019},{x+slot,roof-.225,wall-side*.019},
                                      .018,.026,liner);
                    }
                } else {
                    const CabinSurface plate{{panelStart,glassSill-.078,wall+side*.028},
                                             {panelWidth,0,0},{0,-.26,0}};
                    deck.panel(plate,.026,.009,bridgeLiner,bridgeFrame);
                    deck.beam({x-span+.081,glassSill-.066,wall},{x+span-.081,glassSill-.066,wall},
                              .019,.018,bridgeFrame);
                }
                deck.beam({x+span-.09,floor+.018,lane},{x-span+.09,floor+.018,lane},.035,.021,liner);
                deck.beam({x+span-.16,floor+.030,lane},{x-span+.16,floor+.030,lane},
                          bridge?.010:.016,.011,bridge?bridgeLight:railLight,bridge?.42f:.80f);
                if (!bridge) {
                    const auto lamp=centre+side*std::min(1.20,high-centre-.37);
                    const auto& light=room.kind==StationModuleKind::Freight||room.kind==StationModuleKind::Workshop
                                      ?warmLight:workLight;
                    deck.beam({x,roof-.209,lamp-.34},{x,roof-.209,lamp+.34},.119,.036,frame);
                    deck.beam({x,roof-.234,lamp-.26},{x,roof-.234,lamp+.26},.072,.022,light,.91f);
                }
            }
        }
        outfit(room);
    }
    for (const auto& annex : layout.annexes) {
        const auto& room=annex.room;
        const auto [left,front,right,back]=room.floor;
        const double z=annex.outsideWall(), attach=annex.insideWall(), side=annex.side;
        const double roof=room.ceiling, width=right-left;
        const bool observation=annex.panoramic();
        const auto& lamp=observation?warmLight:workLight;
        const auto taskLamp=stationTaskLamp(annex);
        deck.face({left,floor,front},{left,floor,back},{right,floor,back},{right,floor,front},tread);
        deck.face({left,roof,front},{right,roof,front},{right,roof,back},{left,roof,back},liner);
        endWall(left,front,back,floor,roof);
        endWall(right,front,back,floor,roof);
        sideWall(left,right,attach,layout.modules[annex.parent].ceiling,roof,liner);
        for (double end : {left,right}) {
            const double towards=end==left?1.:-1.;
            const double start=towards>0.?back-.19:front+.19;
            const double span=(back-front-.38)*-towards;
            const CabinSurface dado{{end+towards*.022,.18,start},{0,0,span},{0,-1.02,0}};
            deck.panel(dado,.052,.029,bulkhead,frame);
            const CabinSurface upper{{end+towards*.022,roof-.40,start},{0,0,span},{0,-(roof-.76),0}};
            deck.panel(upper,.052,.024,observation?acoustics:wallPlate,frame);
            deck.beam({end+towards*.056,.25,front+.23},{end+towards*.056,.25,back-.23},.049,.069,frame);
            if (observation) {
                deck.beam({end+towards*.09,.285,front+.32},{end+towards*.09,.285,back-.29},
                          .012,.013,warmLight,.48f);
                for (double z=front+.51;z<back-.43;z+=.31)
                    deck.beam({end+towards*.049,.41,z},{end+towards*.049,roof-.48,z},
                              .012,.008,stitching);
            }
        }
        if (observation) {
            const CabinFinish carpet{glm::mix(colours.liner,colours.fabric,.54f)*.81f,CabinMaterial::Carpet};
            const double carpetLeft=left+.66,carpetRight=right-.66;
            const double carpetFront=front+.45,carpetBack=back-.45;
            const CabinSurface carpetFace{{carpetLeft,floor+.009,carpetFront},
                                         {carpetRight-carpetLeft,0,0},{0,0,carpetBack-carpetFront}};
            deck.panel(carpetFace,.065,.008,carpet,stitching);
            for (double edge : {carpetLeft+.13,carpetRight-.13}) {
                deck.beam({edge,floor+.020,carpetFront+.12},{edge,floor+.020,carpetBack-.12},
                          .021,.008,upholstery);
                deck.beam({edge,floor+.023,carpetFront+.14},{edge,floor+.023,carpetBack-.14},
                          .008,.004,frame);
            }
            sideWall(left,right,z,floor,panoramaSill,bulkhead);
            sideWall(left,right,z,panoramaHead,roof,bulkhead);
            sideWall(left,left+panoramaInset,z,panoramaSill,panoramaHead,bulkhead);
            sideWall(right-panoramaInset,right,z,panoramaSill,panoramaHead,bulkhead);
            for (double y : {panoramaSill,panoramaHead}) {
                deck.beam({left+.04,y,z-side*.028},{right-.04,y,z-side*.028},.105,.125,frame);
                deck.beam({left+.18,y,z-side*.092},{right-.18,y,z-side*.092},.026,.021,warmLight,.58f);
            }
            for (double p : {0.,1./3,2./3,1.}) {
                const double x=std::lerp(left+panoramaInset,right-panoramaInset,p);
                deck.beam({x,panoramaSill,z-side*.041},{x,panoramaHead,z-side*.041},.074,.085,frame);
                for (const double y : {panoramaSill+.16,panoramaHead-.16})
                    deck.beam({x,y,z-side*.095},{x,y,z-side*.111},.033,.033,fastener);
            }
        } else {
            sideWall(left,right,z,floor,roof,bulkhead);
            const CabinSurface panel{{side>0?right-.21:left+.21,1.39,z-side*.032},
                                     {-side*(width-.42),0,0},{0,-1.31,0}};
            deck.panel(panel,.064,.046,painted,frame);
            const double mount=z-side*.095;
            deck.beam({left+.29,.24,mount},{right-.29,.24,mount},.056,.069,painted);
            if (room.kind==StationModuleKind::Analysis) {
                deck.instrument(stationSurveyScreen(annex),true);
                for (double x : {left+.66,right-.66}) {
                    const double rack=z-side*.071;
                    deck.beam({x,.69,rack},{x,1.17,rack},.43,.064,liner);
                    for (unsigned sample=0;sample<3;++sample) {
                        const double at=x+(double(sample)-1.)*.127;
                        const CabinFinish cartridge{glm::mix(wallPlate.colour,accent,.17f+sample*.20f),
                                                    CabinMaterial::Ceramic};
                        deck.beam({at,.73,rack},{at,1.11,rack},.089,.084,cartridge);
                        deck.beam({at,.76,rack},{at,.80,rack},.094,.094,frame);
                        deck.beam({at,1.05,rack},{at,1.12,rack},.097,.097,frame);
                    }
                }
            } else if (room.kind==StationModuleKind::Engineering) {
                for (double y : {.53,.76,1.00,1.21}) {
                    const double pipe=z-side*.068;
                    deck.beam({left+.37,y,pipe},{right-.37,y,pipe},.073,.083,cabinCopper);
                    for (double x : {left+.53,(left+right)*.5,right-.53})
                        deck.beam({x-.06,y,pipe},{x+.06,y,pipe},.102,.111,frame);
                }
            } else {
                for (double x=left+.67;x<right-.40;x+=.69) {
                    const auto face=z-side*.064;
                    deck.beam({x,.44,face},{x,1.18,face},.56,.070,liner);
                    for (double y : {.53,.75,.97}) {
                        deck.beam({x-.245,y,face},{x+.245,y,face},.071,.165,bulkhead);
                        deck.beam({x-.09,y+.008,face-side*.041},{x+.09,y+.008,face-side*.041},.025,.029,painted);
                    }
                }
            }
        }
        for (double x : {left+annexStripInset,right-annexStripInset}) {
            deck.beam({x,roof-.15,front+.11},{x,roof-.15,back-.11},.119,.14,frame);
            deck.beam({x,roof-annexStripDrop,front+annexStripEnd},{x,roof-annexStripDrop,back-annexStripEnd},
                      .057,.024,lamp,observation?.58f:.68f);
        }
        const double doorMid=(annex.doorwayLeft+annex.doorwayRight)*.5;
        const double entry=attach+side*.18, end=observation?attach+side*.35:z-side*.40;
        for (double x : {doorMid-.37,doorMid+.37}) {
            deck.beam({x,floor+.015,entry},{x,floor+.015,end},.043,.021,liner);
            deck.beam({x,floor+.028,entry+side*.038},{x,floor+.028,end-side*.038},.018,.014,railLight,.71f);
        }
        for (size_t i=0;i<room.fixtures.size();++i) {
            const auto& item=room.fixtures[i];
            const double x=(item.left+item.right)*.5,zMid=(item.front+item.back)*.5;
            if (observation && i==0) {
                box(item,floor,-.71,liner);
                box(item,-.71,-.64,worktop);
                const double near=side>0?item.front:item.back;
                const double far=side>0?item.back:item.front;
                planter(x,-.64,near+side*.36);
                const double pivot=far-side*.42;
                deck.beam({x,-.63,pivot},{x,-.31,pivot},.218,.204,frame);
                deck.beam({x-.16,-.295,pivot},{x+.16,-.295,pivot},.14,.15,painted);
                const glm::dvec3 backEnd(x,-.28,pivot-side*.32),frontEnd(x,-.088,pivot+side*.32);
                const auto axis=glm::normalize(frontEnd-backEnd);
                deck.beam(backEnd,frontEnd,.206,.212,frame);
                deck.beam(backEnd+axis*.13,frontEnd-axis*.072,.224,.23,painted);
                deck.beam(frontEnd-axis*.082,frontEnd,.258,.260,liner);
                deck.beam(frontEnd-axis*.020,frontEnd+axis*.003,.216,.219,cabinScreen);
                deck.beam(backEnd-axis*.042,backEnd+axis*.08,.130,.139,liner);
                deck.beam(backEnd-axis*.082,backEnd-axis*.039,.156,.153,cabinRubber);
                deck.beam({x+.079,-.38,pivot-.048},{x+.079,-.38,pivot+.045},.013,.021,railLight,.58f);
                continue;
            }
            if (observation && i==1) {
                const double towards=x<doorMid?1.:-1.;
                const double backX=x-towards*.238;
                const double frontX=x+towards*.312;
                box(item,floor,-.77,liner);
                deck.beam({frontX,-.78,item.front+.065},{frontX,-.78,item.back-.065},.028,.061,frame);
                const unsigned seats=3+unsigned(item.back-item.front>2.63);
                for (unsigned seat=0;seat<seats;++seat) {
                    const double a=std::lerp(item.front+.045,item.back-.045,double(seat)/seats);
                    const double b=std::lerp(item.front+.045,item.back-.045,double(seat+1)/seats);
                    deck.beam({x,-.68,a+.016},{x,-.68,b-.016},.582,.178,upholstery);
                    deck.beam({backX,-.383,a+.016},{backX,-.383,b-.016},.191,.486,upholstery);
                    deck.beam({frontX-.044*towards,-.603,a+.061},{frontX-.044*towards,-.603,b-.061},
                              .013,.010,stitching);
                }
                for (double arm : {item.front+.056,item.back-.056})
                    deck.beam({backX,-.485,arm},{frontX,-.485,arm},.099,.176,frame);
                continue;
            }
            if (observation) {
                deck.beam({x,floor+.08,zMid},{x,-.51,zMid},.132,.127,frame);
                box(item,-.54,-.46,worktop);
                deck.beam({x,-.456,zMid},{x,-.42,zMid},.147,.154,frame);
                deck.beam({x,-.42,zMid},{x,-.21,zMid},.036,.037,frame);
                const auto bulb=taskLamp.position;
                deck.beam(bulb+glm::dvec3(0,-.011,0),bulb+glm::dvec3(0,.058,0),.152,.151,readingLight,.82f);
                deck.beam(bulb+glm::dvec3(0,.059,0),bulb+glm::dvec3(0,.086,0),.188,.192,painted);
                const DeckArea book{x-.095,zMid-.264,x+.063,zMid-.116};
                box(book,-.46,-.432,wallPlate);
                box({book.left-.004,book.front-.004,book.right+.004,book.back+.004},-.435,-.426,upholstery);
                const double cupX=x+.093,cupZ=zMid+.196;
                deck.beam({cupX,-.46,cupZ},{cupX,-.359,cupZ},.069,.069,wallPlate);
                deck.beam({cupX,-.359,cupZ},{cupX,-.346,cupZ},.076,.075,frame);
                deck.beam({cupX,-.345,cupZ},{cupX,-.343,cupZ},.053,.054,liner);
                continue;
            }
            const bool cabinet=room.kind==StationModuleKind::Engineering||room.kind==StationModuleKind::Archive;
            const double top=cabinet?.51:-.37;
            box(item,floor,top,cabinet?frame:liner);
            const double face=i==0?item.right-.018:item.left+.018;
            if (cabinet) {
                for (double shelf : {-.81,-.43,-.05,.32}) {
                    deck.beam({face,shelf,item.front+.086},{face,shelf,item.back-.086},.044,.11,bulkhead);
                    deck.beam({face,shelf+.06,zMid-.13},{face,shelf+.06,zMid+.13},.030,.026,painted);
                }
            } else {
                box(item,top,top+.066,worktop);
                if (room.kind==StationModuleKind::Analysis && i==0) {
                    const double workZ=zMid+.21, trayZ=zMid-.42;
                    const auto light=taskLamp.position;
                    const auto span=glm::dvec3(0,0,taskLamp.halfLength);
                    const auto housing=glm::dvec3(0,.023,0);
                    deck.beam(light-span+housing,light+span+housing,.137,.051,frame);
                    deck.beam(light-span*.94,light+span*.94,.091,.020,inspectionLight,.83f);
                    for (const double offset : {-taskLamp.halfLength*.80,taskLamp.halfLength*.80})
                        deck.beam({left+.067,light.y+.047,light.z+offset},
                                  {light.x,light.y+.047,light.z+offset},.035,.041,frame);
                    deck.beam({x,top+.094,workZ-.25},{x,top+.094,workZ+.26},.51,.076,frame);
                    deck.beam({x-.116,-.24,workZ},{x-.116,.255,workZ},.089,.091,painted);
                    deck.beam({x-.116,.247,workZ},{x+.127,.247,workZ},.134,.115,frame);
                    deck.beam({x+.096,.253,workZ},{x+.096,-.12,workZ},.115,.12,liner);
                    deck.beam({x+.096,-.125,workZ},{x+.096,-.154,workZ},.078,.078,railLight,.45f);
                    deck.beam({x+.096,-.213,workZ-.17},{x+.096,-.213,workZ+.17},.218,.046,wallPlate);
                    deck.beam({x,-.247,trayZ-.16},{x,-.247,trayZ+.16},.284,.069,painted);
                    for (double dx : {-.09,0.,.09}) {
                        deck.beam({x+dx,-.224,trayZ},{x+dx,-.036,trayZ},.052,.054,wallPlate);
                        deck.beam({x+dx,-.062,trayZ},{x+dx,-.024,trayZ},.063,.064,frame);
                    }
                    continue;
                }
                const double displayX=x+(i==0?.032:-.032);
                const CabinSurface console{{displayX,.265,zMid+(i==0?.33:-.33)},
                                           {0,0,i==0?-.66:.66},{0,-.40,0}};
                deck.instrument(console,true);
                for (unsigned row=0;row<4;++row) {
                    const double y=.185-row*.063;
                    deck.beam({displayX+(i==0?.018:-.018),y,zMid-.20},
                              {displayX+(i==0?.018:-.018),y,zMid+.17-double(row%2)*.13},
                              .010,.012,railLight,.59f);
                }
            }
        }
    }
    const auto& bay=layout.freightBay();
    endWall(bay.floor.left,bay.floor.front,bay.floor.back,floor,bay.ceiling);
    const auto dispatch=stationFreightScreen(layout);
    deck.instrument(dispatch,true);
    const double x=bay.floor.left+.084;
    deck.beam({x,1.63,centre-1.09},{x,1.63,centre+1.09},.121,.21,frame);
    deck.beam({x+.068,1.62,centre-.88},{x+.068,1.62,centre+.88},.012,.035,warmLight,.74f);
    for (double side : {-1.,1.}) {
        const double z=centre+side*1.12;
        deck.beam({x,floor,z},{x,1.73,z},.146,.135,liner);
        deck.beam({x+.074,.34,z},{x+.074,1.30,z},.019,.019,railLight,.74f);
        const auto pad=layout.freightPad;
        const double track=centre+side*.81;
        deck.beam({pad.left,bay.ceiling-.30,track},{pad.right+1.22,bay.ceiling-.30,track},.078,.12,frame);
        deck.beam({pad.left+.08,bay.ceiling-.369,track},{pad.right+1.13,bay.ceiling-.369,track},
                  .025,.024,railLight,.64f);
    }
    const double hoist=layout.freightPad.left+.57;
    deck.beam({hoist,bay.ceiling-.39,centre-.84},{hoist,bay.ceiling-.39,centre+.84},.136,.084,liner);
    deck.beam({hoist,bay.ceiling-.45,centre-.27},{hoist,bay.ceiling-.45,centre+.27},.15,.056,frame);
    for (double side : {-1.,1.})
        deck.beam({hoist,bay.ceiling-.46,centre+side*.16},{hoist,bay.ceiling-.62,centre+side*.21},.034,.036,safety,.44f);
    deck.mesh.compact();
    return std::move(deck.mesh);
}

Mesh xalariaAirlockDoor() {
    using namespace stationDeck;
    const CabinFinish shell{{.34f,.38f,.38f},CabinMaterial::Brushed};
    const CabinFinish inset{{.11f,.17f,.18f},CabinMaterial::Painted};
    const CabinFinish hazard{{1.f,.68f,.26f},CabinMaterial::Display};
    CabinBuilder door;
    door.beam({0,(shipDeck::deckFloor+doorTop)*.5,centre-doorHalf+.008},
              {0,(shipDeck::deckFloor+doorTop)*.5,centre+doorHalf-.008},
              .13,doorTop-shipDeck::deckFloor,shell);
    for (double side : {-1.,1.}) {
        const double x=side*.069;
        door.beam({x,-.12,centre-.35},{x,-.12,centre+.35},.040,1.61,inset);
        door.beam({x+side*.023,.46,centre-.31},{x+side*.023,.46,centre+.31},.021,.039,hazard,.85f);
        for (double z : {centre-.29,centre+.29})
            door.beam({x+side*.019,-.77,z},{x+side*.019,.30,z},.036,.031,cabinTrim);
    }
    door.mesh.compact();
    return std::move(door.mesh);
}

Mesh xalariaAirlockSill() {
    using namespace stationDeck;
    CabinBuilder sill;
    sill.beam({0,floor+.015,centre-doorHalf},{0,floor+.015,centre+doorHalf},.27,.027,cabinTrim);
    for (double x : {-.088,.088})
        sill.beam({x,floor+.027,centre-doorHalf+.018},{x,floor+.027,centre+doorHalf-.018},
                  .018,.007,cabinRubber);
    sill.mesh.compact();
    return std::move(sill.mesh);
}

Mesh xalariaAirlockIndicators() {
    using namespace stationDeck;
    const CabinFinish diffuser{{.69f,.73f,.70f},CabinMaterial::Display};
    CabinBuilder lights;
    lights.beam({0,doorTop+.063,centre-.13},{0,doorTop+.063,centre+.13},.219,.024,diffuser,.65f);
    lights.beam({0,floor+.031,centre-.31},{0,floor+.031,centre+.31},.027,.006,diffuser,.52f);
    for (double z : {centre-doorHalf+.065,centre+doorHalf-.065})
        lights.beam({0,.38,z},{0,.55,z},.254,.034,diffuser,.73f);
    lights.mesh.compact();
    return std::move(lights.mesh);
}

Mesh xalariaCharterCrate(const StationLayout& layout) {
    using namespace stationDeck;
    const auto& freightPad=layout.freightPad;
    CabinBuilder cargo;
    const CabinFinish casework{{.58f,.44f,.23f},CabinMaterial::Painted};
    const CabinFinish lock{{.29f,1.f,.68f},CabinMaterial::Display};
    cargo.beam({freightPad.left+.09,-.35,centre},{freightPad.right-.10,-.35,centre},.83,1.07,casework);
    for (double z : {centre-.29,centre+.29}) {
        cargo.beam({freightPad.left+.10,.188,z},{freightPad.right-.10,.188,z},.089,.044,cabinTrim);
        cargo.beam({freightPad.right-.087,-.74,z},{freightPad.right-.087,.189,z},.074,.041,cabinFrame);
    }
    const double face=freightPad.right-.082;
    cargo.beam({face,-.291,centre-.208},{face,-.291,centre+.208},.058,.37,cabinFrame);
    cargo.beam({face+.032,-.20,centre-.141},{face+.032,-.20,centre+.141},.017,.095,cabinScreen);
    for (double z : {-.102,-.047,.008,.063})
        cargo.beam({face+.042,-.20,centre+z},{face+.042,-.20,centre+z+.020},.012,.025,lock,.77f);
    cargo.beam({face+.037,-.401,centre-.12},{face+.037,-.401,centre+.12},.041,.039,cabinTrim);
    for (double z : {-.334,.334})
        cargo.beam({face,-.713,centre+z-.046},{face,-.713,centre+z+.046},.035,.081,cabinCopper);
    cargo.mesh.compact();
    return std::move(cargo.mesh);
}

namespace {
constexpr double terminalWidth = 360, terminalInset = 18, terminalRight = terminalWidth-terminalInset;
constexpr glm::vec3 terminalMuted{.61f,.74f,.74f}, terminalRule{.12f,.25f,.25f};

CabinDisplay cabinDisplay(const CabinSurface& face, bool background = true) {
    const auto scale = glm::length(face.right)/terminalWidth;
    CabinDisplay display;
    display.size = {terminalWidth,glm::length(face.down)/scale};
    display.model = {glm::vec4(face.right/terminalWidth,0),
                     glm::vec4(face.down/display.size.y,0),glm::vec4(face.normal(),0),
                     glm::vec4(face.origin+face.normal()*.005,1)};
    if (background)
        rect(display.content.panels,0,0,terminalWidth,display.size.y,{.022,.044,.050});
    return display;
}

CabinDisplay deckDisplay(const CabinSurface& face, std::string_view title, glm::vec3 accent) {
    auto display = cabinDisplay(face);
    auto& screen = display.content;
    rect(screen.panels,0,0,terminalWidth,3,accent*.65f);
    rect(screen.panels,terminalInset,43,terminalRight-terminalInset,1,terminalRule);
    atlasText(screen,title,terminalInset,13,211,22,accent);
    return display;
}

void terminalText(CockpitOverlay& screen, std::string_view label, double x, double y, double width,
                  double size, glm::vec3 colour, bool rightAligned = false) {
    const double advance = atlasTextWidth(label,size);
    if (advance > width)
        size = std::max(15.,size*width/advance);
    if (rightAligned) {
        const double spare = std::max(0.,width-atlasTextWidth(label,size));
        x += spare;
        width -= spare;
    }
    atlasText(screen,label,x,y,width+.001,size,colour);
}

void terminalBar(CockpitOverlay& screen, double y, double fraction, glm::vec3 accent) {
    constexpr int segments = 12;
    constexpr double pitch = (terminalRight-terminalInset)/segments, gap = 3;
    for (int i=0;i<segments;++i) {
        const double x = terminalInset+i*pitch;
        const double fill = std::clamp(fraction*segments-i,0.,1.);
        rect(screen.panels,x,y,pitch-gap,8,terminalRule);
        if (fill > 0)
            rect(screen.panels,x,y,(pitch-gap)*fill,8,accent);
    }
}
} // namespace

namespace {
enum class SignArrow { None, Ahead, Left, Right };
struct SignLegend {
    std::string_view text;
    glm::vec3 accent;
    SignArrow arrow=SignArrow::None;
};

void signPlate(Mesh& mesh,double x,double y,double w,double h,double cut,double z,glm::vec3 colour) {
    const std::array<glm::dvec3,8> edge{{{x+cut,y,z},{x+w-cut,y,z},{x+w,y+cut,z},{x+w,y+h-cut,z},
        {x+w-cut,y+h,z},{x+cut,y+h,z},{x,y+h-cut,z},{x,y+cut,z}}};
    for (size_t n=1;n+1<edge.size();++n)
        triangle(mesh,edge[0],edge[n],edge[n+1],colour);
}

void signArrow(Mesh& mesh,double x,double y,double size,SignArrow arrow,glm::vec3 colour) {
    const glm::dvec2 axis=arrow==SignArrow::Ahead ? glm::dvec2(0,-1) :
                          glm::dvec2(arrow==SignArrow::Left?-1:1,0);
    const glm::dvec2 across(-axis.y,axis.x);
    const auto point=[&](double along,double side) {
        return glm::dvec3(glm::dvec2(x,y)+size*(along*axis+side*across),-.001);
    };
    const auto a=point(-.46,-.11),b=point(.11,-.11),c=point(.11,.11),d=point(-.46,.11);
    triangle(mesh,a,b,c,colour);
    triangle(mesh,a,c,d,colour);
    triangle(mesh,point(.02,-.34),point(.49,0),point(.02,.34),colour);
}

CabinDisplay wayfindingSign(const CabinSurface& face,std::initializer_list<SignLegend> legends) {
    auto sign=cabinDisplay(face,false);
    sign.model[3]+=sign.model[2]*.009f;
    const double w=sign.size.x,h=sign.size.y;
    const double rim=h*.055, cut=h*.14, tile=h*.69;
    auto& mesh=sign.content.panels;
    signPlate(mesh,0,0,w,h,cut,-.008,{.18f,.23f,.25f});
    signPlate(mesh,rim,h*.07,w-2*rim,h*.86,cut-rim,-.006,{.025f,.041f,.048f});
    const double segment=(w-2*rim)/double(legends.size());
    const double pad=h*.145, band=h*.073;
    double size=h*.76;
    for (const auto& legend : legends) {
        const double iconWidth=legend.arrow==SignArrow::None?0.:tile+pad;
        size=std::min(size,(segment-2*pad-iconWidth)/atlasTextWidth(legend.text,1.));
    }
    double x=rim;
    for (const auto& legend : legends) {
        const bool icon=legend.arrow!=SignArrow::None, trailing=legend.arrow==SignArrow::Right;
        const double iconX=trailing?x+segment-pad-tile:x+pad;
        const double left=x+pad+(icon&&!trailing?tile+pad:0.);
        const double right=x+segment-pad-(icon&&trailing?tile+pad:0.);
        signPlate(mesh,x+h*.20,h*.075,segment-h*.40,band,band*.30,-.004,legend.accent*.83f);
        if (icon) {
            signPlate(mesh,iconX,(h-tile)*.5,tile,tile,tile*.14,-.003,legend.accent*.21f);
            signArrow(mesh,iconX+tile*.5,h*.5,tile*.78,legend.arrow,legend.accent);
        }
        const double advance=atlasTextWidth(legend.text,size);
        atlasText(sign.content,legend.text,left+(right-left-advance)*.5,(h-size)*.5,
                  advance+.01,size,{.94f,.97f,.98f});
        x+=segment;
    }
    return sign;
}

CabinDisplay stationLabel(double x,double z,double top,double width,double direction,SignLegend legend) {
    return wayfindingSign({{x,top,z+direction*width*.5},{0,0,-direction*width},{0,-.164,0}},{legend});
}
}

const std::array<CabinDisplay,9>& xalariaShipSigns() {
    using namespace shipDeck;
    using enum SignArrow;
    static const std::array signs{
        wayfindingSign({{.40,.627,1.025},{-.80,0,0},{0,-.105,0}},{{"Main deck",cabinLamp,Ahead}}),
        wayfindingSign({{-.40,.697,1.239},{.80,0,0},{0,-.119,0}},{{"Cockpit",cabinSignal,Ahead}}),
        wayfindingSign({{.54,.904,cargoFront-.059},{-1.08,0,0},{0,-.112,0}},{{"Cargo bay",cargoLight,Ahead}}),
        wayfindingSign({{-.55,1.101,cargoFront+.033},{1.10,0,0},{0,-.151,0}},{{"Main passage",cabinSignal,Ahead}}),
        wayfindingSign({{.69,.765,cargoScreenZ+.020},{-1.38,0,0},{0,-.148,0}},
                       {{"Cargo terminal",cargoLight,Ahead},{"Airlock",cabinSignal,Right}}),
        wayfindingSign({{-passageHalf+.065,.950,3.138},{0,0,-1.018},{0,-.116,0}},{{"Crew quarters",berthLight}}),
        wayfindingSign({{passageHalf-.065,.950,2.12},{0,0,1.018},{0,-.116,0}},{{"Systems alcove",engineeringLight}}),
        wayfindingSign({{.65,.904,quartersDoorFront+.24},{-1.30,0,0},{0,-.128,0}},
                       {{"Systems alcove",engineeringLight,Left},{"Crew quarters",berthLight,Right}}),
        wayfindingSign({{-.65,.904,quartersDoorBack-.22},{1.30,0,0},{0,-.128,0}},
                       {{"Crew quarters",berthLight,Left},{"Systems alcove",engineeringLight,Right}})
    };
    return signs;
}

const std::array<CabinDisplay,3>& xalariaAirlockSigns() {
    using namespace stationDeck;
    static const std::array signs{
        stationLabel(shipDoor+.048,centre,1.147,.96,1.,{"Airlock",cabinSignal,SignArrow::Ahead}),
        stationLabel(bridgeDoor+.075,centre,1.152,1.03,1.,{"Docking bridge",cabinSignal,SignArrow::Ahead}),
        stationLabel(bridgeDoor-.078,centre,1.107,.82,-1.,{"Return to ship",cabinSignal,SignArrow::Ahead})
    };
    return signs;
}

std::vector<CabinDisplay> xalariaStationSigns(const StationLayout& layout) {
    std::vector<CabinDisplay> signs;
    const auto colour=stationAccent(layout.key.role);
    for (size_t i=2;i<layout.modules.size();++i) {
        const auto& room=layout.modules[i];
        const auto& previous=layout.modules[i-1];
        if (room.kind==previous.kind) continue;
        const double head=std::min(room.ceiling,previous.ceiling);
        const double width=std::min(1.48,std::min(room.floor.back,previous.floor.back)-stationDeck::centre+.68);
        const bool arched=previous.kind==StationModuleKind::Bridge;
        const double entryTop=arched?head-.223:head-.018;
        const double returnTop=arched?head+.225:entryTop;
        const bool freight=room.kind==StationModuleKind::Freight;
        const auto name=freight?(layout.key.vessel?"Transfer bay":"Loading bay"):stationModuleName(room.kind);
        signs.push_back(stationLabel(room.floor.right+.082,stationDeck::centre,entryTop,arched?.96:width,1.,
                                     {name,freight?cargoLight:colour,SignArrow::Ahead}));
        signs.push_back(stationLabel(room.floor.right-(arched?.018:.082),stationDeck::centre,returnTop,width,-1.,
                                     {"Ship airlock",cabinSignal,SignArrow::Ahead}));
    }
    for (const auto& annex : layout.annexes) {
        const double width=annex.doorwayRight-annex.doorwayLeft;
        const double x=(annex.doorwayRight+annex.doorwayLeft)*.5;
        const double y=stationDeck::annexDoorTop+.255;
        const double span=annex.side*width;
        const double offset=annex.side*.065;
        const double wall=annex.insideWall();
        const auto name=stationModuleName(annex.room.kind);
        signs.push_back(wayfindingSign({{x+span*.5,y,wall-offset},{-span,0,0},{0,-.164,0}},{{name,colour}}));
        signs.push_back(wayfindingSign({{x-span*.5,stationDeck::annexDoorTop+.050,wall+offset},
                                       {span,0,0},{0,-.152,0}},
                                      {{"Main corridor",cabinSignal,SignArrow::Ahead}}));
        // Flags extend into the corridor above each annex entrance. The view
        // along the aisle determines which way to turn through the doorway.
        constexpr double flagWidth=1.06;
        for (double direction : {-1.,1.}) {
            const auto turn=direction*annex.side>0.?SignArrow::Left:SignArrow::Right;
            signs.push_back(stationLabel(x+direction*.003,wall-annex.side*flagWidth*.5,y+.25,flagWidth,direction,
                                         {name,colour,turn}));
        }
    }
    return signs;
}

CabinDisplay xalariaLoadingBayDisplay(const StationLayout& layout,const XalariaEconomy& economy,const XContract& charter,
                                      bool claimed,std::string_view sender) {
    const auto signal=stationAccent(layout.key.role);
    auto display=deckDisplay(stationFreightScreen(layout),layout.key.vessel?"Courier transfer":"Freight dispatch",signal);
    auto& screen=display.content;
    terminalText(screen,sender.empty()?economy.placeName(charter.origin):std::string(sender),18,55,324,21,pearl);
    rect(screen.panels,18,88,324,1,terminalRule);
    if (charter.units) {
        terminalText(screen,claimed?"Today's freight collected":"Sealed freight ready",18,102,324,24,signal);
        terminalText(screen,economy.placeName(charter.destination),18,139,324,22,pearl);
        terminalText(screen,std::to_string(charter.units)+" units  /  "+xMoney(charter.reward),18,168,324,17,terminalMuted);
    } else {
        terminalText(screen,"No charter available today",18,110,324,20,terminalMuted);
    }
    return display;
}

CabinDisplay xalariaStationSurveyDisplay(const StationAnnex& annex,const XalariaMapView& map) {
    const auto signal=stationAccent(StationRole::Research);
    auto display=deckDisplay(stationSurveyScreen(annex),"System survey",signal);
    auto& screen=display.content;
    const auto& system=map.catalog->star(map.current);
    terminalText(screen,system.name,18,55,177,31,pearl);
    terminalText(screen,xalariaLookName(map.catalog->region(map.current).look),204,63,138,16,signal,true);
    rect(screen.panels,18,99,324,1,terminalRule);
    terminalText(screen,"Catalogued bodies",18,111,206,19,terminalMuted);
    terminalText(screen,std::to_string(system.worlds.size()),233,108,109,24,pearl,true);
    terminalText(screen,"Wormhole connections",18,147,206,19,terminalMuted);
    terminalText(screen,std::to_string(system.wormholes.size()),233,144,109,24,pearl,true);
    return display;
}

void xalariaStationLocation(CockpitOverlay& hud,std::string_view room,std::string_view station,StationRole role,float roomOpacity) {
    constexpr double x=26,y=23,width=359,height=67;
    const auto accent=stationAccent(role);
    rect(hud.panels,x,y,width,height,ink,.91f);
    rect(hud.panels,x,y,3,height,accent);
    atlasText(hud,room,x+14,y+8,width-28,25,pearl,roomOpacity);
    atlasText(hud,station,x+14,y+40,width-28,16,glm::mix(accent,pearl,.52f));
}

std::array<CabinDisplay,3> xalariaDeckDisplays(const FlightInstruments& instruments,const XalariaMapView& map,
                                               const XalariaEconomy& economy) {
    const glm::vec3 navigationAccent{.42f,.85f,.99f}, cargoAccent{1.f,.75f,.41f};
    auto nav = deckDisplay(navigationDeckScreen,"Navigation",navigationAccent);
    auto& course = nav.content;
    const bool routed = !map.activeRoute.empty();
    const auto mode = map.paused ? "Paused" : map.arrived ? "Arrived" :
                      map.flying ? "Underway" : "Manual";
    terminalText(course,mode,236,15,106,18,map.paused ? cargoAccent : navigationAccent,true);
    const auto starName = [&](uint32_t seed) -> std::string_view {
        return map.catalog ? std::string_view(map.catalog->star(seed).name) : "Unavailable";
    };
    terminalText(course,routed ? "Destination" : "Current system",18,55,324,17,terminalMuted);
    terminalText(course,starName(routed ? map.destination : map.current),18,78,324,39,pearl);
    rect(course.panels,18,130,324,1,terminalRule);
    if (routed) {
        const auto hops = map.activeRoute.size()-1;
        const auto leg = hops ? "Leg "+std::to_string(std::min(map.activeLeg+1,hops))+
                                    " / "+std::to_string(hops) : "Local course";
        terminalText(course,map.finalApproach ? "Final approach" : leg,18,142,157,18,terminalMuted);
        terminalText(course,map.arrived ? "Complete" : flightQuantity(map.range),185,141,157,22,pearl,true);
        terminalBar(course,178,map.progress,map.paused ? cargoAccent : navigationAccent);
    } else {
        terminalText(course,map.wormhole ? "Wormhole to" : "Course",18,142,125,17,terminalMuted);
        terminalText(course,map.wormhole ? starName(map.wormhole->destination) : "No route plotted",
                     147,140,195,22,pearl,true);
    }

    auto systems = deckDisplay(systemsDeckScreen,"Ship systems",cabinSignal);
    auto& health = systems.content;
    const double hull = std::clamp(instruments.hull/100.,0.,1.);
    const double fuel = economy.fuelCapacity() > 0 ? economy.fuel()/economy.fuelCapacity() : 0.;
    const auto hullAccent = hull <= .30 ? danger : cabinSignal;
    const auto fuelAccent = fuel <= .10 ? danger : navigationAccent;
    const bool warning = hull <= .30 || fuel <= .10;
    terminalText(health,warning ? "Attention" : "Online",236,15,106,18,
                 warning ? cargoAccent : cabinSignal,true);
    terminalText(health,"Hull integrity",18,55,166,21,pearl);
    terminalText(health,std::to_string(int(std::round(hull*100)))+"%",188,51,154,28,hullAccent,true);
    terminalBar(health,88,hull,hullAccent);
    rect(health.panels,18,111,324,1,terminalRule);
    terminalText(health,"Fuel reserve",18,125,166,21,pearl);
    char fuelReadout[32];
    std::snprintf(fuelReadout,sizeof(fuelReadout),"%.1f / %.0f",economy.fuel(),economy.fuelCapacity());
    terminalText(health,fuelReadout,188,121,154,28,fuelAccent,true);
    terminalBar(health,158,fuel,fuelAccent);
    terminalText(health,std::to_string(int(std::round(fuel*100)))+"% of tank capacity",
                 18,177,324,16,terminalMuted);

    auto cargo = deckDisplay(cargoDeckScreen,"Cargo terminal",cargoAccent);
    auto& manifest = cargo.content;
    const auto used = economy.cargoUnits();
    const auto free = int64_t(economy.holdCapacity())-used;
    terminalText(manifest,!used ? "Empty" : !free ? "Full" : "Loaded",236,15,106,18,cargoAccent,true);
    terminalText(manifest,"Units loaded",18,55,156,17,terminalMuted);
    terminalText(manifest,"Units free",197,55,145,17,terminalMuted,true);
    terminalText(manifest,std::to_string(used)+" / "+std::to_string(economy.holdCapacity()),
                 18,76,180,37,pearl);
    terminalText(manifest,std::to_string(free),207,76,135,37,cargoAccent,true);
    terminalBar(manifest,118,double(used)/economy.holdCapacity(),cargoAccent);
    rect(manifest.panels,18,137,324,1,terminalRule);
    const auto& stock = economy.cargo();
    const auto largest = std::max_element(stock.begin(),stock.end());
    if (*largest > 0) {
        const auto good = XGood(std::distance(stock.begin(),largest));
        terminalText(manifest,xGoodInfo(good).name,18,145,231,18,pearl);
        terminalText(manifest,std::to_string(*largest)+" units",255,145,87,17,cargoAccent,true);
    } else {
        terminalText(manifest,used ? "Art or sealed freight" : "No freight on board",
                     18,145,324,18,terminalMuted);
    }
    rect(manifest.panels,18,170,324,1,terminalRule);
    const bool connected = economy.dockedPlace().has_value();
    terminalText(manifest,connected ? "E Market / B Case files" : "B Case files / Dock to trade",
                 18,179,324,17,connected?cabinSignal:terminalMuted);
    return {std::move(nav),std::move(systems),std::move(cargo)};
}

Mesh xalariaCockpitGlazing() {
    Mesh glass;
    const auto pane = [&](glm::dvec3 a, glm::dvec3 b, glm::dvec3 c, glm::dvec3 d, glm::vec3 tint) {
        triangle(glass,a,b,c,tint);
        triangle(glass,a,c,d,tint);
    };
    const auto glassCorner = [](glm::dvec3 corner, double side, size_t index) {
        corner.x += .017;
        if (index == 0)
            corner.z -= .018;
        return cabinSide(corner, side);
    };
    pane(glassCorner(cabinSill.front(),-1,0),glassCorner(cabinSill.front(),1,0),
         glassCorner(cabinCeiling.front(),1,0),glassCorner(cabinCeiling.front(),-1,0),{.49,.67,.73});
    for (double side : {-1.,1.})
        for (size_t i = 0; i+1 < cabinSill.size(); ++i)
            pane(glassCorner(cabinSill[i],side,i),glassCorner(cabinSill[i+1],side,i+1),
                 glassCorner(cabinCeiling[i+1],side,i+1),glassCorner(cabinCeiling[i],side,i),{.43,.60,.64});
    for (double side : {-1.,1.}) {
        const auto vertices=cargoWindow(side*(shipDeck::cargoHalf-.008),0);
        const glm::dvec3 centre=glm::mix(vertices[0],vertices[4],.5);
        for (size_t i=0;i<vertices.size();++i)
            triangle(glass,centre,vertices[i],vertices[(i+1)%vertices.size()],{.35,.58,.71});
    }
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

std::array<CabinDisplay,4> xalariaCockpitDisplays(const FlightInstruments& instruments,
                                                  double fuel, double fuelCapacity) {
    const glm::vec3 grid = terminalRule;
    const auto divider = [grid](CockpitOverlay& screen, double y) {
        rect(screen.panels,terminalInset,y,terminalRight-terminalInset,1,grid);
    };
    const auto quantity = [](CockpitOverlay& screen, double value, double x, double y, double width,
                             double size, glm::vec3 colour, bool speed = false) {
        terminalText(screen,flightQuantity(value,speed),x,y,width,size,colour);
    };

    auto driveDisplay = cabinDisplay(driveScreen);
    auto& drive = driveDisplay.content;
    const bool aboveSurface = instruments.altitude >= 0;
    const auto& docking = instruments.docking;
    const bool dockDisplay = docking.active && docking.phase != DockingPhase::Departure;
    const bool linedUp = docking.phase == DockingPhase::ManualCapture || manualClampReady(docking);
    atlasText(drive,dockDisplay ? (docking.closing < -.005 ? "Opening speed" : "Closing speed") : "Velocity",
              18,12,174,18,cabinSignal);
    atlasText(drive,dockDisplay ? "Berth range" : aboveSurface ? "Altitude" : "Stop distance",
              210,12,132,18,cabinSignal);
    divider(drive,34);
    quantity(drive,dockDisplay ? std::abs(docking.closing) : instruments.speed,18,41,172,27,cabinLamp,true);
    quantity(drive,dockDisplay ? docking.range : aboveSurface ? instruments.altitude : instruments.stoppingDistance,
             210,41,132,23,cabinLamp);
    rect(drive.panels,197,10,1,86,grid);
    terminalText(drive,dockDisplay ? docking.docked ? "Clamps secure" : linedUp ? "Hold steady" :
                 manualDocking(docking.phase) ? "Manual approach" : "Auto docking" : instruments.drive,
                 18,78,172,17,cabinSignal);
    const auto bank = xalariaBankAngle(instruments.localUp);
    char bankLabel[18] = "Bank --";
    if (bank) {
        const int degrees = int(std::round(glm::degrees(*bank)));
        if (degrees == 0) std::snprintf(bankLabel,sizeof(bankLabel),"Bank 0");
        else std::snprintf(bankLabel,sizeof(bankLabel),"Bank %c%d",degrees > 0 ? 'L' : 'R',std::abs(degrees));
    }
    if (dockDisplay)
        std::snprintf(bankLabel,sizeof(bankLabel),"Align %.1f deg",glm::degrees(docking.heading));
    terminalText(drive,bankLabel,210,78,132,17,cabinSignal);

    auto portDisplay = cabinDisplay(portScreen);
    auto& port = portDisplay.content;
    terminalText(port,"Throttle",18,21,163,23,pearl);
    terminalText(port,std::to_string(int(std::round(instruments.throttle*100)))+"%",
                 198,17,144,29,amber,true);
    terminalBar(port,58,instruments.throttle,amber);
    divider(port,81);
    const double fuelFraction = fuelCapacity > 0 ? fuel / fuelCapacity : 0.;
    const auto fuelColour = fuelFraction <= .10 ? danger : cabinSignal;
    char fuelReadout[32];
    std::snprintf(fuelReadout,sizeof(fuelReadout),"%.1f / %.0f",fuel,fuelCapacity);
    terminalText(port,"Fuel reserve",18,101,163,23,pearl);
    terminalText(port,fuelReadout,191,98,151,27,fuelColour,true);
    terminalBar(port,138,fuelFraction,fuelColour);

    auto starboardDisplay = cabinDisplay(starboardScreen);
    auto& starboard = starboardDisplay.content;
    const std::string_view name = instruments.targetLabel.empty() ? instruments.destination : instruments.targetLabel;
    const bool hasTarget = instruments.targetVisible && glm::length(instruments.localTarget) > 1e-30;
    const bool aft = hasTarget && instruments.localTarget.z > 0;
    constexpr std::string_view wormholePrefix = "WORMHOLE TO ";
    const bool wormhole = hasTarget && name.starts_with(wormholePrefix);
    terminalText(starboard,wormhole ? "Wormhole" : "Navigation",18,15,176,20,cabinSignal);
    rect(starboard.panels,18,41,176,1,grid);
    rect(starboard.panels,205,16,1,129,grid);
    const auto target = wormhole ? name.substr(wormholePrefix.size()) : name.substr(0,name.find(" / "));
    terminalText(starboard,hasTarget ? target : "No target",18,50,176,25,cabinLamp);
    if (hasTarget) {
        quantity(starboard,instruments.range,18,85,127,18,cabinSignal);
        if (aft)
            atlasText(starboard,"Aft",155,85,39,18,amber);
    }
    terminalText(starboard,"Hull "+std::to_string(int(std::round(instruments.hull)))+"%",
                 18,124,176,20,instruments.hull > 30 ? cabinSignal : danger);

    // The scope is in ship axes, so looking around the cabin leaves the bearing fixed.
    const glm::dvec2 centre{274,81};
    for (double radius : {22.,52.}) {
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
    rect(starboard.panels,centre.x-53,centre.y-.55,106,1.1,grid);
    rect(starboard.panels,centre.x-.55,centre.y-53,1.1,106,grid);
    if (hasTarget) {
        const auto direction = glm::normalize(instruments.localTarget);
        const glm::dvec2 bearing(direction.x,-direction.y);
        const auto heading = glm::length(bearing) > 1e-8 ? glm::normalize(bearing) : glm::dvec2(0,1);
        const double polar = std::acos(std::clamp(-direction.z,-1.,1.));
        const auto p = centre + heading*(45*polar/std::numbers::pi);
        const auto colour = aft ? amber : cabinLamp;
        const glm::dvec3 top(p.x,p.y-5,0), bottom(p.x,p.y+5,0);
        triangle(starboard.panels,top,{p.x-5,p.y,0},bottom,colour);
        triangle(starboard.panels,top,bottom,{p.x+5,p.y,0},colour);
    }

    auto overheadDisplay = cabinDisplay(overheadScreen);
    auto& overhead = overheadDisplay.content;
    const auto lamp = [&](int row, std::string_view label, std::string_view value, glm::vec3 colour) {
        const double y = 39 + row*104;
        rect(overhead.panels,18,y,324,90,{.04,.11,.11});
        rect(overhead.panels,18,y,5,90,colour);
        atlasText(overhead,label,34,y+30,122,26,cabinLamp);
        terminalText(overhead,value,164,y+30,165,26,colour,true);
    };
    lamp(0,"Gear",instruments.gear ? "Down" : "Up",instruments.gear ? cabinSignal : terminalMuted);
    lamp(1,"Assist",instruments.assist ? "On" : "Off",instruments.assist ? cabinSignal : amber);
    const bool departing = instruments.docking.phase == DockingPhase::Departure;
    lamp(2,"Dock",instruments.docking.docked ? "Locked" : departing ? "Departing" :
         docking.phase==DockingPhase::ManualCapture ? "Arming" : instruments.docking.active ? "Approach" : "Free",
         instruments.docking.docked || docking.phase==DockingPhase::ManualCapture ? cabinSignal :
         instruments.docking.active || departing ? amber : terminalMuted);
    return {std::move(driveDisplay),std::move(portDisplay),std::move(starboardDisplay),std::move(overheadDisplay)};
}

CockpitOverlay cockpitOverlay(const FlightInstruments& t,double aspect,std::string_view mode,std::string_view notice,
                              std::span<const CelestialContact> contacts,std::span<const ActivityContact> activity,
                              bool help,double projectionScale,const MarketView* market,glm::dquat head,const CrewView* crew) {
    return cockpitOverlay(t,aspect,mode,notice,contacts,activity,help,projectionScale,market,head,crew,nullptr);
}
CockpitOverlay cockpitOverlay(const FlightInstruments& t, double aspect, std::string_view mode,
                              std::string_view notice, std::span<const CelestialContact> contacts,
                              std::span<const ActivityContact> activity, bool help, double projectionScale,
                              const MarketView* market, glm::dquat headInShip, const CrewView* crew,
                              const ExpeditionPlan* planner,const MicroView* micro,bool showFlightMarkers) {
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
        const std::array<std::pair<const char*,const char*>,27> rows{{
            {"F2","OPEN OR CLOSE THE WORMHOLE ATLAS"},
            {"ATLAS","TYPE TO SEARCH / TAB NEXT RESULT / CLICK STAR OR WORLD"},
            {"ENTER","FLY THE SELECTED WORMHOLE ROUTE OR LOCAL WORLD"},
            {"F3 / ATLAS SPACE","GALAXY-SYSTEM / PAUSE OR RESUME JOURNEY"},
            {"W A S D","FORWARD / LEFT / BACK / RIGHT THRUST"},
            {"Q / E","DOWN / UP THRUST"},
            {"ARROWS","PITCH AND YAW"},
            {"U / O  OR  V / Z","ROLL LEFT / RIGHT"},
            {"HOLD R","LEVEL THE WINGS IN MANUAL FLIGHT"},
            {"C","CAPTURE OR RELEASE MOUSE STEERING"},
            {"HOLD SHIFT + MOUSE","LOOK AROUND THE COCKPIT / RELEASE TO CENTRE"},
            {"F5 / E","LEAVE THE SEAT / SIT DOWN BESIDE THE HELM"},
            {"E AT CARGO TERMINAL","OPEN THE STATION MARKET WHILE DOCKED"},
            {"LEFT OPTION / CTRL","BOOST / PRECISE THRUST"},
            {"B / X","BRAKE AND CANCEL COURSE / TOGGLE CRUISE"},
            {"Y / J","NEXT PLANET / FLY TO SELECTED PLANET"},
            {"G / L","LANDING GEAR / AUTOMATIC LANDING"},
            {"F4 THEN M / D","MANUAL OR AUTOMATIC DOCKING AT THE SELECTED STATION"},
            {"CTRL-TAB","SHOW OR HIDE CONTACTS"},
            {"K / SHIFT-K / I","NEXT CREW / PREVIOUS CREW / HAIL"},
            {"F8","SHIP FIT / PIRACY / PATROL VOUCHERS / REPAIRS"},
            {"F9","CONVOYS / ESCORTS / PIRATE PRIZES / REPORTS"},
            {"F10","FLEET / CAPTAINS / INTELLIGENCE / CAMPAIGNS"},
            {"M / HOLD SPACE","ARM OR SAFE WEAPONS / FIRE IN MANUAL FLIGHT"},
            {"T / 1 2 3","NEXT SHIP TARGET / PULSE, ION, TORPEDO"},
            {"N / F7","SCAN / DISCOVERED DISPATCH LEADS"},
            {"F1 / ESC","CLOSE THIS HELP / RELEASE MOUSE OR EXIT"}
        }};
        double y=158;for(const auto& [key,action]:rows){
            fitted(h,key,40,y,230,12,ice);fitted(h,action,290,y,w-330,12,pearl);y+=26;
        }
        fitted(h,"PUBLIC CATALOGUE SNAPSHOT / INFINITY REAL-TIME FLIGHT",40,866,w-80,12,muted);
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
    const bool changingScale=t.shrinking||(!hybrid&&std::abs(t.scaleRate)>.02);
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
    const auto range=microActive?"FIELD "+flightQuantity(micro->fieldMetres):
        docking?flightQuantity(t.docking.range):t.targetVisible||t.navigating?flightQuantity(t.range):std::string("SELECT WITH Y / K");
    const auto eta=!docking&&!microActive&&t.etaSeconds>=0&&t.etaSeconds<10000?" / ETA "+std::to_string(int(std::ceil(t.etaSeconds)))+"S":"";
    quantity(range+eta,nav.x+14,nav.y+73,nav.w-28,12,ice);
    const auto action=hybrid&&!docking?(t.destination.starts_with("WORMHOLE TO ")?"W / FLY THROUGH THE RING":"J FLY TO WORLD / F2 ATLAS"):
                      manualDocking(t.docking.phase)?"CTRL FINE THRUST / R LEVEL":focus.nextAction;
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
    }else if(dockingDirector){const auto& d=t.docking;const bool manual=manualDocking(d.phase);
        const bool aligned=d.heading<glm::radians(4.)&&std::abs(d.roll)<glm::radians(6.);
        const double lateral=glm::length(glm::dvec2(d.offset));
        const double scale=manual?std::clamp(std::abs(d.offset.z)*.18,1.3,d.width):d.width;
        diamond(h.strokes,{radarX-std::clamp(d.offset.x/scale,-1.,1.)*radarRadius*.78,radarY+std::clamp(d.offset.y/scale,-1.,1.)*21},5,lateral<.85?mint:amber);
        const double angle=std::clamp(d.roll,-1.4,1.4);line(h.strokes,radarX-19*std::cos(angle),radarY+19*std::sin(angle),radarX+19*std::cos(angle),radarY-19*std::sin(angle),ice);
        if (manual && d.phase != DockingPhase::ManualCapture) {
            std::snprintf(value,sizeof(value),"AIM %.1f DEG / ROLL %.1f DEG",glm::degrees(d.heading),glm::degrees(d.roll));
            fitted(h,value,scope.x+14,scope.y+32,scope.w-28,11,aligned?mint:amber);
        } else {
            fitted(h,d.docked?"CLAMPS SECURE":dockingPhaseName(d.phase),scope.x+14,scope.y+32,scope.w-28,11,mint);
        }
        quantity(std::string(d.closing<-.005?"OPEN ":"CLOSE ")+flightQuantity(std::abs(d.closing),true)+
                 " / OFFSET "+flightQuantity(lateral),scope.x+14,scope.y+105,scope.w-28,9.5,ice);
        if(d.phase==DockingPhase::ManualCapture)
            bar(h.panels,scope.x+14,scope.y+96,scope.w-28,d.capture,mint);
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
    fitted(h,hybrid?(t.docking.docked?"ENTER UNDOCK":manualDocking(t.docking.phase)?"J AUTO DOCK / B ABORT":"B BRAKE / X CRUISE"):systemHint,
           systems.x+14,systems.y+108,systems.w-28,9.5,ice);
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
    const bool weaponUseful=focus.mode==CockpitMode::Combat||(docking&&!hybrid)||(focus.mode==CockpitMode::Surface&&t.explorationActive)||!t.stationStatus.empty()||!t.recoveryStatus.empty();
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
    if(hybrid && showFlightMarkers && !docking && !modal && !reconOpen) {
        const auto contact=std::find_if(activity.begin(),activity.end(),[](const auto& c) {
            return c.selected&&!c.occluded&&isXalariaTraffic(c.body.id);
        });
        if(contact!=activity.end()) {
            const auto marker=cockpitTargetMarker(contact->localDirection,aspect,projectionScale);
            const auto p=marker.point;
            const CockpitRect area{p.x-12.,p.y-12.,24.,24.};
            if(!aim.overlaps(area)&&std::none_of(h.hitRegions.begin(),h.hitRegions.end(),[&](const auto& panel) {
                return panel.overlaps(area);
            })) {
                diamond(h.strokes,p,marker.offscreen?5.:9.,mint);
                h.worldLabels.push_back(area);
            }
        }
    }
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
    const float targetOpacity=hybrid&&t.docking.active?float(smoothstep(1.5,18.,t.docking.range)):1.f;
    if (showFlightMarkers && t.targetVisible && targetOpacity>0.f &&
        !(t.targeting&&t.targeting->target&&t.targetLabel==t.targeting->name)) {
        const auto marker = cockpitTargetMarker(eyeFromShip * (t.warpDestination ? t.localWarpDestination : t.localTarget),
                                                 aspect, projectionScale);
        const auto p = marker.point;
        const bool footprint = t.landing.body && !t.landed;
        const auto tint = footprint ? mint : ice;
        if (marker.offscreen) {
            const auto d = marker.direction, across = glm::dvec2(-d.y, d.x);
            line(h.strokes, p.x, p.y, p.x - d.x * 12 + across.x * 6, p.y - d.y * 12 + across.y * 6, tint,targetOpacity);
            line(h.strokes, p.x, p.y, p.x - d.x * 12 - across.x * 6, p.y - d.y * 12 - across.y * 6, tint,targetOpacity);
        } else {
            for (int side : {-1, 1}) {
                line(h.strokes, p.x + side * 10, p.y - 12, p.x + side * 17, p.y - 12, tint,targetOpacity);
                line(h.strokes, p.x + side * 17, p.y - 12, p.x + side * 17, p.y + 12, tint,targetOpacity);
                line(h.strokes, p.x + side * 17, p.y + 12, p.x + side * 10, p.y + 12, tint,targetOpacity);
            }
        }
        if (!footprint && !hybrid) {
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
    if (!hybrid && catalogues && t.phase != "CAVERN EXPLORATION") {
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
void mapLine(CockpitOverlay& h,MapRect r,glm::dvec2 a,glm::dvec2 b,glm::vec3 color,bool arrow=false,double weight=1,bool dashed=false){
    const auto delta=b-a;double lo=0,hi=1;
    const auto clip=[&](double p,double q){
        if(std::abs(p)<1e-12)return q>=0;
        const double t=q/p;if(p<0)lo=std::max(lo,t);else hi=std::min(hi,t);return lo<=hi;
    };
    if(!clip(-delta.x,a.x-r.x)||!clip(delta.x,r.x+r.w-a.x)||!clip(-delta.y,a.y-r.y)||!clip(delta.y,r.y+r.h-a.y))return;
    b=a+delta*hi;
    a+=delta*lo;
    const auto length=glm::length(b-a);
    if (weight>1 && length>1e-6) {
        const auto side=glm::dvec2(a.y-b.y,b.x-a.x)*(weight*.5/length);
        const auto stroke=[&](glm::dvec2 from,glm::dvec2 to) {
            triangle(h.panels,{from-side,0},{from+side,0},{to+side,0},color);
            triangle(h.panels,{from-side,0},{to+side,0},{to-side,0},color);
        };
        if (dashed) {
            for (double at=0;at<length;at+=14)
                stroke(glm::mix(a,b,at/length),glm::mix(a,b,std::min(length,at+8)/length));
        } else {
            stroke(a,b);
        }
    } else {
        line(h.strokes,a.x,a.y,b.x,b.y,color);
    }
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
void liveMarker(CockpitOverlay& h,MapRect r,glm::dvec2 p){
    p=glm::clamp(p,glm::dvec2(r.x+9,r.y+9),glm::dvec2(r.x+r.w-9,r.y+r.h-9));
    const glm::dvec2 a{p.x,p.y-7},b{p.x-5,p.y+5},c{p.x+5,p.y+5};
    triangle(h.panels,{a,0},{b,0},{c,0},mint);
    mapCircle(h,r,p,12,mint*.6f);
}
const glm::vec3 atlasBg{.028,.041,.063}, chartBg{.014,.026,.043}, atlasSurface{.061,.088,.127},
    atlasBorder{.16,.21,.28}, atlasTextColour{.91,.94,.98}, atlasSecondary{.65,.72,.81},
    atlasShip{.57,.91,.78}, atlasSelection{1,.75,.41}, atlasRoute{.40,.77,1.};

struct AtlasPalette {
    std::string_view name;
    glm::vec3 colour, highlight;
};
constexpr std::array<AtlasPalette,4> atlasPalettes{{
    {"Charred Glass",{.96f,.30f,.15f},{1.f,.67f,.28f}},
    {"Verdant Fog",{.28f,.77f,.37f},{.72f,.94f,.33f}},
    {"Tidal Blue",{.22f,.57f,1.f},{.25f,.91f,.90f}},
    {"Acid Etch",{.94f,.28f,.62f},{1.f,.91f,.34f}}
}};
const AtlasPalette& atlasPalette(XalariaLook look) {
    return atlasPalettes.at(size_t(look));
}
void atlasPanel(CockpitOverlay& h, MapRect r, glm::vec3 fill) {
    rect(h.panels,r.x,r.y,r.w,r.h,fill);
}
void atlasGradient(CockpitOverlay& h, MapRect r, glm::vec3 top, glm::vec3 bottom) {
    auto& mesh=h.panels;
    const auto first=uint32_t(mesh.vertices.size());
    const auto left=float(r.x),right=float(r.x+r.w),y0=float(r.y),y1=float(r.y+r.h);
    mesh.vertices.insert(mesh.vertices.end(),{
        {left,y0,0,rgba(top)},{right,y0,0,rgba(top)},
        {left,y1,0,rgba(bottom)},{right,y1,0,rgba(bottom)}
    });
    mesh.indices.insert(mesh.indices.end(),{first,first+1,first+2,first+1,first+3,first+2});
}
void atlasDisc(CockpitOverlay& h, glm::dvec2 centre, double radius, glm::vec3 core,
               glm::vec3 rim, unsigned sides) {
    auto& mesh=h.panels;
    const auto first=uint32_t(mesh.vertices.size());
    mesh.vertices.push_back({float(centre.x),float(centre.y),0,rgba(core)});
    for (unsigned i=0;i<sides;++i) {
        const double angle=i*2*std::numbers::pi/sides;
        const auto point=centre+radius*glm::dvec2(std::cos(angle),std::sin(angle));
        mesh.vertices.push_back({float(point.x),float(point.y),0,rgba(rim)});
        mesh.indices.insert(mesh.indices.end(),{first,first+1+i,first+1+(i+1)%sides});
    }
}
glm::vec3 atlasWorldColour(const XalariaWorld& world, const AtlasPalette& palette) {
    if (world.life)
        return glm::mix(palette.highlight,atlasShip,.63f);
    if (world.primary>=0)
        return glm::mix(palette.colour,atlasSecondary,.48f);
    return world.composition==0 ? palette.highlight : glm::mix(palette.colour,atlasTextColour,.25f);
}
void atlasButton(CockpitOverlay& h, MapRect r, std::string_view name,
                 bool selected=false, bool enabled=true, bool primary=false) {
    const auto fill=primary && enabled ? atlasSelection : selected ? glm::vec3(.14,.22,.31) : atlasSurface;
    atlasGradient(h,r,fill*1.10f,fill*.79f);
    const auto colour = !enabled ? atlasSecondary*.68f : primary ? atlasBg : atlasTextColour;
    const double textWidth = atlasTextWidth(name,17);
    atlasText(h,name,r.x+std::max(9.,(r.w-textWidth)/2),r.y+(r.h-17)/2,r.w-18,17,colour);
    if (selected && !primary)
        atlasPanel(h,{r.x+1,r.y+r.h-2,r.w-2,2},atlasRoute);
}
void atlasLabel(CockpitOverlay& h, MapRect chart, std::string_view name, glm::dvec2 p,
                glm::vec3 colour, double size, bool key=false) {
    if (!chart.contains(p))
        return;
    const double w = atlasTextWidth(name,size);
    const std::array<glm::dvec2,4> positions{{{15,-size-5},{15,8},{-w-15,-size-5},{-w-15,8}}};
    for (const auto offset : positions) {
        const auto at = p+offset;
        const CockpitRect box{at.x-6,at.y-4,w+12,size+8};
        if (!chart.contains({box.x,box.y}) || !chart.contains({box.x+box.w,box.y+box.h}))
            continue;
        if (std::any_of(h.textRuns.begin(),h.textRuns.end(),
                        [&](const auto& text){return text.bounds.overlaps(box);}))
            continue;
        if (key)
            atlasPanel(h,{box.x,box.y,box.w,box.h},chartBg);
        atlasText(h,name,at.x,at.y,w+1,size,colour);
        return;
    }
}
void atlasMarker(CockpitOverlay& h, MapRect r, glm::dvec2 p, glm::vec3 colour, bool ship=false) {
    if (!r.contains(p))
        return;
    const double radius=ship?8.:9.;
    const std::array<glm::dvec2,4> points{{p+glm::dvec2(0,-radius),p+glm::dvec2(radius,0),
                                        p+glm::dvec2(0,radius),p+glm::dvec2(-radius,0)}};
    if (ship) {
        triangle(h.panels,{points[0],0},{p+glm::dvec2(-7,6),0},{p+glm::dvec2(7,6),0},colour);
        return;
    }
    for (size_t i=0;i<points.size();++i)
        mapLine(h,r,points[i],points[(i+1)%points.size()],colour);
}
void atlasRouteLine(CockpitOverlay& h, const XalariaMapView& v, MapRect r,
                    uint32_t from, uint32_t to, double aspect, glm::vec3 colour, bool dashed) {
    const auto a=xalariaMapPoint(v,from,aspect), b=xalariaMapPoint(v,to,aspect);
    const auto draw=[&](glm::dvec2 start,glm::dvec2 end) {
        mapLine(h,r,start,end,colour,!dashed,dashed?2.:2.8,dashed);
    };
    if (v.span>=150) {
        draw(a,b);
        return;
    }
    const double grid=v.catalog->grid;
    glm::dvec2 delta{double(to%v.catalog->grid)-double(from%v.catalog->grid),
                     double(to/v.catalog->grid)-double(from/v.catalog->grid)};
    for(int axis=0;axis<2;++axis)
        delta[axis]-=std::round(delta[axis]/grid)*grid;
    const auto wrappedEnd=a+delta*(r.h/v.span);
    draw(a,wrappedEnd);
    if(glm::length(wrappedEnd-b)>1)
        draw(a+b-wrappedEnd,b);
}
void atlasRegions(CockpitOverlay& h, const XalariaMapView& v, MapRect chart, double aspect) {
    const auto& catalog=*v.catalog;
    const auto side=XalariaCatalog::regionSide;
    const auto count=catalog.grid/side;
    std::vector<bool> occupied(count*count);
    for (const auto& [id,star] : catalog.stars)
        occupied[id/catalog.grid/side*count+(id%catalog.grid)/side]=true;

    const double scale=chart.h/v.span, period=catalog.grid*scale, tile=side*scale;
    const bool wraps=v.span<150;
    const glm::dvec2 chartMin{chart.x,chart.y}, chartMax{chart.x+chart.w,chart.y+chart.h};
    const auto centre=(chartMin+chartMax)*.5;
    const auto clipMin=wraps?glm::max(chartMin,centre-period*.5):chartMin;
    const auto clipMax=wraps?glm::min(chartMax,centre+period*.5):chartMax;
    const auto selected=catalog.region(v.selected);
    for (unsigned y=0;y<count;++y) {
        for (unsigned x=0;x<count;++x) {
            if (!occupied[y*count+x])
                continue;
            const auto& palette=atlasPalette(catalog.region(y*side*catalog.grid+x*side).look);
            const bool focused=x==selected.x&&y==selected.y;
                const auto low=glm::mix(chartBg,palette.colour,focused?.25f:.143f);
                const auto high=glm::mix(low,palette.highlight,focused?.15f:.093f);
            const auto cell=xalariaMapPoint(v,{(x+.5)*side-.5,(y+.5)*side-.5},aspect);
            // Split a region at the wrapped projection's edge, just as its
            // stars are wrapped individually around the chart centre.
            const int copies=wraps?1:0;
            for (int row=-copies;row<=copies;++row) {
                for (int col=-copies;col<=copies;++col) {
                    const auto tileMin=cell+glm::dvec2(col,row)*period-tile*.5;
                    const auto lo=glm::max(clipMin,tileMin+.65);
                    const auto hi=glm::min(clipMax,tileMin+tile-.65);
                    if (hi.x<=lo.x||hi.y<=lo.y)
                        continue;
                    const auto sample=[&](double value) {
                        return glm::mix(high,low,float((value-tileMin.y)/tile));
                    };
                    atlasGradient(h,{lo.x,lo.y,hi.x-lo.x,hi.y-lo.y},sample(lo.y),sample(hi.y));
                }
            }
        }
    }
}
void galaxyChart(CockpitOverlay& h,const XalariaMapView& v,double aspect) {
    const auto r=XalariaMapLayout(aspect).chart;
    const auto& catalog=*v.catalog;
    atlasRegions(h,v,r,aspect);
    if (v.showLinks) {
        for (const auto& [id,star] : catalog.stars) {
            const auto start=xalariaMapPoint(v,id,aspect);
            for (auto link : star.wormholes) {
                const auto end=xalariaMapPoint(v,link,aspect);
                const auto delta=glm::abs(end-start);
                if (v.span>=150 || std::max(delta.x,delta.y)<=r.h*catalog.grid/v.span*.5)
                    mapLine(h,r,start,end,id==v.selected ? atlasSecondary*.55f : atlasBorder*.63f,
                            v.span<32 && id==v.selected);
            }
        }
    }
    const double size = v.span>120 ? 1.6 : v.span>50 ? 2.3 : 4.2;
    for (const auto& [id,star] : catalog.stars) {
        const auto p=xalariaMapPoint(v,id,aspect);
        if (!r.contains(p))
            continue;
        const auto& palette=atlasPalette(catalog.region(id).look);
        const float warmth=float(std::clamp((6000.-star.temperature)/5200.,0.,1.));
        const auto dot=glm::mix(palette.colour,palette.highlight,.15f+warmth*.25f);
        atlasPanel(h,{p.x-size/2,p.y-size/2,size,size},dot*(v.span>120?.66f:.83f));
    }
    const bool active=!v.activeRoute.empty();
    const auto next=active ? v.activeRoute[std::min(v.activeLeg+1,v.activeRoute.size()-1)] : v.selected;
    if (v.route!=v.activeRoute)
        for(size_t i=1;i<v.route.size();++i)
            atlasRouteLine(h,v,r,v.route[i-1],v.route[i],aspect,atlasSelection,true);
    for(size_t i=1;i<v.activeRoute.size();++i)
        atlasRouteLine(h,v,r,v.activeRoute[i-1],v.activeRoute[i],aspect,
                       i<=v.activeLeg?atlasSecondary*.55f:atlasRoute,false);

    const auto ship=xalariaMapPoint(v,v.ship,aspect);
    const auto selected=xalariaMapPoint(v,v.selected,aspect);
    if (v.selected!=v.current)
        atlasMarker(h,r,selected,atlasSelection);
    if (active && v.destination!=v.selected && v.destination!=v.current)
        atlasMarker(h,r,xalariaMapPoint(v,v.destination,aspect),atlasRoute);
    atlasMarker(h,r,ship,atlasShip,true);
    atlasLabel(h,r,"Ship / "+catalog.star(v.current).name,ship,atlasShip,17,true);
    if (v.selected!=v.current)
        atlasLabel(h,r,catalog.star(v.selected).name,selected,atlasSelection,19,true);
    if (active && v.destination!=v.selected && v.destination!=v.current)
        atlasLabel(h,r,catalog.star(v.destination).name,xalariaMapPoint(v,v.destination,aspect),atlasRoute,17,true);
    if (active && next!=v.destination && next!=v.selected && next!=v.current)
        atlasLabel(h,r,catalog.star(next).name,xalariaMapPoint(v,next,aspect),atlasRoute,16,true);
}
void systemChart(CockpitOverlay& h,const XalariaMapView& v,double aspect,const AtlasPalette& palette) {
    const auto r=XalariaMapLayout(aspect).chart;
    const auto& star=v.catalog->star(v.selected);
    const glm::dvec2 centre{r.x+r.w/2,r.y+r.h/2};
    const auto selected=v.selectedWorld;
    const auto arrival = v.destination==v.selected && !v.activeRoute.empty() ? v.activeWorld : std::nullopt;
    const auto orbitFocus = selected ? selected : arrival;
    const auto selectedPrimary = orbitFocus && star.worlds.at(*orbitFocus).primary>=0
        ? std::optional<uint64_t>(uint64_t(star.worlds.at(*orbitFocus).primary)) : orbitFocus;
    const auto sky=glm::mix(chartBg,palette.colour,.079f);
    atlasGradient(h,r,sky,chartBg);
    atlasDisc(h,centre,std::min(r.w,r.h)*.48,glm::mix(sky,palette.colour,.37f),sky,64);
    atlasDisc(h,centre,43,palette.highlight*.55f,glm::mix(sky,palette.colour,.22f),40);
    atlasDisc(h,centre,10,glm::mix(palette.highlight,atlasTextColour,.67f),palette.highlight,24);
    for (const auto& [id,world] : star.worlds) {
        if (!xalariaSystemShowsWorld(v,id))
            continue;
        const auto p=xalariaSystemPoint(v,id,aspect);
        const auto colour=atlasWorldColour(world,palette);
        if (world.primary<0)
            mapCircle(h,r,centre,glm::length(p-centre),
                      selectedPrimary==id ? atlasSelection*.55f : colour*.43f);
        else
            mapLine(h,r,xalariaSystemPoint(v,uint64_t(world.primary),aspect),p,colour*.44f);
        const double radius=world.primary<0?5.3:3.3;
        if (!r.contains(p-glm::dvec2(radius))||!r.contains(p+glm::dvec2(radius)))
            continue;
        atlasDisc(h,p,radius,glm::mix(colour,atlasTextColour,.28f),colour*.78f,14);
        if (id==selected || id==arrival)
            atlasMarker(h,r,p,id==selected?atlasSelection:atlasRoute);
    }
    if (v.current==v.selected) {
        const auto ship=xalariaSystemShipPoint(v,aspect);
        atlasMarker(h,r,ship,atlasShip,true);
        atlasLabel(h,r,"Ship",ship,atlasShip,17,true);
    }
    if (selected)
        atlasLabel(h,r,star.worlds.at(*selected).name,xalariaSystemPoint(v,*selected,aspect),atlasSelection,19,true);
    if (arrival && arrival!=selected)
        atlasLabel(h,r,star.worlds.at(*arrival).name,xalariaSystemPoint(v,*arrival,aspect),atlasRoute,17,true);
    for (const auto& [id,world] : star.worlds)
        if (world.primary<0 && id!=selected && id!=arrival)
            atlasLabel(h,r,world.name,xalariaSystemPoint(v,id,aspect),atlasTextColour,16);
}
void atlasLegend(CockpitOverlay& h,MapRect chart,bool galaxy,bool ship,bool active) {
    const double y=chart.y+chart.h+12;
    const auto show=[&](double x,std::string_view label,glm::vec3 colour) {
        atlasPanel(h,{x,y+5,8,8},colour);
        atlasText(h,label,x+15,y,116,15,atlasSecondary);
    };
    if (ship)
        show(chart.x,"Ship",atlasShip);
    const double selectionX=chart.x+(ship?76:0);
    show(selectionX,galaxy?"Selection / plan":"Selected world",atlasSelection);
    if (active)
        show(selectionX+151,galaxy?"Active course":"Arrival world",atlasRoute);
    if (!galaxy) {
        constexpr std::string_view note="Orbits not to scale";
        const double width=atlasTextWidth(note,15);
        atlasText(h,note,chart.x+chart.w-width,y,width+1,15,atlasSecondary);
    } else {
        double x=chart.x;
        for (const auto& palette : atlasPalettes) {
            const double width=atlasTextWidth(palette.name,14);
            atlasPanel(h,{x,y+23,8,8},palette.colour);
            atlasText(h,palette.name,x+14,y+20,width+1,14,atlasSecondary);
            x+=width+35;
        }
    }
}
}
CockpitOverlay xalariaMapOverlay(const XalariaMapView& v,double aspect) {
    CockpitOverlay h;
    const auto& c=*v.catalog;
    const XalariaMapLayout l(aspect);
    const auto r=l.chart;
    const bool galaxy=v.mode==XalariaMapView::Mode::Galaxy;
    const bool active=v.flying||v.paused;
    const auto& star=c.star(v.selected);
    const auto& palette=atlasPalette(c.region(v.selected).look);
    const auto selectedRow=glm::mix(atlasSurface,palette.colour,.19f);
    const double x=l.search.x,w=l.search.w;
    const auto add=[&](std::string_view value,double y,double size,glm::vec3 colour=atlasTextColour) {
        atlasText(h,value,x,y,w,size,colour);
    };

    atlasGradient(h,{0,0,l.width,900},atlasBg*1.24f,atlasBg*.67f);
    atlasPanel(h,r,chartBg);
    const auto details=glm::mix(atlasSurface,palette.colour,.079f);
    atlasGradient(h,l.details,details,atlasBg*.96f);
    atlasPanel(h,{l.details.x,l.details.y,2,l.details.h},palette.colour*.45f);
    atlasText(h,"Navigation",24,24,180,32,atlasTextColour);
    atlasText(h,"Ship in "+c.star(v.current).name,218,34,l.dispatch.x-236,19,atlasSecondary);
    atlasButton(h,l.dispatch,"Dispatch  F7");
    atlasButton(h,l.economy,"Trade");
    atlasButton(h,l.close,"Back to flight");
    atlasButton(h,l.galaxy,"Galaxy",galaxy);
    atlasButton(h,l.system,"System",!galaxy);
    atlasButton(h,galaxy?l.locate:l.links,"Locate");
    if (galaxy) {
        atlasButton(h,l.fitRoute,"Fit route",false,v.route.size()>1||v.activeRoute.size()>1);
        atlasButton(h,l.fitGalaxy,"Overview");
        atlasButton(h,l.links,"Links",v.showLinks);
        galaxyChart(h,v,aspect);
    } else {
        systemChart(h,v,aspect,palette);
    }
    atlasLegend(h,r,galaxy,galaxy||v.current==v.selected,galaxy?!v.activeRoute.empty():
                !v.activeRoute.empty()&&v.destination==v.selected);

    atlasPanel(h,l.search,v.searchFocused?glm::vec3(.10,.15,.21):atlasSurface);
    atlasText(h,v.query.empty()?"Search stars, types or worlds":v.query,x+12,l.search.y+12,
              w-24-(v.query.empty()?0:l.clearSearch.w),18,v.query.empty()?atlasSecondary:atlasTextColour);
    if (!v.query.empty()) {
        atlasButton(h,l.clearSearch,"Clear");
        add(v.matches.empty()?"No matching destinations":"Search results",177,19);
        const size_t first=(v.matchIndex/l.resultRows)*l.resultRows;
        const size_t end=std::min(v.matches.size(),first+l.resultRows);
        for (size_t i=first;i<end;++i) {
            const double y=l.results.y+(i-first)*l.resultStep;
            if(i==v.matchIndex)
                atlasPanel(h,{x,y,w,l.resultStep-4},selectedRow);
            const auto& result=c.star(v.matches[i]);
            const auto world=c.searchWorld(v.matches[i],v.query);
            std::string name=world?result.worlds.at(*world).name+" / "+result.name:result.name;
            if(!world&&result.surveyStar)name+=" / "+std::string(stellarKindName(result.surveyStar->kind));
            atlasText(h,name,x+10,y+10,w-20,19,
                      i==v.matchIndex?atlasSelection:atlasTextColour);
        }
        if (!v.matches.empty())
            add(std::to_string(v.matchIndex+1)+" of "+std::to_string(v.matches.size())+
                (v.matches.size()>1?"  /  Tab to cycle":""),l.results.y+(end-first)*l.resultStep+9,16,atlasSecondary);
    } else {
        add(star.name,181,30,atlasTextColour);
        atlasPanel(h,{x,229,8,8},palette.colour);
        const auto descriptor=star.surveyStar?stellarKindName(star.surveyStar->kind):palette.name;
        atlasText(h,std::string(descriptor)+"  /  "+std::to_string(star.worlds.size())+(star.worlds.size()==1?" world":" worlds"),
                  x+15,224,w-15,17,glm::mix(atlasTextColour,palette.colour,.37f));
        add(v.current==v.selected?"Current system":v.route.empty()?"No wormhole route":
            std::to_string(v.route.size()-1)+(v.route.size()==2?" wormhole hop":" wormhole hops"),251,17,
            v.route.empty()?atlasSecondary:atlasShip);
        if (galaxy) {
            add("Wormholes from the ship",299,20);
            const auto& nearby=c.star(v.current).wormholes;
            if(nearby.empty())
                add("No outbound links",337,17,atlasSecondary);
            const size_t count=std::min<size_t>(nearby.size(),l.worldRows);
            for(size_t i=0;i<count;++i) {
                const double y=l.nearby.y+i*l.worldStep;
                if(nearby[i]==v.selected)
                    atlasPanel(h,{x,y,w,l.worldStep-4},selectedRow);
                atlasPanel(h,{x+10,y+13,8,8},atlasPalette(c.region(nearby[i]).look).colour);
                atlasText(h,c.star(nearby[i]).name,x+26,y+8,w-36,18,
                          nearby[i]==v.selected?atlasSelection:atlasTextColour);
            }
            if(nearby.size()>count)
                add("Show Links to see all connections",673,16,atlasSecondary);
        } else {
            add("Planets and moons",282,19);
            size_t index=0,row=0;
            for (const auto& [id,world] : star.worlds) {
                if (index++<v.worldScroll)
                    continue;
                if (row==l.worldRows)
                    break;
                const double y=l.worlds.y+row++*l.worldStep;
                if(v.selectedWorld==id)
                    atlasPanel(h,{x,y,w,l.worldStep-4},selectedRow);
                const double indent=world.primary>=0?36.:24.;
                atlasDisc(h,{x+indent-13,y+17},world.primary>=0?2.8:3.8,
                          atlasWorldColour(world,palette),atlasWorldColour(world,palette),12);
                atlasText(h,world.name,x+indent,y+8,w-indent-70,18,
                          v.selectedWorld==id?atlasSelection:atlasTextColour);
                atlasText(h,world.primary>=0?"Moon":world.composition==0?"Gas":"Solid",
                          x+w-57,y+10,54,14,atlasSecondary);
            }
            if(star.worlds.size()>l.worldRows)
                add(std::to_string(v.worldScroll+1)+"-"+
                    std::to_string(std::min(star.worlds.size(),size_t(v.worldScroll+l.worldRows)))+
                    " of "+std::to_string(star.worlds.size())+"  /  Scroll for more",627,15,atlasSecondary);
            if (v.selectedWorld) {
                const auto& world=star.worlds.at(*v.selectedWorld);
                add(world.name,667,22,atlasSelection);
                add(std::to_string(int(world.temperature))+" K  /  Radius "+flightQuantity(world.radius),696,16,atlasSecondary);
                add(std::to_string(world.stations)+(world.stations==1?" station":" stations")+
                    (world.life?"  /  Life detected":""),721,16,atlasSecondary);
            }
        }
    }

    const bool committed=!v.activeRoute.empty();
    const auto& destinationStar=c.star(committed?v.destination:v.selected);
    const auto world=committed?v.activeWorld:v.selectedWorld;
    const auto destination=world?destinationStar.worlds.at(*world).name:destinationStar.name;
    const std::string state=v.paused?"Paused":v.arrived?"Arrived":v.finalApproach?"Final approach":
                            v.flying?"En route":v.route.size()>1?"Planned course":"Selected";
    atlasPanel(h,{24,771,l.width-48,1},atlasBorder);
    atlasText(h,state+" / "+destination,24,781,l.width-48,23,v.paused?atlasSelection:active?atlasRoute:atlasTextColour);
    std::string status;
    if (committed) {
        const auto hops=v.activeRoute.size()-1;
        status=(hops?"Leg "+std::to_string(std::min(v.activeLeg+1,hops))+" of "+std::to_string(hops):"Local flight")+
               "  /  "+flightQuantity(v.range)+"  /  "+std::to_string(int(v.progress*100))+"%";
        if(v.eta>=0 && v.eta<86400)
            status+="  /  ETA "+std::to_string(int(std::ceil(v.eta)))+" s";
    } else if (!v.message.empty() && v.message!="SELECT A WORLD OR PLOT A WORMHOLE ROUTE") {
        status=v.message;
    } else if(v.route.empty()) {
        status="No wormhole route from the ship";
    } else if(v.current==v.selected && !v.selectedWorld) {
        status="Choose a world in System to set a course";
    }
    atlasText(h,status,24,812,l.width-48,16,atlasSecondary);
    const bool canFly=!v.route.empty()&&(v.query.empty()||!v.matches.empty())&&(v.current!=v.selected||v.selectedWorld);
    atlasButton(h,l.fly,v.current==v.selected?"Fly to world":"Fly route",false,canFly,!active);
    if (active) {
        atlasButton(h,l.pause,v.paused?"Resume":"Pause",false,true,v.paused);
        atlasButton(h,l.cancel,"Cancel");
    }
    if(v.wormhole&&v.wormhole->destination==v.selected)
        atlasButton(h,l.wormhole,"Target wormhole");
    atlasText(h,galaxy?"Drag to pan  /  Scroll to zoom":"Click a world to select it",
              l.width-260,854,236,15,atlasSecondary);
    h.hitRegions.push_back({0,0,l.width,900});
    return h;
}
XalariaStoryLayout::XalariaStoryLayout(double aspect):width(900*aspect) {
    const double split=width*.38;
    const double tab=(width-48)/6;
    nearby={24,109,tab-8,40};
    news={24+tab,109,tab-8,40};
    feed={24+tab*2,109,tab-8,40};
    followUps={24+tab*3,109,tab-8,40};
    journal={24+tab*4,109,tab-8,40};
    archive={24+tab*5,109,tab-8,40};
    close={width-161,24,137,42};
    list={24,173,split-36,6*rowHeight};
    detail={split,173,width-split-24,624};
    const double x=detail.x+22,w=detail.w-44,half=(w-10)*.5;
    track={x,694,half,40};
    route={x+half+10,694,half,40};
    primary={x,746,half,40};
    secondary={x+half+10,746,half,40};
    previous={width-233,826,96,38};
    next={width-120,826,96,38};
}

std::optional<size_t> XalariaStoryLayout::pickRow(glm::dvec2 point,size_t count) const {
    if(!list.contains(point))return {};
    const auto index=size_t((point.y-list.y)/rowHeight);
    return index<count?std::optional{index}:std::nullopt;
}

XalariaCombatLayout::XalariaCombatLayout(double aspect):width(900*aspect){
    close={width-161,24,137,42};
    const double third=(width-64)/3,half=(width-58)/2,quarter=(width-72)/4;
    civilian={24,125,third,44};corsair={32+third,125,third,44};gunship={40+2*third,125,third,44};
    arm={24,464,quarter,44};pulse={32+quarter,464,quarter,44};ion={40+2*quarter,464,quarter,44};torpedo={48+3*quarter,464,quarter,44};
    demand={24,535,half,50};seize={34+half,535,half,50};
    claim={24,602,half,50};service={34+half,602,half,50};board={24,674,width-48,46};
}
CockpitOverlay xalariaCombatOverlay(const XalariaCombat& combat,const XalariaEconomy& economy,double aspect){
    CockpitOverlay hud;const XalariaCombatLayout l(aspect);
    atlasGradient(hud,{0,0,l.width,900},atlasBg*1.24f,atlasBg*.67f);
    atlasText(hud,"Armament / Ship fit",24,24,l.close.x-48,32,atlasTextColour);atlasButton(hud,l.close,"Back / F8");
    atlasText(hud,"Refit at a station. Your hold, trading, exploration and investigations remain available.",24,80,l.width-48,17,atlasSecondary);
    atlasButton(hud,l.civilian,"Civilian / 1000 GC",combat.fit()==CombatFit::Civilian);
    atlasButton(hud,l.corsair,"Corsair / 3500 GC",combat.fit()==CombatFit::Corsair);
    atlasButton(hud,l.gunship,"Gunship / 9000 GC",combat.fit()==CombatFit::Gunship);
    const double third=(l.width-64)/3;
    for(unsigned i=0;i<3;++i){
        const double x=24+i*(third+8),w=third;
        constexpr std::array first{"Light defensive pulse","Twin pulse II / ion projector","Triple pulse III / ion projector"};
        constexpr std::array second{"28-point rechargeable shield","48 shield / reinforced armour","80 shield / heavy armour"};
        constexpr std::array thirdLine{"Low operating cost","4 torpedoes / boarding work","10 torpedoes / patrol combat"};
        atlasText(hud,first[i],x,189,w,19,atlasTextColour);
        atlasText(hud,second[i],x,221,w,17,atlasSecondary);
        atlasText(hud,thirdLine[i],x,253,w,17,atlasSecondary);
    }
    const auto target=combat.selected();
    atlasText(hud,target?target->body.name:"T selects a visible ship / K selects a crew",24,315,l.width-48,24,atlasTextColour);
    if(target){
        atlasText(hud,target->body.status,24,353,l.width-48,18,target->body.hostile?glm::vec3(1,.43,.29):atlasSecondary);
        atlasText(hud,"Hull "+std::to_string(int(std::ceil(target->body.hull)))+"% / Shield "+
                  std::to_string(int(std::round(target->shield.fraction*100.)))+"%",24,385,l.width-48,18,atlasSecondary);
    }
    atlasText(hud,"Wallet "+xMoney(economy.credits())+" / Local warrant "+xMoney(combat.localWarrant())+
              " / Patrol vouchers "+xMoney(combat.bountyDue()),24,419,l.width-48,17,atlasSecondary);
    atlasButton(hud,l.arm,combat.armed?"M  Weapons armed":"M  Weapons safe",combat.armed);
    atlasButton(hud,l.pulse,"1  Pulse",combat.weapon==CombatWeapon::Pulse);
    atlasButton(hud,l.ion,"2  Ion",combat.weapon==CombatWeapon::Ion);
    atlasButton(hud,l.torpedo,"3  Torpedo",combat.weapon==CombatWeapon::Torpedo);
    atlasButton(hud,l.demand,"Demand cargo",false,bool(target));
    atlasButton(hud,l.seize,"Seize cargo / must be aboard",false,bool(target));
    atlasButton(hud,l.claim,combat.localWarrant()?"Settle local warrant":"Redeem patrol vouchers");
    atlasButton(hud,l.service,combat.playerHull()<=0?"Recover ship / cargo forfeited":"Repair hull / reload torpedoes");
    atlasButton(hud,l.board,"Request manual boarding approach",false,bool(target));
    atlasText(hud,"Hold Space to fire. Ion disables engines; leave the hull intact if you want to board.",24,752,l.width-48,18,atlasTextColour);
    atlasText(hud,"Stolen cargo needs an Independent or Rebel fence. Piracy calls in local security.",24,784,l.width-48,17,atlasSecondary);
    atlasText(hud,combat.message,24,838,l.width-48,18,{1.,.76,.43});
    hud.hitRegions.push_back({0,0,l.width,900});return hud;
}
void xalariaCombatHud(CockpitOverlay& hud,const XalariaCombat& combat,double aspect,const ActivityContact* contact,double projectionScale){
    if(!combat.armed&&!combat.target&&!combat.localWarrant())return;
    const double width=900*aspect,w=std::max(180.,std::min(440.,width-684.)),x=(width-w)*.5;
    const CockpitRect panel{x,130,w,137};occlude(hud,panel);atlasPanel(hud,{x,130,w,137},atlasSurface);
    constexpr std::array weapons{"Pulse battery","Ion disruptor","Torpedo"};
    const auto tint=combat.armed?glm::vec3(1.,.50,.28):atlasSecondary;
    atlasText(hud,std::string(combat.armed?"ARMED / ":"SAFE / ")+weapons[unsigned(combat.weapon)],x+12,143,w-24,16,tint);
    const auto target=combat.selected();
    auto targetLabel=target?target->body.name:std::string("T Target / F8 Armament");
    if(contact)targetLabel+=" / "+std::to_string(int(std::round(contact->distance)))+" m";
    atlasText(hud,targetLabel,x+12,172,w-24,17,atlasTextColour);
    if(contact&&!contact->occluded){
        const auto marker=cockpitTargetMarker(contact->localDirection,aspect,projectionScale);
        const auto p=marker.point;const double r=marker.offscreen?9.:18.;
        const CockpitRect bounds{p.x-r,p.y-r,r*2,r*2};
        if(std::none_of(hud.hitRegions.begin(),hud.hitRegions.end(),[&](const auto& panel){return panel.overlaps(bounds);})) {
            const auto colour=contact->body.hostile?glm::vec3(1.,.30,.19):glm::vec3(1.,.72,.32);
            for(double side:{-1.,1.})for(double vertical:{-1.,1.}){
                line(hud.strokes,p.x+side*r,p.y+vertical*r,p.x+side*(r-6),p.y+vertical*r,colour);
                line(hud.strokes,p.x+side*r,p.y+vertical*r,p.x+side*r,p.y+vertical*(r-6),colour);
            }
        }
    }
    atlasText(hud,target?"Hull "+std::to_string(int(std::ceil(target->body.hull)))+"% / Shield "+
              std::to_string(int(std::round(target->shield.fraction*100.)))+"%":"M Arm / Space Fire",x+12,199,w-24,14,atlasSecondary);
    atlasText(hud,"Heat "+std::to_string(int(combat.heat()))+"% / Torps "+std::to_string(combat.torpedoes()),x+12,224,w-24,14,tint);
    if(combat.localWarrant())atlasText(hud,"Wanted / "+xMoney(combat.localWarrant()),x+12,246,w-24,13,{1.,.42,.29});
    else if(combat.incomingAt>=0&&combat.convoys().seconds()-combat.incomingAt<5.)atlasText(hud,"Incoming / "+combat.incomingFire,x+12,246,w-24,13,{1.,.42,.29});
}
CabinDisplay xalariaPrizeDisplay(const StationLayout& layout,const XalariaTrafficContact& ship,const XalariaCombat& combat){
    auto display=deckDisplay(stationFreightScreen(layout),"Prize cargo",{1.,.63,.25});auto& screen=display.content;
    const auto units=combat.prizeCargo(ship.body.id);
    terminalText(screen,ship.body.name,18,55,324,21,pearl);
    rect(screen.panels,18,88,324,1,terminalRule);
    terminalText(screen,units?"Cargo under your control":"Cargo transferred",18,103,324,23,cargoLight);
    const auto key=combat.convoys().identity(ship.body.id,combat.convoys().seconds());
    const auto good=key?combat.convoys().plan(*key).good:XGood(ship.body.seed%7);
    terminalText(screen,std::string(xGoodInfo(good).name),18,139,324,21,pearl);
    terminalText(screen,units?std::to_string(units)+" units / E to seize aboard":"Hold transfer complete",18,174,324,17,terminalMuted);
    return display;
}

CockpitOverlay xalariaStoryOverlay(const XalariaStories& stories,const StoryContext& context,double seconds,
                                  double aspect,std::string_view routeAction) {
    constexpr std::array colours{glm::vec3{1.,.46,.40},glm::vec3{1.,.72,.34},glm::vec3{.91,.45,.63},
        glm::vec3{.49,.73,1.},glm::vec3{.74,.60,1.},glm::vec3{.36,.88,.80}};
    CockpitOverlay hud;
    const XalariaStoryLayout l(aspect);
    atlasGradient(hud,{0,0,l.width,900},atlasBg*1.24f,atlasBg*.67f);
    atlasText(hud,"Dispatch",24,24,226,35,atlasTextColour);
    const auto page=stories.page();
    if(page==StoryPage::Feed||page==StoryPage::Archive||page==StoryPage::Nearby||page==StoryPage::News)
        atlasText(hud,stories.catalog().star(stories.system).name,257,36,l.close.x-274,19,atlasSecondary);
    atlasButton(hud,l.close,"Back  Esc");
    atlasButton(hud,l.nearby,"Nearby",page==StoryPage::Nearby);
    atlasButton(hud,l.news,"News",page==StoryPage::News);
    atlasButton(hud,l.feed,"Local leads",page==StoryPage::Feed);
    const auto followLabel=stories.followUpCount()?"Follow-ups  "+std::to_string(stories.followUpCount()):"Follow-ups";
    atlasButton(hud,l.followUps,followLabel,page==StoryPage::FollowUps);
    atlasButton(hud,l.journal,"Journal",page==StoryPage::Journal);
    atlasButton(hud,l.archive,"Discoveries  "+std::to_string(stories.discoveredCount()),
                page==StoryPage::Archive);
    const auto& entries=stories.entries();
    if(page==StoryPage::Nearby){
        atlasText(hud,std::to_string(stories.nearbyCount())+" discovered leads within three jumps",
                  24,76,l.width-48,17,atlasSecondary);
    }else if(page==StoryPage::News){
        atlasText(hud,"Updates on incidents you have discovered. Reports take time to reach you.",
                  24,76,l.width-48,17,atlasSecondary);
    }
    for(size_t i=0;i<entries.size();++i){
        const auto& story=entries[i];
        const auto tint=colours[size_t(story.key.kind)];
        const double y=l.list.y+i*l.rowHeight;
        atlasPanel(hud,{l.list.x,y,l.list.w,l.rowHeight-6},
                   i==stories.row?glm::mix(atlasSurface,tint,.13f):atlasSurface);
        atlasPanel(hud,{l.list.x,y,3,l.rowHeight-6},tint*(i==stories.row?.92f:.46f));
        const double x=l.list.x+14,w=l.list.w-28;
        auto label=std::string(storyKindName(story.key.kind));
        if(story.key.chapter)label+="  /  Chapter "+std::to_string(story.key.chapter+1);
        else if(story.key.live&&stories.incident(story.key,seconds).urgent)label+="  /  Urgent";
        atlasText(hud,label,x,y+11,w,16,tint);
        atlasText(hud,story.headline,x,y+34,w,21,atlasTextColour);
        auto status=stories.result(story.key)?"Reported":stories.tracked()==story.key?"Tracked":
            story.key.chapter?stories.catalog().star(story.system).name+"  /  "+story.place:story.place;
        if(story.key.live&&!story.key.chapter)
            status=stories.catalog().star(story.system).name+" / "+std::to_string(stories.hops(story.key))+
                   (stories.hops(story.key)==1?" jump":" jumps");
        atlasText(hud,status,x,y+65,w,16,atlasSecondary);
    }
    const auto* story=stories.selected();
    if(story){
        const auto tint=colours[size_t(story->key.kind)];
        atlasGradient(hud,l.detail,glm::mix(atlasSurface,tint,.07f),atlasBg);
        atlasPanel(hud,{l.detail.x,l.detail.y,2,l.detail.h},tint*.56f);
        const auto stage=stories.stage(story->key,seconds);
        const auto result=stories.result(story->key);
        const bool tracked=stories.tracked()==story->key;
        const bool available=stories.actionable(story->key);
        const bool ready=tracked&&available&&stories.ready(*story,context);
        const bool living=story->key.live&&!story->key.chapter;
        const auto* investigation=stories.investigation(story->key);
        const bool caseFile=investigation&&stories.evidenceOpen;
        const double reportSeconds=seconds-(page==StoryPage::News?stories.hops(story->key)*120.:0.);
        const double x=l.detail.x+22,w=l.detail.w-44;
        const auto block=[&](std::string_view value,double y,double size,double lineHeight,
                             unsigned lines,glm::vec3 colour){
            for(unsigned line=0;line<lines&&!value.empty();++line){
                size_t end=value.size();
                while(line+1<lines&&atlasTextWidth(value.substr(0,end),size)>w){
                    const auto space=value.rfind(' ',end-1);
                    if(space==value.npos||space==0)break;
                    end=space;
                }
                atlasText(hud,value.substr(0,end),x,y+line*lineHeight,w,size,colour);
                value.remove_prefix(std::min(value.size(),end+1));
            }
        };
        auto heading=std::string(storyKindName(story->key.kind))+"  /  ";
        if(story->key.chapter)heading+="Chapter "+std::to_string(story->key.chapter+1)+" of "+
                                     std::to_string(storyChapterCount)+(result?"  /  Filed":"");
        else if(result)heading+="Filed";
        else if(living)heading+=incidentPhaseName(stories.incident(story->key,reportSeconds).phase);
        else heading+=storyStageName(stage);
        atlasText(hud,heading,x,192,w,17,tint);
        block(story->headline,224,30,35,2,atlasTextColour);
        atlasText(hud,stories.catalog().star(story->system).name+"  /  "+story->place,
                  x,303,w,18,atlasSecondary);
        if(const auto* found=stories.discovery(story->key))
            atlasText(hud,"Found through / "+std::string(storyOriginName(story->key.chapter?StoryOrigin::FollowUp:found->origin)),
                      x,326,w,13,atlasSecondary);
        if(caseFile){
            constexpr std::array labels{"Signal bearing","Physical trace","Recovered recorder","Independent account"};
            const auto notes=stories.evidenceNotes(story->key);
            for(unsigned step=0;step<4;++step){
                const double y=350.+step*62.;
                const bool collected=investigation->evidence&(1u<<step);
                atlasText(hud,std::string(collected?"RECORDED / ":"PENDING / ")+labels[step],x,y,w,16,
                          collected?tint:atlasSecondary);
                block(notes[step],y+21.,16,19,2,collected?atlasTextColour:atlasSecondary);
            }
        }else{
            block(story->report,341,20,25,5,atlasTextColour);
            atlasText(hud,result?"Outcome":investigation&&!story->key.chapter?"Investigation":
                      stage==StoryStage::FirstReport?"Assignment":"Latest report",x,476,w,17,tint);
        }
        const std::string update=result?story->outcomes[*result]:living?
            stories.incidentReport(story->key,reportSeconds):
            stage==StoryStage::FollowUp?story->followUp:story->development;
        if(!caseFile)block(investigation&&!story->key.chapter&&!result?
            stories.investigationTask(story->key):update,505,19,24,4,atlasTextColour);
        const std::string instruction=result?
            (stories.continuation(story->key)?"The next chapter is in Follow-ups or Journal":"Case closed"):!available?
            "Response window closed":!stories.evidenceComplete(story->key)&&!story->key.chapter?
            "B  Case file / N  Scan or take statement":ready?"Choose how to file the report":
            story->site==StorySite::Vessel?"Resolve aboard the freighter beyond its airlock":
            story->site==StorySite::Station?"Resolve inside the target station":
            "Resolve from the target orbit";
        if(living){
            const auto remaining=stories.incident(story->key,seconds).endsAt-seconds;
            const auto delay=stories.hops(story->key)*2;
            const auto timing=page==StoryPage::News?
                (delay?"Report delayed by "+std::to_string(delay)+" minutes":"Local dispatch report"):
                remaining>0.?"Response window / "+std::to_string(int(std::ceil(remaining/60.)))+" min remaining":
                "Response window ended";
            atlasText(hud,timing,x,614,w,16,atlasSecondary);
        }
        atlasText(hud,instruction,x,646,w,17,ready?tint:atlasSecondary);
        if(!routeAction.empty())atlasButton(hud,l.route,routeAction);
        if(!result){
            atlasButton(hud,l.track,tracked?"Tracked":"T  Track lead",tracked,available);
            atlasButton(hud,l.primary,"1  "+story->decisions[0],false,ready);
            atlasButton(hud,l.secondary,"2  "+story->decisions[1],false,ready);
        }
    }else{
        atlasPanel(hud,l.detail,atlasSurface);
        atlasText(hud,page==StoryPage::News?"No reports on your discoveries yet.":
                  page==StoryPage::FollowUps?"Follow-ups must be earned.":"No discovered leads here yet.",
                  l.detail.x+22,213,l.detail.w-44,21,atlasSecondary);
        const double x=l.detail.x+22,w=l.detail.w-44;
        atlasText(hud,"Scan worlds with N from the helm.",x,265,w,19,atlasTextColour);
        atlasText(hud,"Select crews with K, then hail with I.",x,300,w,19,atlasTextColour);
        atlasText(hud,"Walk into stations to meet local contacts.",x,335,w,19,atlasTextColour);
        atlasText(hud,"Complete work to earn more private leads.",x,390,w,19,atlasSecondary);
    }
    const bool paged=stories.sheetCount()>1;
    atlasText(hud,stories.message.empty()?(page==StoryPage::Archive?
              "Your discoveries / Page Up or Down / B Case file":"Up / Down   Tab Feeds   B Case file"):stories.message,
              24,836,l.width-(paged?390:48),19,atlasSecondary);
    if(paged){
        atlasText(hud,std::to_string(stories.sheet()+1)+" / "+std::to_string(stories.sheetCount()),
                  l.previous.x-110,838,100,16,atlasSecondary);
        atlasButton(hud,l.previous,"Previous",false,stories.sheet()>0);
        atlasButton(hud,l.next,"Next",false,stories.sheet()+1<stories.sheetCount());
    }
    hud.hitRegions.push_back({0,0,l.width,900});
    return hud;
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
    fitted(h,c.star(v.current).surveyStar?"F6 STAR SCOPE / F7 DISPATCH":"F5 WALK SHIP / F7 DISPATCH",x,box.y+165,w,9,ice);
    h.hitRegions.push_back(box);
}

XEconomyLayout::XEconomyLayout(double aspect,bool cargoTerminal):width(900*aspect){
 const double split=width*.59;
 if(cargoTerminal){
  const double tabWidth=(width-56)*.5;
  tabs={};
  tabs.front()={24,142,tabWidth,38};
  tabs.back()={32+tabWidth,142,tabWidth,38};
 }else for(size_t i=0;i<tabs.size();++i)tabs[i]={24+(width-48)/5*i,142,(width-48)/5-8,38};
 rows={24,290,split-48,384};details={split,290,width-split-24,384};
 buy={24,700,(rows.w-12)/2,38};sell={buy.x+buy.w+12,700,buy.w,38};
 minus={24,751,44,36};plus={190,751,44,36};
 travel={split,222,details.w,42};manualDock={split+(details.w+10)*.5,222,(details.w-10)*.5,42};
 venue={24,222,rows.w,42};
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
 CockpitOverlay h;const auto& v=e.view;const XEconomyLayout l(aspect,v.cargoTerminal);
 const auto accent=v.cargoTerminal?glm::vec3(1.,.75,.41):mint;
 rect(h.panels,8,8,l.width-16,884,ink,.99f);
 fitted(h,v.cargoTerminal?"SHIP INTERIOR / CARGO BAY":"INFINITY / XALARIA",24,28,l.width-48,13,ice);
 fitted(h,v.cargoTerminal?"CARGO TERMINAL":"TRADE, INDUSTRY AND CULTURE",24,58,l.width-48,21,pearl);
 if(v.cargoTerminal)rect(h.panels,24,88,l.width-48,1,accent*.6f);
 fitted(h,xMoney(e.credits())+" / HOLD "+std::to_string(e.cargoUnits())+"/"+std::to_string(e.holdCapacity())+
  " / FUEL "+std::to_string(int(e.fuel()))+"/"+std::to_string(int(e.fuelCapacity()))+
  " / ENERGY "+std::to_string(int(e.energy()*5))+"%",24,101,l.width-48,12,accent);
 constexpr std::array<std::string_view,5> tabs{"1 MARKET","2 HOLDINGS","3 MINING","4 SERVICES","5 CHARTERS"};
 for(size_t i=0;i<tabs.size();++i)if(l.tabs[i].w>0)mapButton(h,l.tabs[i],tabs[i],size_t(v.page)==i);
 const bool local=e.canTrade(v.place);
 fitted(h,e.placeName(v.place),24,197,l.width-48,13,ice);
 fitted(h,xFactionName(e.faction(v.place)),l.details.x,276,l.details.w,9,muted);
 if(v.cargoTerminal){
  fitted(h,local?"STATION LINK / CLAMPS SECURE":"STATION LINK LOST",34,237,l.venue.w-20,12,local?mint:amber);
  mapButton(h,l.travel,"CLOSE TERMINAL / ESC");
 }else{
  const auto list=e.venues(v.place.system,v.place.world);
  mapButton(h,l.venue,list.size()>1?"CHANGE STATION / CITY":"LOCAL STATION / CITY");
  if(!local&&!v.place.city&&!list.empty()){
   auto automatic=l.travel;automatic.w=l.manualDock.w;
   mapButton(h,automatic,"AUTO DOCK / D",true);
   mapButton(h,l.manualDock,"MANUAL DOCK / M",false,
             e.currentSystem()==v.place.system&&e.currentWorld()==v.place.world&&e.nearOrbit());
  }else mapButton(h,l.travel,local?"UNDOCK / LAUNCH":"TRAVEL AND LAND",!local);
 }
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
  mapButton(h,l.buy,"BUY CARGO / B",true,local&&buy.available&&
            (!v.cargoTerminal||(buy.total<=e.credits()&&v.quantity<=int64_t(e.holdCapacity())-e.cargoUnits())));
  mapButton(h,l.sell,"SELL CARGO / S",false,local&&sell.available&&
            (!v.cargoTerminal||v.quantity<=e.cargo()[v.good]));
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
  const auto offer=e.offer(v.place);
  const bool claimed=e.charterClaimed(offer.id);
  row(!offer.units?"NO CHARTER AVAILABLE":claimed?"TODAY'S CHARTER CLAIMED":
      "TODAY'S CHARTER / "+xMoney(offer.reward),0,v.good==0);
  for(size_t i=0;i<e.contracts().size()&&i<10;++i){const auto& c=e.contracts()[e.contracts().size()-1-i];
   row((c.state==XContract::State::Active?"IN HOLD":c.state==XContract::State::Delivered?"DELIVERED":"EXPIRED")+std::string(" / ")+xMoney(c.reward),i+1,v.good==i+1);
  }
  const XContract* c=&offer;
  if(v.good>0&&v.good<=e.contracts().size())c=&e.contracts()[e.contracts().size()-v.good];
  detail(c->units?e.placeName(c->destination):"NO DELIVERY CHARTER TODAY",0,mint);
  detail("SEALED CARGO "+std::to_string(c->units)+" UNITS",2);detail("REWARD "+xMoney(c->reward),4);
  detail("DEADLINE: DAY "+std::to_string(c->deadline),5);
  detail("DOCK AT THE DESTINATION TO DELIVER",7,muted);detail("PARCELS USE YOUR SHIP'S CARGO HOLD",9,muted);
  mapButton(h,l.action,"ACCEPT TODAY'S CHARTER / ENTER",false,local&&offer.units&&!claimed);
  mapButton(h,l.secondary,"DELIVER PARCELS");mapButton(h,l.tertiary,"PLOT DELIVERY DESTINATION");
 }
 if(v.page==XEconomyView::Page::Market){
  mapButton(h,l.minus,"-");mapButton(h,l.plus,"+");fitted(h,std::to_string(v.quantity)+" UNITS",79,762,103,12,pearl);
 }
 fitted(h,v.message,24,853,l.width-48,11,amber);
 fitted(h,std::string(v.cargoTerminal?"ESC CLOSE / ARROWS SELECT / BRACKETS SET QUANTITY / DAY ":
                      "F4 CLOSE / F2 ATLAS / ARROWS SELECT / ENTER ACT / D TRAVEL / DAY ")+std::to_string(e.day())+
  " / ECONOMY TIME 60X",24,878,l.width-48,9,muted);
 h.hitRegions.push_back({8,8,l.width-16,884});return h;
}

} // namespace infinity

namespace infinity {
XalariaConvoyLayout::XalariaConvoyLayout(double aspect):width(900*aspect){
 close={width-161,24,137,42};list={24,112,280,468};const double half=(width-58)/2;
 escort={24,676,half,44};pirate={34+half,676,half,44};follow={24,734,half,44};hail={34+half,734,half,44};report={24,792,width-48,44};
}
CockpitOverlay xalariaConvoyOverlay(const XalariaConvoys& convoys,const XalariaEconomy& economy,double aspect,double seconds){
 CockpitOverlay hud;const XalariaConvoyLayout l(aspect);atlasGradient(hud,{0,0,l.width,900},atlasBg*1.24f,atlasBg*.67f);
 atlasText(hud,"Convoys / Operations",24,24,l.close.x-48,32,atlasTextColour);atlasButton(hud,l.close,"Back / F9");
 const auto known=convoys.known();const auto selected=convoys.selected();atlasText(hud,"Discovered operations / "+std::to_string(known.size()),24,81,280,17,atlasSecondary);
 const size_t start=(convoys.row/6)*6;
 for(size_t i=start;i<std::min(known.size(),start+6);++i){
  const auto key=known[i];const auto& p=convoys.plan(key);const auto* record=convoys.record(key);const double y=112.+double(i-start)*78.;
  atlasPanel(hud,{24,y,280,70},selected==key?atlasSurface*1.7f:atlasSurface);
  atlasText(hud,economy.placeName(p.origin),34,y+10,260,19,atlasTextColour);
  atlasText(hud,std::string(convoyPhaseName(convoys.stage(key,seconds).phase)),34,y+40,260,15,record&&record->betrayed?glm::vec3(1,.43,.29):atlasSecondary);
 }
 if(known.size()>6)atlasText(hud,"Up / Down / Page "+std::to_string(convoys.row/6+1)+" of "+std::to_string((known.size()+5)/6),24,608,280,16,atlasSecondary);
 const double x=328.,w=l.width-x-24.;
 if(selected){
  const auto key=*selected;const auto& p=convoys.plan(key);const auto* r=convoys.record(key);
  atlasText(hud,"Convoy "+std::to_string(key.service)+"-"+std::to_string(key.cycle%1000),x,112,w,26,atlasTextColour);
  atlasText(hud,"From / "+economy.placeName(p.origin),x,160,w,19,atlasTextColour);
  atlasText(hud,"To / "+economy.placeName(p.destination),x,193,w,19,atlasTextColour);
  atlasText(hud,std::string(convoyPhaseName(convoys.stage(key,seconds).phase)),x,235,w,20,{1.,.75,.42});
  atlasText(hud,"Manifest / "+std::to_string(r?r->remaining:p.units)+" of "+std::to_string(p.units)+" "+std::string(xGoodInfo(p.good).name),x,275,w,19,atlasTextColour);
  atlasText(hud,"Escort / "+std::string(xFactionName(p.faction))+" / Standing "+std::to_string(convoys.standing(p.faction)),x,316,w,18,atlasSecondary);
  atlasText(hud,"Threat / "+std::string(convoys.factionBattle(key)?"Faction interdiction / ":"Pirate ambush / ")+std::string(xFactionName(p.rival)),x,349,w,18,atlasSecondary);
  if(r){
   atlasText(hud,"Fleet fire / "+std::to_string(r->npcShots)+" shots / "+std::to_string(r->npcHits)+" impacts / Your hits "+std::to_string(r->playerHits),x,394,w,18,atlasSecondary);
   atlasText(hud,"Accompanied / "+std::to_string(int(r->accompanied))+" s / minimum 45 s",x,430,w,18,atlasSecondary);
   atlasText(hud,r->betrayed?"Convoy attacked by you / escort credentials void":r->claimed?"Escort payment received":r->duty==ConvoyDuty::Escort?"Your duty / Escort":r->duty==ConvoyDuty::Pirate?"Your intent / Piracy":"No duty accepted",x,470,w,19,r->betrayed?glm::vec3(1,.43,.29):atlasTextColour);
  }
  atlasText(hud,"Escort: defend the manifest and report at the destination berth.",x,532,w,17,atlasSecondary);
  atlasText(hud,"Piracy: disable the freighter, board and take its finite cargo.",x,563,w,17,atlasSecondary);
  atlasText(hud,"Follow uses helm assist; manual flight or B returns control.",x,603,w,17,atlasSecondary);
 }else{
  atlasText(hud,"No convoy reports discovered",x,140,w,25,atlasTextColour);
  atlasText(hud,"Hail a visible convoy or dock at a station for local departures.",x,198,w,18,atlasSecondary);
  atlasText(hud,"Nearby distress calls identify battles while you are flying.",x,236,w,18,atlasSecondary);
 }
 atlasButton(hud,l.escort,"E  Accept escort",false,bool(selected));atlasButton(hud,l.pirate,"P  Mark as prize / illegal",false,bool(selected));
 atlasButton(hud,l.follow,"F  Follow convoy / helm assist",convoys.following,bool(selected));atlasButton(hud,l.hail,"I  Hail selected convoy",false,bool(selected));
 atlasButton(hud,l.report,"R  File escort report at destination",false,bool(selected));
 atlasText(hud,convoys.message,24,854,l.width-48,17,{1.,.76,.43});hud.hitRegions.push_back({0,0,l.width,900});return hud;
}
void xalariaConvoyHud(CockpitOverlay& hud,const XalariaConvoys& convoys,double aspect,double seconds){
 if(!convoys.tracked())return;const auto key=*convoys.tracked();const auto* r=convoys.record(key);if(!r)return;
 const double w=std::max(180.,std::min(440.,900*aspect-684.)),x=(900*aspect-w)*.5;
 const CockpitRect panel{x,280,w,66};occlude(hud,panel);atlasPanel(hud,{x,280,w,66},atlasSurface);
 atlasText(hud,r->betrayed?"CONVOY / PIRACY REPORTED":r->duty==ConvoyDuty::Escort?"CONVOY / ESCORT DUTY":"CONVOY / PRIZE TRACKED",x+12,290,w-24,15,{1.,.73,.40});
 atlasText(hud,std::string(convoyPhaseName(convoys.stage(key,seconds).phase))+" / F9",x+12,319,w-24,14,atlasSecondary);
}
} // namespace infinity
