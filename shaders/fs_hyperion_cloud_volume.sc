$input v_texcoord0
#include <bgfx_shader.sh>
#include "hyperion_pigment.sh"
#include "hyperion_optical_ray.sh"
#include "hyperion_weather.sh"
SAMPLER2D(s_opaqueDepth,2);
SAMPLER2D(s_materialNoise,5);
uniform vec4 u_weatherVisual[3];
#include "hyperion_cloud_ray.sh"
// Hyperion cloudFragment's 3-D billow and erosion, with explicit physical
// footprints. Derivatives inside an early-exiting ray loop would be undefined.
float volumeNoise(vec3 p){
 vec3 cell=floor(p),f=fract(p);f=f*f*(3.-2.*f);
 vec2 uv=mod(cell.xy+vec2(37.,17.)*cell.z,vec2(256.))+f.xy;
 vec2 rg=texture2DLod(s_materialNoise,(uv+.5)/256.,0.).rg;
 return mix(rg.r,rg.g,f.z);
}
float cloudProfile(float height,float top){
 return smoothstep(0.,.15,height)*(1.-smoothstep(.50,1.,height/top));
}
float cloudMass(float cover,float billow,float erosion,float height,float top){
 return max(0.,cover-(1.-billow)*.40-(1.-erosion)*.14)*2.05*cloudProfile(height,top);
}
void main(){
 vec3 o,d;vec4 span=cloudSpan(v_texcoord0,o,d);
 float first=span.y-span.x,total=first+span.w-span.z;
 if(total<=0.){gl_FragColor=vec4(0.,0.,0.,1.);return;}
 float radiusKm=u_atmosphere[3].w,thickness=u_weather[2].w;
 float count=clamp(ceil(total*radiusKm/max(.5,thickness*.08)),12.,u_weather[3].z);
 float stepSize=total/count,kmStep=stepSize*radiusKm;
 float jitter=fract(52.9829189*fract(dot(floor(v_texcoord0/u_weather[3].xy),vec2(.06711056,.00583715))));
 vec3 L=normalize(u_atmosphere[1].xyz);
 float scatteringAngle=dot(d,L),transmission=1.;vec3 sum=vec3(0.);
 float forward=pow(max(0.,scatteringAngle),14.);
 float phase=.15+.24*pow(max(0.,scatteringAngle),2.)+.76*forward;
 float middle=total*.5,middleT=middle<first?span.x+middle:span.z+middle-first;
 vec3 middleN=normalize(o+d*middleT);
 float palette=u_weather[1].y+dot(middleN,vec3(.53,.71,.46))*.96+dot(d,L)*.15;
 vec3 opalLow=pigment(palette),opalMid=pigment(palette+.18),opalHigh=pigment(palette+.36);
 vec3 weather=vec3(0.);
 float lightNear=.5,lightFar=.5;
 bool shadowReady=false;
 for(int i=0;i<24;i++){
  if(float(i)>=count || transmission<.012)break;
  float along=(float(i)+.45+jitter*.10)*stepSize;
  float t=along<first?span.x+along:span.z+along-first;
  vec3 p=o+d*t,n=normalize(p);
  float h=(length(p)-u_weather[0].x)/(u_weather[0].y-u_weather[0].x);
  float footprint=max(kmStep,t*radiusKm*(2.*u_atmosphere[3].y)*u_weather[3].y);
  float resolved=1.-smoothstep(12.,65.,footprint);
  // Broad fronts need fewer lookups than the billows. Re-evaluate as the
  // occupied ray crosses a bank rather than marching an unrelated scene.
  if(i==0 || mod(float(i),4.)<.5){
   float lod=clamp(log2(max(1.,t*(2.*u_atmosphere[3].y)*u_weather[3].y*64./3.14159)),0.,7.);
   weather=opticalWeather(n,lod);
   shadowReady=false;
  }
  float cover=max(0.,weather.r-.035)*(.42+u_weather[1].z*.58);
  if(cover<.025)continue;
  vec3 q=n*(radiusKm*.018)+u_weather[2].xyz+n*(h*thickness*.12)+u_weather[1].y*vec3(31.,17.,47.);
  float macro=resolved>.005?volumeNoise(q*.38+vec3(19.,7.,4.)):.5;
  q+=vec3(.49,-.37,.28)*(macro-.5);
  float billow=resolved>.005?mix(.5,volumeNoise(q),resolved):.5;
  float detail=resolved>.05?mix(.5,volumeNoise(q*mix(2.67,7.8,1.-smoothstep(2.,15.,footprint))+19.),resolved):.5;
  float topHeight=min(.99,.55+cover*.16+weather.g*.16+macro*.15+billow*.11);
  float density=cloudMass(cover,billow,detail,h,topHeight);
  // Thin, high ice filaments use the same wet weather, not an extra climate.
  float cirrus=smoothstep(.77,.90,h)*(1.-smoothstep(.94,1.,h))*smoothstep(.45,.70,weather.r)*detail*.07;
  density=(density+cirrus)*clamp(u_weather[0].z*.8,0.,1.);
  if(density<.002)continue;
  float sun=dot(n,L),day=smoothstep(-.12,.14,sun);
  if(day>.002 && resolved>.005 && (!shadowReady || mod(float(i),2.)<.5)){
   lightNear=mix(.5,volumeNoise(q+L*.51),resolved);
   lightFar=resolved>.28?mix(.5,volumeNoise(q+L*1.31),resolved):lightNear;
   shadowReady=true;
  }
  float nearMass=cloudMass(cover,lightNear,.56,h+sun*.17,topHeight);
  float farMass=cloudMass(cover,lightFar,.54,h+sun*.42,topHeight);
  float column=(density*.16+nearMass*.45+farMass*.39)*max(.24,topHeight-h+.16);
  float selfShadow=exp(-column*2.8/max(.16,.23+sun));
  float gradient=clamp(.62+(billow-lightNear)*1.9,0.,1.);
  float powder=1.-exp(-density*kmStep*.32),silver=pow(1.-clamp(density,0.,1.),2.)*forward;
  vec3 ambient=mix(vec3(.008,.014,.032),vec3(.118,.175,.252),day)*(.58+.42*h)*(.65+billow*.53);
  vec3 sunlight=mix(vec3(1.12,.34,.17),vec3(1.02,.96,.86),smoothstep(.02,.45,sun));
  vec3 colour=ambient+sunlight*day*(selfShadow*(.25+phase*2.0)*gradient+powder*.14);
  colour+=sunlight*day*silver*.38*(.23+.77*selfShadow);colour*=1.-weather.g*.23;
  float shade=clamp(h*.17+billow*.13,0.,.36);
  vec3 opal=shade<.18?mix(opalLow,opalMid,shade/.18):mix(opalMid,opalHigh,(shade-.18)/.18);
  // Hyperion cloudFragment's deterministic, storm-local discharge. There
  // are no invented bolts or screen-wide flashes outside the cloud volume.
  colour+=(vec3(.30,.48,.85)+opalHigh*.35)*u_weatherVisual[2].x*pow(weather.g,4.)*2.2;
  colour*=mix(vec3(1.),.22+opal*1.45,.40);
  colour+=opal*(.018+day*(silver*.09+pow(max(0.,1.-abs(sun)*3.),3.)*.09));
  float opacity=1.-exp(-density*kmStep*.24);
  sum+=transmission*colour*opacity;transmission*=1.-opacity;
 }
 float weight=u_weather[0].w;
 gl_FragColor=vec4(sum*weight,1.-(1.-transmission)*weight);
}
