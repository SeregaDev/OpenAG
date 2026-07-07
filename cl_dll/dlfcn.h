#pragma once

#ifdef _WIN32
#include <windows.h>

#ifndef RTLD_NOW
#define RTLD_NOW 0x02
#endif

#ifndef RTLD_NOLOAD
#define RTLD_NOLOAD 0x04
#endif

static inline void* dlopen(const char* path, int mode = 0)
{
    (void)path;
    (void)mode;
    return (void*)1;
}

static inline void* dlsym(void* handle, const char* symbol)
{
    (void)handle;
    (void)symbol;
    return nullptr;
}

static inline int dlclose(void* handle)
{
    (void)handle;
    return 0;
}

static inline const char* dlerror(void)
{
    return nullptr;
}

#else
#include_next <dlfcn.h>
#endif
