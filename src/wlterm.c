#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <xkbcommon/xkbcommon.h>

#include <wayland-client-protocol.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

static int counter = 0;

struct wl_display *g_display;
struct wl_compositor *g_compositor;
struct wl_seat *g_seat;
struct xkb_keymap *g_xkb_keymap;
struct xkb_state *g_xkb_state;
struct wl_keyboard *g_kbd;
struct wl_pointer *g_pointer;
struct xdg_wm_base *g_xdg_wm_base;
struct wl_shm *g_shm;

struct window {

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    struct wl_buffer *buffer;
    void *shm_data;

    cairo_t *cairo;

    bool open;
    int width;
    int height;

    /* bool inertia; */

    bool resized;
    bool configured;
    double position[2];
    int32_t __position_pending[2];
    double inertia[2]; /* Pixels per second */
    uint32_t axis_time[2];
    double velocity[2];
};

/* int32_t scroll[2]; */
/* uint32_t scroll_time[2]; */

/* bool configured = false; */
static char *font = "Mono";

int scale = 2;

bool running;
void draw(struct window *);

struct window *active_window;
#define MAX_WINDOWS 64
struct window *windows[MAX_WINDOWS];
int open_windows = 0;

static const char overflow[] = "[buffer overflow]";
static const int max_chars = 16384;
PangoLayout *get_pango_layout(cairo_t *cairo, const char *font, const char *text,
                              double scale, bool markup) {
    PangoLayout *layout = pango_cairo_create_layout(cairo);
    PangoAttrList *attrs;
    if (markup) {
        char *buf;
        GError *error = NULL;
        if (pango_parse_markup(text, -1, 0, &attrs, &buf, NULL, &error)) {
            pango_layout_set_text(layout, buf, -1);
            free(buf);
        } else {
            /* wlr_log(WLR_ERROR, "pango_parse_markup '%s' -> error %s", text, */
            /* 		error->message); */
            g_error_free(error);
            markup = false; // fallback to plain text
        }
    }
    if (!markup) {
        attrs = pango_attr_list_new();
        pango_layout_set_text(layout, text, -1);
    }

    pango_attr_list_insert(attrs, pango_attr_scale_new(scale));
    PangoFontDescription *desc = pango_font_description_from_string(font);
    pango_layout_set_font_description(layout, desc);
    pango_layout_set_single_paragraph_mode(layout, 1);
    pango_layout_set_attributes(layout, attrs);
    pango_attr_list_unref(attrs);
    pango_font_description_free(desc);
    return layout;
}

void get_text_size(cairo_t *cairo, const char *font, int *width, int *height,
                   int *baseline, double scale, bool markup, const char *fmt, ...) {
    char buf[max_chars];

    va_list args;
    va_start(args, fmt);
    if (vsnprintf(buf, sizeof(buf), fmt, args) >= max_chars) {
        strcpy(&buf[sizeof(buf) - sizeof(overflow)], overflow);
    }
    va_end(args);

    PangoLayout *layout = get_pango_layout(cairo, font, buf, scale, markup);
    pango_cairo_update_layout(cairo, layout);
    pango_layout_get_pixel_size(layout, width, height);
    if (baseline) {
        *baseline = pango_layout_get_baseline(layout) / PANGO_SCALE;
    }
    g_object_unref(layout);
}

void pango_printf(cairo_t *cairo, const char *font, double scale, bool markup,
                  const char *fmt, ...) {
    char buf[max_chars];

    va_list args;
    va_start(args, fmt);
    if (vsnprintf(buf, sizeof(buf), fmt, args) >= max_chars) {
        strcpy(&buf[sizeof(buf) - sizeof(overflow)], overflow);
    }
    va_end(args);

    PangoLayout *layout = get_pango_layout(cairo, font, buf, scale, markup);
    cairo_font_options_t *fo = cairo_font_options_create();
    cairo_get_font_options(cairo, fo);
    pango_cairo_context_set_font_options(pango_layout_get_context(layout), fo);
    cairo_font_options_destroy(fo);
    pango_cairo_update_layout(cairo, layout);
    pango_cairo_show_layout(cairo, layout);
    g_object_unref(layout);
}

void cairo_set_source_u32(cairo_t *cairo, uint32_t color) {
    cairo_set_source_rgba(
        cairo, (color >> (3 * 8) & 0xFF) / 255.0, (color >> (2 * 8) & 0xFF) / 255.0,
        (color >> (1 * 8) & 0xFF) / 255.0, (color >> (0 * 8) & 0xFF) / 255.0);
}

static void randname(char *buf) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long r = ts.tv_nsec;
    for (int i = 0; i < 6; ++i) {
        buf[i] = 'A' + (r & 15) + (r & 16) * 2;
        r >>= 5;
    }
}

static int anonymous_shm_open(void) {
    char name[] = "/emacs-XXXXXX";
    int retries = 100;

    do {
        randname(name + strlen(name) - 6);

        --retries;
        // shm_open guarantees that O_CLOEXEC is set
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);

    return -1;
}

int create_shm_file(off_t size) {
    int fd = anonymous_shm_open();
    if (fd < 0) {
        return fd;
    }

    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static void pointer_handle_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                                 struct wl_surface *surface, wl_fixed_t sx,
                                 wl_fixed_t sy) {
    /* struct display *display = data; */
    /* struct wl_buffer *buffer; */
    /* struct wl_cursor *cursor = display->default_cursor; */
    /* struct wl_cursor_image *image; */

    /* if (display->window->fullscreen) */
    /* 	wl_pointer_set_cursor(pointer, serial, NULL, 0, 0); */
    /* else if (cursor) { */
    /* 	image = display->default_cursor->images[0]; */
    /* 	buffer = wl_cursor_image_get_buffer(image); */
    /* 	if (!buffer) */
    /* 		return; */
    /* 	wl_pointer_set_cursor(pointer, serial, */
    /* 			      display->cursor_surface, */
    /* 			      image->hotspot_x, */
    /* 			      image->hotspot_y); */
    /* 	wl_surface_attach(display->cursor_surface, buffer, 0, 0); */
    /* 	wl_surface_damage(display->cursor_surface, 0, 0, */
    /* 			  image->width, image->height); */
    /* 	wl_surface_commit(display->cursor_surface); */
    /* } */
}

static void pointer_handle_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                                 struct wl_surface *surface) {}

static void pointer_handle_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                                  wl_fixed_t sx, wl_fixed_t sy) {

    /* fprintf(stderr, "axis motion\n"); */
}

static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
                                  uint32_t serial, uint32_t time, uint32_t button,
                                  uint32_t state) {
    /* struct display *display = data; */

    active_window->inertia[0] = active_window->inertia[1] = 0.0;
    /* if (!display->window->xdg_toplevel) */
    /*     return; */

    /* if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) */
    /*     zxdg_toplevel_v6_move(display->window->xdg_toplevel, display->seat, serial); */
}

static void pointer_handle_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time,
                                uint32_t axis, wl_fixed_t value) {

    uint32_t other_axis = !axis;

    /* static uint32_t a = 999; */
    /* if (a == axis) { */
    /*     scroll[0] = 0; */
    /*     scroll[1] = 0; */
    /* } */

    active_window->__position_pending[axis] = value;
    /* fprintf(stderr, "scroll time: %d\n", time); */

    active_window->velocity[axis] = (double)active_window->__position_pending[axis] /
                                    (time - active_window->axis_time[axis]);
    active_window->axis_time[axis] = time;
    /* fprintf(stderr, "scroll scroll scroll\n"); */

    /* Trigger a throttled redraw */
}

static void pointer_handle_frame(void *data, struct wl_pointer *wl_pointer) {

    if (!active_window->__position_pending[0] && !active_window->__position_pending[1])
        return;

    for (uint32_t axis = 0; axis < 2; ++axis) {

        /* active_window->axis_time[axis] = 0; */
        /* if (active_window->__position_pending[axis]) { */
        /* } else { */
        /*     active_window->velocity[axis] = 0.0; */
        /* } */

        active_window->position[axis] += active_window->__position_pending[axis] / 100.0;

        while (active_window->position[axis] < 0)
            active_window->position[axis] += 800;
        while (active_window->position[axis] > 800)
            active_window->position[axis] -= 800;

        /* a = axis; */
        /* scroll[axis] = value; */

        active_window->__position_pending[axis] = 0;
        /* active_window->__position_pending[1] = 0; */
    }

    /* fprintf(stderr, "scroll frame\n"); */
    wl_surface_commit(active_window->surface);
}

static void pointer_handle_axis_source(void *data, struct wl_pointer *wl_pointer,
                                       uint32_t axis_source) {
    /* fprintf(stderr, "axis source: %d\n", axis_source); */
}
static void pointer_handle_axis_stop(void *data, struct wl_pointer *wl_pointer,
                                     uint32_t time, uint32_t axis) {
    /* active_window->inertia = true; */
    active_window->inertia[axis] = 1.0;
    /* active_window->inertia[0] = active_window->inertia[0] = 0.0; */

    uint32_t tdiff = time - active_window->axis_time[axis];
    /* fprintf(stderr, "%d\n", active_window->axis_time[axis]); */
    /* fprintf(stderr, "scroll velocity: %f\n", active_window->velocity[axis]); */
    /* fprintf(stderr, "scroll velocity: %f\n", active_window->velocity[1]); */

    active_window->inertia[axis] = active_window->velocity[axis];
    /* active_window->inertia[1] = active_window->velocity[1]; */
    active_window->velocity[axis] = 0;
    /* active_window->velocity[1] = 0; */

    wl_surface_commit(active_window->surface);
    /* fprintf(stderr, "axis stop\n"); */
    /* active_window->axis_time[0] = active_window->axis_time[1] = 0; */
    if (time - active_window->axis_time[axis] > 30) {
        active_window->inertia[axis] = 0;
    }

    active_window->axis_time[axis] = 0;
}
static void pointer_handle_axis_discrete(void *data, struct wl_pointer *wl_pointer,
                                         uint32_t axis, int32_t discrete) {
    /* fprintf(stderr, "axis discrete\n"); */
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_handle_enter,
    .leave = pointer_handle_leave,
    .motion = pointer_handle_motion,
    .button = pointer_handle_button,
    .axis = pointer_handle_axis,
    .frame = pointer_handle_frame,
    .axis_source = pointer_handle_axis_source,
    .axis_stop = pointer_handle_axis_stop,
    .axis_discrete = pointer_handle_axis_discrete,
};

static void touch_handle_down(void *data, struct wl_touch *wl_touch, uint32_t serial,
                              uint32_t time, struct wl_surface *surface, int32_t id,
                              wl_fixed_t x_w, wl_fixed_t y_w) {
    /* fprintf(stderr, "touch down\n"); */
    /* struct display *d = (struct display *)data; */

    /* if (!d->shell) */
    /*     return; */

    /* zxdg_toplevel_v6_move(d->window->xdg_toplevel, d->seat, serial); */
}

static void touch_handle_up(void *data, struct wl_touch *wl_touch, uint32_t serial,
                            uint32_t time, int32_t id) {}

static void touch_handle_motion(void *data, struct wl_touch *wl_touch, uint32_t time,
                                int32_t id, wl_fixed_t x_w, wl_fixed_t y_w) {}

static void touch_handle_frame(void *data, struct wl_touch *wl_touch) {}

static void touch_handle_cancel(void *data, struct wl_touch *wl_touch) {}

static const struct wl_touch_listener touch_listener = {
    touch_handle_down,  touch_handle_up,     touch_handle_motion,
    touch_handle_frame, touch_handle_cancel,
};

static void keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard, uint32_t format,
                            int32_t fd, uint32_t size) {

    /* fprintf(stderr, "handling keymap\n"); */
    struct xkb_context *xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        exit(1);
    }
    char *map_shm = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (map_shm == MAP_FAILED) {
        close(fd);
        exit(1);
    }
    struct xkb_keymap *xkb_keymap =
        xkb_keymap_new_from_string(xkb_context, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
    munmap(map_shm, size);
    close(fd);

    g_xkb_state = xkb_state_new(xkb_keymap);
}

static void keyboard_enter(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
                           struct wl_surface *surface, struct wl_array *keys) {

    active_window = wl_surface_get_user_data(surface);
    // Who cares
}

static void keyboard_leave(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
                           struct wl_surface *surface) {
    // Who cares
}
static void keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
                                 int32_t rate, int32_t delay) {}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                               uint32_t mods_depressed, uint32_t mods_latched,
                               uint32_t mods_locked, uint32_t group) {}

void close_window(struct window *w);
struct window *create_window();

static void keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
                         uint32_t time, uint32_t key, uint32_t _key_state) {
    /* struct window *w = data; */
    enum wl_keyboard_key_state key_state = _key_state;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(g_xkb_state, key + 8);
    /* fprintf(stderr, "handling keyinput\n"); */

    if (key_state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return;
    if (sym == XKB_KEY_c) {
        close_window(active_window);
    } else if (sym == XKB_KEY_n) {
        create_window();
    }
    /* draw(active_window); */
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
                                     enum wl_seat_capability caps) {
    struct window *w = data;
    if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
        g_kbd = wl_seat_get_keyboard(g_seat);
        wl_keyboard_add_listener(g_kbd, &keyboard_listener, NULL);
    }
    if (caps & WL_SEAT_CAPABILITY_POINTER) {
        g_pointer = wl_seat_get_pointer(g_seat);
        wl_pointer_add_listener(g_pointer, &pointer_listener, NULL);
    }
}
static void seat_handle_name(void *data, struct wl_seat *wl_seat, const char *name) {
    // Who cares
}

const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};
static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
    /* fprintf(stderr, "released buffer\n"); */
}

static const struct wl_buffer_listener buffer_listener = {.release = buffer_release};

static void resize_surface();
static void handle_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                                      int32_t width, int32_t height,
                                      struct wl_array *states) {

    struct window *w = data;
    w->resized = (width != w->width || height != w->height);

    if (width && height) {
        w->width = width;
        w->height = height;
    }
    /* fprintf(stderr, "width: %i, height: %i\n", w->width, w->height); */
    if (w->configured) {
        resize_surface(w);
        wl_surface_commit(w->surface);
    }
    w->configured = true;
}

static void handle_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    /* fprintf(stderr, "closing window\n"); */
    struct window *w = data;
    close_window(w);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = handle_toplevel_configure,
    .close = handle_toplevel_close,
};

static void handle_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_base_listener = {.ping = handle_ping};

const struct wl_callback_listener frame_listener;
static void frame_handle_done(void *data, struct wl_callback *callback, uint32_t time) {
    static uint32_t t = 0;
    /* fprintf(stderr, "drawing frame: %d ms\n", time - t); */
    t = time;

    /* fprintf(stderr, "frame!!\n"); */
    struct window *w = data;
    wl_callback_destroy(callback);
    draw(w);
    callback = wl_surface_frame(w->surface);
    wl_callback_add_listener(callback, &frame_listener, w);

    if (fabs(w->inertia[0]) > 1 || fabs(w->inertia[1]) > 1) {

        for (uint32_t axis = 0; axis < 2; ++axis) {
            w->position[axis] += w->inertia[axis] / 3.0;

            while (w->position[axis] < 0)
                w->position[axis] += 800;
            while (w->position[axis] > 800)
                w->position[axis] -= 800;

            w->inertia[axis] *= 0.92;
        }
        wl_surface_commit(w->surface);
    }
}

const struct wl_callback_listener frame_listener = {
    .done = frame_handle_done,
};

void draw(struct window *w) {
    if (!w->open)
        return;
    /* fprintf(stderr, "drawing...\n"); */
    cairo_t *cairo = w->cairo;
    cairo_move_to(cairo, 0, 0);
    cairo_set_source_u32(cairo, 0x000000ff);
    cairo_rectangle(cairo, 0, 0, w->width * scale, w->height * scale);
    cairo_fill(cairo);

    cairo_set_source_u32(cairo, 0xffffffff);

    /* fprintf(stderr, "x: %f y: %f\n", w->position[1], w->position[0]); */

    for (int x = -1600; x < w->width * scale; x += 800) {
        int row = 0;
        for (int y = -1600; y < w->height * scale; y += 400) {
            row++;

            cairo_move_to(cairo, x + w->position[1] + ((row % 2) * 400),
                          y + w->position[0]);
            pango_printf(cairo, font, scale, false, "Emacs");
            /* cairo_rectangle(cairo, x + w->position[1] + ((row % 2) * 400), */
            /*                 y + w->position[0], 400, 400); */
            /* cairo_fill(cairo); */
        }
    }

    /* cairo_rectangle(cairo, 200, 200, w->width * 2 - 400, w->height * 2 - 400); */

    /* cairo_set_source_u32(cairo, 0x000000ff); */
    /* cairo_move_to(cairo, 200, 200); */
    /* cairo_line_to(cairo, w->width * scale - 200, 200); */
    /* cairo_line_to(cairo, 200, w->height * scale - 200); */
    /* cairo_close_path(cairo); */
    /* cairo_fill(cairo); */

    /* /\* cairo_set_source_u32(cairo, 0xffffffff); *\/ */
    /* /\* cairo_move_to(cairo, 250, 250); *\/ */
    /* /\* pango_printf(cairo, font, scale, false, "emacs %d", ++counter); *\/ */

    /* /\* if (w == active_window) { *\/ */
    /* /\*     cairo_move_to(cairo, 250, 350); *\/ */
    /* /\*     pango_printf(cairo, font, scale, false, "active"); *\/ */
    /* /\* } *\/ */

    /* cairo_set_source_u32(cairo, 0x888888ff); */
    /* cairo_move_to(cairo, 1000, 1000); */
    /* cairo_line_to(cairo, 1000 + scroll[1] / 10, 1000 + scroll[0] / 10); */
    /* cairo_stroke(cairo); */

    wl_surface_damage(w->surface, 0, 0, w->width, w->height);
    wl_surface_attach(w->surface, w->buffer, 0, 0);
    wl_surface_commit(w->surface);
    /* memset(w->shm_data, 0xff, w->width * 4 * w->height * scale * scale); */

    /* if (cb) { */
    /* } */
}

static void resize_surface(struct window *window) {
    if (!window->open)
        return;
    if (!window->resized)
        return;
    /* fprintf(stderr, "resizing surface\n"); */
    int stride = window->width * 4;
    int size = stride * window->height;

    int fd = create_shm_file(size * scale * scale);
    window->shm_data =
        mmap(NULL, size * scale * scale, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    /* memset(window->shm_data, 0xff, size * scale * scale); */
    struct wl_shm_pool *pool = wl_shm_create_pool(g_shm, fd, size * scale * scale);
    window->buffer =
        wl_shm_pool_create_buffer(pool, 0, window->width * scale, window->height * scale,
                                  stride * scale, WL_SHM_FORMAT_ARGB8888);

    wl_buffer_add_listener(window->buffer, &buffer_listener, NULL);

    cairo_surface_t *s = cairo_image_surface_create_for_data(
        window->shm_data, CAIRO_FORMAT_ARGB32, window->width * scale,
        window->height * scale, window->width * 4 * scale);

    window->cairo = cairo_create(s);

    wl_surface_attach(window->surface, window->buffer, 0, 0);
}

static void handle_xdg_buffer_configure(void *data, struct xdg_surface *xdg_surface,
                                        uint32_t serial) {
    struct window *w = data;
    /* fprintf(stderr, "configured xdg surface\n"); */
    xdg_surface_ack_configure(w->xdg_surface, serial);
}

static struct xdg_surface_listener xdg_surface_listener = {
    .configure = handle_xdg_buffer_configure};

static void handle_global(void *data, struct wl_registry *registry, uint32_t name,
                          const char *interface, uint32_t version) {

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        g_compositor =
            wl_registry_bind(registry, name, &wl_compositor_interface, version);

    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        g_seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
        wl_seat_add_listener(g_seat, &seat_listener, NULL);

    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        g_shm = wl_registry_bind(registry, name, &wl_shm_interface, version);

    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        g_xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, version);
    }
}

static void handle_global_remove(void *data, struct wl_registry *registry,
                                 uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

struct window *create_window() {
    struct window **wp = windows - 1;
    while (*++wp);
    struct window *w = *wp = malloc(sizeof(struct window));

    w->configured = false;
    w->width = 200;
    w->height = 200;
    w->resized = true;
    w->open = true;
    w->position[0] = 0;
    w->position[1] = 0;
    w->__position_pending[0] = 0;
    w->__position_pending[1] = 0;
    w->inertia[0] = w->inertia[1] = 0.0;
    w->axis_time[0] = w->axis_time[1] = 0;

    w->surface = wl_compositor_create_surface(g_compositor);
    wl_surface_set_user_data(w->surface, w);
    wl_surface_set_buffer_scale(w->surface, scale);

    w->xdg_surface = xdg_wm_base_get_xdg_surface(g_xdg_wm_base, w->surface);
    w->xdg_toplevel = xdg_surface_get_toplevel(w->xdg_surface);

    xdg_surface_add_listener(w->xdg_surface, &xdg_surface_listener, w);
    xdg_toplevel_add_listener(w->xdg_toplevel, &xdg_toplevel_listener, w);

    xdg_toplevel_set_title(w->xdg_toplevel, "lol");
    wl_surface_commit(w->surface);
    wl_display_roundtrip(g_display);
    resize_surface(w);
    struct wl_callback *callback = wl_surface_frame(w->surface);
    wl_callback_add_listener(callback, &frame_listener, w);

    wl_surface_commit(w->surface);

    open_windows++;
    return w;
}

void close_window(struct window *w) {
    w->open = false;

    xdg_toplevel_destroy(w->xdg_toplevel);
    xdg_surface_destroy(w->xdg_surface);
    wl_surface_destroy(w->surface);
    wl_buffer_destroy(w->buffer);

    struct window **wp = windows - 1;
    while (*++wp != w);
    free(w);
    *wp = NULL;

    open_windows--;
}

int main(int argc, char *argv[]) {
    memset(windows, 0, MAX_WINDOWS * sizeof(struct window *));

    g_display = wl_display_connect(NULL);
    struct wl_registry *registry = wl_display_get_registry(g_display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    wl_display_roundtrip(g_display);

    struct window *w = create_window();

    running = true;
    while (wl_display_dispatch(g_display) != -1 && open_windows) {
    }

    wl_registry_destroy(registry);
    wl_display_disconnect(g_display);

    return 0;
}
