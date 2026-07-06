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

	static void CheckGLError(const char* label)
	{
		GLenum err;
		while ((err = glGetError()) != GL_NO_ERROR)
		{
			gEngfuncs.Con_Printf("[Bloom] GL Error at %s: 0x%x\n", label, err);
		}
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
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			CheckGLError("Init g_ScreenTexture");

			g_LastWidth = w;
			g_LastHeight = h;
		}

		if (g_BloomTexture == 0)
		{
			glGenTextures(1, &g_BloomTexture);
			glBindTexture(GL_TEXTURE_2D, g_BloomTexture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			CheckGLError("Init g_BloomTexture");
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
		CheckGLError("Capture screen glCopyTexSubImage2D");

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

		// Disable client vertex arrays to prevent crashes or using stale engine pointers in immediate mode glBegin/glEnd
		glDisableClientState(GL_VERTEX_ARRAY);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_NORMAL_ARRAY);

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_ALPHA_TEST);
		glDisable(GL_LIGHTING);
		glDisable(GL_CULL_FACE);
		glDisable(GL_SCISSOR_TEST);

		// Ensure we are working on texture unit 0 and disable unit 1 and 2
		glActiveTexture(GL_TEXTURE1);
		glDisable(GL_TEXTURE_2D);
		glActiveTexture(GL_TEXTURE2);
		glDisable(GL_TEXTURE_2D);
		glActiveTexture(GL_TEXTURE0);
		glEnable(GL_TEXTURE_2D);

		// Set texture environment to REPLACE to copy pixels exactly as they are without tinting
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

		CheckGLError("Setup rendering state");

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

		CheckGLError("Draw downscaled screen");

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

		CheckGLError("Apply threshold passes");

		// 3. Copy downscaled & thresholded result to g_BloomTexture
		glBindTexture(GL_TEXTURE_2D, g_BloomTexture);
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 256, 256);
		CheckGLError("Copy to g_BloomTexture");

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

		CheckGLError("Restore bottom-left corner");

		// 6. Draw bloom texture additively over the whole screen in multiple passes for smooth blur
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE); // Additive blend

		// Change texture environment to MODULATE so the texture color is multiplied by the quad color (glColor4f)
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

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
