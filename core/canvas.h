#pragma once
/**
 * Portable drawing canvas for the synth UI: a bare 240x240 RGB565 framebuffer
 * in GC9A01 panel byte order. This is the SAME pixel format the hardware
 * driver pushes over SPI, so the PC simulator and the real screen render
 * bit-identical frames. No hardware includes allowed in this file.
 *
 * Ported 1:1 from MyProjects/04_hue (gauge + 5x7 font) and gc9a01.h (Color).
 */
#include <stdint.h>
#include <math.h>

// The drawing helpers are called from many places; inlining each call
// costs flash (the Seed has 128 KB) and buys nothing next to the SPI push.
#define SYNTHUI_NOINLINE __attribute__((noinline))

namespace synthui
{
constexpr int W = 240;
constexpr int H = 240;

/** Pack r,g,b (0..255) into RGB565, byte-swapped for the panel. */
constexpr uint16_t Color(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)((((uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3))
                       >> 8)
                      | (((uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3
                                     | b >> 3))
                         << 8));
}

constexpr uint16_t kBlack = Color(0, 0, 0);
constexpr uint16_t kTrack = Color(28, 28, 28);

inline void Fill(uint16_t* fb, uint16_t c)
{
    for(int i = 0; i < W * H; i++)
        fb[i] = c;
}

inline void Pixel(uint16_t* fb, int x, int y, uint16_t c)
{
    if((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H)
        fb[y * W + x] = c;
}

inline void FillRect(uint16_t* fb, int x0, int y0, int w, int h, uint16_t c)
{
    for(int y = y0; y < y0 + h; y++)
        for(int x = x0; x < x0 + w; x++)
            Pixel(fb, x, y, c);
}

SYNTHUI_NOINLINE inline void FillCircle(uint16_t* fb, int cx, int cy, int r, uint16_t c)
{
    for(int y = -r; y <= r; y++)
        for(int x = -r; x <= r; x++)
            if(x * x + y * y <= r * r)
                Pixel(fb, cx + x, cy + y, c);
}

/** Straight line (Bresenham), clipped per pixel. */
SYNTHUI_NOINLINE inline void DrawLine(uint16_t* fb, int x0, int y0, int x1, int y1, uint16_t c)
{
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    const int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int       err = dx + dy;
    for(;;)
    {
        Pixel(fb, x0, y0, c);
        if(x0 == x1 && y0 == y1)
            return;
        const int e2 = 2 * err;
        if(e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if(e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

/** Scale a packed (panel byte order) color's brightness by f (0..1). */
inline uint16_t Dim(uint16_t c, float f)
{
    if(f >= 1.f)
        return c;
    if(f <= 0.f)
        return kBlack;
    uint16_t v = (uint16_t)((c >> 8) | (c << 8)); // un-swap
    int      r = (int)(((v >> 11) & 0x1F) * f);
    int      g = (int)(((v >> 5) & 0x3F) * f);
    int      b = (int)((v & 0x1F) * f);
    uint16_t n = (uint16_t)((r << 11) | (g << 5) | b);
    return (uint16_t)((n >> 8) | (n << 8));
}

/** Vertical line at x from y0 to y1 (either order), clipped. */
inline void DrawVLine(uint16_t* fb, int x, int y0, int y1, uint16_t c)
{
    if(y0 > y1)
    {
        int t = y0;
        y0    = y1;
        y1    = t;
    }
    for(int y = y0; y <= y1; y++)
        Pixel(fb, x, y, c);
}

/** Hue 0..1 -> RGB565, full saturation and brightness. */
inline uint16_t HueColor(float h)
{
    float x    = h * 6.f;
    int   sect = (int)x;
    float f    = x - sect;
    float r = 0.f, g = 0.f, b = 0.f;
    switch(sect % 6)
    {
        case 0: r = 1.f;     g = f;       b = 0.f;     break;
        case 1: r = 1.f - f; g = 1.f;     b = 0.f;     break;
        case 2: r = 0.f;     g = 1.f;     b = f;       break;
        case 3: r = 0.f;     g = 1.f - f; b = 1.f;     break;
        case 4: r = f;       g = 0.f;     b = 1.f;     break;
        default: r = 1.f;    g = 0.f;     b = 1.f - f; break;
    }
    return Color((uint8_t)(r * 255.f), (uint8_t)(g * 255.f),
                 (uint8_t)(b * 255.f));
}

/* ------------------------------------------------------------------ text */
/** Classic 5x7 column font, bit 0 = top row. Extend as labels appear. */
struct Glyph
{
    char    c;
    uint8_t col[5];
};
static const Glyph kGlyphs[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'+', {0x08, 0x08, 0x3E, 0x08, 0x08}},
    {'#', {0x14, 0x7F, 0x14, 0x7F, 0x14}},
    {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
    {'u', {0x3C, 0x40, 0x40, 0x20, 0x7C}},
    {'e', {0x38, 0x54, 0x54, 0x54, 0x18}},
    {'b', {0x7F, 0x48, 0x44, 0x44, 0x38}}, // flat sign in KEY-mode degree labels
    // chord names (Am, Bdim, Dsus4, Cmaj7, Fadd9) and minor numerals (vi, ii)
    {'a', {0x20, 0x54, 0x54, 0x54, 0x78}},
    {'d', {0x38, 0x44, 0x44, 0x48, 0x7F}},
    {'i', {0x00, 0x44, 0x7D, 0x40, 0x00}},
    {'j', {0x20, 0x40, 0x44, 0x3D, 0x00}},
    {'m', {0x7C, 0x04, 0x18, 0x04, 0x78}},
    {'s', {0x48, 0x54, 0x54, 0x54, 0x20}},
    {'v', {0x1C, 0x20, 0x40, 0x20, 0x1C}},
};

SYNTHUI_NOINLINE inline void DrawChar(uint16_t* fb, int x, int y, char c, int scale,
                     uint16_t color)
{
    for(const Glyph& g : kGlyphs)
        if(g.c == c)
        {
            for(int cx = 0; cx < 5; cx++)
                for(int cy = 0; cy < 7; cy++)
                    if((g.col[cx] >> cy) & 1)
                        FillRect(fb, x + cx * scale, y + cy * scale, scale,
                                 scale, color);
            return;
        }
}

/** Draw text with its top-left at (x, y). Advance is 6 columns per char. */
SYNTHUI_NOINLINE inline void DrawText(uint16_t* fb, const char* s, int x, int y, int scale,
                     uint16_t color)
{
    for(int i = 0; s[i]; i++)
        DrawChar(fb, x + i * 6 * scale, y, s[i], scale, color);
}

/** Draw text centered on (cx, cy). Advance is 6 columns per char. */
SYNTHUI_NOINLINE inline void DrawTextCentered(uint16_t* fb, const char* s, int cx, int cy,
                             int scale, uint16_t color)
{
    int n = 0;
    while(s[n])
        n++;
    int w = (n * 6 - 1) * scale;
    int x = cx - w / 2;
    int y = cy - (7 * scale) / 2;
    for(int i = 0; i < n; i++)
        DrawChar(fb, x + i * 6 * scale, y, s[i], scale, color);
}

/* ----------------------------------------------------------------- gauge */
/** Speedometer band over the top 1/3 of the rim (120 degrees), tapering
 *  from kWMin px at the start to kWMax px at full scale. */
SYNTHUI_NOINLINE inline void DrawGauge(uint16_t* fb, float amount, uint16_t color,
                      uint16_t track = kTrack)
{
    constexpr int   kROut = 118;
    constexpr float kWMin = 4.f, kWMax = 26.f;
    constexpr float kStart = 150.f;
    constexpr float kSweep = 120.f;
    constexpr int   kRInMin = kROut - (int)kWMax;
    for(int y = 0; y <= 120 - (kRInMin / 2); y++)
        for(int x = 120 - kROut; x <= 120 + kROut; x++)
        {
            int dx = x - 120, dy = 120 - y;
            int r2 = dx * dx + dy * dy;
            if(r2 < kRInMin * kRInMin || r2 > kROut * kROut)
                continue;
            float phi = atan2f((float)dy, (float)dx) * 57.29578f;
            float t   = (kStart - phi) / kSweep;
            if(t < 0.f || t > 1.f)
                continue;
            float w = kWMin + (kWMax - kWMin) * t;
            if((float)r2 < (kROut - w) * (kROut - w))
                continue;
            Pixel(fb, x, y, t <= amount ? color : track);
        }
}

} // namespace synthui
