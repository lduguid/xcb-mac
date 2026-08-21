/* Finder-style desktop: ordered dither vs hatch / cross-hatch.
 * Mouse is the interface: drag windows, close box, press-and-hold menus,
 * buttons that cancel if you drag off. */

#include "mac.h"

#include <stdio.h>

static const MacMenuItem apple_items[] = {
    { "About Patterns...", 0, 0 },
    { "-", MAC_MENU_SEP, 0 },
    { "Help", MAC_MENU_OFF, 0 },
};
static MacMenuItem file_items[] = {
    { "New", 0, 'N' },
    { "Open...", MAC_MENU_OFF, 'O' },
    { "-", MAC_MENU_SEP, 0 },
    { "Quit", 0, 'Q' },
};
static const MacMenuItem edit_items[] = {
    { "Undo", MAC_MENU_OFF, 'Z' },
    { "-", MAC_MENU_SEP, 0 },
    { "Cut", MAC_MENU_OFF, 'X' },
    { "Copy", MAC_MENU_OFF, 'C' },
    { "Paste", MAC_MENU_OFF, 'V' },
};
static MacMenuItem view_items[] = {
    { "CRT Blend", MAC_MENU_CHECK, 'B' },
};
static const MacMenu menus[] = {
    { "@", apple_items, 3 },
    { "File", file_items, 4 },
    { "Edit", edit_items, 5 },
    { "View", view_items, 1 },
};

static MacRect win_r;
static MacRect about_r;
static int clicks;
static int show_about;
static int win_open = 1;

static MacRect place_ok(MacRect win)
{
    MacRect c = mac_window_content(win);
    int cx = (c.left + c.right) / 2;
    return mac_rect(cx - 42, c.bottom - 28, cx + 42, c.bottom - 8);
}

static void swatch(int x, int y, MacPat pat, const char *name)
{
    MacRect sw = mac_rect(x, y, x + 68, y + 36);
    mac_fill_rect(sw, pat);
    mac_frame_rect(sw);
    mac_text(x + 2, y + 40, name);
}

static void tick(float dt)
{
    int mx, my, mi, it, press, ok;
    MacRect c, btn, about_ok;
    char line[56];
    int i, x0, just_closed = 0;
    const MacPat *greys[] = { &mac_white, &mac_lt_gray, &mac_gray, &mac_dk_gray, &mac_black };
    const char *grey_n[] = { "White", "LtGray", "Gray", "DkGray", "Black" };
    const MacPat *hatches[] = { &mac_hatch, &mac_hatch_v, &mac_hatch_d, &mac_x_hatch, &mac_grid };
    const char *hatch_n[] = { "Hatch", "Vert", "Diag", "XHatch", "Grid" };

    (void)dt;
    mx = mac_mouse_x();
    my = mac_mouse_y();

    if (!mac_menu_tracking() && win_open &&
        mac_window_hit(win_r, mx, my) == MAC_HIT_CLOSE && mac_mouse_released()) {
        win_open = 0;
        just_closed = 1;
    }

    if (!win_open && !just_closed && !mac_menu_tracking() && my >= MAC_MENU_H &&
        mac_mouse_released())
        win_open = 1;

    if (show_about)
        mac_window_drag(&about_r);
    else if (win_open)
        mac_window_drag(&win_r);

    btn = place_ok(win_r);
    about_ok = place_ok(about_r);

    ok = 0;
    if (show_about)
        ok = mac_track_button(about_ok, &press);
    else if (win_open)
        ok = mac_track_button(btn, &press);
    else
        press = 0;
    if (ok) {
        if (show_about)
            show_about = 0;
        else
            clicks++;
    }

    mac_desk();
    if (!win_open)
        mac_text(16, MAC_H - 16, "Click the desktop to open the window.");

    if (win_open) {
        mac_window(win_r, "About Patterns", 1);
        c = mac_window_content(win_r);
        mac_clip(c);

        mac_pen_mode(MAC_COPY);
        mac_pen_pat(mac_black);
        mac_pen_size(1, 1);
        mac_text(c.left + 12, c.top + 8, "Drag the striped title bar to move this window.");

        x0 = c.left + 16;
        for (i = 0; i < 5; i++)
            swatch(x0 + i * 80, c.top + 24, *greys[i], grey_n[i]);
        for (i = 0; i < 5; i++)
            swatch(x0 + i * 80, c.top + 80, *hatches[i], hatch_n[i]);

        mac_fill_oval(mac_rect(c.left + 24, c.top + 132, c.left + 112, c.top + 188), mac_hatch_d);
        mac_frame_oval(mac_rect(c.left + 24, c.top + 132, c.left + 112, c.top + 188));
        mac_fill_oval(mac_rect(c.left + 140, c.top + 132, c.left + 228, c.top + 188), mac_x_hatch);
        mac_frame_oval(mac_rect(c.left + 140, c.top + 132, c.left + 228, c.top + 188));
        mac_fill_oval(mac_rect(c.left + 256, c.top + 132, c.left + 344, c.top + 188), mac_grid);
        mac_frame_oval(mac_rect(c.left + 256, c.top + 132, c.left + 344, c.top + 188));
        mac_text(c.left + 24, c.top + 196, "Cross-hatch reads as stone; checker reads as shade.");

        snprintf(line, sizeof(line), "OK clicks: %d", clicks);
        mac_text(c.left + 12, c.top + 212, line);
        mac_clip_reset();
        mac_button(btn, "OK", press && !show_about, 1);
    }

    if (show_about) {
        MacRect inner;

        mac_window(about_r, "About", 0);
        inner = mac_window_content(about_r);
        mac_clip(inner);
        mac_text(inner.left + 16, inner.top + 16, "xcb-mac  ·  512 x 342  ·  1 bit");
        mac_text(inner.left + 16, inner.top + 32, "The mouse is the whole interface.");
        mac_clip_reset();
        mac_button(about_ok, "OK", press && show_about, 1);
    }

    view_items[0].flags = mac_crt_on() ? MAC_MENU_CHECK : 0;
    if (mac_menus(menus, 4, &mi, &it)) {
        if (mi == 0 && it == 0)
            show_about = 1;
        if (mi == 1 && it == 0)
            win_open = 1;
        if (mi == 1 && it == 3)
            mac_quit();
        if (mi == 3 && it == 0)
            mac_crt(!mac_crt_on());
    }
    mac_cursor(MAC_CUR_ARROW);
    mac_swap();
}

int main(void)
{
    win_r = mac_rect(36, 32, 476, 308);
    about_r = mac_rect(110, 90, 400, 210);
    return mac_main(tick);
}
