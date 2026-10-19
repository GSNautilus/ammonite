/* The C API of the Ammonite Desktop engine (ammonite_engine.dll): what the
 * Python tests, tools and later the standalone call. Plain C structs so
 * ctypes mirrors them 1:1. */
#ifndef AMMONITE_ENGINE_API_H
#define AMMONITE_ENGINE_API_H

#include <stdint.h>

#ifdef _WIN32
# ifdef AMMO_BUILD_DLL
#  define AMMO_API __declspec(dllexport)
# else
#  define AMMO_API
# endif
#else
# define AMMO_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define AMMO_SLOTS    16
#define AMMO_WAVE_LEN 128
#define AMMO_POLY     6   /* voices per channel */
#define AMMO_LFOS     32  /* the modulation pool's LFOs */
#define AMMO_ROUTES   128 /* its routes */
#define AMMO_STEPS    16  /* the longest pattern (LENGTH) */

typedef struct AmmoClock
{
    int    valid;   /* the host has a tempo and a position (bpm, beat) */
    int    playing; /* its transport runs (valid or not: stopped, no new notes; a null clock plays) */
    double bpm;
    double beat;    /* quarter notes since the song start, at the block's first sample */
} AmmoClock;

typedef struct AmmoParamInfo
{
    int      id;
    char     name[48];
    char     symbol[32];
    char     group[16]; /* "global", "slot 1".."slot 16", or "pool" (the modulation pool: LFOs, macros 5..16, routes) */
    float    min, max, def;
    int      steps;     /* > 0: stepped, with that many values */
    uint32_t flags;     /* ammo::ParamFlags */
} AmmoParamInfo;

typedef struct AmmoChannelFrame
{
    int      active;     /* the slot has a generator (it plays, muted or not) */
    int      muted;      /* its mute is on: the output fades to 0, the pattern runs on */
    int      gen;        /* generator type */
    float    note;       /* MIDI note the voice is on (or gliding to) */
    int      pitchClass; /* 0..11 of that note */
    float    env;        /* amp envelope 0..1 */
    float    rms, peak;  /* of the channel's mono output in this block */
    int      hit;        /* a note started in this block */
    uint32_t hitSeed;    /* changes per hit */
    int      step, length; /* the generator's step position */
    int8_t   stepNote[AMMO_STEPS]; /* the notes of the pattern playing (LENGTH of them, from its first step): MIDI,
                                      -1 a rest; a chord's strike: its root. A hit the Free dice pick (Random, VARY)
                                      cannot be foreseen: the note it last played, -2 until it has played. All -1
                                      before the generator's first step */
    int      voices;     /* voices sounding (a chord: one per note) */
    float    voiceNote[AMMO_POLY]; /* their notes, lowest voice first */
    float    duck;       /* how far its DUCK takes it down in this block, 0..1 */
    float    wave[AMMO_WAVE_LEN]; /* one period of the channel's output, phase-aligned */
} AmmoChannelFrame;

typedef struct AmmoFrame
{
    uint32_t seq;
    double   beat, bpm;
    int      playing, hostClock;
    int      chordRootPc, chordDeg, chordQuality, progPos, progLen;
    int      keyMask, chordMask;
    int      instance;
    double   sampleRate;
    uint32_t block;
    float    masterRms, masterPeak;
    float    glueGr, limitGr; /* the master bus: the deepest gain reduction in the block, dB */
    AmmoChannelFrame ch[AMMO_SLOTS];
    /* the modulation pool, at the block's last modulation update: */
    uint32_t lfoRun;             /* bit l: LFO l ran (in use, or a live route takes it) */
    float    lfo[AMMO_LFOS];     /* each LFO now, -1..1 (0 when it did not run) */
    float    route[AMMO_ROUTES]; /* per route, its target's position in its range after modulation (0..1), -1: not live */
    float    cpu;  /* the engine's time rendering this block over the block's length in real time (1: all of it) */
} AmmoFrame;

AMMO_API void*  ammo_create(double sampleRate, int maxBlock);
AMMO_API void   ammo_destroy(void* engine);
AMMO_API int    ammo_param_count(void);                          /* the host's parameters (no routes, nothing hidden) */
AMMO_API int    ammo_param_info(int index, AmmoParamInfo* out);  /* by host index */
AMMO_API int    ammo_param_info_id(int id, AmmoParamInfo* out);  /* by id: any id, hidden ones and routes too (0: none) */
AMMO_API int    ammo_param_step_name(int id, int step, char* buf, int cap);
AMMO_API void   ammo_set_param(void* engine, int id, float value);
AMMO_API float  ammo_get_param(void* engine, int id);
AMMO_API void   ammo_process(void* engine, float* outL, float* outR, int frames, const AmmoClock* clock);
AMMO_API int    ammo_read_frame(void* engine, AmmoFrame* out);
AMMO_API int    ammo_save(void* engine, char* buf, int cap);
AMMO_API int    ammo_load(void* engine, const char* buf, int len);
AMMO_API const char* ammo_version(void);
AMMO_API int         ammo_latency(void* engine); /* samples the output lags the music (the master limiter) */

/* factory presets (engine/Presets.hpp) */
AMMO_API int         ammo_preset_count(void);
AMMO_API const char* ammo_preset_name(int k);
AMMO_API const char* ammo_preset_blurb(int k);
AMMO_API int         ammo_apply_preset(void* engine, int k); /* every saved parameter (the routes too): the preset's value or the default */

/* recording the master output to a 32-bit float stereo WAV (engine/Record.hpp): a thread drains the engine's
 * tap into the file while ammo_process runs, as the plugin's UI does */
AMMO_API int       ammo_record_start(void* engine, const char* pathUtf8); /* 1: recording */
AMMO_API long long ammo_record_stop(void* engine);    /* frames written, -1 if none was recording */
AMMO_API unsigned  ammo_record_dropped(void* engine); /* frames the tap could not fit (the last recording) */

#ifdef __cplusplus
}
#endif

#endif /* AMMONITE_ENGINE_API_H */
