static void drawDefenceCard(CockpitOverlay& h,const CombatAwarenessView& v,double y){
    const CockpitRect p{24,y,274,65};card(h,p,v.missiles?danger:ice);
    const std::string mode=!v.automatic?"MANUAL":v.crewReady?"AUTO":"AUTO / CREW NEEDED";
    fitted(h,"DEFENCE / "+mode,p.x+10,p.y+8,p.w-20,9,ice);
    fitted(h,"DECOYS "+std::to_string(v.decoys)+"/6 / PD "+(v.pointDefenceArmed?std::to_string(v.pointDefence)+" READY":"HOLD")+
        (v.cooldown>0?" / CYCLING":""),p.x+10,p.y+25,p.w-20,8.5,v.decoys?mint:amber);
    fitted(h,v.defence.empty()?"BACKSLASH BURST / F1 DEFENCE KEYS":v.defence,p.x+10,p.y+44,p.w-20,8,muted);
}
