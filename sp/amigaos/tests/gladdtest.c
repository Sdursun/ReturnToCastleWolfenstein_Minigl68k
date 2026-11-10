/*
 * What GL_ADD does, independent of the game. Shows two textures and three ways
 * of combining them:
 *
 *   TEX 1, TEX 2   the two textures on their own
 *   MODULATE       one pass, two textures: tex1 * tex2, which darkens
 *   GL_ADD         one pass, two textures: tex1 + tex2, which brightens
 *   TWO PASS       tex1, then tex2 drawn over it with blendFunc ONE ONE
 *   GREY           MODULATE again, with a grey (half) vertex colour: should be
 *                  half as bright (PiStorm3D 29.3 ignored the vertex colour)
 *
 * GL_ADD and TWO PASS should look the same; GL_ADD gets there with one draw
 * instead of two. The program compares those two panels pixel by pixel and
 * prints the result after closing the display.
 *
 * Usage: gladdtest [seconds]   (default 15, Ctrl-C quits earlier)
 *
 * Build, from sp/amigaos:
 *   m68k-amigaos-gcc -O2 -noixemul -m68040 -m68881
 *     -I../../PiStorm3D/SDK/minigl-shared-library/include
 *     tests/gladdtest.c -o gladdtest -L../../PiStorm3D/SDK/lib -lminigl -lm
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>

#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/minigl.h>

#define SCREEN_W 640
#define SCREEN_H 480
#define TEX_SIZE 64
#define PANEL    180
#define GREY     100

static GLubyte tex1data[TEX_SIZE * TEX_SIZE * 4];
static GLubyte tex2data[TEX_SIZE * TEX_SIZE * 4];
static GLubyte addpixels[PANEL * PANEL * 4];
static GLubyte passpixels[PANEL * PANEL * 4];

static GLuint tex1, tex2;

/* Texture 1: red-brown bricks with dark mortar, like a wall. */
static void MakeBricks(void)
{
	int x, y;

	for (y = 0; y < TEX_SIZE; y++)
	{
		for (x = 0; x < TEX_SIZE; x++)
		{
			GLubyte *p = &tex1data[(y * TEX_SIZE + x) * 4];
			int row = y / 16;
			int shifted = (x + (row & 1) * 16) % 32;
			int mortar = (y % 16) < 2 || shifted < 2;

			p[0] = mortar ? 50 : 130;
			p[1] = mortar ? 50 : 60;
			p[2] = mortar ? 50 : 40;
			p[3] = 255;
		}
	}
}

/* Texture 2: a soft yellow light on black, like the glow layer of a lamp. */
static void MakeGlow(void)
{
	int x, y;

	for (y = 0; y < TEX_SIZE; y++)
	{
		for (x = 0; x < TEX_SIZE; x++)
		{
			GLubyte *p = &tex2data[(y * TEX_SIZE + x) * 4];
			int dx = x - TEX_SIZE / 2;
			int dy = y - TEX_SIZE / 2;
			int d = dx * dx + dy * dy;
			int v = 255 - d * 255 / (24 * 24);

			if (v < 0)
				v = 0;

			p[0] = (GLubyte)v;
			p[1] = (GLubyte)(v * 220 / 255);
			p[2] = (GLubyte)(v * 120 / 255);
			p[3] = 255;
		}
	}
}

static GLuint LoadTexture(const GLubyte *data)
{
	GLuint id = 0;

	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_SIZE, TEX_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	return id;
}

/* ---- labels: a tiny 5x7 font, each pixel a filled quad ------------------ */

typedef struct
{
	char c;
	unsigned char rows[7];
} glyph_t;

static const glyph_t font[] = {
	{ 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
	{ 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
	{ 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
	{ 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E } },
	{ 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
	{ 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
	{ 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
	{ 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
	{ 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
	{ 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
	{ 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
	{ 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 } },
	{ 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
	{ 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
	{ 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } },
	{ '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
	{ '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } },
	{ '_', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F } },
	{ 0, { 0 } }
};

static void DrawLabel(int x, int y, const char *text)
{
	int scale = 2;

	glBegin(GL_QUADS);
	for (; *text; text++, x += 6 * scale)
	{
		const glyph_t *g;
		int row, col;

		for (g = font; g->c && g->c != *text; g++)
			;
		if (!g->c)
			continue;

		for (row = 0; row < 7; row++)
		{
			for (col = 0; col < 5; col++)
			{
				if (g->rows[row] & (0x10 >> col))
				{
					float px = (float)(x + col * scale);
					float py = (float)(y + row * scale);

					glVertex2f(px, py);
					glVertex2f(px + scale, py);
					glVertex2f(px + scale, py + scale);
					glVertex2f(px, py + scale);
				}
			}
		}
	}
	glEnd();
}

/* ---- panels ------------------------------------------------------------- */

static void SingleQuad(int x, int y)
{
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f((float)x, (float)y);
	glTexCoord2f(1, 0); glVertex2f((float)(x + PANEL), (float)y);
	glTexCoord2f(1, 1); glVertex2f((float)(x + PANEL), (float)(y + PANEL));
	glTexCoord2f(0, 1); glVertex2f((float)x, (float)(y + PANEL));
	glEnd();
}

static void MultiQuadSized(int x, int y, int size)
{
	static const float s[4] = { 0, 1, 1, 0 };
	static const float t[4] = { 0, 0, 1, 1 };
	const float px[4] = { (float)x, (float)(x + size), (float)(x + size), (float)x };
	const float py[4] = { (float)y, (float)y, (float)(y + size), (float)(y + size) };
	int i;

	glBegin(GL_QUADS);
	for (i = 0; i < 4; i++)
	{
		glMultiTexCoord2fARB(GL_TEXTURE0_ARB, s[i], t[i]);
		glMultiTexCoord2fARB(GL_TEXTURE1_ARB, s[i], t[i]);
		glVertex2f(px[i], py[i]);
	}
	glEnd();
}

static void DrawOneTexture(int x, int y, GLuint id)
{
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, id);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	SingleQuad(x, y);
	glDisable(GL_TEXTURE_2D);
}

/* One pass: both textures bound, env decides how unit 1 joins unit 0. */
static void DrawCombinedSized(int x, int y, int size, GLint env);

static void DrawCombined(int x, int y, GLint env)
{
	DrawCombinedSized(x, y, PANEL, env);
}

static void DrawCombinedSized(int x, int y, int size, GLint env)
{
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, tex1);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	glActiveTextureARB(GL_TEXTURE1_ARB);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, tex2);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, env);

	MultiQuadSized(x, y, size);

	glDisable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glActiveTextureARB(GL_TEXTURE0_ARB);
	glDisable(GL_TEXTURE_2D);
}

/* The same result the old way: two draws and framebuffer blending. */
static void DrawTwoPass(int x, int y)
{
	DrawOneTexture(x, y, tex1);

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	DrawOneTexture(x, y, tex2);
	glDisable(GL_BLEND);
}

int main(int argc, char **argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 15;
	int frames, frame;
	const int bottomy = 270, addx = 230, passx = 440;
	const int greyx = 530, greyy = 120;
	long maxdiff = -1, differing = 0;
	GLubyte modc[4], greyc[4];
	GLint units = 0;

	if (!MiniGLOpen())
	{
		printf("Cannot open minigl.library 29.1 or newer\n");
		return 20;
	}

	mglChooseWindowMode(GL_FALSE);
	mglChoosePixelDepth(32);
	mglChooseNumberOfBuffers(2);
	/* The labels are one long glBegin block; PiStorm3D's default buffer of 256
	   vertexes cuts them off without an error. */
	mglChooseVertexBufferSize(4096);

	if (!mglCreateContext(0, 0, SCREEN_W, SCREEN_H))
	{
		printf("Cannot create a %dx%d MiniGL context\n", SCREEN_W, SCREEN_H);
		MiniGLClose();
		return 20;
	}

	mglLockMode(MGL_LOCK_AUTOMATIC);
	glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, &units);

	MakeBricks();
	MakeGlow();
	tex1 = LoadTexture(tex1data);
	tex2 = LoadTexture(tex2data);

	glViewport(0, 0, SCREEN_W, SCREEN_H);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0, (GLdouble)SCREEN_W, (GLdouble)SCREEN_H, 0.0, -1.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glClearColor(0.12f, 0.12f, 0.16f, 1.0f);

	frames = seconds * 10;
	for (frame = 0; frame < frames; frame++)
	{
		glClear(GL_COLOR_BUFFER_BIT);
		glColor4f(1, 1, 1, 1);

		DrawOneTexture(130, 30, tex1);
		DrawOneTexture(330, 30, tex2);
		DrawCombined(20, bottomy, GL_MODULATE);
		DrawCombined(addx, bottomy, GL_ADD);
		DrawTwoPass(passx, bottomy);
		glColor4f(0.5f, 0.5f, 0.5f, 1);
		DrawCombinedSized(greyx, greyy, GREY, GL_MODULATE);

		glColor4f(1, 1, 1, 1);
		DrawLabel(130, 12, "TEX 1");
		DrawLabel(330, 12, "TEX 2");
		DrawLabel(20, bottomy - 18, "MODULATE");
		DrawLabel(addx, bottomy - 18, "GL_ADD");
		DrawLabel(passx, bottomy - 18, "TWO PASS");
		DrawLabel(greyx, greyy - 18, "GREY");

		/* Compare once, on the third frame, before it goes on display. */
		if (frame == 2)
		{
			int i;
			int y = SCREEN_H - bottomy - PANEL;

			glReadPixels(addx, y, PANEL, PANEL, GL_RGBA, GL_UNSIGNED_BYTE, addpixels);
			glReadPixels(passx, y, PANEL, PANEL, GL_RGBA, GL_UNSIGNED_BYTE, passpixels);
			glReadPixels(20 + PANEL / 2, SCREEN_H - 1 - (bottomy + PANEL / 2), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, modc);
			glReadPixels(greyx + GREY / 2, SCREEN_H - 1 - (greyy + GREY / 2), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, greyc);

			maxdiff = 0;
			for (i = 0; i < PANEL * PANEL * 4; i++)
			{
				long d = (long)addpixels[i] - (long)passpixels[i];

				if ((i & 3) == 3)
					continue;	/* alpha is not on display */
				if (d < 0)
					d = -d;
				if (d > maxdiff)
					maxdiff = d;
				if (d > 2)
					differing++;
			}
		}

		mglSwitchDisplay();
		Delay(5);

		if (SetSignal(0, 0) & SIGBREAKF_CTRL_C)
			break;
	}

	glDeleteTextures(1, &tex1);
	glDeleteTextures(1, &tex2);

	/* Nothing may print while the context is open: see amiga_glimp.c. */
	mglDeleteContext();
	MiniGLClose();

	printf("Texture units: %ld\n", (long)units);
	if (maxdiff < 0)
		printf("GL_ADD against TWO PASS: not compared\n");
	else
		printf("GL_ADD against TWO PASS: largest difference %ld of 255, %ld channels differ by more than 2\n",
			maxdiff, differing);
	if (maxdiff >= 0)
		printf("Vertex colour with two textures: MODULATE centre %d %d %d, with grey colour %d %d %d (%s)\n",
			modc[0], modc[1], modc[2], greyc[0], greyc[1], greyc[2],
			greyc[0] * 2 < modc[0] + 10 && greyc[0] * 2 > modc[0] - 10 ? "half, correct" : "NOT half: colour ignored");

	return 0;
}
