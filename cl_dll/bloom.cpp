#include "hud.h"
#include "cl_util.h"
#include "bloom.h"

#include <SDL2/SDL_opengl.h>
#include <math.h>

namespace bloom
{
	static cvar_t* cl_bloom = nullptr;
	static cvar_t* cl_bloom_intensity = nullptr;
	static cvar_t* cl_bloom_radius = nullptr;
	static cvar_t* cl_bloom_darkness = nullptr;

	static GLuint g_ScreenTexture = 0;
	static GLuint g_BloomTexture = 0;
	static int g_LastWidth = 0;
	static int g_LastHeight = 0;

	static int NextPowerOfTwo(int n)
	{
		int val = 1;
		while (val < n)
			val *= 2;
		return val;
	}

	static void InitTextures(int w, int h)
	{
		int tw = NextPowerOfTwo(w);
		int th = NextPowerOfTwo(h);

		if (g_ScreenTexture == 0 || g_LastWidth != w || g_LastHeight != h)
		{
			if (g_ScreenTexture != 0)
			{
				glDeleteTextures(1, &g_ScreenTexture);
			}

			glGenTextures(1, &g_ScreenTexture);
			glBindTexture(GL_TEXTURE_2D, g_ScreenTexture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tw, th, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);

			g_LastWidth = w;
			g_LastHeight = h;
		}

		if (g_BloomTexture == 0)
		{
			glGenTextures(1, &g_BloomTexture);
			glBindTexture(GL_TEXTURE_2D, g_BloomTexture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 256, 256, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
		}
	}

	void init()
	{
		cl_bloom = CVAR_CREATE("cl_bloom", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
		cl_bloom_intensity = CVAR_CREATE("cl_bloom_intensity", "0.5", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
		cl_bloom_radius = CVAR_CREATE("cl_bloom_radius", "2.0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
		cl_bloom_darkness = CVAR_CREATE("cl_bloom_darkness", "1", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
	}

	void draw()
	{
		if (!cl_bloom || cl_bloom->value == 0.0f)
			return;

		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);
		int w = viewport[2];
		int h = viewport[3];

		if (w <= 0 || h <= 0)
			return;

		InitTextures(w, h);

		int tw = NextPowerOfTwo(w);
		int th = NextPowerOfTwo(h);

		// 1. Capture full screen into g_ScreenTexture
		glBindTexture(GL_TEXTURE_2D, g_ScreenTexture);
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);

		// Push OpenGL attributes and matrices to prevent state leakage to GoldSrc engine
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

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_ALPHA_TEST);
		glDisable(GL_LIGHTING);
		glDisable(GL_CULL_FACE);
		glDisable(GL_SCISSOR_TEST);
		glEnable(GL_TEXTURE_2D);

		// 2. Set viewport to 256x256 to downscale screen rendering on GPU
		glViewport(0, 0, 256, 256);

		// Draw the full-screen texture into the small viewport
		glBindTexture(GL_TEXTURE_2D, g_ScreenTexture);
		glDisable(GL_BLEND);
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

		float u = (float)w / tw;
		float v = (float)h / th;

		glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
		glTexCoord2f(u,    0.0f); glVertex2f(1.0f, 0.0f);
		glTexCoord2f(u,    v   ); glVertex2f(1.0f, 1.0f);
		glTexCoord2f(0.0f, v   ); glVertex2f(0.0f, 1.0f);
		glEnd();

		// Apply contrast/darkness thresholding by multiplying the texture by itself (squaring the color values)
		int darkness_passes = cl_bloom_darkness ? (int)cl_bloom_darkness->value : 1;
		if (darkness_passes > 0)
		{
			glEnable(GL_BLEND);
			glBlendFunc(GL_DST_COLOR, GL_ZERO); // Multiplicative blending
			for (int p = 0; p < darkness_passes; p++)
			{
				glBegin(GL_QUADS);
				glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
				glTexCoord2f(u,    0.0f); glVertex2f(1.0f, 0.0f);
				glTexCoord2f(u,    v   ); glVertex2f(1.0f, 1.0f);
				glTexCoord2f(0.0f, v   ); glVertex2f(0.0f, 1.0f);
				glEnd();
			}
		}

		// 3. Copy downscaled & thresholded result to g_BloomTexture
		glBindTexture(GL_TEXTURE_2D, g_BloomTexture);
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 256, 256);

		// 4. Restore original viewport
		glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

		// 5. Restore bottom-left corner of the screen using the cached full screen texture
		glBindTexture(GL_TEXTURE_2D, g_ScreenTexture);
		glDisable(GL_BLEND);
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

		float tx = 256.0f / w;
		float ty = 256.0f / h;

		float u_restore = 256.0f / tw;
		float v_restore = 256.0f / th;

		glBegin(GL_QUADS);
		glTexCoord2f(0.0f,      0.0f);      glVertex2f(0.0f, 0.0f);
		glTexCoord2f(u_restore, 0.0f);      glVertex2f(tx,   0.0f);
		glTexCoord2f(u_restore, v_restore); glVertex2f(tx,   ty);
		glTexCoord2f(0.0f,      v_restore); glVertex2f(0.0f,   ty);
		glEnd();

		// 6. Draw bloom texture additively over the whole screen in multiple passes for smooth blur
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE); // Additive blend

		glBindTexture(GL_TEXTURE_2D, g_BloomTexture);

		float bloom_intensity = cl_bloom_intensity ? cl_bloom_intensity->value : 0.5f;
		float bloom_radius = cl_bloom_radius ? cl_bloom_radius->value : 2.0f;

		float ox = bloom_radius / w;
		float oy = bloom_radius / h;

		// 3x3 offset rendering to blur the downscaled texture
		float color_val = bloom_intensity / 9.0f;
		glColor4f(color_val, color_val, color_val, 1.0f);

		for (int dx = -1; dx <= 1; dx++)
		{
			for (int dy = -1; dy <= 1; dy++)
			{
				float x_off = dx * ox;
				float y_off = dy * oy;

				glBegin(GL_QUADS);
				glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f + x_off, 0.0f + y_off);
				glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f + x_off, 0.0f + y_off);
				glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f + x_off, 1.0f + y_off);
				glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f + x_off, 1.0f + y_off);
				glEnd();
			}
		}

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
