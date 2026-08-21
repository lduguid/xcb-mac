#include "mac_internal.h"

#include <string.h>

enum { SND_RATE = 44100 };

static volatile int snd_on = 1;
static volatile int req_hz, req_samples, req_gen;
static int play_gen, play_left, play_phase, play_hz, play_noise;
static unsigned lfsr = 1;

void mac_snd_init(void)
{
    snd_on = 1;
    req_hz = req_samples = req_gen = 0;
    play_gen = play_left = play_phase = play_hz = play_noise = 0;
    lfsr = 1;
}

void mac_snd_shutdown(void)
{
    play_left = 0;
}

int mac_snd_rate(void)
{
    return SND_RATE;
}

void mac_sound(int on)
{
    snd_on = on ? 1 : 0;
    if (!snd_on)
        play_left = 0;
}

int mac_sound_on(void)
{
    return snd_on;
}

void mac_beep(int freq_hz, int ms)
{
    if (ms < 1)
        ms = 1;
    if (ms > 2000)
        ms = 2000;
    if (freq_hz < 0)
        freq_hz = 0;
    if (freq_hz > 8000)
        freq_hz = 8000;
    req_hz = freq_hz;
    req_samples = ms * SND_RATE / 1000;
    req_gen++;
}

void mac_mix(short *out, int frames)
{
    int i, amp, period, bit;

    if (req_gen != play_gen) {
        play_hz = req_hz;
        play_left = req_samples;
        play_phase = 0;
        play_noise = play_hz <= 0;
        play_gen = req_gen;
        if (play_noise)
            lfsr = 0xACE1u;
    }

    memset(out, 0, (size_t)frames * sizeof(*out));
    if (!snd_on || play_left <= 0)
        return;

    amp = 22000;
    period = play_noise ? 2 : SND_RATE / (play_hz < 20 ? 20 : play_hz);
    if (period < 2)
        period = 2;

    for (i = 0; i < frames && play_left > 0; i++, play_left--, play_phase++) {
        if (play_noise) {
            bit = lfsr & 1;
            lfsr = (lfsr >> 1) ^ (bit ? 0xB400 : 0);
            out[i] = (short)(bit ? amp : -amp);
        } else {
            out[i] = (short)((play_phase % period) < (period / 2) ? amp : -amp);
        }
    }
}
