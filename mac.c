#include "mac_internal.h"

#include <math.h>
#include <string.h>

extern const uint8_t mac_font8[96][8];

const MacPat mac_white = {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
const MacPat mac_lt_gray = {{0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22}};
const MacPat mac_gray = {{0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55}};
const MacPat mac_dk_gray = {{0x77, 0xDD, 0x77, 0xDD, 0x77, 0xDD, 0x77, 0xDD}};
const MacPat mac_black = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};
const MacPat mac_hatch = {{0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00}};
const MacPat mac_hatch_v = {{0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22}};
const MacPat mac_hatch_d = {{0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01}};
const MacPat mac_x_hatch = {{0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81}};
const MacPat mac_grid = {{0x22, 0x22, 0xFF, 0x22, 0x22, 0x22, 0xFF, 0x22}};

static uint8_t bits[MAC_H * MAC_ROW];
static uint8_t present[MAC_H * MAC_ROW];

static MacPat pen_pat;
static int pen_mode = MAC_COPY;
static int pen_w = 1, pen_h = 1;
static int pen_x, pen_y;
static int clip_l, clip_t, clip_r, clip_b;
static int cursor_id = MAC_CUR_ARROW;
static int crt_on = 1;

static uint8_t keys[KEY_COUNT], keys_prev[KEY_COUNT];
static int mouse_x, mouse_y, mouse_down, mouse_prev;
static int mouse_px, mouse_py, mouse_dbl, click_age = 999, click_x, click_y;
static int pointer_in = 1;
static int host_over_crt = 1;
static int os_owns_mouse;
static int want_quit;
static int menu_open = -1;
static int win_drag, win_drag_ox, win_drag_oy;
static int btn_track;
static MacRect btn_track_r;

/* Apple (13x11), bit 15 = left. */
static const uint16_t apple_bits[11] = {
    0x0300, 0x0600, 0x1F00, 0x3F80, 0x7FC0, 0x7DE0, 0x7FC0, 0x3F80, 0x1F00, 0x1B00, 0x0000
};

/* Mac arrow: white outline then black fill, bit 15 = left. */
static const uint16_t cur_outline[16] = {
    0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00, 0xFF80,
    0xFFC0, 0xFFE0, 0xFE00, 0xEF00, 0xCF00, 0x8780, 0x0780, 0x0380
};
static const uint16_t cur_black[16] = {
    0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00,
    0x7F80, 0x7C00, 0x6C00, 0x4600, 0x0600, 0x0300, 0x0300, 0x0000
};
static const uint16_t watch_outline[16] = {
    0x07E0, 0x1FF8, 0x3FFC, 0x7FFE, 0x7FFE, 0xFFFF, 0xFFFF, 0xFFFF,
    0xFFFF, 0xFFFF, 0xFFFF, 0x7FFE, 0x7FFE, 0x3FFC, 0x1FF8, 0x07E0
};
static const uint16_t watch_black[16] = {
    0x0000, 0x07E0, 0x1818, 0x2004, 0x2004, 0x4102, 0x4482, 0x4442,
    0x47C2, 0x4002, 0x4002, 0x2004, 0x2004, 0x1818, 0x07E0, 0x0000
};
static const uint16_t cross_outline[16] = {
    0x0000, 0x0180, 0x03C0, 0x03C0, 0x03C0, 0x03C0, 0x3FFC, 0x7FFE,
    0x7FFE, 0x3FFC, 0x03C0, 0x03C0, 0x03C0, 0x03C0, 0x0180, 0x0000
};
static const uint16_t cross_black[16] = {
    0x0000, 0x0000, 0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0x3FFC,
    0x3FFC, 0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0x0000, 0x0000
};
static const uint16_t ibeam_outline[16] = {
    0x0000, 0x3C3C, 0x3C3C, 0x0180, 0x0180, 0x0180, 0x0180, 0x0180,
    0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0x3C3C, 0x3C3C, 0x0000
};
static const uint16_t ibeam_black[16] = {
    0x0000, 0x0000, 0x1818, 0x0000, 0x0000, 0x0180, 0x0180, 0x0180,
    0x0180, 0x0180, 0x0180, 0x0000, 0x0000, 0x1818, 0x0000, 0x0000
};

MacRect mac_rect(int left, int top, int right, int bottom)
{
    MacRect r;

    r.left = left;
    r.top = top;
    r.right = right;
    r.bottom = bottom;
    return r;
}

int mac_rect_w(MacRect r)
{
    return r.right - r.left;
}

int mac_rect_h(MacRect r)
{
    return r.bottom - r.top;
}

int mac_in_rect(int x, int y, MacRect r)
{
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

static int pat_bit(MacPat p, int x, int y)
{
    return (p.row[y & 7] >> (7 - (x & 7))) & 1;
}

static void put_raw(uint8_t *buf, int x, int y, int black)
{
    int i;
    uint8_t m;

    if (x < 0 || y < 0 || x >= MAC_W || y >= MAC_H)
        return;
    i = y * MAC_ROW + (x >> 3);
    m = (uint8_t)(1u << (7 - (x & 7)));
    if (black)
        buf[i] |= m;
    else
        buf[i] &= (uint8_t)~m;
}

static void put_ink(int x, int y, int ink)
{
    int i;
    uint8_t m;

    if (x < clip_l || y < clip_t || x >= clip_r || y >= clip_b)
        return;
    i = y * MAC_ROW + (x >> 3);
    m = (uint8_t)(1u << (7 - (x & 7)));
    switch (pen_mode) {
    case MAC_OR:
        if (ink)
            bits[i] |= m;
        break;
    case MAC_XOR:
        if (ink)
            bits[i] ^= m;
        break;
    case MAC_BIC:
        if (ink)
            bits[i] &= (uint8_t)~m;
        break;
    default:
        if (ink)
            bits[i] |= m;
        else
            bits[i] &= (uint8_t)~m;
        break;
    }
}

static void stamp(int x, int y)
{
    int i, j;

    for (j = 0; j < pen_h; j++)
        for (i = 0; i < pen_w; i++)
            put_ink(x + i, y + j, pat_bit(pen_pat, x + i, y + j));
}

void mac_clip(MacRect r)
{
    clip_l = r.left;
    clip_t = r.top;
    clip_r = r.right;
    clip_b = r.bottom;
    if (clip_l < 0)
        clip_l = 0;
    if (clip_t < 0)
        clip_t = 0;
    if (clip_r > MAC_W)
        clip_r = MAC_W;
    if (clip_b > MAC_H)
        clip_b = MAC_H;
    if (clip_l > clip_r)
        clip_l = clip_r;
    if (clip_t > clip_b)
        clip_t = clip_b;
}

void mac_clip_reset(void)
{
    clip_l = 0;
    clip_t = 0;
    clip_r = MAC_W;
    clip_b = MAC_H;
}

void mac_pen_size(int w, int h)
{
    pen_w = w < 1 ? 1 : w;
    pen_h = h < 1 ? 1 : h;
}

void mac_pen_pat(MacPat pat)
{
    pen_pat = pat;
}

void mac_pen_mode(int mode)
{
    if (mode < MAC_COPY || mode > MAC_BIC)
        mode = MAC_COPY;
    pen_mode = mode;
}

void mac_move_to(int x, int y)
{
    pen_x = x;
    pen_y = y;
}

void mac_line_to(int x, int y)
{
    mac_line(pen_x, pen_y, x, y);
    pen_x = x;
    pen_y = y;
}

void mac_line(int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0, dy = y1 - y0;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    int err, x = x0, y = y0;

    stamp(x, y);
    if (ax > ay) {
        err = ax / 2;
        while (x != x1) {
            x += sx;
            err -= ay;
            if (err < 0) {
                y += sy;
                err += ax;
            }
            stamp(x, y);
        }
    } else {
        err = ay / 2;
        while (y != y1) {
            y += sy;
            err -= ax;
            if (err < 0) {
                x += sx;
                err += ay;
            }
            stamp(x, y);
        }
    }
}

void mac_erase(void)
{
    memset(bits, 0, sizeof(bits));
}

void mac_pixel(int x, int y, int black)
{
    put_ink(x, y, black ? 1 : 0);
}

int mac_get(int x, int y)
{
    int i;
    uint8_t m;

    if (x < 0 || y < 0 || x >= MAC_W || y >= MAC_H)
        return 0;
    i = y * MAC_ROW + (x >> 3);
    m = (uint8_t)(1u << (7 - (x & 7)));
    return (bits[i] & m) ? 1 : 0;
}

static void fill_span(int x0, int x1, int y, MacPat pat, int mode_save)
{
    int x, saved = pen_mode;

    (void)mode_save;
    pen_mode = saved;
    if (x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    for (x = x0; x <= x1; x++)
        put_ink(x, y, pat_bit(pat, x, y));
}

void mac_fill_rect(MacRect r, MacPat pat)
{
    int x, y;

    if (r.left < clip_l)
        r.left = clip_l;
    if (r.top < clip_t)
        r.top = clip_t;
    if (r.right > clip_r)
        r.right = clip_r;
    if (r.bottom > clip_b)
        r.bottom = clip_b;
    for (y = r.top; y < r.bottom; y++)
        for (x = r.left; x < r.right; x++)
            put_ink(x, y, pat_bit(pat, x, y));
}

void mac_paint_rect(MacRect r)
{
    mac_fill_rect(r, pen_pat);
}

void mac_erase_rect(MacRect r)
{
    int saved = pen_mode;

    pen_mode = MAC_COPY;
    mac_fill_rect(r, mac_white);
    pen_mode = saved;
}

void mac_invert_rect(MacRect r)
{
    int x, y, saved = pen_mode;

    pen_mode = MAC_XOR;
    for (y = r.top; y < r.bottom; y++)
        for (x = r.left; x < r.right; x++)
            put_ink(x, y, 1);
    pen_mode = saved;
}

void mac_frame_rect(MacRect r)
{
    int w = r.right - r.left;
    int h = r.bottom - r.top;

    if (w < 1 || h < 1)
        return;
    mac_line(r.left, r.top, r.right - 1, r.top);
    mac_line(r.right - 1, r.top, r.right - 1, r.bottom - 1);
    mac_line(r.right - 1, r.bottom - 1, r.left, r.bottom - 1);
    mac_line(r.left, r.bottom - 1, r.left, r.top);
}

static void oval_spans(MacRect r, MacPat pat, int invert)
{
    int y, w, h;
    double rx, ry, cx, cy;

    w = r.right - r.left;
    h = r.bottom - r.top;
    if (w < 1 || h < 1)
        return;
    rx = w / 2.0;
    ry = h / 2.0;
    cx = r.left + rx;
    cy = r.top + ry;
    if (rx < 0.5)
        rx = 0.5;
    if (ry < 0.5)
        ry = 0.5;
    for (y = r.top; y < r.bottom; y++) {
        double dy = (y + 0.5) - cy;
        double t = 1.0 - (dy * dy) / (ry * ry);
        int x0, x1;

        if (t < 0.0)
            continue;
        {
            double dx = rx * sqrt(t);
            x0 = (int)ceil(cx - dx);
            x1 = (int)floor(cx + dx - 0.0001);
        }
        if (invert) {
            int x, saved = pen_mode;

            pen_mode = MAC_XOR;
            for (x = x0; x <= x1; x++)
                put_ink(x, y, 1);
            pen_mode = saved;
        } else {
            fill_span(x0, x1, y, pat, 0);
        }
    }
}

void mac_fill_oval(MacRect r, MacPat pat)
{
    oval_spans(r, pat, 0);
}

void mac_paint_oval(MacRect r)
{
    mac_fill_oval(r, pen_pat);
}

void mac_erase_oval(MacRect r)
{
    int saved = pen_mode;

    pen_mode = MAC_COPY;
    mac_fill_oval(r, mac_white);
    pen_mode = saved;
}

void mac_invert_oval(MacRect r)
{
    oval_spans(r, mac_black, 1);
}

void mac_frame_oval(MacRect r)
{
    int y, w, h;
    double rx, ry, cx, cy;

    w = r.right - r.left;
    h = r.bottom - r.top;
    if (w < 1 || h < 1)
        return;
    rx = (w - 1) / 2.0;
    ry = (h - 1) / 2.0;
    cx = r.left + rx;
    cy = r.top + ry;
    if (rx < 0.5)
        rx = 0.5;
    if (ry < 0.5)
        ry = 0.5;
    for (y = r.top; y < r.bottom; y++) {
        double dy = (y + 0.5) - cy;
        double t = 1.0 - (dy * dy) / (ry * ry);
        int x0, x1;

        if (t < 0.0)
            continue;
        {
            double dx = rx * sqrt(t);
            x0 = (int)floor(cx - dx + 0.5);
            x1 = (int)floor(cx + dx + 0.5);
        }
        stamp(x0, y);
        stamp(x1, y);
    }
    {
        int x;

        for (x = r.left; x < r.right; x++) {
            double dx = (x + 0.5) - cx;
            double t = 1.0 - (dx * dx) / (rx * rx);
            int y0, y1;

            if (t < 0.0)
                continue;
            {
                double dy = ry * sqrt(t);
                y0 = (int)floor(cy - dy + 0.5);
                y1 = (int)floor(cy + dy + 0.5);
            }
            stamp(x, y0);
            stamp(x, y1);
        }
    }
}

static int in_round_rect(int x, int y, MacRect r, int rx, int ry)
{
    int x0, x1, y0, y1, cx, cy;
    double dx, dy;

    if (x < r.left || x >= r.right || y < r.top || y >= r.bottom)
        return 0;
    if (rx < 1 || ry < 1)
        return 1;
    x0 = r.left + rx;
    x1 = r.right - 1 - rx;
    y0 = r.top + ry;
    y1 = r.bottom - 1 - ry;
    if (x1 < x0)
        x1 = x0;
    if (y1 < y0)
        y1 = y0;
    if (x >= x0 && x <= x1)
        return 1;
    if (y >= y0 && y <= y1)
        return 1;
    cx = x < x0 ? r.left + rx : r.right - 1 - rx;
    cy = y < y0 ? r.top + ry : r.bottom - 1 - ry;
    dx = (x + 0.5) - (cx + 0.5);
    dy = (y + 0.5) - (cy + 0.5);
    return (dx * dx) / ((double)rx * rx) + (dy * dy) / ((double)ry * ry) <= 1.0;
}

void mac_fill_round_rect(MacRect r, int oval_w, int oval_h, MacPat pat)
{
    int x, y, rx = oval_w / 2, ry = oval_h / 2;
    int w = mac_rect_w(r), h = mac_rect_h(r);

    if (rx > w / 2)
        rx = w / 2;
    if (ry > h / 2)
        ry = h / 2;
    for (y = r.top; y < r.bottom; y++)
        for (x = r.left; x < r.right; x++)
            if (in_round_rect(x, y, r, rx, ry))
                put_ink(x, y, pat_bit(pat, x, y));
}

void mac_paint_round_rect(MacRect r, int oval_w, int oval_h)
{
    mac_fill_round_rect(r, oval_w, oval_h, pen_pat);
}

void mac_frame_round_rect(MacRect r, int oval_w, int oval_h)
{
    int x, y, rx = oval_w / 2, ry = oval_h / 2;
    int w = mac_rect_w(r), h = mac_rect_h(r);
    MacRect inner;

    if (rx > w / 2)
        rx = w / 2;
    if (ry > h / 2)
        ry = h / 2;
    inner = mac_rect(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1);
    for (y = r.top; y < r.bottom; y++)
        for (x = r.left; x < r.right; x++)
            if (in_round_rect(x, y, r, rx, ry) && !in_round_rect(x, y, inner, rx > 0 ? rx - 1 : 0, ry > 0 ? ry - 1 : 0))
                stamp(x, y);
}

void mac_copy_bits(const uint8_t *src, int src_row_bytes, int src_x, int src_y,
                   int w, int h, int dst_x, int dst_y, int mode)
{
    int x, y, saved = pen_mode;

    if (!src || src_row_bytes < 1 || w < 1 || h < 1)
        return;
    pen_mode = mode;
    for (y = 0; y < h; y++) {
        const uint8_t *row = src + (size_t)(src_y + y) * (size_t)src_row_bytes;
        for (x = 0; x < w; x++) {
            int sx = src_x + x;
            int bit = (row[sx >> 3] >> (7 - (sx & 7))) & 1;
            if (mode == MAC_COPY)
                put_ink(dst_x + x, dst_y + y, bit);
            else if (bit)
                put_ink(dst_x + x, dst_y + y, 1);
        }
    }
    pen_mode = saved;
}

void mac_text(int x, int y, const char *s)
{
    int saved = pen_mode;
    const unsigned char *p;

    if (!s)
        return;
    pen_mode = MAC_OR;
    for (p = (const unsigned char *)s; *p; p++, x += MAC_FONT_W) {
        const uint8_t *g;
        int row, col;
        unsigned c = *p;

        if (c < 32 || c > 127)
            c = '?';
        g = mac_font8[c - 32];
        for (row = 0; row < 8; row++)
            for (col = 0; col < 8; col++)
                if (g[row] & (1u << col))
                    put_ink(x + col, y + row, 1);
    }
    pen_mode = saved;
}

int mac_text_width(const char *s)
{
    return s ? (int)strlen(s) * MAC_FONT_W : 0;
}

static void draw_apple(int x, int y)
{
    int row, col, saved = pen_mode;

    pen_mode = MAC_OR;
    for (row = 0; row < 11; row++)
        for (col = 0; col < 13; col++)
            if (apple_bits[row] & (0x8000u >> col))
                put_ink(x + col, y + row, 1);
    pen_mode = saved;
}

static int menu_item_x(const char *const *titles, int n, int index)
{
    int x = 6;
    int i;

    for (i = 0; i < index && i < n; i++) {
        if (titles[i] && titles[i][0] == '@' && titles[i][1] == '\0')
            x += 28;
        else
            x += mac_text_width(titles[i] ? titles[i] : "") + 16;
    }
    return x;
}

static int menu_item_w(const char *title)
{
    if (title && title[0] == '@' && title[1] == '\0')
        return 28;
    return mac_text_width(title ? title : "") + 16;
}

void mac_menubar(const char *const *titles, int n)
{
    int i, saved = pen_mode;
    MacRect bar = mac_rect(0, 0, MAC_W, MAC_MENU_H);

    pen_mode = MAC_COPY;
    mac_fill_rect(bar, mac_white);
    mac_fill_rect(mac_rect(0, MAC_MENU_H - 1, MAC_W, MAC_MENU_H), mac_black);
    if (!titles)
        return;
    for (i = 0; i < n; i++) {
        int x = menu_item_x(titles, n, i);
        if (titles[i] && titles[i][0] == '@' && titles[i][1] == '\0')
            draw_apple(x + 7, 4);
        else
            mac_text(x + 8, 6, titles[i] ? titles[i] : "");
    }
    pen_mode = saved;
}

int mac_menu_hit(int x, int y, const char *const *titles, int n)
{
    int i;

    if (y < 0 || y >= MAC_MENU_H || !titles)
        return -1;
    for (i = 0; i < n; i++) {
        int x0 = menu_item_x(titles, n, i);
        int w = menu_item_w(titles[i]);
        if (x >= x0 && x < x0 + w)
            return i;
    }
    return -1;
}

void mac_desk(void)
{
    int saved = pen_mode;

    mac_clip_reset();
    pen_mode = MAC_COPY;
    mac_fill_rect(mac_rect(0, MAC_MENU_H, MAC_W, MAC_H), mac_gray);
    pen_mode = saved;
}

void mac_desktop(const char *const *titles, int n)
{
    mac_desk();
    mac_menubar(titles, n);
}

void mac_quit(void)
{
    want_quit = 1;
}

int mac_want_quit(void)
{
    return want_quit;
}

int mac_menu_tracking(void)
{
    return menu_open >= 0;
}

enum { MENU_ROW = 16, MENU_SEP_H = 8, MENU_PAD = 16 };

static int menu_is_apple(const char *t)
{
    return t && t[0] == '@' && t[1] == '\0';
}

static int menu_title_w(const char *t)
{
    return menu_is_apple(t) ? 28 : mac_text_width(t ? t : "") + 16;
}

static int menus_title_x(const MacMenu *menus, int n, int index)
{
    int x = 6;
    int i;

    for (i = 0; i < index && i < n; i++)
        x += menu_title_w(menus[i].title);
    return x;
}

static int menu_is_sep(const MacMenuItem *it)
{
    if (!it)
        return 1;
    if (it->flags & MAC_MENU_SEP)
        return 1;
    return it->label && it->label[0] == '-' && it->label[1] == '\0';
}

static int menu_can_choose(const MacMenuItem *it)
{
    return it && !menu_is_sep(it) && !(it->flags & MAC_MENU_OFF);
}

static int menu_row_h(const MacMenuItem *it)
{
    return menu_is_sep(it) ? MENU_SEP_H : MENU_ROW;
}

static int pulldown_h(const MacMenu *m)
{
    int i, h = 2;

    if (!m || !m->items)
        return 4;
    for (i = 0; i < m->count; i++)
        h += menu_row_h(&m->items[i]);
    return h + 1;
}

static int pulldown_w(const MacMenu *m)
{
    int i, w;

    w = menu_title_w(m->title);
    if (w < 104)
        w = 104;
    if (!m->items)
        return w;
    for (i = 0; i < m->count; i++) {
        int iw;
        if (menu_is_sep(&m->items[i]))
            continue;
        iw = MENU_PAD + mac_text_width(m->items[i].label ? m->items[i].label : "") + 14;
        if (m->items[i].cmd)
            iw += 28;
        if (iw > w)
            w = iw;
    }
    return w;
}

static int pulldown_item_at(const MacMenu *m, int rel_y)
{
    int i, y = 1;

    if (!m || !m->items)
        return -1;
    for (i = 0; i < m->count; i++) {
        int h = menu_row_h(&m->items[i]);
        if (rel_y >= y && rel_y < y + h)
            return i;
        y += h;
    }
    return -1;
}

static void menu_text_dim(int x, int y, const char *s)
{
    int saved = pen_mode;
    const unsigned char *p;

    if (!s)
        return;
    pen_mode = MAC_OR;
    for (p = (const unsigned char *)s; *p; p++, x += MAC_FONT_W) {
        const uint8_t *g;
        int row, col;
        unsigned c = *p;

        if (c < 32 || c > 127)
            c = '?';
        g = mac_font8[c - 32];
        for (row = 0; row < 8; row++)
            for (col = 0; col < 8; col++)
                if ((g[row] & (1u << col)) && pat_bit(mac_gray, x + col, y + row))
                    put_ink(x + col, y + row, 1);
    }
    pen_mode = saved;
}

static void menu_stamp8(int x, int y, const uint8_t *g, int dim)
{
    int row, col, saved = pen_mode;

    pen_mode = MAC_OR;
    for (row = 0; row < 8; row++)
        for (col = 0; col < 8; col++)
            if (g[row] & (0x80u >> col)) {
                if (!dim || pat_bit(mac_gray, x + col, y + row))
                    put_ink(x + col, y + row, 1);
            }
    pen_mode = saved;
}

static const uint8_t menu_check[8] = { 0x00, 0x01, 0x03, 0x06, 0xCC, 0x78, 0x30, 0x00 };
static const uint8_t menu_cmd[8] = { 0x66, 0x99, 0x81, 0x66, 0x66, 0x81, 0x99, 0x66 };

static void draw_menu_title(const MacMenu *menu, int x, int inverted)
{
    int w = menu_title_w(menu->title);
    MacRect slot = mac_rect(x, 0, x + w, MAC_MENU_H - 1);

    if (menu_is_apple(menu->title))
        draw_apple(x + 7, 4);
    else
        mac_text(x + 8, 6, menu->title ? menu->title : "");
    if (inverted)
        mac_invert_rect(slot);
}

static int menus_title_hit(const MacMenu *menus, int n, int x, int y)
{
    int i;

    if (y < 0 || y >= MAC_MENU_H || !menus)
        return -1;
    for (i = 0; i < n; i++) {
        int x0 = menus_title_x(menus, n, i);
        if (x >= x0 && x < x0 + menu_title_w(menus[i].title))
            return i;
    }
    return -1;
}

static void draw_pulldown(const MacMenu *m, MacRect box, int hover)
{
    int i, y, dim;
    char letter[2];

    mac_fill_rect(mac_rect(box.left + 2, box.top + 2, box.right + 2, box.bottom + 2), mac_black);
    mac_fill_rect(box, mac_white);
    mac_frame_rect(box);

    y = box.top + 1;
    for (i = 0; i < m->count; i++) {
        const MacMenuItem *it = &m->items[i];
        int h = menu_row_h(it);
        MacRect row = mac_rect(box.left + 1, y, box.right - 1, y + h);

        if (menu_is_sep(it)) {
            int x;
            int mid = y + h / 2;
            for (x = box.left + 4; x < box.right - 4; x += 2)
                put_ink(x, mid, 1);
        } else {
            dim = (it->flags & MAC_MENU_OFF) ? 1 : 0;
            if (it->flags & MAC_MENU_CHECK)
                menu_stamp8(box.left + 4, y + 4, menu_check, dim);
            if (dim)
                menu_text_dim(box.left + MENU_PAD, y + 4, it->label ? it->label : "");
            else
                mac_text(box.left + MENU_PAD, y + 4, it->label ? it->label : "");
            if (it->cmd) {
                int cx = box.right - 26;
                menu_stamp8(cx, y + 4, menu_cmd, dim);
                letter[0] = it->cmd;
                letter[1] = 0;
                if (letter[0] >= 'a' && letter[0] <= 'z')
                    letter[0] = (char)(letter[0] - 32);
                if (dim)
                    menu_text_dim(cx + 10, y + 4, letter);
                else
                    mac_text(cx + 10, y + 4, letter);
            }
            if (i == hover && menu_can_choose(it))
                mac_invert_rect(row);
        }
        y += h;
    }
}

int mac_menus(const MacMenu *menus, int n, int *out_menu, int *out_item)
{
    int i, mx, my, hit, hover, chosen;
    int saved_mode, saved_w, saved_h;
    MacPat saved_pat;
    MacRect saved_clip;

    if (out_menu)
        *out_menu = -1;
    if (out_item)
        *out_item = -1;
    if (!menus || n < 1)
        return 0;

    saved_mode = pen_mode;
    saved_pat = pen_pat;
    saved_w = pen_w;
    saved_h = pen_h;
    saved_clip = mac_rect(clip_l, clip_t, clip_r, clip_b);

    mac_clip_reset();
    pen_mode = MAC_COPY;
    mac_pen_size(1, 1);
    mac_pen_pat(mac_black);

    mx = mouse_x;
    my = mouse_y;
    hit = menus_title_hit(menus, n, mx, my);

    if (menu_open >= n)
        menu_open = -1;

    if (mouse_down && !mouse_prev) {
        menu_open = hit;
    } else if (mouse_down && menu_open >= 0 && hit >= 0 && hit != menu_open && my < MAC_MENU_H) {
        menu_open = hit;
    }

    mac_fill_rect(mac_rect(0, 0, MAC_W, MAC_MENU_H), mac_white);
    mac_fill_rect(mac_rect(0, MAC_MENU_H - 1, MAC_W, MAC_MENU_H), mac_black);
    for (i = 0; i < n; i++)
        draw_menu_title(&menus[i], menus_title_x(menus, n, i), i == menu_open);

    chosen = 0;
    hover = -1;
    if (menu_open >= 0 && menu_open < n) {
        const MacMenu *m = &menus[menu_open];
        int pw = pulldown_w(m);
        int ph = pulldown_h(m);
        int px = menus_title_x(menus, n, menu_open);
        MacRect box;

        if (px + pw > MAC_W - 3)
            px = MAC_W - 3 - pw;
        if (px < 1)
            px = 1;
        box = mac_rect(px, MAC_MENU_H - 1, px + pw, MAC_MENU_H - 1 + ph);
        if (box.bottom > MAC_H - 2)
            box.bottom = MAC_H - 2;

        draw_pulldown(m, box, -1);
        if (mac_in_rect(mx, my, box))
            hover = pulldown_item_at(m, my - box.top);
        if (hover >= 0 && m->items && hover < m->count && menu_can_choose(&m->items[hover])) {
            int y = box.top + 1, k;
            for (k = 0; k < hover; k++)
                y += menu_row_h(&m->items[k]);
            mac_invert_rect(mac_rect(box.left + 1, y, box.right - 1, y + menu_row_h(&m->items[hover])));
        }

        if (!mouse_down && mouse_prev) {
            if (hover >= 0 && m->items && hover < m->count && menu_can_choose(&m->items[hover])) {
                chosen = 1;
                if (out_menu)
                    *out_menu = menu_open;
                if (out_item)
                    *out_item = hover;
            }
            menu_open = -1;
        }
    }

    pen_mode = saved_mode;
    pen_pat = saved_pat;
    pen_w = saved_w;
    pen_h = saved_h;
    mac_clip(saved_clip);
    return chosen;
}

static void hline_black(int x0, int x1, int y)
{
    int x, saved = pen_mode;

    pen_mode = MAC_COPY;
    if (x0 > x1) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    for (x = x0; x <= x1; x++)
        put_ink(x, y, 1);
    pen_mode = saved;
}

void mac_window(MacRect r, const char *title, int close_box)
{
    int saved = pen_mode;
    int tw, tx, y;
    MacRect inner, shadow, title_box, close;

    pen_mode = MAC_COPY;
    mac_pen_size(1, 1);
    mac_pen_pat(mac_black);

    shadow = mac_rect(r.left + 2, r.top + 2, r.right + 2, r.bottom + 2);
    mac_fill_rect(shadow, mac_black);

    inner = mac_rect(r.left, r.top, r.right, r.bottom);
    mac_fill_rect(inner, mac_white);
    mac_frame_rect(inner);

    /* title-bar stripes */
    for (y = r.top + 2; y < r.top + MAC_TITLE_H - 1; y += 2)
        hline_black(r.left + 1, r.right - 2, y);
    mac_fill_rect(mac_rect(r.left, r.top + MAC_TITLE_H - 1, r.right, r.top + MAC_TITLE_H), mac_black);

    tw = mac_text_width(title ? title : "");
    tx = r.left + (mac_rect_w(r) - tw) / 2;
    title_box = mac_rect(tx - 4, r.top + 4, tx + tw + 4, r.top + 14);
    mac_fill_rect(title_box, mac_white);
    mac_text(tx, r.top + 5, title ? title : "");

    if (close_box) {
        close = mac_rect(r.left + 8, r.top + 4, r.left + 19, r.top + 15);
        mac_fill_rect(close, mac_white);
        mac_frame_rect(close);
        mac_frame_rect(mac_rect(close.left + 2, close.top + 2, close.right - 2, close.bottom - 2));
    }
    pen_mode = saved;
}

MacRect mac_window_content(MacRect r)
{
    return mac_rect(r.left + 1, r.top + MAC_TITLE_H, r.right - 1, r.bottom - 1);
}

int mac_window_close_hit(MacRect r, int x, int y)
{
    return mac_window_hit(r, x, y) == MAC_HIT_CLOSE;
}

int mac_window_hit(MacRect r, int x, int y)
{
    MacRect close, title, content;

    if (!mac_in_rect(x, y, r))
        return MAC_HIT_NONE;
    close = mac_rect(r.left + 8, r.top + 4, r.left + 19, r.top + 15);
    title = mac_rect(r.left, r.top, r.right, r.top + MAC_TITLE_H);
    content = mac_window_content(r);
    if (mac_in_rect(x, y, close))
        return MAC_HIT_CLOSE;
    if (mac_in_rect(x, y, title))
        return MAC_HIT_TITLE;
    if (mac_in_rect(x, y, content))
        return MAC_HIT_CONTENT;
    return MAC_HIT_NONE;
}

void mac_window_drag(MacRect *r)
{
    int w, h, nx, ny;

    if (!r)
        return;
    if (menu_open >= 0) {
        win_drag = 0;
        return;
    }
    if (mouse_down && !mouse_prev && mac_window_hit(*r, mouse_x, mouse_y) == MAC_HIT_TITLE) {
        win_drag = 1;
        win_drag_ox = mouse_x - r->left;
        win_drag_oy = mouse_y - r->top;
    }
    if (win_drag && mouse_down) {
        w = mac_rect_w(*r);
        h = mac_rect_h(*r);
        nx = mouse_x - win_drag_ox;
        ny = mouse_y - win_drag_oy;
        if (ny < MAC_MENU_H)
            ny = MAC_MENU_H;
        if (ny > MAC_H - 24)
            ny = MAC_H - 24;
        if (nx > MAC_W - 48)
            nx = MAC_W - 48;
        if (nx + w < 48)
            nx = 48 - w;
        r->left = nx;
        r->top = ny;
        r->right = nx + w;
        r->bottom = ny + h;
    }
    if (!mouse_down)
        win_drag = 0;
}

static int same_rect(MacRect a, MacRect b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

int mac_track_button(MacRect r, int *pressed)
{
    int in = mac_in_rect(mouse_x, mouse_y, r);
    int highlight = 0, click = 0;

    if (menu_open >= 0 || win_drag) {
        if (pressed)
            *pressed = 0;
        if (!mouse_down)
            btn_track = 0;
        return 0;
    }
    if (mouse_down && !mouse_prev && in) {
        btn_track = 1;
        btn_track_r = r;
    }
    if (btn_track && same_rect(r, btn_track_r)) {
        highlight = mouse_down && in;
        if (!mouse_down && mouse_prev) {
            click = in;
            btn_track = 0;
        }
    }
    if (pressed)
        *pressed = highlight;
    return click;
}

void mac_button(MacRect r, const char *label, int pressed, int is_default)
{
    int saved = pen_mode;
    int tw, tx, ty;
    MacRect face = r;

    pen_mode = MAC_COPY;
    mac_pen_size(1, 1);
    mac_pen_pat(mac_black);
    if (is_default) {
        mac_frame_round_rect(mac_rect(r.left - 4, r.top - 4, r.right + 4, r.bottom + 4), 16, 16);
        mac_frame_round_rect(mac_rect(r.left - 3, r.top - 3, r.right + 3, r.bottom + 3), 16, 16);
    }
    mac_fill_round_rect(face, 12, 12, mac_white);
    mac_frame_round_rect(face, 12, 12);
    tw = mac_text_width(label ? label : "");
    tx = face.left + (mac_rect_w(face) - tw) / 2;
    ty = face.top + (mac_rect_h(face) - MAC_FONT_H) / 2;
    mac_text(tx, ty, label ? label : "");
    if (pressed)
        mac_invert_rect(mac_rect(face.left + 2, face.top + 2, face.right - 2, face.bottom - 2));
    pen_mode = saved;
}

void mac_cursor(int id)
{
    cursor_id = id;
}

static void plot_cursor(int x, int y, const uint16_t *outline, const uint16_t *fill)
{
    int row, col;

    for (row = 0; row < 16; row++) {
        for (col = 0; col < 16; col++) {
            int o = (outline[row] >> (15 - col)) & 1;
            int b = (fill[row] >> (15 - col)) & 1;
            if (o && !b)
                put_raw(present, x + col, y + row, 0);
            if (b)
                put_raw(present, x + col, y + row, 1);
        }
    }
}

const uint8_t *mac_present(void)
{
    memcpy(present, bits, sizeof(bits));
    if (!pointer_in || cursor_id == MAC_CUR_NONE || os_owns_mouse || !host_over_crt)
        return present;
    /* Host cursor is hidden over the CRT, so the 1-bit pointer sits on the click. */
    if (cursor_id == MAC_CUR_ARROW)
        plot_cursor(mouse_x, mouse_y, cur_outline, cur_black);
    else if (cursor_id == MAC_CUR_WATCH)
        plot_cursor(mouse_x - 8, mouse_y - 8, watch_outline, watch_black);
    else if (cursor_id == MAC_CUR_CROSS)
        plot_cursor(mouse_x - 8, mouse_y - 8, cross_outline, cross_black);
    else if (cursor_id == MAC_CUR_IBEAM)
        plot_cursor(mouse_x - 8, mouse_y - 8, ibeam_outline, ibeam_black);
    return present;
}

enum { HOST_WHITE = 0xfff4efe4u, HOST_BLACK = 0xff1a1510u };

void mac_crt(int on)
{
    crt_on = on ? 1 : 0;
}

int mac_crt_on(void)
{
    return crt_on;
}

static int src_black(const uint8_t *src, int x, int y)
{
    uint8_t m;

    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x >= MAC_W)
        x = MAC_W - 1;
    if (y >= MAC_H)
        y = MAC_H - 1;
    m = (uint8_t)(1u << (7 - (x & 7)));
    return (src[y * MAC_ROW + (x >> 3)] & m) ? 1 : 0;
}

uint32_t mac_host_pixel(const uint8_t *src, int x, int y)
{
    /* Plus-shaped bloom, heavy center: checker still mixes, 1px edges don't. */
    enum { SUM = 12 };
    int white, r, g, b;

    if (!src)
        return HOST_WHITE;
    if (!crt_on)
        return src_black(src, x, y) ? HOST_BLACK : HOST_WHITE;

    white = 0;
    if (!src_black(src, x, y))
        white += 8;
    if (!src_black(src, x, y - 1))
        white += 1;
    if (!src_black(src, x, y + 1))
        white += 1;
    if (!src_black(src, x - 1, y))
        white += 1;
    if (!src_black(src, x + 1, y))
        white += 1;
    r = (0x1a * (SUM - white) + 0xf4 * white) / SUM;
    g = (0x15 * (SUM - white) + 0xef * white) / SUM;
    b = (0x10 * (SUM - white) + 0xe4 * white) / SUM;
    return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

int mac_init(void)
{
    mac_clip_reset();
    pen_pat = mac_black;
    pen_mode = MAC_COPY;
    pen_w = 1;
    pen_h = 1;
    pen_x = pen_y = 0;
    cursor_id = MAC_CUR_ARROW;
    memset(keys, 0, sizeof(keys));
    memset(keys_prev, 0, sizeof(keys_prev));
    mouse_x = MAC_W / 2;
    mouse_y = MAC_H / 2;
    mouse_px = mouse_x;
    mouse_py = mouse_y;
    mouse_down = mouse_prev = 0;
    mouse_dbl = 0;
    click_age = 999;
    pointer_in = 1;
    want_quit = 0;
    menu_open = -1;
    win_drag = 0;
    btn_track = 0;
    mac_snd_init();
    mac_erase();
    return 1;
}

void mac_shutdown(void)
{
    mac_snd_shutdown();
}

void mac_key_set(int key, int down)
{
    if (key > 0 && key < KEY_COUNT)
        keys[key] = down ? 1 : 0;
}

void mac_keys_end_frame(void)
{
    memcpy(keys_prev, keys, sizeof(keys));
    mouse_prev = mouse_down;
    mouse_px = mouse_x;
    mouse_py = mouse_y;
    mouse_dbl = 0;
    if (click_age < 1000)
        click_age++;
}

void mac_mouse_set(int x, int y)
{
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x >= MAC_W)
        x = MAC_W - 1;
    if (y >= MAC_H)
        y = MAC_H - 1;
    mouse_x = x;
    mouse_y = y;
}

void mac_mouse_button(int down)
{
    int adx, ady;

    if (down && !mouse_down) {
        adx = mouse_x - click_x;
        ady = mouse_y - click_y;
        if (adx < 0)
            adx = -adx;
        if (ady < 0)
            ady = -ady;
        mouse_dbl = (click_age < 48 && adx < 12 && ady < 12) ? 1 : 0;
        click_x = mouse_x;
        click_y = mouse_y;
        click_age = 0;
    }
    mouse_down = down ? 1 : 0;
}

void mac_pointer_enter(void)
{
    pointer_in = 1;
}

void mac_pointer_leave(void)
{
    pointer_in = 0;
    mouse_down = 0;
    mouse_prev = 0;
    win_drag = 0;
    btn_track = 0;
    menu_open = -1;
}

void mac_host_pointer(int over_crt, int os_owns)
{
    host_over_crt = over_crt ? 1 : 0;
    os_owns_mouse = os_owns ? 1 : 0;
}

int mac_key_down(int key)
{
    return (key > 0 && key < KEY_COUNT) ? keys[key] : 0;
}

int mac_key_pressed(int key)
{
    return (key > 0 && key < KEY_COUNT) ? (keys[key] && !keys_prev[key]) : 0;
}

int mac_key_released(int key)
{
    return (key > 0 && key < KEY_COUNT) ? (!keys[key] && keys_prev[key]) : 0;
}

int mac_mouse_x(void)
{
    return mouse_x;
}

int mac_mouse_y(void)
{
    return mouse_y;
}

int mac_mouse_down(void)
{
    return mouse_down;
}

int mac_mouse_pressed(void)
{
    return mouse_down && !mouse_prev;
}

int mac_mouse_released(void)
{
    return !mouse_down && mouse_prev;
}

int mac_mouse_double(void)
{
    return mouse_dbl;
}

void mac_mouse_delta(int *dx, int *dy)
{
    if (dx)
        *dx = mouse_x - mouse_px;
    if (dy)
        *dy = mouse_y - mouse_py;
}
