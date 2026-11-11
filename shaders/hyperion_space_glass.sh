#ifndef INFINITY_SPACE_GLASS_FIELD
#define INFINITY_SPACE_GLASS_FIELD
// The two warped sheet families share the same field in the background,
// reflected surfaces and refraction pass. Only the celestial ray sets their phase.
vec2 spaceGlassFold(vec3 d,float phase,float seconds){
 vec3 p=d*2.88;
 float drift=seconds*.006;
 float ridge=p.y+.55*sin(p.x*1.38+phase*6.283)+.29*cos(p.z*1.45+drift);
 ridge+=.13*sin(p.x*2.4-p.z*1.75+phase*2.5);
 float side=p.z*.69-p.x*.58+.36*cos(p.y*1.91+phase*3.+drift);
 side+=.11*sin(p.y*3.1+p.x*1.64);
 return vec2(ridge,side);
}
vec2 spaceGlassSlope(vec2 fold){
 float a=fold.x*2.25+.13*sin(fold.y*3.7);
 float b=fold.y*2.60+.16*cos(fold.x*2.3)+1.3;
 return vec2(cos(a)*.71+sin(b)*.29,sin(a)*.37-cos(b)*.63);
}
vec3 spaceGlassGlow(vec3 d,float phase,float seconds){
 vec2 fold=spaceGlassFold(d,phase,seconds);
 float a=fold.x*2.25+.13*sin(fold.y*3.7);
 float b=fold.y*2.60+.16*cos(fold.x*2.3)+1.3;
 float cut=sin(a),crossing=sin(b);
 float aa=max(fwidth(cut)*1.15,.004);
 float lip=1.-smoothstep(.012,.036+aa,abs(cut));
 float fine=exp(-abs(sin(a-.13-.055*sin(b*4.)))*24.);
 float glass=exp(-abs(sin(a+.23))*4.8);
 float farSheet=exp(-abs(crossing)*12.);
 vec3 N=normalize(vec3(.47*cos(d.x*4.+phase),.78+.20*sin(d.z*3.),.38*cos(d.y*3.-phase)));
 float fresnel=pow(1.-clamp(abs(dot(d,N)),0.,1.),3.);
 float glint=.22+.78*pow(.5+.5*sin(b*.85+phase),3.);
 float flow=.58+.22*sin(d.x*19.+sin(d.z*9.)*2.+phase)+.15*sin(d.z*33.-d.y*17.);
 float body=glass*(.16+fresnel*.72)*flow;
 vec3 c=vec3(.09,.019,.006)*body;
 c+=vec3(.49,.094,.013)*glass*glass*glint*(.17+fresnel*.41);
 c+=vec3(1.4,.46,.065)*fine*(.10+.50*glint)*(.38+fresnel);
 c+=vec3(3.25,1.60,.20)*lip*(.13+.75*fresnel)*glint;
 c+=vec3(.69,.14,.035)*farSheet*(.19+.42*fresnel)*(.44+.56*flow);
 return c;
}
#endif
