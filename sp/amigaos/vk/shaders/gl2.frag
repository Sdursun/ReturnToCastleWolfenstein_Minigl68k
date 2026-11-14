#version 450
#extension GL_GOOGLE_include_directive : require
#include "gl.glsl"
layout(set = 0, binding = 0) uniform sampler2D u_tex0;
layout(set = 1, binding = 0) uniform sampler2D u_tex1;
void main()
{
	vec4 c = env(v_col, texture(u_tex0, v_tc0), pc.env0);
	c = env(c, texture(u_tex1, v_tc1), pc.env1);
	atest(c.a);
	o_col = fog(c);
}
