$input v_texcoord0, v_color0
#include <bgfx_shader.sh>
SAMPLER2D(s_atlasText, 0);

void main() {
    float coverage = texture2D(s_atlasText, v_texcoord0).r;
    float edge = max(0.018, fwidth(coverage) * 0.56);
    gl_FragColor = vec4(v_color0.rgb, v_color0.a * smoothstep(0.5-edge, 0.5+edge, coverage));
}
