/*
 * Workarounds for bugs in the C library (libnix, bebbo's amiga-gcc).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <stdio.h>
#include <limits.h>
#include <math.h>

#define STB_SPRINTF_IMPLEMENTATION
#define STB_SPRINTF_STATIC
#define STB_SPRINTF_NOUNALIGNED
#include "thirdparty/stb_sprintf.h"

/*
 * String formatting. libnix gets two things wrong that the engine, which
 * formats everything through vsprintf/vsnprintf (Com_sprintf, va,
 * Com_Printf, cvar values), trips over:
 *
 * - vsprintf does not terminate the string when the output is empty: it
 *   writes the terminator with fputc() after setting the buffer's space to
 *   the number of characters written, which is 0, so the fputc() finds the
 *   buffer full and drops it. PS_SetBaseFolder("") left stale stack bytes
 *   in botlib's basefolder and ui/menus.txt was looked up as
 *   "         /ui/menus.txt".
 *
 * - %f of a value that rounds up to the next power of ten from below 1
 *   comes out wrong: 0.9999999 prints as "0.1000000", not "1.000000"
 *   (tests/mathtest.c). Cvar_SetValue formats floats with %f, so such a
 *   value would be stored as 0.1.
 *
 * Both are replaced with stb_sprintf. libnix's sprintf and snprintf call
 * these two, so they are covered as well; printf/fprintf to files still
 * use libnix's formatter, which the engine only hands plain strings.
 */
int vsprintf(char *s, const char *format, va_list args)
{
	return stbsp_vsprintf(s, format, args);
}

int vsnprintf(char *s, size_t size, const char *format, va_list args)
{
	if (size > INT_MAX)
		size = INT_MAX;

	return stbsp_vsnprintf(s, (int)size, format, args);
}

/*
 * libnix's float math functions (sqrtf, atan2f, asinf, rintf, cosf, ...)
 * come from its 68000 multilib even with -m68040: -m68040/-m68060 select
 * no FPU multilib. They are soft-float fdlibm code that returns the
 * result in d0, while code built for the FPU reads it from fp0 and gets
 * whatever was left there. gcc also turns float-typed calls like
 * "float y = sqrt(x)" into sqrtf, so the engine hit this all over
 * (VectorNormalize, vectoangles, ...): in the first hardware test the 3D
 * view came out rotated by 90 degrees and the player fell to death.
 * tests/mathtest.c showed cosf(0) returning 0.
 *
 * These replacements go through the double functions, which call the
 * system's mathieeedoub*.library and return in fp0. This file is built
 * with -fno-builtin, or gcc would turn "(float)sqrt(x)" straight back
 * into a call to sqrtf, i.e. into infinite recursion.
 */
float sqrtf(float x)          { return (float)__builtin_sqrt((double)x); }
float sinf(float x)           { return (float)sin(x); }
float cosf(float x)           { return (float)cos(x); }
float tanf(float x)           { return (float)tan(x); }
float asinf(float x)          { return (float)asin(x); }
float acosf(float x)          { return (float)acos(x); }
float atanf(float x)          { return (float)atan(x); }
float atan2f(float y, float x){ return (float)atan2(y, x); }
float floorf(float x)         { return (float)floor(x); }
float ceilf(float x)          { return (float)ceil(x); }
float rintf(float x)          { return (float)rint(x); }
float fabsf(float x)          { return x < 0 ? -x : x; }
float fmodf(float x, float y) { return (float)fmod(x, y); }
float powf(float x, float y)  { return (float)pow(x, y); }
float expf(float x)           { return (float)exp(x); }
float logf(float x)           { return (float)log(x); }
float log10f(float x)         { return (float)log10(x); }
