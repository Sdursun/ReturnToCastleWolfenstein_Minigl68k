#version 450
// Fixed function vertex: position through the GL matrices, turned into
// Vulkan's clip space (y down, depth 0..1).
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_col;
layout(location = 2) in vec2 a_tc0;
layout(location = 3) in vec2 a_tc1;
layout(push_constant) uniform Push {
	mat4 mvp;
	int env0;
	int env1;
	int afunc;
	float aref;
	int fogmode;
	float fogstart;
	float fogend;
	float fogdensity;
	vec4 fogcolor;
} pc;
layout(location = 0) out vec4 v_col;
layout(location = 1) out vec2 v_tc0;
layout(location = 2) out vec2 v_tc1;
layout(location = 3) out float v_fogz;	// eye distance, for fog
void main()
{
	vec4 p = pc.mvp * vec4(a_pos, 1.0);
	v_fogz = abs(p.w);
	p.y = -p.y;
	p.z = (p.z + p.w) * 0.5;
	gl_Position = p;
	v_col = a_col;
	v_tc0 = a_tc0;
	v_tc1 = a_tc1;
}
