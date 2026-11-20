#pragma once
#include "Activity.hpp"
#include "Crew.hpp"
#include "Piloting.hpp"
#include "Presentation.hpp"
namespace infinity {
struct MarketView;
struct ExpeditionPlan;
struct MicroView;
struct XalariaRadioView;
struct CockpitRect {
    double x, y, w, h;
    bool contains(glm::dvec2) const;
    bool overlaps(const CockpitRect&) const;
};
// A uniform fit in framebuffer pixels. A centred safe area keeps the reticle
// aligned with the world projection, including notches and portrait windows.
struct CockpitViewport {
    int x = 0, y = 0, width = 1, height = 1;
    double projectionScale = 1;
    CockpitViewport() = default;
    CockpitViewport(int pixelWidth, int pixelHeight, CockpitRect safe);
    double aspect() const {
        return double(width) / height;
    }
    glm::dvec2 toCanvas(glm::dvec2 point) const;
    glm::dvec2 toPixels(glm::dvec2 point) const;
};
struct CockpitLayout {
    double width;
    CockpitRect traffic, celestial, life, navigation, flight, scope, systems, windscreen;
    explicit CockpitLayout(double aspect);
    std::optional<size_t> trafficRow(glm::dvec2, size_t count) const;
    std::optional<size_t> celestialRow(glm::dvec2, size_t count) const;
};
// A short display list keeps a distant selected actor visible without reordering
// the authoritative contact list used by K cycling and stable identity lookup.
std::vector<size_t> cockpitActivityRows(std::span<const ActivityContact>);
std::vector<size_t> cockpitCelestialRows(std::span<const CelestialContact>);
std::optional<Address> nextLivingWorld(std::span<const CelestialContact>, const std::optional<Address>&);
struct CockpitTargetMarker {
    glm::dvec2 point{}, direction{};
    bool offscreen = false, aft = false;
};
struct CockpitTextRun {
    CockpitRect bounds;
    size_t firstVertex=0,vertexCount=0,firstIndex=0,indexCount=0;
    std::string value;
};
struct AtlasGlyphQuad {
    float x0, y0, x1, y1, u0, v0, u1, v1;
    uint32_t colour;
};
struct CockpitPopup {
    CockpitRect bounds;
    bool critical=false;
};
struct CockpitContactHit {
    CockpitRect bounds;
    std::optional<Address> world;
    std::optional<ActivityId> activity;
    bool catalogue=false,life=false;
};
CockpitTargetMarker cockpitTargetMarker(glm::dvec3 bearing, double aspect, double projectionScale = 1);
struct CockpitOverlay {
    Mesh panels, strokes, lettering;
    std::vector<CockpitRect> worldLabels, hitRegions;
    std::vector<CockpitTextRun> textRuns;
    std::vector<CockpitPopup> popups;
    std::vector<CockpitContactHit> contactHits;
    std::vector<AtlasGlyphQuad> atlasGlyphs;
};
struct XalariaRadioLayout {
    CockpitRect panel,close;
    std::array<CockpitRect,8> actions;
    explicit XalariaRadioLayout(double aspect);
};
void xalariaRadioOverlay(CockpitOverlay&,const XalariaRadioView&,double aspect);
class FlightHudStability {
  public:
    void update(double seconds, double turnRate, bool available);
    float opacity() const { return opacity_; }
    bool showMarkers() const { return quietSeconds_>=.24; }

  private:
    double quietSeconds_ = 0;
    float opacity_ = 0;
};
// Room and interaction text comes from the deck's fixed labels and prompts.
class DeckHudLabel {
  public:
    void update(std::string_view,double seconds);
    std::string_view text() const { return shown_; }
    float opacity() const { return opacity_; }
    bool settled() const { return heldSeconds_>=settleSeconds && shown_==candidate_; }

  private:
    static constexpr double settleSeconds = .10;
    std::string_view shown_,candidate_;
    double heldSeconds_ = 0.;
    float opacity_ = 0.f;
};
std::vector<std::pair<size_t,size_t>> cockpitTextConflicts(const CockpitOverlay&);
// Screen-space world markers yield to panels, notifications and label cards.
void maskCockpitMarkers(Mesh&, const CockpitViewport&, const CockpitOverlay&);
struct CrewPanelLayout {
    CockpitRect panel;
    std::array<CockpitRect, crewRoomCount> rooms;
    std::array<CockpitRect, maxShipCrew> roster;
    explicit CrewPanelLayout(double aspect);
};
struct CrewFlightLayout {
    CockpitRect panel, automatic, weapon, notice;
    std::array<CockpitRect, maxShipCrew> roster;
    std::array<CockpitRect, crewRoomCount> rooms;
    std::array<CockpitRect, 8> actions;
    std::array<CockpitRect, maxWeaponMounts> guns;
    explicit CrewFlightLayout(double aspect);
};
CockpitRect weaponStationCard(double aspect, bool flightConsole);
struct PlannerLayout {
    CockpitRect panel,left,right,details,close;
    CockpitRect body{0,0,0,0},status{0,0,0,0},receipt{0,0,0,0};
    CockpitRect debrief{0,0,0,0};
    double footerY = 0;
    CockpitRect section,composition,route;
    std::array<CockpitRect,3> bays;
    std::array<CockpitRect,10> steps;
    std::array<CockpitRect,4> offers;
    std::array<CockpitRect,3> minus,plus;
    std::array<CockpitRect,6> actions;
    explicit PlannerLayout(double aspect,bool expanded=false,const ExpeditionPlan* view=nullptr);
};
CockpitRect surfaceReconPanel(double aspect);
CockpitRect surfaceReconMapRect(double aspect);
CockpitRect cockpitContactsSummary();
CockpitRect cockpitLifeBadge(double aspect,bool expanded);
// Native vector diagrams share the modal's fitted canvas and authoritative view data.
void drawPlannerDiagrams(CockpitOverlay&,const ExpeditionPlan&,const PlannerLayout&);
// Port of Hyperion's CabinMesh / rebuildCockpit beveled screen-framed cabin.
Mesh cockpitMesh(double aspect);
Mesh xalariaCockpitMesh();
Mesh xalariaCockpitHatch();
Mesh xalariaShipDeck();
struct StationLayout;
struct StationAnnex;
enum class StationRole;
enum class XalariaLook;
Mesh xalariaDockingDeck(const StationLayout&,XalariaLook);
// Glazed envelopes, frame dimensions, and two exterior light sources.
using StationWindowLighting = std::array<glm::vec4,6>;
StationWindowLighting xalariaStationWindows(const StationLayout&);
// Two annexes, with six vectors per room for bounds, fixtures, and lighting.
using StationAnnexLighting = std::array<glm::vec4,12>;
StationAnnexLighting xalariaStationAnnexLighting(const StationLayout&);
Mesh xalariaAirlockDoor();
Mesh xalariaAirlockSill();
Mesh xalariaAirlockIndicators();
Mesh xalariaCharterCrate(const StationLayout&);
struct XalariaMapView;
class XalariaEconomy;
struct XContract;
struct CabinDisplay {
    glm::mat4 model{1};
    glm::dvec2 size{};
    CockpitOverlay content;
};
const std::array<CabinDisplay,9>& xalariaShipSigns();
const std::array<CabinDisplay,3>& xalariaAirlockSigns();
std::vector<CabinDisplay> xalariaStationSigns(const StationLayout&);
CabinDisplay xalariaLoadingBayDisplay(const StationLayout&, const XalariaEconomy&, const XContract&, bool claimed,
                                     std::string_view sender={});
CabinDisplay xalariaStationSurveyDisplay(const StationAnnex&,const XalariaMapView&);
void xalariaStationLocation(CockpitOverlay&,std::string_view room,std::string_view station,StationRole,float roomOpacity);
std::array<CabinDisplay,3> xalariaDeckDisplays(const FlightInstruments&,const XalariaMapView&,
                                               const XalariaEconomy&);
Mesh xalariaCockpitGlazing();
Mesh xalariaCockpitYoke();
Mesh xalariaCockpitThrottle();
struct CockpitControlPoses {
    glm::mat4 yoke{1}, throttle{1};
};
CockpitControlPoses xalariaCockpitControlPoses(const FlightInstruments&);
std::array<CabinDisplay,4> xalariaCockpitDisplays(const FlightInstruments&, double fuel, double fuelCapacity);
void xalariaShieldHud(CockpitOverlay&,double aspect,const ShieldReadout&);
CockpitOverlay cockpitOverlay(const FlightInstruments&, double aspect, std::string_view mode,
                              std::string_view notice = {}, std::span<const CelestialContact> contacts = {},
                              std::span<const ActivityContact> activity = {}, bool help = false,
                              double projectionScale = 1, const MarketView* market = nullptr,
                              glm::dquat headInShip = {1, 0, 0, 0}, const CrewView* crew = nullptr);
CockpitOverlay cockpitOverlay(const FlightInstruments&,double aspect,std::string_view mode,std::string_view notice,
                              std::span<const CelestialContact>,std::span<const ActivityContact>,bool help,
                              double projectionScale,const MarketView*,glm::dquat headInShip,const CrewView*,
                              const ExpeditionPlan*,const MicroView* = nullptr,bool showFlightMarkers = true);
std::string flightQuantity(double value, bool speed = false);
} // namespace infinity
