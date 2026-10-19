/** Ammonite Test (phase 1 of Ammonite Desktop): what the hosts see.
 *  Two variants from one header, chosen by the build (AMMO_UI_WEB / AMMO_UI_GL),
 *  with their own names and ids so both can live in one host session. These
 *  ids are the test plugin's, never the product's (Ammonite is "Ammo"). */
#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#if !defined(AMMO_UI_WEB) && !defined(AMMO_UI_GL)
# error define AMMO_UI_WEB or AMMO_UI_GL (desktop/CMakeLists.txt does)
#endif

#define DISTRHO_PLUGIN_BRAND    "GSNautilus"
#define DISTRHO_PLUGIN_URI      "https://github.com/GSNautilus/ammonite"
#define DISTRHO_PLUGIN_BRAND_ID GSNa

#ifdef AMMO_UI_WEB
# define DISTRHO_PLUGIN_NAME      "Ammonite Test Web"
# define DISTRHO_PLUGIN_CLAP_ID   "com.gsnautilus.ammonite-test-web"
# define DISTRHO_PLUGIN_UNIQUE_ID AmoW
# define AMMO_UNIQUE_ID           d_cconst('A', 'm', 'o', 'W')
# define AMMO_VARIANT             "Web"
// The low-level web view API (distrho/extra/WebView.hpp) for our own UI
// class; DISTRHO_UI_USE_WEB_VIEW stays 0 (that mode has no UI API at all).
# define DISTRHO_UI_WEB_VIEW      1
#else
# define DISTRHO_PLUGIN_NAME      "Ammonite Test GL"
# define DISTRHO_PLUGIN_CLAP_ID   "com.gsnautilus.ammonite-test-gl"
# define DISTRHO_PLUGIN_UNIQUE_ID AmoG
# define AMMO_UNIQUE_ID           d_cconst('A', 'm', 'o', 'G')
# define AMMO_VARIANT             "GL"
#endif

// A synth that plays by itself: no audio in, stereo out. IS_SYNTH gives it
// a MIDI input (hosts expect one on an instrument); it ignores the notes.
#define DISTRHO_PLUGIN_IS_SYNTH           1
#define DISTRHO_PLUGIN_NUM_INPUTS         0
#define DISTRHO_PLUGIN_NUM_OUTPUTS        2
#define DISTRHO_PLUGIN_IS_RT_SAFE         1
#define DISTRHO_PLUGIN_HAS_UI             1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1 // the UI reads the telemetry in-process
#define DISTRHO_PLUGIN_WANT_TIMEPOS       1 // host tempo and position
#define DISTRHO_UI_USE_NANOVG             1
#define DISTRHO_UI_USER_RESIZABLE         1
#define DISTRHO_UI_DEFAULT_WIDTH          720
#define DISTRHO_UI_DEFAULT_HEIGHT         480

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Synth"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "instrument", "synthesizer", "stereo"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
