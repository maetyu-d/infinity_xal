// Included in Cockpit.cpp beside the existing native vector instruments.
static void drawDiscovery(CockpitOverlay& h,const DiscoveryView& v,double aspect){
    const CockpitRect p{450*aspect-410,130,820,574};
    occlude(h,p);card(h,p,ice);
    fitted(h,v.journal?"DISCOVERY JOURNAL":"SIGNAL RECEIVER",p.x+18,p.y+17,p.w-36,18,pearl);
    char frequency[40];std::snprintf(frequency,sizeof(frequency),"%06.1f MHZ",v.frequency);
    fitted(h,frequency,p.x+18,p.y+47,220,14,mint);
    fitted(h,"SNR "+std::to_string(int(v.quality*100))+"% / "+std::to_string(v.observations)+" BEARINGS",p.x+445,p.y+50,350,10,ice);
    const CockpitRect spectrum{p.x+18,p.y+80,p.w-36,76};
    rect(h.panels,spectrum.x,spectrum.y,spectrum.w,spectrum.h,ink*.7f);
    for(unsigned i=0;i<v.spectrum.size();++i){const double x=spectrum.x+(i+.5)*spectrum.w/64;
        const double height=std::clamp(v.spectrum[i],0.,1.)*57;
        line(h.strokes,x,spectrum.y+spectrum.h-6,x,spectrum.y+spectrum.h-6-height,ice*.75f);}
    const double tuned=spectrum.x+(v.frequency-40)/960*spectrum.w;
    line(h.strokes,tuned,spectrum.y+3,tuned,spectrum.y+spectrum.h-3,mint);
    fitted(h,"40",spectrum.x,spectrum.y+spectrum.h+4,40,8,muted);
    fitted(h,v.loading?"MAPPING LOCAL SOURCES / ASYNC":"[ ] FINE / ARROWS COARSE / TAB NEXT PEAK",spectrum.x+60,spectrum.y+spectrum.h+4,spectrum.w-120,8,muted);
    fitted(h,"1000",spectrum.x+spectrum.w-34,spectrum.y+spectrum.h+4,34,8,muted);
    const double x=p.x+437,width=365;
    fitted(h,v.decoded?v.signature:"UNKNOWN CARRIER / LISTEN TO DECODE",x,p.y+185,width,10,v.decoded?mint:ice);
    fitted(h,v.ship?(v.ship->tactical?"TACTICAL BAND / 6 KM JAM / 8 KM SCAN":"CIVILIAN TRANSPONDER / POSITION SHARED"):v.status,x,p.y+207,width,9.5,v.occluded?amber:ice);
    if(v.located){fitted(h,v.destination,x,p.y+232,width,11,mint);
        fitted(h,v.ship?(v.ship->tactical?"ACTIVE SCAN FIX":"TRANSPONDER POSITION FIX"):"SEARCH ERROR "+flightQuantity(v.uncertainty),x,p.y+253,width,9,muted);}
    else fitted(h,v.ship?"MOVING EMITTER / BEARING ONLY":"MOVE BETWEEN BEARINGS / NO RANGE YET",x,p.y+240,width,9,muted);
    if(v.archaeology&&v.located){
        constexpr std::array<std::string_view,3> names{"FOUNDATIONS","FRACTURE CONTACT","MICRODIFFRACTION"};
        for(unsigned i=0;i<3;++i){const auto color=v.evidence==i?pearl:(v.facts&(1u<<i))?mint:muted;
            fitted(h,std::to_string(i+1)+" "+std::string(names[i])+((v.facts&(1u<<i))?" / READ":""),x,p.y+280+i*19,width,10,color);}
        // Wrap the selected finding at a word boundary; no overlapping popups.
        const size_t length=std::min(size_t(46),v.finding.size());size_t split=v.finding.rfind(' ',length);
        if(split==std::string::npos||split<10)split=length;
        fitted(h,v.finding.substr(0,split),x,p.y+347,width,9,ice);
        if(split<v.finding.size())fitted(h,v.finding.substr(split+1),x,p.y+363,width,9,ice);
        fitted(h,"T / "+v.hypothesis,x,p.y+391,width,10,v.solved?mint:amber);
        fitted(h,"SHIP "+flightQuantity(12*v.size)+" / G "+flightQuantity(12*v.requiredSize),x,p.y+416,width,9,muted);
    }else if(v.ship){
        fitted(h,v.transmission[0],x,p.y+282,width,10,v.ship->tactical?amber:mint);
        fitted(h,v.ship->condition,x,p.y+313,width,10,ice);
        fitted(h,v.ship->systems,x,p.y+337,width,9.5,ice);
        bar(h.panels,x,p.y+360,width,v.ship->scan,mint);
        fitted(h,v.ship->status,x,p.y+379,width,9.5,v.ship->jamming?amber:ice);
        fitted(h,"JAM CAPACITOR "+std::to_string(int(v.ship->capacitor*100))+"% / "+(v.ship->radiating?"RADIATING":"PASSIVE"),x,p.y+404,width,10,v.ship->radiating?amber:mint);
        bar(h.panels,x,p.y+426,width,v.ship->capacitor,v.ship->jamming?amber:ice);
        fitted(h,v.ship->operations,x,p.y+440,width,8.5,muted);
    }else if(v.live){
        for(unsigned i=0;i<3;++i){
            const auto& message=v.transmission[i];size_t split=message.rfind(' ',std::min(size_t(47),message.size()));
            if(split==std::string::npos||split<10)split=std::min(size_t(47),message.size());
            fitted(h,message.substr(0,split),x,p.y+286+i*46,width,10,i==0?mint:ice);
            if(split<message.size())fitted(h,message.substr(split+(message[split]==' ')),x,p.y+303+i*46,width,9,ice);
        }
        fitted(h,v.closed?"CALL CLOSED / RECORD RETAINED":v.accepted?"SOURCE SUPPLIES / DELIVER VIA F2 JOBS":v.signature.starts_with("DISTRESS")?"J APPROACH / 0 RECOVER / CTRL-0 BOARD":"R ACCEPT FUNDED JOB / J APPROACH",x,p.y+432,width,9.5,v.closed?muted:mint);
    }else{
        fitted(h,"1 TUNE THE PEAK AND POINT TOWARD IT",x,p.y+288,width,10,ice);
        fitted(h,"2 HOLD SPACE TO IDENTIFY THE CARRIER",x,p.y+312,width,10,ice);
        fitted(h,"3 ENTER / MOVE / ENTER TO TRIANGULATE",x,p.y+336,width,10,ice);
        fitted(h,"J APPROACH AFTER A VALID SOLUTION",x,p.y+375,width,10,mint);
    }
    if(v.journal){
        fitted(h,"LOCAL DISCOVERIES / UP DOWN SELECT",p.x+18,p.y+185,390,10,ice);
        const size_t first=v.journalSelection>=8?v.journalSelection-7:0;
        for(size_t i=first;i<std::min(v.journalRows.size(),first+8);++i)
            fitted(h,v.journalRows[i],p.x+18,p.y+211+(i-first)*23,390,9.5,i==v.journalSelection?mint:muted);
        if(v.journalRows.empty())fitted(h,"NO DECODED CARRIERS YET",p.x+18,p.y+215,390,10,muted);
    }else if(v.solved){
        fitted(h,"RECONSTRUCTED HISTORY",p.x+18,p.y+185,390,11,mint);
        for(unsigned i=0;i<3;++i)fitted(h,v.chronicle[i],p.x+18,p.y+221+i*42,390,9.5,ice);
        fitted(h,"EVIDENCE FITS THE LOCAL MATERIAL HISTORY",p.x+18,p.y+371,390,9,mint);
    }else{
        fitted(h,"DIRECTION FINDER / BEARING ONLY",p.x+18,p.y+185,390,10,ice);
        const glm::dvec2 c{p.x+204,p.y+284};
        for(unsigned i=0;i<48;++i){const double a=i*2*std::numbers::pi/48,b=(i+1)*2*std::numbers::pi/48;
            line(h.strokes,c.x+73*std::sin(a),c.y-73*std::cos(a),c.x+73*std::sin(b),c.y-73*std::cos(b),muted*.65f);}
        line(h.strokes,c.x,c.y-82,c.x,c.y-65,ice);
        if(v.hasSignal&&v.strength>.01){const auto angle=std::atan2(v.bearing.x,-v.bearing.z);
            const glm::dvec2 tip=c+glm::dvec2(std::sin(angle),-std::cos(angle))*64.;
            line(h.strokes,c.x,c.y,tip.x,tip.y,v.quality>.35?mint:amber);diamond(h.strokes,tip,4,ice);}
        fitted(h,v.bearing.y>.4?"ABOVE / PITCH UP":v.bearing.y<-.4?"BELOW / PITCH DOWN":"LEVEL BEARING",p.x+100,p.y+365,210,8,muted);
        fitted(h,"IDENTIFICATION "+std::to_string(int(v.decode*100))+"%",p.x+18,p.y+388,390,10,ice);
        bar(h.panels,p.x+18,p.y+409,390,v.decode,mint);
    }
    if(v.work>0)bar(h.panels,x,p.y+440,width,v.work,mint);
    fitted(h,v.ship?v.ship->emission:v.reward,p.x+18,p.y+463,p.w-36,9,muted);
    fitted(h,v.ship?"SPACE IDENTIFY / V NEXT SHIP / C EMERGENCY CALLS / L JOURNAL":"SPACE LISTEN / C CALLS / V SHIPS / ENTER BEARING / L JOURNAL",p.x+18,p.y+494,p.w-36,10,mint);
    fitted(h,v.ship?"HOLD S SCAN / X JAM / SEMICOLON JAM IN FLIGHT / J APPROACH":v.live?(v.signature.starts_with("DISTRESS")?"J APPROACH / HOLD 0 RECOVER / CTRL-0 BOARD":"R RESPOND / J APPROACH / F2 DELIVER AT PORT"):"1-3 CLUE / G MATCH SIZE / J APPROACH / T HYPOTHESIS",p.x+18,p.y+518,p.w-36,9.5,ice);
    fitted(h,std::string("P PIN / N NAV BEARINGS ")+(v.assistance?"ON":"OFF")+" / B BRAKE / ESC CLOSE",p.x+18,p.y+550,p.w-36,8.5,muted);
}
static void drawDiscoveryMonitor(CockpitOverlay& h,const DiscoveryView& v){
    const CockpitRect p{24,276,274,46};card(h,p,ice);
    char channel[30];std::snprintf(channel,sizeof(channel),"RADIO %05.1f MHZ",v.frequency);
    fitted(h,v.ship&&(v.ship->jamming||v.ship->capacitor<.99)?"JAM "+std::to_string(int(v.ship->capacitor*100))+"% / "+(v.ship->jamming?"ACTIVE":"STANDBY"):!v.monitoring?"RADIO / "+std::to_string(v.calls)+" CALLS / "+std::to_string(v.ships)+" SHIPS":v.live?v.signature:channel,p.x+10,p.y+8,p.w-20,9,v.ship&&v.ship->jamming?amber:ice);
    const auto direction=v.occluded?"LIMB":v.bearing.z>.4?"AFT":std::abs(v.bearing.x)>.25?(v.bearing.x<0?"PORT":"STBD"):"AHEAD";
    fitted(h,!v.monitoring?"CTRL-F11 / C CALLS / V SHIPS":std::string(direction)+" / "+std::to_string(int(v.quality*100))+"% / "+(v.located?"FIX":"BEARING")+" / CTRL-F11",p.x+10,p.y+25,p.w-20,8,mint);
}
