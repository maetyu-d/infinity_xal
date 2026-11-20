#ifndef HYPERION_VINTAGE_LENS
#define HYPERION_VINTAGE_LENS
// A wide-open vintage photographic look, rather than a glass prescription.
// The image circle lives in sensor coordinates and has no animation or history.
float vintageField(vec2 uv,float aspect){
 vec2 sensor=(uv-.5)*vec2(aspect,1.);
 float radius=length(sensor)/length(vec2(aspect,1.)*.5);
 return smoothstep(.23,.96,radius);
}
vec2 vintageRadial(vec2 uv,float aspect){
 vec2 sensor=(uv-.5)*vec2(aspect,1.);
 return length(sensor)>.00001?normalize(sensor):vec2(1.,0.);
}
vec2 vintagePupil(vec2 radial,float field,float angle,float radius){
 vec2 tangent=vec2(-radial.y,radial.x);
 // Pupil clipping and sagittal/tangential separation turn off-axis points
 // into soft cat's eyes whose long axes follow the image circle.
 float clipped=1.-field*.62;
 vec2 pupil=radial*cos(angle)*clipped+tangent*sin(angle)*(1.+field*.42);
 pupil+=radial*field*.11*radius;
 return pupil*radius;
}
vec3 vintageGlare(vec3 light){
 float peak=max(light.r,max(light.g,light.b));
 vec3 glow=light/(.72+peak);
 float luminance=dot(glow,vec3(.299,.587,.114));
 // Mostly source-coloured scatter, with a faint warm veil from the optics.
 return mix(glow,vec3(luminance),.13)*vec3(1.055,1.008,.955);
}
vec3 vintageGrade(vec3 colour,float field){
 vec3 c=max(colour,vec3(0.));
 // Preserve the scene's contrast and colour separation: no lifted black
 // level, flattened gain, desaturation or additional highlight compression.
 // The vintage character comes from the aperture, glow and field softness.
 c*=vec3(1.012,1.003,.988);
 return c*(1.-field*field*.105);
}
#endif
