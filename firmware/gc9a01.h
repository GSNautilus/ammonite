#pragma once
/**
 * Minimal GC9A01 (240x240 round TFT) driver for libDaisy.
 *
 * 4-wire SPI, RGB565, full-frame buffer. Two transports on the same pins:
 *   - HARD: STM32 SPI1 via libDaisy SpiHandle, 12.5 MHz (default for frames)
 *   - SOFT: bit-banged GPIO, ~700 kHz (used for init; also supports
 *           reading registers back from the panel over SDA)
 * Written for the GoldenMorning GMT128-02 7-pin module
 * (VCC GND SCL SDA DC CS RST, backlight hard-wired on).
 * Verified working on Daisy Seed3, 2026-09-05.
 *
 * Protocol rules that matter (each one cost a debugging round):
 *   - CS must stay low from the RAMWR command through the last pixel byte.
 *   - Bit-banged clocks need real pauses; flat-out GPIO toggling is too fast.
 *   - After a HAL SPI transfer, wait a few us before moving DC or CS.
 */
#include <initializer_list>
#include "daisy_seed.h"

namespace daisy
{
class GC9A01
{
  public:
    static constexpr int W = 240;
    static constexpr int H = 240;

    enum class Transport
    {
        SOFT,
        HARD
    };

    struct Config
    {
        SpiHandle::Config spi;
        Pin               sclk, mosi, dc, reset, cs;

        Config()
        {
            sclk  = seed::D8;  // PG11 (SPI1 SCK)
            mosi  = seed::D10; // PB5  (SPI1 MOSI)
            cs    = seed::D7;  // PG10, driven as plain GPIO
            dc    = seed::D6;  // PC12
            reset = seed::D5;  // PD2

            spi.periph         = SpiHandle::Config::Peripheral::SPI_1;
            spi.mode           = SpiHandle::Config::Mode::MASTER;
            spi.direction      = SpiHandle::Config::Direction::TWO_LINES_TX_ONLY;
            spi.datasize       = 8;
            spi.clock_polarity = SpiHandle::Config::ClockPolarity::LOW;
            spi.clock_phase    = SpiHandle::Config::ClockPhase::ONE_EDGE;
            spi.nss            = SpiHandle::Config::NSS::SOFT;
            // SPI1 kernel clock on Daisy is 25 MHz (PLL2P). PS_2 -> 12.5 MHz
            // SCK, verified clean on jumper wires. synth_main.cpp's
            // FAST_DISPLAY_SPI switches the kernel clock to PLL3P
            // (49.15 MHz) -> 24.6 MHz SCK.
            spi.baud_prescaler  = SpiHandle::Config::BaudPrescaler::PS_2;
            spi.pin_config.sclk = sclk;
            spi.pin_config.miso = Pin(PORTX, 0);
            spi.pin_config.mosi = mosi;
            spi.pin_config.nss  = Pin(PORTX, 0);
        }
    };

    /** Pack r,g,b (0..255) into RGB565, already byte-swapped for the panel. */
    static constexpr uint16_t Color(uint8_t r, uint8_t g, uint8_t b)
    {
        uint16_t c = (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
        return (uint16_t)((c >> 8) | (c << 8));
    }

    /** fb must hold W*H uint16_t.
     *  The init sequence always goes out on the slow bit-banged transport
     *  (a few ms, proven on real hardware); frames then use `t`. */
    void Init(const Config& cfg, uint16_t* fb, Transport t = Transport::HARD)
    {
        cfg_ = cfg;
        fb_  = fb;
        dc_.Init(cfg.dc, GPIO::Mode::OUTPUT);
        rst_.Init(cfg.reset, GPIO::Mode::OUTPUT);
        cs_.Init(cfg.cs, GPIO::Mode::OUTPUT);
        cs_.Write(1);
        dc_.Write(1);
        SetTransport(Transport::SOFT);
        Reinit();
        SetTransport(t);
    }

    /** Hardware reset + full init sequence on the current transport. */
    void Reinit()
    {
        // Hardware reset
        rst_.Write(1);
        System::Delay(5);
        rst_.Write(0);
        System::Delay(20);
        rst_.Write(1);
        System::Delay(120);

        // Init sequence (vendor recommended, same as Adafruit/lcdwiki).
        Cmd(0xEF);
        Cmd(0xEB, {0x14});
        Cmd(0xFE);
        Cmd(0xEF);
        Cmd(0xEB, {0x14});
        Cmd(0x84, {0x40});
        Cmd(0x85, {0xFF});
        Cmd(0x86, {0xFF});
        Cmd(0x87, {0xFF});
        Cmd(0x88, {0x0A});
        Cmd(0x89, {0x21});
        Cmd(0x8A, {0x00});
        Cmd(0x8B, {0x80});
        Cmd(0x8C, {0x01});
        Cmd(0x8D, {0x01});
        Cmd(0x8E, {0xFF});
        Cmd(0x8F, {0xFF});
        Cmd(0xB6, {0x00, 0x00});
        Cmd(0x36, {0x48}); // MADCTL: BGR order, pins-at-bottom orientation
        Cmd(0x3A, {0x05}); // COLMOD: 16 bpp
        Cmd(0x90, {0x08, 0x08, 0x08, 0x08});
        Cmd(0xBD, {0x06});
        Cmd(0xBC, {0x00});
        Cmd(0xFF, {0x60, 0x01, 0x04});
        Cmd(0xC3, {0x13});
        Cmd(0xC4, {0x13});
        Cmd(0xC9, {0x22});
        Cmd(0xBE, {0x11});
        Cmd(0xE1, {0x10, 0x0E});
        Cmd(0xDF, {0x21, 0x0C, 0x02});
        Cmd(0xF0, {0x45, 0x09, 0x08, 0x08, 0x26, 0x2A});
        Cmd(0xF1, {0x43, 0x70, 0x72, 0x36, 0x37, 0x6F});
        Cmd(0xF2, {0x45, 0x09, 0x08, 0x08, 0x26, 0x2A});
        Cmd(0xF3, {0x43, 0x70, 0x72, 0x36, 0x37, 0x6F});
        Cmd(0xED, {0x1B, 0x0B});
        Cmd(0xAE, {0x77});
        Cmd(0xCD, {0x63});
        Cmd(0x70, {0x07, 0x07, 0x04, 0x0E, 0x0F, 0x09, 0x07, 0x08, 0x03});
        Cmd(0xE8, {0x34});
        Cmd(0x62,
            {0x18, 0x0D, 0x71, 0xED, 0x70, 0x70, 0x18, 0x0F, 0x71, 0xEF, 0x70, 0x70});
        Cmd(0x63,
            {0x18, 0x11, 0x71, 0xF1, 0x70, 0x70, 0x18, 0x13, 0x71, 0xF3, 0x70, 0x70});
        Cmd(0x64, {0x28, 0x29, 0xF1, 0x01, 0xF1, 0x00, 0x07});
        Cmd(0x66, {0x3C, 0x00, 0xCD, 0x67, 0x45, 0x45, 0x10, 0x00, 0x00, 0x00});
        Cmd(0x67, {0x00, 0x3C, 0x00, 0x00, 0x00, 0x01, 0x54, 0x10, 0x32, 0x98});
        Cmd(0x74, {0x10, 0x85, 0x80, 0x00, 0x00, 0x4E, 0x00});
        Cmd(0x98, {0x3E, 0x07});
        Cmd(0x35); // tearing effect line on
        Cmd(0x21); // display inversion on (this panel needs it for true colours)
        Cmd(0x11); // sleep out
        System::Delay(120);
        Cmd(0x29); // display on
        System::Delay(20);
    }

    /** 3-wire mode: DC is sent as a 9th bit per byte (SOFT transport only). */
    void SetNineBit(bool on) { nine_bit_ = on; }

    /** Send a bare command byte (no parameters). */
    void Command(uint8_t c) { Cmd(c); }

    /** Drive the DC line directly, for probing with a meter. */
    void DebugDc(bool v) { dc_.Write(v); dc_state_ = v; }

    /** Read `nbits` bits back from the panel after command `cmd` on the
     *  SDA line (SOFT transport only). `dummy` = leading dummy clocks.
     *  `pull` selects the SDA input pull, so two reads with opposite pulls
     *  reveal whether the panel is really driving the line. */
    uint32_t ReadBack(uint8_t cmd, int nbits, int dummy, GPIO::Pull pull)
    {
        cs_.Write(0);
        dc_.Write(0);
        dc_state_ = false;
        Tx(&cmd, 1);
        dc_.Write(1);
        dc_state_ = true;
        mosi_.Init(cfg_.mosi, GPIO::Mode::INPUT, pull);
        System::DelayUs(2);
        for(int i = 0; i < dummy; i++)
        {
            sclk_.Write(1);
            System::DelayUs(1);
            sclk_.Write(0);
            System::DelayUs(1);
        }
        uint32_t v = 0;
        for(int i = 0; i < nbits; i++)
        {
            sclk_.Write(1);
            System::DelayUs(1);
            v = (v << 1) | (mosi_.Read() ? 1 : 0);
            sclk_.Write(0);
            System::DelayUs(1);
        }
        cs_.Write(1);
        mosi_.Init(cfg_.mosi, GPIO::Mode::OUTPUT, GPIO::Pull::NOPULL,
                   GPIO::Speed::VERY_HIGH);
        return v;
    }

    /** Set the write window (public for tests). */
    void Window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
    {
        SetWindow(x0, y0, x1, y1);
    }

    /** RAMWR + n raw bytes in one CS window (public for tests). */
    void WriteRaw(uint8_t* data, size_t n)
    {
        uint8_t ramwr = 0x2C;
        cs_.Write(0);
        dc_.Write(0);
        dc_state_ = false;
        Tx(&ramwr, 1);
        dc_.Write(1);
        dc_state_ = true;
        Tx(data, n);
        cs_.Write(1);
    }

    /** Send cmd, then clock n raw bytes back from SDA into out
     *  (SOFT transport only, SDA pulled down). */
    void ReadBytes(uint8_t cmd, uint8_t* out, size_t n)
    {
        cs_.Write(0);
        dc_.Write(0);
        dc_state_ = false;
        Tx(&cmd, 1);
        dc_.Write(1);
        dc_state_ = true;
        mosi_.Init(cfg_.mosi, GPIO::Mode::INPUT, GPIO::Pull::PULLDOWN);
        System::DelayUs(2);
        for(size_t i = 0; i < n; i++)
        {
            uint8_t v = 0;
            for(int b = 0; b < 8; b++)
            {
                sclk_.Write(1);
                System::DelayUs(1);
                v = (v << 1) | (mosi_.Read() ? 1 : 0);
                sclk_.Write(0);
                System::DelayUs(1);
            }
            out[i] = v;
        }
        cs_.Write(1);
        mosi_.Init(cfg_.mosi, GPIO::Mode::OUTPUT, GPIO::Pull::NOPULL,
                   GPIO::Speed::VERY_HIGH);
    }

    /** Switch transport at runtime. Pins are re-muxed accordingly. */
    void SetTransport(Transport t)
    {
        transport_ = t;
        if(t == Transport::SOFT)
        {
            sclk_.Init(cfg_.sclk, GPIO::Mode::OUTPUT, GPIO::Pull::NOPULL,
                       GPIO::Speed::VERY_HIGH);
            mosi_.Init(cfg_.mosi, GPIO::Mode::OUTPUT, GPIO::Pull::NOPULL,
                       GPIO::Speed::VERY_HIGH);
            sclk_.Write(0);
        }
        else
        {
            spi_.Init(cfg_.spi); // re-muxes SCK/MOSI to SPI1 alternate function
        }
    }

    void Fill(uint16_t c)
    {
        for(int i = 0; i < W * H; i++)
            fb_[i] = c;
    }

    inline void Pixel(int x, int y, uint16_t c)
    {
        if((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H)
            fb_[y * W + x] = c;
    }

    void FillRect(int x0, int y0, int w, int h, uint16_t c)
    {
        for(int y = y0; y < y0 + h; y++)
            for(int x = x0; x < x0 + w; x++)
                Pixel(x, y, c);
    }

    void FillCircle(int cx, int cy, int r, uint16_t c)
    {
        for(int y = -r; y <= r; y++)
            for(int x = -r; x <= r; x++)
                if(x * x + y * y <= r * r)
                    Pixel(cx + x, cy + y, c);
    }

    /** Push the whole frame buffer to the panel. */
    void Update()
    {
        SetWindow(0, 0, W - 1, H - 1);
        // RAMWR and its pixel data must share ONE chip-select-low window:
        // the GC9A01 ends the command when CS rises, and pixel bytes that
        // arrive afterwards with no command in progress are discarded.
        uint8_t ramwr = 0x2C;
        cs_.Write(0);
        dc_.Write(0);
        dc_state_ = false;
        Tx(&ramwr, 1);
        dc_.Write(1);
        dc_state_ = true;
        // HAL transfers are limited to 65535 bytes; send in row blocks.
        constexpr size_t kRowsPerChunk = 40;
        constexpr size_t kChunkBytes   = kRowsPerChunk * W * 2;
        uint8_t*         p             = reinterpret_cast<uint8_t*>(fb_);
        for(size_t off = 0; off < (size_t)W * H * 2; off += kChunkBytes)
            Tx(p + off, kChunkBytes);
        cs_.Write(1);
    }

  public:
    /** Half-period of the bit-banged clock in ~2 ns steps (0 = flat out). */
    void SetSoftSpiPause(uint32_t loops) { soft_pause_ = loops; }

  private:
    inline void Pause()
    {
        for(volatile uint32_t i = 0; i < soft_pause_; i++) {}
    }

    /** Send bytes on whichever transport is active; returns only after the
     *  last clock edge has really happened, so DC/CS may change afterwards. */
    void Tx(uint8_t* p, size_t n)
    {
        if(transport_ == Transport::SOFT)
        {
            for(size_t i = 0; i < n; i++)
            {
                uint8_t b = p[i];
                if(nine_bit_)
                {
                    mosi_.Write(dc_state_); // D/C bit leads each byte
                    Pause();
                    sclk_.Write(1);
                    Pause();
                    sclk_.Write(0);
                }
                for(int bit = 7; bit >= 0; bit--)
                {
                    mosi_.Write((b >> bit) & 1);
                    Pause();
                    sclk_.Write(1); // panel samples on the rising edge
                    Pause();
                    sclk_.Write(0);
                }
            }
        }
        else
        {
            spi_.BlockingTransmit(p, n, 200);
            // H7 SPI can report end-of-transfer before the final clock edges
            // are out. Give it a few SCK periods before DC/CS move.
            System::DelayUs(5);
        }
    }

    void Cmd(uint8_t cmd, std::initializer_list<uint8_t> data = {})
    {
        cs_.Write(0);
        dc_.Write(0);
        dc_state_ = false;
        Tx(&cmd, 1);
        if(data.size())
        {
            uint8_t buf[16];
            size_t  n = 0;
            for(uint8_t d : data)
                buf[n++] = d;
            dc_.Write(1);
            dc_state_ = true;
            Tx(buf, n);
        }
        cs_.Write(1);
    }

    void SetWindow(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
    {
        Cmd(0x2A, {0x00, x0, 0x00, x1}); // CASET
        Cmd(0x2B, {0x00, y0, 0x00, y1}); // RASET
    }

    Config    cfg_;
    Transport transport_;
    bool      nine_bit_ = false;
    uint32_t  soft_pause_ = 100; // ~700 kHz; proven-safe for jumper wires
    bool      dc_state_ = true;
    SpiHandle spi_;
    GPIO      sclk_, mosi_, dc_, rst_, cs_;
    uint16_t* fb_;
};
} // namespace daisy
