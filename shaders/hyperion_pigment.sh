// Charred sunset presentation: soot, iron oxide and low ember emission.
// Coordinates and phases still belong to the existing world geometry.
#define psychedelic 0.0
#define extremeQuality 0.0
vec3 pigment(float h){
 float heat=.5+.5*sin(6.283185*h);
 vec3 oxide=mix(vec3(.045,.004,.0015),vec3(.90,.12,.008),smoothstep(.08,.80,heat));
 return mix(oxide,vec3(1.65,.72,.045),pow(heat,9.)*.72);
}
float thread(float p,float width){float span=fwidth(p),aa=clamp(span*.8,.002,.75),resolved=1.-smoothstep(.65,1.75,span);float strand=1.-smoothstep(width,width+aa,abs(sin(p)));float coverage=2.*asin(clamp(width,0.,1.))/3.14159265;return mix(coverage,strand,resolved);}
vec3 silk(vec3 direction,float phase,float seconds){
 vec3 p=direction*3.2;float t=seconds*.008;
 float smoke=.5+.5*sin(p.y*2.4+sin(p.x*2.+phase*6.)+sin(p.z*1.7+t)*.65);
 float folds=.5+.5*sin(p.z*3.2+sin(p.y*2.1-t)*.6);
 return mix(vec3(.012,.007,.006),vec3(.18,.028,.011),pow(smoke*folds,1.6))*.55;
}
vec3 ecstatic(vec3 direction,float phase,float seconds){
 vec3 p=direction*4.8;float t=seconds*.004;
 float a=p.y*1.7+sin(p.x*1.3+p.z*.4+phase*3.)*1.3;
 float b=p.z*1.4+sin(p.y*1.1-p.x*.5+t)*1.2;
 float soot=pow((.5+.5*sin(a))*(.5+.5*sin(b)),1.6);
 return mix(vec3(.008,.004,.003),vec3(.24,.035,.012),soot);
}
