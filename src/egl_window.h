#ifndef EGL_WINDOW_H
#define EGL_WINDOW_H

#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdbool.h>

#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <msdfgl.h>

#include <cglm/mat4.h>

#define MAX_FRAMES 64
#define SCROLL_WINDOW_SIZE 5

struct wlterm_window;
struct wlterm_frame;

struct wlterm_application {

    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct xdg_wm_base *xdg_wm_base;

    struct wl_seat *seat;
    struct wl_shm *shm;

    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
    struct wl_keyboard *kbd;
    struct wl_pointer *pointer;

    EGLDisplay gl_display;
    EGLConfig gl_conf;
    EGLContext gl_context;

    msdfgl_context_t msdfgl_ctx;
    struct wlterm_frame *root_frame;
};
/* extern struct wlterm_application wlterm_ctx; */

extern struct wlterm_frame *active_frame;
extern struct wlterm_frame *frames[];
extern int open_frames;
extern msdfgl_font_t active_font;

struct wlterm_frame {
    struct wlterm_application *application;

    struct wlterm_frame *next;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    struct wl_buffer *buffer;
    void *shm_data;

    bool open;
    int width;
    int height;

    double scale;

    /* OpenGL */
    struct wl_egl_window *gl_window;

    EGLContext gl_context;
    EGLConfig gl_conf;
    EGLDisplay gl_display;
    EGLSurface gl_surface;

    mat4 projection;

    struct wlterm_window *root_window;

};

struct wlterm_window {
    struct wlterm_frame *frame;
    struct wlterm_window *next;

    /* Window position in frame's coordinates */
    int x;
    int y;

    int width;
    int height;

    mat4 projection;
};

struct wlterm_application *wlterm_application_create();
void wlterm_application_destroy(struct wlterm_application *);
int wlterm_application_run(struct wlterm_application *);
struct wlterm_frame *wlterm_frame_create(struct wlterm_application *);
void wlterm_frame_destroy(struct wlterm_frame *);
struct wlterm_window *wlterm_window_create(struct wlterm_frame *);
void wlterm_window_destroy(struct wlterm_window *);


#define FOR_EACH_WINDOW(frame, w) \
    for (struct wlterm_window *w = frame->root_window; w; w = w->next)

void wlterm_frame_resize(struct wlterm_frame *, int, int);
/* struct window *window_create(struct frame *); */
void wlterm_frame_render(struct wlterm_frame *);

#define WLTERM_CHECK_GLERROR \
    do {                                                             \
        GLenum err = glGetError();                                   \
        if (err) {                                                   \
            fprintf(stderr, "line %d error: %x \n", __LINE__, err);  \
        }                                                            \
    } while (0);

#endif /* EGL_WINDOW_H */
