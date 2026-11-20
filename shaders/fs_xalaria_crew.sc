$input v_texcoord0, v_color0
#include <bgfx_shader.sh>
SAMPLER2D(s_deckCrew,0);
void main(){
    float edge=texture2D(s_deckCrew,v_texcoord0).r-.5;
    float pixel=length(vec2(dFdx(edge),dFdy(edge)));
    float lineWidth=clamp(pixel*2.,.048,.26);
    float aa=clamp(pixel*.48,.003,.095);
    float fill=smoothstep(-aa,aa,edge);
    float coverage=smoothstep(-aa,aa,edge+lineWidth);
    // Only the ink and its contour take part in depth testing.
    if(coverage<.01)discard;
    vec3 ink=vec3(.009,.012,.016);
    gl_FragColor=vec4(ink*fill+v_color0.rgb*(coverage-fill),coverage);
}
