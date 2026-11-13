#pragma once
#include "Universe.hpp"
#include <filesystem>
#include <map>
#include <unordered_map>

namespace infinity {
struct XalariaWorld {
    uint64_t id=0, uniqueId=0;
    std::string name, cloudColor;
    int primary=-1, composition=1, lifeStructure=0, intelligence=0;
    double radius=1, distance=0, period=0, tilt=0, tiltRotation=0, temperature=280;
    bool life=false, freezing=false;
    unsigned communities=0, stations=0, mines=0;
    double cloudCover=0, ringInner=0, ringOuter=0;
};
struct XalariaStar {
    uint32_t seed=0;
    unsigned spectralClass=4, technology=1, lifeGrade=0, lifeWorlds=0;
    std::string name;
    double radius=6.957e8, temperature=5772, disc=1e13;
    std::vector<uint32_t> wormholes;
    std::map<uint64_t,XalariaWorld> worlds;
};
enum class XalariaLook { CharredGlass, VerdantFog, TidalBlue, AcidEtch };
struct XalariaRegion {
    unsigned x, y;
    XalariaLook look;
};
std::string_view xalariaLookName(XalariaLook);

// An immutable factual snapshot. Source IDs are addresses; map distance is never a wormhole edge.
class XalariaCatalog {
  public:
    static constexpr unsigned regionSide = 20;
    explicit XalariaCatalog(const std::filesystem::path&);
    std::string fingerprint;
    unsigned grid=200;
    std::map<uint32_t,XalariaStar> stars;
    size_t worldCount=0, lifeCount=0, communityCount=0;
    const XalariaStar& star(uint64_t seed) const;
    const XalariaWorld& world(const Address&) const;
    XalariaRegion region(uint32_t seed) const;
    std::vector<uint32_t> route(uint32_t from,uint32_t to) const;
    std::vector<uint32_t> search(std::string_view query,size_t limit=20) const;
    std::optional<uint64_t> searchWorld(uint32_t system,std::string_view query) const;
    glm::dvec3 mapPosition(uint32_t seed) const;
    Address address(uint32_t seed) const;
    void apply(Node&,const Node& parent) const;
    
};
void bindXalariaLabels(std::shared_ptr<const XalariaCatalog>,uint64_t universeSeed);
std::string xalariaObjectLabel(uint64_t seed);
std::shared_ptr<const XalariaWorld> xalariaWorldFacts(uint64_t seed);
struct XalariaWormholeMouth {
    Address reference; // Planet frame, shared with the station approach.
    glm::dvec3 metres{};
    glm::dquat orientation{1,0,0,0}; // Local -Z points into the mouth from the station.
    double radius=1050;
    uint32_t destination=0;
};
struct XalariaMapView {
    const XalariaCatalog* catalog=nullptr;
    bool open=true;
    uint32_t current=0, selected=0;
    std::vector<uint32_t> route;
    std::string query, message;
    glm::dvec2 centre{100,100};
    double span=210;
    std::optional<uint64_t> selectedWorld;
    unsigned worldScroll=0;
    enum class Mode { Galaxy, System } mode=Mode::Galaxy;
    std::vector<uint32_t> activeRoute;
    size_t activeLeg=0;
    std::optional<uint64_t> activeWorld, currentWorld;
    std::optional<XalariaWormholeMouth> wormhole;
    uint32_t destination=0;
    bool flying=false, paused=false, finalApproach=false, arrived=false, searchFocused=true;
    bool showLinks=false;
    double progress=0, range=0, eta=-1, seconds=0;
    glm::dvec2 ship{100,100};
    glm::dvec3 systemShip{};
    std::vector<uint32_t> matches;
    size_t matchIndex=0;
};
struct XalariaMapRect {
    double x=0,y=0,w=0,h=0;
    bool contains(glm::dvec2 p) const {return p.x>=x&&p.y>=y&&p.x<x+w&&p.y<y+h;}
};
struct XalariaMapLayout {
    double width;
    XalariaMapRect chart, details, search, clearSearch, results, nearby, worlds;
    XalariaMapRect galaxy, system, locate, fitRoute, fitGalaxy, links, economy, close;
    XalariaMapRect fly, pause, cancel, wormhole;
    static constexpr unsigned worldRows=8, resultRows=5;
    static constexpr double worldStep=39, resultStep=43;
    explicit XalariaMapLayout(double aspect);
};
struct CockpitOverlay;
CockpitOverlay xalariaMapOverlay(const XalariaMapView&,double aspect);
void xalariaFlightOverlay(CockpitOverlay&,const XalariaMapView&,double aspect);
glm::dvec2 xalariaMapPoint(const XalariaMapView&,uint32_t seed,double aspect);
glm::dvec2 xalariaMapPoint(const XalariaMapView&,glm::dvec2 grid,double aspect);
glm::dvec2 xalariaMapGrid(const XalariaMapView&,glm::dvec2 canvas,double aspect);
glm::dvec2 xalariaSystemPoint(const XalariaMapView&,uint64_t world,double aspect);
glm::dvec2 xalariaSystemShipPoint(const XalariaMapView&,double aspect);
bool xalariaSystemShowsWorld(const XalariaMapView&,uint64_t world);
std::optional<uint32_t> xalariaPickStar(const XalariaMapView&,glm::dvec2,double aspect);
std::optional<uint64_t> xalariaPickWorld(const XalariaMapView&,glm::dvec2,double aspect);
void xalariaZoomMap(XalariaMapView&,double wheel,glm::dvec2 canvas,double aspect);
void xalariaFitRoute(XalariaMapView&,double aspect);
bool xalariaMapContains(glm::dvec2,double aspect);
} // namespace infinity
