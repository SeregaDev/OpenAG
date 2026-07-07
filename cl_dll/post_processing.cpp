#include "hud.h"
#include "cl_util.h"
#include "post_processing.h"
#include "bloom.h"
#include "motion_blur.h"

#include <SDL2/SDL_opengl.h>
#include <math.h>

namespace post_processing
{
	static cvar_t* cl_pp_enabled = nullptr;
	static cvar_t* cl_pp_contrast = nullptr;
	static cvar_t* cl_pp_brightness = nullptr;
	static cvar_t* cl_pp_saturation = nullptr;
	static cvar_t* cl_pp_vignette = nullptr;
	static cvar_t* cl_pp_scanlines = nullptr;
	static cvar_t* cl_pp_noise = nullptr;
	static cvar_t* cl_pp_tint_r = nullptr;
	static cvar_t* cl_pp_tint_g = nullptr;
	static cvar_t* cl_pp_tint_b = nullptr;
	static cvar_t* cl_pp_tint_strength = nullptr;
	static cvar_t* cl_pp_grayscale = nullptr;
	static cvar_t* cl_pp_sepia = nullptr;
	static cvar_t* cl_pp_vhs_film = nullptr;
	static cvar_t* cl_pp_vhs_strength = nullptr;

	static bool HasLightweightEffects()
	{
		if (!cl_pp_vignette || !cl_pp_scanlines || !cl_pp_noise)
			return false;

		return (cl_pp_vignette->value > 0.0f) ||
		       (cl_pp_scanlines->value > 0.0f) ||
		       (cl_pp_noise->value > 0.0f) ||
		       (cl_pp_brightness && fabsf(cl_pp_brightness->value) > 0.001f) ||
		       (cl_pp_contrast && fabsf(cl_pp_contrast->value - 1.0f) > 0.001f) ||
		       (cl_pp_saturation && fabsf(cl_pp_saturation->value - 1.0f) > 0.001f) ||
		       (cl_pp_tint_strength && cl_pp_tint_strength->value > 0.001f);
	}

	static void ApplyVignette()
	{
		if (!cl_pp_vignette || cl_pp_vignette->value <= 0.0f)
			return;

		float amount = cl_pp_vignette->value;
		glPushAttrib(GL_COLOR_BUFFER_BIT | GL_ENABLE_BIT);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glColor4f(0.0f, 0.0f, 0.0f, amount * 0.35f);
		glBegin(GL_QUADS);
		glVertex2f(0.0f, 0.0f); glVertex2f(1.0f, 0.0f); glVertex2f(1.0f, 1.0f); glVertex2f(0.0f, 1.0f);
		glEnd();
		glPopAttrib();
	}

	static void ApplyScanlines()
	{
		if (!cl_pp_scanlines || cl_pp_scanlines->value <= 0.0f)
			return;

		float amount = cl_pp_scanlines->value;
		glPushAttrib(GL_ENABLE_BIT | GL_LINE_BIT | GL_COLOR_BUFFER_BIT);
		glDisable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glColor4f(0.0f, 0.0f, 0.0f, amount * 0.08f);
		for (int y = 0; y < 480; y += 2)
		{
			glBegin(GL_LINES);
			glVertex2f(0.0f, (float)y / 480.0f);
			glVertex2f(1.0f, (float)y / 480.0f);
			glEnd();
		}
		glPopAttrib();
	}

	static void ApplyNoise()
	{
		if (!cl_pp_noise || cl_pp_noise->value <= 0.0f)
			return;

		float amount = cl_pp_noise->value;
		glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT);
		glDisable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glColor4f(1.0f, 1.0f, 1.0f, amount * 0.05f);
		for (int i = 0; i < 80; i++)
		{
			float x = (float)(i % 16) / 16.0f;
			float y = (float)(i / 16) / 5.0f;
			glBegin(GL_POINTS);
			glVertex2f(x, y);
			glEnd();
		}
		glPopAttrib();
	}

	static void ApplyColorAdjustments()
	{
		float brightness = cl_pp_brightness ? cl_pp_brightness->value : 0.0f;
		float contrast = cl_pp_contrast ? cl_pp_contrast->value : 1.0f;
		float saturation = cl_pp_saturation ? cl_pp_saturation->value : 1.0f;
		float tintStrength = cl_pp_tint_strength ? cl_pp_tint_strength->value : 0.0f;
		float tintR = cl_pp_tint_r ? cl_pp_tint_r->value : 1.0f;
		float tintG = cl_pp_tint_g ? cl_pp_tint_g->value : 1.0f;
		float tintB = cl_pp_tint_b ? cl_pp_tint_b->value : 1.0f;

		if (fabsf(brightness) < 0.001f && fabsf(contrast - 1.0f) < 0.001f && fabsf(saturation - 1.0f) < 0.001f && tintStrength <= 0.001f)
			return;

		glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		if (fabsf(brightness) > 0.001f)
		{
			float alpha = fabsf(brightness) * 0.10f;
			float r = brightness > 0.0f ? 1.0f : 0.0f;
			float g = brightness > 0.0f ? 1.0f : 0.0f;
			float b = brightness > 0.0f ? 1.0f : 0.0f;
			glDisable(GL_TEXTURE_2D);
			glColor4f(r, g, b, alpha);
			glBegin(GL_QUADS);
			glVertex2f(0.0f, 0.0f); glVertex2f(1.0f, 0.0f); glVertex2f(1.0f, 1.0f); glVertex2f(0.0f, 1.0f);
			glEnd();
		}

		if (fabsf(contrast - 1.0f) > 0.001f)
		{
			float delta = contrast - 1.0f;
			float alpha = fabsf(delta) * 0.06f;
			glDisable(GL_TEXTURE_2D);
			glColor4f(0.15f, 0.15f, 0.15f, alpha);
			glBegin(GL_QUADS);
			glVertex2f(0.0f, 0.0f); glVertex2f(1.0f, 0.0f); glVertex2f(1.0f, 1.0f); glVertex2f(0.0f, 1.0f);
			glEnd();
		}

		if (fabsf(saturation - 1.0f) > 0.001f)
		{
			float alpha = fabsf(saturation - 1.0f) * 0.08f;
			glDisable(GL_TEXTURE_2D);
			glColor4f(0.5f, 0.5f, 0.5f, alpha);
			glBegin(GL_QUADS);
			glVertex2f(0.0f, 0.0f); glVertex2f(1.0f, 0.0f); glVertex2f(1.0f, 1.0f); glVertex2f(0.0f, 1.0f);
			glEnd();
		}

		if (tintStrength > 0.001f)
		{
			glDisable(GL_TEXTURE_2D);
			glColor4f(tintR, tintG, tintB, tintStrength * 0.10f);
			glBegin(GL_QUADS);
			glVertex2f(0.0f, 0.0f); glVertex2f(1.0f, 0.0f); glVertex2f(1.0f, 1.0f); glVertex2f(0.0f, 1.0f);
			glEnd();
		}

		glPopAttrib();
	}

	void init()
	{
		cl_pp_enabled = CVAR_CREATE("cl_pp_enabled", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
		cl_pp_contrast = CVAR_CREATE("cl_pp_contrast", "1.0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
		cl_pp_brightness = CVAR_CREATE("cl_pp_brightness", "0.0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
		cl_pp_saturation = CVAR_CREATE("cl_pp_saturation", "1.0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
		cl_pp_vignette = CVAR_CREATE("cl_pp_vignette", "0.0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
		cl_pp_scanlines = CVAR_CREATE("cl_pp_scanlines", "0.0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
		cl_pp_noise = CVAR_CREATE("cl_pp_noise", "0.0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
		cl_pp_tint_r = CVAR_CREATE("cl_pp_tint_r", "1.0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
		cl_pp_tint_g = CVAR_CREATE("cl_pp_tint_g", "1.0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
		cl_pp_tint_b = CVAR_CREATE("cl_pp_tint_b", "1.0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
		cl_pp_tint_strength = CVAR_CREATE("cl_pp_tint_strength", "0.0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE);
	}

	void draw()
	{
		if (!cl_pp_enabled || cl_pp_enabled->value == 0.0f)
			return;
		if (!HasLightweightEffects())
			return;

		glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_VIEWPORT_BIT);
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_LIGHTING);
		glDisable(GL_CULL_FACE);
		glDisable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		ApplyVignette();
		ApplyScanlines();
		ApplyNoise();
		ApplyColorAdjustments();

		glPopMatrix();
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glPopAttrib();
	}
}
