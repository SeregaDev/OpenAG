#define _USE_MATH_DEFINES
#include <cmath>
#include <complex>

#include "hud.h"
#include "cl_util.h"

#include "pm_defs.h"
#include "pm_movevars.h"

enum border {
	RED_GREEN,
	GREEN_WHITE,
	WHITE_GREEN,
	GREEN_RED
};

strafe_data_t g_StrafeData = { 0.0f, 0.0f, 0.0f, false, 0, false, 0 };

cvar_t* cl_speedometer = nullptr;
cvar_t* cl_speedometer_show_strafes = nullptr;

int CHudStrafeGuide::Init()
{
	m_iFlags = HUD_ACTIVE;

	hud_strafeguide = CVAR_CREATE("hud_strafeguide", "0", FCVAR_ARCHIVE);
	hud_strafeguide_zoom = CVAR_CREATE("hud_strafeguide_zoom", "1", FCVAR_ARCHIVE);
	hud_strafeguide_height = CVAR_CREATE("hud_strafeguide_height", "0", FCVAR_ARCHIVE);
	hud_strafeguide_size = CVAR_CREATE("hud_strafeguide_size", "0", FCVAR_ARCHIVE);

	cl_speedometer = CVAR_CREATE("cl_speedometer", "1", FCVAR_ARCHIVE);
	cl_speedometer_show_strafes = CVAR_CREATE("cl_speedometer_show_strafes", "1", FCVAR_ARCHIVE);

	gHUD.AddHudElem(this);
	return 0;
}

int CHudStrafeGuide::VidInit()
{
	return 1;
}

int CHudStrafeGuide::Draw(float time)
{
	if (hud_strafeguide->value == 0)
		return 0;
	
	double fov  = gHUD.default_fov->value / 180 * M_PI / 2;
	double zoom = hud_strafeguide_zoom->value;
	
	int size = gHUD.m_iFontHeight;
	int height = ScreenHeight / 2 - 2*size;
	
	if (hud_strafeguide_size->value != 0)
		size = hud_strafeguide_size->value;
	
	if (hud_strafeguide_height->value != 0)
		height = hud_strafeguide_height->value;
	
	for (int i = 0; i < 4; ++i) {
		int r, g, b;
		switch (i) {
			case RED_GREEN: case WHITE_GREEN:
				r = 0; g = 255; b = 0; break;
			case GREEN_WHITE:
				r = 255; g = 255; b = 255; break;
			case GREEN_RED:
				r = 255; g = 0; b = 0; break;
		}
		
		double boxLeftBase  = -angles[i];
		double boxRightBase = -angles[(i+1)%4];
		
		if (std::abs(boxLeftBase - boxRightBase) < 1e-10)
			continue;
		if (boxLeftBase >= boxRightBase)
			boxRightBase += 2 * M_PI;
		if (std::abs(boxLeftBase - boxRightBase) < 1e-10)
			continue;
		
		for (int iCopy = -8; iCopy <= 8; ++iCopy) {
			double boxLeft  = boxLeftBase  + iCopy * 2 * M_PI;
			double boxRight = boxRightBase + iCopy * 2 * M_PI;
			boxLeft  *= zoom;
			boxRight *= zoom;
			
			if (std::abs(boxLeft) > fov && std::abs(boxRight) > fov && boxRight * boxLeft > 0)
				continue;
			
			boxLeft  = boxLeft  > fov ? fov : boxLeft  < -fov ? -fov : boxLeft;
			boxRight = boxRight > fov ? fov : boxRight < -fov ? -fov : boxRight;
			
			boxLeft  = std::tan(boxLeft ) / std::tan(fov);
			boxRight = std::tan(boxRight) / std::tan(fov);
			
			int boxLeftI  = boxLeft / 1 * ScreenWidth / 2;
			int boxRightI = boxRight/ 1 * ScreenWidth / 2;
			boxLeftI  += ScreenWidth / 2;
			boxRightI += ScreenWidth / 2;
			
			FillRGBA(boxLeftI, height, boxRightI-boxLeftI, size, r, g, b, 60);
		}
	}
	
	return 0;
}

static double angleReduce(double a)
{
	double tmp = std::fmod(a, 2*M_PI);
	if (tmp < 0) tmp += 2*M_PI;
	if (tmp > M_PI) tmp -= 2*M_PI;
	return tmp;
}

void CHudStrafeGuide::Update(struct ref_params_s *pparams)
{
	double frameTime = pparams->frametime;
	auto input = std::complex<double>(pparams->cmd->forwardmove, pparams->cmd->sidemove);
	double viewAngle = pparams->viewangles[1] / 180 * M_PI;
	
	std::complex<double> velocity = lastSimvel;
	lastSimvel = std::complex<double>(pparams->simvel[0], pparams->simvel[1]);
	double velocityAbs = std::abs(velocity);
	bool onground = pparams->onground;

	// Update base strafe data
	g_StrafeData.speed = (float)velocityAbs;
	g_StrafeData.on_ground = onground;
	g_StrafeData.keys = pparams->cmd->buttons;

	static double last_speed = 0.0;
	if (frameTime > 0.0)
	{
		g_StrafeData.accel = (float)((velocityAbs - last_speed) / frameTime);
	}
	last_speed = velocityAbs;

	if (std::norm(input) == 0) {
		for (int i = 0; i < 4; ++i) {
			if (i < 2)
				angles[i] = M_PI;
			else
				angles[i] = -M_PI;
		}
		g_StrafeData.wrong_key = false;
		g_StrafeData.strafe_state = 0; // none
		g_StrafeData.efficiency = 0.0f;
		return;
	}

	double accelCoeff = onground ? pparams->movevars->accelerate : pparams->movevars->airaccelerate;
	//TODO: grab the entity friction from somewhere. pparams->movevars->friction is sv_friction
	//just use the default 1 for now
	double frictionCoeff = 1;
	
	double inputAbs = std::abs(input);
	if (onground)
		inputAbs = min(inputAbs, pparams->movevars->maxspeed);
	else
		inputAbs = min(inputAbs, 30);
	
	input *= inputAbs / std::abs(input);
	
	double uncappedAccel = accelCoeff * frictionCoeff * inputAbs * frameTime;
	
	if (uncappedAccel >= 2 * velocityAbs)
		angles[RED_GREEN] = M_PI;
	else
		angles[RED_GREEN] = std::acos(-uncappedAccel / velocityAbs / 2);
	
	if (velocityAbs <= inputAbs)
		angles[GREEN_WHITE] = 0;
	else
		angles[GREEN_WHITE] = std::acos(inputAbs / velocityAbs);
	
	angles[GREEN_RED] = -angles[RED_GREEN];
	angles[WHITE_GREEN] = -angles[GREEN_WHITE];
	
	double inputAngle = std::log(input).imag();
	double velocityAngle;
	
	if (velocityAbs == 0)
		velocityAngle = 0;
	else
		velocityAngle = std::log(velocity).imag();
	
	for (int i = 0; i < 4; ++i) {
		angles[i] += velocityAngle + inputAngle - viewAngle;
		angles[i] = angleReduce(angles[i]);
	}

	// Calculate and fill strafe analyzer data
	double actual_diff = std::abs(angleReduce(viewAngle - velocityAngle - inputAngle));
	double opt_angle = (velocityAbs <= inputAbs) ? 0.0 : std::acos(inputAbs / velocityAbs);
	double max_accel_angle = (uncappedAccel >= 2 * velocityAbs) ? M_PI : std::acos(-uncappedAccel / velocityAbs / 2);

	double diff_to_opt = std::abs(actual_diff - opt_angle);

	g_StrafeData.wrong_key = (!onground && ((pparams->cmd->buttons & IN_FORWARD) || (pparams->cmd->buttons & IN_BACK)));
	
	if (g_StrafeData.wrong_key)
	{
		g_StrafeData.strafe_state = 5; // wrong key
		g_StrafeData.efficiency = 0.0f;
	}
	else if (velocityAbs < 50.0)
	{
		g_StrafeData.strafe_state = 0; // none
		g_StrafeData.efficiency = 1.0f;
	}
	else if (diff_to_opt < 0.05) // within ~3 degrees
	{
		g_StrafeData.strafe_state = 1; // perfect
		g_StrafeData.efficiency = 1.0f - (float)(diff_to_opt / 0.05) * 0.1f;
	}
	else if (actual_diff < opt_angle)
	{
		g_StrafeData.strafe_state = 2; // understrafe
		g_StrafeData.efficiency = (opt_angle > 0.001) ? (float)(actual_diff / opt_angle) : 1.0f;
	}
	else if (actual_diff < max_accel_angle)
	{
		g_StrafeData.strafe_state = 3; // overstrafe
		double range = max_accel_angle - opt_angle;
		g_StrafeData.efficiency = (range > 0.001) ? (float)(1.0 - (actual_diff - opt_angle) / range) : 0.0f;
	}
	else
	{
		g_StrafeData.strafe_state = 4; // speed loss
		g_StrafeData.efficiency = 0.0f;
	}
}
