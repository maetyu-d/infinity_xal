#include "infinity/XalariaStories.hpp"
#include "infinity/XalariaTraffic.hpp"
#include <XalariaStoryData.hpp>
#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <iomanip>
#include <set>
#include <stdexcept>

namespace infinity {
namespace {
constexpr uint64_t editionSeconds = 45*60;
constexpr size_t kindCount = size_t(StoryKind::Count);
constexpr size_t receiptLimit = 100000;

struct StoryOutline {
    std::string headline,premise;
    std::array<std::string,2> choices,outcomes,investigations;
};

const auto& storyLibrary() {
    static const auto library=[](){
        std::array<std::array<StoryOutline,storyVariantsPerKind>,kindCount> result{};
        std::array<std::array<bool,storyVariantsPerKind>,kindCount> present{};
        std::set<std::string> titles;
        for(const auto line:xalariaStoryLibrary){
            if(line.empty()||line.front()=='#')continue;
            std::vector<std::string> fields;
            auto rest=line;
            while(!rest.empty()){
                const auto separator=rest.find('|');
                fields.emplace_back(rest.substr(0,separator));
                rest.remove_prefix(separator==rest.npos?rest.size():separator+1);
                if(fields.back().empty())throw std::runtime_error("Empty story library field");
            }
            if(fields.size()!=6&&fields.size()!=10)throw std::runtime_error("Invalid story library row");
            const auto kind=std::stoul(fields[0]),index=std::stoul(fields[1])-1;
            if(kind>=kindCount||index>=storyVariantsPerKind||present[kind][index]||!titles.insert(fields[2]).second)
                throw std::runtime_error("Duplicate or invalid story library entry");
            present[kind][index]=true;
            auto& outline=result[kind][index];
            outline.headline=fields[2];outline.premise=fields[3];
            outline.choices={fields[4],fields[5]};
            for(unsigned choice=0;choice<2;++choice){
                outline.outcomes[choice]=fields.size()==10?fields[6+choice]:
                    "The signed record of your response, '"+fields[4+choice]+"', reaches the receiving desk.";
                outline.investigations[choice]=fields.size()==10?fields[8+choice]:
                    "Check the record of '"+fields[4+choice]+"' with an independent witness before it enters the final finding.";
            }
        }
        for(const auto& kind:present)
            if(std::find(kind.begin(),kind.end(),false)!=kind.end())
                throw std::runtime_error("Incomplete story library");
        return result;
    }();
    return library;
}

const StoryOutline& storyOutline(StoryKey key) {
    return storyLibrary().at(size_t(key.kind)).at(key.variant-1);
}

uint64_t storyClock(uint32_t system,double seconds) {
    const auto elapsed=std::isfinite(seconds)?uint64_t(std::clamp(seconds,0.,1e12)):0;
    return elapsed+mix64(system^0xa9db57e7u)%editionSeconds;
}

const XalariaWorld* storyWorld(const XalariaStar& star,StoryKey key,const Random& random) {
    if(key.system==phehmyarStation.system&&key.edition==0&&key.kind==StoryKind::Distress)
        if(const auto port=star.worlds.find(phehmyarStation.world);port!=star.worlds.end()&&port->second.stations)
            return &port->second;
    const XalariaWorld* chosen=nullptr;
    int best=-1;
    uint64_t tie=0;
    for(const auto& [id,w]:star.worlds){
        int score=w.primary<0?1:0;
        switch(key.kind) {
        case StoryKind::Distress:
        case StoryKind::Conflict: score+=(w.stations?8:0);break;
        case StoryKind::Disaster: score+=(w.communities?5:0)+(w.stations?8:0);break;
        case StoryKind::Factions: score+=(w.stations?8:0)+(w.mines?3:0);break;
        case StoryKind::Belief: score+=(w.communities?6:0)+(w.stations?4:0);break;
        case StoryKind::Science: score+=(w.ringOuter>0.?3:0)+(w.life?3:0);break;
        case StoryKind::Count: break;
        }
        const auto order=random.bits(id);
        if(score>best||(score==best&&order>tie)){chosen=&w;best=score;tie=order;}
    }
    return chosen;
}

bool loadKey(std::istream& input,StoryKey& key,const XalariaCatalog& catalog,unsigned version) {
    unsigned kind;
    if(!(input>>key.system>>key.edition>>kind)||kind>=kindCount||!catalog.stars.contains(key.system))
        return false;
    key.kind=StoryKind(kind);
    if(version>=2&&(!(input>>key.chapter)||key.chapter>=storyChapterCount))return false;
    if(version>=3&&(!(input>>key.variant)||key.variant>storyVariantsPerKind))return false;
    unsigned live=0;
    if(version>=4&&(!(input>>live)||live>1))return false;
    key.live=live;
    const auto limit=key.live?(1000000000000ULL/1800+4)*4:1000000000000ULL/editionSeconds+1;
    return key.edition<=limit&&(!key.live||key.variant>0);
}
void writeKey(std::ostream& output,StoryKey key) {
    output<<' '<<key.system<<' '<<key.edition<<' '<<unsigned(key.kind)<<' '<<key.chapter<<' '<<key.variant<<' '<<key.live;
}

struct FollowUpBrief {
    std::string_view headline, report, task;
};
constexpr std::array<std::array<FollowUpBrief,2>,kindCount> followUpBriefs{{
    {{{"The repaired circuit drifts out of tolerance",
       "The emergency circuit held, but a new reading suggests the fault will recur under sustained thrust.",
       "Take a second reading from the assigned orbit and identify the load that triggers it."},
      {"The recovery tug hands over the recorder",
       "The tug brought the crew and their flight recorder in. Its recovery log disagrees with the time printed on the distress packet.",
       "Compare the crew's recorder with the tow master's log before the evidence is released."}}},
    {{{"An evacuation corridor needs a second survey",
       "Rescue crews used your corridor. Their last pass recorded a debris stream crossing the far exit.",
       "Record the crossing from the survey orbit so the next convoy can update its chart."},
      {"A second recorder contradicts the incident report",
       "The preserved incident record predates the official alarm. A recovery crew has found an independent recorder.",
       "Take both timestamps to the inquiry desk and attest to the original sequence."}}},
    {{{"Evacuees challenge the convoy register",
       "The civilian corridor held. Several crews crossed with temporary transponders, and the new watch has listed them as combatants.",
       "Cross-check the convoy register with the civilian crews' clearance packets."},
      {"Mediators request the missing watch log",
       "The mediators took your evidence into the ceasefire hearing. The other side has challenged a gap in the station watch log.",
       "Submit the missing interval to the hearing and decide who can inspect it."}}},
    {{{"The shared register faces its first audit",
       "Both delegations signed the register you witnessed. One berth is still being charged twice under the two tariff schedules.",
       "Compare the first invoices at the audit desk and name the register they will use."},
      {"Independent crews ask to verify the ledger",
       "Your witness copy is circulating among independent crews. It shows a discrepancy in the port's fuel allocations.",
       "Meet the verification team and compare its ledger with the port's signed copy."}}},
    {{{"A second listening post answers the vigil",
       "The congregation recognized a reply in your witness recording. Their elders say their own transmitter was silent at the time.",
       "Listen from the marked orbit and compare the reply with the original vigil."},
      {"The splinter congregation opens its archive",
       "The dissidents compared your unedited copy with tapes that had been held back. Several names were cut from the same verse.",
       "Meet the archive keepers and decide how much of the record they can publish."}}},
    {{{"An independent team repeats the observation",
       "The public readings produced a second detection. It appears only when the receivers have opposite bearings.",
       "Repeat the paired measurement at the new site and compare the two bearings."},
      {"The calibration standard is itself an anomaly",
       "Your calibrated result passed internal review. A second programme is seeing the same residual in its reference source.",
       "Take a control reading in the new orbit before the programme updates the catalogue."}}}
}};
constexpr std::array<std::string_view,5> factionTitles{
    "Trade and Manufacturing","Nature Preservation","Freedom Fighters","Rebel Coalition","Independent dispatch"
};
} // namespace

std::string_view storyKindName(StoryKind kind) {
    constexpr std::array names{"Distress","Disaster","War & conflict","Factions","Beliefs & cults","Science"};
    return names.at(size_t(kind));
}
std::string_view storyStageName(StoryStage stage) {
    constexpr std::array names{"First report","Developing","Follow-up"};
    return names.at(size_t(stage));
}

XalariaStories::XalariaStories(const XalariaEconomy& economy,const XalariaCatalog& catalog)
    :economy_(economy),catalog_(catalog) {storyLibrary();}

StoryKey XalariaStories::latest(uint32_t system,StoryKind kind,double seconds) const {
    const auto edition=storyClock(system,seconds)/editionSeconds;
    // Keep the already established opening rescue and its saved ship identity.
    if(system==phehmyarStation.system&&kind==StoryKind::Distress&&edition==0)
        return {system,edition,kind};
    const auto variant=unsigned((mix64(system^mix64(unsigned(kind)))+edition)%storyVariantsPerKind)+1;
    return {system,edition,kind,0,variant};
}

std::optional<StoryKey> XalariaStories::distressAt(uint32_t system,double seconds) const {
    if(tracked_&&tracked_->system==system&&tracked_->kind==StoryKind::Distress&&actionable(*tracked_))return tracked_;
    for(const auto live:localIncidents(system,seconds))
        if(live.kind==StoryKind::Distress&&incident(live,seconds).urgent&&!result(live))return live;
    const auto key=latest(system,StoryKind::Distress,seconds);
    return result(key)?std::nullopt:std::optional{key};
}

StoryStage XalariaStories::stage(StoryKey key,double seconds) const {
    if(key.chapter)return StoryStage::Developing;
    if(key.live){
        const auto phase=incident(key,seconds).phase;
        return phase==IncidentPhase::Reported?StoryStage::FirstReport:
               phase==IncidentPhase::Responding?StoryStage::Developing:StoryStage::FollowUp;
    }
    if(const auto* found=discovery(key))
        return StoryStage(std::min<unsigned>(2,unsigned(std::max(0.,seconds-found->seconds)/(editionSeconds/3))));
    const auto clock=storyClock(key.system,seconds);
    const auto published=key.edition*editionSeconds;
    return StoryStage(std::min<uint64_t>(2,(clock>=published?clock-published:0)/(editionSeconds/3)));
}

XalariaStory XalariaStories::generateBase(StoryKey key) const {
    if(key.chapter)return generateFollowUp(key);
    const auto& star=catalog_.star(key.system);
    const auto scenario=key.variant?mix64(key.variant+0x53544f5259ULL):0;
    const Random random{mix64(uint64_t(key.system)<<32 ^ mix64(key.edition) ^ mix64(uint64_t(key.kind)) ^ scenario)};
    auto locationKey=key;
    if(key.variant)locationKey.edition+=key.variant;
    const auto* world=storyWorld(star,locationKey,random);
    XalariaStory story{};
    story.key=key;
    story.system=key.system;
    if(world)story.world=world->id;
    const auto focus=world?world->name:star.name;
    if(world&&world->stations&&key.kind!=StoryKind::Science){
        const auto index=!key.variant&&key.system==phehmyarStation.system&&key.kind==StoryKind::Distress&&key.edition==0
            ?0u:uint32_t(random.bits(1)%world->stations);
        story.port=XPlace{key.system,world->id,index,false};
    }
    const XPlace affiliation{key.system,world?world->id:0,0,false};
    story.source=story.port?economy_.faction(*story.port):world?economy_.faction(affiliation):XFaction::Independent;
    const auto rival=XFaction((unsigned(story.source)+1+random.bits(2)%4)%5);
    const std::string source(xFactionName(story.source)),other(xFactionName(rival));
    story.site=story.port?key.kind==StoryKind::Distress?StorySite::Vessel:StorySite::Station:StorySite::Survey;
    story.place=story.port?economy_.placeName(*story.port):world?focus+" orbit":star.name+" survey space";
    if(key.variant){
        const auto& outline=storyOutline(key);
        const auto expand=[&](std::string_view text){
            std::string expanded(text);
            const auto replace=[&](std::string_view token,std::string_view value){
                size_t position=0;
                while((position=expanded.find(token,position))!=expanded.npos){
                    expanded.replace(position,token.size(),value);
                    position+=value.size();
                }
            };
            replace("{world}",focus);
            replace("{system}",star.name);
            replace("{sponsor}",source);
            replace("{rival}",other);
            const auto neighbour=star.wormholes.empty()?star.name:
                catalog_.star(star.wormholes[random.bits(5)%star.wormholes.size()]).name;
            replace("{neighbor}",neighbour);
            return expanded;
        };
        story.headline=outline.headline;
        story.report=expand(outline.premise);
        for(size_t i=0;i<2;++i){
            story.decisions[i]=outline.choices[i];
            story.outcomes[i]=outline.outcomes[i];
        }
        story.development="The receiving desk needs a witnessed record from "+story.place+
                          ". Choose which evidence or response you will submit.";
        if(story.site==StorySite::Vessel){
            if(const auto id=vessel(story))
                    story.development=xalariaTrafficCallsign(*id)+" is carrying the witnesses and the incident packet. "+
                        "Get docking clearance and cross to its transfer deck to file your response.";
        }
        story.followUp="The desk is holding the case open for your report. "+expand(outline.investigations[0]);
        return story;
    }
    const auto variant=random.bits(3)%3;

    switch(key.kind){
    case StoryKind::Distress: {
        constexpr std::array faults{"a drive-bus failure","a solvent fire in the cargo spine",
                                    "a failed cooling loop on the crew deck"};
        if(story.port){
            const auto id=vessel(story);
            const std::string ship=id?xalariaTrafficCallsign(*id):"the outbound freighter";
            story.headline="Distress call from "+ship;
            story.report=ship+" reports "+faults[variant]+". "+source+
                         " dispatch has cleared the outer lane and is asking nearby pilots for boarding assistance.";
            story.development="The crew can hold attitude while visitors clamp to the transfer collar. The faulty circuit is isolated behind the transfer bay.";
            story.followUp="The crew has moved everyone to the transfer deck. A tug can take them in tow once the crew sends a signed report.";
            story.decisions={"Help isolate the fault","Call for a tow"};
            story.outcomes={"The crew verifies that the emergency circuit holds. The port logs the vessel safe for inspection.",
                            "The crew authorizes a tow. The port allocates a tug and takes the damaged freighter off the departure list."};
        }else{
            story.headline="Unanswered beacon near "+focus;
            story.report="A repeater above "+focus+" is sending the same seven-second emergency packet. The last frame mentions "+
                         faults[variant]+". No station has acknowledged the call.";
            story.development="The pulse is Doppler-shifted. An in-orbit recording can separate the source from its reflection.";
            story.followUp="The transmissions are weaker, with power only for the location header. The rescue desk is waiting for a fresh bearing.";
            story.decisions={"Send a rescue bearing","Archive the full signal"};
            story.outcomes={"The rescue desk accepts the fix and reserves an emergency frequency for the search.",
                            "The archive retains the full transmission and releases a verified position to future crews."};
        }
        break;
    }
    case StoryKind::Disaster: {
        const std::array causes{"A chain of fragments struck the reflector array above "+focus+".",
                                "A geomagnetic surge took several emergency repeaters offline near "+focus+".",
                                "A pressure vessel broke apart and spread debris through the approach to "+focus+"."};
        story.headline=std::array{"Reflector array breaks up","Emergency repeaters go dark","Debris closes the approach"}[variant];
        story.report=causes[variant]+" "+source+" crews disagree on which route should stay open for evacuation.";
        story.development="Emergency traffic is moving through an improvised corridor. The latest recording puts civilian craft closer to the debris than the first report suggested.";
        story.followUp="The first crews are back, and their logs disagree about where the breakup started. Salvage crews are waiting for a boundary.";
        story.decisions={"Publish the safe corridor","Preserve the incident record"};
        story.outcomes={"Traffic control rebroadcasts your corridor and gives rescue traffic priority.",
                        "Investigators preserve the original recordings and reopen the inquiry with your evidence."};
        break;
    }
    case StoryKind::Conflict: {
        const std::string border=star.wormholes.empty()?"the outer approach":
            "the wormhole lane toward "+catalog_.star(star.wormholes[random.bits(5)%star.wormholes.size()]).name;
        story.headline=std::array{"Convoys caught in a blockade","Ceasefire broken in orbit","Two fleets claim the lane"}[variant];
        const std::array actions{" have imposed rival inspection lines on "," report opposing breaches of the truce near ",
                                 " have ordered escorts into "};
        story.report=source+" and "+other+actions[variant]+border+". Civilian crews are sending copies of both sets of orders.";
        story.development="Patrol orders conflict with the civilian timetable. A protected traffic corridor depends on both sides accepting the same recording.";
        story.followUp="Fighting has displaced crews from the relay station. The parties are looking for a record they can both verify at the next ceasefire hearing.";
        story.decisions={"Relay civilian safe passage","Send evidence to mediators"};
        story.outcomes={"Both dispatch desks acknowledge the corridor. Civilian crews have a shared route out of the contested lane.",
                        "Mediators log your evidence separately from the fleet reports and enter it into the ceasefire hearing."};
        break;
    }
    case StoryKind::Factions: {
        const std::array disputes{"a common emergency fuel reserve","a levy on independent courier crews",
                                  "the right to use the old relay network"};
        story.headline=std::array{"Port council splits over fuel","Courier levy divides the council","Relay charter contested"}[variant];
        story.report=source+" is contesting "+disputes[variant]+" with "+other+" delegates near "+focus+
                     ". Both sides claim the same station charter backs their position.";
        story.development="Two valid copies of the charter have different signatures. A neutral witness can enter either the amended terms or the original text into the register.";
        story.followUp="The council has adjourned and stopped collecting disputed charges. Local crews still need a public decision before they can sign contracts.";
        story.decisions={"Back the shared charter","File an independent record"};
        story.outcomes={"The council ratifies a shared register. Its statement names your ship as the neutral witness.",
                        "Independent crews publish your witness record and keep their own copy of the ledger."};
        break;
    }
    case StoryKind::Belief: {
        constexpr std::array names{"Choir of the Last Echo","Keepers of the Empty Orbit","Order of the Second Wake"};
        constexpr std::array rites{"broadcasts a litany made from missing replies",
                                   "holds an empty berth for ships whose names have been erased",
                                   "begins each vigil by counting old engine wakes"};
        const std::string sect(names[variant]);
        constexpr std::array customs{"a tape with every third word missing",
                                     "an empty berth registered to a ship that vanished generations ago",
                                     "a star chart drawn only from the trails of passing ships"};
        story.headline=sect+" calls a vigil";
        story.report="The "+sect+" "+rites[variant]+" near "+focus+". Tonight's vigil centres on "+customs[random.bits(17)%3]+
                     ". A breakaway congregation claims the rite is hiding a message inside the public traffic record.";
        story.development="The congregation has offered a witness copy of the ceremony. Dissidents want proof that the channel stays open to anyone who leaves.";
        story.followUp="A second vigil has split off from the first. Both groups claim the same recording and have asked an outsider to hold it.";
        story.decisions={"Witness the vigil","Give the dissidents a copy"};
        story.outcomes={"The archive records the ritual with the congregation's consent and preserves the names of its sponsors.",
                        "The dissidents receive an unedited copy. Both groups keep access to the public record."};
        break;
    }
    case StoryKind::Science: {
        constexpr std::array programmes{"Quiet Baseline","Night Garden","Far Echo"};
        constexpr std::array signals{"a periodic radio null that moves with the receiver",
                                     "a repeating absorption band unlike the standard catalogue models",
                                     "an echo that arrives before the predicted return"};
        const std::string programme(programmes[variant]);
        story.headline="Programme "+programme+" requests a second reading";
        const std::string instrument=world&&world->ringOuter>0.?"The rings give them a reference for the timing. ":
            world&&world->life?"The team is comparing orbital readings with a surface biosphere survey. ":"";
        story.report="The "+programme+" team reports "+signals[variant]+" near "+focus+". "+instrument+
                     "The programme wants a second ship to observe from a separate orbit.";
        story.development="The instruments agree on the time but disagree on the bearing. An independent navigation timestamp can resolve the discrepancy.";
        story.followUp="The second measurement window is open. The team has agreed to mark the result provisional until it has your calibration record.";
        story.decisions={"Share raw observations","Submit a calibrated result"};
        story.outcomes={"The programme releases the raw readings with your ship's position and keeps the anomaly open to independent analysis.",
                        "The programme accepts your calibration and adds the observation to its reference catalogue."};
        break;
    }
    case StoryKind::Count: break;
    }
    return story;
}

XalariaStory XalariaStories::generateFollowUp(StoryKey key) const {
    auto parent=key;
    --parent.chapter;
    auto first=key;
    first.chapter=0;
    const auto root=generate(first);
    const auto previous=parent.chapter?generate(parent):root;
    const auto branch=result(first).value();
    const auto handoff=result(parent).value();
    const bool final=key.chapter+1==storyChapterCount;
    const auto& brief=followUpBriefs.at(size_t(key.kind)).at(branch);
    const auto preference=handoff?XFaction::Independent:root.source;
    const bool field=!final&&(key.kind==StoryKind::Science||
                     (branch==0&&(key.kind==StoryKind::Disaster||key.kind==StoryKind::Distress||
                                  key.kind==StoryKind::Belief)));
    const Random random{mix64(uint64_t(key.system)<<32 ^ mix64(key.edition) ^
                             mix64(31*key.chapter+5*unsigned(key.kind)+branch+13*handoff)^
                             (key.variant?mix64(key.variant):0))};
    XalariaStory story{};
    story.key=key;
    int best=-1;
    uint64_t tie=0;
    const auto consider=[&](uint32_t system,std::optional<uint64_t> world,std::optional<XPlace> port,int score) {
        const auto rank=random.bits(mix64(system)^mix64(world.value_or(0))^
                                    mix64(port?uint64_t(port->station)+1:0));
        if(score<best||(score==best&&rank<=tie))return;
        story.system=system;
        story.world=world;
        story.port=port;
        best=score;
        tie=rank;
    };
    const auto& exits=catalog_.star(previous.system).wormholes;
    const auto examine=[&](uint32_t system){
        const auto& candidate=catalog_.star(system);
        // An official review returns toward the original sponsor when there
        // is a direct flight. Public evidence goes to another dispatch desk.
        const bool preferredDestination=handoff==0?system==key.system:system!=key.system;
        const int direction=final&&preferredDestination?24:0;
        consider(system,std::nullopt,std::nullopt,direction);
        for(const auto& [id,world]:candidate.worlds){
            const int orbitScore=direction+(world.primary<0?2:1)+
                                 (field&&key.kind==StoryKind::Science&&world.ringOuter>0.?2:0);
            consider(system,id,std::nullopt,orbitScore);
            if(field)continue;
            for(unsigned index=0;index<world.stations;++index){
                const XPlace port{system,id,index,false};
                consider(system,id,port,orbitScore+8+(economy_.faction(port)==preference?5:0));
            }
        }
    };
    if(!key.variant&&key.chapter==1&&key.kind==StoryKind::Distress&&root.port==phehmyarStation&&branch==1&&
       std::find(exits.begin(),exits.end(),eunhiaStation.system)!=exits.end()&&
       economy_.orbitalStationId(eunhiaStation)){
        story.system=eunhiaStation.system;
        story.world=eunhiaStation.world;
        story.port=eunhiaStation;
    }else if(exits.empty()){
        examine(previous.system);
    }else{
        for(const auto system:exits)examine(system);
    }
    const auto& target=catalog_.star(story.system);
    story.site=story.port?StorySite::Station:StorySite::Survey;
    story.source=story.port?economy_.faction(*story.port):preference;
    story.place=story.port?economy_.placeName(*story.port):
        story.world?target.worlds.at(*story.world).name+" orbit":target.name+" survey space";
    const std::string desk(factionTitles.at(size_t(story.source)));
    if(!final){
        if(key.variant){
            const auto& outline=storyOutline(key);
            story.headline="Evidence review / "+std::string(outline.headline);
            story.report=root.outcomes[branch]+" The case began at "+root.place+".";
            story.development=std::string(outline.investigations[branch])+" "+desk+" will receive the report.";
        }else{
            story.headline=brief.headline;
            story.report=std::string(brief.report)+" The original report came from "+root.place+".";
            story.development=std::string(brief.task)+" "+desk+" will receive the report.";
        }
        story.decisions={"File with the sponsor","Release a public copy"};
        story.outcomes={desk+" accepts the evidence for a formal review and requests you as a witness.",
                        "A public copy goes into the dispatch archive. A separate reviewer asks you to certify it."};
    }else{
        story.headline=key.variant?(handoff?"Public review / ":"Sponsor review / ")+root.headline:
            handoff?"Public evidence reaches its final review":"The sponsor calls its closing review";
        story.report=handoff?"A public copy of your evidence has arrived from ":"The sponsor received the signed file from ";
        story.report+=previous.place+". The reviewer is comparing it with the original "+root.place+" report.";
        story.development="Check the final record at the assignment site. "+desk+
                          " has left the disputed passages open for your decision.";
        story.decisions={"Sign the final record","File a dissenting note"};
        story.outcomes={desk+" closes the case with your verified record and sends the finding back to "+
                        catalog_.star(key.system).name+".",
                        "Your dissent is preserved with the finding. "+catalog_.star(key.system).name+
                        " receives both versions before the case closes."};
    }
    return story;
}

std::optional<ActivityId> XalariaStories::vessel(const XalariaStory& story) const {
    return story.site==StorySite::Vessel&&story.port?xalariaOrbitalFreighter(economy_,*story.port):std::nullopt;
}

bool XalariaStories::siteReady(const XalariaStory& story,const StoryContext& context) const {
    if(context.system!=story.system)return false;
    switch(story.site){
    case StorySite::Vessel: {
        const auto target=vessel(story);
        return target&&context.boardedVessel==target;
    }
    case StorySite::Station:return story.port&&context.boardedStation==story.port;
    case StorySite::Survey:return story.world?context.orbit==story.world:context.stellarOrbit;
    }
    return false;
}

bool XalariaStories::ready(const XalariaStory& story,const StoryContext& context) const {
    return siteReady(story,context)&&(story.key.chapter||evidenceComplete(story.key));
}

std::optional<StoryKey> XalariaStories::continuation(StoryKey key) const {
    if(key.chapter+1>=storyChapterCount||!result(key))return {};
    ++key.chapter;
    return key;
}

void XalariaStories::show(uint32_t selectedSystem,double seconds) {
    system=selectedSystem;
    open=true;
    showPage(StoryPage::Feed,seconds);
}
void XalariaStories::showPage(StoryPage next,double seconds) {
    page_=next;
    sheet_=0;
    row=0;
    message.clear();
    rebuild(seconds);
}
void XalariaStories::nextPage(double seconds) {
    constexpr std::array order{StoryPage::Nearby,StoryPage::News,StoryPage::Feed,
                               StoryPage::FollowUps,StoryPage::Journal,StoryPage::Archive};
    const auto found=std::find(order.begin(),order.end(),page_);
    showPage(order[(size_t(found-order.begin())+1)%order.size()],seconds);
}
size_t XalariaStories::sheetCount() const {
    if(page_==StoryPage::Nearby)return std::max<size_t>(1,(nearby_.size()+4)/5);
    if(page_==StoryPage::Archive||page_==StoryPage::Feed)return std::max<size_t>(1,(pageKeys().size()+kindCount-1)/kindCount);
    if(page_==StoryPage::News)return std::max<size_t>(1,(news_.size()+kindCount-1)/kindCount);
    const size_t count=page_==StoryPage::FollowUps?pending_.size():receipts_.size()+bool(tracked_);
    return std::max<size_t>(1,(count+kindCount-1)/kindCount);
}
void XalariaStories::changeSheet(int delta,double seconds) {
    const auto next=size_t(std::clamp(int(sheet_)+delta,0,int(sheetCount()-1)));
    if(next==sheet_)return;
    sheet_=next;
    row=0;
    message.clear();
    rebuild(seconds);
}
std::vector<StoryKey> XalariaStories::pageKeys() const {
    std::vector<StoryKey> keys;
    if(page_==StoryPage::FollowUps){
        keys.assign(pending_.rbegin(),pending_.rend());
    }else if(page_==StoryPage::Journal){
        keys.reserve(receipts_.size()+bool(tracked_));
        if(tracked_)keys.push_back(*tracked_);
        for(auto it=receipts_.rbegin();it!=receipts_.rend();++it)keys.push_back(it->key);
    }else if(page_==StoryPage::Archive||page_==StoryPage::Feed){
        for(auto it=discoveries_.rbegin();it!=discoveries_.rend();++it)
            if(page_==StoryPage::Archive||it->key.system==system)keys.push_back(it->key);
    }else if(page_==StoryPage::Nearby){
        for(const auto& entry:nearby_)keys.push_back(entry.key);
    }else if(page_==StoryPage::News){
        for(const auto& entry:news_)keys.push_back(entry.key);
    }
    return keys;
}
void XalariaStories::openReport(StoryKey key,double seconds) {
    if(!known(key))return;
    page_=result(key)?StoryPage::Journal:key.chapter?StoryPage::FollowUps:StoryPage::Feed;
    if(page_==StoryPage::Feed)system=key.system;
    const auto keys=pageKeys();
    const auto found=std::find(keys.begin(),keys.end(),key);
    const size_t index=found==keys.end()?0:size_t(found-keys.begin());
    sheet_=index/kindCount;
    row=index%kindCount;
    message.clear();
    rebuild(seconds);
}
void XalariaStories::refresh(double seconds) {
    advance(seconds);
    const auto selectedKey=selected()?std::optional{selected()->key}:std::nullopt;
    if(open&&feedSecond_!=uint64_t(seconds_))rebuild(seconds);
    if(selectedKey){
        const auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& entry){return entry.key==selectedKey;});
        if(found!=entries_.end())row=size_t(found-entries_.begin());
    }
}
void XalariaStories::rebuild(double seconds) {
    advance(seconds);
    feedSecond_=uint64_t(seconds_);
    rebuildKnownFeeds();
    sheet_=std::min(sheet_,sheetCount()-1);
    entries_.clear();
    {
        const auto keys=pageKeys();
        const size_t pageSize=page_==StoryPage::Nearby?5:kindCount;
        for(size_t i=sheet_*pageSize;i<keys.size()&&entries_.size()<pageSize;++i)
            entries_.push_back(generate(keys[i]));
    }
    row=entries_.empty()?0:std::min(row,entries_.size()-1);
}
void XalariaStories::select(size_t next) {
    if(next<entries_.size()){row=next;message.clear();}
}
void XalariaStories::step(int delta){
    if(!entries_.empty())select(size_t(std::clamp(int(row)+delta,0,int(entries_.size()-1))));
}
const XalariaStory* XalariaStories::selected() const {
    return row<entries_.size()?&entries_[row]:nullptr;
}

std::optional<unsigned> XalariaStories::result(StoryKey key) const {
    const auto found=outcomes_.find(key);
    return found!=outcomes_.end()?std::optional{found->second}:std::nullopt;
}

bool XalariaStories::track(StoryKey key) {
    if(!actionable(key)||(key.chapter&&std::find(pending_.begin(),pending_.end(),key)==pending_.end()))
        return false;
    tracked_=key;
    message="Lead tracked in Journal";
    return true;
}

bool XalariaStories::resolve(StoryKey key,const StoryContext& context,unsigned choice,XalariaEconomy& ledger) {
    if(tracked_!=key||!actionable(key)||choice>1||receipts_.size()>=receiptLimit)return false;
    const auto story=generate(key);
    if(!ready(story,context))return false;
    outcomes_.emplace(key,choice);
    receipts_.push_back({key,choice,seconds_});
    interventionTimes_.emplace(key,seconds_);
    tracked_.reset();
    std::erase(pending_,key);
    const auto next=continuation(key);
    if(next)pending_.push_back(*next);
    const unsigned engineering=key.kind==StoryKind::Science||key.kind==StoryKind::Disaster||key.kind==StoryKind::Distress;
    const auto* evidence=investigation(key);
    const int64_t bonus=!key.chapter&&evidence?(choice?
        (investigationPlan(key).cause==InvestigationCause::Failure?8000:16000):4000):0;
    ledger.rewardDispatch((story.site==StorySite::Vessel?64000:story.site==StorySite::Station?42000:24000)+bonus,
                          engineering?(choice?7:10):0,engineering?0:(choice?10:7));
    if(open)openReport(key,seconds_);
    message=next?"Report filed. A follow-up is ready.":"Case closed. The final decision is in your journal.";
    return true;
}

std::string XalariaStories::serialize() const {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output<<std::setprecision(17)<<"XALARIA_STORIES 6 "<<bool(tracked_);
    if(tracked_)writeKey(output,*tracked_);
    output<<' '<<receipts_.size();
    for(const auto& receipt:receipts_){writeKey(output,receipt.key);output<<' '<<receipt.choice<<' '<<receipt.seconds;}
    output<<' '<<seconds_;
    output<<' '<<discoveries_.size();
    for(const auto& discovery:discoveries_){
        writeKey(output,discovery.key);output<<' '<<unsigned(discovery.origin)<<' '<<discovery.seconds;
    }
    output<<' '<<encounters_.size();
    for(const auto token:encounters_)output<<' '<<token;
    output<<' '<<investigations_.size();
    for(const auto& [key,state]:investigations_){
        writeKey(output,key);output<<' '<<state.evidence;
        for(const auto when:state.collected)output<<' '<<when;
    }
    return output.str();
}

bool XalariaStories::restore(std::string_view saved) {
    if(saved.empty())return true;
    std::istringstream input{std::string(saved)};
    input.imbue(std::locale::classic());
    std::string tag;
    unsigned version,tracked;
    if(!(input>>tag>>version>>tracked)||tag!="XALARIA_STORIES"||version<1||version>6||tracked>1)return false;
    std::optional<StoryKey> active;
    if(tracked){
        active.emplace();
        if(!loadKey(input,*active,catalog_,version))return false;
    }
    size_t count;
    if(!(input>>count)||count>receiptLimit)return false;
    std::vector<Receipt> receipts;
    std::map<StoryKey,unsigned> outcomes;
    std::map<StoryKey,double> times;
    receipts.reserve(count);
    for(size_t i=0;i<count;++i){
        Receipt receipt{};
        if(!loadKey(input,receipt.key,catalog_,version)||!(input>>receipt.choice)||receipt.choice>1||
           active==receipt.key)return false;
        if(version>=4&&(!(input>>receipt.seconds)||!std::isfinite(receipt.seconds)||
                       receipt.seconds<0.||receipt.seconds>1e12))return false;
        if(receipt.key.chapter){
            auto parent=receipt.key;
            --parent.chapter;
            if(!outcomes.contains(parent))return false;
        }
        if(!outcomes.emplace(receipt.key,receipt.choice).second)return false;
        times.emplace(receipt.key,receipt.seconds);
        receipts.push_back(receipt);
    }
    double seconds=0.;
    if(version>=4&&(!(input>>seconds)||!std::isfinite(seconds)||seconds<0.||seconds>1e12))return false;
    for(const auto& receipt:receipts)if(receipt.seconds>seconds)return false;
    std::vector<StoryDiscovery> discoveries;
    std::map<StoryKey,size_t> discovered;
    std::set<uint64_t> encounters;
    const auto remember=[&](StoryKey key,StoryOrigin origin,double when){
        key.chapter=0;
        if(discovered.contains(key))return false;
        discovered.emplace(key,discoveries.size());discoveries.push_back({key,origin,when});return true;
    };
    if(version>=5){
        size_t total;
        if(!(input>>total)||total>receiptLimit)return false;
        for(size_t i=0;i<total;++i){
            StoryKey key{};unsigned origin;double when;
            if(!loadKey(input,key,catalog_,version)||key.chapter||!(input>>origin>>when)||
               origin>unsigned(StoryOrigin::PreviousJourney)||!std::isfinite(when)||when<0.||when>seconds||
               !remember(key,StoryOrigin(origin),when))return false;
        }
        if(!(input>>total)||total>receiptLimit)return false;
        for(size_t i=0;i<total;++i){uint64_t token;if(!(input>>token)||!encounters.insert(token).second)return false;}
        const auto recorded=[&](StoryKey key){key.chapter=0;return discovered.contains(key);};
        if(active&&!recorded(*active))return false;
        for(const auto& receipt:receipts)if(!recorded(receipt.key))return false;
    }else{
        for(const auto& receipt:receipts)remember(receipt.key,StoryOrigin::PreviousJourney,receipt.seconds);
        if(active)remember(*active,StoryOrigin::PreviousJourney,0.);
    }
    std::map<StoryKey,InvestigationState> investigations;
    if(version>=6){
        size_t total;if(!(input>>total)||total>receiptLimit)return false;
        for(size_t i=0;i<total;++i){
            StoryKey key{};InvestigationState state{};
            if(!loadKey(input,key,catalog_,version)||key.chapter||!discovered.contains(key)||
               !(input>>state.evidence)||(state.evidence!=0&&state.evidence!=1&&state.evidence!=3&&
                state.evidence!=7&&state.evidence!=15))return false;
            double previous=discoveries[discovered.at(key)].seconds;
            for(unsigned step=0;step<4;++step){
                auto& when=state.collected[step];
                if(!(input>>when)||!std::isfinite(when)||when<0.||when>seconds)return false;
                if(state.evidence&(1u<<step)){if(when<previous)return false;previous=when;}
                else if(when!=0.)return false;
            }
            if(outcomes.contains(key)&&state.evidence!=15)return false;
            if(!investigations.emplace(key,state).second)return false;
        }
    }else{
        std::set<StoryKey> established;
        if(active){auto root=*active;root.chapter=0;established.insert(root);}
        for(const auto& receipt:receipts){auto root=receipt.key;root.chapter=0;established.insert(root);}
        for(const auto& found:discoveries)
            if(!established.contains(found.key)&&found.origin!=StoryOrigin::PreviousJourney)
                investigations.emplace(found.key,InvestigationState{});
    }
    input>>std::ws;
    if(!input.eof())return false;
    std::vector<StoryKey> pending;
    for(const auto& receipt:receipts){
        auto next=receipt.key;
        if(++next.chapter<storyChapterCount&&!outcomes.contains(next))pending.push_back(next);
    }
    if(active&&active->chapter&&std::find(pending.begin(),pending.end(),*active)==pending.end())return false;
    tracked_=active;
    receipts_=std::move(receipts);
    outcomes_=std::move(outcomes);
    interventionTimes_=std::move(times);
    pending_=std::move(pending);
    seconds_=seconds;
    discoveries_=std::move(discoveries);discovered_=std::move(discovered);encounters_=std::move(encounters);
    discoveryNotice_.clear();listeningSystem_.reset();visitingStation_.reset();
    listeningSeconds_=visitingSeconds_=0.;observationSecond_=-1.;feedSecond_=UINT64_MAX;
    investigations_=std::move(investigations);evidenceOpen=false;
    incidentSites_.clear();
    return true;
}
} // namespace infinity
