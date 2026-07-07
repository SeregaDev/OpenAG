@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
link /dump /symbols "C:\Users\-spectreee\Documents\GitHub\OpenAG\build\client.dir\Release\imgui_helper.obj" | findstr /I "cl_custom_hud"
