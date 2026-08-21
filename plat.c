#define _POSIX_C_SOURCE 200809L

#include "mac_internal.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>

#include <alsa/asoundlib.h>
#include <pulse/error.h>
#include <pulse/simple.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>
#include <xcb/dbe.h>

extern xcb_connection_t *XGetXCBConnection(Display *dpy);
extern void XSetEventQueueOwner(Display *dpy, int owner);
#ifndef XCBOwnsEventQueue
#define XCBOwnsEventQueue 1
#endif

enum { SCALE = 2, TICK_US = 16667, BEZEL = 48 }; /* bezel: host padding around the CRT */
enum { RGB_BEZEL = 0xffa8a098u, RGB_BEZEL_INK = 0xff1a1510u };

static Display *dpy;
static xcb_connection_t *conn;
static xcb_window_t win;
static xcb_gcontext_t gc;
static xcb_pixmap_t pixmap;
static xcb_dbe_back_buffer_t back;
static int have_dbe;
static int img_depth;
static xcb_intern_atom_reply_t *wm_delete;
static xcb_generic_event_t *pending;
static int running = 1;
static uint32_t *stage;
static int stage_w, stage_h;
static uint32_t put_max;
static pa_simple *pulse;
static snd_pcm_t *pcm;
static pthread_t audio_th;
static volatile int audio_run;
static Cursor blank_cur;
static int os_owns;
static int last_hx = BEZEL + 8, last_hy = BEZEL + 8;

enum { MIX_FRAMES = 512 };

static void *audio_thread(void *arg)
{
    short mono[MIX_FRAMES];
    short stereo[MIX_FRAMES * 2];

    (void)arg;
    while (audio_run) {
        int i, err;

        mac_mix(mono, MIX_FRAMES);
        if (pulse) {
            for (i = 0; i < MIX_FRAMES; i++) {
                stereo[i * 2] = mono[i];
                stereo[i * 2 + 1] = mono[i];
            }
            err = pa_simple_write(pulse, stereo, sizeof(stereo), NULL);
            if (err < 0)
                break;
        } else if (pcm) {
            snd_pcm_sframes_t n = snd_pcm_writei(pcm, mono, MIX_FRAMES);
            if (n < 0)
                snd_pcm_recover(pcm, (int)n, 1);
        }
    }
    return NULL;
}

static int pulse_start(void)
{
    pa_sample_spec spec;
    int err = 0;

    spec.format = PA_SAMPLE_S16LE;
    spec.rate = (uint32_t)mac_snd_rate();
    spec.channels = 2;
    pulse = pa_simple_new(NULL, "xcb-mac", PA_STREAM_PLAYBACK, NULL, "beep", &spec, NULL, NULL, &err);
    if (!pulse)
        fprintf(stderr, "xcb-mac: Pulse: %s\n", pa_strerror(err));
    return pulse != NULL;
}

static int alsa_start(void)
{
    int err;

    err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0)
        return 0;
    err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1,
                             mac_snd_rate(), 1, 40000);
    if (err < 0) {
        snd_pcm_close(pcm);
        pcm = NULL;
        return 0;
    }
    return 1;
}

static void audio_start(void)
{
    pulse = NULL;
    pcm = NULL;
    audio_run = 0;
    if (!pulse_start() && !alsa_start()) {
        fprintf(stderr, "xcb-mac: no audio (Pulse/ALSA)\n");
        return;
    }
    audio_run = 1;
    if (pthread_create(&audio_th, NULL, audio_thread, NULL) != 0) {
        audio_run = 0;
        if (pulse) {
            pa_simple_free(pulse);
            pulse = NULL;
        }
        if (pcm) {
            snd_pcm_close(pcm);
            pcm = NULL;
        }
    }
}

static void audio_stop(void)
{
    if (audio_run) {
        audio_run = 0;
        pthread_join(audio_th, NULL);
    }
    if (pulse) {
        pa_simple_drain(pulse, NULL);
        pa_simple_free(pulse);
        pulse = NULL;
    }
    if (pcm) {
        snd_pcm_close(pcm);
        pcm = NULL;
    }
}

static int map_key(KeySym ks)
{
    if (ks == XK_Left || ks == XK_a || ks == XK_A)
        return KEY_LEFT;
    if (ks == XK_Right || ks == XK_d || ks == XK_D)
        return KEY_RIGHT;
    if (ks == XK_Up || ks == XK_w || ks == XK_W)
        return KEY_UP;
    if (ks == XK_Down || ks == XK_s || ks == XK_S)
        return KEY_DOWN;
    if (ks == XK_Escape)
        return KEY_ESC;
    if (ks == XK_q || ks == XK_Q)
        return KEY_Q;
    if (ks == XK_space)
        return KEY_SPACE;
    if (ks == XK_Return || ks == XK_KP_Enter)
        return KEY_ENTER;
    return 0;
}

static xcb_generic_event_t *next_event(void)
{
    xcb_generic_event_t *ev;

    if (pending) {
        ev = pending;
        pending = NULL;
        return ev;
    }
    return xcb_poll_for_event(conn);
}

static int is_auto_repeat(const xcb_key_release_event_t *rel)
{
    xcb_generic_event_t *ev = next_event();
    xcb_key_press_event_t *pr;

    if (!ev)
        return 0;
    if ((ev->response_type & ~0x80) == XCB_KEY_PRESS) {
        pr = (xcb_key_press_event_t *)ev;
        if (pr->detail == rel->detail && pr->time == rel->time) {
            free(ev);
            return 1;
        }
    }
    pending = ev;
    return 0;
}

static void fill_bezel(void)
{
    int x, y, sw = MAC_W * SCALE, sh = MAC_H * SCALE;
    int x0 = BEZEL - 1, y0 = BEZEL - 1, x1 = BEZEL + sw, y1 = BEZEL + sh;

    for (y = 0; y < stage_h; y++)
        for (x = 0; x < stage_w; x++)
            stage[(size_t)y * stage_w + x] = RGB_BEZEL;
    if (x0 < 0)
        return;
    for (x = x0; x <= x1 && x < stage_w; x++) {
        if (y0 >= 0 && y0 < stage_h)
            stage[(size_t)y0 * stage_w + x] = RGB_BEZEL_INK;
        if (y1 >= 0 && y1 < stage_h)
            stage[(size_t)y1 * stage_w + x] = RGB_BEZEL_INK;
    }
    for (y = y0; y <= y1 && y < stage_h; y++) {
        if (x0 >= 0 && x0 < stage_w)
            stage[(size_t)y * stage_w + x0] = RGB_BEZEL_INK;
        if (x1 >= 0 && x1 < stage_w)
            stage[(size_t)y * stage_w + x1] = RGB_BEZEL_INK;
    }
}

static int host_cur = -1; /* -1 unknown, 0 arrow, 1 blank */

static int in_crt(int hx, int hy)
{
    return hx >= BEZEL && hy >= BEZEL && hx < BEZEL + MAC_W * SCALE && hy < BEZEL + MAC_H * SCALE;
}

static void apply_host_cursor(void)
{
    int want;

    if (!dpy || !win)
        return;
    want = (os_owns || !in_crt(last_hx, last_hy)) ? 0 : 1;
    if (want == host_cur)
        return;
    host_cur = want;
    XDefineCursor(dpy, (Window)win, want ? blank_cur : None);
    XFlush(dpy);
}

static void host_mouse(int hx, int hy)
{
    last_hx = hx;
    last_hy = hy;
    /* Clamp Mac coords only — do not warp or confine the OS pointer. */
    mac_mouse_set((hx - BEZEL) / SCALE, (hy - BEZEL) / SCALE);
    mac_host_pointer(in_crt(hx, hy), os_owns);
    apply_host_cursor();
}

static void upload_rows(int y0, int h)
{
    int row_bytes = stage_w * 4;
    int max_rows, chunk;

    if (h < 1)
        return;
    max_rows = (int)(put_max / (uint32_t)row_bytes);
    if (max_rows < 1)
        max_rows = 1;
    while (h > 0) {
        chunk = h < max_rows ? h : max_rows;
        xcb_put_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, pixmap, gc, (uint16_t)stage_w,
                      (uint16_t)chunk, 0, (int16_t)y0, 0, (uint8_t)img_depth,
                      (uint32_t)chunk * (uint32_t)row_bytes,
                      (const uint8_t *)(stage + (size_t)y0 * stage_w));
        y0 += chunk;
        h -= chunk;
    }
}

static void render_crt(void)
{
    const uint8_t *src = mac_present();
    int x, y, sy, vw = MAC_W, vh = MAC_H;
    int crt_bytes = MAC_W * SCALE * 4;

    for (y = 0; y < vh; y++) {
        uint32_t *d0 = stage + (size_t)(y * SCALE + BEZEL) * stage_w + BEZEL;

        for (x = 0; x < vw; x++) {
            uint32_t p = mac_host_pixel(src, x, y);
            uint32_t *d = d0 + x * SCALE;
            for (sy = 0; sy < SCALE; sy++)
                d[sy] = p;
        }
        for (sy = 1; sy < SCALE; sy++)
            memcpy(d0 + (size_t)sy * stage_w, d0, (size_t)crt_bytes);
    }
}

static void present(int full)
{
    xcb_drawable_t dst = have_dbe ? (xcb_drawable_t)back : (xcb_drawable_t)win;
    int sh = MAC_H * SCALE;

    if (full)
        xcb_copy_area(conn, pixmap, dst, gc, 0, 0, 0, 0, (uint16_t)stage_w, (uint16_t)stage_h);
    else
        xcb_copy_area(conn, pixmap, dst, gc, BEZEL, BEZEL, BEZEL, BEZEL, (uint16_t)(MAC_W * SCALE),
                      (uint16_t)sh);
    if (have_dbe) {
        xcb_dbe_swap_info_t info;

        memset(&info, 0, sizeof(info));
        info.window = win;
        info.swap_action = XCB_DBE_SWAP_ACTION_COPIED;
        xcb_dbe_swap_buffers(conn, 1, &info);
    }
    xcb_flush(conn);
}

static void blit_visible(void)
{
    render_crt();
    upload_rows(BEZEL, MAC_H * SCALE);
    present(0);
}

static void blit_full(void)
{
    render_crt();
    upload_rows(0, stage_h);
    present(1);
}

void mac_swap(void)
{
    blit_visible();
}

static void on_event(xcb_generic_event_t *ev)
{
    switch (ev->response_type & ~0x80) {
    case XCB_EXPOSE: {
        xcb_expose_event_t *ex = (xcb_expose_event_t *)ev;

        if (ex->count != 0)
            break;
        present(1);
        break;
    }
    case XCB_MOTION_NOTIFY: {
        xcb_motion_notify_event_t *m = (xcb_motion_notify_event_t *)ev;
        host_mouse(m->event_x, m->event_y);
        break;
    }
    case XCB_ENTER_NOTIFY: {
        xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t *)ev;
        host_mouse(e->event_x, e->event_y);
        mac_pointer_enter();
        break;
    }
    case XCB_LEAVE_NOTIFY: {
        xcb_leave_notify_event_t *e = (xcb_leave_notify_event_t *)ev;
        if (e->detail == XCB_NOTIFY_DETAIL_INFERIOR)
            break;
        host_mouse(e->event_x, e->event_y);
        mac_pointer_leave();
        break;
    }
    case XCB_BUTTON_PRESS: {
        xcb_button_press_event_t *b = (xcb_button_press_event_t *)ev;
        mac_pointer_enter();
        host_mouse(b->event_x, b->event_y);
        if (b->detail == 1)
            mac_mouse_button(1);
        break;
    }
    case XCB_BUTTON_RELEASE: {
        xcb_button_release_event_t *b = (xcb_button_release_event_t *)ev;
        host_mouse(b->event_x, b->event_y);
        if (b->detail == 1)
            mac_mouse_button(0);
        break;
    }
    case XCB_KEY_PRESS: {
        xcb_key_press_event_t *kp = (xcb_key_press_event_t *)ev;
        KeySym ks = XkbKeycodeToKeysym(dpy, kp->detail, 0, 0);
        int k;

        if (ks == XK_F12) {
            os_owns = !os_owns;
            mac_host_pointer(in_crt(last_hx, last_hy), os_owns);
            apply_host_cursor();
            break;
        }
        k = map_key(ks);
        mac_key_set(k, 1);
        if (k == KEY_ESC || k == KEY_Q)
            running = 0;
        break;
    }
    case XCB_KEY_RELEASE: {
        xcb_key_release_event_t *kr = (xcb_key_release_event_t *)ev;
        if (is_auto_repeat(kr))
            break;
        mac_key_set(map_key(XkbKeycodeToKeysym(dpy, kr->detail, 0, 0)), 0);
        break;
    }
    case XCB_CLIENT_MESSAGE: {
        xcb_client_message_event_t *cm = (xcb_client_message_event_t *)ev;
        if (wm_delete && cm->data.data32[0] == wm_delete->atom)
            running = 0;
        break;
    }
    default:
        break;
    }
}

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

int mac_main(void (*tick)(float dt))
{
    xcb_screen_t *screen;
    xcb_intern_atom_reply_t *wm_proto = NULL;
    int fd;
    double prev;
    const char *title = "xcb-mac";

    if (!mac_init()) {
        fprintf(stderr, "cannot allocate framebuffer\n");
        return 1;
    }

    stage_w = MAC_W * SCALE + BEZEL * 2;
    stage_h = MAC_H * SCALE + BEZEL * 2;
    stage = calloc((size_t)stage_w * (size_t)stage_h, sizeof(*stage));
    if (!stage) {
        mac_shutdown();
        return 1;
    }
    fill_bezel();

    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "cannot open X display\n");
        mac_shutdown();
        return 1;
    }
    conn = XGetXCBConnection(dpy);
    if (!conn || xcb_connection_has_error(conn)) {
        fprintf(stderr, "cannot get XCB connection\n");
        return 1;
    }
    XSetEventQueueOwner(dpy, XCBOwnsEventQueue);
    XkbSetDetectableAutoRepeat(dpy, True, NULL);

    screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
    img_depth = screen->root_depth;
    put_max = xcb_get_maximum_request_length(conn) * 4u / 2u;
    if (put_max < 4096)
        put_max = 4096;
    win = xcb_generate_id(conn);
    gc = xcb_generate_id(conn);
    pixmap = xcb_generate_id(conn);

    {
        uint32_t mask = XCB_CW_BACK_PIXMAP | XCB_CW_BIT_GRAVITY | XCB_CW_BACKING_STORE |
                        XCB_CW_EVENT_MASK;
        uint32_t values[] = {
            XCB_BACK_PIXMAP_NONE,
            XCB_GRAVITY_STATIC,
            XCB_BACKING_STORE_WHEN_MAPPED,
            XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                XCB_EVENT_MASK_POINTER_MOTION | XCB_EVENT_MASK_BUTTON_PRESS |
                XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_STRUCTURE_NOTIFY |
                XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW,
        };
        xcb_create_window(conn, XCB_COPY_FROM_PARENT, win, screen->root, 80, 60,
                          (uint16_t)stage_w, (uint16_t)stage_h, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                          screen->root_visual, mask, values);
    }
    {
        XSizeHints sh;

        memset(&sh, 0, sizeof(sh));
        sh.flags = PSize | PMinSize | PMaxSize;
        sh.width = sh.min_width = sh.max_width = stage_w;
        sh.height = sh.min_height = sh.max_height = stage_h;
        XSetWMNormalHints(dpy, (Window)win, &sh);
        XFlush(dpy);
    }
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
                        (uint32_t)strlen(title), title);
    {
        xcb_intern_atom_cookie_t pc = xcb_intern_atom(conn, 1, 12, "WM_PROTOCOLS");
        xcb_intern_atom_cookie_t dc = xcb_intern_atom(conn, 0, 16, "WM_DELETE_WINDOW");
        wm_proto = xcb_intern_atom_reply(conn, pc, NULL);
        wm_delete = xcb_intern_atom_reply(conn, dc, NULL);
        if (wm_proto && wm_delete)
            xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, wm_proto->atom, XCB_ATOM_ATOM, 32, 1,
                                &wm_delete->atom);
    }
    {
        uint32_t gv[] = {screen->white_pixel, 0};

        xcb_create_gc(conn, gc, win, XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES, gv);
    }
    xcb_create_pixmap(conn, screen->root_depth, pixmap, win, (uint16_t)stage_w, (uint16_t)stage_h);
    {
        const xcb_query_extension_reply_t *ext = xcb_get_extension_data(conn, &xcb_dbe_id);

        have_dbe = 0;
        if (ext && ext->present) {
            xcb_dbe_query_version_cookie_t ck =
                xcb_dbe_query_version(conn, XCB_DBE_MAJOR_VERSION, XCB_DBE_MINOR_VERSION);
            xcb_dbe_query_version_reply_t *rep = xcb_dbe_query_version_reply(conn, ck, NULL);

            if (rep) {
                back = xcb_generate_id(conn);
                xcb_dbe_allocate_back_buffer(conn, win, back, XCB_DBE_SWAP_ACTION_COPIED);
                have_dbe = 1;
                free(rep);
            }
        }
        if (!have_dbe) {
            uint32_t bg = pixmap;

            xcb_change_window_attributes(conn, win, XCB_CW_BACK_PIXMAP, &bg);
        }
    }
    xcb_map_window(conn, win);
    xcb_flush(conn);
    {
        XColor c;
        Pixmap pix = XCreatePixmap(dpy, DefaultRootWindow(dpy), 1, 1, 1);

        memset(&c, 0, sizeof(c));
        blank_cur = XCreatePixmapCursor(dpy, pix, pix, &c, &c, 0, 0);
        XFreePixmap(dpy, pix);
        apply_host_cursor();
    }
    blit_full();
    audio_start();

    fd = xcb_get_file_descriptor(conn);
    prev = now_sec();
    while (running) {
        xcb_generic_event_t *ev;
        fd_set fds;
        struct timeval tv;
        double t, dt, left;

        while ((ev = next_event())) {
            on_event(ev);
            free(ev);
        }
        if (!running)
            break;

        t = now_sec();
        dt = t - prev;
        prev = t;
        if (dt < 0.0)
            dt = 0.0;
        if (dt > 0.05)
            dt = 0.05;
        tick((float)dt);
        if (mac_want_quit())
            running = 0;
        mac_keys_end_frame();

        left = TICK_US / 1000000.0 - (now_sec() - t);
        if (left < 0.0)
            left = 0.0;
        tv.tv_sec = 0;
        tv.tv_usec = (suseconds_t)(left * 1000000.0);
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        select(fd + 1, &fds, NULL, NULL, &tv);
    }
    free(pending);

    audio_stop();
    if (have_dbe) {
        xcb_dbe_deallocate_back_buffer(conn, back);
        have_dbe = 0;
    }
    if (blank_cur) {
        XDefineCursor(dpy, (Window)win, None);
        XFreeCursor(dpy, blank_cur);
        blank_cur = 0;
        host_cur = -1;
    }
    xcb_free_pixmap(conn, pixmap);
    xcb_free_gc(conn, gc);
    free(wm_proto);
    free(wm_delete);
    free(stage);
    mac_shutdown();
    XCloseDisplay(dpy);
    return 0;
}
