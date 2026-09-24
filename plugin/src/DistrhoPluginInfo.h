/** Ammonite as a plugin (DPF): what the hosts see. The IDs below make the
 *  VST3 UID and the CLAP ID: never change them once released, or projects
 *  lose the plugin. */
#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "GSNautilus"
#define DISTRHO_PLUGIN_NAME    "Ammonite"
#define DISTRHO_PLUGIN_URI     "https://github.com/GSNautilus/ammonite"
#define DISTRHO_PLUGIN_CLAP_ID "com.gsnautilus.ammonite"

#define DISTRHO_PLUGIN_BRAND_ID  GSNa
#define DISTRHO_PLUGIN_UNIQUE_ID Ammo

// A synth that plays by itself: no audio in, stereo out. IS_SYNTH gives it
// a MIDI input (hosts expect one on an instrument); it ignores the notes.
#define DISTRHO_PLUGIN_IS_SYNTH     1
#define DISTRHO_PLUGIN_NUM_INPUTS   0
#define DISTRHO_PLUGIN_NUM_OUTPUTS  2
#define DISTRHO_PLUGIN_IS_RT_SAFE   1
#define DISTRHO_PLUGIN_HAS_UI       0
#define DISTRHO_PLUGIN_WANT_TIMEPOS 0

#define DISTRHO_PLUGIN_VST3_CATEGORIES "Instrument|Synth"
#define DISTRHO_PLUGIN_CLAP_FEATURES   "instrument", "synthesizer", "stereo"

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
