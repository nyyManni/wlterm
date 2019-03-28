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
    GLuint bg_shader;

    /* Window contents. */

    GLuint projection_uniform;
    GLuint bg_projection_uniform;
    GLuint bg_accent_color_uniform;
    GLuint color_uniform;
    GLuint offset_uniform;
    
    

    /* Scrolling stuff */
    double position[2];
    int32_t __position_pending[2];
    double inertia[2]; /* Pixels per second */
    uint32_t axis_time[2];
    double velocity[2];
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
