#version 450
#extension GL_GOOGLE_include_directive : require
#include "gl.glsl"
layout(set = 0, binding = 0) uniform sampler2D u_tex0;
void main()
{
	vec4 c = env(v_col, texture(u_tex0, v_tc0), pc.env0);
	atest(c.a);
	o_col = fog(c);
}
