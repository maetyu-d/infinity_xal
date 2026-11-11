// Included beside the native vector cockpit instruments.
static glm::vec3 targetingColor(const TargetingView& v){
    return v.state==TargetLock::Locked?mint:v.state==TargetLock::Acquiring||v.state==TargetLock::Memory?amber:danger;
}
static void drawTargetingReadout(CockpitOverlay& h,const TargetingView& v,const CockpitRect& p){
    const auto color=targetingColor(v);
    fitted(h,v.status,p.x+12,711,292,8.8,color);
    const auto rate=v.closing>=0?"CLOSING ":"OPENING ";
    std::string detail=flightQuantity(v.range)+" / "+rate+flightQuantity(std::abs(v.closing),true);
    if(v.missile&&v.flightTime>=0){char time[40];std::snprintf(time,sizeof(time)," / TGO %.1fS",v.flightTime);detail+=time;}
    fitted(h,detail,p.x+12,729,292,8.8,ice);
    if(v.state==TargetLock::Acquiring||v.state==TargetLock::Memory)bar(h.panels,p.x+12,742,292,v.progress,color);
}
