#include "infinity/XalariaStories.hpp"
#include "infinity/XalariaTraffic.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <utility>

namespace infinity {
namespace {
uint64_t encounterToken(StoryOrigin source,uint32_t system,uint64_t place,uint64_t detail=0) {
    return mix64(0x464f554e44535459ULL^uint64_t(source)^mix64(system)^
                 std::rotl(mix64(place),17)^std::rotl(mix64(detail),37));
}
}

std::string_view storyOriginName(StoryOrigin origin) {
    constexpr std::array names{"Local distress broadcast","Orbital survey","Crew conversation","Station contact",
        "Trusted contact","Earned follow-up","Previous journey"};
    return names.at(size_t(origin));
}

const StoryDiscovery* XalariaStories::discovery(StoryKey key) const {
    key.chapter=0;
    const auto found=discovered_.find(key);
    return found==discovered_.end()?nullptr:&discoveries_[found->second];
}

bool XalariaStories::known(StoryKey key) const {
    if(!key.chapter)return discovery(key)!=nullptr;
    return outcomes_.contains(key)||tracked_==key||std::find(pending_.begin(),pending_.end(),key)!=pending_.end();
}

bool XalariaStories::discover(StoryKey key,StoryOrigin origin) {
    if(key.chapter||size_t(origin)>size_t(StoryOrigin::PreviousJourney)||!catalog_.stars.contains(key.system)||
       size_t(key.kind)>=size_t(StoryKind::Count)||key.variant>storyVariantsPerKind||
       discovered_.contains(key)||discoveries_.size()>=100000)return false;
    if(key.live){
        const auto state=incident(key,seconds_);
        if(!key.variant||state.started>seconds_||state.closed())return false;
    }
    if(origin!=StoryOrigin::PreviousJourney)investigations_.emplace(key,InvestigationState{});
    discovered_.emplace(key,discoveries_.size());
    discoveries_.push_back({key,origin,seconds_});
    feedSecond_=UINT64_MAX;
    discoveryNotice_="NEW LEAD / "+generate(key).headline+" / F7";
    return true;
}

std::string XalariaStories::takeDiscoveryNotice() {
    return std::exchange(discoveryNotice_,{});
}

unsigned XalariaStories::trust(XFaction faction) const {
    const auto standing=factionStanding(faction);
    return standing>=3?2:standing>0?1:0;
}

StoryKey XalariaStories::encounterLead(uint32_t system,uint64_t seed,StoryKind kind,unsigned tier) const {
    // Public tips, established contacts and trusted contacts draw different work.
    const unsigned first=tier==0?1:tier==1?65:129;
    const unsigned count=tier<2?64:32;
    const unsigned start=unsigned(mix64(seed)%count);
    for(unsigned i=0;i<count;++i){
        const StoryKey key{system,0,kind,0,first+(start+i)%count};
        if(!known(key))return key;
    }
    return {system,0,kind,0,first+start};
}

std::optional<StoryKey> XalariaStories::survey(const StoryContext& context,double seconds) {
    advance(seconds);
    if(!catalog_.stars.contains(context.system)||(!context.orbit&&!context.stellarOrbit))return {};
    if(context.orbit&&!catalog_.star(context.system).worlds.contains(*context.orbit))return {};
    const auto token=encounterToken(StoryOrigin::Survey,context.system,context.orbit.value_or(UINT64_MAX));
    if(encounters_.contains(token)||encounters_.size()>=100000)return {};
    const auto key=encounterLead(context.system,token,StoryKind::Science,0);
    if(!discover(key,StoryOrigin::Survey))return {};
    encounters_.insert(token);
    return key;
}

std::optional<StoryKey> XalariaStories::hear(const XalariaTrafficContact& contact,double seconds) {
    advance(seconds);
    if(!isXalariaTraffic(contact.body.id)||!catalog_.stars.contains(contact.port.system))return {};
    if(tracked_&&investigation(*tracked_)&&vessel(generateBase(*tracked_))==contact.body.id)return {};
    const auto tier=trust(economy_.faction(contact.port));
    const auto token=encounterToken(StoryOrigin::Conversation,contact.port.system,
                                   seedOf(contact.body.id.home),contact.body.id.slot^mix64(tier));
    if(encounters_.contains(token)||encounters_.size()>=100000)return {};
    if(const auto distress=distressAt(contact.port.system,seconds);distress&&
       vessel(generate(*distress))==contact.body.id&&discover(*distress,StoryOrigin::Conversation)){
        encounters_.insert(token);
        return distress;
    }
    constexpr std::array publicKinds{StoryKind::Disaster,StoryKind::Factions,StoryKind::Science};
    constexpr std::array privateKinds{StoryKind::Conflict,StoryKind::Belief,StoryKind::Science};
    const auto kind=(tier?privateKinds:publicKinds)[mix64(token)%3];
    const auto key=encounterLead(contact.port.system,token,kind,tier);
    if(!discover(key,tier?StoryOrigin::TrustedContact:StoryOrigin::Conversation))return {};
    encounters_.insert(token);
    return key;
}

bool XalariaStories::observe(const StoryContext& context,double seconds,double dt) {
    advance(seconds);
    const bool present=catalog_.stars.contains(context.system)&&(context.orbit||context.stellarOrbit||
                       context.boardedStation||context.boardedVessel);
    if(!present||!std::isfinite(dt)||dt<=0.){
        listeningSystem_.reset();visitingStation_.reset();listeningSeconds_=visitingSeconds_=0.;return false;
    }
    if(listeningSystem_!=context.system){listeningSystem_=context.system;listeningSeconds_=0.;}
    listeningSeconds_+=std::min(dt,.25);
    if(visitingStation_!=context.boardedStation){visitingStation_=context.boardedStation;visitingSeconds_=0.;}
    if(visitingStation_)visitingSeconds_+=std::min(dt,.25);
    // No list is revealed on arrival. Listen locally, or physically reach a concourse.
    if(observationSecond_==std::floor(seconds_))return false;
    observationSecond_=std::floor(seconds_);
    bool found=false;
    if(listeningSeconds_>=20.){
        const auto token=encounterToken(StoryOrigin::Signal,context.system,uint64_t(seconds_/1800.));
        if(!encounters_.contains(token)&&encounters_.size()<100000){
            for(const auto key:localIncidents(context.system,seconds_)){
                const auto state=incident(key,seconds_);
                if((key.kind==StoryKind::Distress||key.kind==StoryKind::Disaster)&&state.urgent&&
                   discover(key,StoryOrigin::Signal)){
                    encounters_.insert(token);found=true;break;
                }
            }
        }
    }
    if(visitingStation_&&visitingSeconds_>=5.){
        const auto& port=*visitingStation_;
        if(port.system!=context.system||!economy_.orbitalStationId(port))return found;
        const auto tier=trust(economy_.faction(port));
        const auto token=encounterToken(StoryOrigin::Station,port.system,port.world,
                                       (uint64_t(port.station)<<8)^tier);
        if(!encounters_.contains(token)&&encounters_.size()<100000){
            constexpr std::array kinds{StoryKind::Factions,StoryKind::Belief,StoryKind::Disaster,StoryKind::Conflict};
            const auto key=encounterLead(port.system,token,kinds[mix64(token)%kinds.size()],tier);
            if(discover(key,tier?StoryOrigin::TrustedContact:StoryOrigin::Station)){
                encounters_.insert(token);found=true;
            }
        }
    }
    return found;
}

void XalariaStories::rebuildKnownFeeds() {
    nearby_.clear();news_.clear();
    const auto area=neighbourhood(system);
    for(auto it=discoveries_.rbegin();it!=discoveries_.rend();++it){
        const auto key=it->key;
        const auto place=std::find_if(area.begin(),area.end(),[&](const auto& entry){return entry.first==key.system;});
        if(place==area.end())continue;
        if(actionable(key))nearby_.push_back({key,place->second});
        if(key.live&&incident(key,seconds_-place->second*120.).phase>=IncidentPhase::Contained)
            news_.push_back({key,place->second});
    }
    std::stable_sort(nearby_.begin(),nearby_.end(),[&](const auto& a,const auto& b){
        const bool au=a.key.live&&incident(a.key,seconds_).urgent;
        const bool bu=b.key.live&&incident(b.key,seconds_).urgent;
        return std::tuple(!au,a.hops)<std::tuple(!bu,b.hops);
    });
    unsigned urgent=0;
    std::erase_if(nearby_,[&](const auto& entry){
        return entry.key.live&&incident(entry.key,seconds_).urgent&&urgent++>=2;
    });
    if(nearby_.size()>20)nearby_.resize(20);
}
} // namespace infinity
