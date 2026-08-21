# xcb-mac

You are drawing on a compact Macintosh. The page is **512×342, 1 bit**: 0 is white, 1 is black. There is no grey hardware. Midtones are 8×8 dither patterns (the same trick MacPaint and Dark Castle used). You never plot RGB.

Include `mac.h`. The host scales that 1-bit page inside a bezel, composites the arrow, and (optionally) blooms neighbors so a checker reads as grey. The bitmap **persists** across frames — XOR a sprite to erase it, or erase and redraw. Call `mac_swap` when the page is ready.

Linux and Windows. Do not include X11 or Win32; that is `plat.c` / `plat_win32.c`.

## Build

```bash
make
./xcb-mac-paint
```

Windows:

```bash
make -f Makefile.win32 TAG=-mingw    # xcb-mac-paint-mingw.exe
make -f Makefile.win32 TOOLSET=msvc TAG=-msvc
```

Demos: `xcb-mac` (Finder desktop), `xcb-mac-quest`, `xcb-mac-castle`, `xcb-mac-paint`.

Host keys: **F12** lets the OS own the mouse again. CRT blend is a View-menu / `mac_crt` option.

## Your loop

```c
#include "mac.h"

static void tick(float dt)
{
    (void)dt;
    mac_desk();
    mac_window(mac_rect(40, 40, 280, 180), "Hello", 1);
    mac_text(56, 80, "It is 1986.");
    mac_swap();
}

int main(void)
{
    return mac_main(tick);
}
```

`tick` runs every frame with elapsed seconds. Right/bottom of a `MacRect` are **exclusive**, like QuickDraw.

## Pen, ink, shapes

Set a pen, then draw:

```c
mac_pen_pat(mac_gray);     /* or mac_black, mac_lt_gray, mac_hatch, … */
mac_pen_mode(MAC_COPY);    /* COPY, OR, XOR, BIC */
mac_pen_size(1, 1);
mac_move_to(10, 10);
mac_line_to(100, 80);
mac_paint_rect(mac_rect(20, 20, 80, 60));
mac_frame_oval(mac_rect(90, 20, 160, 70));
```

`mac_copy_bits` stamps a 1-bit source (bit 7 of each byte is the left pixel). White bits are transparent except in `MAC_COPY`. `mac_text` is an 8×8 System-style font.

Clip with `mac_clip` / `mac_clip_reset`. `mac_get` / `mac_pixel` poke a single bit if you must.

## Chrome

Menus, windows, and buttons are how the machine feels, not decoration.

```c
static MacMenuItem file_items[] = {
    { "Quit", 0, 'Q' },
};
static const MacMenu menus[] = {
    { "@", apple_items, 2 },
    { "File", file_items, 1 },
};

/* last in the tick, so the pulldown sits on top */
if (mac_menus(menus, 2, &which_menu, &which_item))
    /* an enabled item was chosen this frame */;
```

`mac_window`, `mac_window_drag`, `mac_window_close_hit`, `mac_track_button` implement press / drag / cancel-if-you-leave. Label `"-"` or `MAC_MENU_SEP` is a divider; `MAC_MENU_CHECK` / `MAC_MENU_OFF` are Finder ticks and dimmed items.

One mouse button: `mac_mouse_down`, `mac_mouse_pressed`, `mac_mouse_double`. Coordinates are **Mac pixels**, clamped to the 512×342 page.

## Sound

One square-wave voice, like the original speaker: `mac_beep(freq_hz, ms)` or `mac_sound(1)` / `mac_sound(0)`.

## What to steal

| Demo | Ideas |
|------|--------|
| `demo.c` | Desktop dither vs hatch, draggable windows, menus |
| `demo_quest.c` | XOR sprites, rooms, a tiny adventure |
| `demo_castle.c` | Patterned stone, a side-view set piece |
| `demo_paint.c` | Tools, FatBits, patterns as paint, `.pbm` / `.h` export |
