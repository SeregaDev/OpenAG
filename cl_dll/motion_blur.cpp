#include "hud.h"
#include "cl_util.h"
#include "motion_blur.h"

#include <SDL2/SDL_opengl.h>
#include <math.h>

namespace motion_blur
{
	static cvar_t* cl_motion_blur = nullptr;
	static cvar_t* cl_motion_blur_time = nullptr;
	static cvar_t* cl_motion_blur_chromatic = nullptr;

	static GLuint g_AccumTexture = 0;
	static int g_LastWidth = 0;
	static int g_LastHeight = 0;

	static int NextPowerOfTwo(int n)
	{
		int val = 1;
		while (val < n)
			val *= 2;
		return val;
	}

	static void InitTextures(int w, int h, bool& is_new)
	{
		int tw = NextPowerOfTwo(w);
		int th = NextPowerOfTwo(h);

		if (g_AccumTexture == 0 || g_LastWidth != w || g_LastHeight != h)
		{
			if (g_AccumTexture != 0)
			{
				glDeleteTextures(1, &g_AccumTexture);
			}

			glGenTextures(1, &g_AccumTexture);
			glBindTexture(GL_TEXTURE_2D, g_AccumTexture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

			g_LastWidth = w;
			g_LastHeight = h;
			is_new = true;
		}
	}

	void init()
	{
		cl_motion_blur = CVAR_CREATE("cl_motion_blur", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
		cl_motion_blur_time = CVAR_CREATE("cl_motion_blur_time", "0.15", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
		cl_motion_blur_chromatic = CVAR_CREATE("cl_motion_blur_chromatic", "1.5", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
	}

	void draw()
	{
		if (!cl_motion_blur || cl_motion_blur->value == 0.0f)
			return;

		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);
		int w = viewport[2];
		int h = viewport[3];

		if (w <= 0 || h <= 0)
			return;

		bool is_new = false;
		InitTextures(w, h, is_new);

		int tw = NextPowerOfTwo(w);
		int th = NextPowerOfTwo(h);

		if (is_new)
		{
			glBindTexture(GL_TEXTURE_2D, g_AccumTexture);
			glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
			return;
		}

		// Calculate frame-rate independent blend factor (alpha)
		float dt = (float)gHUD.m_flTimeDelta;
		if (dt < 0.001f) dt = 0.001f;
		if (dt > 0.1f) dt = 0.1f;

		float decay_time = cl_motion_blur_time ? cl_motion_blur_time->value : 0.15f;
		if (decay_time < 0.01f) decay_time = 0.01f;

		float blur_alpha = expf(-dt / decay_time);
		if (blur_alpha < 0.0f) blur_alpha = 0.0f;
		if (blur_alpha > 0.98f) blur_alpha = 0.98f;

		// Push OpenGL attributes and matrices
		glPushAttrib(GL_ALL_ATTRIB_BITS);
		glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);

		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glMatrixMode(GL_TEXTURE);
		glPushMatrix();

		// Set up 2D orthographic projection
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);

		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();

		// Disable all client arrays to prevent crashes with stale engine pointers
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_NORMAL_ARRAY);

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_ALPHA_TEST);
		glDisable(GL_LIGHTING);
		glDisable(GL_CULL_FACE);
		glDisable(GL_SCISSOR_TEST);

		// Select texture unit 0 and disable units 1 & 2
		glActiveTexture(GL_TEXTURE1);
		glDisable(GL_TEXTURE_2D);
		glActiveTexture(GL_TEXTURE2);
		glDisable(GL_TEXTURE_2D);
		glActiveTexture(GL_TEXTURE0);
		glEnable(GL_TEXTURE_2D);

		glBindTexture(GL_TEXTURE_2D, g_AccumTexture);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

		float u = (float)w / tw;
		float v = (float)h / th;

		// Simple LCG random generator for sub-pixel jitter to smooth out ghosting/stepping
		static unsigned int seed = 12345;
		seed = seed * 1664525 + 1013904223;
		float jx = (((float)(seed & 0xFFFF) / 65535.0f) - 0.5f) * 0.5f; // -0.25 to 0.25 pixels
		seed = seed * 1664525 + 1013904223;
		float jy = (((float)(seed & 0xFFFF) / 65535.0f) - 0.5f) * 0.5f; // -0.25 to 0.25 pixels

		float jx_offset = jx / w;
		float jy_offset = jy / h;

		float chromatic_strength = cl_motion_blur_chromatic ? cl_motion_blur_chromatic->value : 1.5f;
		float shift_x = chromatic_strength / w;

		if (chromatic_strength > 0.0f)
		{
			// 1. Red Channel Pass
			glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE);
			glColor4f(1.0f, 1.0f, 1.0f, blur_alpha);
			glBegin(GL_QUADS);
			glTexCoord2f(0.0f, 0.0f); glVertex2f(-shift_x + jx_offset, jy_offset);
			glTexCoord2f(u,    0.0f); glVertex2f(1.0f - shift_x + jx_offset, jy_offset);
			glTexCoord2f(u,    v   ); glVertex2f(1.0f - shift_x + jx_offset, 1.0f + jy_offset);
			glTexCoord2f(0.0f, v   ); glVertex2f(-shift_x + jx_offset, 1.0f + jy_offset);
			glEnd();

			// 2. Green Channel Pass
			glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_FALSE);
			glColor4f(1.0f, 1.0f, 1.0f, blur_alpha);
			glBegin(GL_QUADS);
			glTexCoord2f(0.0f, 0.0f); glVertex2f(jx_offset, jy_offset);
			glTexCoord2f(u,    0.0f); glVertex2f(1.0f + jx_offset, jy_offset);
			glTexCoord2f(u,    v   ); glVertex2f(1.0f + jx_offset, 1.0f + jy_offset);
			glTexCoord2f(0.0f, v   ); glVertex2f(jx_offset, 1.0f + jy_offset);
			glEnd();

			// 3. Blue Channel Pass
			glColorMask(GL_FALSE, GL_FALSE, GL_TRUE, GL_FALSE);
			glColor4f(1.0f, 1.0f, 1.0f, blur_alpha);
			glBegin(GL_QUADS);
			glTexCoord2f(0.0f, 0.0f); glVertex2f(shift_x + jx_offset, jy_offset);
			glTexCoord2f(u,    0.0f); glVertex2f(1.0f + shift_x + jx_offset, jy_offset);
			glTexCoord2f(u,    v   ); glVertex2f(1.0f + shift_x + jx_offset, 1.0f + jy_offset);
			glTexCoord2f(0.0f, v   ); glVertex2f(shift_x + jx_offset, 1.0f + jy_offset);
			glEnd();

			// Restore full color mask
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		}
		else
		{
			glColor4f(1.0f, 1.0f, 1.0f, blur_alpha);
			glBegin(GL_QUADS);
			glTexCoord2f(0.0f, 0.0f); glVertex2f(jx_offset, jy_offset);
			glTexCoord2f(u,    0.0f); glVertex2f(1.0f + jx_offset, jy_offset);
			glTexCoord2f(u,    v   ); glVertex2f(1.0f + jx_offset, 1.0f + jy_offset);
			glTexCoord2f(0.0f, v   ); glVertex2f(jx_offset, 1.0f + jy_offset);
			glEnd();
		}

		// 2. Capture the blended result from the framebuffer back into g_AccumTexture
		glBindTexture(GL_TEXTURE_2D, g_AccumTexture);
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);

		// Restore OpenGL state
		glMatrixMode(GL_TEXTURE);
		glPopMatrix();
		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();

		glPopClientAttrib();
		glPopAttrib();
	}
}
