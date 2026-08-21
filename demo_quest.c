/* Mouse-aim collect-em-up in a System 1 window. Grey floor is 50% dither. */

#include "mac.h"

#include <stdio.h>
#include <stdlib.h>

#define ROCKS 5
#define BITS 8

static MacMenuItem apple_items[] = {
    { "About Spark...", 0, 0 },
    { "-", MAC_MENU_SEP, 0 },
};
static MacMenuItem file_items[] = {
    { "New Game", 0, 'N' },
    { "-", MAC_MENU_SEP, 0 },
    { "Quit", 0, 'Q' },
};
static MacMenuItem game_items[] = {
    { "Pause", 0, 'P' },
    { "Sound", MAC_MENU_CHECK, 'S' },
    { "CRT Blend", MAC_MENU_CHECK, 'B' },
};
static const MacMenu menus[] = {
    { "@", apple_items, 2 },
    { "File", file_items, 3 },
    { "Game", game_items, 3 },
};

static MacRect win_r;
static MacRect about_r;
static int show_about;
static int paused;
static float px, py, pvx, pvy;
static float rx[ROCKS], ry[ROCKS], rvx[ROCKS], rvy[ROCKS];
static float bx[BITS], by[BITS];
static int b_on[BITS];
static int score, lives, dead;
static float hit_flash;
static int greeted;

static int sound_enabled(void)
{
    return (game_items[1].flags & MAC_MENU_CHECK) != 0;
}

static void spark_beep(int hz, int ms)
{
    if (sound_enabled())
        mac_beep(hz, ms);
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static float dist2(float ax, float ay, float bx, float by)
{
    float dx = ax - bx, dy = ay - by;
    return dx * dx + dy * dy;
}

static void place_bit(int i, MacRect c)
{
    bx[i] = (float)(c.left + 20 + (rand() % (mac_rect_w(c) - 40)));
    by[i] = (float)(c.top + 20 + (rand() % (mac_rect_h(c) - 40)));
    b_on[i] = 1;
}

static void reset_play(MacRect c)
{
    int i;

    px = (float)(c.left + mac_rect_w(c) / 2);
    py = (float)(c.top + mac_rect_h(c) / 2);
    pvx = pvy = 0;
    dead = 0;
    hit_flash = 0;
    for (i = 0; i < ROCKS; i++) {
        rx[i] = (float)(c.left + 30 + rand() % (mac_rect_w(c) - 60));
        ry[i] = (float)(c.top + 30 + rand() % (mac_rect_h(c) - 60));
        rvx[i] = (float)(40 + rand() % 50) * (rand() & 1 ? 1.0f : -1.0f);
        rvy[i] = (float)(30 + rand() % 50) * (rand() & 1 ? 1.0f : -1.0f);
    }
    for (i = 0; i < BITS; i++)
        place_bit(i, c);
}

static MacRect place_ok(MacRect win)
{
    MacRect c = mac_window_content(win);
    int cx = (c.left + c.right) / 2;
    return mac_rect(cx - 42, c.bottom - 28, cx + 42, c.bottom - 8);
}

static void shift_world(int dx, int dy)
{
    int i;

    if (!dx && !dy)
        return;
    px += (float)dx;
    py += (float)dy;
    for (i = 0; i < ROCKS; i++) {
        rx[i] += (float)dx;
        ry[i] += (float)dy;
    }
    for (i = 0; i < BITS; i++) {
        bx[i] += (float)dx;
        by[i] += (float)dy;
    }
}

static void tick(float dt)
{
    MacRect c;
    int i, mx, my, remain, mi, it;
    char title[40];
    float acc = 520.0f;
    int play;

    c = mac_window_content(win_r);
    mx = mac_mouse_x();
    my = mac_mouse_y();
    play = !paused && !show_about && !mac_menu_tracking();

    if (!greeted) {
        greeted = 1;
        spark_beep(880, 120);
    }

    if (!mac_menu_tracking() && mac_window_hit(win_r, mx, my) == MAC_HIT_CLOSE && mac_mouse_released())
        mac_quit();

    {
        int ox = win_r.left, oy = win_r.top;
        if (show_about)
            mac_window_drag(&about_r);
        else
            mac_window_drag(&win_r);
        shift_world(win_r.left - ox, win_r.top - oy);
        c = mac_window_content(win_r);
    }

    if (play && !dead) {
        if (mac_window_hit(win_r, mx, my) == MAC_HIT_CONTENT) {
            pvx += ((float)mx - px) * 6.0f * dt;
            pvy += ((float)my - py) * 6.0f * dt;
        }
        if (mac_key_down(KEY_LEFT))
            pvx -= acc * dt;
        if (mac_key_down(KEY_RIGHT))
            pvx += acc * dt;
        if (mac_key_down(KEY_UP))
            pvy -= acc * dt;
        if (mac_key_down(KEY_DOWN))
            pvy += acc * dt;
        pvx *= 0.90f;
        pvy *= 0.90f;
        px = clampf(px + pvx * dt, (float)(c.left + 10), (float)(c.right - 10));
        py = clampf(py + pvy * dt, (float)(c.top + 10), (float)(c.bottom - 10));
    } else if (play && dead && mac_window_hit(win_r, mx, my) == MAC_HIT_CONTENT &&
               (mac_key_pressed(KEY_SPACE) || mac_mouse_pressed())) {
        if (lives > 0) {
            reset_play(c);
            spark_beep(660, 80);
        }
    }

    if (play) {
        for (i = 0; i < ROCKS; i++) {
            rx[i] += rvx[i] * dt;
            ry[i] += rvy[i] * dt;
            if (rx[i] < (float)(c.left + 16) || rx[i] > (float)(c.right - 16))
                rvx[i] = -rvx[i];
            if (ry[i] < (float)(c.top + 16) || ry[i] > (float)(c.bottom - 16))
                rvy[i] = -rvy[i];
            rx[i] = clampf(rx[i], (float)(c.left + 16), (float)(c.right - 16));
            ry[i] = clampf(ry[i], (float)(c.top + 16), (float)(c.bottom - 16));
            if (!dead && dist2(px, py, rx[i], ry[i]) < 18.0f * 18.0f) {
                dead = 1;
                lives--;
                hit_flash = 0.35f;
                spark_beep(0, 180);
            }
        }

        remain = 0;
        for (i = 0; i < BITS; i++) {
            if (!b_on[i])
                continue;
            remain++;
            if (!dead && dist2(px, py, bx[i], by[i]) < 14.0f * 14.0f) {
                b_on[i] = 0;
                score += 10;
                spark_beep(1320, 45);
            }
        }
        if (!dead && remain == 0) {
            score += 50;
            spark_beep(880, 120);
            reset_play(c);
        }
    }

    if (hit_flash > 0.0f)
        hit_flash -= dt;

    snprintf(title, sizeof(title), "Spark   %d   lives %d", score, lives < 0 ? 0 : lives);

    mac_desk();
    mac_window(win_r, title, 1);

    c = mac_window_content(win_r);
    mac_clip(c);
    mac_pen_mode(MAC_COPY);
    mac_fill_rect(c, mac_gray);

    for (i = 0; i < BITS; i++) {
        if (!b_on[i])
            continue;
        mac_fill_oval(mac_rect((int)bx[i] - 3, (int)by[i] - 3, (int)bx[i] + 4, (int)by[i] + 4), mac_white);
        mac_frame_oval(mac_rect((int)bx[i] - 3, (int)by[i] - 3, (int)bx[i] + 4, (int)by[i] + 4));
    }
    for (i = 0; i < ROCKS; i++) {
        mac_fill_oval(mac_rect((int)rx[i] - 12, (int)ry[i] - 10, (int)rx[i] + 12, (int)ry[i] + 10), mac_dk_gray);
        mac_frame_oval(mac_rect((int)rx[i] - 12, (int)ry[i] - 10, (int)rx[i] + 12, (int)ry[i] + 10));
    }
    if (!dead || ((int)(hit_flash * 20.0f) & 1)) {
        mac_fill_oval(mac_rect((int)px - 7, (int)py - 7, (int)px + 8, (int)py + 8), mac_white);
        mac_frame_oval(mac_rect((int)px - 7, (int)py - 7, (int)px + 8, (int)py + 8));
        mac_fill_oval(mac_rect((int)px - 2, (int)py - 2, (int)px + 3, (int)py + 3), mac_black);
    }
    if (dead) {
        mac_fill_rect(mac_rect(c.left + 80, c.top + 90, c.right - 80, c.top + 140), mac_white);
        mac_frame_rect(mac_rect(c.left + 80, c.top + 90, c.right - 80, c.top + 140));
        mac_text(c.left + 120, c.top + 108, lives > 0 ? "Click or space to continue" : "Game over  ·  Esc to quit");
    }
    if (paused && !dead) {
        mac_fill_rect(mac_rect(c.left + 140, c.top + 100, c.right - 140, c.top + 128), mac_white);
        mac_frame_rect(mac_rect(c.left + 140, c.top + 100, c.right - 140, c.top + 128));
        mac_text(c.left + 188, c.top + 108, "Paused");
    }
    mac_clip_reset();

    if (show_about) {
        MacRect inner, about_ok;
        int press = 0;

        about_ok = place_ok(about_r);
        if (mac_track_button(about_ok, &press))
            show_about = 0;
        mac_window(about_r, "About Spark", 0);
        inner = mac_window_content(about_r);
        mac_clip(inner);
        mac_text(inner.left + 16, inner.top + 14, "The mouse is the whole game.");
        mac_text(inner.left + 16, inner.top + 30, "Point in the window to steer.");
        mac_clip_reset();
        mac_button(about_ok, "OK", press, 1);
    }

    if (mac_menus(menus, 3, &mi, &it)) {
        if (mi == 0 && it == 0)
            show_about = 1;
        if (mi == 1 && it == 0) {
            lives = 3;
            score = 0;
            paused = 0;
            reset_play(mac_window_content(win_r));
            spark_beep(523, 90);
        }
        if (mi == 1 && it == 2)
            mac_quit();
        if (mi == 2 && it == 0)
            paused = !paused;
        if (mi == 2 && it == 1) {
            if (game_items[1].flags & MAC_MENU_CHECK)
                game_items[1].flags &= ~MAC_MENU_CHECK;
            else
                game_items[1].flags |= MAC_MENU_CHECK;
            mac_sound(sound_enabled());
            if (sound_enabled())
                mac_beep(880, 80);
        }
        if (mi == 2 && it == 2) {
            if (game_items[2].flags & MAC_MENU_CHECK)
                game_items[2].flags &= ~MAC_MENU_CHECK;
            else
                game_items[2].flags |= MAC_MENU_CHECK;
            mac_crt((game_items[2].flags & MAC_MENU_CHECK) != 0);
        }
    }
    if (mac_menu_tracking())
        mac_cursor(MAC_CUR_ARROW);
    else if (paused || show_about)
        mac_cursor(MAC_CUR_WATCH);
    else if (mac_window_hit(win_r, mx, my) == MAC_HIT_CONTENT)
        mac_cursor(MAC_CUR_CROSS);
    else
        mac_cursor(MAC_CUR_ARROW);
    mac_swap();
}

int main(void)
{
    MacRect c;

    win_r = mac_rect(24, 28, 488, 320);
    about_r = mac_rect(120, 100, 392, 220);
    c = mac_window_content(win_r);
    lives = 3;
    score = 0;
    reset_play(c);
    mac_sound(1);
    return mac_main(tick);
}
