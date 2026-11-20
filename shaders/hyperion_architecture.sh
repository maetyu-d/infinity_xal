// Source: Hyperion flight/architecture.hpp, structureLight and its glass,
// ceramic coating, room lighting and grazing-film branches.
// Only material presentation; local metre coordinates never follow the camera.
float structureHash(vec3 p){p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}
vec2 structurePlane(vec3 local,int axis){
 return axis==2?local.xy:axis==0?local.zy:local.xz;
}
// Hyperion architecture.hpp::windowInterior, adapted from model UVs to
// Infinity's existing metre-grid facades. The room is optical presentation
// behind an existing glass surface; no building or simulation is generated.
vec3 windowInterior(vec3 V,vec3 N,vec2 grid,vec2 du,vec2 dv,vec3 position,vec3 accent,float identity,float working){
 float det=du.x*dv.y-du.y*dv.x;
 if(abs(det)<1e-12)return accent*(.015+working*.09);
 vec3 px=dFdx(position)*u_materialDomain.z,py=dFdy(position)*u_materialDomain.z;
 vec3 tangent=(px*dv.y-py*du.y)/det,bitangent=(py*du.x-px*dv.x)/det;
 vec3 ray=vec3(-dot(V,normalize(tangent))/max(length(tangent),.05),-dot(V,normalize(bitangent))/max(length(bitangent),.05),-max(dot(V,N),.08)/2.8);
 vec3 safeRay=vec3(ray.x<0.?-1.:1.,ray.y<0.?-1.:1.,-1.)*max(abs(ray),vec3(.00001));
 vec3 origin=vec3(clamp(fract(grid),vec2(0.),vec2(1.))-.5,-.001);
 vec3 wall=vec3(safeRay.x>0.?.5:-.5,safeRay.y>0.?.5:-.5,-1.);
 vec3 travel=(wall-origin)/safeRay;float t=min(travel.x,min(travel.y,travel.z));vec3 hit=origin+safeRay*t;
 float back=step(travel.z,min(travel.x,travel.y)+.0001),floorFace=step(travel.y,min(travel.x,travel.z)+.0001)*step(safeRay.y,0.);
 vec3 room=mix(vec3(.20,.15,.10),accent*.25+vec3(.08,.10,.14),identity);
 room*=mix(.30,.64,back)*mix(1.,.55,floorFace);
 float details=1.-smoothstep(.08,.35,max(length(du),length(dv)));
 float desk=(1.-smoothstep(.025,.065,abs(hit.y+.24)))*(1.-smoothstep(.20,.35,abs(hit.x)))*back;
 float lamp=thread(hit.x*7.,.22)*smoothstep(.22,.42,hit.y)*back;
 room+=details*(vec3(.025,.035,.05)*desk+mix(vec3(1.,.59,.29),accent,.35)*lamp*.16);
 room*=.12+working*mix(.18,1.,step(.24,identity))*1.15;
 return mix(accent*(.015+working*.09),room,details);
}
void structureLight(out vec3 fill, out vec3 glint){
 // Regional colour belongs to incoming light. Keep a neutral component in
 // the fill so painted metal, ceramic and safety markings stay identifiable.
 fill=vec3(1.,.37,.15)*u_regionLook[1].y
     +vec3(.43,.92,.54)*u_regionLook[0].x
     +vec3(.33,.65,1.)*u_regionLook[1].x
     +vec3(1.,.38,.68)*u_regionLook[1].z;
 glint=vec3(1.,.70,.25)*u_regionLook[1].y
      +vec3(.72,1.,.45)*u_regionLook[0].x
      +vec3(.57,.80,1.)*u_regionLook[1].x
      +vec3(1.,.93,.41)*u_regionLook[1].z;
}
vec3 architecture(vec3 base,vec3 N,vec3 V,vec3 L,vec3 p,vec3 authoredPoint,float phase,float sunVisibility,vec3 position,float powered,out vec4 reflection){
 float role=u_materialDomain.y,localY=authoredPoint.y;
 bool harbour=role<-.5;
 bool stationHull=role< -1.5 && role> -2.5;
 bool vessel=role< -2.5;
 // Opaque station parts use alpha to distinguish metre-scale hardware and fixture lenses.
 bool dockHardware=harbour&&powered<.75;
 float dockEmission=dockHardware?1.-smoothstep(.12,.49,powered):0.;
 if(harbour)powered=1.;
 vec3 face=surfaceFrameNormal(authoredPoint,vec3(0.,1.,0.));
 // Remove the fixed light baked into the existing surface-mesh colours.
 // True geometry normals, sun and sky then illuminate the same triangles once.
 if(u_materialDomain.w<.5){
  vec3 authoredN=face;
  vec3 worldN=surfaceFrameNormal(position,V);
  if(dot(worldN,V)<0.)authoredN=-authoredN;
  vec3 bakedLight=vessel?vec3(.4,.8,-.5):vec3(.4,1.,.3);
  base/=vessel?max(.54,.54+.46*max(0.,dot(authoredN,normalize(bakedLight)))):
               max(.55,.55+.45*max(0.,dot(authoredN,normalize(bakedLight))));
 }
 bool paving=abs(role-8.)<.1||abs(role-20.)<.1||abs(role-21.)<.1||abs(role-6.)<.1||(harbour&&!vessel&&!dockHardware&&abs(face.y)>.65);
 bool pad=abs(role-7.)<.1,solar=abs(role-22.)<.1;

 bool vegetation=role>16.5&&role<17.5;
 bool person=role>8.5&&role<10.5;
 bool built=!vegetation&&!person;
 float glass=built&&!dockHardware&&!pad&&!solar&&(!paving||harbour)?
  smoothstep(1.6,2.5,base.b/max(base.r,.01))*smoothstep(1.02,1.23,base.b/max(base.g,.01)):0.;
 float inlay=vegetation||person||paving||solar?0.:smoothstep(.44,.70,base.g-base.r)*smoothstep(.4,.65,base.g);
 vec3 dx=dFdx(p),dy=dFdy(p);
 vec3 w=abs(face);
 // The authored 45-degree bevels have two equal components. Resolve ties
 // consistently so adjacent pixels cannot select different material planes.
 float tie=.018;
 int axis=w.z>w.x-tie&&w.z>w.y-tie?2:w.x>w.y-tie?0:1;
 vec2 uv=structurePlane(p,axis);
 // Project the actual footprint on that plane. Differencing selected UVs
 // across a seam otherwise gives the texture sampler a false mip level.
 vec2 uvDx=structurePlane(dx,axis),uvDy=structurePlane(dy,axis);
 vec2 pitch=dockHardware?vec2(1.32,.86):vessel?vec2(4.8,7.2):harbour?(paving?vec2(3.6,4.8):vec2(26.,20.)):vec2(3.6,3.2);
 vec2 grid=uv/pitch,cell=fract(grid),edge=min(cell,1.-cell);
 vec2 gridDx=uvDx/pitch,gridDy=uvDy/pitch;
 float footprint=max(length(dx),length(dy));
 float fine=1.-smoothstep(.4,1.8,footprint);
 float panel=structureHash(vec3(floor(grid),phase*113.));
 vec2 span=abs(gridDx)+abs(gridDy);
 float panelAA=max(span.x,span.y);
 float panelFinish=panel;
 if(stationHull || vessel){
  // Average the adjoining panels only within the pixel's footprint.
  // At distance, unresolved patches converge to the mean finish.
  vec2 halfSpan=clamp(span*.5,vec2(.0001),vec2(.5));
  vec2 lower=floor(grid-halfSpan);
  vec2 amount=clamp((grid+halfSpan-lower-1.)/(2.*halfSpan),0.,1.);
  float seed=phase*113.;
  float a=mix(structureHash(vec3(lower,seed)),structureHash(vec3(lower+vec2(1.,0.),seed)),amount.x);
  float b=mix(structureHash(vec3(lower+vec2(0.,1.),seed)),structureHash(vec3(lower+vec2(1.,1.),seed)),amount.x);
  panelFinish=mix(mix(a,b,amount.y),.5,smoothstep(.48,1.18,panelAA));
 }
 float joint=(1.-smoothstep(.006,.018+panelAA,min(edge.x,edge.y)))*fine;
 float bevel=(1.-smoothstep(.022,.057+panelAA,min(edge.x,edge.y)))*fine*(1.-joint);
 vec2 corner=min(abs(cell-.065),abs(cell-.935));
 float bolts=(1.-smoothstep(max(0.,.009-panelAA),.009+panelAA,length(corner)))*(1.-smoothstep(.01,.06,panelAA));
 // Factory finish, panel joints and aggregate use the building's local
 // coordinates and its authored paint.
 float materialScale=dockHardware?1.72:vessel?3.2:harbour?14.:3.6;
 vec2 materialUV=uv/materialScale+phase*vec2(7.1,3.7);
 vec2 materialDx=uvDx/materialScale,materialDy=uvDy/materialScale;
 vec4 finish=texture2DGrad(s_materialDetail,materialUV*.125,materialDx*.125,materialDy*.125);
 vec4 detailField=texture2DGrad(s_materialDetail,materialUV,materialDx,materialDy);
 float wear=smoothstep(.35,.75,finish.b),pores=detailField.a*detailField.g;
 float upright=1.-abs(dot(N,normalize(u_environment.xyz)));
 float runoff=smoothstep(.42,.75,finish.a)*upright*wear;
 vec3 paint=pow(max(base,vec3(.001)),vec3(1.72));
 float paintLuma=dot(paint,vec3(.25,.61,.14));
 vec3 albedo=mix(vec3(paintLuma),paint,.58)*.77+vec3(.020,.022,.025);
 albedo*=(.86+panelFinish*.24)*(.84+finish.r*.23+detailField.r*.10*fine);
 albedo=mix(albedo,albedo*.45+vec3(.012,.018,.024),runoff*.38);
 albedo*=1.-pores*.16*fine;
 albedo*=1.-joint*.39;
 albedo+=vec3(.033,.037,.041)*(bolts+bevel*.22);
 float rough=mix(dockHardware?.44:harbour?.29:.43,.088,glass);
 float metal=mix(dockHardware?.49:harbour?.66:.26,.12,glass);
 rough=clamp(rough+(detailField.a-.5)*.04+wear*.035,.074,.62);
 // Painted hull panels and exposed edge metal no longer share one polished
 // finish. The broad panel variation survives after the fine pores fade away.
 if(harbour && !dockHardware && !stationHull && glass<.1 && !paving){
  float paintCoat=smoothstep(.07,.24,max(base.r,max(base.g,base.b))-min(base.r,min(base.g,base.b)));
  rough=mix(.30,.49,paintCoat)+wear*.09;
  metal=mix(.76,.24,paintCoat);
  albedo*=.89+panelFinish*.21;
  albedo=mix(albedo,albedo*.52+vec3(.034,.040,.045),joint*.48);
  if(vessel){
   // Exposed turned alloy and the opaque paint coat retain different broad
   // highlights. Neither inherits the surrounding world's material.
   rough=mix(.235,.51,paintCoat)+wear*.065;
   metal=mix(.90,.18,paintCoat);
   // Ink and enamel lie in the coating, not on coplanar decal triangles.
   // Their edges and the small stencil bars have explicit pixel footprints.
   float aa=max(.0015,panelAA);
   float stripe=(1.-smoothstep(.012,.020+aa,abs(cell.y-.87)))*
                smoothstep(.10,.13+aa,cell.x)*(1.-smoothstep(.61,.64+aa,cell.x));
   float label=smoothstep(.68-aa,.68+aa,cell.y)*(1.-smoothstep(.75-aa,.75+aa,cell.y))*
               smoothstep(.15-aa,.15+aa,cell.x)*(1.-smoothstep(.34-aa,.34+aa,cell.x));
   float bars=sin(cell.x*167.);
   bars=smoothstep(-max(.05,fwidth(bars)),max(.05,fwidth(bars)),bars);
   label*=bars*(1.-smoothstep(.012,.055,panelAA));
   float marked=1.-smoothstep(.45,.48,panelFinish);
   vec3 enamel=mix(vec3(.33,.18,.051),vec3(.44,.40,.27),step(.24,panelFinish));
   albedo=mix(albedo,enamel,(stripe*.63+label*.45)*marked*fine);
  }
 }
 // Flush service covers and recessed seals are part of the coating, with
 // explicit metre/pixel filtering. They never add coplanar decal surfaces.
 float access=0.,accessRim=0.;
 if((vessel||stationHull) && glass<.1 && !paving && !dockHardware){
  vec2 q=abs(cell-vec2(.49,.44))-vec2(.26,.23);
  float distance=max(q.x,q.y);
  float aa=max(.002,panelAA*1.25);
  float selected=1.-smoothstep(.11,.21,panelFinish);
  access=(1.-smoothstep(-aa,aa,distance))*selected*fine;
  accessRim=(1.-smoothstep(.005,.013+aa,abs(distance)))*selected*fine;
  float latch=(1.-smoothstep(.018,.029+aa,abs(cell.x-.69)))*
              (1.-smoothstep(.035,.050+aa,abs(cell.y-.44)))*selected*fine;
  albedo=mix(albedo,albedo*.71+vec3(.012,.015,.018),access*.58);
  albedo*=1.-accessRim*.61;
  albedo=mix(albedo,vec3(.08,.09,.10),latch*.55);
  rough=clamp(rough+access*.065-latch*.14,.16,.85);
 }
 if(paving||pad){
  float kerb=smoothstep(.30,.50,base.r+base.g*.15);
  vec3 aggregate=mix(vec3(.025,.035,.044),vec3(.12,.135,.14),kerb);
  albedo=aggregate*(.67+detailField.r*.44+finish.b*.22);
  rough=harbour?.19:pad?.43:.73;
  metal=harbour?.41:.08;
  if(stationHull){
   float jointShade=1.-joint*.55;
   float treadPhase=(uv.x+uv.y*.61)*38.;
   float tread=.5+.5*sin(treadPhase);
   tread=mix(.5,tread,1.-smoothstep(.55,2.1,fwidth(treadPhase)));
   float scuff=smoothstep(.37,.76,finish.r)*fine;
   albedo*=jointShade*(.80+panelFinish*.25+tread*.10);
   albedo=mix(albedo,albedo*.65+vec3(.040,.046,.048),scuff*.25);
   rough=.51+wear*.17-scuff*.12;
   metal=.23+scuff*.15;
  }
 }
 if(solar){
  float cells=(1.-smoothstep(.025,.055+panelAA,min(edge.x,edge.y)))*fine;
  albedo=vec3(.006,.016,.045)*(1.-cells*.65);rough=.07;metal=.52;
 }

 if(vegetation){albedo=base*.23;rough=.92;metal=0.;}
 if(person){albedo=base*.48;rough=.76;metal=.05;}
 if(glass>.1){
  albedo=mix(albedo,vec3(.014,.025,.038)*(.81+panelFinish*.35),glass);
  rough=mix(rough,.086,glass*.78);
 }
 // Brushed orbital structure has broad highlights at grazing angles.
 if(stationHull)rough=max(rough,glass>.1?.22:paving?.47:.39);
 float day=smoothstep(-.15,.18,u_surface.x);
 // Hyperion structureBump's recessed panel joints, in actual local metres.
 // Position and silhouettes stay untouched; unresolved joints fade out.
 if(built){
  float relief=(-joint*.008+bevel*.005+(detailField.r-.5)*.002*fine-pores*.001*fine+bolts*.004-accessRim*.006-access*.002)/max(u_materialDomain.z,1e-12);
  // Keep fine paint relief from breaking up large highlights during a pan.
  N=materialBump(N,position,relief,stationHull?.07:vessel?.10:.22);
 }
 vec3 nx=dFdx(N),ny=dFdy(N);float variance=min(.25,max(dot(nx,nx),dot(ny,ny))*.5);
 rough=pow(min(1.,pow(rough,4.)+variance),.25);
 float fresnel=pow(1.-clamp(dot(N,V),0.,1.),4.);
 // Streamed paving has camera-relative vertices, rather than a building's
 // local footing height. Its illumination must not follow that origin.
 float footing=u_materialDomain.w>.5?1.:mix(.42,1.,smoothstep(.015,.15,localY));
 vec3 skyUp=normalize(u_environment.xyz);
 float skyFacing=clamp(dot(N,skyUp)*.5+.5,0.,1.);
 vec3 fill,glint;
 structureLight(fill,glint);
 vec3 sunColour=mix(vec3(1.05,1.02,.97),glint,.26+.27*(1.-day));
 // Solve the machining direction from the same object-space panel plane.
 // Scale derivatives before normalization to retain tiny hulls at speed.
 float uvDet=uvDx.x*uvDy.y-uvDx.y*uvDy.x;
 vec3 worldDx=dFdx(position),worldDy=dFdy(position);
 float worldSpan=max(max(abs(worldDx.x),max(abs(worldDx.y),abs(worldDx.z))),
                     max(abs(worldDy.x),max(abs(worldDy.y),abs(worldDy.z))));
 vec3 grain=(worldDx/max(worldSpan,1e-30))*uvDy.y-(worldDy/max(worldSpan,1e-30))*uvDx.y;
 grain=builtTangent(N,grain*(uvDet<0.?-1.:1.));
 float machining=built?(1.-glass)*smoothstep(.45,.85,metal)*.48:0.;
 vec3 c=builtBrdf(albedo,N,V,L,rough,metal,grain,machining)*sunVisibility*sunColour*(vessel?1.88:1.52);
 float coat=built&&!paving?(1.-glass)*(1.-smoothstep(.30,.70,metal)):0.;
 c+=builtCoat(N,V,L,max(.27,rough*.73))*sunColour*sunVisibility*coat*.64;
 float key=sunVisibility*smoothstep(-.10,.40,dot(N,L));
 vec3 skyIrradiance=(vec3(.024,.029,.038)+fill*.14)*mix(.24,1.,key);
 vec3 groundBounce=(vec3(.010,.012,.017)+fill*.026)*mix(.30,1.,key);
 c+=albedo*mix(groundBounce,skyIrradiance,skyFacing)*footing;
 vec3 r=reflect(-V,N);
 float elevation=dot(r,skyUp);
 float horizon=exp(-abs(elevation-.04)/mix(.075,.30,rough));
 float reflectedSun=pow(max(dot(r,L),0.),mix(420.,18.,rough))*sunVisibility;
 // The horizon and the same stellar bearing that lights the facade both
 // appear on polished faces. Rough concrete gathers a broader reflection.
 vec3 sky=fill*(.029+.13*horizon)*(.35+.65*day)+glint*(horizon*.10+reflectedSun*.94);
 if(vessel)sky+=mix(vec3(.17,.19,.22),fill*.29,.54)*horizon*(.38+.62*day);
 vec3 reflectance=environmentReflectance(mix(vec3(.035),albedo,metal),fresnel,rough);
 c+=sky*reflectance*(built?1.:.12);
 // A broad grazing coating highlight gives hull bevels a second response
 // without giving the paint the material of the surrounding nebula.
 if(built && !paving && glass<.1)
  c+=glint*reflectedSun*(.018+.055*fresnel)*(1.-joint)*sunVisibility;
 c*=1.-joint*.24;
 // Lit rooms recede into dark glass; they never bleach the facade coating.
 float glassEdge=joint*glass*(.10+fresnel*.65);
 c+=vec3(2.4,.80,.10)*glassEdge;
 float room=step(.37,panel),blind=smoothstep(.18,.28,cell.y)*(1.-smoothstep(.80,.86,cell.y));
 vec3 roomColour=mix(vec3(.30,.63,.70),vec3(1.,.74,.43),step(.53,panel));
 if(glass>.1){
  vec3 wallN=surfaceFrameNormal(position,V);if(dot(wallN,V)<0.)wallN=-wallN;
  vec3 interior=windowInterior(V,wallN,grid,gridDx,gridDy,position,roomColour,panel,(.18+(1.-day)*.82)*powered);
  c=mix(c,interior+sky*(.05+fresnel*.65),glass*(1.-fresnel*.78)*fine*.84);
  float mullion=(1.-smoothstep(.018,.051+panelAA,abs(cell.x-.5)))*fine;
  c=mix(c,c*.32+sky*.10,mullion*glass*.66);
 }
 c+=roomColour*glass*room*blind*(.035+(1.-day)*.85)*fine*powered;
 if(vegetation)c+=base*pow(max(0.,dot(-L,V)),2.)*max(0.,-dot(N,L))*sunVisibility*.18;
 // Fixed-spectrum fixtures are part of the structure's authored colours.
 float fixture=dockHardware?0.:vessel?glass*.18:harbour?smoothstep(.16,.42,max(base.r,max(base.g,base.b))-min(base.r,min(base.g,base.b))):inlay;
 c+=base*fixture*(harbour?.75:1.25)*powered;
 c+=base*dockEmission*2.6;
 // The optical pass uses the sign to preserve the finish through the grade.
 reflection=vec4(N,built?-rough:rough);
 return c;
}
