#pragma once
#include "infinity/Discovery.hpp"
#include <stdexcept>
#include <algorithm>

namespace infinity {
// Native proof uses real procedural sources and sensor/controller transitions.
// It does not alter the playable camera, universe or saved player state.
inline std::array<DiscoveryView,3> discoveryProofViews(const Universe& u){
    const auto system=Address{}.child(Kind::Galaxy,0).child(Kind::System,9);
    const auto catalog=generateDiscoveryPage(u,system);
    if(!catalog)throw std::runtime_error("Discovery proof catalog unavailable");
    const auto it=std::find_if(catalog->sources.begin(),catalog->sources.end(),[](const auto& s){return bool(s.relic);});
    if(it==catalog->sources.end())throw std::runtime_error("Discovery proof has no landable relic");
    auto page=std::make_shared<DiscoveryPage>();page->system=system;page->sources.push_back(*it);
    DiscoveryController controller;controller.page(page);controller.tune(it->frequency-controller.view().frequency);
    ReceiverObserver observer;observer.reference=it->id.planet;
    const auto up=glm::normalize(it->metres),side=tangentFrame(up)*glm::dvec3(1,0,0);
    observer.position=it->metres+up*20000.-side*12000.;observer.forward=glm::normalize(it->metres-observer.position);
    auto tick=[&](unsigned count,bool listen){for(unsigned i=0;i<count;++i){observer.seconds+=.1;controller.update(u,observer,.1,listen,1);}};
    tick(70,true);controller.recordBearing();
    std::array<DiscoveryView,3> views;views[0]=controller.view(true);
    observer.position=it->metres+up*20000.+side*12000.;observer.forward=glm::normalize(it->metres-observer.position);
    tick(1,false);controller.recordBearing();
    if(!controller.record()||!controller.record()->located)throw std::runtime_error("Native discovery proof failed physical triangulation");
    for(unsigned evidence=0;evidence<3;++evidence){
        controller.evidence(evidence);const auto& e=it->relic->evidence[evidence];
        observer.reference=e.address;observer.size=e.requiredSize;
        observer.position=evidence==0?e.metres+up*15.:glm::dvec3(0,0,std::pow(10.,u.describe(e.address).frame.logRadius)*2);
        const auto eye=u.pointRelative(observer.reference,observer.position/std::pow(10.,u.describe(observer.reference).frame.logRadius),it->id.planet,0);
        observer.forward=glm::inverse(u.relative(observer.reference,it->id.planet,0).orientation)*glm::normalize(it->metres-eye);
        tick(45,true);
        if(evidence==1){controller.evidence(2);views[1]=controller.view(true);}
    }
    while(controller.view().theory!=unsigned(it->relic->cause))controller.hypothesis();
    controller.conclude();views[2]=controller.view(true);
    if(!views[2].solved)throw std::runtime_error("Native archaeology proof failed evidence reconstruction");
    return views;
}
} // namespace infinity
