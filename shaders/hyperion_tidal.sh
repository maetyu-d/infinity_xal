#ifndef HYPERION_TIDAL
#define HYPERION_TIDAL

// Long swells carry smaller ripples in the same direction. Filter each
// wavelength in screen space so distant crests do not turn into a grid.
vec3 tidalSwells(vec2 p,float seconds,float footprint){
 vec2 q=p+vec2(.32*sin(p.y*.39+seconds*.34),.58*sin(p.x*.76-seconds*.26));
 vec2 a=vec2(.62,1.65),b=vec2(-1.24,2.71),c=vec2(2.36,5.67),d=vec2(-3.58,9.21);
 vec4 phase=vec4(dot(q,a),dot(q,b),dot(q,c),dot(q,d))-seconds*vec4(.81,1.13,1.63,2.04);
 vec4 frequency=vec4(length(a),length(b),length(c),length(d));
 vec4 weight=vec4(.19,.075,.023,.009)*(1.-smoothstep(vec4(.45),vec4(1.65),frequency*footprint));
 vec4 slope=cos(phase)*weight;
 return vec3(dot(sin(phase),weight),a*slope.x+b*slope.y+c*slope.z+d*slope.w);
}

// A curved current is evaluated in the celestial field, with its own axis
// and tangent. The gaps and swells share that frame as the ship rolls.
vec4 tidalCurrent(vec3 ray,vec3 axis,vec3 course,vec2 shape,float phase,float seconds){
 vec3 side=cross(axis,course);
 float along=dot(ray,course),crossing=dot(ray,side);
 float centre=dot(ray,axis)-shape.x
              +.17*sin(along*3.6+crossing*2.0+phase)
              +.067*sin(crossing*5.8-along*2.7+phase*.71);
 float width=shape.y*(.87+.20*cos(along*7.6+phase)+.13*sin(crossing*5.1-phase));
 float edge=centre/width;
 float water=1.-smoothstep(.74,1.10,abs(edge));
 vec2 p=vec2(along*8.4+crossing*4.1,centre*16.+1.5*sin(crossing*3.7+phase));
 p+=vec2(phase*.48,phase*.29);
 float footprint=max(length(dFdx(p)),length(dFdy(p)));
 vec3 swell=tidalSwells(p,seconds,footprint);
 vec3 across=axis-ray*dot(ray,axis);
 across*=inversesqrt(max(dot(across,across),.001));
 vec3 tangent=cross(ray,across);
 vec3 normal=normalize(-ray*.72+across*(clamp(edge,-1.3,1.3)*.63-swell.z)-tangent*swell.y);
 vec3 reflected=reflect(ray,normal);
 vec3 light=normalize(vec3(-.31,.29,-.92));
 float facing=clamp(dot(normal,-ray),0.,1.);
 float fresnel=.024+.72*pow(1.-facing,4.);
 float reflection=max(dot(reflected,light),0.);
 // Break reflections into moving glints and let smaller flecks collect on
 // the high side of each swell.
 vec2 windDrift=vec2(.12,.63)*seconds;
 float wind=strataNoise(vec3(p*vec2(2.45,5.6)-windDrift,4.4+phase),vec3(0.));
 vec2 drift=p+vec2(wind*.36,swell.x*.82);
 float spray=strataNoise(vec3(drift*vec2(8.1,16.6)-windDrift*2.4,16.8+phase),vec3(0.));
 float surf=strataNoise(vec3(drift*vec2(3.4,7.6)-windDrift*1.45,21.+phase),vec3(0.));
 float chop=smoothstep(.31,.68,wind);
 float swellLight=clamp(.40+swell.x*1.44+dot(normal,light)*.39+(wind-.5)*.20,0.,1.);
 vec3 ocean=mix(vec3(.0016,.0065,.019),vec3(.016,.067,.132),swellLight);
 ocean*=mix(.40,1.08,smoothstep(-.20,.12,swell.x));
 ocean+=vec3(.055,.107,.175)*fresnel*(.41+.59*pow(reflection,4.));
 ocean+=vec3(.25,.42,.57)*pow(reflection,35.)*(.18+.82*chop);
 float fringe=exp(-abs(swell.x-.117+(surf-.5)*.11)*21.);
 float shimmer=pow(clamp(reflection,0.,1.),4.)*.56+.44*chop;
 ocean+=vec3(.048,.112,.20)*fringe*shimmer*smoothstep(.26,.66,surf);
 float crest=smoothstep(.115,.202,swell.x+(wind-.5)*.068);
 float lace=1.-abs(spray*2.-1.);
 float foam=crest*smoothstep(.42,.72,wind*.58+lace*.42)*(1.-smoothstep(.060,.24,footprint));
 float foamSpill=crest*smoothstep(.49,.81,spray)*smoothstep(.38,.66,surf)*.40;
 ocean=mix(ocean,vec3(.55,.72,.83),min(1.,foam*(.37+.60*lace)+foamSpill));
 return vec4(ocean,water);
}

vec3 tidalSky(vec3 ray,vec3 space,float phase,float seconds){
 float dust=dot(space,vec3(.27,.58,.15));
 vec3 sky=vec3(.0015,.0036,.008)+vec3(.34,.53,.79)*dust*.19;
 vec3 farAxis=normalize(vec3(.58,.72,-.38)),farCourse=normalize(cross(farAxis,vec3(.31,-.64,.79)));
 vec3 midAxis=normalize(vec3(-.68,.39,.63)),midCourse=normalize(cross(midAxis,vec3(.72,.56,.19)));
 vec3 nearAxis=normalize(vec3(.23,-.46,.86)),nearCourse=normalize(cross(nearAxis,vec3(.89,.41,.18)));
 vec4 distant=tidalCurrent(ray,farAxis,farCourse,vec2(.16,.24),phase*6.+2.4,seconds*.77);
 vec4 middle=tidalCurrent(ray,midAxis,midCourse,vec2(-.07,.31),phase*6.+4.7,seconds*.87);
 vec4 nearby=tidalCurrent(ray,nearAxis,nearCourse,vec2(.18,.28),phase*6.+.8,seconds);
 sky=mix(sky,distant.rgb*.48,distant.a);
 sky=mix(sky,middle.rgb*.74,middle.a);
 return mix(sky,nearby.rgb,nearby.a);
}
#endif
