#ifndef HYPERION_CABIN
#define HYPERION_CABIN
#include "hyperion_built_finish.sh"
#include "hyperion_canopy_shadow.sh"
uniform vec4 u_stationWindows[6]; // Bridge, panorama, bridge profile, opening heights, star, and wormhole.
uniform vec4 u_stationAnnexes[12]; // Two rooms: floor bounds, light strips, task light, and three footprints.
uniform vec4 u_stationDoors[2]; // Door positions and width; lifted fractions and vertical travel.

vec3 cabinWindowLight(){
 return vec3(1.,.47,.21)*u_regionLook[1].y
       +vec3(.62,1.,.43)*u_regionLook[0].x
       +vec3(.35,.74,1.)*u_regionLook[1].x
       +vec3(1.,.48,.72)*u_regionLook[1].z;
}

float cabinBox(vec2 p,vec2 halfExtent,float soft){
 vec2 edge=halfExtent-abs(p);
 return smoothstep(0.,soft,min(edge.x,edge.y));
}

float cabinFinishNoise(vec3 p,vec3 origin){
 float footprint=max(length(dFdx(p)),length(dFdy(p)));
 // Gloss relief is filtered before Nyquist, rather than after it shimmers.
 float resolved=1.-smoothstep(.20,.68,footprint);
 return mix(.5,strataNoiseSample(p,origin),resolved);
}

float cabinWeave(vec3 p,vec3 N,float frequency){
 vec3 weight=abs(N);
 weight/=max(.001,weight.x+weight.y+weight.z);
 vec3 f=p*frequency;
 vec3 thread=sin(f);
 vec3 cells=vec3(thread.y*thread.z,thread.x*thread.z,thread.x*thread.y);
 vec3 footprint=fwidth(f);
 vec3 visible=vec3(1.)-smoothstep(vec3(.56),vec3(2.6),
                                   vec3(max(footprint.y,footprint.z),max(footprint.x,footprint.z),
                                        max(footprint.x,footprint.y)));
 return .5+dot(cells*visible,weight)*.5;
}

float cabinTread(vec3 p){
 vec2 tile=floor(p.xz/.045);
 vec2 cell=fract(p.xz/.045)-.5;
 cell.x*=mix(-1.,1.,mod(tile.x+tile.y,2.));
 vec2 axis=vec2(.7071,.7071);
 vec2 bar=vec2(dot(cell,axis),dot(cell,vec2(-axis.y,axis.x)));
 float footprint=max(length(dFdx(p.xz)),length(dFdy(p.xz)))/.045;
 return cabinBox(bar,vec2(.34,.098),.066)*(1.-smoothstep(.16,.78,footprint));
}

float cabinContactShade(vec3 p){
 float height=p.z>1.16?p.y+1.10:p.y+.82;
 float hull=p.z>3.72?1.64:p.z>1.83&&p.z<3.40&&abs(p.x)>.81?2.11:.79;
 float rim=exp(-abs(hull-abs(p.x))*14.);
 float lower=exp(-abs(height)*10.);
 float rib=0., ceilingRim=0.;
 if(p.z>3.72){
  float z=min(min(abs(p.z-3.95),abs(p.z-4.97)),min(abs(p.z-6.13),abs(p.z-7.26)));
  rib=exp(-z*23.)*smoothstep(1.0,1.58,abs(p.x));
  ceilingRim=rim*exp(-abs(p.y-1.19)*14.);
 }else if(p.z>1.17){
  float z=min(min(abs(p.z-2.03),abs(p.z-3.22)),abs(p.z-3.49));
  rib=exp(-z*22.)*exp(-abs(abs(p.x)-.79)*12.);
  ceilingRim=rim*exp(-abs(p.y-.97)*14.);
 }
 return 1.-rim*lower*.48-rib*.24-ceilingRim*.21;
}

float cabinWindowTransmission(vec3 p,vec3 light){
 // Trace to the approximate canopy envelope. Roof, sill and aft wall
 // stop direct stellar light. Soften the frame edges across the glass.
 vec3 inv=sign(light+vec3(.00001))/max(abs(light),vec3(.00001));
 vec3 limits=mix(vec3(-1.025,-.82,-1.462),vec3(1.025,.56,1.14),step(vec3(0.),light));
 vec3 t=max(vec3(0.),(limits-p)*inv);
 vec3 front=p+light*t.z,side=p+light*t.x;
 float aperture=0.;
 // The proxy envelope stands in for a deep, bevelled pressure frame.
 // Feather its edge over that depth, then widen by the stellar disc.
 // Fully covered shadows and the unobstructed key keep their contrast.
 float sourceRadius=clamp(u_cabinSun[1].w,.002,.055);
 if(t.z<=min(t.x,t.y) && light.z<0.)
  aperture=cabinBox(front.xy-vec2(0.,.09),vec2(.86,.425),.12+t.z*sourceRadius);
 else if(t.x<min(t.y,t.z))
  aperture=cabinBox(side.zy-vec2(-.13,.12),vec2(1.17,.405),.12+t.x*sourceRadius);
 if(aperture<.001)return 0.;
 return aperture*canopyFrameSun(p,light);
}

// Reflect the fixed roof light strips and the console glow into nearby
// materials. The intersections remain in hull coordinates during freelook.
vec3 cabinFixtureReflection(vec3 p,vec3 ray,float roughness){
 float blur=.018+roughness*.21;
 float roofHeight=.584-p.y, consoleHeight=p.y+.415;
 vec3 roof=p+ray*(max(0.,roofHeight)/max(.045,ray.y));
 vec3 lower=p+ray*(max(0.,consoleHeight)/max(.06,-ray.y));
 float footprint=clamp(fwidth(roof.x)*.65,0.,.18);
 float strip=exp(-abs(abs(roof.x)-.59)/(blur+footprint))*(blur/(blur+footprint));
 strip*=cabinBox(vec2(0.,roof.z+.14),vec2(1.,.89),.18);
 float central=cabinBox(vec2(roof.x,roof.z+.30),vec2(.16,.19),.075+blur);
 float dashboard=cabinBox(lower.xz-vec2(0.,-1.17),vec2(.39,.13),.06+blur);
 float consoles=cabinBox(vec2(abs(lower.x),lower.z)-vec2(.85,-.51),vec2(.16,.28),.075+blur);
 float roofVisible=smoothstep(.035,.12,ray.y)*smoothstep(0.,.035,roofHeight);
 float consoleVisible=smoothstep(.045,.14,-ray.y)*smoothstep(0.,.035,consoleHeight);
 float collar=0.;
 if(ray.z<-.045 && p.z> -1.33){
  vec3 hit=p+ray*((-1.338-p.z)/ray.z);
  float width=.006+blur*.55;
  collar=exp(-pow((hit.y-.491)/width,2.))*(.006/width)*
         smoothstep(.28,.33,abs(hit.x))*(1.-smoothstep(.69,.73,abs(hit.x)));
 }
 return vec3(1.,.78,.50)*(strip*.83+central*.27)*roofVisible/(1.+roughness*2.)
       +vec3(.15,.72,.64)*(dashboard*.34+consoles*.25)*consoleVisible
       +vec3(1.,.58,.25)*collar*2.4;
}

vec3 deckFixtureReflection(vec3 p,vec3 ray,float roughness){
 vec3 c=vec3(0.);
 bool cargo=p.z>=3.72;
 float blur=.016+roughness*.18;
 if(ray.y>.025){
  float t=((cargo?1.106:.917)-p.y)/ray.y;
  if(t>0.){
   vec3 hit=p+t*ray;
   float nearest=clamp(4.45+floor((hit.z-4.45)/1.04+.5)*1.04,4.45,6.53);
   float s=cargo?cabinBox(vec2(abs(hit.x)-.76,hit.z-nearest),vec2(.07+blur,.24+blur),blur):
                  cabinBox(vec2(abs(hit.x)-.49,hit.z-2.50),vec2(.041+blur,1.04),blur);
   c+=mix(vec3(.90,.88,.73),vec3(1.,.55,.22),cargo?1.:0.)*s*.88;
  }
 }
 if(ray.y<-.035 && p.y>-.98){
  vec3 hit=p+ray*((-1.076-p.y)/ray.y);
  float rail=cabinBox(vec2(abs(hit.x)-.53,hit.z-4.53),vec2(.022+blur,2.81),blur);
  c+=vec3(.10,.69,.82)*rail*.66;
 }
 return c;
}

vec3 deckLamp(vec3 p,vec3 N,vec3 V,vec3 body,float roughness,float metal,
              vec3 position,vec3 colour,float intensity){
 vec3 offset=position-p;
 float range2=dot(offset,offset);
 vec3 L=surfaceUnit(offset,vec3(0.,1.,0.));
 // Fixtures have finite lenses. Broaden their lobe by their apparent size
 // before evaluating it, so small lights cannot become flashing pinpoints.
 float effective=pow(min(1.,pow(max(.28,roughness),4.)+.0012/(.16+range2)),.25);
 return colour*builtBrdf(body,N,V,L,effective,metal,vec3(1.,0.,.13),0.)*
        intensity/(.28+range2)*.90;
}

vec3 cabinStripLight(vec3 p,vec3 N,vec3 V,vec3 body,float rough,float metal,
                    vec3 grain,float machining,float side,vec3 colour,float power){
 // Integrate the actual long roof strip for diffuse illumination. The
 // reflected aperture below is continuous, rather than eight point glints.
 vec3 diffuse=vec3(0.);
 for(int tap=0;tap<8;++tap){
  float z=mix(-.98,.72,(float(tap)+.5)/8.);
  vec3 delta=vec3(side,.578,z)-p;
  float range2=dot(delta,delta);
  float facing=max(dot(N,surfaceUnit(delta,vec3(0.,1.,0.))),0.);
  diffuse+=body*(1.-metal)*facing/(.28+range2);
 }
 vec3 R=reflect(-V,N);
 float height=.578-p.y;
 float distance=height/max(R.y,.025);
 vec3 hit=p+R*distance;
 // Analytic aperture integration with roughness-dependent width gives a
 // long, unbroken fixture reflection even during roll and strong boost.
 float blur=max(.018,abs(distance)*rough*rough*.60);
 float across=blur*(1.+machining*.45),along=blur*(1.-machining*.26);
 float x=(atan((side+.036-hit.x)/across)-atan((side-.036-hit.x)/across))/3.141593;
 float z=(atan((.72-hit.z)/along)-atan((-.98-hit.z)/along))/3.141593;
 float aperture=max(0.,x*z)*smoothstep(.025,.13,R.y)*smoothstep(0.,.08,height);
 float nv=max(0.,dot(N,V));
 vec3 f0=mix(vec3(.04),body,metal);
 vec3 fresnel=f0+(max(vec3(1.-rough),f0)-f0)*pow(1.-nv,5.);
 return colour*power*(diffuse*(1.55/8.)+fresnel*aperture*2.7);
}

bool stationInterior(vec3 p){
 return u_stationWindows[0].w>.0 && p.x<=-1.64;
}

vec3 stationLampColour(){
 return vec3(1.,.73,.47)*u_regionLook[1].y
       +vec3(.95,1.,.61)*u_regionLook[0].x
       +vec3(.63,.89,1.)*u_regionLook[1].x
       +vec3(1.,.88,.56)*u_regionLook[1].z;
}

bool insideStationAnnex(vec2 p,vec4 bounds){
 return bounds.z>bounds.x && all(greaterThanEqual(p,bounds.xy)) && all(lessThanEqual(p,bounds.zw));
}

int stationAnnexSlot(vec3 p){
 if(insideStationAnnex(p.xz,u_stationAnnexes[0]))return 0;
 if(insideStationAnnex(p.xz,u_stationAnnexes[6]))return 6;
 return -1;
}

float stationAnnexContact(vec3 p,int room){
 float nearness=1.-smoothstep(.025,.41,abs(p.y+1.10));
 if(nearness<=0.)return 1.;
 vec4 bounds=u_stationAnnexes[room];
 vec2 edge=min(p.xz-bounds.xy,bounds.zw-p.xz);
 float wall=exp(-max(0.,min(edge.x,edge.y))*16.);
 float foot=0.;
 for(int i=3;i<6;i++){
  vec4 footprint=u_stationAnnexes[room+i];
  if(footprint.z>footprint.x){
   vec2 gap=max(vec2(0.),max(footprint.xy-p.xz,p.xz-footprint.zw));
   foot=max(foot,exp(-length(gap)*11.));
  }
 }
 return 1.-nearness*max(wall*.30,foot*.60);
}

vec3 stationAnnexCeilingColour(vec4 strip){
 return mix(stationLampColour(),vec3(.79,.88,1.),strip.w>.5?.13:.54);
}

vec3 stationAnnexFixtures(vec3 p,vec3 N,vec3 V,vec3 body,float roughness,float metal,int room){
 vec4 bounds=u_stationAnnexes[room],strip=u_stationAnnexes[room+1];
 vec4 task=u_stationAnnexes[room+2];
 vec3 ceiling=stationAnnexCeilingColour(strip);
 float z=clamp(p.z,bounds.y+strip.z,bounds.w-strip.z);
 float roofMask=1.-smoothstep(strip.x-.06,strip.x+.09,p.y);
 vec3 lit=body*(vec3(.069,.063,.067)+ceiling*.046);
 lit+=roofMask*(deckLamp(p,N,V,body,roughness,metal,vec3(bounds.x+strip.y,strip.x,z),ceiling,.79)+
                deckLamp(p,N,V,body,roughness,metal,vec3(bounds.z-strip.y,strip.x,z),ceiling,.79));
 if(task.w>=0.){
  bool table=task.w<.01;
  vec3 source=vec3(task.xy,clamp(p.z,task.z-task.w,task.z+task.w));
  float below=source.y-p.y;
  if(below>0.){
   float facing=below/max(.001,length(source-p));
   float cone=smoothstep(table?.49:.40,table?.86:.80,facing)*smoothstep(.019,.078,below);
   float reach=1.-smoothstep(table?.72:1.0,table?1.58:1.91,length(source-p));
   vec3 tint=table?vec3(1.,.74,.37):vec3(.68,.85,1.);
   lit+=deckLamp(p,N,V,body,roughness,metal,source,tint,cone*reach*(table?1.16:1.45));
  }
 }
 return lit;
}

bool stationBridgeInterior(vec3 p){
 vec4 bridge=u_stationWindows[0];
 return p.x>bridge.x && p.x<bridge.y && abs(p.z-bridge.z)<bridge.w+.014;
}

bool stationPanoramaInterior(int room){
 if(room<0)return false;
 return u_stationAnnexes[room+1].w>.5 && u_stationWindows[1].y>u_stationWindows[1].x;
}

float stationRayExit(float clearance,float outward){
 return outward>.0005?max(0.,clearance)/outward:1e6;
}

vec3 stationBridgeArchNormal(vec3 p){
 vec4 bridge=u_stationWindows[0],profile=u_stationWindows[2];
 vec2 radii=vec2(profile.z-profile.y,bridge.w);
 vec2 slope=vec2(p.y-profile.y,p.z-bridge.z)/(radii*radii);
 return normalize(vec3(0.,slope));
}

float stationBridgeTransmission(vec3 p,vec3 ray,float angular){
 vec4 bridge=u_stationWindows[0],profile=u_stationWindows[2];
 vec4 sizes=u_stationWindows[3];
 float z=p.z-bridge.z;
 float stop=min(stationRayExit(ray.x>0.?bridge.y-p.x:p.x-bridge.x,abs(ray.x)),
                stationRayExit(p.y-sizes.w,-ray.y));
 float side=stationRayExit(bridge.w-sign(ray.z)*z,abs(ray.z));
 if(p.y+ray.y*side>profile.y)side=1e6;
 vec2 radii=vec2(profile.z-profile.y,bridge.w);
 vec2 origin=vec2(p.y-profile.y,z)/radii;
 vec2 slope=ray.yz/radii;
 float quadratic=dot(slope,slope),projection=dot(origin,slope);
 float discriminant=projection*projection-quadratic*(dot(origin,origin)-1.);
 float roof=(-projection+sqrt(max(0.,discriminant)))/max(1e-8,quadratic);
 if(discriminant<0. || roof<0. || p.y+roof*ray.y<profile.y || quadratic<1e-8)roof=1e6;
 float depth=min(side,roof);
 if(depth>=stop || depth>=1e5)return 0.;
 vec3 hit=p+ray*depth;
 float incidence=depth==side?abs(ray.z):dot(ray,stationBridgeArchNormal(hit));
 float edge=min(hit.y-profile.x,min(hit.x-bridge.x,bridge.y-hit.x));
 float blur=clamp(.008+depth*angular,.008,.23);
 float rib=abs(fract((bridge.y-hit.x)/sizes.z+.5)-.5)*sizes.z;
 return smoothstep(.025-blur,.025+blur,edge)*smoothstep(profile.w-blur,profile.w+blur,rib)*
        mix(.53,.91,clamp(incidence,0.,1.));
}

float stationPanoramaTransmission(vec3 p,vec3 ray,float angular){
 vec4 window=u_stationWindows[1];
 vec2 height=u_stationWindows[3].xy;
 float side=sign(window.z-window.w);
 if(ray.z*side<=.0005)return 0.;
 float depth=max(0.,(window.z-p.z)/ray.z);
 vec3 hit=p+ray*depth;
 float width=window.y-window.x;
 float edge=min(min(hit.x-window.x,window.y-hit.x),min(hit.y-height.x,height.y-hit.y));
 float paneWidth=width/3.;
 float rib=abs(fract((hit.x-window.x)/paneWidth+.5)-.5)*paneWidth;
 float blur=clamp(.008+depth*angular,.008,.26);
 return smoothstep(.042-blur,.042+blur,edge)*smoothstep(.038-blur,.038+blur,rib)*
        mix(.53,.91,abs(ray.z));
}

float stationGlassTransmission(vec3 p,vec3 ray,float angular,bool bridge){
 if(bridge)return stationBridgeTransmission(p,ray,angular);
 return stationPanoramaTransmission(p,ray,angular);
}

float stationSkyPatch(vec3 p,vec3 N,vec3 centre,vec3 outward,float area){
 vec3 offset=centre-p;
 float range2=max(dot(offset,offset),.018);
 vec3 direction=offset*inversesqrt(range2);
 float facing=max(dot(direction,outward),0.);
 float weight=clamp(area*facing/(range2+area*.63),0.,1.);
 return weight*(.12+.68*max(dot(N,direction),0.));
}

vec3 stationExteriorBeam(vec3 N,vec3 V,vec3 L,vec3 body,float roughness,float metal){
 float facing=max(dot(N,L),0.);
 vec3 halfVector=normalize(V+L+vec3(1e-7));
 float specular=pow(max(dot(N,halfVector),0.),mix(95.,9.,roughness))*(1.-roughness*.60);
 return facing*(body*(.83-metal*.36)+mix(vec3(.045),body,metal)*specular*.56);
}

vec3 stationWindowLight(vec3 p,vec3 N,vec3 V,vec3 L,vec3 body,float roughness,float metal,int room){
 bool bridge=stationBridgeInterior(p);
 if(!bridge && !stationPanoramaInterior(room))return vec3(0.);
 vec3 sky=cabinWindowLight();
 float diffuse=0.;
 if(bridge){
  vec4 bounds=u_stationWindows[0],profile=u_stationWindows[2];
  float x=clamp(p.x,bounds.x+.31,bounds.y-.31);
  diffuse+=stationSkyPatch(p,N,vec3(x,profile.z,clamp(p.z,bounds.z-bounds.w*.35,bounds.z+bounds.w*.35)),
                           vec3(0.,1.,0.),1.42);
  for(int side=-1;side<=1;side+=2){
   diffuse+=stationSkyPatch(p,N,vec3(x,mix(profile.x,profile.y,.66),bounds.z+float(side)*bounds.w),
                            vec3(0.,0.,float(side)),1.37)*.66;
  }
 }else{
  vec4 window=u_stationWindows[1];
  vec2 height=u_stationWindows[3].xy;
  float side=sign(window.z-window.w);
  float paneWidth=(window.y-window.x)/3.;
  for(int pane=0;pane<3;pane++){
   float x=window.x+(float(pane)+.5)*paneWidth;
   diffuse+=stationSkyPatch(p,N,vec3(x,mix(height.x,height.y,.64),window.z),
                            vec3(0.,0.,side),paneWidth*(height.y-height.x))*.76;
  }
 }
 vec3 lit=body*sky*diffuse;
 vec4 star=u_stationWindows[4];
 if(max(star.r,max(star.g,star.b))>.001){
  float glass=stationGlassTransmission(p,L,star.w,bridge);
  lit+=star.rgb*glass*stationExteriorBeam(N,V,L,body,roughness,metal);
 }
 vec4 wormhole=u_stationWindows[5];
 if(wormhole.w>.001){
  float glass=stationGlassTransmission(p,wormhole.xyz,wormhole.w*.62,bridge);
  float power=clamp(wormhole.w*6.2,0.,1.65);
  lit+=sky*power*glass*stationExteriorBeam(N,V,wormhole.xyz,body,roughness,metal);
 }
 return lit;
}

vec3 stationWindowReflection(vec3 p,vec3 ray,float roughness,int room){
 bool bridge=stationBridgeInterior(p);
 if(roughness>.66 || (!bridge && !stationPanoramaInterior(room)))return vec3(0.);
 float glass=stationGlassTransmission(p,ray,.018+roughness*.085,bridge);
 return cabinWindowLight()*glass*(.54-roughness*.42);
}

vec3 stationBridgeRailColour(){
 return mix(vec3(1.,.88,.72),stationLampColour(),.23);
}

vec3 stationBridgeRailReflection(vec3 p,vec3 ray,float roughness){
 if(abs(ray.z)<.001)return vec3(0.);
 vec4 bounds=u_stationWindows[0],profile=u_stationWindows[2];
 float rail=bounds.z+sign(ray.z)*(bounds.w-.065);
 float depth=(rail-p.z)/ray.z;
 if(depth<=0.)return vec3(0.);
 vec3 hit=p+ray*depth;
 float endMask=smoothstep(bounds.x,bounds.x+.10,hit.x)*(1.-smoothstep(bounds.y-.10,bounds.y,hit.x));
 return stationBridgeRailColour()*exp(-abs(hit.y-profile.x+.071)/(.014+roughness*.11))*endMask*.57;
}

vec3 stationFixtureReflection(vec3 p,vec3 ray,float roughness,int room){
 vec3 window=stationWindowReflection(p,ray,roughness,room);
 if(stationBridgeInterior(p))return window+stationBridgeRailReflection(p,ray,roughness);
 if(room>=0){
  vec4 bounds=u_stationAnnexes[room],strip=u_stationAnnexes[room+1];
  float height=strip.x-p.y;
  if(ray.y<=.04 || height<=0.)return window;
  vec3 hit=p+ray*height/ray.y;
  float edge=min(abs(hit.x-bounds.x-strip.y),abs(hit.x-bounds.z+strip.y));
  float endMask=smoothstep(bounds.y+strip.z-.10,bounds.y+strip.z,hit.z)*
                (1.-smoothstep(bounds.w-strip.z,bounds.w-strip.z+.10,hit.z));
  return window+stationAnnexCeilingColour(strip)*exp(-edge/(.028+roughness*.15))*endMask*.90;
 }
 float height=1.74-p.y;
 if(ray.y<=.04 || height<=0.)return window;
 vec3 hit=p+ray*height/ray.y;
 float rib=abs(fract((hit.x+3.7)/1.55)-.5)*1.55;
 float blur=.036+roughness*.19;
 float strips=exp(-rib/blur)*exp(-abs(abs(hit.z-6.87)-1.16)*2.2);
 return window+stationLampColour()*strips*.77;
}

vec3 doorwaySpill(vec3 p,vec3 N,vec3 V,vec3 body,float roughness,float metal,
                 vec4 portal,vec3 nearSide,vec3 farSide,float power){
 float distanceFromDoor=abs(p.x-portal.x);
 float floorHeight=u_stationWindows[3].w;
 float opening=portal.w-floorHeight;
 if(distanceFromDoor>2.65 || opening<.022 || abs(p.z-portal.y)>portal.z+1.18)return vec3(0.);
 float side=p.x>=portal.x?1.:-1.;
 vec3 source=vec3(portal.x-side*.61,floorHeight+min(1.28,opening*.72),portal.y);
 vec3 hit=mix(p,source,distanceFromDoor/(distanceFromDoor+.61));
 float edge=min(portal.z-abs(hit.z-portal.y),min(hit.y-floorHeight,portal.w-hit.y));
 float blur=.042+distanceFromDoor*.10;
 float aperture=smoothstep(-blur,blur,edge)*smoothstep(.035,.60,opening);
 float reach=1.-smoothstep(.57,2.65,distanceFromDoor);
 vec3 tint=side>0.?farSide:nearSide;
 return deckLamp(p,N,V,body,roughness,metal,source,tint,power*.59*aperture*reach);
}

vec3 cabinThresholdLight(vec3 p,vec3 N,vec3 V,vec3 body,float roughness,float metal){
 vec4 tunnel=u_stationWindows[0];
 if(tunnel.w<=0.)return vec3(0.);
 vec4 doors=u_stationDoors[0],travel=u_stationDoors[1];
 float floorHeight=u_stationWindows[3].w;
 float shipHead=min(travel.z,floorHeight+travel.x*travel.w);
 float bridgeHead=min(travel.z,floorHeight+travel.y*travel.w);
 vec3 ship=vec3(1.,.68,.38);
 vec3 chamber=stationLampColour();
 vec3 bridge=mix(cabinWindowLight(),chamber,.33);
 vec3 station=mix(chamber,vec3(.78,.91,1.),.46);
 vec3 lit=doorwaySpill(p,N,V,body,roughness,metal,vec4(doors.x,doors.z,doors.w,shipHead),
                      ship,chamber,.83);
 lit+=doorwaySpill(p,N,V,body,roughness,metal,vec4(doors.y,doors.z,doors.w,bridgeHead),
                   chamber,bridge,1.12);
 lit+=doorwaySpill(p,N,V,body,roughness,metal,vec4(tunnel.x,tunnel.z,tunnel.w-.035,u_stationWindows[2].z-.095),
                   bridge,station,.83);
 return lit;
}

vec3 cabinDeckLighting(vec3 p,vec3 N,vec3 V,vec3 L,vec3 body,float roughness,float metal,int room){
 if(stationInterior(p)){
  vec3 window=stationWindowLight(p,N,V,L,body,roughness,metal,room);
  if(stationBridgeInterior(p)){
   vec4 bounds=u_stationWindows[0],profile=u_stationWindows[2];
   float rail=bounds.w-.065,y=profile.x-.071;
   float x=clamp(p.x,bounds.x+.10,bounds.y-.10);
   vec3 colour=stationBridgeRailColour();
   return window+body*.044+
          deckLamp(p,N,V,body,roughness,metal,vec3(x,y,bounds.z-rail),colour,.34)+
          deckLamp(p,N,V,body,roughness,metal,vec3(x,y,bounds.z+rail),colour,.34);
  }
  if(room>=0)return stationAnnexFixtures(p,N,V,body,roughness,metal,room)+window;
  // Pressure modules place their light ribs on one 1.55 m socket grid.
  float x=p.x > -3.70 ? -2.67 : -4.475+floor((p.x+4.475)/1.55+.5)*1.55;
  float offset=p.z-6.87;
  float spread=smoothstep(.57,1.43,abs(offset));
  float lateral=max(spread*1.13,abs(offset)-.51);
  vec3 socket=vec3(x,mix(1.06,1.88,spread),6.87+(offset>0.?1.:-1.)*lateral);
  float lowPool=exp(-abs(p.y+1.10)*2.4-abs(abs(offset)-.69)*2.1);
  vec3 lamp=stationLampColour();
  vec3 floorBounce=cabinWindowLight()*lowPool*.22;
  return body*(vec3(.030,.027,.027)+lamp*.024+floorBounce)+
         deckLamp(p,N,V,body,roughness,metal,socket,lamp,1.60)+
         window;
 }
 vec3 lit=body*vec3(.012,.021,.032);
 float side=p.x<0.?-1.:1.;
 if(p.z>=3.72){
  // The loading lights are repeated along the two ceiling rails.
  float nearest=clamp(4.45+floor((p.z-4.45)/1.04+.5)*1.04,4.45,6.53);
  lit+=deckLamp(p,N,V,body,roughness,metal,vec3(side*.76,1.106,nearest),
                vec3(1.,.66,.35),1.02);
  lit+=deckLamp(p,N,V,body,roughness,metal,vec3(-side*.76,1.106,nearest),
                vec3(.55,.78,1.),.19);
  float projected=1.-smoothstep(.14,.48,abs(p.z-5.55+(p.x-side*1.64)*.33));
  float floorPool=exp(-abs(p.y+1.10)*2.2-abs(p.x-side*1.64)*.90);
  lit+=body*cabinWindowLight()*projected*floorPool*.20*max(.12,N.y);
 }else if(abs(p.x)>.78 && p.z>1.83 && p.z<3.40){
  bool berth=side<0.;
  vec3 lamp=berth?vec3(-1.978,.278,clamp(p.z,2.12,3.11)):
                  vec3(1.54,-.49,clamp(p.z,2.18,3.06));
  vec3 tint=berth?vec3(1.,.51,.24):vec3(.24,.78,1.);
  lit+=deckLamp(p,N,V,body,roughness,metal,lamp,tint,berth?.67:.58);
  lit+=deckLamp(p,N,V,body,roughness,metal,vec3(side*1.55,.885,2.61),
                berth?vec3(1.,.67,.38):vec3(.43,.87,1.),.62);
 }else{
  vec3 lamp=vec3(side*.49,.917,clamp(p.z,1.45,3.55));
  lit+=deckLamp(p,N,V,body,roughness,metal,lamp,vec3(.94,.87,.69),.71);
  lit+=deckLamp(p,N,V,body,roughness,metal,vec3(-lamp.x,lamp.y,lamp.z),vec3(.58,.75,1.),.31);
 }
 // Both low rails continue past the loading aperture.
 float floorRail=exp(-abs(abs(p.x)-.53)*10.-abs(p.y+1.10)*5.3);
 lit+=body*vec3(.08,.56,.68)*floorRail*.35;
 return lit;
}

float cabinSeamGrid(vec2 uv){
 vec2 grid=uv/vec2(.41,.56);
 vec2 footprint=fwidth(grid);
 vec2 cell=fract(grid);
 vec2 edge=min(cell,1.-cell);
 vec2 spread=max(vec2(.010),footprint*.7);
 vec2 lines=(1.-smoothstep(vec2(.004),vec2(.012)+spread,edge))*
            (1.-smoothstep(vec2(.018),vec2(.09),footprint));
 return max(lines.x,lines.y);
}

float cabinPanelSeam(vec3 p,vec3 N){
 vec3 weight=abs(N);
 weight*=weight;
 weight*=weight;
 weight/=max(.001,weight.x+weight.y+weight.z);
 return dot(weight,vec3(cabinSeamGrid(p.zy),cabinSeamGrid(p.xz),cabinSeamGrid(p.xy)));
}

#endif
