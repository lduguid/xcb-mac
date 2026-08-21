#define WIN32_LEAN_AND_MEAN

#include "mac_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <mmsystem.h>

enum { SCALE = 2, BEZEL = 48 };

static HWND hwnd;
static int running = 1;
static uint32_t *dib;
static int dib_w, dib_h;
static int os_owns;
static int last_hx = BEZEL + 8, last_hy = BEZEL + 8;
static HRESULT(WINAPI *dwm_flush)(void);
static HDC memdc;
static HBITMAP membmp;
static HGDIOBJ memold;
static int mem_w, mem_h;
static HWAVEOUT wo;
static HANDLE snd_ev, snd_th;
static WAVEHDR snd_hdr[4];
static short snd_pcm[4][1024];
static volatile int audio_run;

static DWORD WINAPI audio_thread(LPVOID arg)
{
    (void)arg;
    while (audio_run) {
        int i;
        WaitForSingleObject(snd_ev, 80);
        if (!audio_run)
            break;
        for (i = 0; i < 4; i++) {
            if (snd_hdr[i].dwFlags & WHDR_DONE) {
                mac_mix(snd_pcm[i], 1024);
                waveOutWrite(wo, &snd_hdr[i], sizeof(WAVEHDR));
            }
        }
    }
    return 0;
}

static void audio_start(void)
{
    WAVEFORMATEX fmt;
    int i;

    wo = NULL;
    audio_run = 0;
    snd_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!snd_ev)
        return;
    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = mac_snd_rate();
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 2;
    fmt.nAvgBytesPerSec = (DWORD)(mac_snd_rate() * 2);
    if (waveOutOpen(&wo, WAVE_MAPPER, &fmt, (DWORD_PTR)snd_ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(snd_ev);
        snd_ev = NULL;
        return;
    }
    memset(snd_hdr, 0, sizeof(snd_hdr));
    for (i = 0; i < 4; i++) {
        snd_hdr[i].lpData = (LPSTR)snd_pcm[i];
        snd_hdr[i].dwBufferLength = 1024 * sizeof(short);
        waveOutPrepareHeader(wo, &snd_hdr[i], sizeof(WAVEHDR));
        mac_mix(snd_pcm[i], 1024);
        waveOutWrite(wo, &snd_hdr[i], sizeof(WAVEHDR));
    }
    audio_run = 1;
    snd_th = CreateThread(NULL, 0, audio_thread, NULL, 0, NULL);
    if (!snd_th)
        audio_run = 0;
}

static void audio_stop(void)
{
    int i;

    if (audio_run) {
        audio_run = 0;
        if (snd_ev)
            SetEvent(snd_ev);
        if (snd_th) {
            WaitForSingleObject(snd_th, 1000);
            CloseHandle(snd_th);
            snd_th = NULL;
        }
    }
    if (wo) {
        waveOutReset(wo);
        for (i = 0; i < 4; i++)
            waveOutUnprepareHeader(wo, &snd_hdr[i], sizeof(WAVEHDR));
        waveOutClose(wo);
        wo = NULL;
    }
    if (snd_ev) {
        CloseHandle(snd_ev);
        snd_ev = NULL;
    }
}

static int map_vk(WPARAM vk)
{
    if (vk == VK_LEFT || vk == 'A')
        return KEY_LEFT;
    if (vk == VK_RIGHT || vk == 'D')
        return KEY_RIGHT;
    if (vk == VK_UP || vk == 'W')
        return KEY_UP;
    if (vk == VK_DOWN || vk == 'S')
        return KEY_DOWN;
    if (vk == VK_ESCAPE)
        return KEY_ESC;
    if (vk == 'Q')
        return KEY_Q;
    if (vk == VK_SPACE)
        return KEY_SPACE;
    if (vk == VK_RETURN)
        return KEY_ENTER;
    return 0;
}

static int in_crt(int hx, int hy)
{
    return hx >= BEZEL && hy >= BEZEL && hx < BEZEL + MAC_W * SCALE && hy < BEZEL + MAC_H * SCALE;
}

static void host_mouse(int hx, int hy)
{
    last_hx = hx;
    last_hy = hy;
    mac_mouse_set((hx - BEZEL) / SCALE, (hy - BEZEL) / SCALE);
    mac_host_pointer(in_crt(hx, hy), os_owns);
}

static void mouse_from_lparam(LPARAM lp)
{
    int x = (short)LOWORD(lp);
    int y = (short)HIWORD(lp);
    host_mouse(x, y);
}

static void paint_bezel(HDC hdc)
{
    RECT rc;
    HBRUSH br;
    HPEN pen, oldpen;
    HBRUSH oldbr;
    int sw = MAC_W * SCALE, sh = MAC_H * SCALE;

    GetClientRect(hwnd, &rc);
    ExcludeClipRect(hdc, BEZEL, BEZEL, BEZEL + sw, BEZEL + sh);
    br = CreateSolidBrush(RGB(0xa8, 0xa0, 0x98));
    FillRect(hdc, &rc, br);
    DeleteObject(br);
    pen = CreatePen(PS_SOLID, 1, RGB(0x1a, 0x15, 0x10));
    oldpen = (HPEN)SelectObject(hdc, pen);
    oldbr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, BEZEL - 1, BEZEL - 1, BEZEL + sw + 1, BEZEL + sh + 1);
    SelectObject(hdc, oldbr);
    SelectObject(hdc, oldpen);
    DeleteObject(pen);
    SelectClipRgn(hdc, NULL);
}

static void ensure_backbuf(HDC wnd)
{
    RECT rc;
    int w, h;

    GetClientRect(hwnd, &rc);
    w = rc.right;
    h = rc.bottom;
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    if (memdc && w == mem_w && h == mem_h)
        return;
    if (memdc) {
        SelectObject(memdc, memold);
        DeleteObject(membmp);
        DeleteDC(memdc);
        memdc = NULL;
    }
    memdc = CreateCompatibleDC(wnd);
    membmp = CreateCompatibleBitmap(wnd, w, h);
    memold = SelectObject(memdc, membmp);
    mem_w = w;
    mem_h = h;
    paint_bezel(memdc);
}

static void blit_visible(HDC hdc)
{
    BITMAPINFO bmi;
    const uint8_t *src = mac_present();
    int x, y, vw = MAC_W, vh = MAC_H;
    int sw = vw * SCALE, sh = vh * SCALE;

    for (y = 0; y < vh; y++) {
        uint32_t *dst = dib + (size_t)y * vw;
        for (x = 0; x < vw; x++)
            dst[x] = mac_host_pixel(src, x, y);
    }

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = vw;
    bmi.bmiHeader.biHeight = -vh;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(hdc, BEZEL, BEZEL, sw, sh, 0, 0, vw, vh, dib, &bmi, DIB_RGB_COLORS, SRCCOPY);
}

void mac_swap(void)
{
    HDC hdc = GetDC(hwnd);

    if (hdc) {
        ensure_backbuf(hdc);
        blit_visible(memdc);
        BitBlt(hdc, 0, 0, mem_w, mem_h, memdc, 0, 0, SRCCOPY);
        ReleaseDC(hwnd, hdc);
    }
    if (dwm_flush)
        dwm_flush();
}

static LRESULT CALLBACK wnd_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(w, &ps);
        ensure_backbuf(hdc);
        paint_bezel(memdc);
        blit_visible(memdc);
        BitBlt(hdc, 0, 0, mem_w, mem_h, memdc, 0, 0, SRCCOPY);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        if (LOWORD(lp) != HTCLIENT)
            break;
        if (os_owns || !in_crt(last_hx, last_hy))
            SetCursor(LoadCursorA(NULL, IDC_ARROW));
        else
            SetCursor(NULL);
        return TRUE;
    case WM_MOUSELEAVE:
        mac_mouse_button(0);
        mac_pointer_leave();
        return 0;
    case WM_MOUSEMOVE:
        mac_pointer_enter();
        mouse_from_lparam(lp);
        {
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = w;
            tme.dwHoverTime = 0;
            TrackMouseEvent(&tme);
        }
        return 0;
    case WM_LBUTTONDOWN:
        mac_pointer_enter();
        mouse_from_lparam(lp);
        mac_mouse_button(1);
        return 0;
    case WM_LBUTTONUP:
        mouse_from_lparam(lp);
        mac_mouse_button(0);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (lp & (1u << 30))
            return 0;
        if (wp == VK_F12) {
            os_owns = !os_owns;
            mac_host_pointer(in_crt(last_hx, last_hy), os_owns);
            SetCursor((os_owns || !in_crt(last_hx, last_hy)) ? LoadCursorA(NULL, IDC_ARROW) : NULL);
            return 0;
        }
        {
            int k = map_vk(wp);
            mac_key_set(k, 1);
            if (k == KEY_ESC || k == KEY_Q)
                running = 0;
        }
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        mac_key_set(map_vk(wp), 0);
        return 0;
    case WM_CLOSE:
        running = 0;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static double now_sec(void)
{
    static LARGE_INTEGER freq;
    LARGE_INTEGER t;

    if (!freq.QuadPart)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)freq.QuadPart;
}

int mac_main(void (*tick)(float dt))
{
    WNDCLASSEXA wc;
    RECT rc;
    HINSTANCE inst = GetModuleHandleA(NULL);
    HMODULE dwm;
    double prev;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

    if (!mac_init()) {
        fprintf(stderr, "cannot allocate framebuffer\n");
        return 1;
    }
    dib_w = MAC_W * SCALE;
    dib_h = MAC_H * SCALE;
    dib = calloc((size_t)MAC_W * (size_t)MAC_H, sizeof(*dib));
    if (!dib) {
        mac_shutdown();
        return 1;
    }

    dwm = LoadLibraryA("dwmapi.dll");
    if (dwm) {
        FARPROC p = GetProcAddress(dwm, "DwmFlush");
        memcpy(&dwm_flush, &p, sizeof(p));
    }

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wc.lpszClassName = "XcbMac";
    RegisterClassExA(&wc);

    rc.left = 0;
    rc.top = 0;
    rc.right = dib_w + BEZEL * 2;
    rc.bottom = dib_h + BEZEL * 2;
    AdjustWindowRect(&rc, style, FALSE);
    hwnd = CreateWindowExA(0, "XcbMac", "xcb-mac", style, CW_USEDEFAULT, CW_USEDEFAULT,
                           rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, inst, NULL);
    if (!hwnd) {
        fprintf(stderr, "cannot create window\n");
        return 1;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    audio_start();

    timeBeginPeriod(1);
    prev = now_sec();
    while (running) {
        MSG msg;
        double t, dt, left;

        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT)
                running = 0;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!running)
            break;

        t = now_sec();
        dt = t - prev;
        prev = t;
        if (dt < 0.0)
            dt = 0.0;
        if (dt > 0.05)
            dt = 0.05;
        tick((float)dt);
        if (mac_want_quit())
            running = 0;
        mac_keys_end_frame();

        if (!dwm_flush) {
            left = 1.0 / 60.0 - (now_sec() - t);
            if (left > 0.0)
                Sleep((DWORD)(left * 1000.0 + 0.5));
        }
    }
    audio_stop();
    timeEndPeriod(1);
    if (memdc) {
        SelectObject(memdc, memold);
        DeleteObject(membmp);
        DeleteDC(memdc);
        memdc = NULL;
    }
    free(dib);
    mac_shutdown();
    if (dwm)
        FreeLibrary(dwm);
    return 0;
}
