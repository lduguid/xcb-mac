/* MacPaint-style 1-bit studio. The document is a compact Mac screen
 * (512x342). Patterns, FatBits, and a pencil are how Dark Castle-like
 * stills were actually made — not QuickDraw ovals. */

#include "mac.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { DOC_W = MAC_W, DOC_H = MAC_H, DOC_ROW = MAC_ROW };
enum { TOOL_W = 32, CELL = 16, PAT_H = 32, ZOOM = 8 };
enum {
    T_PENCIL, T_ERASER, T_BRUSH, T_SPRAY, T_FILL, T_LINE,
    T_RECT, T_RECTF, T_OVAL, T_OVALF, T_RRECT, T_RRECTF,
    T_HAND, T_FAT, T_COUNT
};

static const char *const tool_name[T_COUNT] = {
    "Pencil",
    "Eraser",
    "Brush",
    "Spray Can",
    "Paint Bucket",
    "Line",
    "Rectangle",
    "Filled Rect",
    "Oval",
    "Filled Oval",
    "Round Rect",
    "Filled Round Rect",
    "Grabber",
    "FatBits"
};

static const MacPat pats[] = {
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}, /* white */
    {{0x88, 0x00, 0x22, 0x00, 0x88, 0x00, 0x22, 0x00}}, /* 12% */
    {{0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22}}, /* ltGray */
    {{0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55}}, /* gray */
    {{0x77, 0xDD, 0x77, 0xDD, 0x77, 0xDD, 0x77, 0xDD}}, /* dkGray */
    {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}}, /* black */
    {{0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00}}, /* hatch */
    {{0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22}}, /* vert */
    {{0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01}}, /* diag */
    {{0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81}}, /* xhatch */
    {{0x22, 0x22, 0xFF, 0x22, 0x22, 0x22, 0xFF, 0x22}}, /* grid */
    {{0xF0, 0xF0, 0xF0, 0xF0, 0x0F, 0x0F, 0x0F, 0x0F}}, /* brick */
    {{0xCC, 0xCC, 0x33, 0x33, 0xCC, 0xCC, 0x33, 0x33}}, /* 2x2 */
    {{0xFE, 0xBA, 0xEE, 0x55, 0xBA, 0xEE, 0x55, 0xBB}}, /* clump */
    {{0xAA, 0x00, 0xAA, 0x00, 0xAA, 0x00, 0xAA, 0x00}}, /* dots */
    {{0xEE, 0xDD, 0xBB, 0x77, 0xEE, 0xDD, 0xBB, 0x77}}, /* scales */
};
enum { N_PATS = (int)(sizeof pats / sizeof pats[0]) };

static const char *const pat_name[N_PATS] = {
    "White", "12%", "LtGray", "Gray", "DkGray", "Black",
    "Hatch", "Vert", "Diag", "XHatch", "Grid", "Brick",
    "2x2", "Clump", "Dots", "Scales"
};
enum { PAT_WELL = 40 };

static MacMenuItem apple_items[] = {
    { "About Paint...", 0, 0 },
    { "-", MAC_MENU_SEP, 0 },
};
static MacMenuItem file_items[] = {
    { "New", 0, 'N' },
    { "Open painting.pbm", 0, 'O' },
    { "Save painting.pbm", 0, 'S' },
    { "Export painting.h", 0, 'E' },
    { "-", MAC_MENU_SEP, 0 },
    { "Quit", 0, 'Q' },
};
static MacMenuItem edit_items[] = {
    { "Undo", 0, 'Z' },
    { "-", MAC_MENU_SEP, 0 },
    { "Invert", 0, 'I' },
    { "Fill", 0, 0 },
};
static MacMenuItem goodies_items[] = {
    { "FatBits", 0, 'F' },
    { "Grid", 0, 'G' },
    { "CRT Blend", 0, 'B' },
    { "-", MAC_MENU_SEP, 0 },
    { "Pen size", MAC_MENU_OFF, 0 },
    { "1 x 1  pixel", 0, 0 },
    { "2 x 2  square", 0, 0 },
    { "4 x 4  square", 0, 0 },
    { "8 x 8  square", 0, 0 },
};
static const MacMenu menus[] = {
    { "@", apple_items, 2 },
    { "File", file_items, 6 },
    { "Edit", edit_items, 4 },
    { "Goodies", goodies_items, 9 },
};

static uint8_t doc[DOC_H * DOC_ROW];
static uint8_t undo_buf[DOC_H * DOC_ROW];
static uint8_t vis[DOC_H][DOC_W];
static int can_undo;
static int tool = T_PENCIL;
enum { INK_COPY, INK_OR, INK_ERASE };

static int pat_i = 5; /* black */
static int brush = 8;
static int fatbits, grid = 1;
static int scroll_x, scroll_y, fat_x, fat_y;
static int dragging, shape_on, drag_x0, drag_y0, drag_x1, drag_y1, last_x, last_y;
static int pencil_white, pan_x, pan_y, pan_sx, pan_sy;
static unsigned rng = 1;
static MacRect about_r;
static int show_about;
static int hover_tool = -1;
static float hover_age;
static int hover_pat = -1;
static float hover_pat_age;

static MacRect canvas_r(void)
{
    return mac_rect(TOOL_W, MAC_MENU_H, MAC_W, MAC_H - PAT_H);
}

static int pat_ink(MacPat p, int x, int y)
{
    return (p.row[y & 7] >> (7 - (x & 7))) & 1;
}

static MacPat cur_pat(void)
{
    return pats[pat_i];
}

static int doc_get(int x, int y)
{
    uint8_t m;

    if (x < 0 || y < 0 || x >= DOC_W || y >= DOC_H)
        return 0;
    m = (uint8_t)(1u << (7 - (x & 7)));
    return (doc[y * DOC_ROW + (x >> 3)] & m) ? 1 : 0;
}

static void doc_put(int x, int y, int black)
{
    uint8_t m;
    int i;

    if (x < 0 || y < 0 || x >= DOC_W || y >= DOC_H)
        return;
    i = y * DOC_ROW + (x >> 3);
    m = (uint8_t)(1u << (7 - (x & 7)));
    if (black)
        doc[i] |= m;
    else
        doc[i] &= (uint8_t)~m;
}

static int pat_is_black(MacPat p)
{
    int i;

    for (i = 0; i < 8; i++)
        if (p.row[i] != 0xFF)
            return 0;
    return 1;
}

static void doc_ink(int x, int y, MacPat p, int mode)
{
    int bit = pat_is_black(p) ? 1 : pat_ink(p, x, y);

    if (mode == INK_ERASE)
        doc_put(x, y, 0);
    else if (mode == INK_OR) {
        if (bit)
            doc_put(x, y, 1);
    } else
        doc_put(x, y, bit);
}

static void doc_plot(int x, int y, MacPat p, int erase)
{
    doc_ink(x, y, p, erase ? INK_ERASE : INK_COPY);
}

static void snapshot(void)
{
    memcpy(undo_buf, doc, sizeof doc);
    can_undo = 1;
}

static void do_undo(void)
{
    uint8_t tmp[DOC_H * DOC_ROW];

    if (!can_undo)
        return;
    memcpy(tmp, doc, sizeof doc);
    memcpy(doc, undo_buf, sizeof doc);
    memcpy(undo_buf, tmp, sizeof doc);
}

static void doc_clear(void)
{
    memset(doc, 0, sizeof doc);
}

static int pen_sz(void)
{
    return brush < 1 ? 1 : brush;
}

static void stamp(int cx, int cy, int size, MacPat p, int mode)
{
    int x, y, r;

    if (size < 1)
        size = 1;
    r = size / 2;
    for (y = cy - r; y < cy - r + size; y++)
        for (x = cx - r; x < cx - r + size; x++)
            doc_ink(x, y, p, mode);
}

static void doc_line(int xa, int ya, int xb, int yb, int size, MacPat p, int mode)
{
    int dx = abs(xb - xa), sx = xa < xb ? 1 : -1;
    int dy = -abs(yb - ya), sy = ya < yb ? 1 : -1;
    int err = dx + dy, e2;

    for (;;) {
        stamp(xa, ya, size, p, mode);
        if (xa == xb && ya == yb)
            break;
        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            xa += sx;
        }
        if (e2 <= dx) {
            err += dx;
            ya += sy;
        }
    }
}

static void doc_fill_rect(MacRect r, MacPat p)
{
    int x, y;

    if (r.left > r.right) {
        int t = r.left;
        r.left = r.right;
        r.right = t;
    }
    if (r.top > r.bottom) {
        int t = r.top;
        r.top = r.bottom;
        r.bottom = t;
    }
    for (y = r.top; y < r.bottom; y++)
        for (x = r.left; x < r.right; x++)
            doc_plot(x, y, p, 0);
}

static void doc_frame_rect(MacRect r, MacPat p)
{
    int x, y;

    if (r.left > r.right) {
        int t = r.left;
        r.left = r.right;
        r.right = t;
    }
    if (r.top > r.bottom) {
        int t = r.top;
        r.top = r.bottom;
        r.bottom = t;
    }
    if (r.right - r.left < 1 || r.bottom - r.top < 1)
        return;
    for (x = r.left; x < r.right; x++) {
        stamp(x, r.top, pen_sz(), p, INK_COPY);
        stamp(x, r.bottom - 1, pen_sz(), p, INK_COPY);
    }
    for (y = r.top; y < r.bottom; y++) {
        stamp(r.left, y, pen_sz(), p, INK_COPY);
        stamp(r.right - 1, y, pen_sz(), p, INK_COPY);
    }
}

static void doc_oval(MacRect r, MacPat p, int fill)
{
    int y, w, h;
    double rx, ry, cx, cy;

    if (r.left > r.right) {
        int t = r.left;
        r.left = r.right;
        r.right = t;
    }
    if (r.top > r.bottom) {
        int t = r.top;
        r.top = r.bottom;
        r.bottom = t;
    }
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
        if (fill) {
            int x;
            for (x = x0; x <= x1; x++)
                doc_plot(x, y, p, 0);
        } else {
            stamp(x0, y, pen_sz(), p, INK_COPY);
            stamp(x1, y, pen_sz(), p, INK_COPY);
        }
    }
}

static int in_rr(int x, int y, MacRect r, int rad)
{
    int x0, x1, y0, y1, cx, cy;
    double dx, dy;

    if (x < r.left || x >= r.right || y < r.top || y >= r.bottom)
        return 0;
    if (rad < 1)
        return 1;
    x0 = r.left + rad;
    x1 = r.right - 1 - rad;
    y0 = r.top + rad;
    y1 = r.bottom - 1 - rad;
    if (x1 < x0)
        x1 = x0;
    if (y1 < y0)
        y1 = y0;
    if (x >= x0 && x <= x1)
        return 1;
    if (y >= y0 && y <= y1)
        return 1;
    cx = x < x0 ? r.left + rad : r.right - 1 - rad;
    cy = y < y0 ? r.top + rad : r.bottom - 1 - rad;
    dx = (x + 0.5) - (cx + 0.5);
    dy = (y + 0.5) - (cy + 0.5);
    return dx * dx + dy * dy <= (double)rad * rad;
}

static void doc_rrect(MacRect r, MacPat p, int fill)
{
    int x, y, rad, w, h;
    MacRect inner;

    if (r.left > r.right) {
        int t = r.left;
        r.left = r.right;
        r.right = t;
    }
    if (r.top > r.bottom) {
        int t = r.top;
        r.top = r.bottom;
        r.bottom = t;
    }
    w = r.right - r.left;
    h = r.bottom - r.top;
    rad = (w < h ? w : h) / 4;
    if (rad < 3)
        rad = 3;
    inner = mac_rect(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1);
    for (y = r.top; y < r.bottom; y++)
        for (x = r.left; x < r.right; x++)
            if (in_rr(x, y, r, rad) && (fill || !in_rr(x, y, inner, rad > 0 ? rad - 1 : 0))) {
                if (fill)
                    doc_plot(x, y, p, 0);
                else
                    stamp(x, y, pen_sz(), p, INK_COPY);
            }
}

static void flood(int sx, int sy, MacPat p)
{
    int target, *stk, sp = 0, cap = 8192;

    if (sx < 0 || sy < 0 || sx >= DOC_W || sy >= DOC_H)
        return;
    target = doc_get(sx, sy);
    stk = (int *)malloc((size_t)cap * 2 * sizeof(int));
    if (!stk)
        return;
    memset(vis, 0, sizeof vis);
    stk[sp++] = sx;
    stk[sp++] = sy;
    while (sp >= 2) {
        int y = stk[--sp];
        int x = stk[--sp];
        int l, r, i;

        if (x < 0 || y < 0 || x >= DOC_W || y >= DOC_H || vis[y][x])
            continue;
        if (doc_get(x, y) != target)
            continue;
        l = x;
        while (l > 0 && !vis[y][l - 1] && doc_get(l - 1, y) == target)
            l--;
        r = x;
        while (r + 1 < DOC_W && !vis[y][r + 1] && doc_get(r + 1, y) == target)
            r++;
        for (i = l; i <= r; i++) {
            vis[y][i] = 1;
            doc_plot(i, y, p, 0);
            if (y > 0 && !vis[y - 1][i] && doc_get(i, y - 1) == target) {
                if (sp + 2 > cap * 2) {
                    cap *= 2;
                    stk = (int *)realloc(stk, (size_t)cap * 2 * sizeof(int));
                    if (!stk)
                        return;
                }
                stk[sp++] = i;
                stk[sp++] = y - 1;
            }
            if (y + 1 < DOC_H && !vis[y + 1][i] && doc_get(i, y + 1) == target) {
                if (sp + 2 > cap * 2) {
                    cap *= 2;
                    stk = (int *)realloc(stk, (size_t)cap * 2 * sizeof(int));
                    if (!stk)
                        return;
                }
                stk[sp++] = i;
                stk[sp++] = y + 1;
            }
        }
    }
    free(stk);
}

static int rnd(int n)
{
    rng = rng * 1103515245u + 12345u;
    return (int)((rng >> 16) % (unsigned)(n < 1 ? 1 : n));
}

static void spray_at(int cx, int cy, MacPat p)
{
    int i, rad = brush * 4 + 4, n = 18 + brush * 8;

    for (i = 0; i < n; i++) {
        int a = rnd(rad * 2 + 1) - rad;
        int b = rnd(rad * 2 + 1) - rad;
        if (a * a + b * b <= rad * rad)
            doc_ink(cx + a, cy + b, p, INK_OR);
    }
}

static void clamp_view(void)
{
    MacRect c = canvas_r();
    int vw = mac_rect_w(c), vh = mac_rect_h(c);
    int max_x, max_y, fw, fh;

    if (fatbits) {
        fw = vw / ZOOM;
        fh = vh / ZOOM;
        if (fw < 1)
            fw = 1;
        if (fh < 1)
            fh = 1;
        max_x = DOC_W - fw;
        max_y = DOC_H - fh;
        if (max_x < 0)
            max_x = 0;
        if (max_y < 0)
            max_y = 0;
        if (fat_x < 0)
            fat_x = 0;
        if (fat_y < 0)
            fat_y = 0;
        if (fat_x > max_x)
            fat_x = max_x;
        if (fat_y > max_y)
            fat_y = max_y;
    } else {
        max_x = DOC_W - vw;
        max_y = DOC_H - vh;
        if (max_x < 0)
            max_x = 0;
        if (max_y < 0)
            max_y = 0;
        if (scroll_x < 0)
            scroll_x = 0;
        if (scroll_y < 0)
            scroll_y = 0;
        if (scroll_x > max_x)
            scroll_x = max_x;
        if (scroll_y > max_y)
            scroll_y = max_y;
    }
}

static int screen_to_doc(int mx, int my, int *ox, int *oy)
{
    MacRect c = canvas_r();

    if (!mac_in_rect(mx, my, c) && !dragging)
        return 0;
    if (fatbits) {
        *ox = fat_x + (mx - c.left) / ZOOM;
        *oy = fat_y + (my - c.top) / ZOOM;
    } else {
        *ox = scroll_x + (mx - c.left);
        *oy = scroll_y + (my - c.top);
    }
    if (*ox < 0)
        *ox = 0;
    if (*oy < 0)
        *oy = 0;
    if (*ox >= DOC_W)
        *ox = DOC_W - 1;
    if (*oy >= DOC_H)
        *oy = DOC_H - 1;
    return 1;
}

static void enter_fat(int x, int y)
{
    MacRect c = canvas_r();

    fatbits = 1;
    fat_x = x - mac_rect_w(c) / (ZOOM * 2);
    fat_y = y - mac_rect_h(c) / (ZOOM * 2);
    clamp_view();
}

static MacRect drag_rect(void)
{
    return mac_rect(drag_x0, drag_y0, drag_x1 + 1, drag_y1 + 1);
}

static void commit_shape(void)
{
    MacPat p = cur_pat();
    MacRect r = drag_rect();

    switch (tool) {
    case T_LINE:
        doc_line(drag_x0, drag_y0, drag_x1, drag_y1, pen_sz(), pats[5], INK_COPY);
        break;
    case T_RECT:
        doc_frame_rect(r, pats[5]);
        break;
    case T_RECTF:
        doc_fill_rect(r, p);
        break;
    case T_OVAL:
        doc_oval(r, pats[5], 0);
        break;
    case T_OVALF:
        doc_oval(r, p, 1);
        break;
    case T_RRECT:
        doc_rrect(r, pats[5], 0);
        break;
    case T_RRECTF:
        doc_rrect(r, p, 1);
        break;
    default:
        break;
    }
}

static int is_shape(int t)
{
    return t == T_LINE || t == T_RECT || t == T_RECTF || t == T_OVAL || t == T_OVALF ||
           t == T_RRECT || t == T_RRECTF;
}

static int save_pbm(const char *path)
{
    FILE *f = fopen(path, "wb");

    if (!f)
        return 0;
    fprintf(f, "P4\n%d %d\n", DOC_W, DOC_H);
    fwrite(doc, 1, sizeof doc, f);
    fclose(f);
    return 1;
}

static int load_pbm(const char *path)
{
    FILE *f = fopen(path, "rb");
    char magic[8];
    int w = 0, h = 0, c;
    size_t n;

    if (!f)
        return 0;
    if (fscanf(f, "%7s", magic) != 1 || strcmp(magic, "P4") != 0) {
        fclose(f);
        return 0;
    }
    c = fgetc(f);
    while (c == '#') {
        while (c != '\n' && c != EOF)
            c = fgetc(f);
        c = fgetc(f);
    }
    ungetc(c, f);
    if (fscanf(f, "%d %d", &w, &h) != 2 || w != DOC_W || h != DOC_H) {
        fclose(f);
        return 0;
    }
    fgetc(f);
    snapshot();
    n = fread(doc, 1, sizeof doc, f);
    fclose(f);
    return n == sizeof doc;
}

static int export_h(const char *path)
{
    FILE *f = fopen(path, "w");
    int i;

    if (!f)
        return 0;
    fprintf(f, "/* 512x342 1-bit, bit 7 = left. mac_copy_bits(paint_bits, 64, 0, 0, 512, 342, x, y, MAC_COPY); */\n");
    fprintf(f, "enum { PAINT_W = 512, PAINT_H = 342, PAINT_ROW = 64 };\n");
    fprintf(f, "static const unsigned char paint_bits[] = {\n");
    for (i = 0; i < (int)sizeof doc; i++) {
        if (i % 16 == 0)
            fprintf(f, "    ");
        fprintf(f, "0x%02X%s", doc[i], i + 1 == (int)sizeof doc ? "" : ",");
        if (i % 16 == 15 || i + 1 == (int)sizeof doc)
            fprintf(f, "\n");
    }
    fprintf(f, "};\n");
    fclose(f);
    return 1;
}

static void draw_tool_icon(int t, int x, int y)
{
    mac_pen_pat(mac_black);
    mac_pen_mode(MAC_COPY);
    mac_pen_size(1, 1);
    switch (t) {
    case T_PENCIL:
        mac_line(x + 3, y + 12, x + 12, y + 3);
        mac_frame_rect(mac_rect(x + 10, y + 2, x + 14, y + 6));
        break;
    case T_ERASER:
        mac_frame_rect(mac_rect(x + 3, y + 5, x + 13, y + 12));
        mac_fill_rect(mac_rect(x + 4, y + 6, x + 8, y + 11), mac_gray);
        break;
    case T_BRUSH:
        mac_fill_rect(mac_rect(x + 6, y + 2, x + 10, y + 8), mac_black);
        mac_fill_rect(mac_rect(x + 4, y + 8, x + 12, y + 13), mac_dk_gray);
        break;
    case T_SPRAY:
        mac_pixel(x + 8, y + 4, 1);
        mac_pixel(x + 5, y + 6, 1);
        mac_pixel(x + 11, y + 6, 1);
        mac_pixel(x + 7, y + 8, 1);
        mac_pixel(x + 10, y + 9, 1);
        mac_pixel(x + 4, y + 10, 1);
        mac_pixel(x + 12, y + 11, 1);
        break;
    case T_FILL:
        mac_frame_rect(mac_rect(x + 4, y + 6, x + 12, y + 13));
        mac_line(x + 6, y + 6, x + 8, y + 3);
        mac_fill_rect(mac_rect(x + 5, y + 9, x + 11, y + 12), mac_gray);
        break;
    case T_LINE:
        mac_line(x + 3, y + 12, x + 13, y + 3);
        break;
    case T_RECT:
        mac_frame_rect(mac_rect(x + 3, y + 4, x + 13, y + 12));
        break;
    case T_RECTF:
        mac_fill_rect(mac_rect(x + 3, y + 4, x + 13, y + 12), mac_gray);
        mac_frame_rect(mac_rect(x + 3, y + 4, x + 13, y + 12));
        break;
    case T_OVAL:
        mac_frame_oval(mac_rect(x + 3, y + 4, x + 13, y + 13));
        break;
    case T_OVALF:
        mac_fill_oval(mac_rect(x + 3, y + 4, x + 13, y + 13), mac_gray);
        mac_frame_oval(mac_rect(x + 3, y + 4, x + 13, y + 13));
        break;
    case T_RRECT:
        mac_frame_round_rect(mac_rect(x + 3, y + 4, x + 13, y + 12), 8, 8);
        break;
    case T_RRECTF:
        mac_fill_round_rect(mac_rect(x + 3, y + 4, x + 13, y + 12), 8, 8, mac_gray);
        break;
    case T_HAND:
        mac_frame_rect(mac_rect(x + 6, y + 4, x + 10, y + 12));
        mac_line(x + 6, y + 6, x + 4, y + 8);
        mac_line(x + 10, y + 6, x + 12, y + 8);
        break;
    case T_FAT:
        mac_frame_rect(mac_rect(x + 3, y + 3, x + 8, y + 8));
        mac_frame_rect(mac_rect(x + 7, y + 7, x + 13, y + 13));
        break;
    default:
        break;
    }
}

static int tool_at(int mx, int my)
{
    int t;

    if (mx < 0 || mx >= TOOL_W || my < MAC_MENU_H || my >= MAC_H - PAT_H)
        return -1;
    t = (my - MAC_MENU_H) / CELL * 2 + mx / CELL;
    if (t < 0 || t >= T_COUNT)
        return -1;
    return t;
}

static void draw_tooltip(int t)
{
    const char *s;
    int row, tw, w, h, x, y;
    MacRect r;

    if (t < 0 || t >= T_COUNT)
        return;
    if (t == T_BRUSH) {
        static char brush_tip[40];

        snprintf(brush_tip, sizeof brush_tip, "Brush  %d x %d", brush, brush);
        s = brush_tip;
    } else if (t == T_ERASER) {
        static char erase_tip[40];
        int e = brush < 2 ? 2 : brush;

        snprintf(erase_tip, sizeof erase_tip, "Eraser  %d x %d", e, e);
        s = erase_tip;
    } else if (t == T_LINE) {
        static char line_tip[40];

        snprintf(line_tip, sizeof line_tip, "Line  %d px", brush);
        s = line_tip;
    } else
        s = tool_name[t];
    tw = mac_text_width(s);
    w = tw + 10;
    h = 14;
    row = t / 2;
    x = TOOL_W + 4;
    y = MAC_MENU_H + row * CELL + (CELL - h) / 2;
    if (y < MAC_MENU_H)
        y = MAC_MENU_H;
    if (y + h > MAC_H - PAT_H)
        y = MAC_H - PAT_H - h;
    if (x + w > MAC_W - 2)
        x = MAC_W - 2 - w;
    r = mac_rect(x, y, x + w, y + h);
    mac_fill_rect(r, mac_white);
    mac_frame_rect(r);
    mac_fill_rect(mac_rect(x - 3, y + h / 2 - 2, x + 1, y + h / 2 + 3), mac_white);
    mac_line(x - 3, y + h / 2 - 2, x, y + h / 2 - 2);
    mac_line(x - 3, y + h / 2 - 2, x - 3, y + h / 2 + 2);
    mac_line(x - 3, y + h / 2 + 2, x, y + h / 2 + 2);
    mac_text(x + 5, y + 3, s);
}

static void draw_pat_tip(int i)
{
    const char *s;
    int tw, w, h, x, y;
    MacRect r;

    if (i < 0 || i >= N_PATS)
        return;
    s = pat_name[i];
    tw = mac_text_width(s);
    w = tw + 10;
    h = 14;
    x = PAT_WELL + 4;
    y = MAC_H - PAT_H - h - 2;
    if (y < MAC_MENU_H)
        y = MAC_MENU_H;
    if (x + w > MAC_W - 2)
        x = MAC_W - 2 - w;
    r = mac_rect(x, y, x + w, y + h);
    mac_fill_rect(r, mac_white);
    mac_frame_rect(r);
    mac_text(x + 5, y + 3, s);
}

static void draw_tools(void)
{
    int i;

    mac_fill_rect(mac_rect(0, MAC_MENU_H, TOOL_W, MAC_H - PAT_H), mac_white);
    for (i = 0; i < T_COUNT; i++) {
        int col = i % 2, row = i / 2;
        int x = col * CELL, y = MAC_MENU_H + row * CELL;
        MacRect cell = mac_rect(x, y, x + CELL, y + CELL);

        mac_frame_rect(cell);
        draw_tool_icon(i, x, y);
        if (i == tool || (i == T_FAT && fatbits))
            mac_invert_rect(mac_rect(x + 1, y + 1, x + CELL - 1, y + CELL - 1));
    }
}

static void draw_pats(void)
{
    int i, cols = 8, well = PAT_WELL;
    int pw = (MAC_W - well) / cols, ph = PAT_H / 2;
    MacRect well_r = mac_rect(0, MAC_H - PAT_H, well, MAC_H);

    mac_fill_rect(well_r, cur_pat());
    mac_frame_rect(well_r);
    mac_frame_rect(mac_rect(1, MAC_H - PAT_H + 1, well - 1, MAC_H - 1));
    mac_pen_pat(mac_white);
    mac_pen_mode(MAC_COPY);
    mac_frame_rect(mac_rect(2, MAC_H - PAT_H + 2, well - 2, MAC_H - 2));
    mac_pen_pat(mac_black);

    for (i = 0; i < N_PATS; i++) {
        int col = i % cols, row = i / cols;
        int x = well + col * pw, y = MAC_H - PAT_H + row * ph;
        MacRect sw = mac_rect(x, y, x + pw, y + ph);

        mac_fill_rect(sw, pats[i]);
        mac_frame_rect(sw);
        if (i == pat_i) {
            mac_frame_rect(mac_rect(x + 1, y + 1, x + pw - 1, y + ph - 1));
            mac_pen_pat(mac_white);
            mac_frame_rect(mac_rect(x + 2, y + 2, x + pw - 2, y + ph - 2));
            mac_pen_pat(mac_black);
            mac_frame_rect(mac_rect(x + 3, y + 3, x + pw - 3, y + ph - 3));
        }
    }
}

static int pat_at(int mx, int my)
{
    int cols = 8, well = PAT_WELL;
    int pw, ph, i;

    if (my < MAC_H - PAT_H)
        return -1;
    if (mx < well)
        return pat_i;
    pw = (MAC_W - well) / cols;
    ph = PAT_H / 2;
    i = (my - (MAC_H - PAT_H)) / ph * cols + (mx - well) / pw;
    if (i < 0 || i >= N_PATS)
        return -1;
    return i;
}

static void doc_to_screen(int dx, int dy, int *sx, int *sy)
{
    MacRect c = canvas_r();

    if (fatbits) {
        *sx = c.left + (dx - fat_x) * ZOOM;
        *sy = c.top + (dy - fat_y) * ZOOM;
    } else {
        *sx = c.left + (dx - scroll_x);
        *sy = c.top + (dy - scroll_y);
    }
}

static void draw_preview(void)
{
    int ax, ay, bx, by;
    MacRect r, c = canvas_r();

    if (!dragging || !shape_on || !is_shape(tool) || !mac_mouse_down())
        return;
    mac_clip(c);
    mac_pen_pat(mac_black);
    mac_pen_mode(MAC_XOR);
    {
        int pw = fatbits ? pen_sz() * ZOOM : pen_sz();

        mac_pen_size(pw, pw);
    }
    doc_to_screen(drag_x0, drag_y0, &ax, &ay);
    doc_to_screen(drag_x1, drag_y1, &bx, &by);
    if (fatbits && tool != T_LINE) {
        bx += ZOOM - 1;
        by += ZOOM - 1;
    }
    r = mac_rect(ax, ay, bx + 1, by + 1);
    switch (tool) {
    case T_LINE:
        mac_line(ax, ay, bx, by);
        break;
    case T_RECT:
    case T_RECTF:
        mac_frame_rect(r);
        break;
    case T_OVAL:
    case T_OVALF:
        mac_frame_oval(r);
        break;
    case T_RRECT:
    case T_RRECTF:
        mac_frame_round_rect(r, 16, 16);
        break;
    default:
        break;
    }
    mac_pen_mode(MAC_COPY);
    mac_pen_size(1, 1);
    mac_clip_reset();
}

static void draw_doc(void)
{
    MacRect c = canvas_r();
    int vw = mac_rect_w(c), vh = mac_rect_h(c);

    mac_fill_rect(c, mac_white);
    if (fatbits) {
        int x, y, fw = vw / ZOOM, fh = vh / ZOOM;

        for (y = 0; y < fh; y++) {
            for (x = 0; x < fw; x++) {
                int dx = fat_x + x, dy = fat_y + y;
                int sx = c.left + x * ZOOM, sy = c.top + y * ZOOM;
                MacRect cell = mac_rect(sx, sy, sx + ZOOM, sy + ZOOM);

                mac_fill_rect(cell, doc_get(dx, dy) ? mac_black : mac_white);
                if (grid)
                    mac_frame_rect(cell);
            }
        }
    } else {
        int w = vw, h = vh;

        if (scroll_x + w > DOC_W)
            w = DOC_W - scroll_x;
        if (scroll_y + h > DOC_H)
            h = DOC_H - scroll_y;
        if (w > 0 && h > 0)
            mac_copy_bits(doc, DOC_ROW, scroll_x, scroll_y, w, h, c.left, c.top, MAC_COPY);
    }
    mac_frame_rect(c);
}

static void sync_menus(void)
{
    int i;
    const int sizes[] = { 1, 2, 4, 8 };

    edit_items[0].flags = can_undo ? 0 : MAC_MENU_OFF;
    goodies_items[0].flags = fatbits ? MAC_MENU_CHECK : 0;
    goodies_items[1].flags = grid ? MAC_MENU_CHECK : 0;
    goodies_items[2].flags = mac_crt_on() ? MAC_MENU_CHECK : 0;
    for (i = 0; i < 4; i++)
        goodies_items[5 + i].flags = (brush == sizes[i]) ? MAC_MENU_CHECK : 0;
}

static void handle_menus(int mi, int it)
{
    if (mi == 0 && it == 0)
        show_about = 1;
    if (mi == 1 && it == 0) {
        snapshot();
        doc_clear();
    }
    if (mi == 1 && it == 1) {
        if (!load_pbm("painting.pbm"))
            mac_beep(180, 80);
    }
    if (mi == 1 && it == 2) {
        if (!save_pbm("painting.pbm"))
            mac_beep(180, 80);
        else
            mac_beep(880, 40);
    }
    if (mi == 1 && it == 3) {
        if (!export_h("painting.h"))
            mac_beep(180, 80);
        else
            mac_beep(880, 40);
    }
    if (mi == 1 && it == 5)
        mac_quit();
    if (mi == 2 && it == 0)
        do_undo();
    if (mi == 2 && it == 2) {
        snapshot();
        {
            int i;
            for (i = 0; i < (int)sizeof doc; i++)
                doc[i] = (uint8_t)~doc[i];
        }
    }
    if (mi == 2 && it == 3) {
        snapshot();
        doc_fill_rect(mac_rect(0, 0, DOC_W, DOC_H), cur_pat());
    }
    if (mi == 3 && it == 0) {
        if (fatbits)
            fatbits = 0;
        else
            enter_fat(scroll_x + 80, scroll_y + 60);
    }
    if (mi == 3 && it == 1)
        grid = !grid;
    if (mi == 3 && it == 2)
        mac_crt(!mac_crt_on());
    if (mi == 3 && it >= 5 && it <= 8) {
        const int sizes[] = { 1, 2, 4, 8 };
        brush = sizes[it - 5];
    }
}

static void paint_at(int x, int y, int first)
{
    MacPat p = cur_pat();

    switch (tool) {
    case T_PENCIL:
        if (first)
            pencil_white = doc_get(x, y);
        if (first)
            doc_put(x, y, pencil_white ? 0 : 1);
        else
            doc_line(last_x, last_y, x, y, 1, pats[5], pencil_white ? INK_ERASE : INK_COPY);
        break;
    case T_ERASER:
        stamp(x, y, brush < 2 ? 2 : brush, p, INK_ERASE);
        if (!first)
            doc_line(last_x, last_y, x, y, brush < 2 ? 2 : brush, p, INK_ERASE);
        break;
    case T_BRUSH:
        stamp(x, y, brush, p, pat_is_black(p) ? INK_COPY : INK_OR);
        if (!first)
            doc_line(last_x, last_y, x, y, brush, p, pat_is_black(p) ? INK_COPY : INK_OR);
        break;
    case T_SPRAY:
        spray_at(x, y, p);
        break;
    default:
        break;
    }
    last_x = x;
    last_y = y;
}

static void tick(float dt)
{
    int mx, my, dx, dy, mi, it, t;
    MacRect c = canvas_r();
    int space;

    mx = mac_mouse_x();
    my = mac_mouse_y();
    space = mac_key_down(KEY_SPACE);
    t = tool_at(mx, my);
    if (t == hover_tool && t >= 0 && !dragging && !mac_menu_tracking() && !mac_mouse_down())
        hover_age += dt;
    else {
        hover_tool = t;
        hover_age = 0;
    }
    {
        int p = pat_at(mx, my);

        if (p == hover_pat && p >= 0 && !dragging && !mac_menu_tracking() && !mac_mouse_down())
            hover_pat_age += dt;
        else {
            hover_pat = p;
            hover_pat_age = 0;
        }
    }

    if (mac_key_down(KEY_LEFT)) {
        if (fatbits)
            fat_x -= 2;
        else
            scroll_x -= 2;
    }
    if (mac_key_down(KEY_RIGHT)) {
        if (fatbits)
            fat_x += 2;
        else
            scroll_x += 2;
    }
    if (mac_key_down(KEY_UP)) {
        if (fatbits)
            fat_y -= 2;
        else
            scroll_y -= 2;
    }
    if (mac_key_down(KEY_DOWN)) {
        if (fatbits)
            fat_y += 2;
        else
            scroll_y += 2;
    }
    clamp_view();

    if (!mac_menu_tracking() && !show_about && mac_mouse_pressed()) {
        if (t >= 0) {
            dragging = 0;
            shape_on = 0;
            if (t == T_FAT) {
                if (fatbits)
                    fatbits = 0;
                else
                    enter_fat(scroll_x + mac_rect_w(c) / 2, scroll_y + mac_rect_h(c) / 2);
            } else {
                tool = t;
            }
        } else if (my >= MAC_H - PAT_H) {
            int i = pat_at(mx, my);

            dragging = 0;
            shape_on = 0;
            if (i >= 0)
                pat_i = i;
        } else if (mac_in_rect(mx, my, c) && screen_to_doc(mx, my, &dx, &dy)) {
            if (mac_mouse_double() && tool == T_PENCIL && !fatbits) {
                enter_fat(dx, dy);
            } else if (tool == T_HAND || space) {
                dragging = 1;
                shape_on = 0;
                pan_x = mx;
                pan_y = my;
                pan_sx = fatbits ? fat_x : scroll_x;
                pan_sy = fatbits ? fat_y : scroll_y;
            } else if (tool == T_FILL) {
                snapshot();
                flood(dx, dy, cur_pat());
            } else if (is_shape(tool)) {
                snapshot();
                dragging = 1;
                shape_on = 1;
                drag_x0 = drag_x1 = dx;
                drag_y0 = drag_y1 = dy;
                last_x = dx;
                last_y = dy;
            } else {
                snapshot();
                dragging = 1;
                shape_on = 0;
                paint_at(dx, dy, 1);
            }
        }
    }

    if (dragging && (tool == T_HAND || space) && mac_mouse_down()) {
        int ox = pan_sx - (mx - pan_x);
        int oy = pan_sy - (my - pan_y);
        if (fatbits) {
            fat_x = ox;
            fat_y = oy;
        } else {
            scroll_x = ox;
            scroll_y = oy;
        }
        clamp_view();
    } else if (dragging && mac_mouse_down() && screen_to_doc(mx, my, &dx, &dy)) {
        if (shape_on) {
            drag_x1 = dx;
            drag_y1 = dy;
        }         else if (tool != T_HAND)
            paint_at(dx, dy, 0);
    }

    if (dragging && !mac_mouse_down() && !mac_mouse_released()) {
        dragging = 0;
        shape_on = 0;
    }

    if (dragging && mac_mouse_released()) {
        if (shape_on && is_shape(tool))
            commit_shape();
        dragging = 0;
        shape_on = 0;
    }

    if (show_about)
        mac_window_drag(&about_r);

    mac_fill_rect(mac_rect(0, 0, MAC_W, MAC_H), mac_white);
    draw_doc();
    draw_preview();
    draw_tools();
    draw_pats();
    if (!show_about && !mac_menu_tracking() && hover_tool >= 0 && hover_age > 0.35f)
        draw_tooltip(hover_tool);
    if (!show_about && !mac_menu_tracking() && hover_pat >= 0 && hover_pat_age > 0.35f)
        draw_pat_tip(hover_pat);

    if (show_about) {
        mac_window(about_r, "About", 1);
        mac_text(about_r.left + 12, about_r.top + 32, "1-bit MacPaint studio.");
        mac_text(about_r.left + 12, about_r.top + 44, "Document is 512x342.");
        mac_text(about_r.left + 12, about_r.top + 56, "FatBits: pixel work.");
        mac_text(about_r.left + 12, about_r.top + 68, "Save painting.pbm");
        mac_text(about_r.left + 12, about_r.top + 80, "or export painting.h.");
        mac_text(about_r.left + 12, about_r.top + 92, "Space+drag pans.");
        mac_text(about_r.left + 12, about_r.top + 104, "Goodies: CRT Blend.");
        if (mac_window_hit(about_r, mx, my) == MAC_HIT_CLOSE && mac_mouse_released())
            show_about = 0;
    }

    sync_menus();
    if (mac_menus(menus, 4, &mi, &it))
        handle_menus(mi, it);

    if (mac_in_rect(mx, my, c) && !mac_menu_tracking())
        mac_cursor(tool == T_HAND || space ? MAC_CUR_ARROW : MAC_CUR_CROSS);
    else
        mac_cursor(MAC_CUR_ARROW);
    mac_swap();
}

int main(void)
{
    about_r = mac_rect(118, 70, 394, 220);
    doc_clear();
    mac_crt(0);
    return mac_main(tick);
}
