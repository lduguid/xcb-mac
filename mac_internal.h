#ifndef MAC_INTERNAL_H
#define MAC_INTERNAL_H

#include "mac.h"

int mac_init(void);
void mac_shutdown(void);
const uint8_t *mac_present(void); /* MAC_H * MAC_ROW, cursor composited */
uint32_t mac_host_pixel(const uint8_t *src, int x, int y);
void mac_key_set(int key, int down);
void mac_keys_end_frame(void);
void mac_mouse_set(int x, int y);
void mac_mouse_button(int down);
void mac_pointer_enter(void);
void mac_pointer_leave(void);
/* over_crt: host pointer is over the 512x342 screen (not the bezel).
 * os_owns: F12 — show the host cursor, skip the 1-bit cursor. */
void mac_host_pointer(int over_crt, int os_owns);
int mac_want_quit(void);

void mac_snd_init(void);
void mac_snd_shutdown(void);
int mac_snd_rate(void);
void mac_mix(short *out, int frames);

#endif
