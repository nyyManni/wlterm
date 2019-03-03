#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <xkbcommon/xkbcommon.h>

#include <wayland-client-protocol.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"
#define _POSIX_C_SOURCE 200809L



bool configured = false;

struct wl_display *display;
struct wl_seat *seat;
struct wl_compositor *compositor;
struct wl_shm *shm;
struct wl_keyboard *kbd;
struct xkb_keymap *xkb_keymap;
struct xkb_context *xkb_context;
struct xkb_state *xkb_state;
struct xdg_surface *xdg_surface;
struct xdg_wm_base *xdg_wm_base;
struct xdg_toplevel *xdg_toplevel;
struct wl_shm_pool *pool;
struct wl_buffer *buffer;

cairo_t *cairo;
struct wl_buffer *buffer;
struct wl_surface *surface;
void *shm_data = NULL;
size_t shm_data_len;

int window_width = 200;
int window_height = 200;
bool resized = true;
bool running;

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
    shm_data_len = size;

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
    enum wl_keyboard_key_state key_state = _key_state;
    xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state, key + 8);
    fprintf(stderr, "handling keyinput\n");
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
    struct dmenu_panel *panel = data;
    if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
        kbd = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(kbd, &keyboard_listener, panel);
    }
}
static void seat_handle_name(void *data, struct wl_seat *wl_seat, const char *name) {
    // Who cares
}

const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
    .name = seat_handle_name,
};
static void buffer_release(void *data, struct wl_buffer *wl_buffer) {}

static const struct wl_buffer_listener buffer_listener = {.release = buffer_release};

static void handle_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
                                      int32_t width, int32_t height,
                                      struct wl_array *states) {
    resized = (width != window_width || height != window_height);

    if (width != 0 && height) {
        window_width = width;
        window_height = height;
    }
    fprintf(stderr, "width: %i, height: %i\n", window_width, window_height);
    if (configured) {
        fprintf(stderr, "resizing...\n");
        int stride = window_width * 4;
        int size = stride * window_height;
        int fd = create_shm_file(size);
        shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        pool = wl_shm_create_pool(shm, fd, size);
        buffer = wl_shm_pool_create_buffer(pool, 0, window_width, window_height, stride,
                                           WL_SHM_FORMAT_ARGB8888);
        wl_buffer_add_listener(buffer, &buffer_listener, NULL);
        cairo_surface_t *s = cairo_image_surface_create_for_data(
            shm_data, CAIRO_FORMAT_ARGB32, window_width, window_height, window_width * 4);

        cairo = cairo_create(s);
        wl_surface_attach(surface, buffer, 0, 0);

        wl_display_roundtrip(display);
        wl_surface_commit(surface);
    }
    configured = true;
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


void draw() {
    cairo_set_source_u32(cairo, 0x0);
    cairo_rectangle(cairo, 0, 0, window_width, window_height);
    cairo_fill(cairo);
    cairo_set_source_u32(cairo, 0xffffffff);
    cairo_rectangle(cairo, 100, 100, window_width - 200, window_height - 200);

    cairo_fill(cairo);

    cairo_set_source_u32(cairo, 0x000000ff);
    cairo_move_to(cairo, 100, 100);
    cairo_line_to(cairo, window_width - 100, 100);
    cairo_line_to(cairo, 100, window_height - 100);
    cairo_close_path(cairo);
    cairo_fill(cairo);
}

static void resize_surface() {
    fprintf(stderr, "resizing surface\n");
    int stride = window_width * 4;
    int size = stride * window_height;

    int fd = create_shm_file(size);
    shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    pool = wl_shm_create_pool(shm, fd, size);
    buffer = wl_shm_pool_create_buffer(pool, 0, window_width, window_height, stride,
                                       WL_SHM_FORMAT_ARGB8888);

    wl_buffer_add_listener(buffer, &buffer_listener, NULL);

    cairo_surface_t *s = cairo_image_surface_create_for_data(
        shm_data, CAIRO_FORMAT_ARGB32, window_width, window_height, window_width * 4);

    cairo = cairo_create(s);

    wl_surface_attach(surface, buffer, 0, 0);
}

static void handle_xdg_buffer_configure(void *data, struct xdg_surface *xdg_surface,
                                        uint32_t serial) {
    fprintf(stderr, "configured xdg surface\n");

    xdg_surface_ack_configure(xdg_surface, serial);
}

static struct xdg_surface_listener xdg_surface_listener = {
    .configure = handle_xdg_buffer_configure};

static void handle_global(void *data, struct wl_registry *registry, uint32_t name,
                          const char *interface, uint32_t version) {

    fprintf(stderr, "Event %s\n", interface);
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version);

    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
        wl_seat_add_listener(seat, &seat_listener, NULL);

    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, version);

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

int main(int argc, char *argv[]) {
    fprintf(stderr, "clocks per second: %lu\n", CLOCKS_PER_SEC);

    display = wl_display_connect(NULL);
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    wl_display_roundtrip(display);

    surface = wl_compositor_create_surface(compositor);

    xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, surface);
    xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);

    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
    xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);

    xdg_toplevel_set_title(xdg_toplevel, "lol");

    wl_surface_commit(surface);

    wl_display_roundtrip(display);
    resize_surface();

    wl_surface_commit(surface);

    running = true;
    while (wl_display_dispatch(display) != -1 && running) {
        if (resized) {
            draw();
        }
    }

    wl_display_disconnect(display);

    return 0;
}
