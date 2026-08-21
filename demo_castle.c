/* Dark Castle title homage. Drawn with QuickDraw-style primitives only:
 * checker sky, white moon oval, black cloud/castle masses, outlined gothic type. */

#include "mac.h"

static MacMenuItem apple_items[] = {
    { "About Splash...", 0, 0 },
    { "-", MAC_MENU_SEP, 0 },
};
static MacMenuItem file_items[] = {
    { "Quit", 0, 'Q' },
};
static MacMenuItem view_items[] = {
    { "CRT Blend", MAC_MENU_CHECK, 'B' },
};
static const MacMenu menus[] = {
    { "@", apple_items, 2 },
    { "File", file_items, 1 },
    { "View", view_items, 1 },
};

static MacRect about_r;
static int show_about;
static float storm;
static int boom;

static void fr(int x, int y, int w, int h, MacPat p)
{
    if (w < 1 || h < 1)
        return;
    mac_fill_rect(mac_rect(x, y, x + w, y + h), p);
}

static void fo(int x, int y, int w, int h, MacPat p)
{
    if (w < 1 || h < 1)
        return;
    mac_fill_oval(mac_rect(x, y, x + w, y + h), p);
}

static void stroke(int x0, int y0, int x1, int y1, int w, MacPat p)
{
    if (w < 1)
        w = 1;
    mac_pen_pat(p);
    mac_pen_mode(MAC_COPY);
    mac_pen_size(w, w);
    mac_line(x0, y0, x1, y1);
    mac_pen_size(1, 1);
}

static void blob(char ch, int x, int y, int s, int pad, MacPat p)
{
    int t = 2 * s - pad;
    int h = 12 * s - 2 * pad;
    int stem = (5 * s) / 3;

    if (h < 3)
        return;
    x += pad;
    y += pad;
    if (t < 2)
        t = 2;

    switch (ch) {
    case 'D':
        fr(x, y, stem, h, p);
        fo(x + s / 2, y, 7 * s - pad, h, p);
        break;
    case 'A': {
        int i, mid = x + 4 * s - pad;
        for (i = 0; i < h; i++) {
            int half = (4 * s * i) / (12 * s) + s / 3;
            fr(mid - half, y + i, half * 2, 1, p);
        }
        fr(x + s, y + (7 * s) - pad, 6 * s, (3 * s) / 2, p);
        break;
    }
    case 'R':
        fr(x, y, stem, h, p);
        fo(x + s / 3, y, 6 * s, 7 * s - pad, p);
        stroke(x + 3 * s, y + 6 * s, x + 8 * s - pad, y + h - s / 2, t, p);
        break;
    case 'K':
        fr(x, y, stem, h, p);
        stroke(x + stem - 2, y + 6 * s, x + 8 * s - pad, y + pad, t, p);
        stroke(x + stem - 2, y + 6 * s, x + 8 * s - pad, y + h - pad, t, p);
        break;
    case 'C':
        fo(x, y, 8 * s - pad, h, p);
        break;
    case 'S':
        fo(x, y, 8 * s - pad, 7 * s, p);
        fo(x, y + 5 * s - pad, 8 * s - pad, 7 * s, p);
        break;
    case 'T':
        fr(x, y, 8 * s - 2 * pad, stem, p);
        fr(x + 3 * s - pad, y, stem, h, p);
        break;
    case 'L':
        fr(x, y, stem, h, p);
        fr(x, y + h - stem, 7 * s - pad, stem, p);
        break;
    case 'E':
        fr(x, y, stem, h, p);
        fr(x, y, 7 * s - pad, stem, p);
        fr(x, y + (5 * s) - pad / 2, 6 * s - pad, (s > 2 ? s : 2), p);
        fr(x, y + h - stem, 7 * s - pad, stem, p);
        break;
    default:
        break;
    }
}

static void hole(char ch, int x, int y, int s, int grow, MacPat p)
{
    switch (ch) {
    case 'D':
        fo(x + 2 * s - grow, y + 2 * s - grow, 4 * s + 2 * grow, 8 * s + 2 * grow, p);
        break;
    case 'A':
        fo(x + 3 * s - grow, y + 2 * s - grow, 2 * s + 2 * grow, 4 * s + grow, p);
        break;
    case 'R':
        fo(x + 2 * s - grow, y + 2 * s - grow, 3 * s + 2 * grow, 4 * s + grow, p);
        break;
    case 'C':
        fo(x + s - grow, y + 2 * s - grow, 6 * s + 2 * grow, 8 * s + 2 * grow, p);
        fr(x + 5 * s, y + 3 * s - grow, 4 * s, 6 * s + 2 * grow, p);
        break;
    case 'S':
        fo(x + s - grow, y + s - grow, 6 * s + 2 * grow, 5 * s + grow, p);
        fo(x + s - grow, y + 6 * s - grow, 6 * s + 2 * grow, 5 * s + grow, p);
        fr(x + 5 * s, y + 2 * s, 4 * s, 2 * s, p);
        fr(x - s, y + 8 * s, 4 * s, 2 * s, p);
        break;
    default:
        break;
    }
}

static int has_hole(char ch)
{
    return ch == 'D' || ch == 'A' || ch == 'R' || ch == 'C' || ch == 'S';
}

static void letter(char ch, int x, int y, int s, MacPat sky)
{
    blob(ch, x, y, s, -3, mac_white);
    blob(ch, x, y, s, -2, mac_black);
    blob(ch, x, y, s, 0, mac_white);
    if (has_hole(ch)) {
        hole(ch, x, y, s, 3, mac_black);
        hole(ch, x, y, s, 0, sky);
    }
}

static int word_w(const char *w, int s)
{
    int n = 0;
    while (w[n])
        n++;
    if (n < 1)
        return 0;
    return n * 10 * s - 2 * s;
}

static void draw_word(const char *w, int x, int y, int s, MacPat sky)
{
    for (; *w; w++, x += 10 * s)
        letter(*w, x, y, s, sky);
}

static void merlons(int x, int y, int n, int step, int bw, int bh)
{
    int i;
    for (i = 0; i < n; i++)
        fr(x + i * step, y, bw, bh, mac_black);
}

static void tower(int x, int y, int w, int h)
{
    int i;

    fr(x, y, w, h, mac_black);
    for (i = 0; i < w / 2; i++)
        fr(x + i, y - 8 - i, w - 2 * i, 2, mac_black);
    merlons(x - 2, y - 10, w / 10 + 1, 10, 6, 12);
}

static void window_lit(int x, int y)
{
    fr(x, y, 5, 11, mac_white);
    fr(x + 1, y + 1, 3, 4, mac_lt_gray);
}

static void castle(MacPat sky)
{
    int i;

    tower(8, 96, 48, 170);
    fr(0, 168, 96, 174, mac_black);
    fr(70, 150, 36, 80, mac_black);
    merlons(70, 140, 4, 10, 6, 12);
    fr(48, 200, 70, 142, mac_black);
    fo(86, 228, 28, 42, sky);
    fr(92, 248, 16, 94, sky);

    window_lit(18, 118);
    window_lit(36, 118);
    window_lit(26, 142);
    window_lit(14, 188);
    window_lit(34, 188);
    window_lit(54, 210);
    window_lit(78, 178);

    for (i = 0; i < 7; i++)
        fo(-10 + i * 8, 250 + (i % 3) * 12, 28, 22, mac_black);
}

static void cloud(int x, int y, int w)
{
    int i, n = w / 24;
    if (n < 3)
        n = 3;
    for (i = 0; i < n; i++) {
        int cx = x + (i * (w - 36)) / (n - 1);
        int bump = (i % 3 == 1) ? -8 : (i % 3 == 2) ? -3 : 0;
        fo(cx, y - 8 + bump, 40, 20, mac_white);
        fo(cx + 4, y - 10 + bump, 22, 12, mac_lt_gray);
    }
    for (i = 0; i < n; i++) {
        int cx = x + (i * (w - 36)) / (n - 1);
        int bump = (i % 3 == 1) ? -6 : 0;
        fo(cx - 2, y + 2 + bump, 44, 24, mac_black);
    }
    fr(x + 16, y + 10, w - 32, 18, mac_black);
}

static void tree(void)
{
    stroke(498, 338, 456, 168, 9, mac_black);
    stroke(456, 168, 390, 128, 6, mac_black);
    stroke(456, 172, 508, 108, 5, mac_black);
    stroke(430, 148, 368, 162, 4, mac_black);
    stroke(470, 210, 420, 188, 5, mac_black);
    stroke(488, 260, 440, 230, 6, mac_black);
    fo(448, 160, 28, 22, mac_black);
    fo(500, 118, 22, 16, mac_black);
    fo(378, 154, 18, 14, mac_black);
    fo(430, 224, 20, 16, mac_black);
    fo(490, 300, 36, 28, mac_black);
}

static void foliage(void)
{
    int i;
    const int clumps[][4] = {
        { 0, -8, 90, 40 }, { 40, 4, 70, 28 }, { 200, -6, 80, 26 },
        { 300, 0, 60, 22 }, { 420, -10, 100, 36 }, { 480, 8, 50, 24 },
        { 360, 250, 70, 50 }, { 410, 270, 90, 60 }, { 460, 230, 70, 50 },
        { 380, 300, 80, 50 }, { 300, 310, 70, 40 }, { 0, 300, 80, 50 },
    };

    for (i = 0; i < 6; i++)
        fo(clumps[i][0], clumps[i][1], clumps[i][2], clumps[i][3], mac_black);

    for (i = 6; i < 12; i++) {
        fo(clumps[i][0], clumps[i][1], clumps[i][2], clumps[i][3], mac_black);
        fo(clumps[i][0] + 10, clumps[i][1] + 8, clumps[i][2] - 16, clumps[i][3] - 10, mac_dk_gray);
    }
    fo(400, 292, 50, 28, mac_gray);
    fo(448, 308, 40, 22, mac_dk_gray);

    stroke(6, 236, 22, 228, 2, mac_white);
    stroke(18, 248, 34, 238, 2, mac_lt_gray);
    stroke(40, 242, 52, 234, 2, mac_white);
}

static void splash(int flash)
{
    MacPat sky = flash ? mac_white : mac_gray;
    int s = 5;
    int y0 = 112;

    mac_fill_rect(mac_rect(0, 0, MAC_W, MAC_H), sky);
    fo(292, 18, 104, 104, mac_white);
    if (!flash)
        fo(304, 30, 28, 22, mac_lt_gray);

    cloud(70, 52, 280);
    cloud(250, 78, 200);
    cloud(40, 88, 160);

    castle(sky);
    tree();
    foliage();

    draw_word("DARK", (MAC_W - word_w("DARK", s)) / 2, y0, s, sky);
    draw_word("CASTLE", (MAC_W - word_w("CASTLE", s)) / 2, y0 + 12 * s + 6, s, sky);
}

static void tick(float dt)
{
    int mi, it, flash = 0;

    storm += dt;
    if (storm > 5.2f) {
        if (storm < 5.32f)
            flash = 1;
        else if (storm < 5.40f)
            flash = 0;
        else if (storm < 5.62f)
            flash = 1;
        else {
            storm = 0;
            boom = 0;
        }
        if (flash && !boom) {
            mac_beep(0, 140);
            boom = 1;
        }
    }

    splash(flash);

    if (show_about) {
        mac_window_drag(&about_r);
        mac_window(about_r, "About", 1);
        mac_text(about_r.left + 16, about_r.top + 36, "1-bit QuickDraw homage.");
        mac_text(about_r.left + 16, about_r.top + 48, "Sky is patGray. Moon is");
        mac_text(about_r.left + 16, about_r.top + 60, "a white oval. The rest is");
        mac_text(about_r.left + 16, about_r.top + 72, "rects, ovals, and strokes.");
        if (mac_window_hit(about_r, mac_mouse_x(), mac_mouse_y()) == MAC_HIT_CLOSE &&
            mac_mouse_released())
            show_about = 0;
    }

    view_items[0].flags = mac_crt_on() ? MAC_MENU_CHECK : 0;
    if (mac_menus(menus, 3, &mi, &it)) {
        if (mi == 0 && it == 0)
            show_about = 1;
        if (mi == 1 && it == 0)
            mac_quit();
        if (mi == 2 && it == 0)
            mac_crt(!mac_crt_on());
    }
    mac_cursor(MAC_CUR_ARROW);
    mac_swap();
}

int main(void)
{
    about_r = mac_rect(120, 90, 392, 210);
    return mac_main(tick);
}
