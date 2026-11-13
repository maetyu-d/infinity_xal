#include "infinity/XalariaNavigation.hpp"
#include "infinity/XalariaEconomy.hpp"
#include <algorithm>
#include <iomanip>
#include <locale>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace infinity {
namespace {
uint32_t containingSystem(Address a,uint32_t fallback){
    while(a.kind()>Kind::System)a=a.parent();
    return a.kind()==Kind::System?uint32_t(a.id()):fallback;
}

std::optional<XalariaWormholeMouth> manualMouth(const Universe& universe, const XalariaCatalog& catalog,
                                              uint32_t system, uint64_t world, uint32_t destination) {
    const auto star = catalog.stars.find(system);
    if (star == catalog.stars.end() || !catalog.stars.contains(destination) ||
        std::find(star->second.wormholes.begin(), star->second.wormholes.end(), destination) ==
            star->second.wormholes.end())
        return {};
    const auto planet = star->second.worlds.find(world);
    if (planet == star->second.worlds.end() || planet->second.stations == 0)
        return {};
    const auto reference = catalog.address(system).child(Kind::Planet, world);
    // Station 1 uses orbital harbour slot 0. The mouth is outside its traffic
    // corridor, on the same bearing as a normal departure from the berth.
    const auto berth = dockingBerth(sampleActivity(universe.describe(reference), 0, 0));
    if (!berth)
        return {};
    const auto outward = berth->orientation * glm::dvec3(0, 0, 1);
    const auto up = berth->orientation * glm::dvec3(0, 1, 0);
    return XalariaWormholeMouth{reference, berth->metres + outward * 8200.,
                               glm::quatLookAtRH(outward, up), 1050., destination};
}
}
XalariaNavigation::XalariaNavigation(const Universe& u,Navigation& n,Pilot& p)
    :universe_(u),navigation_(n),pilot_(p),catalog_(*u.xalaria()){
    view.catalog=&catalog_;view.current=containingSystem(n.anchor,catalog_.stars.begin()->first);
    const auto oylanhai = manualMouth(u, catalog_, 14940, 0, 14538);
    const auto ziksa = manualMouth(u, catalog_, 14538, 1, 14940);
    if (oylanhai && ziksa)
        wormholeMouths_ = std::array{*oylanhai, *ziksa};
    view.selected=view.destination=view.current;view.span=26;
    view.centre={double(view.current%catalog_.grid),double(view.current/catalog_.grid)};
    arrival_=p.instruments().warpArrivalSequence;refresh();
    view.message="SELECT A WORLD OR PLOT A WORMHOLE ROUTE";
    if(const auto target=p.instruments().warpDestination;target&&p.instruments().warp==WarpPhase::Route){
        const auto destination=containingSystem(*target,view.current);
        auto path=catalog_.route(view.current,destination);
        if(!path.empty()){
            route_=std::move(path);target_=*target;phase_=Phase::Transit;
            if(target->kind()==Kind::Planet)world_=target->id();
            view.selected=destination;view.selectedWorld=world_;legRange_=std::max(1.,p.instruments().range);
            view.message="EXISTING FLIGHT TARGET LINKED TO THE MAP";refresh();
        }
    }
}
const XalariaWormholeMouth* XalariaNavigation::wormholeAt(uint32_t system) const {
    if (wormholeMouths_)
        for (const auto& mouth : *wormholeMouths_)
            if (mouth.reference.parent().id() == system)
                return &mouth;
    return nullptr;
}
bool XalariaNavigation::targetWormhole() {
    const auto* mouth = wormholeAt(view.current);
    if (!mouth)
        return false;
    if (!pilot_.instruments().docking.active)
        pause();
    search("");
    selectSystem(mouth->destination);
    view.message = "MANUAL WORMHOLE TO " + catalog_.star(mouth->destination).name + " / FLY THROUGH THE RING";
    view.open = false;
    return true;
}
bool XalariaNavigation::manualFlight() const {
    const auto& flight = pilot_.instruments();
    return flight.warp == WarpPhase::Off && !flight.navigating && !flight.docking.active &&
           !pilot_.ship().landed && !pilot_.ship().shrinking && pilot_.ship().hull > 0;
}
glm::dvec3 XalariaNavigation::eyeInMetres(const Address& reference) const {
    const auto pose = universe_.relative(navigation_.anchor, reference, 0.);
    return pose.position + pose.orientation * (navigation_.cameraPosition() * std::pow(10., navigation_.logDistance));
}
bool XalariaNavigation::crossWormhole(const XalariaWormholeMouth& mouth, glm::dvec3 before, glm::dvec3 after) {
    const auto forward = mouth.orientation * glm::dvec3(0, 0, -1);
    before -= mouth.metres;
    after -= mouth.metres;
    const double start = glm::dot(before, forward), end = glm::dot(after, forward);
    const bool outward = start < 0 && end >= 0;
    const bool inward = start > 0 && end <= 0;
    if (!outward && !inward)
        return false;
    // Use the swept segment so a fast ship cannot skip the disk between frames.
    const auto atPlane = glm::mix(before, after, -start / (end - start));
    if (glm::length(atPlane) + pilot_.collisionRadius() >= mouth.radius)
        return false;
    const auto& exit = *wormholeAt(mouth.destination);
    if (economy_ && !economy_->payWormholeFuel()) {
        view.message = "WORMHOLE NEEDS 8 FUEL / RETURN TO THE STATION";
        return false;
    }
    const auto reverse = glm::angleAxis(std::numbers::pi, glm::dvec3(0, 1, 0));
    const auto entryOrientation = outward ? mouth.orientation : mouth.orientation * reverse;
    const auto rotation = exit.orientation * reverse * glm::inverse(entryOrientation);
    const auto frame = universe_.relative(navigation_.anchor, mouth.reference, 0.).orientation;
    const auto orientation = glm::normalize(rotation * frame * navigation_.orientation);
    const auto velocity = rotation * frame * pilot_.ship().velocity;
    const auto exitForward = exit.orientation * glm::dvec3(0, 0, 1);
    const auto eye = exit.metres + rotation * atPlane + exitForward * (95. + std::min(std::abs(end), 220.));
    pilot_.clearWaypoint();
    navigation_.changeFrame(exit.reference);
    const double scale = universe_.describe(exit.reference).frame.logRadius;
    navigation_.setRenderScale(scale);
    navigation_.orientation = orientation;
    navigation_.offset = eye / std::pow(10., scale) - orientation * glm::dvec3(0, 0, 1);
    navigation_.selected.reset();
    pilot_.resetMotion();
    // Limit the exit speed to station approach speeds while keeping the
    // player's direction of travel and attitude through the opening.
    pilot_.setVelocity(velocity * std::min(1., 160. / std::max(1., glm::length(velocity))));
    route_.clear();
    world_.reset();
    phase_ = Phase::Idle;
    leg_ = 0;
    delay_ = 0;
    search("");
    selectSystem(mouth.destination);
    selectWorld(exit.reference.id());
    view.mode = XalariaMapView::Mode::System;
    view.open = false;
    view.message = "WORMHOLE EXIT / " + catalog_.star(mouth.destination).name + " / " +
                   catalog_.world(exit.reference).name + " STATION APPROACH";
    return true;
}
bool XalariaNavigation::advanceFlight(const HelmInput& input, double dt, const std::shared_ptr<const Geometry>& visible) {
    const auto system = containingSystem(navigation_.anchor, view.current);
    const auto* mouth = manualFlight() ? wormholeAt(system) : nullptr;
    const auto before = mouth ? eyeInMetres(mouth->reference) : glm::dvec3{};
    pilot_.update(input, dt, visible);
    const bool crossed = mouth && manualFlight() && containingSystem(navigation_.anchor, view.current) == system &&
                         crossWormhole(*mouth, before, eyeInMetres(mouth->reference));
    update(dt);
    if (view.wormhole && navigation_.logDistance > 4. &&
        glm::length(eyeInMetres(view.wormhole->reference) - view.wormhole->metres) < 18000.) {
        // Preserve the physical eye position while keeping the approach
        // inside the same near-depth range as local station geometry.
        navigation_.setRenderScale(4.);
    }
    return crossed;
}
Address XalariaNavigation::defaultWorld(uint32_t id) const {
    const auto& s=catalog_.star(id);
    for(const auto& [wid,w]:s.worlds)if(w.primary<0&&w.composition==1&&w.life)return catalog_.address(id).child(Kind::Planet,wid);
    for(const auto& [wid,w]:s.worlds)if(w.primary<0)return catalog_.address(id).child(Kind::Planet,wid);
    return catalog_.address(id);
}
void XalariaNavigation::selectSystem(uint32_t id){
    if(phase_==Phase::Arrived){phase_=Phase::Idle;route_.clear();world_.reset();}
    catalog_.star(id);view.selected=id;view.selectedWorld.reset();view.worldScroll=0;refresh();
}
void XalariaNavigation::selectWorld(uint64_t id){
    if(phase_==Phase::Arrived){phase_=Phase::Idle;route_.clear();world_.reset();}
    const auto& worlds=catalog_.star(view.selected).worlds;worlds.at(id);view.selectedWorld=id;
    const auto at=unsigned(std::distance(worlds.begin(),worlds.find(id)));
    const auto rows = XalariaMapLayout::worldRows;
    if (at<view.worldScroll || at>=view.worldScroll+rows) {
        const auto last = worlds.size()>rows ? unsigned(worlds.size()-rows) : 0u;
        view.worldScroll=std::min(last,at>rows/2?at-rows/2:0u);
    }
}
void XalariaNavigation::selectFromFlight(const Address& a){
    if(a.kind()<Kind::System)return;
    search("");
    auto body=a;while(body.kind()>Kind::Planet)body=body.parent();
    selectSystem(containingSystem(a,view.current));
    if(body.kind()==Kind::Planet){selectWorld(body.id());view.mode=XalariaMapView::Mode::System;}
}
void XalariaNavigation::search(std::string query){
    if(query.size()>96)query.resize(96);view.query=std::move(query);view.matchIndex=0;
    view.matches=catalog_.search(view.query,64);
    if(!view.matches.empty())nextMatch(0);
}
void XalariaNavigation::nextMatch(int direction){
    if(view.matches.empty())return;
    const auto count=long(view.matches.size());view.matchIndex=size_t((long(view.matchIndex)+direction+count)%count);
    selectSystem(view.matches[view.matchIndex]);
    if (const auto world=catalog_.searchWorld(view.selected,view.query)) {
        selectWorld(*world);
        view.mode=XalariaMapView::Mode::System;
    }
}
bool XalariaNavigation::beginLeg(){
    if(route_.empty())return false;
    const bool local=route_.size()==1;
    if(!local&&leg_+1>=route_.size())return false;
    const uint32_t destination=local?route_.front():route_[leg_+1];
    if(!local){const auto& links=catalog_.star(route_[leg_]).wormholes;
        if(std::find(links.begin(),links.end(),destination)==links.end())throw std::logic_error("Invalid committed wormhole route");}
    const bool final=local||leg_+2==route_.size();
    target_=final&&world_?catalog_.address(destination).child(Kind::Planet,*world_):defaultWorld(destination);
    if(target_!=navigation_.anchor)navigation_.select(target_);
    if(economy_&&!economy_->beginVoyage(!local))return false;
    if(!pilot_.warpTo(target_)){if(economy_){economy_->endVoyage();economy_->view.message=pilot_.instruments().phase;}return false;}
    arrival_=pilot_.instruments().warpArrivalSequence;
    legRange_=std::max(1.,pilot_.instruments().range);phase_=Phase::Transit;
    view.message=local?"LOCAL COURSE TO "+universe_.describe(target_).label():
        "WORMHOLE "+std::to_string(leg_+1)+" OF "+std::to_string(route_.size()-1)+" / "+catalog_.star(destination).name;
    return true;
}
bool XalariaNavigation::fly(){
    if(!view.query.empty()&&view.matches.empty()){view.message="NO MATCHING DESTINATION / CLEAR SEARCH OR SELECT A SYSTEM";return false;}
    refresh();const auto plan=catalog_.route(view.current,view.selected);
    if(plan.empty()){view.message="NO WORMHOLE ROUTE TO THIS SYSTEM";return false;}
    if(plan.size()==1&&!view.selectedWorld){view.message="SELECT A WORLD IN THIS SYSTEM";return false;}
    // Validate before replacing the old journey. Browsing is otherwise read-only.
    if(view.selectedWorld)catalog_.star(view.selected).worlds.at(*view.selectedWorld);
    if(economy_)economy_->endVoyage();
    route_=plan;world_=view.selectedWorld;leg_=0;delay_=0;
    if(!beginLeg()){
        phase_=Phase::Paused;pausedPhase_=Phase::Transit;
        view.message=economy_?economy_->view.message:"COURSE READY / CLEAR THE SURFACE OR BERTH, THEN RESUME";refresh();return false;
    }
    view.open=false;refresh();return true;
}
void XalariaNavigation::pause(){
    if(phase_!=Phase::Transit&&phase_!=Phase::BetweenLegs&&phase_!=Phase::FinalApproach)return;
    pausedPhase_=phase_;phase_=Phase::Paused;pilot_.brake();
    view.message="ROUTE PAUSED / THE SHIP IS BRAKING";refresh();
}
bool XalariaNavigation::resume(){
    if(phase_!=Phase::Paused)return false;
    bool success=true;
    if(pausedPhase_==Phase::FinalApproach){pilot_.navigate(target_);phase_=Phase::FinalApproach;}
    else if(pausedPhase_==Phase::BetweenLegs)success=beginLeg();
    else {success=(!economy_||economy_->beginVoyage(route_.size()>1))&&pilot_.warpTo(target_);if(success){if(legRange_<=1)legRange_=std::max(1.,pilot_.instruments().range);arrival_=pilot_.instruments().warpArrivalSequence;phase_=Phase::Transit;}}
    view.message=success?"RESUMING COURSE TO "+universe_.describe(target_).label():economy_?economy_->view.message:"GAIN CLEARANCE BEFORE RESUMING";
    refresh();return success;
}
void XalariaNavigation::cancel(){
    if(economy_)economy_->endVoyage();
    pilot_.brake();route_.clear();world_.reset();phase_=Phase::Idle;leg_=0;delay_=0;
    view.message="COURSE CANCELLED / MANUAL FLIGHT";refresh();
}
void XalariaNavigation::refresh(){
    view.current=containingSystem(navigation_.anchor,view.current);
    if (const auto* mouth = wormholeAt(view.current))
        view.wormhole = *mouth;
    else
        view.wormhole.reset();
    if(plannedFrom_!=view.current||plannedTo_!=view.selected){
        view.route=catalog_.route(view.current,view.selected);plannedFrom_=view.current;plannedTo_=view.selected;
    }
    view.currentWorld.reset();auto a=navigation_.anchor;
    while(a.kind()>Kind::Planet)a=a.parent();
    if(a.kind()==Kind::Planet)view.currentWorld=a.id();
    const auto galaxy=catalog_.address(view.current).parent();
    const auto frame=universe_.describe(navigation_.anchor);
    const auto localEye=navigation_.cameraPosition()*std::pow(10.,navigation_.logDistance-frame.frame.logRadius);
    const auto gp=universe_.pointRelative(navigation_.anchor,localEye,galaxy,universe_.describe(galaxy).frame.logRadius);
    view.ship={gp.x*110+100,gp.z*110+100};
    view.systemShip=universe_.pointRelative(navigation_.anchor,localEye,catalog_.address(view.selected),0);
    view.activeRoute=route_;view.activeLeg=leg_;view.activeWorld=world_;
    if(!view.activeWorld&&!route_.empty()){
        const auto arrival=defaultWorld(route_.back());if(arrival.kind()==Kind::Planet)view.activeWorld=arrival.id();
    }
    view.destination=route_.empty()?view.selected:route_.back();
    view.flying=phase_==Phase::Transit||phase_==Phase::BetweenLegs||phase_==Phase::FinalApproach;
    view.paused=phase_==Phase::Paused;view.finalApproach=phase_==Phase::FinalApproach;view.arrived=phase_==Phase::Arrived;
    view.range=pilot_.instruments().range;view.eta=pilot_.instruments().etaSeconds;
    view.progress=phase_==Phase::FinalApproach||phase_==Phase::Arrived?1.:std::clamp(1-view.range/legRange_,0.,1.);
    view.seconds=pilot_.ship().flightSeconds;
}
void XalariaNavigation::update(double dt){
    if(!std::isfinite(dt)||dt<0)return;
    if(economy_&&phase_==Phase::Transit)economy_->voyageProgress(std::clamp(1-pilot_.instruments().range/legRange_,0.,1.));
    const auto event=pilot_.instruments().warpArrivalSequence;
    if(phase_==Phase::Transit&&event!=arrival_){
        if(economy_)economy_->endVoyage(true);
        arrival_=event;
        if(route_.size()>1)++leg_;
        if(leg_+1<route_.size()){
            phase_=Phase::BetweenLegs;delay_=1.4;
            view.message="ENTERED "+catalog_.star(route_[leg_]).name+" / NEXT WORMHOLE PREPARING";
        }else{phase_=Phase::FinalApproach;view.message="FINAL APPROACH / "+universe_.describe(target_).label();}
    }else if(phase_==Phase::BetweenLegs){
        delay_-=dt;
        if(delay_<=0&&!beginLeg()){pausedPhase_=Phase::BetweenLegs;phase_=Phase::Paused;view.message=economy_?economy_->view.message:"ROUTE PAUSED / GAIN CLEARANCE, THEN RESUME";}
    }else if(phase_==Phase::FinalApproach&&!pilot_.instruments().navigating){
        phase_=Phase::Arrived;view.message="ARRIVED / "+universe_.describe(target_).label();
    }
    refresh();
}
std::optional<double> xalariaBankAngle(glm::dvec3 localUp) {
    if (glm::length(glm::dvec2(localUp)) < .035)
        return std::nullopt;
    return std::atan2(localUp.x, localUp.y);
}

glm::dvec3 XalariaNavigation::bankReference() const {
    const auto& flight = pilot_.instruments();
    if (flight.altitude >= 0)
        return flight.localUp;
    const auto system = catalog_.address(view.current);
    const auto systemToFrame = universe_.relative(system, navigation_.anchor, 0.).orientation;
    return glm::inverse(navigation_.orientation) * systemToFrame * glm::dvec3(0,1,0);
}

double XalariaNavigation::levelRollInput() const {
    const auto& flight = pilot_.instruments();
    if (flight.landed || flight.navigating || flight.docking.active || flight.docking.docked)
        return 0;
    const auto bank = xalariaBankAngle(bankReference());
    return bank ? std::clamp(*bank*1.15 - flight.rollRate*.52, -.68, .68) : 0.;
}

FlightInstruments XalariaNavigation::instruments() const {
    auto t=pilot_.instruments();
    if (t.altitude < 0 && !t.docking.active && !t.docking.docked)
        t.localUp = bankReference();
    if(t.logMetres!=navigation_.logDistance){
        t.logMetres=navigation_.logDistance;
        t.scaleRate=0;
    }
    if(view.wormhole && view.selected==view.wormhole->destination && manualFlight()){
        const auto& mouth=*view.wormhole;
        const auto bearing=mouth.metres-eyeInMetres(mouth.reference);
        const auto frame=universe_.relative(mouth.reference,navigation_.anchor,0.).orientation;
        t.destination="WORMHOLE TO "+catalog_.star(mouth.destination).name;
        t.targetLabel=t.destination;
        t.localTarget=glm::inverse(navigation_.orientation)*frame*bearing;
        t.range=glm::length(bearing);t.targetVisible=t.range>0;t.etaSeconds=-1;
    }else if(phase_==Phase::Idle&&view.selectedWorld&&!t.docking.active&&!t.docking.docked){
        const auto target=catalog_.address(view.selected).child(Kind::Planet,*view.selectedWorld);
        const auto delta=universe_.relative(target,navigation_.anchor,navigation_.logDistance).position-navigation_.cameraPosition();
        t.destination=t.targetLabel=catalog_.star(view.selected).worlds.at(*view.selectedWorld).name;
        t.localTarget=glm::inverse(navigation_.orientation)*delta;
        t.range=glm::length(delta)*std::pow(10.,navigation_.logDistance);t.targetVisible=glm::length(delta)>1e-30;t.etaSeconds=-1;
    }
    return t;
}
std::string XalariaNavigation::serialize() const {
    std::ostringstream s;s.imbue(std::locale::classic());s<<std::setprecision(17);
    s<<"XALARIA_ROUTE 1 "<<catalog_.fingerprint<<' '<<int(phase_)<<' '<<int(pausedPhase_)<<' '<<leg_<<' '<<legRange_<<' '<<delay_
     <<' '<<(world_?std::to_string(*world_):"-")<<' '<<route_.size();
    for(auto id:route_)s<<' '<<id;
    s<<' '<<view.selected<<' '<<(view.selectedWorld?std::to_string(*view.selectedWorld):"-")<<' '<<int(view.mode)
     <<' '<<view.span<<' '<<view.centre.x<<' '<<view.centre.y;
    return s.str();
}
bool XalariaNavigation::restore(std::string_view data){
    try{
        std::istringstream s{std::string(data)};s.imbue(std::locale::classic());
        std::string tag,fingerprint,world,selectedWorld;int version,phase,paused,mode;size_t leg,count;double range,delay,span,cx,cy;uint32_t selected;
        if(!(s>>tag>>version>>fingerprint>>phase>>paused>>leg>>range>>delay>>world>>count)||tag!="XALARIA_ROUTE"||version!=1||fingerprint!=catalog_.fingerprint||count>catalog_.stars.size()||phase<0||phase>5||paused<0||paused>5||!std::isfinite(range)||range<=0||!std::isfinite(delay))return false;
        std::vector<uint32_t> route;for(size_t i=0;i<count;++i){uint32_t id;if(!(s>>id)||!catalog_.stars.contains(id))return false;route.push_back(id);}
        for(size_t i=1;i<route.size();++i){const auto& links=catalog_.star(route[i-1]).wormholes;if(std::find(links.begin(),links.end(),route[i])==links.end())return false;}
        if(!(s>>selected>>selectedWorld>>mode>>span>>cx>>cy)||!catalog_.stars.contains(selected)||mode<0||mode>1||!std::isfinite(span)||span<8||span>230||!std::isfinite(cx)||!std::isfinite(cy))return false;
        std::string extra;if(s>>extra)return false;
        std::optional<uint64_t> wid,preview;
        if(world!="-"){wid=std::stoull(world);if(route.empty()||!catalog_.star(route.back()).worlds.contains(*wid))return false;}
        if(selectedWorld!="-"){preview=std::stoull(selectedWorld);if(!catalog_.star(selected).worlds.contains(*preview))return false;}
        if((!route.empty()&&leg>=route.size())||(route.empty()&&phase!=0))return false;
        if(route.size()>1&&((phase==int(Phase::Transit)||(phase==int(Phase::Paused)&&paused==int(Phase::Transit))||phase==int(Phase::BetweenLegs))&&leg+1>=route.size()))return false;
        phase_=Phase(phase);pausedPhase_=Phase(paused);route_=std::move(route);world_=wid;leg_=leg;legRange_=range;delay_=delay;
        view.selected=selected;view.selectedWorld=preview;view.mode=XalariaMapView::Mode(mode);view.span=span;view.centre={cx,cy};
        if(!route_.empty()){
            const auto destination=phase_==Phase::Transit||(phase_==Phase::Paused&&pausedPhase_==Phase::Transit)?route_[std::min(leg_+1,route_.size()-1)]:route_.back();
            const bool last=destination==route_.back();target_=last&&world_?catalog_.address(destination).child(Kind::Planet,*world_):defaultWorld(destination);
            if(phase_==Phase::Transit&&pilot_.instruments().warp!=WarpPhase::Route){phase_=Phase::Paused;pausedPhase_=Phase::Transit;}
        }
        arrival_=pilot_.instruments().warpArrivalSequence;refresh();
        if(phase_==Phase::Idle&&view.wormhole&&view.selected==view.wormhole->destination)
            view.message="MANUAL WORMHOLE TO "+catalog_.star(view.selected).name+" / FLY THROUGH THE RING";
        else
            view.message=phase_==Phase::Paused?"SAVED ROUTE / RESUME WHEN READY":"SAVED JOURNEY RESTORED";
        return true;
    }catch(const std::exception&){return false;}
}
} // namespace infinity
