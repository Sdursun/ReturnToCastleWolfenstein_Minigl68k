// Shared by the three fragment shaders: texture environment, alpha test, fog.
// env: 0 modulate, 1 replace, 2 decal, 3 add.
// afunc: 0 none, 1 greater, 2 less, 3 greater or equal.
// fogmode: 0 off, 1 linear, 2 exp, 3 exp2 (GL fog, on the eye distance).
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
layout(location = 0) in vec4 v_col;
layout(location = 1) in vec2 v_tc0;
layout(location = 2) in vec2 v_tc1;
layout(location = 3) in float v_fogz;
layout(location = 0) out vec4 o_col;
vec4 env(vec4 c, vec4 t, int mode)
{
	if (mode == 1) return t;
	if (mode == 2) return vec4(mix(c.rgb, t.rgb, t.a), c.a);
	if (mode == 3) return vec4(c.rgb + t.rgb, c.a * t.a);
	return c * t;
}
void atest(float a)
{
	if (pc.afunc == 1) { if (!(a > pc.aref)) discard; }
	else if (pc.afunc == 2) { if (!(a < pc.aref)) discard; }
	else if (pc.afunc == 3) { if (!(a >= pc.aref)) discard; }
}
vec4 fog(vec4 c)
{
	float f;
	if (pc.fogmode == 0) return c;
	if (pc.fogmode == 1) f = (pc.fogend - v_fogz) / (pc.fogend - pc.fogstart);
	else if (pc.fogmode == 2) f = exp(-pc.fogdensity * v_fogz);
	else { float d = pc.fogdensity * v_fogz; f = exp(-d * d); }
	return vec4(mix(pc.fogcolor.rgb, c.rgb, clamp(f, 0.0, 1.0)), c.a);
}
