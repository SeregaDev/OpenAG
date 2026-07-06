#pragma once
#include <complex>

struct strafe_data_t
{
	float speed;
	float accel;
	float efficiency;
	bool wrong_key;
	int strafe_state; // 0 = none, 1 = perfect, 2 = understrafe, 3 = overstrafe, 4 = speed loss, 5 = wrong key
	bool on_ground;
	int keys;
};

extern strafe_data_t g_StrafeData;

class CHudStrafeGuide : public CHudBase
{
	double angles[6] = {0.};
	
	std::complex<double> lastSimvel = 0.;
	
	cvar_t* hud_strafeguide;
	cvar_t* hud_strafeguide_zoom;
	cvar_t* hud_strafeguide_height;
	cvar_t* hud_strafeguide_size;
	
public:
	virtual int Init();
	virtual int VidInit();
	virtual int Draw(float time);

	void Update(struct ref_params_s *ppmove);
};
