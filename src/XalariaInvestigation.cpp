#include "infinity/XalariaStories.hpp"
#include "infinity/XalariaCombat.hpp"
#include "infinity/XalariaTraffic.hpp"
#include <algorithm>
#include <cmath>

namespace infinity {
namespace {
StoryKey rootKey(StoryKey key){key.chapter=0;return key;}
uint64_t caseSeed(StoryKey key) {
    return mix64(uint64_t(key.system)<<32^mix64(key.edition)^mix64(key.variant)^mix64(unsigned(key.kind))^0x45564944454e4345ULL);
}
constexpr std::array<std::array<std::string_view,3>,6> objectNames{{
    {"Detached cargo assembly","Emergency flight recorder","Rescue telemetry probe"},
    {"Damaged monitoring relay","Safety sensor buffer","Independent sensor probe"},
    {"Scored hull fragment","Weapons timing recorder","Civilian observation probe"},
    {"Discarded registry beacon","Access-log capsule","Public registry relay"},
    {"Sealed ceremonial container","Ritual signal archive","Independent listening probe"},
    {"Displaced research instrument","Experiment data capsule","Control instrument"}
}};
std::string finding(StoryKind kind,InvestigationCause cause) {
    constexpr std::array<std::array<std::string_view,3>,6> findings{{
        {"The emergency began with an equipment failure.","The emergency circuit was interrupted deliberately.","The emergency transmission was staged before the alleged failure."},
        {"An uncorrected sensor fault preceded the disaster.","A commanded override defeated the safety system.","The reported emergency used an earlier incident's recordings."},
        {"A machinery failure produced the apparent attack signature.","A timed weapons discharge preceded the convoy's distress call.","The claimed attack was assembled from unrelated traffic records."},
        {"An archive conversion lost the original access entry.","An authorized account deliberately altered the access record.","The disputed restriction was issued using a fabricated authority."},
        {"A damaged relay repeated an old ceremonial signal.","A separate transmitter deliberately modified the ritual signal.","The alleged revelation was recorded before its claimed observation."},
        {"Instrument drift explains the claimed anomaly.","An external source deliberately contaminated the observing run.","The reported measurement was copied from a different experiment."}
    }};
    return std::string(findings[size_t(kind)][size_t(cause)]);
}
unsigned evidenceCount(unsigned mask){return unsigned(bool(mask&1))+unsigned(bool(mask&2))+unsigned(bool(mask&4))+unsigned(bool(mask&8));}
}

const InvestigationState* XalariaStories::investigation(StoryKey key) const {
    const auto found=investigations_.find(rootKey(key));
    return found==investigations_.end()?nullptr:&found->second;
}

InvestigationPlan XalariaStories::investigationPlan(StoryKey key) const {
    key=rootKey(key);
    const auto story=generateBase(key);
    const auto& star=catalog_.star(story.system);
    // Catalogue systems have worlds; retain a system-frame fallback for sparse catalogues.
    const auto world=story.world?story.world:star.worlds.empty()?std::nullopt:std::optional{star.worlds.begin()->first};
    const auto frame=world?catalog_.address(story.system).child(Kind::Planet,*world):catalog_.address(story.system);
    const Random random{caseSeed(key)};
    const auto direction=glm::normalize(glm::dvec3(.4+random.unit(1),.3+random.unit(2),.4+random.unit(3)));
    const auto tangent=glm::normalize(glm::cross(direction,glm::dvec3(0,1,0)));
    const auto up=glm::cross(direction,tangent);
    const double radius=world?std::max(1000.,star.worlds.at(*world).radius):std::max(1000.,star.radius);
    glm::dvec3 centre;
    if(story.port){
        const auto harbour=economy_.portBody(*story.port);
        const auto out=glm::normalize(harbour.position);
        centre=harbour.position+out*8500.;
    }else centre=direction*(radius*2.2+16000.);
    const auto axis=glm::normalize(tangent*(.6+random.unit(6))+up*(random.unit(7)-.5));
    const auto first=centre+axis*(650.+random.unit(8)*1300.)+up*320.;
    const auto second=first+axis*(1100.+random.unit(9)*1100.)+direction*240.;
    const auto third=second-axis*420.+up*(750.+random.unit(10)*650.);
    const auto cause=InvestigationCause(random.bits(12)%3);
    const auto claim=random.bits(13)%3==0?cause:InvestigationCause((unsigned(cause)+1+random.bits(14)%2)%3);
    return {key,frame,centre,{first,second,third},5000.,cause,claim};
}

bool XalariaStories::evidenceComplete(StoryKey key) const {
    const auto* state=investigation(key);
    return !state||state->evidence==15;
}

std::string XalariaStories::investigationTask(StoryKey key) const {
    const auto* state=investigation(key);
    if(!state)return {};
    if(!(state->evidence&1))return "Search the reported area / N scan within 6 km";
    if(!(state->evidence&2))return "N scan the traced object / within 180 m / below 25 m/s";
    if(!(state->evidence&4))return "N recover its recorder / within 75 m / below 8 m/s";
    if(!(state->evidence&8))return generateBase(rootKey(key)).site==StorySite::Survey?
        "N compare the control probe / within 140 m / below 15 m/s":
        "Board the witness's ship or station / N take a statement";
    return "Evidence verified / choose where the finding goes";
}

std::array<std::string,4> XalariaStories::evidenceNotes(StoryKey key) const {
    std::array<std::string,4> notes{
        "Unconfirmed account. Search the reported area for a repeatable signal.",
        "No fragment has been examined at close range.",
        "No recorder has been recovered. The cause remains unverified.",
        "No independent account has been checked against the physical record."
    };
    const auto* state=investigation(key);if(!state)return notes;
    const auto plan=investigationPlan(key);
    if(state->evidence&1)notes[0]="Repeated transmissions narrow the bearing to a detached object.";
    if(state->evidence&2)notes[1]=std::string(objectNames[size_t(key.kind)][0])+" carries a recorder's pairing code.";
    if(state->evidence&4)notes[2]=finding(key.kind,plan.cause);
    if(state->evidence&8){
        notes[3]=generateBase(rootKey(key)).site==StorySite::Survey?
            "The separate control reading ":"The witness claims ";
        if(generateBase(rootKey(key)).site==StorySite::Survey)
            notes[3]+=plan.claim==plan.cause?"supports the recorder's finding.":"conflicts with the first report. The recorder preserves the timing.";
        else notes[3]+=plan.claim==plan.cause?"the same sequence. Their account matches the recorder.":
            "a different sequence. Their account contradicts the recorder.";
    }
    return notes;
}

XalariaStory XalariaStories::generate(StoryKey key) const {
    auto story=generateBase(key);
    const auto* state=investigation(key);
    if(!state||key.chapter)return story;
    const auto notes=evidenceNotes(key);
    if(state->evidence<7){
        story.report="Unverified account. "+story.report;
        story.place=story.world?catalog_.star(story.system).worlds.at(*story.world).name+" / approximate search area":"Reported survey area";
    }
    story.development=investigationTask(key);
    story.followUp=notes[2]+" "+notes[3];
    story.decisions={"Send the verified record to its sponsor","Release the finding for independent review"};
    const auto findingText=state->evidence&4?notes[2]:"The physical record is still incomplete.";
    story.outcomes={findingText+" The sponsor receives the original evidence under a signed chain of custody.",
                    findingText+" An independent review receives the evidence and any contradictory account."};
    return story;
}

std::optional<FlightWaypoint> XalariaStories::investigationWaypoint(StoryKey key) const {
    const auto* state=investigation(key);
    if(!known(key)||key.chapter||!state||evidenceComplete(key)||!actionable(key))return {};
    const auto plan=investigationPlan(key);
    unsigned step=evidenceCount(state->evidence);
    if(step==3&&generateBase(key).site!=StorySite::Survey)return {};
    FlightWaypoint point;
    point.reference=plan.frame;
    point.metres=step==0?plan.centre:plan.clues[std::min(2u,step-1)];
    point.standOff=step==0?1500.:step==2?45.:100.;
    point.label=step==0?"INVESTIGATION / APPROXIMATE SEARCH AREA":
        "INVESTIGATION / "+std::string(objectNames[size_t(key.kind)][std::min(2u,step-1)]);
    return point;
}

std::vector<ActivityBody> XalariaStories::evidenceBodies(const Address& frame) const {
    std::vector<ActivityBody> bodies;
    // Only the tracked case is materialized. Retargeting does not change any position.
    if(!tracked_||tracked_->chapter||result(*tracked_)||!investigation(*tracked_))return bodies;
    const auto plan=investigationPlan(*tracked_);
    if(plan.frame!=frame)return bodies;
    const auto& state=*investigation(*tracked_);
    const unsigned count=generateBase(*tracked_).site==StorySite::Survey?3:2;
    for(unsigned i=0;i<count;++i){
        if(i==1&&(state.evidence&4))continue; // Recovered capsule is aboard the player ship.
        ActivityBody body;
        body.id={frame,investigationActivityTag|(uint32_t(caseSeed(*tracked_))&0x0ffffffcu)|i};
        body.kind=ActivityKind::Debris;body.position=plan.clues[i];body.radius=i==0?11.:i==1?2.6:6.;
        body.orientation=glm::angleAxis(.7+double(i)*.9,glm::normalize(glm::dvec3(.3,.7,.2)));
        body.seed=caseSeed(*tracked_)^i;body.tint=i==1?glm::vec3(1.,.68,.24):glm::vec3(.32,.80,.90);
        body.name=std::string(objectNames[size_t(tracked_->kind)][i]);
        const bool located=i==0?(state.evidence&1):i==1?(state.evidence&2):(state.evidence&4);
        body.status=located?"EVIDENCE / N CLOSE SCAN":"UNIDENTIFIED TRANSPONDER";
        bodies.push_back(std::move(body));
    }
    return bodies;
}

bool XalariaStories::scanEvidence(const StoryContext& context,const Address& frame,glm::dvec3 eye,
                                  glm::dvec3 velocity,double seconds) {
    advance(seconds);
    if(!tracked_||tracked_->chapter||!actionable(*tracked_))return false;
    auto found=investigations_.find(*tracked_);
    if(found==investigations_.end()||found->second.evidence==15||context.system!=tracked_->system)return false;
    auto& state=found->second;
    const auto plan=investigationPlan(*tracked_);
    const unsigned step=evidenceCount(state.evidence);
    bool accepted=false;
    if(step==3&&generateBase(*tracked_).site!=StorySite::Survey)
        accepted=siteReady(generateBase(*tracked_),context);
    else if((context.orbit||context.stellarOrbit)&&frame==plan.frame&&
            std::isfinite(glm::length(eye))&&std::isfinite(glm::length(velocity))){
        const auto target=step==0?plan.centre:plan.clues[step-1];
        const double range=glm::distance(eye,target),speed=glm::length(velocity);
        accepted=range<=(step==0?6000.:step==1?180.:step==2?75.:140.)&&
                 speed<=(step==0?2500.:step==1?25.:step==2?8.:15.);
    }
    if(!accepted){message=investigationTask(*tracked_);return false;}
    state.evidence|=1u<<step;state.collected[step]=seconds_;
    feedSecond_=UINT64_MAX;
    discoveryNotice_="EVIDENCE "+std::to_string(step+1)+"/4 / "+evidenceNotes(*tracked_)[step]+" / F7 CASE FILE";
    message=investigationTask(*tracked_);
    return true;
}

int XalariaStories::factionStanding(XFaction faction) const {
    int points=0;
    for(const auto& receipt:receipts_){
        const auto root=rootKey(receipt.key);
        const auto source=generateBase(root).source;
        if(investigation(root)&&receipt.choice==1&&source!=XFaction::Independent){
            if(faction==XFaction::Independent)++points;
            if(faction==source)points+=investigationPlan(root).cause==InvestigationCause::Failure?1:-1;
        }else if(source==faction)++points;
    }
    for(const auto& contract:economy_.contracts())
        if(contract.state==XContract::State::Delivered&&
           (economy_.faction(contract.origin)==faction||economy_.faction(contract.destination)==faction))++points;
    return points+(economy_.combat()?economy_.combat()->convoys().standing(faction):0);
}
} // namespace infinity
