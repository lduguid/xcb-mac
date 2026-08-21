CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -O2 $(shell pkg-config --cflags x11 xcb xcb-dbe) \
	$(shell pkg-config --cflags libpulse-simple 2>/dev/null) \
	$(shell pkg-config --cflags alsa 2>/dev/null)
MACHINE = $(shell gcc -dumpmachine)
X11XCB = $(shell test -e /usr/lib/$(MACHINE)/libX11-xcb.so && echo -lX11-xcb || echo /usr/lib/$(MACHINE)/libX11-xcb.so.1)
LIBS = $(shell pkg-config --libs x11 xcb xcb-dbe) $(X11XCB) -lm \
	$(shell pkg-config --libs libpulse-simple 2>/dev/null) \
	$(shell pkg-config --libs alsa 2>/dev/null || echo -lasound) -lpthread

HARNESS = mac.c mac_font.c mac_snd.c plat.c mac.h mac_internal.h
.PHONY: all clean

all: xcb-mac xcb-mac-quest xcb-mac-castle xcb-mac-paint

xcb-mac: demo.c $(HARNESS)
	$(CC) $(CFLAGS) -o $@ demo.c mac.c mac_font.c mac_snd.c plat.c $(LIBS)

xcb-mac-quest: demo_quest.c $(HARNESS)
	$(CC) $(CFLAGS) -o $@ demo_quest.c mac.c mac_font.c mac_snd.c plat.c $(LIBS)

xcb-mac-castle: demo_castle.c $(HARNESS)
	$(CC) $(CFLAGS) -o $@ demo_castle.c mac.c mac_font.c mac_snd.c plat.c $(LIBS)

xcb-mac-paint: demo_paint.c $(HARNESS)
	$(CC) $(CFLAGS) -o $@ demo_paint.c mac.c mac_font.c mac_snd.c plat.c $(LIBS)

clean:
	rm -f xcb-mac xcb-mac-quest xcb-mac-castle xcb-mac-paint
