#ifndef EGL_WINDOW_H
#define EGL_WINDOW_H

#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdbool.h>

#include <GLES2/gl2.h>
#include <EGL/egl.h>

#define MAX_WINDOWS 64
#define SCALE 2

struct window {

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    struct wl_buffer *buffer;
    void *shm_data;

    bool open;
    int width;
    int height;

    /* OpenGL */
    struct wl_egl_window *gl_window;

    EGLContext gl_ctx;
    EGLConfig gl_conf;
    EGLDisplay gl_display;
    EGLSurface gl_surface;

    GLuint shader_program;
    GLuint text_shader;

    /* Window contents. */

    GLuint projection_uniform;
    GLuint color_uniform;
};

struct glyph {
    FT_ULong code;
    GLuint texture;

    GLfloat offset_x;
    GLfloat offset_y;
    GLfloat width;
    GLfloat height;

    int advance;
    int bearing_x;
    int bearing_y;
};


void init_egl();
void kill_egl();
bool load_font(const char *, int);
struct window *window_create();
void window_close(struct window *);
void window_render(struct window *);

#endif /* EGL_WINDOW_H */
