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
#define _POSIX_C_SOURCE 200809L

static int counter = 0;


struct wl_display *g_display;
struct wl_compositor *g_compositor;
struct wl_seat *g_seat;
struct xkb_keymap *g_xkb_keymap;
struct wl_keyboard *g_kbd;
struct xdg_wm_base *g_xdg_wm_base;
struct wl_shm *g_shm;

struct window {

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    struct wl_buffer *buffer;
    void *shm_data;

    cairo_t *cairo;

    int width;
    int height;

    bool resized;
    bool configured;
};

bool configured = false;
static char *font = "Mono";

struct wl_seat *seat;
struct wl_keyboard *kbd;
struct xkb_keymap *xkb_keymap;
struct xkb_context *xkb_context;
struct xkb_state *xkb_state;
struct xdg_wm_base *xdg_wm_base;
int scale = 2;


bool running;
void draw(struct window *);

static const char overflow[] = "[buffer overflow]";
static const int max_chars = 16384;
PangoLayout *get_pango_layout(cairo_t *cairo, const char *font,
		const char *text, double scale, bool markup) {
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

void pango_printf(cairo_t *cairo, const char *font,
		double scale, bool markup, const char *fmt, ...) {
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

static void keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard, uint32_t format,
                            int32_t fd, uint32_t size) {

    fprintf(stderr, "handling keymap\n");
    xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        exit(1);
    }
    char *map_shm = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (map_shm == MAP_FAILED) {
        close(fd);
        exit(1);
    }
    xkb_keymap =
        xkb_keymap_new_from_string(xkb_context, map_shm, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
    munmap(map_shm, size);
    close(fd);

    xkb_state = xkb_state_new(xkb_keymap);
}
struct window *active_window;

static void keyboard_enter(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
                           struct wl_surface *surface, struct wl_array *keys) {
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

static void keyboard_key(void *data, struct wl_keyboard *wl_keyboard, uint32_t serial,
                         uint32_t time, uint32_t key, uint32_t _key_state) {
    /* struct window *w = data; */
    enum wl_keyboard_key_state key_state = _key_state;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state, key + 8);
    fprintf(stderr, "handling keyinput\n");
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
        kbd = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(kbd, &keyboard_listener, NULL);
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
    fprintf(stderr, "released buffer\n");
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
    fprintf(stderr, "width: %i, height: %i\n", w->width, w->height);
    if (w->configured) {
        resize_surface(w);
        /* wl_display_roundtrip(g_display); */
        wl_surface_commit(w->surface);
    }
    w->configured = true;
}

static void handle_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    running = 0;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = handle_toplevel_configure,
    .close = handle_toplevel_close,
};

static void handle_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_base_listener = {.ping = handle_ping};


static void frame_handle_done(void *data, struct wl_callback *callback,
		uint32_t time) {
    struct window *w = data;
	wl_callback_destroy(callback);
	draw(w);
}

const struct wl_callback_listener frame_listener = {
	.done = frame_handle_done,
};

void draw(struct window * w) {
    fprintf(stderr, "drawing...\n");
    cairo_t *cairo = w->cairo;
    cairo_move_to(cairo, 0, 0);
    cairo_set_source_u32(cairo, 0x0);
    cairo_rectangle(cairo, 0, 0, w->width * scale, w->height * scale);
    cairo_fill(cairo);
    cairo_set_source_u32(cairo, 0xffffffff);
    cairo_rectangle(cairo, 200, 200, w->width * 2 - 400, w->height * 2 - 400);

    cairo_fill(cairo);

    cairo_set_source_u32(cairo, 0x000000ff);
    cairo_move_to(cairo, 200, 200);
    cairo_line_to(cairo, w->width * scale - 200, 200);
    cairo_line_to(cairo, 200, w->height * scale - 200);
    cairo_close_path(cairo);
    cairo_fill(cairo);

    cairo_set_source_u32(cairo, 0xffffffff);
    cairo_move_to(cairo, 250, 250);
    pango_printf(cairo, font, scale, false, "emacs %d", ++counter);

    wl_surface_damage(w->surface, 0, 0, w->width, w->height);
    wl_surface_attach(w->surface, w->buffer, 0, 0);
    wl_surface_commit(w->surface);
    /* memset(w->shm_data, 0xff, w->width * 4 * w->height * scale * scale); */

    /* if (cb) { */
        struct wl_callback *callback = wl_surface_frame(w->surface);
        wl_callback_add_listener(callback, &frame_listener, w);
    /* } */
}

static void resize_surface(struct window *window) {
    if (!window->resized) return;
    fprintf(stderr, "resizing surface\n");
    int stride = window->width * 4;
    int size = stride * window->height;

    int fd = create_shm_file(size * scale * scale);
    window->shm_data = mmap(NULL, size * scale * scale, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    /* memset(window->shm_data, 0xff, size * scale * scale); */
    struct wl_shm_pool *pool = wl_shm_create_pool(g_shm, fd, size * scale * scale);
    window->buffer = wl_shm_pool_create_buffer(pool, 0, window->width * scale,
                                       window->height * scale, stride * scale,
                                       WL_SHM_FORMAT_ARGB8888);

    wl_buffer_add_listener(window->buffer, &buffer_listener, NULL);

    cairo_surface_t *s = cairo_image_surface_create_for_data(
        window->shm_data, CAIRO_FORMAT_ARGB32,
        window->width *scale, window->height * scale,
        window->width * 4 * scale);

    window->cairo = cairo_create(s);

    wl_surface_attach(window->surface, window->buffer, 0, 0);
}


static void handle_xdg_buffer_configure(void *data, struct xdg_surface *xdg_surface,
                                        uint32_t serial) {
    struct window *w = data;
    fprintf(stderr, "configured xdg surface\n");
    xdg_surface_ack_configure(w->xdg_surface, serial);
}

static struct xdg_surface_listener xdg_surface_listener = {
    .configure = handle_xdg_buffer_configure};

static void handle_global(void *data, struct wl_registry *registry, uint32_t name,
                          const char *interface, uint32_t version) {

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        g_compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version);

    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
        wl_seat_add_listener(seat, &seat_listener, NULL);

    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        g_shm = wl_registry_bind(registry, name, &wl_shm_interface, version);

    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, version);
    }
}

static void handle_global_remove(void *data, struct wl_registry *registry,
                                 uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

struct window *create_window() {
    struct window *w = malloc(sizeof(struct window));
    w->configured = false;
    w->width = 200;
    w->height = 200;
    w->resized = true;

    w->surface = wl_compositor_create_surface(g_compositor);
	wl_surface_set_buffer_scale(w->surface, scale);

    w->xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, w->surface);
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

    return w;
}

void close_window(struct window* w) {

    wl_surface_destroy(w->surface);
}


int main(int argc, char *argv[]) {
    g_display = wl_display_connect(NULL);
    struct wl_registry *registry = wl_display_get_registry(g_display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    wl_display_roundtrip(g_display);

    struct window *w = create_window();
    active_window = w;
    struct window *w2 = create_window();

    running = true;
    /* wl_display_dispatch(g_display); */
    while (wl_display_dispatch(g_display) != -1 && running) {
    /* draw(w); */
        /* /\* if (w2->resized) { *\/ */
            /* draw(w); */
        /* } */
    }

    wl_display_disconnect(g_display);

    return 0;
}
