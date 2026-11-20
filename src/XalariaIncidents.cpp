#include "infinity/XalariaStories.hpp"
#include "infinity/XalariaTraffic.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <queue>
#include <set>

namespace infinity {
namespace {
constexpr uint64_t incidentPeriod=1800;
uint64_t incidentCycle(double seconds) {return uint64_t(std::clamp(seconds,0.,1e12))/incidentPeriod+2;}
uint64_t incidentSeed(StoryKey key) {
    return mix64((uint64_t(key.system)<<32)^mix64(key.edition)^0x494e434944454e54ULL);
}
}

std::string_view incidentPhaseName(IncidentPhase phase) {
    constexpr std::array names{"Reported","Response dispatched","Contained","Escalating","Resolved","Response failed"};
    return names.at(size_t(phase));
}

unsigned XalariaStories::activityLevel(uint32_t system) const {
    const auto& star=catalog_.star(system);
    unsigned stations=0,communities=0;
    for(const auto& [id,world]:star.worlds){stations+=world.stations;communities+=world.communities;}
    const unsigned score=unsigned(star.wormholes.size())+star.technology+(stations>=5?3:stations?1:0)+(communities?2:0);
    return score>=10?3:score>=6?2:1;
}

void XalariaStories::advance(double seconds) {
    if(std::isfinite(seconds)&&seconds>=0.)seconds_=std::min(seconds,1e12);
}

std::vector<StoryKey> XalariaStories::localIncidents(uint32_t system,double seconds) const {
    std::vector<StoryKey> keys;
    const auto cycle=incidentCycle(seconds);
    const auto lanes=activityLevel(system);
    for(uint64_t era=cycle-2;era<=cycle+1;++era)
        for(unsigned lane=0;lane<lanes;++lane){
            const auto edition=era*4+lane;
            const auto seed=incidentSeed({system,edition,StoryKind::Distress});
            const auto kind=StoryKind((mix64(system)+era+lane)%unsigned(StoryKind::Count));
            keys.push_back({system,edition,kind,0,unsigned(seed%storyVariantsPerKind)+1,true});
        }
    return keys;
}

StoryIncident XalariaStories::incident(StoryKey key,double seconds) const {
    const auto seed=incidentSeed(key);
    const double start=double(key.edition/4)*incidentPeriod-2.*incidentPeriod-double(seed%incidentPeriod);
    const double response=start+180.+double((seed>>9)%180);
    const double decision=start+600.+double((seed>>19)%240);
    const double end=start+1020.+double((seed>>29)%480);
    const auto responder=XFaction((seed>>39)%5);
    int capability=48+int(catalog_.star(key.system).technology)*5+int(activityLevel(key.system))*4;
    auto root=key;root.chapter=0;
    const auto assistance=interventionTimes_.find(root);
    if(assistance!=interventionTimes_.end()&&assistance->second<=seconds)capability+=result(root)==0?34:23;
    const bool successful=int((seed>>45)%100)<std::min(97,capability);
    const auto phase=seconds<response?IncidentPhase::Reported:
        seconds<decision?IncidentPhase::Responding:
        seconds<end?(successful?IncidentPhase::Contained:IncidentPhase::Escalated):
        successful?IncidentPhase::Completed:IncidentPhase::Failed;
    const bool urgent=seconds>=start&&seconds<decision&&
        (key.kind==StoryKind::Distress||key.kind==StoryKind::Disaster);
    return {key,phase,start,response,decision,end,responder,urgent};
}

bool XalariaStories::actionable(StoryKey key) const {
    if(!known(key)||result(key))return false;
    if(!key.live||key.chapter)return true;
    const auto state=incident(key,seconds_);
    return state.started<=seconds_&&!state.closed();
}

std::vector<std::pair<uint32_t,unsigned>> XalariaStories::neighbourhood(uint32_t system) const {
    std::vector<std::pair<uint32_t,unsigned>> systems{{system,0}};
    std::set<uint32_t> visited{system};
    for(size_t i=0;i<systems.size()&&systems.size()<96;++i){
        const auto [id,hops]=systems[i];
        if(hops==3)continue;
        for(const auto neighbour:catalog_.star(id).wormholes)
            if(visited.insert(neighbour).second)systems.emplace_back(neighbour,hops+1);
    }
    return systems;
}

std::vector<NearbyIncident> XalariaStories::nearby(uint32_t system,double seconds) const {
    std::vector<NearbyIncident> candidates;
    for(const auto [id,hops]:neighbourhood(system))
        for(const auto key:localIncidents(id,seconds)){
            const auto state=incident(key,seconds);
            if(state.started<=seconds&&!state.closed()&&!result(key))candidates.push_back({key,hops});
        }
    std::sort(candidates.begin(),candidates.end(),[&](const auto& a,const auto& b){
        const auto as=incident(a.key,seconds),bs=incident(b.key,seconds);
        return std::tuple(!as.urgent,a.hops,-as.started,a.key)<
               std::tuple(!bs.urgent,b.hops,-bs.started,b.key);
    });
    std::vector<NearbyIncident> selected;
    unsigned urgent=0;
    const auto limit=10+5*(activityLevel(system)-1);
    for(const auto& candidate:candidates){
        const bool emergency=incident(candidate.key,seconds).urgent;
        if(emergency&&urgent==2)continue;
        selected.push_back(candidate);
        urgent+=emergency;
        if(selected.size()==limit)break;
    }
    return selected;
}

std::vector<NearbyIncident> XalariaStories::news(uint32_t system,double seconds) const {
    std::vector<NearbyIncident> reports;
    for(const auto [id,hops]:neighbourhood(system))
        for(const auto key:localIncidents(id,seconds)){
            const auto state=incident(key,seconds-hops*120.);
            if(state.phase>=IncidentPhase::Contained)reports.push_back({key,hops});
        }
    std::sort(reports.begin(),reports.end(),[&](const auto& a,const auto& b){
        return std::tuple(-incident(a.key,seconds).started,a.hops,a.key)<
               std::tuple(-incident(b.key,seconds).started,b.hops,b.key);
    });
    if(reports.size()>24)reports.resize(24);
    return reports;
}

unsigned XalariaStories::hops(StoryKey key) const {
    const auto& reports=page_==StoryPage::News?news_:nearby_;
    const auto found=std::find_if(reports.begin(),reports.end(),[&](const auto& entry){return entry.key==key;});
    return found==reports.end()?0:found->hops;
}

std::string XalariaStories::incidentReport(StoryKey key,double seconds) const {
    const auto state=incident(key,seconds);
    const std::string faction(xFactionName(state.responder));
    constexpr std::array responses{
        "a recovery crew has departed to receive the emergency packet",
        "relief dispatch has assigned emergency supply runs",
        "patrols have begun inspections and civilian mediators are on the channel",
        "delegates have opened an arbitration watch",
        "a witness delegation has asked both congregations for a common record",
        "an independent research crew has begun a second observing run"
    };
    if(state.phase==IncidentPhase::Reported)
        return "The incident is on the open channel. "+faction+" is preparing its response. The local crew can still use assistance.";
    if(state.phase==IncidentPhase::Responding)
        return faction+" reports that "+responses.at(size_t(key.kind))+".";
    if(state.phase==IncidentPhase::Contained)
        return faction+" has contained the immediate problem. The response crews are checking the remaining claims before closing the watch.";
    if(state.phase==IncidentPhase::Escalated)
        return "The first response has fallen behind. "+faction+" is asking for another crew before the response window closes.";
    constexpr std::array success{
        "The recovery crew reached the witnesses. The rescue watch is closed and the support convoy is departing.",
        "Relief crews stabilized the affected area. Emergency supply runs are finishing and the restricted corridor is reopening.",
        "Mediators secured a civilian passage agreement. Inspection patrols are standing down from the disputed convoy lane.",
        "The delegates registered an interim agreement. Independent crews have received the new access record.",
        "The congregations accepted a witnessed record. Their visiting delegation is leaving with copies for both groups.",
        "The research crew completed its control run. The programme has released a provisional finding and its observing ship is returning."
    };
    constexpr std::array failure{
        "The recovery watch ended without a safe transfer. The crew evacuated and their case has passed to the receiving authority.",
        "Relief supplies missed the stabilization window. The affected area remains restricted and local supply costs have risen.",
        "Mediators failed to obtain an agreement. The inspection cordon remains in force and civilian convoys are being delayed.",
        "The arbitration ended without agreement. The previous restrictions remain and both parties have filed separate claims.",
        "The witness delegation could not reconcile the accounts. The congregations have separated their archives.",
        "The independent control failed to reproduce the result. The programme has withdrawn the claim pending a new experiment."
    };
    return faction+" reports. "+std::string((state.phase==IncidentPhase::Completed?success:failure).at(size_t(key.kind)));
}

const std::vector<XalariaStory>& XalariaStories::incidentSites(uint32_t system,double seconds) const {
    const auto cacheKey=std::pair{system,incidentCycle(seconds)};
    if(const auto found=incidentSites_.find(cacheKey);found!=incidentSites_.end())return found->second;
    if(incidentSites_.size()>=128)incidentSites_.erase(incidentSites_.begin());
    std::vector<XalariaStory> sites;
    for(const auto key:localIncidents(system,seconds))sites.push_back(generate(key));
    return incidentSites_.emplace(cacheKey,std::move(sites)).first->second;
}

std::pair<double,double> XalariaStories::trafficClock(const ActivityId& station,double seconds) const {
    const auto port=economy_.orbitalPlace(station);
    if(!port)return {seconds,1.};
    double clock=seconds,rate=1.;
    for(const auto& story:incidentSites(port->system,seconds)){
        if(story.port!=port)continue;
        const auto state=incident(story.key,seconds);
        const double duration=state.endsAt-state.responseAt;
        const double t=(seconds-state.responseAt)/duration;
        if(t<=0.||t>=1.)continue;
        const double amplitude=story.key.kind==StoryKind::Conflict?-55.:
            story.key.kind==StoryKind::Disaster?35.:story.key.kind==StoryKind::Distress?-24.:12.;
        const double angle=std::numbers::pi*t;
        clock+=amplitude*std::pow(std::sin(angle),2);
        rate+=amplitude*std::numbers::pi/duration*std::sin(2.*angle);
    }
    for(const auto& [key,evidence]:investigations_){
        if(key.system!=port->system||!result(key)||generateBase(key).port!=port)continue;
        const double t=(seconds-interventionTimes_.at(key))/900.;
        if(t<=0.||t>=1.)continue;
        const bool review=*result(key)==1&&investigationPlan(key).cause!=InvestigationCause::Failure;
        const double amplitude=review?-28.:18.,angle=std::numbers::pi*t;
        clock+=amplitude*std::pow(std::sin(angle),2);
        rate+=amplitude*std::numbers::pi/900.*std::sin(2.*angle);
    }
    return {clock,rate};
}

std::string XalariaStories::trafficAdvisory(const XPlace& port,double seconds) const {
    for(const auto& [key,evidence]:investigations_){
        if(key.system!=port.system||!result(key)||generateBase(key).port!=port)continue;
        const double age=seconds-interventionTimes_.at(key);
        if(age>=0.&&age<900.)return *result(key)==1?"INDEPENDENT EVIDENCE REVIEW / ACCESS CHECKS":
            "VERIFIED SPONSOR REPORT / RELIEF CLEARANCE";
    }
    for(const auto& story:incidentSites(port.system,seconds)){
        if(story.port!=port)continue;
        const auto state=incident(story.key,seconds);
        if(state.phase==IncidentPhase::Responding||state.phase==IncidentPhase::Escalated)
            return std::string(storyKindName(story.key.kind))+" / "+std::string(incidentPhaseName(state.phase))+
                   " / "+std::string(xFactionName(state.responder));
    }
    return {};
}

double XalariaStories::marketFactor(const XPlace& port,XGood good) const {
    double factor=1.;
    for(const auto& story:incidentSites(port.system,seconds_)){
        if(!story.world||story.world!=port.world)continue;
        const auto state=incident(story.key,seconds_);
        if(seconds_<state.responseAt)continue;
        const bool supplies=good==XGood::Seed||good==XGood::Neurocells||good==XGood::Nanomites;
        const double weight=story.key.kind==StoryKind::Disaster?(supplies?.22:.09):
                            story.key.kind==StoryKind::Conflict?.15:0.;
        const double elapsed=seconds_-state.endsAt;
        const double tail=state.phase==IncidentPhase::Failed?1.-smoothstep(0.,900.,elapsed):
                          1.-smoothstep(0.,180.,elapsed);
        factor+=weight*tail*(state.phase==IncidentPhase::Escalated?1.4:1.);
    }
    for(const auto& [key,evidence]:investigations_){
        if(key.system!=port.system||!result(key))continue;
        const auto story=generateBase(key);
        if(story.world!=port.world)continue;
        const double t=(seconds_-interventionTimes_.at(key))/900.;
        if(t<0.||t>1.)continue;
        const bool review=*result(key)==1&&investigationPlan(key).cause!=InvestigationCause::Failure;
        factor+=(review?.08:-.04)*std::pow(std::sin(std::numbers::pi*t),2);
    }
    return std::clamp(factor,.85,1.6);
}

std::pair<double,double> XalariaEconomy::trafficClock(const ActivityId& station,double seconds) const {
    return stories_?stories_->trafficClock(station,seconds):std::pair{seconds,1.};
}
std::string XalariaEconomy::trafficAdvisory(const XPlace& port,double seconds) const {
    return stories_?stories_->trafficAdvisory(port,seconds):std::string{};
}
} // namespace infinity
