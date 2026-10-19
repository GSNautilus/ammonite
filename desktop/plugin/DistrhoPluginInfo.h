/** Ammonite Desktop: what the hosts see. IDENTITY IS PROVISIONAL:
 *  the product name, vendor and plugin ids are not settled. These never collide
 *  with the hardware plugin (Ammo) or the test plugins (AmoW, AmoG).
 *  Once released, the ids must never change. */
#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND     "GSNautilus"
#define DISTRHO_PLUGIN_NAME      "Ammonite Desktop"
#define DISTRHO_PLUGIN_URI       "https://github.com/GSNautilus/ammonite"
#define DISTRHO_PLUGIN_CLAP_ID   "com.gsnautilus.ammonite-desktop"
#define DISTRHO_PLUGIN_BRAND_ID  GSNa
#define DISTRHO_PLUGIN_UNIQUE_ID AmoD
#define AMMO_UNIQUE_ID           d_cconst('A', 'm', 'o', 'D')

// A synth that plays by itself: no audio in, stereo out. IS_SYNTH gives it
// a MIDI input (hosts expect one on an instrument); it ignores the notes.
#define DISTRHO_PLUGIN_IS_SYNTH           1
#define DISTRHO_PLUGIN_NUM_INPUTS         0
#define DISTRHO_PLUGIN_NUM_OUTPUTS        2
#define DISTRHO_PLUGIN_IS_RT_SAFE         1
#define DISTRHO_PLUGIN_HAS_UI             1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1 // the UI reads the engine's telemetry in-process
#define DISTRHO_PLUGIN_WANT_TIMEPOS       1 // host tempo and position
#define DISTRHO_PLUGIN_WANT_LATENCY       1 // the master limiter looks 1.5 ms ahead
#define DISTRHO_PLUGIN_WANT_STATE         1 // the modulation routes: saved with the patch, not host parameters
#define DISTRHO_PLUGIN_WANT_FULL_STATE    1 // ... read back from the engine whenever the host saves
#define DISTRHO_UI_USE_NANOVG             1 // only the fallback message when the web view fails
#define DISTRHO_UI_WEB_VIEW               1 // the low-level web view API for our UI
#define DISTRHO_UI_USER_RESIZABLE         1
// Designed for a 3440 x 1440 monitor: the window opens just under full size
// (the standalone host clamps it to the work area, keeping the aspect).
#define DISTRHO_UI_DEFAULT_WIDTH          3200
#define DISTRHO_UI_DEFAULT_HEIGHT         1302

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Synth"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "instrument", "synthesizer", "stereo"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
