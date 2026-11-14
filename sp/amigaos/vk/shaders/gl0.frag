#version 450
#extension GL_GOOGLE_include_directive : require
#include "gl.glsl"
void main()
{
	atest(v_col.a);
	o_col = fog(v_col);
}
