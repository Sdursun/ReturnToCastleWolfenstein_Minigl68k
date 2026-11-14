#version 450
// The last pass: the frame through the game's gamma ramp (GLimp_SetGamma),
// as a PC's hardware gamma table would do it. 256 x 1, one channel each.
layout(set = 0, binding = 0) uniform sampler2D u_scene;
layout(set = 1, binding = 0) uniform sampler2D u_ramp;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_col;
void main()
{
	vec3 c = texture(u_scene, v_uv).rgb * (255.0 / 256.0) + (0.5 / 256.0);
	o_col = vec4(texture(u_ramp, vec2(c.r, 0.5)).r,
	             texture(u_ramp, vec2(c.g, 0.5)).g,
	             texture(u_ramp, vec2(c.b, 0.5)).b, 1.0);
}
