#ifndef HYPERION_CANOPY_SHADOW
#define HYPERION_CANOPY_SHADOW
uniform vec4 u_cabinSun[2]; // Hull-local direction/power; source colour/angular radius.

float canopyTubeShadow(vec3 p,vec3 light,vec3 a,vec3 b,float radius){
 vec3 axis=b-a,q=p-a;
 float along=dot(axis,light),length2=dot(axis,axis);
 float u=clamp((dot(q,axis)-dot(q,light)*along)/max(.00001,length2-along*along),0.,1.);
 vec3 closest=a+axis*u;
 float travel=dot(closest-p,light);
 if(travel<.008)return 1.;
 float distance=length(p+light*travel-closest);
 float penumbra=max(.0025,fwidth(distance)*1.35)+travel*clamp(u_cabinSun[1].w,.002,.055);
 return smoothstep(radius-penumbra,radius+penumbra,distance);
}

float canopyFrameSun(vec3 p,vec3 light){
 if(p.z>1.16 || abs(p.x)>1.10)return 1.;
 float visibility=1.;
 for(int side=0;side<2;++side){
  float x=side==0?-.812:.812;
  visibility=min(visibility,canopyTubeShadow(p,light,vec3(x,-.25,-1.285),vec3(x,.40,-1.285),.018));
  visibility=min(visibility,canopyTubeShadow(p,light,vec3(side==0?-.72:.72,.607,-1.285),vec3(x,.40,-1.285),.016));
  for(int clampIndex=0;clampIndex<3;++clampIndex){
   float y=-.22+float(clampIndex)*.30;
   visibility=min(visibility,canopyTubeShadow(p,light,vec3(x,y-.013,-1.285),vec3(x,y+.013,-1.285),.026));
  }
 }
 visibility=min(visibility,canopyTubeShadow(p,light,vec3(-.72,.607,-1.285),vec3(.72,.607,-1.285),.018));
 return visibility;
}
#endif
