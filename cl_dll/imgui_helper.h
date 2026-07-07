#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void ImGuiHelper_Init();
void ImGuiHelper_VidInit();
void ImGuiHelper_Shutdown();
void ImGuiHelper_Draw();
int ImGuiHelper_HandleKey(int down, int keynum, const char *pszCurrentBinding);

// Global status to check if ImGui is capturing input/active
extern bool g_ShowImGuiMenu;
extern bool g_ShowCrosshairEditor;
extern bool g_ShowAGSettings;
extern bool g_ShowPauseMenu;
extern bool g_ShowMainMenu;
extern bool g_IsBackgroundMap;
extern "C" struct cvar_s* cl_custom_hud;
extern struct cvar_s* cl_custom_menu;

void ImGuiHelper_UpdateInputState();

#ifdef __cplusplus
}
#endif
