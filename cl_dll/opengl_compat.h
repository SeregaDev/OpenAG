#pragma once

#include <SDL2/SDL_opengl.h>

#if defined(_WIN32)
static inline void APIENTRY glActiveTexture(GLenum texture)
{
    static void (APIENTRY *pfn)(GLenum) = nullptr;
    if (!pfn)
        pfn = reinterpret_cast<void (APIENTRY *)(GLenum)>(wglGetProcAddress("glActiveTexture"));
    if (pfn)
        pfn(texture);
}
#endif
