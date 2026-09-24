/**
 * Ammonite firmware: thin Daisy shell around ../core - the same core the PC
 * simulator runs. Hardware in, hardware out, zero synth logic here.
 *
 * SEED3: AGND (pin 20) is NOT bonded to DGND (pin 40) on the board. They
 *   MUST be jumpered together (datasheet v2.1 p.12, fig 1.5) or the ADC
 *   reference floats and every channel reads full scale with random dips.
 * Pots: pot i (panel order, see ENGINE_PLAN.md) -> ADC Ai. Pot 10 (knob
 *   11, PAGE) selects the page, pots 0..9 are that page's functions (on a
 *   column page one column per oscillator), pot 11 is VOLUME.
 *   A0..A10 = physical pins 22..32, A11 = physical pin 35.
 *   Every pot: one end -> pin 21 (+3V3A), other end -> pin 20 (AGND),
 *   wiper -> its A pin.
 * Display: VCC 38 (3V3D), GND 40, SCL 9 (D8), SDA 11 (D10), DC 7 (D6),
 *   CS 8 (D7), RST 6 (D5) - see gc9a01.h.
 * Audio: OUT L 18, OUT R 19, ground 20 (AGND).
 */
#include "daisy_seed.h"
#include "gc9a01.h"
#include "synth_core.h"

using namespace daisy;

// Bring-up aid: 1 = show raw ADC values of all 12 knobs instead of the synth
// screen (audio still runs). Set back to 0 for the real instrument.
#define DIAG_ADC 0
// 1 = ignore the synth and output a clean 220 Hz sine on both channels.
// If THIS sounds bad, the engine is innocent (codec / load / jack / DMA).
#define DIAG_TONE 0
// 1 = run the display SPI at 24.6 MHz (~38 ms per frame, ~26 fps) instead
// of 12.5 MHz (~74 ms, ~13.5 fps). Works on the hardware (user confirmed
// 2026-09-23, jumper wires). If a rewired panel shows garbage, set it to 0.
#define FAST_DISPLAY_SPI 1

#if DIAG_ADC
#include "canvas.h"
// SDRAM self-test region (1 MB) and audio-callback load meter (DWT cycles).
static uint32_t DSY_SDRAM_BSS sdram_test[1 << 18];
static uint32_t sdram_errors = 0;
static uint32_t cpu_last = 0, cpu_max = 0;
static float    cpu_avg  = 0.f;
static inline uint16_t* fb_or(uint16_t* fb) { return fb; }
#endif

// Which pots are physically wired. An unconnected ADC input floats and
// wanders, which the core reads as constant knob movement (the readout
// flashes between names). Unwired knobs are skipped and stay at the core's
// defaults. Set every entry to true once the panel is fully wired.
static const bool kPotWired[synth::kNumPots] = {
    true,  true,  true,  // knobs 1-3   page functions
    true,  true,  true,  // knobs 4-6   page functions
    true,  true,  true,  // knobs 7-9   page functions
    true,  true,  true,  // knob 10 function / sub-page, 11 PAGE, 12 VOLUME
};

// Pots that read backwards because of how they are mounted (the bottom
// rows are upside down in the panel). true = firmware inverts the reading.
// If any other knob turns the wrong way, flip its entry here; no rewiring.
static const bool kPotInvert[synth::kNumPots] = {
    false, false, false, // 1-3   (top row)
    true,  true,  true,  // 4-6   (bottom row)
    false, false, false, // 7-9   (top row)
    true,  true,  true,  // 10-12 (bottom row)
};

DaisySeed hw;
GC9A01    lcd;

// NOT in DMA_BUFFER_MEM_SECTION. That section is the start of RAM_D2, and
// only its first 32 KB are non-cacheable (libDaisy MPU region 0). A 115 KB
// framebuffer placed there pushed libDaisy's audio DMA buffers into
// cacheable memory, so the codec played stale cache garbage (2026-09-21).
// The display driver uses blocking SPI, so plain RAM is fine.
static uint16_t framebuf[GC9A01::W * GC9A01::H];

// Big DSP buffers live in SDRAM (they overflow main RAM); the core only
// ever sees them through the Buffers struct.
static synth::DelayLineT DSY_SDRAM_BSS delayL[synth::kOscs]; // one stereo delay per osc
static synth::DelayLineT DSY_SDRAM_BSS delayR[synth::kOscs];
static daisysp::ReverbSc DSY_SDRAM_BSS reverb;
static synth::PreLineT   DSY_SDRAM_BSS preL, preR;

void AudioCallback(AudioHandle::InterleavingInputBuffer  in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t                                size)
{
#if DIAG_ADC
    uint32_t t0 = DWT->CYCCNT;
#endif
#if DIAG_TONE
    static float ph = 0.f;
    for(size_t n = 0; n < size; n += 2)
    {
        float s = 0.05f * sinf(ph); // -26 dBFS: safe to probe with an ADC pin
        ph += 6.2831853f * 220.f / 48000.f;
        if(ph > 6.2831853f)
            ph -= 6.2831853f;
        out[n]     = s;
        out[n + 1] = s;
    }
#else
    synth::ProcessAudio(out, size / 2); // core writes interleaved stereo
#endif
#if DIAG_ADC
    uint32_t dt = DWT->CYCCNT - t0;
    cpu_last    = dt;
    if(dt > cpu_max)
        cpu_max = dt;
    cpu_avg += 0.02f * ((float)dt - cpu_avg);
#endif
}

int main(void)
{
    hw.Configure();
    hw.Init();
    hw.SetAudioBlockSize(48);

#if DIAG_ADC
    // Cycle counter for the load meter.
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    // SDRAM: write a pattern, wait (refresh), read it back.
    for(uint32_t i = 0; i < (1u << 18); i++)
        sdram_test[i] = (i * 2654435761u) ^ 0xA5A5A5A5u;
    System::Delay(200);
    for(uint32_t i = 0; i < (1u << 18); i++)
        if(sdram_test[i] != ((i * 2654435761u) ^ 0xA5A5A5A5u))
            sdram_errors++;
#endif

    // 12 pots, pot i -> ADC channel Ai (panel order).
    const Pin kPotPins[synth::kNumPots]
        = {seed::A0, seed::A1, seed::A2, seed::A3, seed::A4,  seed::A5,
           seed::A6, seed::A7, seed::A8, seed::A9, seed::A10, seed::A11};
    // Long sampling time: the default 8.5 cycles is far too short for a
    // pot's source impedance and lets channels smear into each other.
    AdcChannelConfig adc_cfg[synth::kNumPots];
    for(int i = 0; i < synth::kNumPots; i++)
        adc_cfg[i].InitSingle(kPotPins[i],
                              AdcChannelConfig::SPEED_387CYCLES_5);
    hw.adc.Init(adc_cfg, synth::kNumPots);
    hw.adc.Start();

#if DIAG_ADC
    // Self-test with no wiring: the Seed's own DAC drives A8 (DAC1_OUT1,
    // pin 30, knob 9) to 25 % and A7 (DAC1_OUT2, pin 29, knob 8) to 75 %.
    // A working ADC must show ~25 on entry 9 and ~75 on entry 8.
    DacHandle::Config dac_cfg;
    dac_cfg.bitdepth   = DacHandle::BitDepth::BITS_12;
    dac_cfg.buff_state = DacHandle::BufferState::ENABLED;
    dac_cfg.mode       = DacHandle::Mode::POLLING;
    dac_cfg.chn        = DacHandle::Channel::BOTH;
    hw.dac.Init(dac_cfg);
    hw.dac.WriteValue(DacHandle::Channel::ONE, 1024); // 25 % -> A8
    hw.dac.WriteValue(DacHandle::Channel::TWO, 3072); // 75 % -> A7
#endif

#if FAST_DISPLAY_SPI
    // SPI1 normally runs off PLL2P (25 MHz -> 12.5 MHz SCK at PS_2, the
    // fastest prescaler). PLL3P is already running at 49.15 MHz for the
    // audio codec, so pointing the SPI1/2/3 kernel-clock mux at it doubles
    // the SCK. Only the mux register is touched: HAL_RCCEx_PeriphCLKConfig
    // would reprogram PLL3 itself and could stop the audio.
    __HAL_RCC_SPI123_CONFIG(RCC_SPI123CLKSOURCE_PLL3);
#endif
    GC9A01::Config cfg;
    lcd.Init(cfg, framebuf);

    synth::Buffers buffers;
    for(int o = 0; o < synth::kOscs; o++)
    {
        buffers.delayL[o] = &delayL[o];
        buffers.delayR[o] = &delayR[o];
    }
    buffers.reverb = &reverb;
    buffers.preL   = &preL;
    buffers.preR   = &preR;
    synth::Init(hw.AudioSampleRate(), buffers); // core sets knob defaults

    hw.StartAudio(AudioCallback);

    uint32_t frame = 0;
    for(;;)
    {
        for(int i = 0; i < synth::kNumPots; i++)
        {
            if(!kPotWired[i])
                continue;
            float v = hw.adc.GetFloat(i);
            synth::SetPot(i, kPotInvert[i] ? 1.f - v : v);
        }
#if DIAG_ADC
        synth::RenderDiagnostics(framebuf);
        {
            // Audio block budget: 48 frames * 10000 cycles = 480000 cycles.
            char     line[16];
            int      avg = (int)(cpu_avg / 4800.f);
            int      mx  = (int)(cpu_max / 4800);
            uint16_t ok  = synthui::Color(60, 230, 120);
            uint16_t bad = synthui::Color(255, 70, 70);
            if(sdram_errors == 0)
                synthui::DrawTextCentered(fb_or(framebuf), "SDRAM OK", 120, 208, 2, ok);
            else
                synthui::DrawTextCentered(fb_or(framebuf), "SDRAM BAD", 120, 208, 2, bad);
            int n = 0;
            const char* p = "CPU ";
            while(*p) line[n++] = *p++;
            if(avg >= 100) line[n++] = '0' + (avg / 100) % 10;
            if(avg >= 10) line[n++] = '0' + (avg / 10) % 10;
            line[n++] = '0' + avg % 10;
            line[n++] = ' ';
            if(mx >= 100) line[n++] = '0' + (mx / 100) % 10;
            if(mx >= 10) line[n++] = '0' + (mx / 10) % 10;
            line[n++] = '0' + mx % 10;
            line[n] = 0;
            synthui::DrawTextCentered(fb_or(framebuf), line, 120, 226, 2,
                                      (mx < 90) ? ok : bad);
        }
#else
        synth::RenderScreen(framebuf);
#endif
        lcd.Update();
        hw.SetLed((frame++ / 6) & 1);
    }
}
