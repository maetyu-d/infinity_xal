// Celestial coordinates keep the mist stable as the camera turns or rebases.
// The opaque depth limits the veil in front of nearby objects.
vec3 regionalFog(vec2 uv,vec3 scene,inout float built){
 float strength=u_regionLook[0].x*u_regionLook[0].w;
 if(strength<=0.)return scene;
 vec2 ndc=uv*2.-1.;
 vec3 ray=normalize(vec3(ndc.x*u_reflection[0].x,ndc.y*u_reflection[0].y*u_reflection[0].z,-1.));
 vec3 direction=normalize(mul(u_spaceGlassRotation,vec4(ray,0.)).xyz);
 float drift=sin(u_regionLook[0].y*.055)*.17;
 float beltAxis=(direction.y+.29*sin(direction.z*4.1+direction.x*1.6))*2.25;
 float belt=exp(-beltAxis*beltAxis);
 vec2 solarOffset=(uv-u_stellar[0].xy)*vec2(u_reflection[0].x/u_reflection[0].y,1.);
 float sunGlow=exp(-dot(solarOffset,solarOffset)*26.)*u_stellar[0].w*sqrt(u_stellar[2].y);
 float depth=texture2D(s_opaqueDepth,uv).r;
 if(depth>0.){
  float z=u_reflection[0].w*u_reflection[1].x/
      (u_reflection[0].w+depth*(u_reflection[1].x-u_reflection[0].w));
  float distanceLog=u_regionLook[0].z+log2(z/max(-ray.z,.0001))*.30103;
  float nearHaze=smoothstep(1.2,5.0,distanceLog)*.38;
  float deepHaze=smoothstep(6.8,14.4,distanceLog)*.31;
  float cloud=strataNoiseSample(direction*3.4+vec3(3.,17.,8.+drift),vec3(0.));
  float opacity=(nearHaze+deepHaze)*mix(.73,1.03,cloud);
  vec3 aerial=mix(vec3(.009,.032,.018),vec3(.080,.165,.048),.20+.42*belt+.22*cloud);
  aerial+=vec3(.36,.35,.085)*sunGlow;
  built*=1.-opacity*strength;
  return mix(scene,aerial,opacity*strength);
 }
 // Integrate successive cloud layers front to back. Each has independent
 // folds and colour, with brighter edges where light enters the mist.
 vec3 scattered=vec3(0.);
 float transmission=1.;
 for(int layer=0;layer<5;layer++){
  float i=float(layer);
  vec3 q=direction*(3.2+i*1.65)+vec3(9.+i*1.57,16.-i*.81,2.+drift*(1.+i*.23));
  float broad=strataNoiseSample(q,vec3(0.));
  float wisps=strataNoiseSample(q*2.17+vec3(3.,-1.,.2),vec3(0.));
  float folds=strataNoiseSample(q*4.07+vec3(-4.,8.,.7),vec3(0.));
  float volume=smoothstep(.27,.72,broad*.58+wisps*.29+folds*.13);
  float side=strataNoiseSample(q+vec3(.39,.24,-.16),vec3(0.));
  float edge=max(0.,broad-side);
  float rim=4.*volume*(1.-volume)*smoothstep(.37,.65,wisps);
  float glow=clamp(rim*(.36+.39*belt)+edge*1.2+sunGlow*.65,0.,1.);
  vec3 lit=mix(vec3(.025,.22,.12),vec3(.54,.92,.19),i*.16+.23*belt);
  vec3 scatterLight=mix(vec3(.003,.014,.009),lit,glow*glow);
  scatterLight+=vec3(.58,.53,.082)*sunGlow*(.58+rim*.42);
  float opacity=1.-exp(-(.018+volume*volume*(.58+.57*belt)));
  scattered+=transmission*opacity*scatterLight;
  transmission*=1.-opacity;
 }
 return mix(scene,scene*transmission+scattered,strength);
}
