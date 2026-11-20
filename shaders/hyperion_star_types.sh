uniform vec4 u_starType[2]; // family+1, convection scale, spots, wind; phase, spin Hz, K, active
#include "hyperion_noise.sh"
// Coordinates are stellar-local unit normals. Derivative-filtered convection
// keeps the same features under zoom, rotation and a change of render units.
vec3 typedPhotosphere(vec3 n,vec3 colour,float mu,float seconds){
 float kind=u_starType[0].x,phase=u_starType[1].x;
 float spin=kind>7.5&&kind<8.5?0.:mod(seconds*u_starType[1].y,1.)*6.283;
 float cs=cos(spin),sn=sin(spin);n.xz=mat2(cs,-sn,sn,cs)*n.xz;
 vec3 p=n*u_starType[0].y+phase*vec3(17.,31.,11.);
 float cell=strataNoise(p,vec3(0.)),fine=strataNoise(p*2.03+9.,vec3(0.));
 float rising=smoothstep(.24,.78,cell*.77+fine*.23);
 float fineGrain=strataNoise(p*4.11+vec3(18.,4.,7.),vec3(0.));
 float lanes=(1.-smoothstep(.04,.145,abs(cell-.44)))*smoothstep(.32,.74,fine);
 float activeRegion=strataNoise(n*4.8+phase*31.,vec3(0.));
 float activeBelt=.52+.48*(1.-smoothstep(.10,.48,abs(abs(n.y)-.26)));
 float magnetic=smoothstep(.44,.76,activeRegion*.79+fine*.14+fineGrain*.07)*activeBelt;
 float spotStrength=.38+.62*u_starType[0].z;
 float umbra=smoothstep(.47,.78,magnetic)*spotStrength;
 float penumbra=smoothstep(.25,.56,magnetic)*spotStrength;
 float faculae=4.*penumbra*(1.-penumbra)*smoothstep(.37,.82,fineGrain);
 float convection=clamp(rising-lanes*.32+fineGrain*.16,0.,1.);
 float meso=strataNoise(n*vec3(13.,17.,13.)+phase*27.,vec3(0.));
 float cells=smoothstep(.31,.70,cell);
 float cellEdge=exp(-abs(cell-.43)*28.)*(.40+.60*fine);
 float network=pow(max(0.,1.-abs(cell-.58)*4.6),7.)*
               smoothstep(.33,.78,fineGrain)*(.31+.69*activeRegion);
 float value=(.13+convection*1.20)*(1.-penumbra*.47-umbra*.51)+faculae*.51+network*.31;
 if(kind<6.5 || kind>9.5){
  value*=.69+meso*.47+cells*.19;
  value+=cellEdge*.19*(1.-umbra*.8);
  colour=mix(colour*vec3(1.,.62,.36),mix(colour,vec3(1.),.14),cells);
 }
 // Cool intergranular lanes and bright cell walls preserve large convective
 // relief through the optical shoulder. Fine fibrils are footprint-filtered.
 if(kind<6.5 || kind>9.5){
  float cellWall=exp(-abs(cell-.51-fine*.09)*21.);
  float granularEdge=(1.-smoothstep(.022,.12,abs(cell-.38)))*smoothstep(.28,.75,fine);
  float fibrilPhase=fine*67.+fineGrain*11.;
  float fibrils=pow(.5+.5*sin(fibrilPhase),5.)*(1.-smoothstep(.55,1.9,fwidth(fibrilPhase)));
  value*=1.-granularEdge*.45;
  value+=cellWall*(.05+fibrils*.19)*(.23+.77*faculae);
 }
 if(kind>9.5){
  float veil=strataNoise(n*vec3(7.,13.,7.)+phase*29.,vec3(0.));
  float carbonBand=smoothstep(.42,.69,veil)*(.4+.6*(1.-mu));
  colour=mix(colour,colour*vec3(.66,.25,.15),carbonBand*.54);
 }
 if(kind>6.5&&kind<7.5)value=.96+convection*.06; // compact cooling white dwarf
 if(kind>7.5&&kind<8.5){ // exposure-averaged magnetic caps; never strobe
  float cap=pow(abs(dot(n,normalize(vec3(.35,.86,.37)))),18.);
  value=.64+cap*.7;
 }
 if(kind>8.5&&kind<9.5){
  float belt=sin(n.y*24.+(cell-.5)*3.+phase*6.283);
  value=.42+convection*.45+belt*.17;
  colour=mix(colour,colour*vec3(.62,.28,.13),smoothstep(.1,.8,belt)*.6);
 }
 if(kind<6.5 || kind>9.5){
  // Nonlinear granulation must fade before a convection cell shrinks to
  // a pixel. Broad magnetic regions survive this spatial filtering.
  float footprint=max(length(dFdx(p)),length(dFdy(p)));
  float resolved=1.-smoothstep(.18,.65,footprint);
  value=mix(.62*(1.-umbra*.58),value,resolved);
  if((kind>2.5 && kind<4.5) || kind>9.5)value=.15+value*.54;
 }
 float limb=.42+.58*pow(clamp(mu,0.,1.),.65);
 return colour*max(.06,value)*limb;
}
