uniform vec4 u_planet[5];
uniform vec4 u_planetRings[2];
#include "hyperion_ring_density.sh"
vec3 giantClouds(vec3 n,float seconds){
 vec3 axis=normalize(u_planet[3].xyz);
 float latitude=dot(n,axis),phase=u_planet[0].y;
 vec3 east=normalize(cross(axis,abs(axis.y)<.9?vec3(0.,1.,0.):vec3(1.,0.,0.))),north=cross(axis,east);
 float longitude=atan2(dot(n,north),dot(n,east)+1e-8);
 float jet=latitude*u_planet[0].z*3.14159265;
 float drift=sin(jet*.57)*seconds*.00025;
 vec3 clouds=vec3(dot(n,east),dot(n,north),latitude);
 float billow=strataNoise(clouds*vec3(6.,6.,14.)+vec3(phase*30.,2.,drift),vec3(0.));
 float ripple=strataNoise(clouds*vec3(16.,16.,91.)+vec3(drift,phase*13.,billow*1.7),vec3(5.,0.,0.));
 float belt=jet+(billow-.5)*2.6+.35*sin(longitude*5.+latitude*12.-drift);
 float storm=0.,spiral=0.,eye=0.;
 for(int i=0;i<3;i++){
  float index=float(i);
  float lat=(i==1?-1.:1.)*(.18+phase*.22+index*.14);
  float heading=phase*6.283+index*2.39+seconds*.00011;
  vec3 centre=axis*sin(lat)+(east*cos(heading)+north*sin(heading))*cos(lat);
  vec3 tangent=normalize(cross(axis,centre)),meridian=cross(centre,tangent);
  vec2 q=vec2(dot(n-centre,tangent)/(.31-index*.051),dot(n-centre,meridian)/(.15-index*.025));
  float distance2=dot(q,q),facing=smoothstep(.72,.96,dot(n,centre));
  float core=exp(-distance2*1.20)*facing;
  float spin=atan2(q.y,q.x+1e-8)*2.-log(distance2+.024)*5.1+billow*2.8;
  float arms=.5+.5*sin(spin);
  belt+=core*(arms-.30)*5.6;
  storm=max(storm,core);
  spiral=max(spiral,core*pow(arms,3.));
  eye=max(eye,exp(-distance2*34.)*facing);
 }
 float belts=mix(.5,.5+.5*sin(belt),1.-smoothstep(.6,2.,fwidth(belt)));
 float shear=.5+.5*sin(belt*2.13+ripple*2.3);
 shear=mix(.5,shear,1.-smoothstep(.45,1.4,fwidth(belt)*2.13));
 vec3 low=u_planet[1].rgb*.64,high=u_planet[2].rgb*1.20;
 vec3 albedo=mix(low,high,mix(.5,belts,u_planet[3].w*.85+.15));
 albedo*=.69+.22*billow+.28*ripple+.19*shear;
 albedo=mix(albedo,low*.84+high*(.45+spiral*.71),storm*.71);
 albedo=mix(albedo,low*.69,eye*.68);
 return albedo;
}
float planetRingSun(vec3 n,vec3 localLight){
 if(u_planetRings[0].z<=0.)return 1.;
 vec3 axis=u_planetRings[1].xyz;
 float denominator=dot(axis,localLight);
 if(abs(denominator)<.0001)return 1.;
 float t=-dot(n,axis)/denominator;
 float radius=length(n+localLight*t),footprint=max(.001,fwidth(radius));
 if(t<=0.)return 1.;
 float optical=ringDensity(u_planetRings[0],radius,footprint);
 return exp(-optical/max(.08,abs(denominator))*1.8);
}
