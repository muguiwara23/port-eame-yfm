#ifndef MEMORIES_PC_COMPAT_SDL_OPENGL_H
#define MEMORIES_PC_COMPAT_SDL_OPENGL_H
/* SDL's OpenGL declarations (its embedded gl.h and glext.h: the types, the
 * constants and the PFNGL...PROC function-pointer types the picture pass
 * loads through SDL_GL_GetProcAddress), on every platform.
 *
 * SDL_opengl.h declares nothing on iOS ("No OpenGL on iOS"), whose GL is
 * OpenGL ES. The port draws there as it does on Android: an ES 3 context
 * through these same declarations, with the entry points looked up at run
 * time. So on iOS the header is read as on any other platform. Nothing here
 * links a desktop GL symbol: gl_desktop_none.c stands in for the
 * fixed-function calls, which that path never makes. */
#include <SDL3/SDL_platform.h>
#ifdef SDL_PLATFORM_IOS
#undef SDL_PLATFORM_IOS
#include <SDL3/SDL_opengl.h>
#define SDL_PLATFORM_IOS 1
#else
#include <SDL3/SDL_opengl.h>
#endif

#endif
