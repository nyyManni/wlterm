#ifndef EGL_UTIL_H
#define EGL_UTIL_H
#include <stdlib.h>
#include <sys/time.h>
#include <GLES2/gl2.h>
#include <stdbool.h>
#include <EGL/egl.h>

bool check_egl_extension(const char *extensions, const char *extension);

void *platform_get_egl_proc_address(const char *address);

EGLDisplay platform_get_egl_display(EGLenum platform, void *native_display,
                                           const EGLint *attrib_list);

EGLSurface platform_create_egl_surface(EGLDisplay dpy, EGLConfig config,
                                              void *native_window, const EGLint *attrib_list);

EGLBoolean platform_destroy_egl_surface(EGLDisplay display, EGLSurface surface);

char *read_file(const char *filename);

GLuint create_shader(const char *source, GLenum shader_type);

static inline GLuint make_shader(char *filename, GLenum type) {
    
    char *src = read_file(filename);
    GLuint shader = create_shader(src, type);
    free(src);
    return shader;
}

static inline uint32_t timestamp() {
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

#endif /* EGL_UTIL_H */
