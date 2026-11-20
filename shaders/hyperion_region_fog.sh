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
  float fibres=strataNoise(q*vec3(7.9,2.7,5.1)+vec3(wisps*1.3,4.,folds*.8),vec3(0.));
  float volume=smoothstep(.30,.73,broad*.45+wisps*.33+folds*.17+fibres*.05);
  float side=strataNoiseSample(q+vec3(.39,.24,-.16),vec3(0.));
  float litDepth=strataNoiseSample(q+vec3(.79,.48,-.32),vec3(0.));
  float edge=max(0.,broad-side*.61-litDepth*.39);
  float rim=4.*volume*(1.-volume)*smoothstep(.37,.65,wisps);
  float filaments=pow(max(0.,1.-abs(folds*.66+fibres*.34-.51)*4.4),5.);
  float glow=clamp(rim*(.36+.47*belt)+edge*2.9+sunGlow*.80+filaments*.12,0.,1.);
  float shadow=exp(-max(0.,side*.55+litDepth*.45-.31)*(1.8+volume*2.8));
  vec3 lit=mix(vec3(.025,.22,.12),vec3(.54,.92,.19),i*.16+.23*belt);
  vec3 scatterLight=mix(vec3(.002,.009,.007),lit,pow(glow,1.55)*shadow);
  scatterLight+=vec3(.27,.67,.20)*filaments*rim*shadow*(.055+.08*belt);
  scatterLight+=vec3(.58,.53,.082)*sunGlow*(.58+rim*.42);
  float opacity=1.-exp(-(.012+volume*volume*(.63+.64*belt))*(.77+fibres*.35));
  scattered+=transmission*opacity*scatterLight;
  transmission*=1.-opacity;
 }
 // Preserve the shape and convection of a resolved photosphere behind the
 // fog while retaining a layer of scattered light over its limb.
 vec2 stellarOffset=solarOffset/max(.00001,u_stellar[0].z);
 float disc=(1.-smoothstep(.90,1.13,length(stellarOffset)))*u_stellar[0].w;
 float through=mix(transmission,max(.57,transmission),disc);
 return mix(scene,scene*through+scattered*(1.-disc*.35),strength);
}
