#ifndef HYPERION_GEOLOGY
#define HYPERION_GEOLOGY
float geologyProfile(vec3 p){
 float province=strataNoise(p*.19+vec3(8.,3.,17.),vec3(0.));
 float uplift=smoothstep(.39,.69,province);
 float ridge=1.-abs(strataNoise(p,vec3(0.))*2.-1.);
 float cut=strataNoise(p*2.71+vec3(7.,13.,3.),vec3(0.));
 // Broad sedimentary plains separate the folded mountain provinces.
 // The same height field is sampled by the view and sunward relief rays.
 return clamp(.20+province*.23+uplift*(ridge*ridge*.38+cut*.12),0.,1.);
}
vec4 orbitalGeology(vec3 n,vec3 view,vec3 light,float phase){
 vec3 q=n*31.+vec3(phase*31.,8.,phase*19.);
 vec3 warp=vec3(strataNoise(q*.21,vec3(0.)),
                strataNoise(q*.21+17.,vec3(0.)),strataNoise(q*.21+39.,vec3(0.)))-.5;
 q+=warp*5.6;
 vec3 viewTangent=view-n*dot(view,n);
 float grazing=1.-clamp(dot(view,n),0.,1.);
 vec3 shift=-viewTangent*(.36+.40*grazing);
 float rayHeight=1.,height=geologyProfile(q);
 vec3 hit=q;
 // A bounded, globe-local relief intersection supplies parallax between
 // ridges and their gullies. This cannot alter the retained collision mesh.
 for(int step=0;step<7;++step){
  float reach=(float(step)+.5)/7.;
  vec3 samplePoint=q+shift*reach;
  float surface=geologyProfile(samplePoint);
  float crossed=smoothstep(-.08,.08,surface-(1.-reach));
  float take=crossed*rayHeight;
  hit=mix(hit,samplePoint,take);
  height=mix(height,surface,take);
  rayHeight*=1.-crossed;
 }
 vec3 sunTangent=light-n*dot(light,n);
 float slope=max(.10,dot(n,light));
 float shadow=0.;
 for(int step=1;step<=3;++step){
  float distance=float(step)*.22;
  float blocker=geologyProfile(hit+sunTangent*distance);
  shadow=max(shadow,smoothstep(.025,.16,blocker-height-distance*slope*.8));
 }
 float fine=strataNoise(hit*4.7+13.,vec3(0.));
 float bedding=.5+.5*sin((height+warp.y*.11)*33.);
 bedding=mix(.5,bedding,1.-smoothstep(.6,2.,fwidth(height*33.)));
 return vec4(height,fine,bedding,1.-shadow*.64);
}
#endif
