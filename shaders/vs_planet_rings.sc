$input a_position
$output v_pattern
#include <bgfx_shader.sh>
uniform vec4 u_rings[4];
void main(){
 vec2 bounds=u_rings[0].xy;
 float margin=bounds.y*.04;
 float radius=mix(max(0.,bounds.x-margin),bounds.y+margin,a_position.y);
 v_pattern=vec3(a_position.x,0.,a_position.z)*radius;
 gl_Position=mul(u_modelViewProj,vec4(v_pattern/bounds.y,1.));
}
