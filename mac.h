#ifndef XCB_MAC_H
#define XCB_MAC_H

#include <stdint.h>

/* Compact Macintosh (128K / 512K / Plus / SE / Classic) drawing port.
 *
 * The original machine is 512x342, 1 bit per pixel: 0 = white, 1 = black.
 * There is no grey hardware. Midtones are 8x8 dither patterns: ordered
 * checkers (patGray / patLtGray / patDkGray) and hatches / cross-hatches
 * (MacPaint's other trick). Games like Dark Castle painted stone, grass,
 * and shadow that way — density for value, hatch direction for material.
 *
 * You never plot RGB. You set a pen (pattern + transfer mode), then MoveTo /
 * LineTo, frame and fill rects/ovals, stamp 1-bit bitmaps, and draw System-style
 * chrome. The host scales the 1-bit page inside a bezel so the pointer can
 * reach the Mac screen edges without leaving the OS window (Mac coords clamp;
 * the OS pointer is never grabbed). Over the CRT the host cursor is hidden;
 * F12 shows it again. Optionally blooming neighbors (CRT Blend) so a checker
 * reads as grey, and composites the arrow cursor.
 *
 * The bitmap persists across frames (offscreen GrafPort). XOR a sprite to
 * erase it, or erase/fill and redraw each tick. Call mac_swap to present. */

enum {
    MAC_W = 512,
    MAC_H = 342,
    MAC_ROW = MAC_W / 8, /* 64 */
    MAC_MENU_H = 20,
    MAC_TITLE_H = 19,
    MAC_FONT_H = 8,
    MAC_FONT_W = 8
};

enum {
    MAC_COPY = 0, /* srcCopy: dst = ink */
    MAC_OR,       /* srcOr:   dst |= ink */
    MAC_XOR,      /* srcXor:  dst ^= ink */
    MAC_BIC       /* srcBic:  dst &= ~ink */
};

typedef struct {
    uint8_t row[8];
} MacPat;

typedef struct {
    int left, top, right, bottom; /* right/bottom exclusive, like QuickDraw */
} MacRect;

extern const MacPat mac_white;   /* 0% checker */
extern const MacPat mac_lt_gray; /* 25% */
extern const MacPat mac_gray;    /* 50% */
extern const MacPat mac_dk_gray; /* 75% */
extern const MacPat mac_black;   /* 100% */
extern const MacPat mac_hatch;   /* horizontal */
extern const MacPat mac_hatch_v; /* vertical */
extern const MacPat mac_hatch_d; /* diagonal */
extern const MacPat mac_x_hatch; /* both diagonals */
extern const MacPat mac_grid;    /* orthogonal cross-hatch */

MacRect mac_rect(int left, int top, int right, int bottom);
int mac_in_rect(int x, int y, MacRect r);
int mac_rect_w(MacRect r);
int mac_rect_h(MacRect r);

void mac_clip(MacRect r);
void mac_clip_reset(void);

void mac_pen_size(int w, int h);
void mac_pen_pat(MacPat pat);
void mac_pen_mode(int mode);
void mac_move_to(int x, int y);
void mac_line_to(int x, int y);
void mac_line(int x1, int y1, int x2, int y2);

void mac_erase(void); /* white page */
void mac_pixel(int x, int y, int black);
int mac_get(int x, int y); /* 0 white, 1 black */

void mac_frame_rect(MacRect r);
void mac_paint_rect(MacRect r);           /* fill with current pen pat */
void mac_fill_rect(MacRect r, MacPat pat);
void mac_erase_rect(MacRect r);           /* white */
void mac_invert_rect(MacRect r);

void mac_frame_oval(MacRect r);
void mac_paint_oval(MacRect r);
void mac_fill_oval(MacRect r, MacPat pat);
void mac_erase_oval(MacRect r);
void mac_invert_oval(MacRect r);

void mac_frame_round_rect(MacRect r, int oval_w, int oval_h);
void mac_paint_round_rect(MacRect r, int oval_w, int oval_h);
void mac_fill_round_rect(MacRect r, int oval_w, int oval_h, MacPat pat);

/* 1-bit source, bit 7 of each byte is the leftmost pixel (QuickDraw).
 * Only black source bits transfer; white is transparent except MAC_COPY. */
void mac_copy_bits(const uint8_t *src, int src_row_bytes, int src_x, int src_y,
                   int w, int h, int dst_x, int dst_y, int mode);

void mac_text(int x, int y, const char *s);
int mac_text_width(const char *s);

/* System 1 chrome. mac_desk is the grey desktop (no bar). mac_desktop also
 * stamps a title-only bar. titles[0] == "@" draws the Apple.
 *
 * For a real menu, fill MacMenu tables and call mac_menus last in the tick
 * (so the pulldown sits on top of windows). Press, drag, release — same as
 * Finder. label "-" or MAC_MENU_SEP is a dotted divider; MAC_MENU_OFF is
 * dimmed; MAC_MENU_CHECK draws a tick; cmd is an optional command-key. */
enum {
    MAC_MENU_SEP = 1,
    MAC_MENU_OFF = 2,
    MAC_MENU_CHECK = 4
};

typedef struct MacMenuItem {
    const char *label;
    int flags;
    char cmd; /* 0 = none; drawn as cloverleaf + letter */
} MacMenuItem;

typedef struct MacMenu {
    const char *title;
    const MacMenuItem *items;
    int count;
} MacMenu;

void mac_desk(void);
void mac_desktop(const char *const *titles, int n);
void mac_menubar(const char *const *titles, int n);
int mac_menu_hit(int x, int y, const char *const *titles, int n); /* -1 none */

/* Draw bar + open pulldown. Returns 1 the frame an enabled item is chosen. */
int mac_menus(const MacMenu *menus, int n, int *out_menu, int *out_item);
int mac_menu_tracking(void); /* 1 while a menu is pulled down */

void mac_quit(void);

void mac_window(MacRect r, const char *title, int close_box);
MacRect mac_window_content(MacRect r);
int mac_window_close_hit(MacRect r, int x, int y);
int mac_window_hit(MacRect r, int x, int y); /* MAC_HIT_* */
void mac_window_drag(MacRect *r);           /* title-bar drag, Finder-style */

enum {
    MAC_HIT_NONE = 0,
    MAC_HIT_CLOSE,
    MAC_HIT_TITLE,
    MAC_HIT_CONTENT
};

void mac_button(MacRect r, const char *label, int pressed, int is_default);
/* Press inside, drag: highlight only while inside; release inside = click. */
int mac_track_button(MacRect r, int *pressed);

enum {
    MAC_CUR_NONE = 0,
    MAC_CUR_ARROW = 1,
    MAC_CUR_WATCH,
    MAC_CUR_CROSS,
    MAC_CUR_IBEAM
};
void mac_cursor(int id);

enum {
    KEY_LEFT = 1,
    KEY_RIGHT,
    KEY_UP,
    KEY_DOWN,
    KEY_ESC,
    KEY_Q,
    KEY_SPACE,
    KEY_ENTER,
    KEY_COUNT
};

int mac_key_down(int key);
int mac_key_pressed(int key);
int mac_key_released(int key);

int mac_mouse_x(void);
int mac_mouse_y(void);
int mac_mouse_down(void);     /* the one Macintosh button */
int mac_mouse_pressed(void);
int mac_mouse_released(void);
int mac_mouse_double(void);   /* 1 on the second press of a double-click */
void mac_mouse_delta(int *dx, int *dy);

/* Original Mac speaker: one square-wave voice (freq 0 = click/noise). */
void mac_sound(int on);
int mac_sound_on(void);
void mac_beep(int freq_hz, int ms);

/* Host CRT: light phosphor bloom so a 50% checker reads as grey without
 * melting 1-pixel edges. The framebuffer stays 1-bit; only the window blends. */
void mac_crt(int on);
int mac_crt_on(void);

void mac_swap(void);
int mac_main(void (*tick)(float dt));

#endif /* XCB_MAC_H */
