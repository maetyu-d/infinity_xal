#pragma once
#include "XalariaEconomy.hpp"

namespace infinity {

enum class StoryKind : uint8_t { Distress, Disaster, Conflict, Factions, Belief, Science, Count };
enum class StorySite : uint8_t { Survey, Station, Vessel };
enum class StoryStage : uint8_t { FirstReport, Developing, FollowUp };
enum class StoryPage : uint8_t { Feed, FollowUps, Journal, Archive, Nearby, News };
enum class IncidentPhase : uint8_t { Reported, Responding, Contained, Escalated, Completed, Failed };
enum class StoryOrigin : uint8_t { Signal, Survey, Conversation, Station, TrustedContact, FollowUp, PreviousJourney };
inline constexpr unsigned storyChapterCount = 3;
inline constexpr unsigned storyVariantsPerKind = 160;

struct StoryKey {
    uint32_t system;
    uint64_t edition;
    StoryKind kind;
    unsigned chapter = 0;
    unsigned variant = 0;
    bool live = false;
    auto operator<=>(const StoryKey&) const = default;
};

struct XalariaStory {
    StoryKey key;
    uint32_t system;
    std::optional<uint64_t> world;
    std::optional<XPlace> port;
    StorySite site;
    XFaction source;
    std::string place, headline, report, development, followUp;
    std::array<std::string,2> decisions, outcomes;
};

struct StoryContext {
    uint32_t system = 0;
    std::optional<uint64_t> orbit;
    std::optional<XPlace> boardedStation;
    std::optional<ActivityId> boardedVessel;
    bool stellarOrbit = false;
};

struct StoryIncident {
    StoryKey key;
    IncidentPhase phase;
    double started, responseAt, decisionAt, endsAt;
    XFaction responder;
    bool urgent;
    bool closed() const {return phase==IncidentPhase::Completed||phase==IncidentPhase::Failed;}
};

struct NearbyIncident {
    StoryKey key;
    unsigned hops;
};

struct StoryDiscovery {StoryKey key;StoryOrigin origin;double seconds;};
enum class InvestigationCause : uint8_t { Failure, Interference, Fabrication };
struct InvestigationState {unsigned evidence=0;std::array<double,4> collected{};};
struct InvestigationPlan {
    StoryKey key;
    Address frame;
    glm::dvec3 centre;
    std::array<glm::dvec3,3> clues;
    double searchRadius=5000.;
    InvestigationCause cause,claim;
};
inline constexpr uint32_t investigationActivityTag=0x40000000u;
inline bool isInvestigationEvidence(const ActivityId& id) {return (id.slot&0xf0000000u)==investigationActivityTag;}
Mesh investigationMesh(unsigned step,unsigned detail);
std::string_view storyOriginName(StoryOrigin);

std::string_view storyKindName(StoryKind);
std::string_view storyStageName(StoryStage);
std::string_view incidentPhaseName(IncidentPhase);

class XalariaStories {
  public:
    explicit XalariaStories(const XalariaEconomy&,const XalariaCatalog&);
    const XalariaCatalog& catalog() const {return catalog_;}
    XalariaStory generate(StoryKey) const;
    StoryKey latest(uint32_t system,StoryKind,double seconds) const;
    void advance(double seconds);
    StoryIncident incident(StoryKey,double seconds) const;
    std::vector<NearbyIncident> nearby(uint32_t system,double seconds) const;
    std::vector<NearbyIncident> news(uint32_t system,double seconds) const;
    unsigned activityLevel(uint32_t system) const;
    std::string incidentReport(StoryKey,double seconds) const;
    bool actionable(StoryKey) const;
    bool known(StoryKey) const;
    const StoryDiscovery* discovery(StoryKey) const;
    const std::vector<StoryDiscovery>& discoveries() const {return discoveries_;}
    size_t discoveredCount() const {return discoveries_.size();}
    // Record a lead confirmed by a gameplay encounter; browsing never calls this.
    bool discover(StoryKey,StoryOrigin);
    std::optional<StoryKey> survey(const StoryContext&,double seconds);
    std::optional<StoryKey> hear(const XalariaTrafficContact&,double seconds);
    bool observe(const StoryContext&,double seconds,double dt);
    unsigned trust(XFaction) const;
    std::string takeDiscoveryNotice();
    const InvestigationState* investigation(StoryKey) const;
    InvestigationPlan investigationPlan(StoryKey) const;
    bool evidenceComplete(StoryKey) const;
    std::vector<ActivityBody> evidenceBodies(const Address&) const;
    std::optional<FlightWaypoint> investigationWaypoint(StoryKey) const;
    bool scanEvidence(const StoryContext&,const Address&,glm::dvec3 eye,glm::dvec3 velocity,double seconds);
    std::array<std::string,4> evidenceNotes(StoryKey) const;
    std::string investigationTask(StoryKey) const;
    int factionStanding(XFaction) const;
    bool evidenceOpen=false;
    std::pair<double,double> trafficClock(const ActivityId&,double seconds) const;
    std::string trafficAdvisory(const XPlace&,double seconds) const;
    double marketFactor(const XPlace&,XGood) const;
    std::optional<StoryKey> distressAt(uint32_t system,double seconds) const;
    StoryStage stage(StoryKey,double seconds) const;
    std::optional<ActivityId> vessel(const XalariaStory&) const;
    bool ready(const XalariaStory&,const StoryContext&) const;
    std::optional<StoryKey> continuation(StoryKey) const;
    void show(uint32_t system,double seconds);
    void showPage(StoryPage,double seconds);
    void nextPage(double seconds);
    void changeSheet(int,double seconds);
    void openReport(StoryKey,double seconds);
    void refresh(double seconds);
    void select(size_t);
    void step(int);
    const XalariaStory* selected() const;
    const std::vector<XalariaStory>& entries() const {return entries_;}
    const std::optional<StoryKey>& tracked() const {return tracked_;}
    StoryPage page() const {return page_;}
    size_t sheet() const {return sheet_;}
    size_t sheetCount() const;
    size_t followUpCount() const {return pending_.size();}
    size_t nearbyCount() const {return nearby_.size();}
    unsigned hops(StoryKey) const;
    std::optional<unsigned> result(StoryKey) const;
    bool track(StoryKey);
    bool resolve(StoryKey,const StoryContext&,unsigned choice,XalariaEconomy&);
    std::string serialize() const;
    bool restore(std::string_view);

    bool open = false;
    uint32_t system = 0;
    size_t row = 0;
    std::string message;

  private:
    struct Receipt {StoryKey key;unsigned choice;double seconds=0.;};
    const XalariaEconomy& economy_;
    const XalariaCatalog& catalog_;
    std::optional<StoryKey> tracked_;
    std::vector<Receipt> receipts_;
    std::map<StoryKey,unsigned> outcomes_;
    std::map<StoryKey,double> interventionTimes_;
    std::vector<StoryKey> pending_;
    std::vector<XalariaStory> entries_;
    StoryPage page_=StoryPage::Feed;
    size_t sheet_=0;
    double seconds_=0.;
    uint64_t feedSecond_=UINT64_MAX;
    std::vector<NearbyIncident> nearby_,news_;
    std::vector<StoryDiscovery> discoveries_;
    std::map<StoryKey,size_t> discovered_;
    std::set<uint64_t> encounters_;
    std::map<StoryKey,InvestigationState> investigations_;
    std::string discoveryNotice_;
    std::optional<uint32_t> listeningSystem_;
    std::optional<XPlace> visitingStation_;
    double listeningSeconds_=0.,visitingSeconds_=0.,observationSecond_=-1.;
    mutable std::map<std::pair<uint32_t,uint64_t>,std::vector<XalariaStory>> incidentSites_;
    const std::vector<XalariaStory>& incidentSites(uint32_t,double) const;
    std::vector<StoryKey> localIncidents(uint32_t,double) const;
    std::vector<std::pair<uint32_t,unsigned>> neighbourhood(uint32_t) const;
    XalariaStory generateFollowUp(StoryKey) const;
    XalariaStory generateBase(StoryKey) const;
    bool siteReady(const XalariaStory&,const StoryContext&) const;
    std::vector<StoryKey> pageKeys() const;
    StoryKey encounterLead(uint32_t,uint64_t,StoryKind,unsigned tier) const;
    void rebuildKnownFeeds();
    void rebuild(double seconds);
};

struct XalariaStoryLayout {
    double width;
    XalariaMapRect feed,followUps,journal,archive,nearby,news,close,list,detail,track,route,primary,secondary,previous,next;
    static constexpr double rowHeight = 104.;
    explicit XalariaStoryLayout(double aspect);
    std::optional<size_t> pickRow(glm::dvec2,size_t) const;
};
CockpitOverlay xalariaStoryOverlay(const XalariaStories&,const StoryContext&,double seconds,double aspect,
                                   std::string_view routeAction);
} // namespace infinity
