/* Ammonite Desktop: the page. Binds the engine's parameter table and
   telemetry (pushed by AmmoniteUI.cpp) to the stage and one panel of glass
   docked on the left or the right; the stage centres in the room left over.

   The panel has four tabs: Conductor (key, chords, time, space, master),
   Modulation (the pool of LFOs and macros, each with the targets it
   moves), Channels (an accordion: every channel a row with its wave, level,
   pan, eye and mute; one opens at a time to show what it contains,
   Generator, Instrument, Effects and Visual, one section at a
   time), Visual (the stage as a
   whole, its camera, and each channel's look). It collapses to a rail.
   The stage itself takes a drag (orbit), the wheel (zoom) and a
   double-click (reset): see the camera.

   The layout is designed 1440 high and scaled to the window's height (or
   its width, below 2000 wide); its width follows the window.

   The DOM is built on structural changes only (tab, selection, slots on /
   off, sections, presets); values, chip states and texts refresh in place,
   so menus stay open and scroll positions survive. Canvases repaint every
   frame.

   Host -> page: ammo.params(defs) ammo.values(pairs) ammo.param(id, v)
                 ammo.presets(list, current, file) ammo.page(json) ammo.rec(o) ammo.toast(text)
                 ammo.info(o) ammo.tick(frames, host)
   Page -> host: ready | set <id> <v> | edit <id> <0|1> | preset <k> | save <json> | open | record <0|1> |
                 key <down> <vk> <repeat> | gl ok ... | gl fail ... | log ...
   A patch file carries the page's own state with the engine's (pageState(), ammo.page). Ctrl+S, Ctrl+O, R and B
   are the page's keys (save, open, record, the pin bar); every other key goes to the host.
   The pin bar: any knob or fader pinned (right-click, a long press by touch, or its pin on
   hover) gets a copy along the bottom of the stage, beside the panel, so the panel collapsed to its rail and the bar
   open make a playing view.
   In a plain browser, dev.html loads fake-engine.js to stand in for the host.

   Mini mode: the same page is Ammonite Mini's, on an Android tablet. Mini's own index.html and
   dev.html set window.AMMO_MINI = true before any script loads (they also leave out the stage canvas and
   shells.js); everything else is this page, copied into Mini's repo as it is. MINI then: no stage, camera, motion
   layer or stage FX (no Shells at all), no Visual tab, no eyes or channel VISUAL group, no rail or dock; the panel
   fills the screen, designed LAYOUT_H = 900 high (0.89 on the tablet's 1333x800 CSS px); touch-sized (app.css,
   html.mini: every target at least 54 layout px = 48 CSS px); a 4 px dead zone before a drag, a double-tap resets,
   a second finger held makes a drag fine (and moves, ends or clicks nothing itself), hover looks only with a mouse;
   Sync hidden (the tablet's clock is internal), no output picker; the pin bar is a drawer along the bottom of the
   screen (the panel shortens above it); page state under 'ammo.mini.ui'; a patch's page line kept whole as it came
   (desktop's looks, stage and camera) and written back with Mini's macro names and pins. The host is the app's
   bridge, window.ammoHost (a WebMessageListener), which speaks this page's messages. Desktop is unchanged by it. */
(function () {
  'use strict';
  var MINI = !!window.AMMO_MINI;
  var LAYOUT_H = MINI ? 900 : 1440, MIN_W = MINI ? 1500 : 2000, PANEL_W = 1336, RAIL_W = 64, M = 12;
  var bridge = (window.chrome && window.chrome.webview) || window.ammoHost; // desktop's WebView2, or Mini's bridge
  if (MINI) document.documentElement.classList.add('mini');
  function send(m) { if (bridge) bridge.postMessage(m); }

  /* ------------------------------------------------------------- data */
  var DEFS = {}, ORDER = {}, SLOTP = {}, P = {}, PRESETS = [], presetNow = 1, patchFile = '', INFO = {};
  var rec = { on: false, t0: 0 };
  var SLOTS = 16, SLOT_BASE = 64, SLOT_STRIDE = 128;
  var PROGS = [[], [0, 4, 5, 3], [0, 5, 3, 4], [5, 3, 0, 4], [0, 5, 2, 6], [1, 4, 0, 0], [0, 3], [0, 4, 5, 2, 3, 0, 3, 4], [0, 6, 5, 4]];
  var ROMAN = ['I', 'II', 'III', 'IV', 'V', 'VI', 'VII'];
  var PROG_CUSTOM = 9, PROG_STEPS = 8, progSel = 0; // Progression = Custom; the step its chips edit
  var SCALE_IV = [[0, 2, 4, 5, 7, 9, 11], [0, 2, 3, 5, 7, 8, 10], [0, 2, 3, 5, 7, 9, 10], [0, 1, 3, 5, 7, 8, 10], [0, 2, 4, 6, 7, 9, 11],
                  [0, 2, 4, 5, 7, 9, 10], [0, 2, 3, 5, 7, 8, 11], [0, 2, 4, 7, 9], [0, 3, 5, 7, 10], [0, 3, 5, 6, 7, 10]];
  var QUAL = ['', 'm', 'dim', 'aug', 'sus'];
  /* modulation (engine/Mod.hpp): one pool of sources, up to 32 LFOs and 16 macros,
     none belonging to a channel, and 128 routes (source, target, amount, polarity), saved with the patch but not host
     parameters. A route adds amount x source to its target's place in its range: Bipolar an LFO swings around the knob,
     Unipolar it goes up from it (down for a negative amount); a macro is 0..1 either way. An LFO or macro is in use
     (listed in the Modulation tab) by its flag; + Add takes the first free one. */
  var POOL_LFOS = 32, POOL_MACROS = 16, POOL_ROUTES = 128, SRC_LFO1 = 1, SRC_M1 = 33, AMT_NEW = 0.25, ROUTE_BASE = 2400;
  var MACRO_NAMES = ['Bloom', 'Motion', 'Space', 'Grit']; // macros 1..4 as named before the pool (renamable: ui.mnames)
  // what a route can move: a channel's continuous parameters (engine/Params.hpp kModDests) and these globals
  var DEST_SYMS = ['level', 'pan', 'tune', 'shape', 'detune', 'sub', 'noise', 'drive', 'glide', 'cutoff', 'reso', 'filter_type', 'filt_amt',
                   'filt_a', 'filt_d', 'amp_a', 'amp_d', 'amp_s', 'amp_r', 'pitch_amt', 'pitch_d', 'arp_gate', 'arp_density', 'arp_vary', 'arp_ratchet',
                   'chord_strum', 'rhy_chance', 'drum_tune', 'drum_decay', 'drum_tone', 'drum_snap', 'fx_drive', 'fx_tone', 'cho_rate', 'cho_depth',
                   'cho_mix', 'dly_fb', 'dly_damp', 'dly_mix', 'rev_send', 'duck_amt'];
  var GLOBAL_TARGETS = ['swing', 'volume', 'rev_size', 'rev_decay', 'rev_damp', 'rev_pre', 'rev_width', 'rev_return', 'glue', 'ceiling'];
  var LDIVB = [64, 32, 16, 8, 4, 3, 2, 1.5, 1, 2 / 3, 0.75, 0.5, 1 / 3, 0.25, 1 / 6, 0.125]; // the LFO divisions in beats
  var assign = null; // assigning a source: {src} (any channel's targets, and the Conductor's)
  function gid(sym) { return DEFS[sym] ? DEFS[sym].id : -1; }
  function sid(slot, sym) { return SLOT_BASE + SLOT_STRIDE * slot + (SLOTP[sym] != null ? SLOTP[sym] : 0); }
  function val(id) { return P[id] != null ? P[id] : (ORDER[id] ? ORDER[id].def : 0); }
  function sval(slot, sym) { return val(sid(slot, sym)); }
  function gval(sym) { return val(gid(sym)); }
  function names(id) { return ORDER[id] ? ORDER[id].names || [] : []; }
  function nameOf(id) { var d = ORDER[id]; return d && d.names ? d.names[Math.round(val(id)) - d.min] || '' : ''; }

  /* knob kinds and formats per parameter symbol */
  var KINDS = {
    level: ['level', 'pct'], pan: ['pan', 'bip'], degree: ['range', 'deg'], oct: ['range', 'octs'],
    arp_range: ['range', 'range'], arp_div: ['div', 'div'], arp_len: ['len', 'steps'], arp_gate: ['level', 'pct'],
    arp_density: ['density', 'pct'], arp_vary: ['chance', 'pct'], arp_ratchet: ['chance', 'pct'],
    shape: ['wave', 'pct'], detune: ['detune', 'cents'], sub: ['level', 'pct'], noise: ['density', 'pct'],
    cutoff: ['filter', 'hz'], reso: ['reso', 'pct'], filter_type: ['type', 'type'], drive: ['drive', 'pct'],
    amp_a: ['attack', 'att'], amp_d: ['env', 'dec'], amp_s: ['level', 'pct'], amp_r: ['env', 'dec'],
    filt_amt: ['bipenv', 'oct4'], filt_a: ['attack', 'att'], filt_d: ['env', 'dec'],
    pitch_amt: ['bipenv', 'semi'], pitch_d: ['env', 'pdec'], glide: ['lfo', 'glide'],
    tempo: ['tempo', 'bpm'], swing: ['swing', 'pct'], root: ['note', 'note'], octave: ['range', 'octs'], volume: ['level', 'pct'],
    macro1: ['tail', 'pct'], macro2: ['lfo', 'pct'], macro3: ['tail', 'pct'], macro4: ['drive', 'pct'],
    rhy_rotate: ['range', 'int'], rhy_chance: ['chance', 'pct'], note_shift: ['range', 'int'], seed: ['range', 'name'],
    fx_drive: ['drive', 'pct'], fx_tone: ['bipenv', 'bip'], cho_rate: ['lfo', 'chz'], cho_depth: ['detune', 'pct'], cho_mix: ['level', 'pct'],
    dly_fb: ['env', 'pct'], dly_damp: ['level', 'dhz'], dly_mix: ['level', 'pct'], rev_send: ['level', 'pct'], duck_amt: ['env', 'pct'],
    rev_size: ['len', 'pct'], rev_decay: ['env', 'sec'], rev_damp: ['level', 'rhz'], rev_pre: ['attack', 'ms'], rev_width: ['density', 'pct'], rev_return: ['level', 'pct'], glue: ['drive', 'pct'], ceiling: ['level', 'db'],
    drum_tune: ['detune', 'sint'], drum_decay: ['env', 'pct'], drum_tone: ['filter', 'pct'], drum_snap: ['attack', 'pct'],
    v_flow: ['attack', 'pct'], v_pulse: ['env', 'pct'], v_motion: ['lfo', 'pct'],
    v_presence: ['level', 'pct'], v_response: ['attack', 'pct'], v_cpulse: ['env', 'pct'], v_detail: ['wave', 'pct'],
    v_size: ['size', 'times'], v_alpha: ['level', 'pct'], v_tilt: ['tilt', 'angle'], v_zoom: ['size', 'times'], v_persp: ['persp', 'pct'], v_orbit: ['lfo', 'pct']
  };
  (function () {
    KINDS.tune = ['detune', 'scents'];
    for (var l = 1; l <= POOL_LFOS; l++) { var p = 'lfo' + l + '_'; KINDS[p + 'rate'] = ['lfo', 'lhz']; KINDS[p + 'div'] = ['lfo', 'name']; KINDS[p + 'phase'] = ['phase', 'deg360']; }
    for (var m = 5; m <= POOL_MACROS; m++) KINDS['macro' + m] = KINDS['macro' + ((m - 1) % 4 + 1)];
    for (var r = 1; r <= POOL_ROUTES; r++) KINDS['route' + r + '_amt'] = ['bipenv', 'bip'];
  })();
  function specOf(id) {
    var d = ORDER[id]; if (!d) return null;
    var kk = KINDS[d.s] || ['level', 'pct'];
    if (d.s === 'cho_rate' && vibrato(d.slot)) kk = ['lfo', 'vhz']; // Vibrato's rate keeps the wide curve
    return { id: id, kind: kk[0], fmt: kk[1], min: d.min, max: d.max, steps: d.steps, def: d.def, label: d.n, log: !!LOGP[d.s], names: d.names };
  }
  /* knobs that turn by ratios, not steps (Zoom 0.5x .. 16x: 1x a fifth of the way round, each turn of the wheel the
     same percentage anywhere) */
  var LOGP = { v_zoom: 1 };

  /* ---------------------------------------------------------- telemetry */
  // lf: the pool's LFOs that ran {index: value}; rt: the live routes' target positions {route: 0..1}
  var F = { seq: 0, beat: 0, bpm: 0, playing: 0, host: 0, cr: 9, cd: 0, cq: 1, pp: -1, pl: 0, km: 0, cm: 0, sr: 0, bl: 0, mr: 0, mp: 0, gg: 0, lg: 0, lf: {}, rt: {}, idleMs: 0, evalMs: 0 };
  var LFO_HIST = 240, LH = { h: [], pos: 0 }; // each pool LFO's last four seconds
  for (var lh0 = 0; lh0 < POOL_LFOS; lh0++) LH.h.push(new Float32Array(LFO_HIST));
  var MH = { rms: new Float32Array(240), peak: new Float32Array(240), pos: 0 }; // the master meter's last four seconds
  var Q = [], LAT = 20, lastBurstAt = 0, burstGap = 10;
  var stats = { ticks: 0, frames: 0, tickT: [], tickMs: 0 };
  var CH = {};
  // wave: one period of the output, scaled to its peak (-1..1); ws: that peak (the channel's level in it)
  for (var s0 = 0; s0 < SLOTS; s0++) CH[s0] = { hue: null, sat: 92, amp: 0, note: 57, pc: 9, bytes: new Uint8Array(128), wave: new Float32Array(128), ws: 0, active: 0, muted: 0, hit: 0, hitSeed: 0, step: 0, len: 0, rms: 0, peak: 0, glow: 0,
                                                 notes: [], vnotes: [], sn: null, dkAcc: 0, dkHist: new Float32Array(240), dkPos: 0 };

  /* The stage's own controls: parameters of the page, not the engine. They live in the same table (ids from
     VBASE up, past the engine's 2112) so knobs, chips, drags and resets work on them unchanged, but they never
     go to the host: they are kept with the rest of the page's state in localStorage. Their continuous knobs are
     modulation targets as the engine's are (a route names the id; the page applies it: pageMod). New ones go at the end of
     their list (an id is its place in it). The camera's are here too (see the camera, below the motion layer);
     Yaw and Lie have no knob. */
  var VBASE = 4096, VSLOT = VBASE + 16, VSTRIDE = 8;
  var VGLOBAL = [['Flow', 'v_flow', 0, 1, 0.6, 0], ['Pulse', 'v_pulse', 0, 1, 0.4, 0],
                 ['Pulse every', 'v_pdiv', 0, 3, 0, 4, ['1 beat', '2 beats', '1 bar', '2 bars']], ['Motion', 'v_motion', 0, 1, 0.4, 0],
                 ['Tilt', 'v_tilt', -90, 90, 17, 0], ['Zoom', 'v_zoom', 0.5, 16, 1, 0], ['Perspective', 'v_persp', 0, 1, 0.44, 0], ['Orbit', 'v_orbit', 0, 1, 0.4, 0],
                 ['Yaw', 'v_yaw', -180, 180, 0, 0], ['Lie', 'v_lie', 0, 1, 0, 0]];
  var VCHAN = [['Presence', 'v_presence', 0, 1, 0.5, 0], ['Response', 'v_response', 0, 1, 0.5, 0], ['Pulse', 'v_cpulse', 0, 1, 0.7, 0], ['Detail', 'v_detail', 0, 1, 0.75, 0],
               ['Size', 'v_size', 0.5, 1.5, 1, 0], ['Alpha', 'v_alpha', 0, 1, 1, 0]];
  /* Orbit turns the form at ORBIT_K * Orbit^2 rad/s: Orbit 0.4 is the turn the default Motion (0.4) gave before
     the camera took the turn over from Motion */
  var ORBIT_K = 0.4737;
  /* The stage FX and each channel's particles (2026-10-06): blocks of their own past the first ones (those are
     full: VGLOBAL may not pass 16, VCHAN not 8), so no saved id moves. A state saved before them has none of
     these ids and takes the defaults: every effect Off, every channel Surface, the stage exactly as it was.
     VSTRUCT: switches that change which knobs the panel shows. */
  var VFXBASE = VBASE + 256, VPSLOT = VBASE + 512, VPSTRIDE = 8;
  /* the color maps (matplotlib's, 9 stops each; Rainbow: the hue circle); magma and inferno start near black, so
     a channel at their start would vanish: those begin a little in */
  var COLMAP_NAMES = ['Viridis', 'Magma', 'Plasma', 'Inferno', 'Turbo', 'Rainbow', 'Cool-warm'];
  var COLMAPS = ['440154472d7b3b528b2c728e21918c28ae805ec962addc30fde725', '0000041d114751127c832681b73779e75263fc8961fec488fcfdbf',
                 '0d08874c02a17e03a8aa2395cc4778e66c5cf89540fdc527f0f921', '000004210c4a57106e8a226abc3754e45a31f98e09f9cb35fcffa4',
                 '30123b466be328bceb32f298a4fc3ceecf3afb7e21d02f057a0403', null, '3b4cc06282ea8db0feb9d0f9dddcdcf5c4acf4987add5f4bb40426'];
  var COLMAP_LO = [0, 0.22, 0, 0.22, 0, 0, 0];
  /** Map m at t (0..1) -> [r, g, b] 0..1. */
  function colMap(m, t) {
    t = Math.max(0, Math.min(1, t));
    if (!COLMAPS[m]) return Knobs.hslRgb(t * 330, 85, 58); // Rainbow: red round to magenta
    t = COLMAP_LO[m] + (1 - COLMAP_LO[m]) * t;
    var hx = COLMAPS[m], x = t * 8, i = Math.min(7, Math.floor(x)), f = x - i, out = [];
    for (var ch = 0; ch < 3; ch++) {
      var a = parseInt(hx.substr(i * 6 + ch * 2, 2), 16), b = parseInt(hx.substr(i * 6 + 6 + ch * 2, 2), 16);
      out.push((a + (b - a) * f) / 255);
    }
    return out;
  }
  var OFFON = ['Off', 'On'];
  var VFX = [['Mirror', 'fx_mir', 0, 1, 0, 2, OFFON], ['Divisions', 'fx_mir_div', 2, 8, 2, 7], ['Opacity', 'fx_mir_op', 0, 1, 0.5, 0], ['Original on top', 'fx_mir_top', 0, 1, 1, 2, OFFON],
             ['Feedback', 'fx_fb', 0, 1, 0, 2, OFFON], ['Amount', 'fx_fb_amt', 0, 1, 0.5, 0], ['Zoom', 'fx_fb_zoom', -1, 1, 0, 0], ['Rotate', 'fx_fb_rot', -1, 1, 0, 0],
             ['Drift X', 'fx_fb_dx', -1, 1, 0, 0], ['Drift Y', 'fx_fb_dy', -1, 1, 0, 0], ['Hue', 'fx_fb_hue', -1, 1, 0, 0], ['Blend', 'fx_fb_blend', 0, 1, 0, 2, ['Max', 'Add']],
             ['Kick', 'fx_fb_kick', 0, 1, 0, 0],
             ['Tracer', 'fx_tr', 0, 1, 0, 2, OFFON], ['Frames', 'fx_tr_n', 2, 180, 6, 179], ['Spacing', 'fx_tr_gap', 1, 8, 3, 8], ['Fade', 'fx_tr_fade', 0, 1, 0.6, 0],
             // the stage's colors (2026-10-06): Pitch (each channel its note's hue, as always), Palette (a color map
             // spread over the channels, the louder further along), Cycle (the map running by, each channel offset)
             ['Color', 'col_mode', 0, 2, 0, 3, ['Pitch', 'Palette', 'Cycle']], ['Map', 'col_map', 0, 6, 0, 7, COLMAP_NAMES],
             ['Spread', 'col_spread', 0, 1, 0.8, 0], ['Level', 'col_lvl', 0, 1, 0.3, 0], ['Speed', 'col_speed', 0, 1, 0.35, 0],
             ['Bloom', 'fx_bl', 0, 1, 0, 2, OFFON], ['Strength', 'fx_bl_str', 0, 1, 0.5, 0], ['Radius', 'fx_bl_rad', 0, 1, 0.5, 0], ['Threshold', 'fx_bl_thr', 0, 1, 0.3, 0]];
  var VPCHAN = [['Render', 'v_mode', 0, 2, 0, 3, ['Surface', 'Particles', 'Both']], ['Density', 'v_pdens', 0, 1, 0.5, 0], ['Scatter', 'v_pscat', 0, 1, 0.08, 0],
                ['Jitter', 'v_pjit', 0, 1, 0.3, 0], ['Color jitter', 'v_pcol', 0, 1, 0.25, 0], ['Point size', 'v_psize', 1, 8, 2.5, 0], ['Softness', 'v_psoft', 0, 1, 0.6, 0],
                ['Fill', 'v_pfill', 0, 1, 0, 0]];
  /* The scope (2026-10-13): the stage as an oscilloscope, one lane per channel. A block of its
     own past the particles' (VSCBASE, room for 32), so no saved id moves; a state saved before it takes the
     defaults: the 3D form, as it was. */
  var VSCBASE = VBASE + 768;
  var VSCOPE = [['Stage', 'v_stage', 0, 1, 0, 2, ['3D form', 'Scope']], ['Cycles', 'sc_cyc', 1, 4, 2, 4, ['1', '2', '3', '4']], ['Height', 'sc_gain', 0.25, 2, 1, 0],
                ['Level', 'sc_lvl', 0, 1, 0, 2, ['Normalized', 'True']], ['Line width', 'sc_lw', 1, 8, 2, 0], ['Glow', 'sc_glow', 0, 1, 0.35, 0],
                ['Smoothing', 'sc_smooth', 0, 1, 0.4, 0], ['Grid', 'sc_grid', 0, 1, 0, 2, OFFON], ['Layout', 'sc_lay', 0, 1, 0, 2, ['Stacked', 'Overlaid']],
                ['Legend', 'sc_key', 0, 1, 1, 2, OFFON]];
  var VSTRUCT = { fx_mir: 1, fx_fb: 1, fx_tr: 1, fx_bl: 1, v_mode: 1, col_mode: 1, v_stage: 1 };
  var VKINDS = { fx_mir_div: ['len', 'int'], fx_mir_op: ['level', 'pct'], fx_fb_amt: ['env', 'pct'], fx_fb_zoom: ['bipenv', 'bip'], fx_fb_rot: ['bipenv', 'bip'],
                 fx_fb_dx: ['pan', 'bip'], fx_fb_dy: ['bipenv', 'bip'], fx_fb_hue: ['bipenv', 'bip'], fx_fb_kick: ['attack', 'pct'], fx_tr_n: ['density', 'int'],
                 col_spread: ['detune', 'pct'], col_lvl: ['level', 'pct'], col_speed: ['lfo', 'pct'], v_pfill: ['density', 'pct'],
                 fx_bl_str: ['level', 'pct'], fx_bl_rad: ['size', 'pct'], fx_bl_thr: ['env', 'pct'],
                 fx_tr_gap: ['len', 'frames'], fx_tr_fade: ['env', 'pct'], v_pdens: ['density', 'pct'], v_pscat: ['chance', 'pct'], v_pjit: ['lfo', 'pct'],
                 v_pcol: ['detune', 'pct'], v_psize: ['size', 'px'], v_psoft: ['attack', 'pct'],
                 sc_gain: ['size', 'times'], sc_lw: ['size', 'px'], sc_glow: ['level', 'pct'], sc_smooth: ['attack', 'pct'] };
  for (var vk0 in VKINDS) KINDS[vk0] = VKINDS[vk0];
  function vgid(sym) {
    for (var i = 0; i < VGLOBAL.length; i++) if (VGLOBAL[i][1] === sym) return VBASE + i;
    for (i = 0; i < VFX.length; i++) if (VFX[i][1] === sym) return VFXBASE + i;
    for (i = 0; i < VSCOPE.length; i++) if (VSCOPE[i][1] === sym) return VSCBASE + i;
    return -1;
  }
  /** Page state saved before the camera (2026-10-06) has no Orbit: Motion turned the form then, at
      0.1 * 4^(2 Motion - 1) rad/s; Orbit takes that turn over (Motion keeps the waves' drift). */
  function migrateVp(vp) {
    var o = vgid('v_orbit'), m = vgid('v_motion');
    if (vp && vp[o] == null && vp[m] != null) vp[o] = Math.min(1, Math.sqrt(0.1 * Math.pow(4, 2 * +vp[m] - 1) / ORBIT_K));
  }
  function isHost(id) { return id < VBASE; }
  function addVirtual() {
    function add(id, d, slot) {
      var o = { id: id, n: d[0], s: d[1], slot: slot, min: d[2], max: d[3], def: d[4], steps: d[5], flags: 0 };
      if (d[6]) o.names = d[6];
      ORDER[id] = o; if (slot < 0) DEFS[o.s] = o; // per slot: vsid(), not sid()
      if (P[id] == null) P[id] = ui.vp[id] != null ? ui.vp[id] : o.def; // ui: the page's state, loaded below before the host's 'ready' answer
    }
    VGLOBAL.forEach(function (d, i) { add(VBASE + i, d, -1); });
    for (var s = 0; s < SLOTS; s++) VCHAN.forEach(function (d, i) { add(VSLOT + VSTRIDE * s + i, d, s); });
    VFX.forEach(function (d, i) { add(VFXBASE + i, d, -1); });
    for (s = 0; s < SLOTS; s++) VPCHAN.forEach(function (d, i) { add(VPSLOT + VPSTRIDE * s + i, d, s); });
    VSCOPE.forEach(function (d, i) { add(VSCBASE + i, d, -1); });
  }
  function vsid(slot, sym) {
    for (var i = 0; i < VCHAN.length; i++) if (VCHAN[i][1] === sym) return VSLOT + VSTRIDE * slot + i;
    for (i = 0; i < VPCHAN.length; i++) if (VPCHAN[i][1] === sym) return VPSLOT + VPSTRIDE * slot + i;
    return -1;
  }
  function vval(slot, sym) { return val(vsid(slot, sym)); }

  window.ammo = {
    params: function (list) {
      DEFS = {}; ORDER = {}; SLOTP = {};
      for (var i = 0; i < list.length; i++) {
        var d = list[i]; ORDER[d.id] = d;
        if (d.slot < 0) DEFS[d.s] = d; else if (d.slot === 0) SLOTP[d.s] = d.id - SLOT_BASE;
      }
      for (var k in ORDER) { if (P[k] == null) P[k] = ORDER[k].def; if (/^route\d+_pol$/.test(ORDER[k].s)) ORDER[k].names = ['Unipolar', 'Bipolar']; }
      if (!MINI) addVirtual(); // the stage's own parameters: Mini has no stage
      structure = true;
    },
    values: function (pairs) { for (var i = 0; i < pairs.length; i++) P[pairs[i][0]] = pairs[i][1]; rtDirty = true; structure = true; },
    param: function (id, v) {
      P[id] = v; var d = ORDER[id];
      if (isRoute(id)) rtDirty = true;
      if (d && isStruct(d.s)) structure = true; else dirty = true;
    },
    /* a preset chosen (not the first list, nor a patch file: that brings its own page line) takes the macros' names
       back to Bloom, Motion, Space, Grit */
    presets: function (list, cur, file) {
      if (PRESETS.length && cur >= 0 && cur !== presetNow) { ui.mnames = {}; saveUi(); structure = true; }
      PRESETS = list; presetNow = cur; patchFile = file || ''; dirty = true;
    },
    /* a patch file's page state (a JSON string): looks, the stage's and each channel's visual settings, the
       camera (eased there, not jumped; a file from before the camera gets its defaults and Motion's turn) */
    page: function (json) {
      var o; try { o = typeof json === 'string' ? JSON.parse(json) : json; } catch (e) { return; }
      if (!o || typeof o !== 'object') return;
      if (MINI) { // the stage's settings kept as they came (pageState writes them back); the names and pins taken
        ui.page = o;
        ui.mnames = o.mnames && typeof o.mnames === 'object' ? o.mnames : {};
        if (Array.isArray(o.pins)) ui.pins = cleanPins(o.pins);
        if (o.bar != null) ui.bar = !!o.bar;
        saveUi(); structure = true; return;
      }
      ui.looks = o.looks && typeof o.looks === 'object' ? o.looks : {};
      ui.vp = o.vp && typeof o.vp === 'object' ? o.vp : {};
      migrateVp(ui.vp);
      if (o.view === 'nested' || o.view === 'merged') ui.view = o.view;
      ui.mnames = o.mnames && typeof o.mnames === 'object' ? o.mnames : {};
      // the pin bar as saved (a file from before it keeps the bar as it is)
      if (Array.isArray(o.pins)) ui.pins = cleanPins(o.pins);
      if (o.bar != null) ui.bar = !!o.bar;
      for (var id in ORDER) if (!isHost(+id)) P[id] = ui.vp[id] != null ? +ui.vp[id] : ORDER[id].def;
      camAim(gval('v_yaw') * DEG, 0.6);
      saveUi(); structure = true;
    },
    rec: function (o) {
      var was = rec.on;
      rec.on = !!o.on;
      if (rec.on) rec.t0 = performance.now() - (o.secs || 0) * 1000;
      else if (was && o.path) toast((o.ok === 0 ? 'Recording failed: ' : 'Recorded ' + fmtTime(o.secs || 0) + ' to ') + o.path + (o.dropped ? ' (' + o.dropped + ' frames lost)' : ''));
      dirty = true;
    },
    toast: function (text) { toast(text); },
    info: function (o) { INFO = o; },
    outputs: function (o) { if (MINI) return; var first = !OUTS; OUTS = o; if (first) structure = true; else fillOutputs(); },
    tick: function (list, host) {
      var a = performance.now(), n = list.length, i;
      if (host) { F.idleMs = host.idleMs; F.evalMs = host.evalMs; }
      if (lastBurstAt) burstGap = burstGap * 0.9 + Math.min(50, a - lastBurstAt) * 0.1;
      lastBurstAt = a;
      LAT = Math.max(8, Math.min(60, burstGap * 1.5));
      for (i = 0; i < n; i++) {
        var f = list[i], blockMs = f.sr > 0 ? 1000 * f.bl / f.sr : 2.7;
        Q.push({ t: a - (n - 1 - i) * blockMs, f: f });
        if (f.cpu != null) CPU.push({ t: a, c: f.cpu, k: f.cpk != null ? f.cpk : f.cpu, ms: blockMs }); // cpk: Mini's frames fold blocks, their worst
        // every note into its channel's history as it arrives (sampling one frame per paint would miss most),
        // stamped for when the page shows that frame
        for (var hd in f.ch) if (f.ch[hd].dk) CH[hd].dkAcc = Math.max(CH[hd].dkAcc, f.ch[hd].dk); // the duck's deepest since the last paint
        for (var hs in f.ch) if (f.ch[hs].h) { var hn = CH[hs].notes; hn.push({ n: f.ch[hs].n, t: (a - (n - 1 - i) * blockMs + LAT) / 1000 }); if (hn.length > 48) hn.shift(); }
      }
      while (Q.length > 64) Q.shift();
      while (CPU.length && (a - CPU[0].t > 1000 || CPU.length > 4000)) CPU.shift();
      stats.ticks++; stats.frames += n; stats.tickT.push(a); if (stats.tickT.length > 2400) stats.tickT.shift();
      stats.tickMs = stats.tickMs * 0.9 + (performance.now() - a) * 0.1;
    }
  };

  /* the CPU meter: the engine's render time per block over the block's length, the last second of frames; the
     mean weighs each block by its length (render time over real time), the peak is the worst block (a frame that
     folds several blocks, Mini's, brings their worst as cpk) */
  var CPU = [];
  function cpuNow() {
    var busy = 0, real = 0, peak = 0;
    for (var i = 0; i < CPU.length; i++) { busy += CPU[i].c * CPU[i].ms; real += CPU[i].ms; peak = Math.max(peak, CPU[i].k); }
    return real > 0 ? { mean: busy / real, peak: peak } : null;
  }
  function cpuPct(x) { var p = 100 * x; return (p < 10 ? p.toFixed(1) : Math.round(p)) + '%'; }
  function cpuHtml() {
    return '<div class="cpu" title="CPU: the engine\'s time rendering each block against the block\'s length in real time, over the last second ' +
      '(mean, and the peak block). Near 100% the audio would break up."><span class="lab">CPU</span><span class="cpun" data-cpu="mean"></span>' +
      '<span class="cpubar"><i data-cpu="bar"></i><b data-cpu="pk"></b></span><span class="cpupk" data-cpu="peak"></span></div>';
  }
  function refreshCpu() {
    var c = cpuNow(), mean = c ? cpuPct(c.mean) : '–', peak = c ? 'pk ' + cpuPct(c.peak) : '', lvl = !c ? '' : c.peak >= 0.9 ? ' hot' : c.peak >= 0.7 ? ' warm' : '';
    document.querySelectorAll('.cpu').forEach(function (el) {
      if (el.className !== 'cpu' + lvl) el.className = 'cpu' + lvl;
      var m = el.querySelector('[data-cpu="mean"]'), p = el.querySelector('[data-cpu="peak"]');
      if (m.textContent !== mean) m.textContent = mean;
      if (p.textContent !== peak) p.textContent = peak;
      el.querySelector('[data-cpu="bar"]').style.width = (c ? 100 * Math.min(1, c.mean) : 0) + '%';
      el.querySelector('[data-cpu="pk"]').style.left = (c ? 100 * Math.min(1, c.peak) : 0) + '%';
    });
  }

  function sampleTelemetry(now, dt) {
    if (!Q.length) return;
    var td = now - LAT;
    while (Q.length > 1 && Q[1].t <= td) Q.shift();
    var f = Q[0].f;
    if (f.s !== F.seq) {
      F.seq = f.s; F.beat = f.b; F.bpm = f.t; F.playing = f.p; F.host = f.h; F.cr = f.cr; F.cd = f.cd; F.cq = f.cq; F.pp = f.pp; F.pl = f.pl;
      F.km = f.km; F.cm = f.cm; F.sr = f.sr; F.bl = f.bl; F.mr = f.mr; F.mp = f.mp; F.gg = f.gg || 0; F.lg = f.lg || 0; F.lf = f.lf || {}; F.rt = f.rt || {};
      for (var s = 0; s < SLOTS; s++) {
        var c = CH[s], fc = f.ch[s];
        c.hit = 0;
        if (!fc) { c.active = 0; c.muted = 0; c.amp *= 0.9; continue; }
        c.active = fc.a; c.muted = fc.m ? 1 : 0; c.note = fc.n; c.pc = fc.pc; c.amp = fc.e; c.rms = fc.r; c.peak = fc.k; c.hit = fc.h; c.hitSeed = fc.sd; c.step = fc.st; c.len = fc.ln; c.vnotes = fc.v || []; c.sn = fc.sn || null;
        c.ws = fc.ws || 0;
        if (fc.w && fc.w.length) {
          var bin = atob(fc.w), m = Math.min(bin.length, 128);
          for (var i = 0; i < m; i++) { var b = bin.charCodeAt(i), sgn = b >= 128 ? b - 256 : b; c.wave[i] = sgn / 127; c.bytes[i] = (b + 128) & 255; }
        }
      }
    }
    for (var s2 = 0; s2 < SLOTS; s2++) {
      var c2 = CH[s2], target = drumHue(s2);
      if (target == null) target = Knobs.hueOf(c2.pc);
      if (c2.hue == null) c2.hue = target;
      else { var dd = ((target - c2.hue + 540) % 360) - 180; c2.hue = (c2.hue + dd * (1 - Math.exp(-dt / 0.12)) + 360) % 360; }
      c2.glow = c2.hit ? 1 : c2.glow * Math.exp(-dt / 0.35);
      if (s2 === 0) { MH.rms[MH.pos] = F.mr; MH.peak[MH.pos] = F.mp; MH.pos = (MH.pos + 1) % MH.rms.length; }
      c2.dkHist[c2.dkPos] = c2.dkAcc; c2.dkPos = (c2.dkPos + 1) % c2.dkHist.length; c2.dkAcc = 0;
    }
    for (var l = 0; l < POOL_LFOS; l++) LH.h[l][LH.pos] = F.lf[l] != null ? F.lf[l] : 0;
    LH.pos = (LH.pos + 1) % LFO_HIST;
  }

  /* a drum with no pitch to follow (Fixed) takes a color of its own model, so a kit is not all one hue */
  var DRUM_HUES = [10, 45, 190, 215, 300, 28, 160, 265];
  function drumHue(s) {
    if (!ORDER[0] || Math.round(sval(s, "inst")) !== 1 || Math.round(sval(s, "rhy_pitch")) !== 0 || Math.round(sval(s, "gen")) !== 4) return null;
    return DRUM_HUES[Math.round(sval(s, "drum_model"))] || 0;
  }

  /* ------------------------------------------------------------- state */
  // dock: which edge the panel sits on; rail: collapsed to a strip; open: the channels open in the accordion;
  // grps / secs: per channel, the groups (gen, inst, fx, vg) and the sections open in it; everything shut at first.
  // single: one at a time (the default): opening a channel, a group or a section closes the others beside it, and
  // switching it on shuts everything but `last`, the last thing opened {ch, grp, sec}. sel: the channel last opened
  // (focus, the visual tab). tgrp: the Visual tab's groups open (shut at first, One at a time as the channels'),
  // lastT the last of them opened. Kept per viewer in localStorage: the page's own, not the patch's. uiv: what was kept
  // before 2 (any number open, groups open unless folded, a channel's pattern open at first) is not taken back.
  // pins: the ids pinned to the bar, in its order; bar: the bar shown (both kept in a patch file's page line too)
  var PIN_MAX = 16;
  var ui = { dock: 'left', tab: 'channels', rail: false, sel: 0, open: {}, secs: {}, grps: {}, last: null, tgrp: {}, lastT: null, single: true, view: 'nested', focus: false, looks: {}, vp: {}, mnames: {},
             pins: [], bar: false, uiv: 2, page: null };
  var KEEP = ['dock', 'tab', 'rail', 'open', 'secs', 'grps', 'last', 'tgrp', 'lastT', 'single', 'view', 'focus', 'looks', 'vp', 'mnames', 'pins', 'bar', 'uiv'];
  // Mini: no stage, so no dock, rail, Visual tab or looks; page: the last patch file's page line, kept whole for its save
  if (MINI) KEEP = ['tab', 'open', 'secs', 'grps', 'last', 'single', 'mnames', 'pins', 'bar', 'uiv', 'page'];
  var UI_KEY = MINI ? 'ammo.mini.ui' : 'ammo.ui';
  /* each section's group */
  var GRP_OF = { pat: 'gen', pitch: 'gen', chord: 'gen', drum: 'inst', osc: 'inst', filt: 'inst', amp: 'inst',
                 drive: 'fx', cho: 'fx', dly: 'fx', rev: 'fx', duck: 'fx', vis: 'vg', part: 'vg' };
  function secsOf(s) { return ui.secs[s] || {}; }
  function isOpen(s) { return !!ui.open[s]; }
  function grpOpen(s, key) { return !!(ui.grps[s] && ui.grps[s][key]); }
  try {
    var saved = JSON.parse(localStorage.getItem(UI_KEY) || '{}');
    if (saved.uiv !== ui.uiv) ['open', 'secs', 'shut', 'single', 'uiv'].forEach(function (k) { delete saved[k]; });
    KEEP.forEach(function (k) { if (saved[k] != null) ui[k] = saved[k]; });
  } catch (e) { /* no storage */ }
  migrateVp(ui.vp);
  ui.pins = cleanPins(ui.pins);
  if (MINI) {
    if (ui.tab !== 'conductor' && ui.tab !== 'modulation') ui.tab = 'channels';
    for (var o0 in ui.open) if (ui.open[o0]) ui.sel = +o0; // an open channel is the one selected
  }
  function saveUi() { camKeep(); try { var o = {}; KEEP.forEach(function (k) { o[k] = ui[k]; }); localStorage.setItem(UI_KEY, JSON.stringify(o)); } catch (e) { /* no storage */ } }

  var structure = true, dirty = true, drag = null, hover = null, T = 0, t0 = performance.now(), glStatus = 'init', lastRefresh = 0, reveal = false, stageX = null;
  var LOOKS = [['shell', 'SHELL'], ['ripple', 'RIPPLE'], ['breath', 'BREATH'], ['glow', 'GLOW']];
  var HINT = { shell: 'Its waveform as a glass surface. Nested: its own shell. Merged: part of one surface.',
               ripple: 'Each note strikes the surface from its place on the circle of fifths; one crest travels across it and fades.',
               breath: 'No surface of its own: the whole form swells and stretches with it.',
               glow: 'Soft light inside the glass, in its color.' };
  var VIS_HINT = 'Presence: how much the music moves it. Response: how quickly it follows, against Flow. Pulse: how much the beat swells it. ' +
                 'Detail: a soft round form, or every corner of its wave. Size: its radius, 0.5x to 1.5x (merged: its weight in the surface). ' +
                 'Alpha: how strongly it shows, so it can sit behind the others without being hidden.';

  function activeSlots() { var out = []; for (var s = 0; s < SLOTS; s++) if (sval(s, 'gen') > 0) out.push(s); return out; }
  function slotName(s) { return 'Channel ' + (s + 1); }
  var GEN_ARP = 1, GEN_CHORD = 2, GEN_BASS = 3, GEN_RHYTHM = 4, INST_DRUM = 1;
  var STRUCT = { gen: 1, chord_rhythm: 1, inst: 1, cho_mode: 1 }; // parameters that change which sections (or knobs) a channel has
  /** A parameter whose change rebuilds the panel: STRUCT's, and the pool's that add or remove rows (an LFO's sync
      swaps its Rate and Division knobs; in use; a route's source and target). */
  function isStruct(sym) { return !!STRUCT[sym] || /^(lfo\d+_(on|sync)|macro\d+_on|route\d+_(src|target))$/.test(sym); }
  function isDrum(s) { return Math.round(sval(s, 'inst')) === INST_DRUM; }
  function hitsOf(s) { var len = Math.round(sval(s, 'arp_len')); return Math.floor(sval(s, 'arp_density') * len + 0.5) + '/' + len; }
  function genOf(s) { return Math.round(sval(s, 'gen')); }
  function holds(s) { return Math.round(sval(s, 'chord_rhythm')) === 0; }
  function chordArp(s) { return Math.round(sval(s, 'chord_rhythm')) === 2; }
  function vibrato(s) { return s >= 0 && Math.round(sval(s, 'cho_mode')) === 2; } // all wet: MIX does nothing
  function chorusOn(s) { return sval(s, 'cho_on') >= 0.5 && (vibrato(s) || sval(s, 'cho_mix') > 0); }
  /** A channel's row subtitle: what its generator plays, in a line. */
  /** The folded PATTERN line's tail: Rotate, Shift, Count, Seed where they are not the defaults (and apply). */
  function notesSum(s) {
    var gen = genOf(s), arpish = gen === GEN_ARP || (gen === GEN_CHORD && chordArp(s)), out = '';
    var rot = Math.round(sval(s, 'rhy_rotate')), sh = Math.round(sval(s, 'note_shift')), cnt = Math.round(sval(s, 'note_count')), sd = Math.round(sval(s, 'seed'));
    if (rot && gen !== GEN_RHYTHM) out += ' · rotate ' + rot;
    if (arpish && sh) out += ' · shift ' + sh;
    if (arpish && cnt && cnt !== (gen === GEN_CHORD ? 2 : 1)) out += ' · count ' + (cnt === 2 ? 'hits' : 'steps');
    if (sd && !(gen === GEN_CHORD && !chordArp(s))) out += ' · seed ' + sd;
    return out;
  }
  function slotSub(s) {
    var div = Knobs.fmt('div', sval(s, 'arp_div'));
    switch (genOf(s)) {
      case GEN_ARP: return nameOf(sid(s, 'arp_mode')) + ' · ' + nameOf(sid(s, 'arp_pool')) + ' · ' + div;
      case GEN_CHORD: return 'CHORD · ' + nameOf(sid(s, 'chord_notes')) + ' · ' + nameOf(sid(s, 'chord_voicing')) + ' · ' +
        (holds(s) ? 'HOLD' : chordArp(s) ? 'ARP ' + nameOf(sid(s, 'arp_mode')).toUpperCase() + ' · ' + div : div);
      case GEN_BASS: return 'BASS · ' + nameOf(sid(s, 'bass_pattern')) + ' · ' + div;
      case GEN_RHYTHM: return 'RHYTHM · ' + (isDrum(s) ? nameOf(sid(s, 'drum_model')) : 'SYNTH') + ' · ' + hitsOf(s) + ' · ' + div;
      default: return 'OFF';
    }
  }
  function lookOf(s) { return ui.looks[s] || 'shell'; }
  function keyName() { var sc = ORDER[gid('scale')]; return Knobs.NOTES[Math.round(gval('root')) % 12] + ' ' + (sc && sc.names ? sc.names[Math.round(gval('scale'))].toLowerCase() : ''); }
  function chordName() {
    var sc = Math.round(gval('scale')), pc = F.cr;
    return Knobs.NOTES[pc] + (sc >= 10 ? ORDER[gid('scale')].names[sc].toLowerCase() : QUAL[F.cq] || '');
  }

  function setParam(id, v, gesture) {
    var d = ORDER[id]; if (!d) return;
    if (d.steps > 0) v = Math.round(v);
    v = Math.max(d.min, Math.min(d.max, v));
    if (!isHost(id)) { P[id] = v; ui.vp[id] = v; saveUi(); if (VSTRUCT[d.s]) structure = true; else dirty = true; return; }
    if (gesture) send('edit ' + id + ' 1');
    P[id] = v; send('set ' + id + ' ' + (d.steps > 0 ? v.toFixed(0) : v.toFixed(5)));
    if (gesture) send('edit ' + id + ' 0');
    if (isRoute(id)) rtDirty = true;
    if (isStruct(d.s)) structure = true; else dirty = true;
  }

  /* patch files and recording: the host shows the dialogs and writes the files */
  function pageState() {
    if (MINI) { // the page line as it came (desktop's stage settings, untouched), with Mini's names and pins
      var o = {}, p = ui.page && typeof ui.page === 'object' ? ui.page : { v: 1, looks: {}, vp: {}, view: 'nested' };
      for (var k in p) o[k] = p[k];
      o.mnames = ui.mnames; o.pins = ui.pins; o.bar = ui.bar;
      return o;
    }
    camKeep(); return { v: 1, looks: ui.looks, vp: ui.vp, view: ui.view, mnames: ui.mnames, pins: ui.pins, bar: ui.bar };
  }
  function savePatch() { send('save ' + JSON.stringify(pageState())); }
  function openPatch() { send('open'); }
  function toggleRec() { send('record ' + (rec.on ? 0 : 1)); }
  function fmtTime(secs) { var t = Math.max(0, Math.floor(secs)); return Math.floor(t / 60) + ':' + ('0' + t % 60).slice(-2); }
  var elToast = null, toastUntil = 0;
  function toast(text) {
    if (!elToast) { elToast = document.createElement('div'); elToast.id = 'toast'; elUI.appendChild(elToast); }
    elToast.textContent = text; elToast.title = text; elToast.classList.add('show');
    toastUntil = performance.now() + 6000;
  }

  /* --------------------------------------------------------------- DOM */
  var $ = function (id) { return document.getElementById(id); };
  var elStage = $('stage'), elUI = $('ui'), elPanel = $('panel'), elStats = $('stats');
  function esc(s) { return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;'); }
  /* the output device, in our standalone host only (it hands the page its devices; in a DAW the DAW has the
     output): a speaker and the list. The list is asked for again as the pointer comes over it, so a device
     plugged in since shows up. */
  var OUTS = null, outAsked = 0;
  var SPEAKER_SVG = '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">' +
    '<path d="M11 5 6 9H2v6h4l5 4V5z"></path><path d="M15.5 8.5a5 5 0 0 1 0 7"></path><path d="M19 5a10 10 0 0 1 0 14"></path></svg>';
  function outOptions() {
    var o = '<option value=""' + (OUTS.cur ? '' : ' selected') + '>Windows default' + (OUTS.cur ? '' : ' (' + esc(OUTS.name) + ')') + '</option>';
    (OUTS.list || []).forEach(function (d) { o += '<option value="' + esc(d.id) + '"' + (OUTS.cur === d.id ? ' selected' : '') + '>' + esc(d.name) + '</option>'; });
    return o;
  }
  function outputHtml() {
    if (!OUTS) return '';
    return '<label class="outpick" title="Output device: ' + esc(OUTS.name) + ', ' + OUTS.rate + ' Hz">' + SPEAKER_SVG + '<select class="sel outsel" data-out="1">' + outOptions() + '</select></label>';
  }
  function fillOutputs() {
    document.querySelectorAll('select.outsel').forEach(function (el) { if (document.activeElement !== el) el.innerHTML = outOptions(); });
    document.querySelectorAll('.outpick').forEach(function (el) { el.title = 'Output device: ' + OUTS.name + ', ' + OUTS.rate + ' Hz'; });
  }
  document.addEventListener('pointerover', function (e) {
    var t = e.target;
    if (OUTS && t && t.classList && t.classList.contains('outsel') && performance.now() - outAsked > 1000) { outAsked = performance.now(); send('outputs'); }
  });
  /** A knob, its name, and under a knob a route can move, its tags (filled in by refresh(), so a route made mid-drag
      shows its tag at once). */
  function knobCell(id, label, compact) {
    KLAB[id] = label;
    return '<div class="kcell' + (compact ? ' compact' : '') + '"><canvas class="knob" data-id="' + id + '"></canvas><span class="klab">' + esc(label) + '</span>' +
      (isTarget(id) ? '<div class="mtags" data-tags="' + id + '"></div>' : '') +
      '<button class="pinbtn" data-pintog="' + id + '" aria-pressed="' + pinned(id) + '" title="Pin to the bar (or right-click the knob)">' + PIN_SVG + '</button></div>';
  }

  /* ---- the pin bar: up to PIN_MAX knobs and faders, each a copy of the panel's (the same id,
     so dragging, the wheel, double-click, assigning, tags and rings all work on it), labelled by where it lives in
     its channel's or its source's colour; dim while its channel (or LFO, or macro) is off, kept. Right-click one (a
     long press by touch) for Pin / Unpin; drag a pin's label along the bar to move it. */
  var KLAB = {}; // each knob's name as the panel last labelled it
  var PIN_SVG = '<svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round">' +
    '<path d="M9 3h6l-1.2 6.5L17 13H7l3.2-3.5z"></path><path d="M12 13v8"></path></svg>';
  var BAR_SVG = '<svg width="20" height="16" viewBox="0 0 20 16" fill="none" stroke="currentColor" stroke-width="1.5"><rect x="1" y="1" width="18" height="14" rx="2.5"></rect>' +
    '<rect x="3.5" y="9.5" width="13" height="3.5" rx="1" fill="currentColor" stroke="none"></rect></svg>';
  function pinned(id) { return ui.pins.indexOf(id) >= 0; }
  /** Pins as saved: numbers, once each, PIN_MAX at most (ids the table does not have are kept, shown only if they come). */
  function cleanPins(list) {
    var out = [];
    (Array.isArray(list) ? list : []).forEach(function (x) { x = +x; if (isFinite(x) && x >= 0 && out.indexOf(x) < 0 && out.length < PIN_MAX) out.push(x); });
    return out;
  }
  /** A control that can be pinned: any knob or fader but a route's amount (its route can go). */
  function pinnable(id) { return !!ORDER[id] && !isRoute(id); }
  var PIN_GROUPS = [[/^(tempo|octave)$/, 'Time'], [/^root$/, 'Harmony'], [/^v_(flow|pulse|motion)$/, 'Motion'], [/^v_(tilt|zoom|persp|orbit)$/, 'Camera'],
                    [/^fx_mir_/, 'Mirror'], [/^fx_fb_/, 'Feedback'], [/^fx_tr_/, 'Tracer'], [/^fx_bl_/, 'Bloom'], [/^col_/, 'Color'], [/^sc_/, 'Scope']];
  var LFO_KEYS = { rate: 'Rate', div: 'Division', phase: 'Phase' };
  /** Where a pinned control lives, as the bar labels it: "3 · Cutoff", "Bloom", "LFO 2 · Rate", "Space · Size". */
  function pinName(id) {
    var d = ORDER[id]; if (!d) return '?';
    var m = /^macro(\d+)$/.exec(d.s); if (m) return macroName(+m[1] - 1);
    m = /^lfo(\d+)_(\w+)$/.exec(d.s); if (m) return 'LFO ' + m[1] + ' · ' + (LFO_KEYS[m[2]] || d.n);
    var own = TNAMES[d.s] || KLAB[id] || d.n;
    if (d.slot >= 0) return (d.slot + 1) + ' · ' + own;
    if (TNAMES[d.s]) return own;
    for (var i = 0; i < PIN_GROUPS.length; i++) if (PIN_GROUPS[i][0].test(d.s)) return PIN_GROUPS[i][1] + ' · ' + (KLAB[id] || d.n);
    return own;
  }
  /** A pin's control still means something: its channel plays, its LFO or macro is in use. */
  function pinLive(id) {
    var d = ORDER[id]; if (!d) return false;
    if (d.slot >= 0) return sval(d.slot, 'gen') > 0;
    var m = /^macro(\d+)$/.exec(d.s); if (m) return macroOn(+m[1] - 1);
    m = /^lfo(\d+)_/.exec(d.s); if (m) return lfoOn(+m[1] - 1);
    return true;
  }
  /** A pin's source colour (an LFO's or a macro's own knobs), or null (a channel's: its live colour, set each frame). */
  function pinSrc(id) {
    var d = ORDER[id], m = d && /^(?:lfo(\d+)_|macro(\d+)$)/.exec(d.s);
    return m ? (m[1] ? SRC_LFO1 + +m[1] - 1 : SRC_M1 + +m[2] - 1) : 0;
  }
  function pinCell(id) {
    var d = ORDER[id]; if (!d) return '';
    var fader = d.slot >= 0 && isHost(id) && (d.s === 'level' || d.s === 'pan'), src = pinSrc(id), nm = pinName(id);
    return '<div class="pin' + (fader ? ' fader' : '') + (pinLive(id) ? '' : ' off') + '" data-pinc="' + id + '"' + (d.slot >= 0 ? ' data-pslot="' + d.slot + '"' : '') +
      (src ? ' style="' + srcVars(src) + '"' : '') + '><span class="pinlab" title="' + esc(nm) + '. Drag along the bar to move it; right-click the control to unpin it">' + esc(nm) + '</span>' +
      (fader ? '<canvas class="hfader' + (d.s === 'pan' ? ' pan' : '') + '" data-id="' + id + '" title="' + esc(nm) + '"></canvas>' : '<canvas class="knob" data-id="' + id + '"></canvas>') +
      (!fader && isTarget(id) ? '<div class="mtags" data-tags="' + id + '"></div>' : '') +
      '<button class="pinx" data-pintog="' + id + '" title="Unpin from the bar">×</button></div>';
  }
  var elBar = null;
  /** The bar from scratch (with the panel: on structural changes), its scroll kept. */
  function buildBar() {
    if (!elBar) { elBar = document.createElement('div'); elBar.id = 'pinbar'; elUI.appendChild(elBar); }
    var row = elBar.querySelector('.pins'), scroll = row ? row.scrollLeft : 0;
    elBar.style.display = ui.bar ? '' : 'none';
    if (!ui.bar) { if (elBar.firstChild) elBar.innerHTML = ''; return; }
    var cells = ui.pins.map(pinCell).join(''), n = ui.pins.length;
    elBar.innerHTML = '<div class="pinhead"><span class="lab">PINS</span><span class="pincount">' + n + '/' + PIN_MAX + '</span>' +
      '<button class="iconbtn" data-act="bar" title="Hide the bar (B)">×</button></div>' +
      (cells ? '<div class="pins">' + cells + '</div>' : MINI ? '<div class="pinempty">Hold any knob or fader and choose <b>Pin to bar</b>; hold a pinned one to unpin it. ' +
        'Up to ' + PIN_MAX + ', saved with the patch.</div>' : '<div class="pinempty">Right-click any knob or fader and choose <b>Pin to bar</b> (by touch: hold it), ' +
        'or click the pin beside a knob. Up to ' + PIN_MAX + ', saved with the patch. B shows and hides the bar.</div>');
    row = elBar.querySelector('.pins'); if (row) row.scrollLeft = scroll;
  }
  function toggleBar() { ui.bar = !ui.bar; saveUi(); structure = true; }
  /** Pin or unpin; pinning shows the bar. Past PIN_MAX, a word instead. */
  function togglePin(id) {
    var i = ui.pins.indexOf(id);
    if (i >= 0) ui.pins.splice(i, 1);
    else if (ui.pins.length >= PIN_MAX) { toast('The bar is full (' + PIN_MAX + ' pins): unpin one first'); return; }
    else if (pinnable(id)) { ui.pins.push(id); ui.bar = true; }
    closeMenu(); saveUi(); structure = true;
  }
  /** A knob's or fader's menu (right-click, a long press by touch): Pin to bar / Unpin from bar, or that the bar is full. */
  function openPinMenu(id, x, y) {
    closeMenu();
    if (!pinnable(id)) return;
    var el = document.createElement('div'), S = uiScale(), on = pinned(id), full = !on && ui.pins.length >= PIN_MAX, src = pinSrc(id);
    el.className = 'modmenu pinmenu';
    var d = ORDER[id], cc = d.slot >= 0 ? CH[d.slot] : null, hh = cc && cc.hue != null ? cc.hue : 90;
    if (src) el.setAttribute('style', srcVars(src));
    else if (cc) el.setAttribute('style', '--c:' + Knobs.hsla(hh, 85, 70, 1) + ';--ce:' + Knobs.hsla(hh, 70, 60, 0.6));
    el.innerHTML = '<div class="mmhead"><span class="srcdot"></span><b>' + esc(pinName(id)) + '</b></div>' +
      (full ? '<div class="hint flush">The bar is full (' + PIN_MAX + ' pins): unpin one first.</div>'
            : '<button class="tbtn mmpin" data-pintog="' + id + '">' + PIN_SVG + (on ? 'Unpin from bar' : 'Pin to bar') + '</button>');
    elUI.appendChild(el);
    menu = { pin: id, el: el };
    var LW = window.innerWidth / S, LH = window.innerHeight / S;
    el.style.left = Math.max(8, Math.min(LW - el.offsetWidth - 8, x / S + 8)) + 'px';
    el.style.top = Math.max(8, Math.min(LH - el.offsetHeight - 8, y / S + 8)) + 'px';
  }
  /* a pin's label dragged along the bar moves it there (the others make room as it is let go) */
  var pinDrag = null;
  document.addEventListener('pointerdown', function (e) {
    var lab = e.button === 0 && e.target.closest ? e.target.closest('.pinlab') : null; if (!lab) return;
    var cell = lab.parentNode;
    e.preventDefault();
    pinDrag = { id: +cell.getAttribute('data-pinc'), el: cell, x0: e.clientX, moved: false };
    try { lab.setPointerCapture(e.pointerId); } catch (err) { /* fine */ }
  });
  document.addEventListener('pointermove', function (e) {
    if (!pinDrag) return;
    var dx = (e.clientX - pinDrag.x0) / uiScale();
    if (!pinDrag.moved && Math.abs(dx) < 6) return;
    pinDrag.moved = true; pinDrag.el.classList.add('moving'); pinDrag.el.style.transform = 'translateX(' + dx.toFixed(1) + 'px)';
  });
  function endPinDrag(e) {
    if (!pinDrag) return;
    var p = pinDrag; pinDrag = null;
    p.el.classList.remove('moving'); p.el.style.transform = '';
    if (!p.moved || e.type === 'pointercancel') return;
    var at = 0; // its new place: after every other pin whose middle is left of the pointer
    elBar.querySelectorAll('.pin').forEach(function (c) { if (c === p.el) return; var r = c.getBoundingClientRect(); if (r.left + r.width / 2 < e.clientX) at++; });
    var i = ui.pins.indexOf(p.id); if (i < 0) return;
    ui.pins.splice(i, 1); ui.pins.splice(at, 0, p.id);
    saveUi(); structure = true;
  }
  document.addEventListener('pointerup', endPinDrag);
  document.addEventListener('pointercancel', endPinDrag);
  /* the bar's place: beside the panel (or its rail), along the bottom; its height (and a gap) is taken off the
     stage's room, eased as the panel's side is */
  var barPos = '', stageB = null;
  function placeBar(LW, LH, taken, dt) {
    var h = 0;
    if (elBar && ui.bar) {
      var pos = ui.dock + ':' + taken;
      if (pos !== barPos) { barPos = pos; elBar.style.left = (ui.dock === 'left' ? taken : M) + 'px'; elBar.style.right = (ui.dock === 'left' ? M : taken) + 'px'; }
      h = elBar.offsetHeight + M;
      pinColours();
    }
    stageB = stageB == null ? h : stageB + (h - stageB) * ease(dt, 0.12);
    if (Math.abs(stageB - h) < 0.25) stageB = h;
    return stageB;
  }
  function pinColours() {
    document.querySelectorAll('.pin[data-pslot]').forEach(function (c) { // a channel's pin in its live colour
      var cc = CH[+c.getAttribute('data-pslot')], col = Knobs.hsla(cc.hue == null ? 90 : cc.hue, Math.min(90, cc.sat), 70, 1);
      if (c._c !== col) { c._c = col; c.style.setProperty('--c', col); }
    });
  }
  /** Mini:the bar is a drawer along the bottom of the screen (app.css html.mini #pinbar), the panel shortened to end
      above it; returns its height and gap (0 shut), for the toast. */
  var drawerH = -1;
  function placeDrawer() {
    var h = 0;
    if (elBar && ui.bar) {
      h = elBar.offsetHeight + 8;
      pinColours();
    }
    if (h !== drawerH) { drawerH = h; elPanel.style.bottom = (10 + h) + 'px'; }
    return h;
  }
  /** Chips for a stepped parameter; `from`: the first of its values to offer (the ones below are hidden). */
  function chipRow(id, label, from) {
    var d = ORDER[id], v = Math.round(val(id)), out = '<div class="chips" data-chips="' + id + '">' + (label ? '<span class="lab">' + esc(label) + '</span>' : '');
    for (var i = from || 0; i < d.names.length; i++) {
      var sv = d.min + i;
      out += '<button class="chip" data-id="' + id + '" data-val="' + sv + '" aria-pressed="' + (v === sv ? 'true' : 'false') + '">' + esc(d.names[i]) + '</button>';
    }
    return out + '</div>';
  }
  function selectEl(id) {
    var d = ORDER[id], v = Math.round(val(id)), out = '<select class="sel" data-id="' + id + '">';
    for (var i = 0; i < d.names.length; i++) { var sv = d.min + i; out += '<option value="' + sv + '"' + (v === sv ? ' selected' : '') + '>' + esc(d.names[i]) + '</option>'; }
    return out + '</select>';
  }
  function group(title, body, extra) {
    return '<section class="group"><div class="gtitle"><span>' + title + '</span>' + (extra || '') + '</div>' + body + '</section>';
  }
  /** A section inside an open channel; any number open at once. Closed, its head shows a summary. */
  function csec(s, key, title, body) {
    var open = !!secsOf(s)[key], ref = s + ':' + key;
    return '<div class="sec' + (open ? ' open' : '') + '"><button class="sechead" data-csec="' + ref + '" aria-expanded="' + open + '"><span class="chev">' + (open ? '▾' : '▸') + '</span>' +
      '<span class="t">' + title + '</span><span class="sum" data-sum="' + ref + '"></span></button>' +
      (open ? '<div class="secbody">' + body + '</div>' : '') + '</div>';
  }
  /** A group in an open channel (GENERATOR, INSTRUMENT, EFFECTS, VISUAL): a band its label opens and folds; while
      open its head (the chips) sits on the band and its sections below, set in; while folded a summary. */
  function cgrp(s, key, title, head, inner) {
    var shut = !grpOpen(s, key), ref = s + ':' + key;
    return '<div class="cgrp' + (shut ? '' : ' open') + '"><div class="cgroup"><button class="cghead" data-cgrp="' + ref + '" aria-expanded="' + !shut + '" title="' +
      (shut ? 'Open ' : 'Fold ') + title.toLowerCase() + '"><span class="chev">' + (shut ? '▸' : '▾') + '</span><span class="cglab">' + title + '</span></button>' +
      (shut ? '<span class="cgsum" data-sum="' + ref + '"></span></div></div>' : head + '</div><div class="grpbody">' + inner + '</div></div>');
  }
  /** A group of the Visual tab: a band as a channel's groups are, its label opening and folding it (One at a time:
      opening one folds the others); `head` sits on the band either way (an effect's On / Off), a summary while folded. */
  function tgroup(key, title, body, head) {
    var open = !!ui.tgrp[key];
    return '<div class="cgrp tabgrp' + (open ? ' open' : '') + '"><div class="cgroup"><button class="cghead" data-tgrp="' + key + '" aria-expanded="' + open + '" title="' +
      (open ? 'Fold ' : 'Open ') + title.toLowerCase() + '"><span class="chev">' + (open ? '▾' : '▸') + '</span><span class="cglab">' + title + '</span></button>' + (head || '') +
      (open ? '</div><div class="grpbody">' + body + '</div></div>' : '<span class="cgsum" data-tsum="' + key + '"></span></div></div>');
  }
  /** Many open / One at a time: one switch for the channels and the Visual tab. */
  function singleHtml() {
    return '<div class="chopts"><span class="lab">SECTIONS</span><div class="chips">' +
      '<button class="chip" data-act="single" data-single="0" aria-pressed="' + !ui.single + '" title="Open any number of channels, groups and sections">Many open</button>' +
      '<button class="chip" data-act="single" data-single="1" aria-pressed="' + ui.single + '" title="Opening a channel, group or section closes the others; ' +
        'switching this on shuts everything but the last one opened">One at a time</button></div></div>';
  }
  /** A section's body: its view on the left, its knobs (up to six) on the right. */
  /** PATTERN's second row, under the first's knobs: which notes the pattern plays.
      `cells` are knobs (Rotate, Shift, Seed: the generator's); `count` the COUNT chips' Auto (1 Steps, 2 Hits) or 0
      for none. */
  function noteRow(s, cells, count) {
    var out = '<div class="srow noterow"><div class="notel">' + (count ? countChips(s, count) : '') + '</div><div class="knobs">' + cells.join('');
    for (var i = cells.length; i < 6; i++) out += '<div></div>';
    return out + '</div></div>';
  }
  /** COUNT as two chips, Steps and Hits; the one the generator's Auto plays as stands for Auto (it sets 0, the
      default, and lights at 0), the other sets its own value (an explicit value lights its chip too: data-same). */
  function countChips(s, auto) {
    var id = sid(s, 'note_count'), v = Math.round(val(id)), out = '<div class="chips" data-chips="' + id + '"><span class="lab">COUNT</span>';
    [[1, 'Steps', 'The note follows the step: Rotate moves the rhythm and changes the notes'],
     [2, 'Hits', 'Each hit takes the next note: the run stays whole at any density, Rotate moves only the rhythm']].forEach(function (c) {
      var dv = c[0] === auto ? 0 : c[0];
      out += '<button class="chip" data-id="' + id + '" data-val="' + dv + '" data-same="' + c[0] + '" title="' + esc(c[2]) + '" aria-pressed="' +
        (v === dv || v === c[0] ? 'true' : 'false') + '">' + c[1] + '</button>';
    });
    return out + '</div>';
  }
  function srow(view, slot, cells, attrs) {
    var out = '<div class="srow"><canvas class="viz ' + view + '" data-slot="' + slot + '"' + (attrs || '') + '></canvas><div class="knobs">' + cells.join('');
    for (var i = cells.length; i < 6; i++) out += '<div></div>';
    return out + '</div></div>';
  }
  function eyeSvg(open) {
    return open ? '<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12z"></path><circle cx="12" cy="12" r="3"></circle></svg>'
      : '<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="M3 3l18 18"></path><path d="M10.6 5.1A10 10 0 0 1 12 5c6.4 0 10 7 10 7a17 17 0 0 1-3.2 4"></path><path d="M6.6 6.6C3.8 8.5 2 12 2 12s3.6 7 10 7a9.6 9.6 0 0 0 5.4-1.6"></path></svg>';
  }
  /* the dock button: a window with the panel drawn on the side it will move to */
  function dockSvg(side) {
    var x = side === 'right' ? 12 : 3.5;
    return '<svg width="20" height="16" viewBox="0 0 20 16" fill="none" stroke="currentColor" stroke-width="1.5"><rect x="1" y="1" width="18" height="14" rx="2.5"></rect>' +
      '<rect x="' + x + '" y="3.5" width="4.5" height="9" rx="1" fill="currentColor" stroke="none"></rect></svg>';
  }
  var LOGO = '<svg width="26" height="26" viewBox="-2 4 24 24" fill="none" stroke="#efe6d8" stroke-width="1.6" stroke-linecap="round"><path d="M15 14 a1 1 0 0 1 -1 1 a2 2 0 0 1 -2 -2 a3 3 0 0 1 3 -3 a5 5 0 0 1 5 5 a8 8 0 0 1 -8 8 a13 13 0 0 1 -13 -13"></path></svg>';
  function other(side) { return side === 'left' ? 'right' : 'left'; }

  /** The DOM from scratch: on structural changes only. The scroll position is kept within a tab. */
  function build() {
    structure = false; dirty = false;
    if (!ORDER[0]) return;
    var act0 = activeSlots();
    if (act0.length && act0.indexOf(ui.sel) < 0) ui.sel = act0[0];
    var body = elPanel.querySelector('.pbody'), scroll = body ? body.scrollTop : 0, sameTab = body && body.getAttribute('data-tab') === ui.tab;
    if (MINI) ui.rail = false;
    elPanel.className = MINI ? 'glass' : 'glass dock-' + ui.dock + (ui.rail ? ' rail' : '');
    var play = '<button class="playbtn" data-act="play" title="Play / stop (Space)"><span data-txt="play"></span></button>';
    var recb = '<button class="recbtn" data-act="rec" title="Record the output to a WAV file (R)"><span class="rdot"></span><span class="rtime" data-txt="rectime"></span></button>';
    var dock = '<button class="iconbtn lg" data-act="dock" title="Move the panel to the ' + other(ui.dock) + '">' + dockSvg(other(ui.dock)) + '</button>';
    var bar = '<button class="iconbtn lg barbtn" data-act="bar" aria-pressed="' + ui.bar + '" title="' + (ui.bar ? 'Hide' : 'Show') + ' the pin bar (B)">' + BAR_SVG + '</button>';
    buildBar();
    var toEdge = ui.dock === 'left' ? '‹' : '›', fromEdge = ui.dock === 'left' ? '›' : '‹';
    if (ui.rail) {
      var html = '<button class="iconbtn lg" data-act="rail" title="Open the panel">' + fromEdge + '</button>' + play + recb + bar + dock + '<div class="raildiv"></div>';
      activeSlots().forEach(function (s) { html += '<button class="raildot" data-pick="' + s + '" title="' + slotName(s) + '"><canvas class="dot" data-slot="' + s + '"></canvas></button>'; });
      elPanel.innerHTML = html;
      refresh();
      return;
    }
    var slots = activeSlots(), tabs = [['conductor', 'CONDUCTOR', ''], ['modulation', 'MODULATION', ''], ['channels', 'CHANNELS', '<b>' + slots.length + '</b>'], ['visual', 'VISUAL', '']];
    if (MINI) tabs.pop(); // no stage (the full-screen Scope comes as a view of its own)
    var out = '<div class="ptop">' + LOGO + '<span class="brand">AMMONITE</span>' +
      '<div class="preset"><button data-act="preset-prev" title="Previous preset">‹</button><span class="name" data-txt="preset"></span><button data-act="preset-next" title="Next preset">›</button></div>' +
      '<button class="iconbtn lg txt" data-act="open" title="Load a patch file (Ctrl+O)">Load</button>' +
      '<button class="iconbtn lg txt" data-act="save" title="Save the patch to a file (Ctrl+S)">Save</button>' +
      '<div class="grow"></div>' + cpuHtml() + outputHtml() + '<canvas class="hfader master" data-id="' + gid('volume') + '" title="Master volume (as the Conductor\'s)"></canvas>' +
      recb + play + bar + (MINI ? '</div>' : dock + '<button class="iconbtn lg" data-act="rail" title="Collapse the panel">' + toEdge + '</button></div>') +
      '<div class="tabs" role="tablist">';
    tabs.forEach(function (t) { out += '<button class="tab" role="tab" data-tab="' + t[0] + '" aria-selected="' + (ui.tab === t[0]) + '">' + t[1] + (t[2] ? ' ' + t[2] : '') + '</button>'; });
    if (assign) out += '</div><div class="assignbar" style="' + srcVars(assign.src) + '"><span class="adot"></span><span>Assigning <b>' + esc(srcName(assign.src)) +
      (MINI ? '</b>: tap a knob of any channel, or of the Conductor, to route it there (+25%), again to remove it; drag from the tap, or drag its tag, to set how far.</span>'
            : '</b>: click a knob of any channel, or of the Conductor, to route it there (+25%), again to remove it; drag from the click, or drag its tag, to set how far.</span>') +
      '<div class="grow"></div><button class="tbtn" data-act="assign-done">Done (Esc)</button>';
    out += '</div><div class="pbody" data-tab="' + ui.tab + '">' + (ui.tab === 'conductor' ? conductorHtml() : ui.tab === 'modulation' ? modulationHtml() : ui.tab === 'visual' ? visualHtml(slots) : channelsHtml(slots)) + '</div>';
    elPanel.innerHTML = out;
    body = elPanel.querySelector('.pbody');
    if (sameTab) body.scrollTop = scroll;
    if (reveal) { var open = body.querySelector('.ch[data-ch="' + ui.sel + '"]'); if (open) open.scrollIntoView({ block: 'nearest' }); reveal = false; }
    refresh();
  }

  function conductorHtml() {
    var four = function (cells) { return '<div class="knobs">' + cells.join('') + '<div></div><div></div></div>'; };
    return '<div class="cgrid">' +
      group('HARMONY',
        '<div class="wheelrow"><canvas class="wheel"></canvas><div class="keybox"><span class="lab">KEY</span><span class="big" data-txt="key"></span>' +
          '<span class="chordnow">now <b data-txt="chord"></b> <span data-txt="progpos"></span></span>' +
          '<div class="row center"><span class="lab w64">SCALE</span>' + selectEl(gid('scale')) + '</div>' +
          '<div class="row">' + knobCell(gid('root'), 'Root') + '</div></div></div>') +
      group('PROGRESSION',
        '<div class="row center">' + selectEl(gid('prog')) + '<button class="tbtn" data-act="prog-custom" title="Copy this progression into Custom to change its chords">Edit as Custom</button></div>' +
        '<canvas class="prog"></canvas>' +
        '<div data-chips-prog="1"></div>' + chipRow(gid('change'), 'CHANGE') + chipRow(gid('restart'), 'RESTART')) +
      group('TIME',
        '<div class="row center" style="gap:14px"><span class="big tempo" data-txt="tempo" title="Click to type a tempo"></span><span class="lab">BPM</span>' +
          (MINI ? '' : '<span class="pill" data-txt="sync"></span><div class="grow"></div>' + chipRow(gid('sync'), 'SYNC')) + '</div>' + // Mini: the internal clock only
        four([knobCell(gid('tempo'), 'Tempo'), knobCell(gid('swing'), 'Swing'), knobCell(gid('octave'), 'Octave'), knobCell(gid('volume'), 'Volume')])) +
      group('MASTER', '<div class="srow"><canvas class="viz masterview narrow" data-slot="-1"></canvas><div class="knobs three">' +
          knobCell(gid('glue'), 'Glue') + knobCell(gid('ceiling'), 'Ceiling') + knobCell(gid('volume'), 'Volume') + '</div></div>' +
        '<div class="hint flush">Glue: a gentle compressor that holds the mix together (its make-up keeps the level). Ceiling: nothing passes it.</div>') +
      wideGroup('SPACE', srow('spaceview', -1, [knobCell(gid('rev_size'), 'Size'), knobCell(gid('rev_decay'), 'Decay'), knobCell(gid('rev_damp'), 'Damping'),
        knobCell(gid('rev_pre'), 'Pre-delay'), knobCell(gid('rev_width'), 'Width'), knobCell(gid('rev_return'), 'Return')]) +
        '<div class="hint flush">The reverb every channel shares: each channel\'s SEND (in its Effects) sets how much of it goes in.</div>') +
      '</div>';
  }
  /** The Modulation tab: the LFOs in use, then the macros in use, each with its settings and its targets; + Add LFO
      and + Add macro take the first free one (gone at the cap, a count near it). */
  function modulationHtml() {
    var lfos = [], macros = [], l, m;
    for (l = 0; l < POOL_LFOS; l++) if (lfoOn(l)) lfos.push(lfoHtml(l));
    for (m = 0; m < POOL_MACROS; m++) if (macroOn(m)) macros.push(macroHtml(m));
    function sec(title, list, cap, act, what, empty) {
      var n = list.length;
      return '<div class="modsec"><div class="modhead"><span>' + title + '</span>' + (n >= cap - 4 ? '<span class="modcount">' + n + ' of ' + cap + '</span>' : '') + '</div>' +
        (n ? '<div class="cgrid">' + list.join('') + '</div>' : '<div class="empty">' + empty + '</div>') +
        (n < cap ? '<button class="addbtn" data-act="' + act + '">+ Add ' + what + '</button>' : '') + '</div>';
    }
    return sec('LFOS', lfos, POOL_LFOS, 'add-lfo', 'LFO', 'No LFOs yet. Add one, then Assign it to the knobs it should move.') +
      sec('MACROS', macros, POOL_MACROS, 'add-macro', 'macro', 'No macros yet: a macro is one knob that moves many (a DAW can automate it).');
  }
  /** A source's head: its colour, its name (a macro's: click to rename), what it moves in a line, Assign, remove. */
  function srcHead(src) {
    var mac = src >= SRC_M1;
    return '<div class="srchead"><span class="srcdot"></span><span class="srcname' + (mac ? ' rename" data-rename="' + (src - SRC_M1) +
      '" title="Click to rename' + (ui.mnames[src - SRC_M1] ? ' (empty: its own name again)' : '') + '">' : '">') + esc(srcName(src)) + '</span>' +
      '<span class="srcsum" data-srcsum="' + src + '"></span>' + assignBtn(src) +
      '<button class="iconbtn" data-act="rm-src" data-src="' + src + '" title="Remove ' + esc(srcName(src)) + ' and its targets">×</button></div>';
  }
  /** An LFO: shape, sync and restart, its view and Rate or Division and Phase, its targets. */
  function lfoHtml(l) {
    var tempo = Math.round(val(lid(l, 'sync'))) === 1, src = SRC_LFO1 + l, k = function (key, lab) { return knobCell(lid(l, key), lab); };
    return '<section class="group srcgrp" data-src="' + src + '" style="' + srcVars(src) + '">' + srcHead(src) +
      chipRow(lid(l, 'shape'), 'SHAPE') + '<div class="row center">' + chipRow(lid(l, 'sync'), 'SYNC') +
      '<span class="lab">RESTART</span>' + selectEl(lid(l, 'restart')) + '</div>' +
      '<div class="srow"><canvas class="viz lfoview narrow" data-slot="-1" data-lfo="' + l + '"></canvas><div class="knobs two">' +
      (tempo ? k('div', 'Division') : k('rate', 'Rate')) + k('phase', 'Phase') + '</div></div>' + targetsHtml(src) + '</section>';
  }
  /** A macro: its knob, its targets. */
  function macroHtml(m) {
    var src = SRC_M1 + m;
    return '<section class="group srcgrp" data-src="' + src + '" style="' + srcVars(src) + '">' + srcHead(src) +
      '<div class="srow"><div class="knobs two">' + knobCell(macroId(m), macroName(m)) + '<div></div></div></div>' + targetsHtml(src) + '</section>';
  }
  /** A source's targets: where (channel and parameter, or the Conductor's group and knob), Bipolar / Unipolar (a macro
      only goes one way), the amount as a fader and typed (% of the target's range), the same in the target's units,
      remove. The same numbers as the target's tag, live either way. */
  function targetsHtml(src) {
    var rows = '', mac = src >= SRC_M1;
    routeTable().list.forEach(function (rt) {
      if (rt.src !== src || rt.target < 0) return;
      var r = rt.r, tn = targetName(rt.target);
      rows += '<div class="route" data-route="' + r + '"><span class="tname" title="' + esc(tn) + '">' + esc(tn) + '</span>' +
        '<button class="pol" data-act="pol" data-route="' + r + '"' + (mac ? ' disabled title="A macro moves its targets one way: up from the knob (down for a negative amount)"' :
          ' title="Bipolar: the LFO swings it both ways around the knob. Unipolar: up from the knob only (down for a negative amount)"') + ' data-poltxt="' + r + '"></button>' +
        '<canvas class="hfader amt" data-id="' + rid(r, 'amt') + '" data-lab="AMT" title="Amount: how far it moves its target, of the whole range"></canvas>' +
        '<input class="num" data-amtin="' + r + '" inputmode="decimal" title="Amount, % of the target\'s range (-100 .. 100)"><span class="pctu">%</span>' +
        '<span class="amtu" data-amtu="' + r + '" title="The amount in the target\'s units"></span>' +
        '<button class="iconbtn" data-act="route-clear" data-route="' + r + '" title="Remove this target">×</button></div>';
    });
    return '<div class="routes">' + (rows || '<div class="hint flush">No targets yet: Assign, then click the knobs it should move.</div>') + '</div>';
  }
  function wideGroup(title, body) { return group(title, body).replace('<section class="group">', '<section class="group wide">'); }

  function channelsHtml(slots) {
    var html = slots.length ? singleHtml() : '';
    for (var i = 0; i < slots.length; i++) html += channelHtml(slots[i]);
    if (!slots.length) html += '<div class="empty">No channels playing. Add one to start a pattern.</div>';
    if (slots.length < SLOTS) html += '<button class="addbtn" data-act="add">+ Add channel</button>';
    return html;
  }

  /** One channel: the row (always), and what it contains (when open): the generator's sections, the instrument's, its look. */
  function channelHtml(s) {
    var open = isOpen(s), k = function (sym, label) { return knobCell(sid(s, sym), label); };
    var html = '<div class="ch' + (open ? ' open' : '') + '" data-ch="' + s + '"><div class="chrow">' +
      '<button class="chpick" data-pick="' + s + '" aria-expanded="' + open + '"><span class="chev">' + (open ? '▾' : '▸') + '</span><canvas class="bar" data-slot="' + s + '"></canvas>' +
        '<span class="names"><span class="nm">' + slotName(s) + '</span><span class="sub" data-chsub="' + s + '"></span></span></button>' +
      '<canvas class="scope" data-pick="' + s + '" data-slot="' + s + '"></canvas>' +
      '<span class="note" data-note="' + s + '"></span>' +
      '<canvas class="hfader" data-id="' + sid(s, 'level') + '" title="Level"></canvas>' +
      '<canvas class="hfader pan" data-id="' + sid(s, 'pan') + '" title="Pan"></canvas>' +
      (MINI ? '' : '<button class="tog eye" data-id="' + sid(s, 'show') + '" data-toggle="1" title="Show in the visualizer"></button>') +
      '<button class="tog mute" data-id="' + sid(s, 'mute') + '" data-toggle="1" title="Mute">M</button></div>';
    if (!open) return html + '</div>';
    var gen = genOf(s), look = lookOf(s);
    var body = '';
    var rhythm = [k('arp_div', 'Division'), k('arp_len', 'Length'), k('arp_density', 'Density'), k('arp_gate', 'Gate'), k('arp_vary', 'Vary'), k('arp_ratchet', 'Ratchet')];
    if (gen === GEN_ARP) {
      body += csec(s, 'pat', 'PATTERN', '<div class="chipline">' + chipRow(sid(s, 'arp_mode'), 'MODE') + chipRow(sid(s, 'arp_pool'), 'POOL') + '</div>' + srow('stepview', s, rhythm) +
          noteRow(s, [k('rhy_rotate', 'Rotate'), k('note_shift', 'Shift'), k('seed', 'Seed')], 1)) +
        csec(s, 'pitch', 'PITCH', srow('rollview', s, [k('degree', 'Degree'), k('oct', 'Octave'), k('arp_range', 'Range'), k('tune', 'Fine')]));
    } else if (gen === GEN_CHORD) {
      // Arp: the arp's patterns (Off and Loop play as Up, so not offered) and Let ring; no strum
      var arp = chordArp(s);
      body += csec(s, 'chord', 'CHORD', '<div class="chipline">' + chipRow(sid(s, 'chord_notes'), 'NOTES') + chipRow(sid(s, 'chord_voicing'), 'VOICING') + '</div>' +
          '<div class="chipline">' + chipRow(sid(s, 'chord_lead'), 'LEADING') + chipRow(sid(s, 'chord_rhythm'), 'RHYTHM') + '</div>' +
          (arp ? '<div class="chipline">' + chipRow(sid(s, 'arp_mode'), 'MODE', 2) + chipRow(sid(s, 'chord_ring'), 'LET RING') + '</div>' : '') +
          srow('chordview', s, [k('degree', 'Degree'), k('oct', 'Octave'), k('tune', 'Fine')].concat(arp ? [] : [k('chord_strum', 'Strum')])));
      if (!holds(s)) body += csec(s, 'pat', 'PATTERN', srow('stepview', s, arp ? rhythm : rhythm.slice(0, 4)) +
          noteRow(s, arp ? [k('rhy_rotate', 'Rotate'), k('note_shift', 'Shift'), k('seed', 'Seed')] : [k('rhy_rotate', 'Rotate')], arp ? 2 : 0));
    } else if (gen === GEN_RHYTHM) {
      body += csec(s, 'pat', 'PATTERN', srow('stepview', s, [k('arp_div', 'Division'), k('arp_len', 'Length'), k('arp_density', 'Hits'),
          k('rhy_rotate', 'Rotate'), k('rhy_chance', 'Chance'), k('arp_ratchet', 'Roll')]) + noteRow(s, ['<div></div>', '<div></div>', '<div></div>', '<div></div>', k('seed', 'Seed')], 0)) +
        csec(s, 'pitch', 'FEEL', '<div class="chipline">' + chipRow(sid(s, 'rhy_pitch'), 'PITCH') + '</div>' +
          srow('rollview', s, [k('arp_vary', 'Humanize'), k('arp_gate', 'Gate'), k('oct', 'Octave'), k('degree', 'Degree'), k('tune', 'Fine')]));
    } else if (gen === GEN_BASS) {
      body += csec(s, 'pat', 'PATTERN', '<div class="chipline">' + chipRow(sid(s, 'bass_pattern'), 'PATTERN') + '</div>' + srow('stepview', s, rhythm) +
          noteRow(s, [k('rhy_rotate', 'Rotate'), '<div></div>', k('seed', 'Seed')], 0)) +
        csec(s, 'pitch', 'PITCH', srow('rollview', s, [k('degree', 'Degree'), k('oct', 'Octave'), k('tune', 'Fine')]));
    }
    body = cgrp(s, 'gen', 'GENERATOR', chipRow(sid(s, 'gen'), ''), body);
    var inst = '';
    if (isDrum(s)) inst += csec(s, 'drum', 'DRUM', '<div class="chipline">' + chipRow(sid(s, 'drum_model'), 'MODEL') + '</div>' +
      srow('drumview', s, [k('drum_tune', 'Tune'), k('drum_decay', 'Decay'), k('drum_tone', 'Tone'), k('drum_snap', 'Snap')]));
    else inst += csec(s, 'osc', 'OSCILLATOR', srow('waveview', s, [k('shape', 'Shape'), k('detune', 'Detune'), k('sub', 'Sub'), k('noise', 'Noise'), k('drive', 'Drive'), k('glide', 'Glide')])) +
      csec(s, 'filt', 'FILTER', srow('filtview', s, [k('cutoff', 'Cutoff'), k('reso', 'Reso'), k('filter_type', 'Type'), k('filt_amt', 'Env amt'), k('filt_a', 'Attack'), k('filt_d', 'Decay')])) +
      csec(s, 'amp', 'AMP', srow('envview', s, [k('amp_a', 'Attack'), k('amp_d', 'Decay'), k('amp_s', 'Sustain'), k('amp_r', 'Release'), k('pitch_amt', 'Pitch env'), k('pitch_d', 'Pitch dec')]));
    body += cgrp(s, 'inst', 'INSTRUMENT', chipRow(sid(s, 'inst'), ''), inst) + cgrp(s, 'fx', 'EFFECTS', '', effectsHtml(s, k)) +
      '<div class="cgroup chmod"><span class="cglab">MODULATION</span><span class="cgsum" data-chmod="' + s + '"></span>' +
      '<button class="tbtn" data-act="to-mod" title="The LFOs and macros, and what they move">Modulation tab ›</button></div>';
    if (MINI) return html + '<div class="chbody">' + body + '</div></div>'; // no stage: no VISUAL group
    var vis = '<div class="lookrow"><span class="lab">LOOK</span><div class="lookseg">';
    for (var i = 0; i < LOOKS.length; i++) vis += '<button data-look="' + LOOKS[i][0] + '" data-slot="' + s + '" aria-pressed="' + (look === LOOKS[i][0]) + '">' + LOOKS[i][1] + '</button>';
    vis += '</div><button class="small" data-id="' + sid(s, 'show') + '" data-toggle="1" data-shown="' + s + '"></button></div><div class="hint">' + esc(HINT[look]) + '</div>' +
      srow('motionview', s, [knobCell(vsid(s, 'v_presence'), 'Presence'), knobCell(vsid(s, 'v_response'), 'Response'), knobCell(vsid(s, 'v_cpulse'), 'Pulse'), knobCell(vsid(s, 'v_detail'), 'Detail'),
        knobCell(vsid(s, 'v_size'), 'Size'), knobCell(vsid(s, 'v_alpha'), 'Alpha')]) + '<div class="hint">' + esc(VIS_HINT) + '</div>';
    if (scopeOn()) body += cgrp(s, 'vg', 'VISUAL', '', '<div class="lookrow"><span class="lab">SCOPE</span><button class="small" data-id="' + sid(s, 'show') + '" data-toggle="1" data-shown="' + s + '"></button></div>' +
      '<div class="hint">The stage is the scope (Visual tab): this channel is one of its waves. Its look, motion and particles belong to the 3D form.</div>');
    else body += cgrp(s, 'vg', 'VISUAL', '', csec(s, 'vis', 'LOOK', vis) + csec(s, 'part', 'PARTICLES', particleHtml(s, look)));
    return html + '<div class="chbody">' + body + '</div></div>';
  }

  /** The channel's EFFECTS, after the instrument, in the order the sound passes them: Drive, Chorus, Delay, then the
      reverb send and the duck. */
  function effectsHtml(s, k) {
    return csec(s, 'drive', 'DRIVE', srow('driveview', s, [k('fx_drive', 'Amount'), k('fx_tone', 'Tone')])) +
      csec(s, 'cho', 'CHORUS', '<div class="chipline">' + chipRow(sid(s, 'cho_on'), 'CHORUS') + chipRow(sid(s, 'cho_mode'), 'MODE') + '</div>' +
        srow('chorusview', s, [k('cho_rate', 'Rate'), k('cho_depth', 'Depth')].concat(vibrato(s) ? [] : [k('cho_mix', 'Mix')]))) +
      csec(s, 'dly', 'DELAY', '<div class="chipline">' + chipRow(sid(s, 'dly_div'), 'DIVISION') + chipRow(sid(s, 'dly_pp'), 'PING-PONG') + '</div>' +
        srow('delayview', s, [k('dly_fb', 'Feedback'), k('dly_damp', 'Damping'), k('dly_mix', 'Mix')])) +
      csec(s, 'rev', 'REVERB', srow('revview', s, [k('rev_send', 'Send')]) +
        '<div class="hint flush">How much of it goes into the reverb every channel shares (its size, decay and return: the Conductor\'s SPACE).</div>') +
      csec(s, 'duck', 'DUCK', '<div class="chipline"><div class="row center"><span class="lab w64">DUCK BY</span>' + selectEl(sid(s, 'duck_src')) + '</div></div>' +
        srow('duckview', s, [k('duck_amt', 'Amount')]));
  }

  /* ---- modulation: the pool's ids, its sources, the route table, assigning */
  function lid(l, key) { return gid('lfo' + (l + 1) + '_' + key); }
  function macroId(m) { return gid('macro' + (m + 1)); }
  function rid(r, key) { return gid('route' + (r + 1) + '_' + key); }
  function isRoute(id) { return id >= ROUTE_BASE && id < ROUTE_BASE + 4 * POOL_ROUTES; }
  function lfoOn(l) { return val(lid(l, 'on')) >= 0.5; }
  function macroOn(m) { return val(gid('macro' + (m + 1) + '_on')) >= 0.5; }
  function macroName(m) { return (ui.mnames && ui.mnames[m]) || MACRO_NAMES[m] || 'Macro ' + (m + 1); }
  function srcName(src) { return src >= SRC_M1 ? macroName(src - SRC_M1) : 'LFO ' + (src - SRC_LFO1 + 1); }
  /** A source's short name, for its tags: L1 .. L32; a macro its name's initials (BL for Bloom, BW for Big wash), M5 for
      Macro 5. */
  function srcShort(src) {
    if (src < SRC_M1) return 'L' + (src - SRC_LFO1 + 1);
    var m = src - SRC_M1, w = macroName(m).trim().split(/\s+/);
    if (!(ui.mnames && ui.mnames[m]) && !MACRO_NAMES[m]) return 'M' + (m + 1);
    return (w.length > 1 ? w[0].charAt(0) + w[1].charAt(0) : w[0].slice(0, 2)).toUpperCase();
  }
  /** Each source's colour, by its pool number, on its dot, trace, tags, rings, target rows and the assign bar: flat
      and a little pale beside the channels' neon. LFOs take eight hues picked to sit apart on the dark glass (cyan,
      amber, violet, lime, coral, blue, rose, mint), LFOs 9 .. 32 the same again turned and lighter or deeper each
      round; macros eight of their own, paler (Bloom pink, Motion gold, Space periwinkle, Grit orange). */
  var LFO_HUES = [186, 40, 268, 92, 8, 214, 330, 152], MACRO_HUES = [318, 50, 232, 20, 122, 288, 172, 0];
  function srcTone(src) {
    var mac = src >= SRC_M1, n = mac ? src - SRC_M1 : src - SRC_LFO1, k = Math.floor(n / 8) % 4;
    return { h: ((mac ? MACRO_HUES : LFO_HUES)[n % 8] + [0, 16, -14, 30][k] + 360) % 360,
             s: mac ? [56, 44, 60, 40][k] : [78, 62, 84, 56][k], l: mac ? [76, 82, 70, 80][k] : [64, 74, 58, 70][k] };
  }
  function srcHue(src) { return srcTone(src).h; }
  function srcColor(src, a, dl) { var t = srcTone(src); return Knobs.hsla(t.h, t.s, Math.max(0, Math.min(100, t.l + (dl || 0))), a == null ? 1 : a); }
  /** The source's colours as CSS variables for the page's own elements: --c its colour, --cb a tint to fill, --ce an edge. */
  function srcVars(src) { return '--c:' + srcColor(src) + ';--cb:' + srcColor(src, 0.16, -22) + ';--ce:' + srcColor(src, 0.6, -8); }
  /** Bipolar: an LFO's route swings around the knob; unipolar, or any macro's, goes one way. */
  function bip(rt) { return rt.src < SRC_M1 && !rt.uni; }
  /** A route's amount in its target's units where it has them: the amount is a share of the target's range, so these
      are exact (Cutoff's 20 Hz .. 20 kHz is 9.97 octaves: 50% is 5.0 oct; Fine tune's 200 cents: 25% is 50 ct; Pan's L100
      .. R100). Knobs on a curve (the times, the rates, the dampings) read as a % of their range, as does anything without
      a unit. Bipolar shows ± (∓ for a negative amount: the LFO turned upside down), one way its sign. Keyed by the
      parameter's symbol: [units per whole range (0: the range itself), unit, decimals]. */
  var MOD_UNITS = { cutoff: [Math.log(1000) / Math.LN2, ' oct', 1], filt_amt: [8, ' oct', 1], tune: [0, ' ct', 0], detune: [0, ' ct', 0],
                    pitch_amt: [24, ' st', 1], drum_tune: [0, ' st', 1], pan: [200, '% L/R', 0], ceiling: [0, ' dB', 1], v_tilt: [0, '°', 0] };
  function amtText(target, amt, bi) {
    var d = ORDER[target], u = d && MOD_UNITS[d.s], x = amt * (u ? u[0] || d.max - d.min : 100), s = Math.abs(x).toFixed(u ? u[2] : 0);
    return (+s === 0 ? '' : bi ? (x < 0 ? '∓' : '±') : x < 0 ? '−' : '+') + s + (u ? u[1] : '%');
  }
  /** A route as its tag reads: "L1 ±5.0 oct"; and in full, as the knob's value line reads while it is dragged. */
  function tagText(rt) { return srcShort(rt.src) + ' ' + amtText(rt.target, rt.amt, bip(rt)); }
  function routeLine(rt) { return srcName(rt.src) + ' ' + amtText(rt.target, rt.amt, bip(rt)); }
  /** A route can move this control: a channel's continuous parameter, one of the Conductor's in GLOBAL_TARGETS, or a
      continuous knob of the page's own (the stage's look, camera, FX and scope, each channel's visual: see pageMod). */
  var PAGE_NOMOD = { v_yaw: 1, v_lie: 1 }; // the camera's, with no knob
  function isTarget(id) {
    var d = ORDER[id]; if (!d) return false;
    if (!isHost(id)) return d.steps === 0 && !PAGE_NOMOD[d.s];
    return d.slot >= 0 ? DEST_SYMS.indexOf(d.s) >= 0 : GLOBAL_TARGETS.indexOf(d.s) >= 0 && id < 64;
  }
  /** Where a target lives, in plain words: "Channel 2 · Sustain", "Space · Size" (the knobs' own names, as the panel
      labels them). */
  var TNAMES = { level: 'Level', pan: 'Pan', tune: 'Fine tune', shape: 'Shape', detune: 'Detune', sub: 'Sub', noise: 'Noise', drive: 'Drive', glide: 'Glide',
                 cutoff: 'Cutoff', reso: 'Reso', filter_type: 'Filter type', filt_amt: 'Filter env', filt_a: 'Filter attack', filt_d: 'Filter decay',
                 amp_a: 'Attack', amp_d: 'Decay', amp_s: 'Sustain', amp_r: 'Release', pitch_amt: 'Pitch env', pitch_d: 'Pitch decay', arp_gate: 'Gate',
                 arp_density: 'Density', arp_vary: 'Vary', arp_ratchet: 'Ratchet', chord_strum: 'Strum', rhy_chance: 'Chance', drum_tune: 'Drum tune',
                 drum_decay: 'Drum decay', drum_tone: 'Drum tone', drum_snap: 'Drum snap', fx_drive: 'Drive amount', fx_tone: 'Drive tone', cho_rate: 'Chorus rate',
                 cho_depth: 'Chorus depth', cho_mix: 'Chorus mix', dly_fb: 'Delay feedback', dly_damp: 'Delay damping', dly_mix: 'Delay mix', rev_send: 'Reverb send',
                 duck_amt: 'Duck amount', swing: 'Time · Swing', volume: 'Master · Volume', rev_size: 'Space · Size', rev_decay: 'Space · Decay',
                 rev_damp: 'Space · Damping', rev_pre: 'Space · Pre-delay', rev_width: 'Space · Width', rev_return: 'Space · Return', glue: 'Master · Glue',
                 ceiling: 'Master · Ceiling', v_presence: 'Presence', v_response: 'Response', v_cpulse: 'Visual pulse', v_detail: 'Detail', v_size: 'Size',
                 v_alpha: 'Alpha', v_pdens: 'Particle density', v_pscat: 'Particle scatter', v_pjit: 'Particle jitter', v_pcol: 'Particle color jitter',
                 v_psize: 'Point size', v_psoft: 'Point softness', v_pfill: 'Particle fill' };
  function targetName(id) {
    var d = ORDER[id]; if (!d) return '?';
    var n = TNAMES[d.s] || d.n;
    if (d.slot >= 0) return slotName(d.slot) + ' · ' + n;
    if (!isHost(id)) for (var i = 0; i < PIN_GROUPS.length; i++) if (PIN_GROUPS[i][0].test(d.s)) return PIN_GROUPS[i][1] + ' · ' + d.n; // "Feedback · Zoom"
    return n;
  }
  /** The route table, read again when a route's value changed: every route {r, src, target, amt, uni, live} (by its
      number too), and by target those with one (live or at 0: a tag dragged to 0 stays). */
  var RTAB = { list: [], byTarget: {}, byR: {} }, rtDirty = true;
  function routeTable() {
    if (!rtDirty || !ORDER[0]) return RTAB;
    rtDirty = false;
    var list = [], by = {}, byR = {};
    for (var r = 0; r < POOL_ROUTES; r++) {
      var src = Math.round(val(rid(r, 'src'))), t = Math.round(val(rid(r, 'target'))), amt = val(rid(r, 'amt'));
      if (!src) continue;
      var rt = { r: r, src: src, target: isTarget(t) ? t : -1, amt: amt, uni: Math.round(val(rid(r, 'pol'))) === 0 };
      rt.live = rt.target >= 0 && amt !== 0;
      list.push(rt); byR[r] = rt;
      if (rt.target >= 0) (by[t] = by[t] || []).push(rt);
    }
    RTAB = { list: list, byTarget: by, byR: byR };
    return RTAB;
  }
  function routeOf(r) { return routeTable().byR[r] || null; }
  function assignBtn(src) {
    var on = assign && assign.src === src;
    return '<button class="tbtn assign' + (on ? ' on' : '') + '" data-act="assign" data-src="' + src + '" title="' +
      (on ? 'Stop assigning (Esc)' : 'Route it: click the knobs it should move (again to remove; drag to set how far)') + '">' + (on ? 'Done' : 'Assign') + '</button>';
  }
  /** What a source moves, in a line: "Channel 1 · Cutoff ±1.5 oct · Space · Size ±10%". */
  function routedText(src) {
    return routeTable().list.filter(function (rt) { return rt.src === src && rt.live; })
      .map(function (rt) { return targetName(rt.target) + ' ' + amtText(rt.target, rt.amt, bip(rt)); }).join(', ');
  }
  /** The route from `src` to `target`, or -1. */
  function findRoute(src, target) { var l = routeTable().list; for (var i = 0; i < l.length; i++) if (l[i].src === src && l[i].target === target) return l[i].r; return -1; }
  /** The first route no source holds, or -1 (all 128 in use). */
  function freeRoute() { for (var r = 0; r < POOL_ROUTES; r++) if (!Math.round(val(rid(r, 'src')))) return r; return -1; }
  function setRoute(r, src, target, amt) {
    setParam(rid(r, 'amt'), amt, true); setParam(rid(r, 'pol'), 1, true); setParam(rid(r, 'target'), target, true); setParam(rid(r, 'src'), src, true);
  }
  function clearRoute(r) { setRoute(r, 0, -1, 0); }
  function assignable(id) { return !!(assign && isTarget(id)); }
  /** How the source being assigned stands to a control: 2 it moves it already, 1 it can, 0 not assigning. */
  function assignState(id) { return assign && isTarget(id) ? (findRoute(assign.src, id) >= 0 ? 2 : 1) : 0; }
  /** A control's modulation, for its rings: one per live route on it, in its source's colour, its reach (lo .. hi of
      the range, 0..1) and where that source has it now (at: the knob plus this route alone, -1 if not known); live
      where the engine has it with every route added (the routes' position, the same for each; -1 if not known); hot
      the route whose tag is hovered or dragged. Null when no route moves it. */
  var hotRoute = null;
  function modOf(id) {
    var list = routeTable().byTarget[id]; if (!list) return null;
    var x = toPos(id, val(id)), live = -1, rings = [];
    function c01(v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
    list.forEach(function (rt) {
      if (!rt.live) return;
      var a = rt.amt, t = srcTone(rt.src), lo, hi, s;
      if (bip(rt)) { lo = x - Math.abs(a); hi = x + Math.abs(a); } // an LFO -1..1 around the knob
      else { lo = x + Math.min(0, a); hi = x + Math.max(0, a); } // a macro 0..1, or an LFO unipolar
      s = srcNow(rt);
      rings.push({ r: rt.r, lo: c01(lo), hi: c01(hi), at: s == null ? -1 : c01(x + a * s), h: t.h, s: t.s, l: t.l });
      if (F.rt[rt.r] != null) live = F.rt[rt.r];
    });
    if (!isHost(id) && VM[id] != null) live = toPos(id, VM[id]);
    return rings.length ? { rings: rings, live: live, hot: hotRoute } : null;
  }
  /** A route's source now, as it adds to its target: a macro 0..1, an LFO from the telemetry (unipolar (s + 1) / 2), null
      while the engine sends none for it. */
  function srcNow(rt) {
    if (rt.src >= SRC_M1) return val(macroId(rt.src - SRC_M1));
    var s = F.lf[rt.src - SRC_LFO1];
    return s == null ? null : rt.uni ? (s + 1) / 2 : s;
  }
  /** A value's position in its control's range, 0..1 along the knob (Zoom by ratios), and back. */
  function toPos(id, v) { var d = ORDER[id]; return LOGP[d.s] ? Math.log(v / d.min) / Math.log(d.max / d.min) : (v - d.min) / Math.max(1e-9, d.max - d.min); }
  function fromPos(id, x) { var d = ORDER[id]; return LOGP[d.s] ? d.min * Math.pow(d.max / d.min, x) : d.min + x * (d.max - d.min); }
  /* Routes onto the page's own parameters: the engine leaves them be (they are not its targets), so the page applies
     them as the engine applies its own (Mod.hpp), once a frame before the stage is drawn: each adds amount x source
     to the knob's position in its range, the routes on one parameter add, the sum clamped. VM: those parameters'
     values this frame by id, read by mg / mv (what the stage draws); val, gval and vval stay the knobs' own (what
     the panel shows and saves). */
  var VM = {};
  function pageMod() {
    var add = null, list = routeTable().list;
    VM = {};
    for (var i = 0; i < list.length; i++) {
      var rt = list[i]; if (!rt.live || isHost(rt.target)) continue;
      var s = srcNow(rt); if (s == null) continue;
      add = add || {}; add[rt.target] = (add[rt.target] || 0) + rt.amt * s;
    }
    for (var id in add) VM[id] = fromPos(+id, Math.max(0, Math.min(1, toPos(+id, val(+id)) + add[id])));
  }
  function mg(sym) { var id = gid(sym); return VM[id] != null ? VM[id] : val(id); }
  function mv(slot, sym) { var id = vsid(slot, sym); return VM[id] != null ? VM[id] : val(id); }
  /** + Add: the first free LFO (from its table defaults: a Tempo sine a bar long) or macro (at 0). */
  function addSource(lfo) {
    var n = lfo ? POOL_LFOS : POOL_MACROS;
    for (var i = 0; i < n; i++) {
      if (lfo ? lfoOn(i) : macroOn(i)) continue;
      if (lfo) ['shape', 'sync', 'rate', 'div', 'phase', 'restart'].forEach(function (k) { var id = lid(i, k); setParam(id, ORDER[id].def, true); });
      else setParam(macroId(i), 0, true);
      setParam(lfo ? lid(i, 'on') : gid('macro' + (i + 1) + '_on'), 1, true);
      return;
    }
  }
  /** Remove a source: its targets go, then it is no longer in use. */
  function removeSource(src) {
    routeTable().list.forEach(function (rt) { if (rt.src === src) clearRoute(rt.r); });
    if (assign && assign.src === src) assign = null;
    setParam(src >= SRC_M1 ? gid('macro' + (src - SRC_M1 + 1) + '_on') : lid(src - SRC_LFO1, 'on'), 0, true);
    structure = true;
  }

  /** STAGE FX: the picture of the stage passes Mirror, then Feedback, then Tracer, each with its switch in its
      title; switched off, an effect costs nothing and its group shows only what it does. */
  function stageFxHtml() {
    function row(cells) { var o = '<div class="srow"><div class="knobs">' + cells.join(''); for (var i = cells.length; i < 6; i++) o += '<div></div>'; return o + '</div></div>'; }
    function k(sym, lab) { return knobCell(gid(sym), lab); }
    function fx(title, sym, what, body, hint) {
      var on = gval(sym) >= 0.5;
      return tgroup(sym, title, (on ? body : '') + '<div class="hint flush">' + esc(on ? hint : what) + '</div>', chipRow(gid(sym), ''));
    }
    return fx('MIRROR', 'fx_mir', 'Stage FX, first: the stage tiled around itself in mirrored copies, twice over (a kaleidoscope behind the form).',
        row([k('fx_mir_div', 'Divisions'), k('fx_mir_op', 'Opacity')]) + chipRow(gid('fx_mir_top'), 'ON TOP'),
        'Divisions: copies across each way, each copy split again as many times. Opacity: how bright the copies are. On top: the form itself in front of its copies.') +
      fx('FEEDBACK', 'fx_fb', 'Stage FX, second: the last frame drawn back under the new one, moved a little each time: trails, tunnels, spirals.',
        row([k('fx_fb_amt', 'Amount'), k('fx_fb_zoom', 'Zoom'), k('fx_fb_rot', 'Rotate'), k('fx_fb_dx', 'Drift X'), k('fx_fb_dy', 'Drift Y'), k('fx_fb_hue', 'Hue')]) +
        row([k('fx_fb_kick', 'Kick')]) + chipRow(gid('fx_fb_blend'), 'BLEND'),
        'Amount: how long the past lingers. Zoom: the past grows (a tunnel toward you) or shrinks into the form. Rotate: it turns as it goes, a spiral. Drift: it slides ' +
        'sideways or up and down. Hue: its colors turn as it ages. Kick: drum hits (the beat, with no drums) punch the zoom. Blend: Max keeps the brighter (clean trails), ' +
        'Add glows where they overlap. At no zoom, turn or drift it is plain trails.') +
      fx('TRACER', 'fx_tr', 'Stage FX, last: copies of past frames held behind the form, a fixed number of them, like a strobe.',
        row([k('fx_tr_n', 'Frames'), k('fx_tr_gap', 'Spacing'), k('fx_tr_fade', 'Fade')]),
        'Frames: how many past frames are held (up to ' + Shells.TRACER_MAX + ', at half resolution; past 24 they are held in groups, the brightest of each). ' +
        'Spacing: one frame kept in every so many, so the copies spread out. Fade: the older copies dimmer.') +
      fx('BLOOM', 'fx_bl', 'Stage FX, at the very end: a glow around everything bright, spilling over the dark around it.',
        row([k('fx_bl_str', 'Strength'), k('fx_bl_rad', 'Radius'), k('fx_bl_thr', 'Threshold')]),
        'Strength: how bright the glow. Radius: how far it spreads, from a tight halo to a wide haze. Threshold: how bright a thing must be to glow ' +
        '(low: nearly everything, high: only the brightest crests).') + colorHtml();
  }
  /** COLOR: how the stage colors its channels. The panel keeps each channel's note color either way. */
  function colorHtml() {
    var mode = Math.round(gval('col_mode')), k = function (sym, lab) { return knobCell(gid(sym), lab); };
    var cells = mode === 1 ? [k('col_spread', 'Spread'), k('col_lvl', 'Level')] : mode === 2 ? [k('col_spread', 'Spread'), k('col_speed', 'Speed')] : [];
    var o = '<div class="srow"><div class="knobs">' + cells.join(''); for (var i = cells.length; i < 6; i++) o += '<div></div>';
    return tgroup('color', 'COLOR', (mode ? chipRow(gid('col_map'), 'MAP') + o + '</div></div>' : '') + '<div class="hint flush">' + esc(COL_HINT[mode]) + '</div>', chipRow(gid('col_mode'), ''));
  }
  var COL_HINT = ['Pitch: each channel in its note\'s color (the circle of fifths), drums in their own. Palette and Cycle color the stage from a color map instead.',
                   'Palette: the channels spread along the map (Spread: how far apart), each moving further along as it plays louder (Level).',
                   'Cycle: the map runs by (Speed), each channel a step behind the next (Spread); maps that do not join end to end run back and forth.'];
  var PART_HINT = { off: 'Render: Surface, the glass as it is. Particles: the same form drawn as points that vibrate. Both: points over the glass.',
                    shell: 'Density: how many points. Scatter: how far they stray off the surface, at random. Jitter: how much each one shakes, more as it plays louder and on ' +
                           'its hits. Color jitter: each point its own hue and brightness. Point size, Softness: a hard dot or a soft blur. Fill: from the surface into the whole form. ' +
                           'Merged: its points on the shared surface, in its color.',
                    glow: 'Glow as particles: motes filling the glass instead of the light (Both: both). Density, Scatter, Jitter, Color jitter, Point size, Softness as for a shell.',
                    pearl: 'Ripple and breath have no surface of their own: when no shell is up, the pearl drawn for them is made of these points.' };
  /** A channel's particles, in its VISUAL section: Surface / Particles / Both and, unless Surface, their knobs. */
  function particleHtml(s, look) {
    var mode = Math.round(vval(s, 'v_mode')), kc = function (sym, lab) { return knobCell(vsid(s, sym), lab); };
    return '<div class="chipline">' + chipRow(vsid(s, 'v_mode'), 'RENDER') + '</div>' +
      (mode ? srow('partview', s, [kc('v_pdens', 'Density'), kc('v_pscat', 'Scatter'), kc('v_pjit', 'Jitter'), kc('v_pcol', 'Color jitter'), kc('v_psize', 'Point size'), kc('v_psoft', 'Softness')]) +
        (look === 'glow' ? '' : '<div class="srow"><div class="knobs">' + kc('v_pfill', 'Fill') + '<div></div><div></div><div></div><div></div><div></div></div></div>') : '') +
      '<div class="hint">' + esc(!mode ? PART_HINT.off : look === 'glow' ? PART_HINT.glow : look === 'shell' ? PART_HINT.shell : PART_HINT.pearl) + '</div>';
  }

  /** The two views of the stage, at the top of the Visual tab: each brings its own groups (3D form: STAGE, MOTION,
      CAMERA, LOOKS; Scope: SCOPE, CHANNELS); the stage FX and COLOR belong to both. */
  function viewHtml() {
    return '<div class="chopts vview">' + chipRow(gid('v_stage'), 'MODE') + '<span class="hint flush">' +
      (scopeOn() ? 'Each channel\'s wave as an oscilloscope draws it.' : 'The channels as glass forms in 3D.') + '</span></div>';
  }
  function focusHtml() {
    return '<div class="row center"><span class="lab w64">FOCUS</span><button class="small" data-act="focus" aria-pressed="' + ui.focus + '">' + (ui.focus ? 'On' : 'Off') + '</button>' +
      '<span class="hint flush">Dim the channels that are not open.</span></div>';
  }
  /** SCOPE: its layout and its lines. */
  function scopeHtml() {
    var k = function (sym, lab) { return knobCell(gid(sym), lab); };
    return chipRow(gid('sc_lay'), 'LAYOUT') +
      '<div class="srow"><div class="knobs">' + k('sc_gain', 'Height') + k('sc_lw', 'Line width') + k('sc_glow', 'Glow') + k('sc_smooth', 'Smoothing') + '<div></div><div></div></div></div>' +
      chipRow(gid('sc_cyc'), 'CYCLES') + chipRow(gid('sc_lvl'), 'LEVEL') + chipRow(gid('sc_grid'), 'GRID') + chipRow(gid('sc_key'), 'LEGEND') + focusHtml() +
      '<div class="hint flush">Stacked: a lane per channel shown (its eye in CHANNELS), top to bottom. Overlaid: every wave on one line, a key along the top. ' +
        'Each wave is locked to its own period, so overlaid they compare in shape, not in time. Cycles: the period shown that many times. ' +
        'Height: how tall a wave draws. Level: Normalized, every wave full height; True, as loud as it plays (its level in it), so the loud ones draw big. ' +
        'Glow: a soft halo round the line (0: a plain crisp line). Smoothing: the line follows each new wave gently instead of jumping (0: every frame as it comes). ' +
        'Legend: each channel\'s name and note (Stacked: on its lane; Overlaid: a key along the top); off, the waves have the room to themselves. ' +
        'A muted or hidden channel leaves the scope.</div>';
  }
  /** STAGE (3D form): Nested / Merged, Focus. */
  function stageHtml() {
    return '<div class="row center"><span class="lab w64">FORM</span><div class="seg"><button data-view="nested" aria-pressed="' + (ui.view === 'nested') + '">NESTED</button><button data-view="merged" aria-pressed="' + (ui.view === 'merged') + '">MERGED</button></div></div>' +
      '<div class="hint flush">' + (ui.view === 'nested' ? 'Each channel its own shell, inner to outer.' : 'Every channel\'s wave added into one surface.') + '</div>' + focusHtml();
  }
  function visualHtml(slots) {
    var sc = scopeOn(), html = singleHtml() + viewHtml() + (sc ? tgroup('scope', 'SCOPE', scopeHtml()) + stageFxHtml() : tgroup('stage', 'STAGE', stageHtml()) +
      tgroup('motion', 'MOTION',
        '<div class="srow"><div class="knobs">' + knobCell(gid('v_flow'), 'Flow') + knobCell(gid('v_pulse'), 'Pulse') + knobCell(gid('v_motion'), 'Motion') + '<div></div><div></div><div></div></div></div>' +
        chipRow(gid('v_pdiv'), 'PULSE EVERY') +
        '<div class="hint flush">Flow: how slowly the form follows the music, from every transient to a slow tide. Pulse: a swell on the beat, ' +
          'only while playing. Motion: how fast its waves drift (the turning is the camera\'s Orbit).</div>') +
      tgroup('camera', 'CAMERA',
        '<div class="srow"><div class="knobs">' + knobCell(gid('v_tilt'), 'Tilt') + knobCell(gid('v_zoom'), 'Zoom') + knobCell(gid('v_persp'), 'Perspective') +
          knobCell(gid('v_orbit'), 'Orbit') + '<div></div><div></div></div></div>' +
        '<div class="chips"><span class="lab">VIEW</span>' + CAM_NAMES.map(function (v) { return '<button class="chip" data-camview="' + v[0] + '" aria-pressed="false">' + v[1] + '</button>'; }).join('') + '</div>' +
        '<div class="hint flush">On the stage: drag to turn it, the wheel zooms (0.5x to 16x), a double-click resets. Tilt: seen from above (90°: straight down) or below. ' +
          'Perspective: how much nearer parts grow. Orbit: how fast it turns by itself. Side lays the form down, its axis left to right.</div>') + stageFxHtml());
    // the channels: in 3D their looks and eyes, in the scope their eyes only
    var rows = '';
    for (var i = 0; i < slots.length; i++) {
      var s = slots[i], look = lookOf(s);
      rows += '<div class="vrow"><canvas class="bar" data-slot="' + s + '"></canvas><span class="nm">' + slotName(s) + '</span>';
      if (!sc) {
        rows += '<div class="lookseg">';
        for (var j = 0; j < LOOKS.length; j++) rows += '<button data-look="' + LOOKS[j][0] + '" data-slot="' + s + '" aria-pressed="' + (look === LOOKS[j][0]) + '">' + LOOKS[j][1] + '</button>';
        rows += '</div>';
      } else rows += '<span class="grow"></span>';
      rows += '<button class="tog eye" data-id="' + sid(s, 'show') + '" data-toggle="1" title="' + (sc ? 'Show in the scope' : 'Show in the visualizer') + '"></button></div>';
    }
    rows = rows || '<div class="empty">No channels playing.</div>';
    return html + (sc ? tgroup('lanes', 'CHANNELS', rows) : tgroup('looks', 'LOOKS', rows));
  }

  /** The Visual tab's groups folded: what they hold, in a line. */
  function pc(sym) { return Math.round(gval(sym) * 100) + '%'; }
  function sgn(sym) { var v = Math.round(gval(sym) * 100); return (v > 0 ? '+' : '') + v + '%'; }
  var TSUMS = {
    stage: function () { return ui.view + (ui.focus ? ' · focus' : ''); },
    scope: function () {
      var cy = Math.round(gval('sc_cyc'));
      return nameOf(gid('sc_lay')).toLowerCase() + ' · ' + cy + ' cycle' + (cy === 1 ? '' : 's') + ' · ' + nameOf(gid('sc_lvl')).toLowerCase() + (gval('sc_grid') >= 0.5 ? ' · grid' : '') + (gval('sc_key') >= 0.5 ? '' : ' · no legend') + (ui.focus ? ' · focus' : '');
    },
    motion: function () { return 'flow ' + pc('v_flow') + ' · pulse ' + pc('v_pulse') + ' every ' + nameOf(gid('v_pdiv')) + ' · motion ' + pc('v_motion'); },
    camera: function () { return 'tilt ' + Math.round(gval('v_tilt')) + '° · zoom ' + gval('v_zoom').toFixed(2) + 'x · orbit ' + pc('v_orbit'); },
    fx_mir: function () { return Math.round(gval('fx_mir_div')) + ' divisions · ' + pc('fx_mir_op') + (gval('fx_mir_top') >= 0.5 ? ' · on top' : ''); },
    fx_fb: function () { return 'amount ' + pc('fx_fb_amt') + ' · zoom ' + sgn('fx_fb_zoom') + ' · rotate ' + sgn('fx_fb_rot') + ' · ' + nameOf(gid('fx_fb_blend')).toLowerCase(); },
    fx_tr: function () { return Math.round(gval('fx_tr_n')) + ' frames · ' + Knobs.fmt('frames', gval('fx_tr_gap')) + ' · fade ' + pc('fx_tr_fade'); },
    fx_bl: function () { return 'strength ' + pc('fx_bl_str') + ' · radius ' + pc('fx_bl_rad') + ' · threshold ' + pc('fx_bl_thr'); },
    color: function () { var m = Math.round(gval('col_mode')); return m ? nameOf(gid('col_map')).toLowerCase() : 'each channel its note\'s color'; },
    looks: function () { var n = activeSlots().length; return n + ' channel' + (n === 1 ? '' : 's'); },
    lanes: function () { var n = 0, a = activeSlots(); a.forEach(function (s) { if (sval(s, 'show') >= 0.5 && sval(s, 'mute') < 0.5) n++; }); return n + ' of ' + a.length + ' shown'; }
  };

  /** Values into the DOM in place: texts, chip states, selects, toggles. */
  function refresh() {
    dirty = false; lastRefresh = performance.now();
    var txt = function (key, s) { document.querySelectorAll('[data-txt="' + key + '"]').forEach(function (el) { if (el.textContent !== s) el.textContent = s; }); };
    var pr = PRESETS[presetNow];
    txt('preset', patchFile || (pr ? pr.n : '—'));
    document.querySelectorAll('[data-txt="preset"]').forEach(function (el) { el.title = patchFile ? 'Patch file: ' + patchFile : pr ? pr.b : ''; });
    txt('rectime', rec.on ? fmtTime((performance.now() - rec.t0) / 1000) : '');
    document.querySelectorAll('.recbtn').forEach(function (b) { if (b.classList.contains('on') !== rec.on) b.classList.toggle('on', rec.on); });
    txt('key', keyName()); txt('chord', chordName());
    txt('progpos', F.pl > 0 ? '· ' + (F.pp + 1) + '/' + F.pl : '· ' + romanOf(F.cd % 7));
    txt('tempo', (F.bpm ? F.bpm : gval('tempo')).toFixed(F.bpm && Math.abs(F.bpm - Math.round(F.bpm)) > 0.05 ? 1 : 0));
    var hostSync = F.host && gval('sync') === 0;
    txt('sync', hostSync ? 'HOST' : 'INTERNAL');
    refreshCpu();
    document.querySelectorAll('[data-txt="sync"]').forEach(function (el) { el.className = 'pill' + (hostSync ? '' : ' int'); });
    document.querySelectorAll('[data-shown]').forEach(function (el) { var t = sval(+el.getAttribute('data-shown'), 'show') >= 0.5 ? 'Shown' : 'Hidden'; if (el.textContent !== t) el.textContent = t; });
    document.querySelectorAll('[data-chsub]').forEach(function (el) { var s = slotSub(+el.getAttribute('data-chsub')); if (el.textContent !== s) el.textContent = s; });
    document.querySelectorAll('[data-txt="play"]').forEach(function (el) {
      var want = F.playing ? 'pause' : 'play';
      if (el.getAttribute('data-state') !== want) {
        el.setAttribute('data-state', want);
        el.innerHTML = F.playing ? '<svg width="14" height="14" viewBox="0 0 16 16" fill="currentColor"><rect x="3" y="2" width="3.5" height="12" rx="1"></rect><rect x="9.5" y="2" width="3.5" height="12" rx="1"></rect></svg>'
                                 : '<svg width="14" height="14" viewBox="0 0 16 16" fill="currentColor"><path d="M4 2.5 L13.5 8 L4 13.5 Z"></path></svg>';
      }
    });
    // chips and toggles; the camera's view chips light while the camera is at that view (a drag leaves it)
    document.querySelectorAll('button.chip[data-id]').forEach(function (b) {
      var cv = Math.round(val(+b.getAttribute('data-id'))), same = b.getAttribute('data-same');
      var want = cv === +b.getAttribute('data-val') || (same != null && cv === +same) ? 'true' : 'false';
      if (b.getAttribute('aria-pressed') !== want) b.setAttribute('aria-pressed', want);
    });
    var vnow = camViewNow();
    document.querySelectorAll('[data-camview]').forEach(function (b) {
      var want = b.getAttribute('data-camview') === vnow ? 'true' : 'false';
      if (b.getAttribute('aria-pressed') !== want) b.setAttribute('aria-pressed', want);
    });
    document.querySelectorAll('[data-toggle]').forEach(function (b) {
      var onv = val(+b.getAttribute('data-id')) >= 0.5 ? 'true' : 'false';
      if (b.getAttribute('aria-pressed') !== onv || (b.classList.contains('eye') && !b.firstChild)) {
        b.setAttribute('aria-pressed', onv); if (b.classList.contains('eye')) b.innerHTML = eyeSvg(onv === 'true');
      }
    });
    document.querySelectorAll('select.sel[data-id]').forEach(function (sl) {
      if (document.activeElement === sl) return;
      var v = String(Math.round(val(+sl.getAttribute('data-id'))));
      if (sl.value !== v) sl.value = v;
    });
    // the chord chips above the change: the manual chord while the progression is off; Custom's length and the
    // degree of its selected step (named for the key: ii, vii°)
    var pm = Math.round(gval('prog'));
    document.querySelectorAll('[data-chips-prog]').forEach(function (el) {
      var sig = pm === 0 ? 'off:' + Math.round(gval('scale')) : pm === PROG_CUSTOM ? 'c' + progStep() + ':' + Math.round(gval('scale')) : '';
      if (el.getAttribute('data-sig') === sig) return;
      el.innerHTML = pm === 0 ? degreeChips(gid('chord'), 'CHORD') : pm === PROG_CUSTOM ? progEditHtml() : '';
      el.setAttribute('data-sig', sig);
    });
    document.querySelectorAll('[data-act="prog-custom"]').forEach(function (b) { var want = pm > 0 && pm < PROG_CUSTOM ? '' : 'none'; if (b.style.display !== want) b.style.display = want; });
    // a closed section's head: what is in it, in a line
    var U = { min: 0, max: 1 }, f = function (s, sym, fm) { return Knobs.fmt(fm, sval(s, sym), ORDER[sid(s, sym)]); };
    var SUMS = {
      pat: function (s) {
        if (genOf(s) === GEN_RHYTHM) return hitsOf(s) + ' hits · ' + Knobs.fmt('div', sval(s, 'arp_div')) + (Math.round(sval(s, 'rhy_rotate')) ? ' · rotate ' + Math.round(sval(s, 'rhy_rotate')) : '') +
          (sval(s, 'rhy_chance') < 0.99 ? ' · chance ' + f(s, 'rhy_chance', 'pct') : '') + notesSum(s);
        return (genOf(s) === GEN_CHORD ? Knobs.fmt('div', sval(s, 'arp_div')) : slotSub(s).toLowerCase()) + ' · ' + Math.round(sval(s, 'arp_len')) + ' steps · density ' + f(s, 'arp_density', 'pct') + notesSum(s); },
      drum: function (s) { return (nameOf(sid(s, 'drum_model')) + ' · tune ' + f(s, 'drum_tune', 'sint') + ' · decay ' + f(s, 'drum_decay', 'pct') +
                                  ' · tone ' + f(s, 'drum_tone', 'pct')).toLowerCase(); },
      chord: function (s) { return (nameOf(sid(s, 'chord_notes')) + ' · ' + nameOf(sid(s, 'chord_voicing')) + ' · ' + nameOf(sid(s, 'chord_lead')) + ' · ' + nameOf(sid(s, 'chord_rhythm'))).toLowerCase() +
                                   (chordArp(s) ? ' ' + nameOf(sid(s, 'arp_mode')).toLowerCase() + (sval(s, 'chord_ring') >= 0.5 ? ' · let ring' : '')
                                                : sval(s, 'chord_strum') > 0.01 ? ' · strum ' + f(s, 'chord_strum', 'pct') : ''); },
      pitch: function (s) { return 'degree ' + f(s, 'degree', 'deg') + ' · octave ' + f(s, 'oct', 'octs') + (genOf(s) === GEN_ARP ? ' · range ' + f(s, 'arp_range', 'range') : ''); },
      osc: function (s) { return 'shape ' + f(s, 'shape', 'pct') + ' · detune ' + f(s, 'detune', 'cents') + (sval(s, 'sub') > 0.01 ? ' · sub ' + f(s, 'sub', 'pct') : ''); },
      filt: function (s) { return Knobs.fmt('type', sval(s, 'filter_type'), U) + ' ' + Knobs.fmt('hz', sval(s, 'cutoff'), U) + ' · reso ' + f(s, 'reso', 'pct'); },
      amp: function (s) { return 'A ' + f(s, 'amp_a', 'att') + ' · D ' + f(s, 'amp_d', 'dec') + ' · S ' + f(s, 'amp_s', 'pct') + ' · R ' + f(s, 'amp_r', 'dec'); },
      drive: function (s) { return sval(s, 'fx_drive') <= 0 ? 'off' : 'amount ' + f(s, 'fx_drive', 'pct') + ' · tone ' + f(s, 'fx_tone', 'bip'); },
      cho: function (s) { return !chorusOn(s) ? 'off' : nameOf(sid(s, 'cho_mode')).toLowerCase() + ' · rate ' + f(s, 'cho_rate', vibrato(s) ? 'vhz' : 'chz') + ' · depth ' + f(s, 'cho_depth', 'pct') +
                                 (vibrato(s) ? '' : ' · mix ' + f(s, 'cho_mix', 'pct')); },
      dly: function (s) { return sval(s, 'dly_mix') <= 0 ? 'off' : nameOf(sid(s, 'dly_div')) + ' · feedback ' + f(s, 'dly_fb', 'pct') + ' · mix ' + f(s, 'dly_mix', 'pct') + (sval(s, 'dly_pp') >= 0.5 ? ' · ping-pong' : ''); },
      rev: function (s) { return sval(s, 'rev_send') > 0 ? 'send ' + f(s, 'rev_send', 'pct') : 'off'; },
      duck: function (s) { return ducked(s) ? 'by ' + slotName(Math.round(sval(s, 'duck_src')) - 1) + ' · ' + f(s, 'duck_amt', 'pct') : 'off'; },
      gen: function (s) { return nameOf(sid(s, 'gen')).toLowerCase() + ' · ' + SUMS[genOf(s) === GEN_CHORD ? 'chord' : 'pat'](s); },
      inst: function (s) { return nameOf(sid(s, 'inst')).toLowerCase() + ' · ' + SUMS[isDrum(s) ? 'drum' : 'osc'](s); },
      vg: function (s) { return SUMS.vis(s); },
      part: function (s) {
        var m = Math.round(vval(s, 'v_mode'));
        return !m ? 'surface' : nameOf(vsid(s, 'v_mode')).toLowerCase() + ' · density ' + Math.round(vval(s, 'v_pdens') * 100) + '%' +
          (vval(s, 'v_pfill') > 0.005 ? ' · fill ' + Math.round(vval(s, 'v_pfill') * 100) + '%' : '');
      },
      fx: function (s) {
        var out = [];
        if (sval(s, 'fx_drive') > 0) out.push('drive');
        if (chorusOn(s)) out.push(nameOf(sid(s, 'cho_mode')).toLowerCase());
        if (sval(s, 'dly_mix') > 0) out.push('delay ' + nameOf(sid(s, 'dly_div')));
        if (sval(s, 'rev_send') > 0) out.push('reverb ' + f(s, 'rev_send', 'pct'));
        if (ducked(s)) out.push('ducked');
        return out.length ? out.join(' · ') : 'none';
      },
      vis: function (s) { return lookOf(s) + (sval(s, 'show') >= 0.5 ? '' : ' · hidden') + ' · presence ' + Math.round(vval(s, 'v_presence') * 100) + '% · detail ' + Math.round(vval(s, 'v_detail') * 100) + '%' +
                                 (Math.abs(vval(s, 'v_size') - 1) > 0.005 ? ' · size ' + vval(s, 'v_size').toFixed(2) + 'x' : '') +
                                 (vval(s, 'v_alpha') < 0.995 ? ' · alpha ' + Math.round(vval(s, 'v_alpha') * 100) + '%' : '') +
                                 (Math.round(vval(s, 'v_mode')) ? ' · ' + nameOf(vsid(s, 'v_mode')).toLowerCase() + ' ' + Math.round(vval(s, 'v_pdens') * 100) + '%' +
                                   (vval(s, 'v_pfill') > 0.005 ? ' · fill ' + Math.round(vval(s, 'v_pfill') * 100) + '%' : '') : ''); } };
    document.querySelectorAll('[data-tsum]').forEach(function (el) {
      var t = TSUMS[el.getAttribute('data-tsum')](); if (el.textContent !== t) el.textContent = t;
    });
    // what each LFO and macro moves, in its head; what moves each channel, in its card
    document.querySelectorAll('[data-srcsum]').forEach(function (el) {
      var src = +el.getAttribute('data-srcsum'), t = srcSum(src);
      if (el.textContent !== t) { el.textContent = t; el.title = t; }
    });
    document.querySelectorAll('[data-chmod]').forEach(function (el) {
      var cs = +el.getAttribute('data-chmod'), out = [];
      routeTable().list.forEach(function (rt) {
        var d = ORDER[rt.target];
        if (rt.live && d && d.slot === cs) out.push(srcName(rt.src) + ' → ' + (TNAMES[d.s] || d.n) + ' ' + amtText(rt.target, rt.amt, bip(rt)));
      });
      var t = out.length ? out.join(' · ') : 'nothing moves this channel';
      if (el.textContent !== t) el.textContent = t;
    });
    refreshMod();
    document.querySelectorAll('.pinbtn[data-pintog]').forEach(function (b) { var want = String(pinned(+b.getAttribute('data-pintog'))); if (b.getAttribute('aria-pressed') !== want) b.setAttribute('aria-pressed', want); });
    document.querySelectorAll('[data-sum]').forEach(function (el) {
      var ref = el.getAttribute('data-sum').split(':'), s = +ref[0], t = secsOf(s)[ref[1]] && !el.classList.contains('cgsum') ? '' : SUMS[ref[1]](s);
      if (el.textContent !== t) el.textContent = t;
    });
  }

  /** A source's head line: an LFO's shape and speed, a macro's value; what it moves. */
  function srcSum(src) {
    var moved = routedText(src), head;
    if (src >= SRC_M1) head = Math.round(val(macroId(src - SRC_M1)) * 100) + '%';
    else {
      var l = src - SRC_LFO1, rs = Math.round(val(lid(l, 'restart')));
      head = nameOf(lid(l, 'shape')).toLowerCase() + ' · ' + (Math.round(val(lid(l, 'sync'))) === 1 ? nameOf(lid(l, 'div')) : Knobs.fmt('lhz', val(lid(l, 'rate')))) +
        (rs ? ' · restarts on ' + slotName(rs - 1).toLowerCase() : '');
    }
    return head + ' · ' + (moved ? 'moves ' + moved : 'moves nothing yet');
  }

  /** The modulation's numbers in place: each knob's tags (rebuilt when the routes on it change, so a route made by a
      click shows its tag at once; else their texts), each target row's polarity, typed amount and units; a hovered
      or dragged route lit. The tag menu shuts if its route went. */
  function refreshMod() {
    var tab = routeTable();
    document.querySelectorAll('[data-tags]').forEach(function (el) {
      var list = tab.byTarget[+el.getAttribute('data-tags')] || [], sig = list.map(function (rt) { return rt.r + ':' + rt.src; }).join(',');
      if (el.getAttribute('data-sig') !== sig) {
        el.innerHTML = list.map(function (rt) {
          return '<span class="mtag" data-route="' + rt.r + '" style="' + srcVars(rt.src) + '" title="' + esc(srcName(rt.src) + ' → ' + targetName(rt.target)) +
            '. Drag up or down: how far (Shift: fine). Double-click: +25%. Right-click (long-press): type it, Bipolar / Unipolar, remove."><b></b></span>';
        }).join('');
        el.setAttribute('data-sig', sig);
      }
      for (var i = 0; i < el.children.length; i++) {
        var tg = el.children[i], rt = tab.byR[+tg.getAttribute('data-route')]; if (!rt) continue;
        var tx = tagText(rt), hot = hotRoute === rt.r;
        if (tg.firstChild.textContent !== tx) tg.firstChild.textContent = tx;
        if (tg.classList.contains('hot') !== hot) tg.classList.toggle('hot', hot);
      }
    });
    document.querySelectorAll('.route[data-route]').forEach(function (el) { var hot = hotRoute === +el.getAttribute('data-route'); if (el.classList.contains('hot') !== hot) el.classList.toggle('hot', hot); });
    document.querySelectorAll('[data-poltxt]').forEach(function (b) {
      var rt = tab.byR[+b.getAttribute('data-poltxt')], t = !rt ? '' : rt.uni || rt.src >= SRC_M1 ? '↑ Unipolar' : '↕ Bipolar';
      if (b.textContent !== t) b.textContent = t;
    });
    document.querySelectorAll('[data-amtin]').forEach(function (inp) {
      var rt = tab.byR[+inp.getAttribute('data-amtin')]; if (!rt || document.activeElement === inp) return;
      var t = String(Math.round(rt.amt * 100)); if (inp.value !== t) inp.value = t;
    });
    document.querySelectorAll('[data-amtu]').forEach(function (el) {
      var rt = tab.byR[+el.getAttribute('data-amtu')], t = rt ? amtText(rt.target, rt.amt, bip(rt)) : '';
      if (el.textContent !== t) el.textContent = t;
    });
    if (menu && menu.pin == null && !tab.byR[menu.r]) closeMenu();
  }

  /** A tag's menu (right-click, or a long press by touch): the amount typed (% of the target's range) with its units
      beside it, Bipolar / Unipolar (an LFO's), remove. Enter, Esc or a press anywhere else closes it. */
  var menu = null;
  function openMenu(r, x, y) {
    closeMenu();
    var rt = routeOf(r); if (!rt) return;
    var el = document.createElement('div'), S = uiScale();
    el.className = 'modmenu'; el.setAttribute('style', srcVars(rt.src));
    el.innerHTML = '<div class="mmhead"><span class="srcdot"></span><b>' + esc(srcName(rt.src)) + '</b><span>→ ' + esc(targetName(rt.target)) + '</span></div>' +
      '<div class="mmrow"><span class="lab">AMOUNT</span><input class="num" data-amtin="' + r + '" inputmode="decimal"><span class="pctu">%</span><span class="amtu" data-amtu="' + r + '"></span></div>' +
      (rt.src < SRC_M1 ? chipRow(rid(r, 'pol'), '') : '<div class="hint flush">A macro moves it one way: up from the knob (down for a negative amount).</div>') +
      '<button class="tbtn mmrm" data-act="route-clear" data-route="' + r + '">Remove</button>';
    elUI.appendChild(el);
    menu = { r: r, el: el };
    refreshMod();
    var LW = window.innerWidth / S, LH = window.innerHeight / S;
    el.style.left = Math.max(8, Math.min(LW - el.offsetWidth - 8, x / S + 8)) + 'px';
    el.style.top = Math.max(8, Math.min(LH - el.offsetHeight - 8, y / S + 8)) + 'px';
    var inp = el.querySelector('input'); if (!MINI) { inp.focus(); inp.select(); } // by touch the keyboard waits for a tap on the box
  }
  function closeMenu() { if (!menu) return; if (menu.el.parentNode) menu.el.parentNode.removeChild(menu.el); menu = null; }

  /* a tag: drag it up or down for its amount (Shift: fine), a double-click sets +25% (what a new route gets), a right-click
     or a long press by touch opens its menu; hovering one, or its target row, lights its ring alone */
  var lastTag = { r: -1, t: 0 }, LONG_MS = 500;
  document.addEventListener('pointerdown', function (e) {
    var t = e.target;
    if (menu && !(t.closest && t.closest('.modmenu'))) closeMenu();
    var tg = t.closest ? t.closest('.mtag') : null; if (!tg) return;
    var r = +tg.getAttribute('data-route'); if (!routeOf(r)) return;
    e.preventDefault();
    if (e.button === 2) { openMenu(r, e.clientX, e.clientY); return; }
    if (e.button !== 0) return;
    var aid = rid(r, 'amt'), now = performance.now(), x0 = e.clientX, y0 = e.clientY;
    if (lastTag.r === r && now - lastTag.t < 350) { lastTag.r = -1; setParam(aid, AMT_NEW, true); return; }
    lastTag = { r: r, t: now };
    drag = { cv: tg, id: aid, pid: e.pointerId, x: x0, y: y0, v: val(aid), h: false, rate: 0.5, tag: { r: r, v0: val(aid), moved: 0 } };
    hotRoute = r; dirty = true;
    send('edit ' + aid + ' 1');
    if (e.pointerType && e.pointerType !== 'mouse') drag.tag.press = setTimeout(function () {
      if (!drag || !drag.tag || drag.tag.r !== r || drag.tag.moved > 8) return;
      if (val(aid) !== drag.tag.v0) setParam(aid, drag.tag.v0); // held still: the amount as it was, and the menu
      endDrag(); openMenu(r, x0, y0);
    }, LONG_MS);
    try { tg.setPointerCapture(e.pointerId); } catch (err) { /* fine */ }
  });
  document.addEventListener('input', function (e) {
    var t = e.target; if (!t.hasAttribute || !t.hasAttribute('data-amtin')) return;
    var v = parseFloat(String(t.value).replace(',', '.').replace('−', '-'));
    if (isFinite(v)) setParam(rid(+t.getAttribute('data-amtin'), 'amt'), Math.max(-1, Math.min(1, v / 100)), true);
  });
  /** A macro's name typed: click it; Enter or clicking away keeps it (empty: its own name again), Esc does not. */
  function renameMacro(el) {
    var m = +el.getAttribute('data-rename'), inp = document.createElement('input'), done = false;
    inp.type = 'text'; inp.className = 'renamein'; inp.value = macroName(m); inp.maxLength = 24;
    el.style.display = 'none'; el.parentNode.insertBefore(inp, el.nextSibling);
    function finish(keep) {
      if (done) return; done = true;
      var v = inp.value.trim();
      if (keep) { if (!v || v === (MACRO_NAMES[m] || 'Macro ' + (m + 1))) delete ui.mnames[m]; else ui.mnames[m] = v; saveUi(); }
      if (inp.parentNode) inp.parentNode.removeChild(inp);
      el.style.display = ''; structure = true;
    }
    inp.addEventListener('keydown', function (k) {
      if (k.keyCode === 13) finish(true); else if (k.keyCode === 27) finish(false);
      k.stopPropagation(); // typed here, not played on the engine
    });
    inp.addEventListener('keyup', function (k) { k.stopPropagation(); });
    inp.addEventListener('blur', function () { finish(true); });
    inp.focus(); inp.select();
  }

  /* ------------------------------------------------------------ events */
  /** What was opened last, for One at a time to keep when it is switched on. */
  function remember(s, grp, sec) { ui.last = { ch: s, grp: grp || null, sec: sec || null }; }
  /** One at a time switched on: everything shut but the last thing opened (its channel, group and section; and in
      the Visual tab, its group). */
  function snapShut() {
    var L = ui.last;
    ui.tgrp = {}; if (ui.lastT) ui.tgrp[ui.lastT] = true; // the Visual tab: its last group opened
    ui.open = {}; ui.grps = {}; ui.secs = {};
    if (!L || L.ch == null || sval(L.ch, 'gen') <= 0) return;
    ui.open[L.ch] = true; ui.sel = L.ch;
    if (L.grp) { ui.grps[L.ch] = {}; ui.grps[L.ch][L.grp] = true; }
    if (L.sec && GRP_OF[L.sec] === L.grp) { ui.secs[L.ch] = {}; ui.secs[L.ch][L.sec] = true; }
  }
  /** A channel opened: in One at a time the only one. */
  function openChannel(s) {
    if (ui.single) ui.open = {};
    ui.open[s] = true; ui.sel = s; reveal = true;
    if (!ui.last || ui.last.ch !== s) remember(s);
  }
  function pick(s) {
    if (ui.rail) { ui.rail = false; ui.tab = 'channels'; openChannel(s); }
    else if (isOpen(s)) { delete ui.open[s]; if (ui.last && ui.last.ch === s) ui.last = null; }
    else openChannel(s);
    structure = true; saveUi();
  }
  /* the tempo typed: click the BPM; Enter or clicking away sets it, Esc keeps the old one */
  function editTempo(el) {
    if (F.host && gval('sync') === 0) { toast('Following the host\'s tempo: switch SYNC off to set your own'); return; }
    var inp = document.createElement('input'), done = false;
    inp.type = 'text'; inp.inputMode = 'decimal'; inp.className = 'big tempo tempoin'; inp.value = el.textContent;
    el.style.display = 'none'; el.parentNode.insertBefore(inp, el.nextSibling);
    function finish(keep) {
      if (done) return; done = true;
      var v = parseFloat(inp.value.replace(',', '.'));
      if (keep && isFinite(v)) setParam(gid('tempo'), v, true);
      if (inp.parentNode) inp.parentNode.removeChild(inp);
      el.style.display = ''; dirty = true;
    }
    inp.addEventListener('keydown', function (k) {
      if (k.keyCode === 13) finish(true); else if (k.keyCode === 27) finish(false);
      k.stopPropagation(); // typed here, not played on the engine
    });
    inp.addEventListener('keyup', function (k) { k.stopPropagation(); });
    inp.addEventListener('blur', function () { finish(true); });
    inp.focus(); inp.select();
  }
  document.addEventListener('click', function (e) {
    if (e.target.matches && e.target.matches('span.big.tempo')) { editTempo(e.target); return; }
    if (e.target.matches && e.target.matches('[data-rename]')) { renameMacro(e.target); return; }
    if (e.target.matches && e.target.matches('canvas.prog') && Math.round(gval('prog')) === PROG_CUSTOM) {
      var rb = e.target.getBoundingClientRect(), pn = Math.round(gval('prog_len'));
      progSel = Math.max(0, Math.min(pn - 1, Math.floor((e.clientX - rb.left) / rb.width * pn))); dirty = true; return;
    }
    var t = e.target.closest ? e.target.closest('button,[data-pick]') : null;
    if (!t) return;
    var act = t.getAttribute('data-act');
    if (t.hasAttribute('data-tab')) { ui.tab = t.getAttribute('data-tab'); structure = true; saveUi(); return; }
    if (t.hasAttribute('data-pick')) { pick(+t.getAttribute('data-pick')); return; }
    if (t.hasAttribute('data-pintog')) { togglePin(+t.getAttribute('data-pintog')); return; }
    if (t.hasAttribute('data-csec')) {
      var ref = t.getAttribute('data-csec').split(':'), cs = +ref[0], o = {}, cur = secsOf(cs), opening = !cur[ref[1]];
      if (!ui.single || !opening) for (var key in cur) o[key] = cur[key];
      o[ref[1]] = opening; ui.secs[cs] = o; ui.sel = cs;
      if (opening) remember(cs, GRP_OF[ref[1]], ref[1]);
      else if (ui.last && ui.last.ch === cs && ui.last.sec === ref[1]) ui.last.sec = null;
      structure = true; saveUi(); return;
    }
    if (t.hasAttribute('data-tgrp')) {
      var tk = t.getAttribute('data-tgrp'), topen = !ui.tgrp[tk];
      if (ui.single && topen) ui.tgrp = {};
      ui.tgrp[tk] = topen; ui.lastT = topen ? tk : (ui.lastT === tk ? null : ui.lastT);
      structure = true; saveUi(); return;
    }
    if (t.hasAttribute('data-cgrp')) {
      var gref = t.getAttribute('data-cgrp').split(':'), gs = +gref[0], gid0 = gref[1], g = {}, gcur = ui.grps[gs] || {}, gopen = !gcur[gid0];
      if (!ui.single || !gopen) for (var gk in gcur) g[gk] = gcur[gk];
      g[gid0] = gopen; ui.grps[gs] = g; ui.sel = gs;
      if (gopen) {
        // One at a time: the sections of the other groups close with them
        var sc = secsOf(gs), keepS = {}, lastS = null;
        for (var sk in sc) if (sc[sk] && (!ui.single || GRP_OF[sk] === gid0)) { keepS[sk] = true; if (GRP_OF[sk] === gid0) lastS = sk; }
        ui.secs[gs] = keepS; remember(gs, gid0, lastS);
      } else if (ui.last && ui.last.ch === gs && ui.last.grp === gid0) { ui.last.grp = null; ui.last.sec = null; }
      structure = true; saveUi(); return;
    }
    if (t.hasAttribute('data-view')) { ui.view = t.getAttribute('data-view'); structure = true; saveUi(); return; }
    if (t.hasAttribute('data-look')) { ui.looks[t.hasAttribute('data-slot') ? +t.getAttribute('data-slot') : ui.sel] = t.getAttribute('data-look'); structure = true; saveUi(); return; }
    if (t.hasAttribute('data-camview')) { camView(t.getAttribute('data-camview')); return; }
    if (act === 'single') { ui.single = t.getAttribute('data-single') === '1'; if (ui.single) snapShut(); structure = true; saveUi(); return; }
    if (t.classList.contains('chip')) {
      var cid = +t.getAttribute('data-id'), cv = +t.getAttribute('data-val'), cd = ORDER[cid];
      setParam(cid, cv, true);
      if (cd && cd.s === 'gen' && cv === GEN_RHYTHM && !isDrum(cd.slot)) setParam(sid(cd.slot, 'inst'), INST_DRUM, true); // a rhythm wants a drum
      return;
    }
    if (t.hasAttribute('data-toggle')) { var id = +t.getAttribute('data-id'); setParam(id, val(id) >= 0.5 ? 0 : 1, true); return; }
    if (act === 'dock') { ui.dock = other(ui.dock); structure = true; saveUi(); }
    else if (act === 'rail') { ui.rail = !ui.rail; structure = true; saveUi(); }
    else if (act === 'bar') toggleBar();
    else if (act === 'focus') { ui.focus = !ui.focus; structure = true; saveUi(); }
    else if (act === 'play') { send('key 1 32 0'); send('key 0 32 0'); }
    else if (act === 'save') savePatch();
    else if (act === 'open') openPatch();
    else if (act === 'rec') toggleRec();
    else if (act === 'prog-custom') progToCustom(Math.round(gval('prog')));
    else if (act === 'assign') {
      var asrc = +t.getAttribute('data-src');
      assign = assign && assign.src === asrc ? null : { src: asrc }; structure = true;
    }
    else if (act === 'assign-done') { assign = null; structure = true; }
    else if (act === 'route-clear') { clearRoute(+t.getAttribute('data-route')); closeMenu(); }
    else if (act === 'pol') { var pid = rid(+t.getAttribute('data-route'), 'pol'); setParam(pid, Math.round(val(pid)) ? 0 : 1, true); }
    else if (act === 'add-lfo' || act === 'add-macro') addSource(act === 'add-lfo');
    else if (act === 'rm-src') removeSource(+t.getAttribute('data-src'));
    else if (act === 'to-mod') { ui.tab = 'modulation'; structure = true; saveUi(); }
    else if (act === 'add') {
      var slots = activeSlots();
      for (var s = 0; s < SLOTS; s++) if (slots.indexOf(s) < 0) { setParam(sid(s, 'gen'), 1, true); ui.tab = 'channels'; openChannel(s); saveUi(); break; }
      structure = true;
    }
    else if (act === 'preset-prev' || act === 'preset-next') {
      var n = PRESETS.length, d = act === 'preset-next' ? 1 : -1; if (!n) return;
      send('preset ' + (presetNow < 0 ? (d > 0 ? 0 : n - 1) : (presetNow + d + n) % n)); // from a file: the first or the last
    }
  });
  document.addEventListener('change', function (e) {
    var t = e.target;
    if (t.tagName === 'SELECT' && t.hasAttribute('data-out')) { send('output ' + t.value); t.blur(); return; }
    if (t.tagName === 'SELECT' && t.hasAttribute('data-id')) {
      // Custom as it comes starts from the library progression it was on
      var selId = +t.getAttribute('data-id'), was = Math.round(val(selId));
      if (selId === gid('prog') && +t.value === PROG_CUSTOM && was > 0 && was < PROG_CUSTOM && customUntouched()) progToCustom(was);
      else setParam(selId, +t.value, true);
      t.blur();
    }
  });

  /* knobs drag up and down, faders left and right; double-click resets, the wheel nudges, Shift is fine.
     Mini, by finger, pen or mouse: a drag starts past SLOP px (a tap never nudges), a double-tap resets (a second press
     soon after a tap), and a second finger held anywhere while dragging makes it fine as Shift does: that finger only
     holds (it moves, ends and clicks nothing, and a long press waits for the drag instead). */
  var SLOP = 4, held = {}, nHeld = 0, quietUntil = 0;
  function ctlOf(e) { var t = e.target; return t && t.classList && (t.classList.contains('knob') || t.classList.contains('hfader')) ? t : null; }
  function uiScale() { return Math.min(window.innerHeight / LAYOUT_H, window.innerWidth / MIN_W); }
  if (MINI) {
    // first (capture), so no other handler sees a holding finger: not a tag, not a pin's label, not a new drag
    document.addEventListener('pointerdown', function (e) {
      if (!drag || e.pointerId === drag.pid) return;
      if (!held[e.pointerId]) { held[e.pointerId] = true; nHeld++; }
      if (drag.lp) { clearTimeout(drag.lp.timer); drag.lp = null; } // a fine drag coming, not a long press
      if (drag.tag) clearTimeout(drag.tag.press);
      e.preventDefault(); e.stopImmediatePropagation();
    }, true);
    document.addEventListener('pointermove', function (e) { if (held[e.pointerId] || (drag && e.pointerId !== drag.pid)) e.stopImmediatePropagation(); }, true);
    var holdEnd = function (e) {
      if (held[e.pointerId]) { delete held[e.pointerId]; nHeld--; quietUntil = performance.now() + 400; e.stopImmediatePropagation(); return; }
      if (drag && drag.pid != null && e.pointerId !== drag.pid) e.stopImmediatePropagation();
    };
    document.addEventListener('pointerup', holdEnd, true);
    document.addEventListener('pointercancel', holdEnd, true);
    // a holding finger's tap (while dragging, or just after) is no click
    document.addEventListener('click', function (e) { if (drag || nHeld || performance.now() < quietUntil) { e.stopPropagation(); e.preventDefault(); } }, true);
  }
  document.addEventListener('pointerdown', function (e) {
    var cv = ctlOf(e); if (!cv) return;
    if (e.button === 2) { e.preventDefault(); openPinMenu(+cv.getAttribute('data-id'), e.clientX, e.clientY); return; }
    if (e.button !== 0) return;
    e.preventDefault();
    var id = +cv.getAttribute('data-id'), d = ORDER[id]; if (!d) return;
    if (assign && assignable(id)) { assignDown(e, cv, id); return; }
    var now = performance.now();
    if (MINI) { if (cv._tapAt && now - cv._tapAt < 350) { setParam(id, d.def, true); cv._tapAt = 0; return; } } // the second tap of two
    else {
      if (cv._lastDown && now - cv._lastDown < 350) { setParam(id, d.def, true); cv._lastDown = 0; return; }
      cv._lastDown = now;
    }
    drag = { cv: cv, id: id, pid: e.pointerId, x: e.clientX, y: e.clientY, v: val(id), h: cv.classList.contains('hfader') };
    if (isHost(id)) send('edit ' + id + ' 1');
    if (e.pointerType && e.pointerType !== 'mouse' && pinnable(id)) { // by touch: held still, the value as it was and the pin menu
      var x0 = e.clientX, y0 = e.clientY, v0 = val(id);
      drag.lp = { moved: 0, timer: setTimeout(function () {
        if (!drag || drag.cv !== cv || drag.lp.moved > 8) return;
        if (val(id) !== v0) setParam(id, v0);
        endDrag(); openPinMenu(id, x0, y0);
      }, LONG_MS) };
    }
    try { cv.setPointerCapture(e.pointerId); } catch (err) { /* fine */ }
  });
  /** Assigning: a press on a target routes the source there (a free route, amount AMT_NEW, bipolar) and starts a drag
      of that route's amount; a press on one it already moves, let go without a drag, removes the route. */
  function assignDown(e, cv, id) {
    var r = findRoute(assign.src, id), added = false;
    if (r < 0) {
      r = freeRoute();
      if (r < 0) { toast('All ' + POOL_ROUTES + ' routes are in use: remove a target first'); return; }
      setRoute(r, assign.src, id, AMT_NEW); added = true;
    }
    var aid = rid(r, 'amt');
    drag = { cv: cv, id: aid, pid: e.pointerId, x: e.clientX, y: e.clientY, v: val(aid), h: cv.classList.contains('hfader'), w: cv.clientWidth, assign: { r: r, added: added, moved: 0 } };
    send('edit ' + aid + ' 1');
    try { cv.setPointerCapture(e.pointerId); } catch (err) { /* fine */ }
  }
  document.addEventListener('pointermove', function (e) {
    if (!drag) {
      if (MINI && e.pointerType !== 'mouse') return; // hover looks only with a mouse: a finger would leave them lit
      hover = ctlOf(e);
      var hr = e.target.closest ? e.target.closest('.mtag,.route[data-route]') : null, hn = hr ? +hr.getAttribute('data-route') : null;
      if (hn !== hotRoute) { hotRoute = hn; dirty = true; }
      return;
    }
    if (MINI && !drag.moved) {
      // the dead zone: nothing moves yet; how far it has strayed still counts toward a long press's 8 px and an
      // assigning click's 4 (as the largest offset, not summed, since drag.x / y stay at the press)
      var off = Math.abs(e.clientX - drag.x) + Math.abs(e.clientY - drag.y);
      if (drag.assign) drag.assign.moved = Math.max(drag.assign.moved, off);
      if (drag.tag) drag.tag.moved = Math.max(drag.tag.moved, off);
      if (drag.lp) drag.lp.moved = Math.max(drag.lp.moved, off);
      if (Math.abs(drag.h ? e.clientX - drag.x : e.clientY - drag.y) < SLOP) return;
      drag.moved = true; drag.x = e.clientX; drag.y = e.clientY; return; // from here, without a jump
    }
    var mv = Math.abs(e.clientX - drag.x) + Math.abs(e.clientY - drag.y);
    if (drag.assign) drag.assign.moved += mv;
    if (drag.tag) drag.tag.moved += mv;
    if (drag.lp) drag.lp.moved += mv;
    // a tag: 200 px for a whole amount (the full -1..1 is 400), as the knob's own drag is 200 px for its range
    var d = ORDER[drag.id], range = (d.max - d.min) * (drag.rate || 1), fine = e.shiftKey || nHeld ? 0.25 : 1; // nHeld: Mini's second finger
    var delta = drag.h ? (e.clientX - drag.x) / ((drag.cv.clientWidth || drag.w || 200) * uiScale()) : (drag.y - e.clientY) / (200 * uiScale());
    drag.v = Math.max(d.min, Math.min(d.max, LOGP[d.s] ? drag.v * Math.pow(d.max / d.min, delta * fine) : drag.v + delta * fine * range));
    drag.x = e.clientX; drag.y = e.clientY;
    var v = d.steps > 0 ? Math.round(drag.v) : drag.v;
    if (P[drag.id] !== v) { P[drag.id] = v; if (isHost(drag.id)) send('set ' + drag.id + ' ' + (d.steps > 0 ? v.toFixed(0) : v.toFixed(5))); else ui.vp[drag.id] = v; if (isRoute(drag.id)) rtDirty = true; dirty = true; }
  });
  function endDrag(e) {
    if (!drag) return;
    if (isHost(drag.id)) send('edit ' + drag.id + ' 0'); else saveUi();
    // Mini: a press let go without a drag is a tap (the first of a double-tap); a long press or a cancel is not
    if (MINI && drag.cv) drag.cv._tapAt = drag.moved || drag.assign || drag.tag || !e || e.type === 'pointercancel' ? 0 : performance.now();
    var a = drag.assign, tg = drag.tag; if (drag.lp) clearTimeout(drag.lp.timer); drag = null;
    if (tg) { clearTimeout(tg.press); hotRoute = null; dirty = true; }
    if (a && !a.added && a.moved < 4) clearRoute(a.r); // a click on a routed knob: the route goes
  }
  document.addEventListener('pointerup', endDrag);
  document.addEventListener('pointercancel', endDrag);
  document.addEventListener('wheel', function (e) {
    var cv = ctlOf(e);
    if (!cv) { var row = e.deltaY && e.target.closest ? e.target.closest('#pinbar .pins') : null; if (row) { row.scrollLeft += e.deltaY; e.preventDefault(); } return; } // the bar's row, sideways
    if (!e.deltaY) return;
    e.preventDefault();
    var id = +cv.getAttribute('data-id'), d = ORDER[id]; if (!d) return;
    var step = d.steps > 0 ? 1 : (d.max - d.min) * (e.shiftKey ? 0.005 : 0.02);
    if (LOGP[d.s]) { setParam(id, val(id) * Math.pow(d.max / d.min, (e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? 0.005 : 0.02)), true); return; }
    setParam(id, val(id) + (e.deltaY < 0 ? step : -step), true);
  }, { passive: false });
  document.addEventListener('contextmenu', function (e) { e.preventDefault(); });
  function fwdKey(e, down) {
    if (e.keyCode === 123) return;
    // typing an amount (a target row's box, the tag menu's): the keys are the box's; Enter or Esc leaves it
    var ae = document.activeElement;
    if (ae && ae.tagName === 'INPUT') {
      if (down && (e.keyCode === 13 || e.keyCode === 27)) { if (menu && menu.el.contains(ae)) closeMenu(); else ae.blur(); e.preventDefault(); }
      return;
    }
    if (e.keyCode === 27 && menu) { if (down) closeMenu(); e.preventDefault(); return; }
    if (e.keyCode === 27 && assign) { if (down) { assign = null; structure = true; } e.preventDefault(); return; }
    // the page's own: Ctrl+S save, Ctrl+O open, R record, B the pin bar (on the key going down, not its repeats)
    if (e.ctrlKey && !e.altKey && !e.shiftKey && (e.keyCode === 83 || e.keyCode === 79)) {
      if (down && !e.repeat) { if (e.keyCode === 83) savePatch(); else openPatch(); }
      e.preventDefault(); return;
    }
    if (e.keyCode === 82 && !e.ctrlKey && !e.altKey && !e.metaKey) { if (down && !e.repeat) toggleRec(); e.preventDefault(); return; }
    if (e.keyCode === 66 && !e.ctrlKey && !e.altKey && !e.metaKey) { if (down && !e.repeat) toggleBar(); e.preventDefault(); return; } // B: the pin bar
    if (window.AMMO_DEV && (e.keyCode === 116 || e.ctrlKey)) return; // in a browser: reload and devtools stay the browser's
    if (e.keyCode === 113) { if (down) elStats.style.display = elStats.style.display === 'block' ? 'none' : 'block'; e.preventDefault(); return; }
    if (document.activeElement && document.activeElement.tagName === 'SELECT') return;
    send('key ' + (down ? 1 : 0) + ' ' + e.keyCode + ' ' + (e.repeat ? 1 : 0));
    e.preventDefault();
  }
  window.addEventListener('keydown', function (e) { fwdKey(e, true); });
  window.addEventListener('keyup', function (e) { fwdKey(e, false); });

  /* ------------------------------------------------------- sub-visuals */
  function fit2d(cv) {
    var w = cv.clientWidth, h = cv.clientHeight; if (!w || !h) return null;
    var dpr = Math.min(2, (window.devicePixelRatio || 1) * uiScale());
    var W = Math.round(w * dpr), H = Math.round(h * dpr);
    if (cv.width !== W || cv.height !== H) { cv.width = W; cv.height = H; }
    var ctx = cv.getContext('2d'); ctx.setTransform(dpr, 0, 0, dpr, 0, 0); ctx.clearRect(0, 0, w, h);
    return { ctx: ctx, w: w, h: h };
  }

  /* the circle of fifths: the key's notes, the chord's, and the notes sounding now */
  function drawWheel(cv) {
    var g = fit2d(cv); if (!g) return;
    var ctx = g.ctx, cx = g.w / 2, cy = g.h / 2, R = Math.min(g.w, g.h) / 2 - 36, i, pc;
    var sounding = {};
    for (var s = 0; s < SLOTS; s++) { var c = CH[s]; if (c.active && sval(s, 'mute') < 0.5) sounding[c.pc] = Math.max(sounding[c.pc] || 0, Math.min(1, c.amp)); }
    // chord tones joined
    var tones = [];
    for (pc = 0; pc < 12; pc++) if ((F.cm >> pc) & 1) tones.push(pc);
    if (tones.length > 1) {
      ctx.beginPath();
      for (i = 0; i < tones.length; i++) { var k = (tones[i] * 7) % 12, a = -Math.PI / 2 + k * Math.PI / 6; if (i) ctx.lineTo(cx + Math.cos(a) * R, cy + Math.sin(a) * R); else ctx.moveTo(cx + Math.cos(a) * R, cy + Math.sin(a) * R); }
      ctx.closePath(); ctx.strokeStyle = 'rgba(239,230,216,0.18)'; ctx.lineWidth = 1; ctx.stroke();
      ctx.fillStyle = Knobs.hsla(Knobs.hueOf(F.cr), 80, 50, 0.07); ctx.fill();
    }
    ctx.beginPath(); ctx.arc(cx, cy, R, 0, Math.PI * 2); ctx.strokeStyle = 'rgba(255,255,255,0.08)'; ctx.lineWidth = 1; ctx.stroke();
    for (i = 0; i < 12; i++) {
      pc = (i * 7) % 12;
      var ang = -Math.PI / 2 + i * Math.PI / 6, x = cx + Math.cos(ang) * R, y = cy + Math.sin(ang) * R;
      var inKey = (F.km >> pc) & 1, inChord = (F.cm >> pc) & 1, isRoot = pc === F.cr, amp = sounding[pc] || 0, hue = Knobs.hueOf(pc);
      if (inKey) Knobs.glowDot(ctx, x, y, isRoot ? 6 : 4.5, hue, 85, inChord ? 0.8 : 0.3);
      else { ctx.beginPath(); ctx.arc(x, y, 2.5, 0, Math.PI * 2); ctx.fillStyle = 'rgba(255,255,255,0.1)'; ctx.fill(); }
      if (amp > 0.02) Knobs.glowDot(ctx, x, y, 7 + 9 * amp, hue, 92, amp);
      var lx = cx + Math.cos(ang) * (R + 15), ly = cy + Math.sin(ang) * (R + 15);
      Knobs.text(ctx, Knobs.NOTES[pc], lx, ly, (isRoot ? '600 ' : '500 ') + '12px ' + Knobs.SANS, inKey ? (isRoot ? '#efe6d8' : 'rgba(236,236,240,0.8)') : 'rgba(236,236,240,0.3)', 'center');
    }
    ctx.save(); ctx.globalCompositeOperation = 'lighter';
    Knobs.text(ctx, Knobs.NOTES[F.cr], cx, cy - 10, '600 34px ' + Knobs.SANS, Knobs.hsla(Knobs.hueOf(F.cr), 85, 68, 0.95), 'center');
    ctx.restore();
    Knobs.text(ctx, F.pl > 0 ? romanOf(F.cd % 7) : (QUAL[F.cq] || 'chord'), cx, cy + 16, '500 12px ' + Knobs.MONO, 'rgba(220,220,228,0.6)', 'center');
  }

  /* the progression playing as degrees: a library one, Custom's steps up to its length, none while off */
  function progDegs() {
    var p = Math.round(gval('prog'));
    if (p !== PROG_CUSTOM) return PROGS[p] || [];
    for (var out = [], i = 0, n = Math.round(gval('prog_len')); i < n; i++) out.push(Math.round(gval('prog_s' + (i + 1))));
    return out;
  }
  function progStep() { return Math.max(0, Math.min(progSel, Math.round(gval('prog_len')) - 1)); }
  /** Degree `d` as a numeral for the key: upper case major, lower case minor, ° diminished, + augmented. */
  function romanOf(d) {
    var sc = Math.round(gval('scale')), iv = SCALE_IV[sc < 10 ? sc : ([11, 15, 17].indexOf(sc) >= 0 ? 1 : 0)];
    var at = function (q) { return iv[q % iv.length] + 12 * Math.floor(q / iv.length); };
    var t3 = at(d + 2) - at(d), t5 = at(d + 4) - at(d), r = ROMAN[d % 7];
    return t3 === 3 ? r.toLowerCase() + (t5 === 6 ? '°' : '') : r + (t3 === 4 && t5 === 8 ? '+' : '');
  }
  /** Chips for a degree parameter (the manual chord, a Custom step), named for the key. */
  function degreeChips(id, label) {
    var v = Math.round(val(id)), out = '<div class="chips" data-chips="' + id + '"><span class="lab">' + esc(label) + '</span>';
    for (var i = 0; i < 7; i++) out += '<button class="chip" data-id="' + id + '" data-val="' + i + '" aria-pressed="' + (v === i) + '">' + esc(romanOf(i)) + '</button>';
    return out + '</div>';
  }
  /** Custom: how many steps, and the chord of the one selected on the bar. */
  function progEditHtml() {
    var lid = gid('prog_len'), n = Math.round(gval('prog_len')), st = progStep(), i;
    var out = '<div class="chips" data-chips="' + lid + '"><span class="lab">STEPS</span>';
    for (i = 1; i <= PROG_STEPS; i++) out += '<button class="chip" data-id="' + lid + '" data-val="' + i + '" aria-pressed="' + (n === i) + '">' + i + '</button>';
    return out + '</div>' + degreeChips(gid('prog_s' + (st + 1)), 'STEP ' + (st + 1)) +
      '<div class="hint flush">' + (MINI ? 'Tap' : 'Click') + ' a chord on the bar to choose it, then its degree here.</div>';
  }
  /** Edit as Custom: a library progression copied into Custom's steps, and Custom on. */
  function progToCustom(from) {
    var degs = PROGS[from] || []; if (!degs.length) return;
    setParam(gid('prog_len'), degs.length, true);
    for (var i = 0; i < degs.length; i++) setParam(gid('prog_s' + (i + 1)), degs[i], true);
    progSel = 0; setParam(gid('prog'), PROG_CUSTOM, true);
  }
  /** Custom as it comes: every one of its parameters still at its default. */
  function customUntouched() {
    for (var i = 0; i <= PROG_STEPS; i++) { var id = gid(i ? 'prog_s' + i : 'prog_len'); if (Math.round(val(id)) !== Math.round(ORDER[id].def)) return false; }
    return true;
  }

  function drawProg(cv) {
    var g = fit2d(cv); if (!g) return;
    var ctx = g.ctx, w = g.w, h = g.h, sel = Math.round(gval('prog')) === PROG_CUSTOM ? progStep() : -1;
    var prog = progDegs(), degs = prog.length ? prog : [Math.round(gval('chord'))], n = degs.length, bw = w / n, i, j;
    var scale = Math.round(gval('scale')), root = Math.round(gval('root')), iv = SCALE_IV[scale < 10 ? scale : ([11, 15, 17].indexOf(scale) >= 0 ? 1 : 0)];
    var changeBeats = [2, 4, 8, 16][Math.round(gval('change'))], cur = prog.length ? F.pp : 0;
    for (i = 0; i < n; i++) {
      var d = degs[i], x = i * bw + 2, isCur = i === cur;
      var tones = [0, 2, 4].map(function (k) { var q = d + k; return (root + iv[q % iv.length] + 12 * Math.floor(q / iv.length)) % 12; });
      var gr = ctx.createLinearGradient(x, 0, x + bw, 0);
      for (j = 0; j < 3; j++) gr.addColorStop(j / 2, Knobs.hsla(Knobs.hueOf(tones[j]), 85, 50, isCur ? 0.42 : 0.1));
      ctx.beginPath(); if (ctx.roundRect) ctx.roundRect(x, 1, bw - 4, h - 2, 7); else ctx.rect(x, 1, bw - 4, h - 2); ctx.fillStyle = gr; ctx.fill();
      if (i === sel) { ctx.strokeStyle = 'rgba(239,230,216,0.85)'; ctx.lineWidth = 1.5; ctx.stroke(); }
      var third = (12 + (tones[1] - tones[0])) % 12, name = Knobs.NOTES[tones[0]] + (third === 3 ? 'm' : '');
      Knobs.text(ctx, name, x + 10, h / 2, '600 ' + Math.round(h * 0.4) + 'px ' + Knobs.SANS, isCur ? Knobs.hsla(Knobs.hueOf(tones[0]), 90, 74, 1) : 'rgba(220,220,228,0.55)');
      Knobs.text(ctx, romanOf(d), x + bw - 12, h / 2, '500 11px ' + Knobs.MONO, 'rgba(220,220,228,0.45)', 'right');
    }
    if (prog.length) {
      var px = (((F.beat % (changeBeats * n)) + changeBeats * n) % (changeBeats * n)) / (changeBeats * n) * w;
      ctx.fillStyle = 'rgba(239,230,216,0.9)'; ctx.fillRect(px - 0.75, 0, 1.5, h);
    }
  }

  /* the arp's step lane: LENGTH steps, the Euclidean hits (each filled to its gate), the step playing now */
  function drawSteps(cv, slot) {
    var g = fit2d(cv); if (!g) return;
    var ctx = g.ctx, w = g.w, h = g.h, c = CH[slot];
    var len = Math.max(1, Math.round(sval(slot, 'arp_len'))), hits = Math.floor(sval(slot, 'arp_density') * len + 0.5), hue = hueOfSlot(slot), gate = sval(slot, 'arp_gate');
    var rot = Math.round(sval(slot, 'rhy_rotate')) % len; // every generator's ROTATE
    // the notes the engine foresees (stepNote: -1 a rest, -2 not known yet); none for a drum's fixed pitch
    var sn = c.active && c.sn && c.sn.length === len && !(genOf(slot) === GEN_RHYTHM && Math.round(sval(slot, 'rhy_pitch')) === 0) ? c.sn : null;
    pane(ctx, w, h);
    var x0 = 10, x1 = w - 10, top = 24, bot = h - 10, pad = 3, sw = (x1 - x0 - pad * (len - 1)) / len, cur = c.active ? c.step % len : -1;
    var nfs = Math.max(8, Math.min(13, Math.floor(sw * 0.42)));
    for (var i = 0; i < len; i++) {
      var on = (((i - rot + len) % len) * hits) % len < hits, x = x0 + i * (sw + pad), now = i === cur;
      ctx.beginPath(); if (ctx.roundRect) ctx.roundRect(x, top, sw, bot - top, 4); else ctx.rect(x, top, sw, bot - top);
      ctx.fillStyle = on ? Knobs.hsla(hue, 60, 45, now ? 0.3 : 0.14) : 'rgba(255,255,255,0.04)'; ctx.fill();
      if (now) { ctx.strokeStyle = on ? Knobs.hsla(hue, 90, 70, 0.9) : 'rgba(239,230,216,0.5)'; ctx.lineWidth = 1; ctx.stroke(); }
      if (!on) continue;
      var gw = Math.max(3, sw * Math.max(0.05, gate));
      ctx.beginPath(); if (ctx.roundRect) ctx.roundRect(x, top, gw, bot - top, 4); else ctx.rect(x, top, gw, bot - top);
      ctx.fillStyle = Knobs.hsla(hue, 85, now ? 66 : 55, now ? 0.9 : 0.42); ctx.fill();
      if (now) { ctx.save(); ctx.globalCompositeOperation = 'lighter'; ctx.shadowBlur = 14; ctx.shadowColor = Knobs.hsla(hue, 90, 60, 0.9); ctx.fill(); ctx.restore(); }
      if (sn && sn[i] !== -1) {
        var nn = sn[i] >= 0 ? Knobs.NOTES[sn[i] % 12] : '?';
        Knobs.text(ctx, nn, x + sw / 2, bot - nfs * 0.75, '600 ' + nfs + 'px ' + Knobs.SANS,
          sn[i] < 0 ? 'rgba(236,236,240,0.35)' : now ? '#141414' : Knobs.hsla(sn[i] >= 0 ? Knobs.hueOf(sn[i] % 12) : hue, 80, 80, 0.95), 'center');
      }
    }
    caption(ctx, 'STEPS · ' + (cur >= 0 ? (cur + 1) + '/' : '') + len + ' · ' + hits + ' hits');
  }

  /* ---- the section views: each a dark pane with a caption top left */
  function pane(ctx, w, h) {
    ctx.fillStyle = 'rgba(0,0,0,0.25)'; ctx.beginPath(); if (ctx.roundRect) ctx.roundRect(0, 0, w, h, 10); else ctx.rect(0, 0, w, h); ctx.fill();
  }
  function caption(ctx, s) { Knobs.text(ctx, s, 10, 12, '500 10px ' + Knobs.MONO, 'rgba(220,220,228,0.5)'); }
  function ducked(s) { var src = Math.round(sval(s, 'duck_src')) - 1; return src >= 0 && src !== s && sval(s, 'duck_amt') > 0; }
  function hueOfSlot(slot) { return CH[slot].hue == null ? 90 : CH[slot].hue; }
  function noteName(n) { return Knobs.NOTES[((Math.round(n) % 12) + 12) % 12] + Math.floor(Math.round(n) / 12 - 1); }

  /* oscillator: two cycles of its live wave, filled from the centre line, as bright as it is loud */
  function drawWave(cv, slot) {
    var g = fit2d(cv); if (!g) return;
    var ctx = g.ctx, w = g.w, h = g.h, c = CH[slot], hue = hueOfSlot(slot), sat = 92, amp = c.active ? Math.min(1, c.amp) : 0, k = 0.45 + 0.55 * amp, mid = h / 2 + 6, i;
    pane(ctx, w, h);
    ctx.strokeStyle = 'rgba(255,255,255,0.07)'; ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(10, mid); ctx.lineTo(w - 10, mid); ctx.stroke();
    var p = new Path2D(), N = 200, A = (h - 30) * 0.46;
    for (i = 0; i <= N; i++) {
      var u = i / N, ph = (u * 2 + T * 0.2) % 1, idx = Math.floor(ph * 128) % 128, y = mid - (c.active ? c.wave[idx] : 0) * A;
      if (i) p.lineTo(10 + (w - 20) * u, y); else p.moveTo(10, y);
    }
    var fill = new Path2D(p); fill.lineTo(w - 10, mid); fill.lineTo(10, mid); fill.closePath();
    var gr = ctx.createLinearGradient(0, mid - A, 0, mid + A);
    gr.addColorStop(0, Knobs.hsla(hue, sat, 60, 0.2 * k)); gr.addColorStop(0.5, Knobs.hsla(hue, sat, 50, 0.02)); gr.addColorStop(1, Knobs.hsla(hue, sat, 60, 0.2 * k));
    ctx.fillStyle = gr; ctx.fill(fill);
    Knobs.neon(ctx, p, hue, sat, Math.max(1.4, h / 70), c.active ? k : 0.2);
    caption(ctx, c.active ? 'WAVE · ' + noteName(c.note) : 'WAVE');
  }

  /* an LFO: its shape faint behind (one cycle from its phase), and what it has done over the last four seconds as a
     trace running in from the right, the value now a dot at the end. */
  function lfoAt(shape, pos) {
    var f = pos - Math.floor(pos), k = Math.floor(pos), rnd = function (n) { var x = Math.sin(n * 127.1 + 311.7) * 43758.5453; return (x - Math.floor(x)) * 2 - 1; };
    switch (shape) {
      case 1: return f < 0.25 ? 4 * f : f < 0.75 ? 2 - 4 * f : 4 * f - 4;
      case 2: return 2 * f - 1;
      case 3: return 1 - 2 * f;
      case 4: return f < 0.5 ? 1 : -1;
      case 5: return rnd(k);
      case 6: var a = rnd(k), b = rnd(k + 1), sm = f * f * (3 - 2 * f); return a + (b - a) * sm;
      default: return Math.sin(f * 2 * Math.PI);
    }
  }
  function drawLfo(cv) {
    var g = fit2d(cv); if (!g) return;
    var l = +cv.getAttribute('data-lfo'), src = SRC_LFO1 + l, ctx = g.ctx, w = g.w, h = g.h, hue = srcHue(src), sat = srcTone(src).s, i;
    var shape = Math.round(val(lid(l, 'shape'))), tempo = Math.round(val(lid(l, 'sync'))) === 1, ph = val(lid(l, 'phase'));
    var routed = routedText(src) !== '', hist = LH.h[l], pos = LH.pos, N = hist.length, live = F.lf[l] != null, now = live ? F.lf[l] : 0;
    pane(ctx, w, h);
    var x0 = 12, x1 = w - 12, top = 24, bot = h - 10, mid0 = (top + bot) / 2, A = (bot - top) / 2 - 2;
    ctx.fillStyle = 'rgba(255,255,255,0.06)'; ctx.fillRect(x0, mid0, x1 - x0, 1);
    var q = new Path2D();
    for (i = 0; i <= 120; i++) { var u = i / 120, y = mid0 - lfoAt(shape, u + ph) * A; if (i) q.lineTo(x0 + (x1 - x0) * u, y); else q.moveTo(x0, y); }
    ctx.strokeStyle = Knobs.hsla(hue, sat, 60, 0.18); ctx.lineWidth = 1.2; ctx.stroke(q);
    if (live) {
      var tr = new Path2D();
      for (i = 0; i < N; i++) { var y2 = mid0 - hist[(pos + i) % N] * A, x = x0 + (x1 - x0) * i / (N - 1); if (i) tr.lineTo(x, y2); else tr.moveTo(x, y2); }
      Knobs.neon(ctx, tr, hue, sat, Math.max(1.3, h / 80), routed ? 0.85 : 0.3);
      Knobs.glowDot(ctx, x1, mid0 - now * A, 3, hue, sat, routed ? 1 : 0.4);
    }
    caption(ctx, 'LFO ' + (l + 1) + ' · ' + nameOf(lid(l, 'shape')).toUpperCase() + ' · ' + (tempo ? nameOf(lid(l, 'div')) : Knobs.fmt('lhz', val(lid(l, 'rate')))) +
      (routed ? '' : ' · no targets'));
  }

  /* filter: its response, filled under, with the envelope level along the bottom */
  function drawFilter(cv, slot) {
    var g = fit2d(cv); if (!g) return;
    var ctx = g.ctx, w = g.w, h = g.h, c = CH[slot], hue = hueOfSlot(slot), sat = 92, amp = Math.min(1, c.amp), k = 0.5 + 0.5 * amp, mid = h / 2 + 4, i;
    pane(ctx, w, h);
    var cut = sval(slot, 'cutoff'), rs = sval(slot, 'reso'), mode = sval(slot, 'filter_type'), fc = 20 * Math.pow(1000, cut), q = 0.6 + rs * 6;
    var fp = new Path2D(), x0 = 10, x1 = w - 10;
    for (i = 0; i <= 160; i++) {
      var uu = i / 160, fq = 20 * Math.pow(1000, uu), xx = fq / fc, re = 1 - xx * xx, im = xx / q, den = Math.sqrt(re * re + im * im);
      var m = mode < 0.5 ? (1 / den) * (1 - mode * 2) + (xx / q / den) * (mode * 2) : (xx / q / den) * (2 - mode * 2) + (xx * xx / den) * (mode * 2 - 1);
      var db = 20 * Math.log(Math.max(m, 1e-4)) / Math.LN10, yy = mid + h * 0.08 - Math.max(-30, Math.min(15, db)) / 30 * h * 0.4;
      if (i) fp.lineTo(x0 + (x1 - x0) * uu, yy); else fp.moveTo(x0, yy);
    }
    var fq2 = new Path2D(fp); fq2.lineTo(x1, h - 8); fq2.lineTo(x0, h - 8); fq2.closePath();
    var gr = ctx.createLinearGradient(0, 0, 0, h); gr.addColorStop(0, Knobs.hsla(hue, sat, 55, 0.24 * k)); gr.addColorStop(1, Knobs.hsla(hue, sat, 50, 0));
    ctx.fillStyle = gr; ctx.fill(fq2);
    ctx.strokeStyle = 'rgba(255,255,255,0.12)'; ctx.setLineDash([2, 4]); ctx.beginPath(); ctx.moveTo(x0 + (x1 - x0) * cut, 20); ctx.lineTo(x0 + (x1 - x0) * cut, h - 10); ctx.stroke(); ctx.setLineDash([]);
    Knobs.neon(ctx, fp, hue, sat, Math.max(1.4, h / 70), k);
    ctx.fillStyle = 'rgba(255,255,255,0.06)'; ctx.fillRect(x0, h - 6, x1 - x0, 2);
    ctx.fillStyle = Knobs.hsla(hue, sat, 65, 0.9); ctx.fillRect(x0, h - 6, (x1 - x0) * amp, 2);
    caption(ctx, 'FILTER · ' + Knobs.fmt('hz', cut, { min: 0, max: 1 }) + ' · ' + Knobs.fmt('type', mode, { min: 0, max: 1 }));
  }

  /* amp: the envelope's shape (segments on a square-root time scale so short and long both read), the level
     it is at now as a line across it, and the pitch envelope's drop, faint, when there is one */
  function drawEnv(cv, slot) {
    var g = fit2d(cv); if (!g) return;
    var ctx = g.ctx, w = g.w, h = g.h, c = CH[slot], hue = hueOfSlot(slot), sat = 92, amp = c.active ? Math.min(1, c.amp) : 0;
    pane(ctx, w, h);
    var a = 0.001 * Math.pow(2, 12 * sval(slot, 'amp_a')), d = 0.005 * Math.pow(2, 9.64 * sval(slot, 'amp_d')), r = 0.005 * Math.pow(2, 9.64 * sval(slot, 'amp_r')), sus = sval(slot, 'amp_s');
    var wa = Math.sqrt(a), wd = Math.sqrt(d), wr = Math.sqrt(r), ws = (wa + wd + wr) * 0.35 + 0.05, tot = wa + wd + ws + wr;
    var x0 = 12, x1 = w - 12, top = 24, bot = h - 10, X = function (t) { return x0 + (x1 - x0) * t / tot; }, Y = function (v) { return bot - (bot - top) * v; };
    var p = new Path2D(); p.moveTo(X(0), Y(0)); p.lineTo(X(wa), Y(1)); p.lineTo(X(wa + wd), Y(sus)); p.lineTo(X(wa + wd + ws), Y(sus)); p.lineTo(X(tot), Y(0));
    var fill = new Path2D(p); fill.closePath();
    var gr = ctx.createLinearGradient(0, top, 0, bot); gr.addColorStop(0, Knobs.hsla(hue, sat, 58, 0.22)); gr.addColorStop(1, Knobs.hsla(hue, sat, 50, 0.02));
    ctx.fillStyle = gr; ctx.fill(fill);
    var pa = sval(slot, 'pitch_amt');
    if (Math.abs(pa) > 0.01) {
      var pd = Math.sqrt(0.005 * Math.pow(2, 7.64 * sval(slot, 'pitch_d'))), pp = new Path2D(), mid = (top + bot) / 2;
      for (var i = 0; i <= 60; i++) { var t = i / 60 * tot, y = mid - pa * (bot - top) * 0.45 * Math.exp(-t / Math.max(0.02, pd)); if (i) pp.lineTo(X(t), y); else pp.moveTo(X(t), y); }
      ctx.save(); ctx.setLineDash([3, 4]); ctx.strokeStyle = 'rgba(239,230,216,0.35)'; ctx.lineWidth = 1; ctx.stroke(pp); ctx.restore();
    }
    if (amp > 0.01) { ctx.fillStyle = Knobs.hsla(hue, sat, 70, 0.5 * amp + 0.2); ctx.fillRect(x0, Y(amp) - 0.5, x1 - x0, 1); }
    Knobs.neon(ctx, p, hue, sat, Math.max(1.4, h / 70), 0.55 + 0.45 * amp);
    caption(ctx, 'ENVELOPE' + (Math.abs(pa) > 0.01 ? ' · PITCH ' + Knobs.fmt('semi', pa) : ''));
  }

  /* drum: the shape of one hit (the envelope, the noise layer shaded, the pitch sweep dashed), from the knobs,
     and a playhead that runs along it after every hit */
  var DRUM_NAMES = ['KICK', 'SNARE', 'HAT', 'OPEN HAT', 'CLAP', 'TOM', 'RIM', 'NOISE'];
  function drumShape(model, d, snap) { // the engine's decay times (DrumVoice.hpp), seconds: [body, noise, pitch sweep]
    switch (model) {
      case 0: return [0.12 + d * d * 1.4, 0.003, 0.03 * (snap > 0.01 ? 1 : 0)];
      case 1: return [0.06 + d * 0.15, 0.08 + d * d * 0.5, 0.02];
      case 2: return [0, 0.012 + d * 0.06, 0];
      case 3: return [0, 0.15 + d * d * 1.2, 0];
      case 4: return [0.08 + d * 0.4, 0.006 + snap * 0.004, 0];
      case 5: return [0.12 + d * d * 0.8, 0.002, 0.06 * (snap > 0.01 ? 1 : 0)];
      case 6: return [0.008 * (1 + 3 * d), 0.004, 0];
      default: return [0, 0.03 + d * d * 1.5, 0];
    }
  }
  function drawDrum(cv, slot) {
    var g = fit2d(cv); if (!g) return;
    var ctx = g.ctx, w = g.w, h = g.h, c = CH[slot], hue = hueOfSlot(slot), sat = 92, i;
    pane(ctx, w, h);
    var model = Math.round(sval(slot, 'drum_model')), sh = drumShape(model, sval(slot, 'drum_decay'), sval(slot, 'drum_snap'));
    var main = Math.max(sh[0], sh[1]), span = Math.min(2, Math.max(0.25, 4.5 * main)), x0 = 10, x1 = w - 10, top = 24, bot = h - 10, N = 160;
    function X(t) { return x0 + (x1 - x0) * t / span; }
    function Y(v) { return bot - (bot - top) * v; }
    function amp(t) {
      if (model === 4) { // clap: three bursts 10 ms apart, then the tail
        var b = t < 0.03 ? Math.exp(-(t % 0.01) / sh[1]) : 0, tail = t >= 0.03 ? 0.7 * Math.exp(-(t - 0.03) / sh[0]) : 0;
        return Math.max(b, tail);
      }
      return Math.max(sh[0] ? Math.exp(-t / sh[0]) : 0, sh[1] ? Math.exp(-t / sh[1]) * (sh[0] ? 0.8 : 1) : 0);
    }
    var p = new Path2D(), nz = new Path2D();
    for (i = 0; i <= N; i++) { var t = span * i / N, y = Y(amp(t)); if (i) p.lineTo(X(t), y); else p.moveTo(X(t), y); }
    if (sh[1] > 0.004 && sh[0] > 0) { // the noise layer under the body
      nz.moveTo(X(0), bot);
      for (i = 0; i <= N; i++) { var tn = span * i / N; nz.lineTo(X(tn), Y(0.8 * Math.exp(-tn / sh[1]))); }
      nz.lineTo(X(span), bot); nz.closePath();
      ctx.fillStyle = 'rgba(239,230,216,0.08)'; ctx.fill(nz);
    }
    var fill = new Path2D(p); fill.lineTo(X(span), bot); fill.lineTo(X(0), bot); fill.closePath();
    var gr = ctx.createLinearGradient(0, top, 0, bot); gr.addColorStop(0, Knobs.hsla(hue, sat, 58, 0.24)); gr.addColorStop(1, Knobs.hsla(hue, sat, 50, 0.02));
    ctx.fillStyle = gr; ctx.fill(fill);
    if (sh[2] > 0) { // the pitch sweep, falling from its start
      var pp = new Path2D(), sw = model === 0 ? 5 * sval(slot, 'drum_snap') : 0.8 * sval(slot, 'drum_snap');
      for (i = 0; i <= N; i++) { var tp = span * i / N, v = (1 + sw * Math.exp(-tp / sh[2])) / (1 + sw); if (i) pp.lineTo(X(tp), Y(v)); else pp.moveTo(X(tp), Y(v)); }
      ctx.save(); ctx.setLineDash([3, 4]); ctx.strokeStyle = 'rgba(239,230,216,0.4)'; ctx.lineWidth = 1; ctx.stroke(pp); ctx.restore();
    }
    Knobs.neon(ctx, p, hue, sat, Math.max(1.4, h / 70), 0.5 + 0.5 * Math.min(1, c.amp * 1.5));
    var last = c.notes.length ? c.notes[c.notes.length - 1].t : -1e9, since = performance.now() / 1000 - last;
    if (since >= 0 && since < span) {
      var px = X(since), a = amp(since);
      ctx.fillStyle = 'rgba(239,230,216,0.35)'; ctx.fillRect(px - 0.5, top, 1, bot - top);
      Knobs.glowDot(ctx, px, Y(a), 4, hue, sat, 0.6 + 0.4 * a);
    }
    caption(ctx, (DRUM_NAMES[model] || 'DRUM') + ' · ' + Math.round(span * 1000) + ' ms');
  }

  /* ---- the effects' views (the curves are the engine's, Fx.hpp) */
  function offNote(ctx, w, h, on) { if (!on) Knobs.text(ctx, 'OFF', w - 12, 12, '500 10px ' + Knobs.MONO, 'rgba(220,220,228,0.4)', 'right'); }

  /* drive: the transfer curve (input across, output up, the straight line faint behind), the level playing now as
     a dot riding it, and the tone's tilt as a short slope along the bottom */
  function drawDriveView(cv, slot) {
    var g = fit2d(cv); if (!g) return;
    var ctx = g.ctx, w = g.w, h = g.h, c = CH[slot], hue = hueOfSlot(slot), sat = 92, i;
    pane(ctx, w, h);
    var amt = sval(slot, 'fx_drive'), tone = sval(slot, 'fx_tone'), on = amt > 0, gn = 1 + 31 * amt * amt, ga = gn * 0.3, mk = Math.sqrt(1 + 0.5 * ga * ga) / gn;
    var cx = w / 2, cy = h / 2 + 6, sx = (w - 40) / 2, sy = (h - 40) / 2, span = 0.6;
    function T(x) { return on ? Math.tanh(gn * x) * mk : x; }
    ctx.strokeStyle = 'rgba(255,255,255,0.07)'; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(cx - sx, cy); ctx.lineTo(cx + sx, cy); ctx.moveTo(cx, cy - sy); ctx.lineTo(cx, cy + sy); ctx.stroke();
    ctx.setLineDash([3, 4]); ctx.beginPath(); ctx.moveTo(cx - sx, cy + sy); ctx.lineTo(cx + sx, cy - sy); ctx.stroke(); ctx.setLineDash([]);
    var p = new Path2D();
    for (i = 0; i <= 120; i++) { var x = -span + 2 * span * i / 120, y = Math.max(-span, Math.min(span, T(x))); if (i) p.lineTo(cx + sx * x / span, cy - sy * y / span); else p.moveTo(cx + sx * x / span, cy - sy * y / span); }
    Knobs.neon(ctx, p, hue, sat, Math.max(1.4, h / 70), on ? 0.5 + 0.5 * Math.min(1, c.amp) : 0.2);
    if (on && c.active) { // the level now: its peak riding the curve
      var lx = Math.min(span, (c.peak || c.amp * 0.3) * 1.4), ly = Math.max(-span, Math.min(span, T(lx)));
      Knobs.glowDot(ctx, cx + sx * lx / span, cy - sy * ly / span, 4, hue, sat, 0.5 + 0.5 * Math.min(1, c.amp));
    }
    var tx = w - 74, ty = h - 14; // the tilt
    ctx.strokeStyle = on ? Knobs.hsla(hue, 70, 70, 0.7) : 'rgba(255,255,255,0.15)'; ctx.beginPath(); ctx.moveTo(tx, ty + tone * 6); ctx.lineTo(tx + 60, ty - tone * 6); ctx.stroke();
    caption(ctx, 'DRIVE' + (on ? ' · +' + Math.round(20 * Math.log(gn) / Math.LN10) + ' dB · tone ' + Knobs.fmt('bip', tone) : ''));
    offNote(ctx, w, h, on);
  }

  /* chorus: each line's delay sweeping by in time, at its rest (faint): Chorus two lines, left solid and right
     dashed in opposite phase; Ensemble three a side, 120 degrees apart, at staggered rests, with the shimmer on top;
     Vibrato one, both sides together. The scale is the same for all, so depth reads as depth; brighter with more mix.
     The curves are engine/Fx.hpp's (ChorusHz, VibratoHz, ChorusSweep). */
  var CHO_DEV = 0.011486, CHO_SHIMMER = 6.9, CHO_SH_DEPTH = 0.125, ENS_REST = [[-5, -1, 3], [-3, 1, 5]]; // ms from 11: kEnsembleBase, left and right
  function chorusSweep(mode, depth, hz) {
    var w = 2 * Math.PI * hz;
    return mode === 1 ? depth * Math.min(0.003, CHO_DEV / (w * (1 + CHO_SHIMMER * CHO_SH_DEPTH))) : mode === 2 ? depth * 0.004 : depth * Math.min(0.004, CHO_DEV / w);
  }
  function drawChorusView(cv, slot) {
    var g = fit2d(cv); if (!g) return;
    var ctx = g.ctx, w = g.w, h = g.h, hue = hueOfSlot(slot), sat = 92, i, j;
    pane(ctx, w, h);
    var on = chorusOn(slot), mode = Math.round(sval(slot, 'cho_mode')), r = sval(slot, 'cho_rate'), dep = sval(slot, 'cho_depth'), mix = mode === 2 ? 1 : sval(slot, 'cho_mix');
    var hz = mode === 2 ? 0.05 * Math.pow(2, 7 * r) : 0.08 * Math.pow(40, r), sw = chorusSweep(mode, dep, hz) * 1000; // ms
    var x0 = 12, x1 = w - 12, top = 24, bot = h - 10, mid = (top + bot) / 2, secs = Math.max(2 / hz, 0.6);
    var msPx = ((bot - top) / 2 - 4) / 8.5; // +-8.5 ms: Ensemble's outer rests and their sweep
    var lines = mode === 1 ? [0, 1, 2] : [0], sides = mode === 2 ? [1] : [1, -1], k = on ? 0.35 + 0.65 * mix : 0.18;
    ctx.fillStyle = 'rgba(255,255,255,0.06)';
    for (j = 0; j < lines.length; j++) [0, 1].forEach(function (side) { ctx.fillRect(x0, mid - (mode === 1 ? ENS_REST[side][j] : 0) * msPx - 0.5, x1 - x0, 1); });
    sides.forEach(function (sg) {
      for (j = 0; j < lines.length; j++) {
        var off = j / 3, rest = mode === 1 ? ENS_REST[sg > 0 ? 0 : 1][j] : 0, p = new Path2D();
        for (i = 0; i <= 160; i++) {
          var u = i / 160, t = T - secs * (1 - u), ph = 2 * Math.PI * (hz * t + off);
          var s = sw * Math.sin(ph) + (mode === 1 ? CHO_SH_DEPTH * sw * Math.sin(2 * Math.PI * (CHO_SHIMMER * hz * t + off)) : 0);
          var x = x0 + (x1 - x0) * u, y = mid - (rest + sg * s) * msPx;
          if (i) p.lineTo(x, y); else p.moveTo(x, y);
        }
        if (sg > 0) Knobs.neon(ctx, p, hue + 12 * j, sat, Math.max(1.2, h / (mode === 1 ? 90 : 70)), k);
        else { ctx.save(); ctx.setLineDash([4, 4]); Knobs.neon(ctx, p, hue + 30 + 12 * j, sat, Math.max(1.1, h / 85), k * 0.8); ctx.restore(); }
      }
    });
    var cents = 1200 * Math.log(1 + 2 * Math.PI * hz * sw / 1000 * (mode === 1 ? 1 + CHO_SHIMMER * CHO_SH_DEPTH : 1)) / Math.LN2;
    var hzs = (hz < 1 ? hz.toFixed(2) : hz.toFixed(1)) + ' Hz';
    caption(ctx, ['CHORUS · 8 ±', 'ENSEMBLE · 3 LINES A SIDE · ±', 'VIBRATO · ±'][mode] + sw.toFixed(1) + ' ms · ' + hzs + ' · ±' + Math.round(cents) + ' ct');
    offNote(ctx, w, h, on);
  }

  /* delay: the echo train on a beat grid: the note, then each echo as tall as it is loud (left above the line,
     right below; ping-pong alternates), darker as the damping takes its top; a playhead runs out from each hit */
  var DLY_BEATS = [0.125, 0.25, 1 / 6, 0.5, 1 / 3, 0.75, 1, 2 / 3, 1.5, 2, 4];
  function drawDelayView(cv, slot) {
    var g = fit2d(cv); if (!g) return;
    var ctx = g.ctx, w = g.w, h = g.h, c = CH[slot], hue = hueOfSlot(slot), sat = 92, i;
    pane(ctx, w, h);
    var mix = sval(slot, 'dly_mix'), on = mix > 0, fb = sval(slot, 'dly_fb'), pp = sval(slot, 'dly_pp') >= 0.5, damp = sval(slot, 'dly_damp');
    var div = DLY_BEATS[Math.round(sval(slot, 'dly_div'))] || 0.5, bpm = F.bpm || gval('tempo'), beats = Math.min(8, Math.max(2, Math.ceil(div * 7)));
    var x0 = 14, x1 = w - 12, top = 24, bot = h - 10, mid = (top + bot) / 2, A = (bot - top) / 2 - 2, X = function (b) { return x0 + (x1 - x0) * b / beats; };
    for (i = 0; i <= beats * 4; i++) { ctx.fillStyle = i % 4 ? 'rgba(255,255,255,0.04)' : 'rgba(255,255,255,0.1)'; ctx.fillRect(X(i / 4) - 0.5, top, 1, bot - top); }
    ctx.fillStyle = 'rgba(255,255,255,0.06)'; ctx.fillRect(x0, mid - 0.5, x1 - x0, 1);
    var bw = Math.max(3, Math.min(9, (x1 - x0) * div / beats * 0.3));
    function bar(b, amp, side, light) { // side: 1 up (left), -1 down (right), 0 both
      var hh = A * Math.min(1, amp), x = X(b) - bw / 2;
      ctx.fillStyle = Knobs.hsla(hue, sat, light, on ? 0.85 : 0.25);
      if (side >= 0) ctx.fillRect(x, mid - hh, bw, hh);
      if (side <= 0) ctx.fillRect(x, mid, bw, hh);
    }
    bar(0, 1, 0, 80);
    for (var k = 1; on && k * div <= beats + 1e-6 && k < 64; k++) {
      var amp = mix * Math.pow(fb, k - 1), light = 66 - Math.min(40, (k - 1) * damp * 14);
      if (amp < 0.01) break;
      bar(k * div, amp, pp ? (k % 2 ? 1 : -1) : 0, light);
    }
    var last = c.notes.length ? c.notes[c.notes.length - 1].t : -1e9, since = (performance.now() / 1000 - last) * bpm / 60;
    if (on && since >= 0 && since < beats) { ctx.fillStyle = 'rgba(239,230,216,0.45)'; ctx.fillRect(X(since) - 0.5, top, 1, bot - top); }
    caption(ctx, 'DELAY · ' + nameOf(sid(slot, 'dly_div')) + ' · ' + Math.round(Math.min(2, div * 60 / bpm) * 1000) + ' ms' + (pp ? ' · PING-PONG' : ''));
    offNote(ctx, w, h, on);
  }

  /* duck: the duck envelope over the last four seconds (the gain hanging from the top line, dipping on each of
     the source's hits, in the source's color) */
  function drawDuckView(cv, slot) {
    var g = fit2d(cv); if (!g) return;
    var ctx = g.ctx, w = g.w, h = g.h, c = CH[slot], hue = hueOfSlot(slot), sat = 92, i;
    pane(ctx, w, h);
    var src = Math.round(sval(slot, 'duck_src')) - 1, amt = sval(slot, 'duck_amt'), on = ducked(slot);
    var x0 = 12, x1 = w - 12, top = 24, bot = h - 10, H = bot - top, N = c.dkHist.length, sh = on ? hueOfSlot(src) : hue;
    ctx.fillStyle = 'rgba(255,255,255,0.06)'; ctx.fillRect(x0, top, x1 - x0, 1);
    if (on) { ctx.setLineDash([2, 4]); ctx.fillStyle = 'rgba(255,255,255,0.05)'; ctx.fillRect(x0, top + H * amt, x1 - x0, 1); ctx.setLineDash([]); }
    var p = new Path2D(); p.moveTo(x0, top);
    for (i = 0; i < N; i++) { var v = c.dkHist[(c.dkPos + i) % N]; p.lineTo(x0 + (x1 - x0) * i / (N - 1), top + H * Math.min(1, v)); }
    var fill = new Path2D(p); fill.lineTo(x1, top); fill.closePath();
    ctx.fillStyle = Knobs.hsla(sh, sat, 55, on ? 0.22 : 0.06); ctx.fill(fill);
    Knobs.neon(ctx, p, sh, sat, Math.max(1.4, h / 70), on ? 0.8 : 0.15);
    caption(ctx, on ? 'DUCK · by ' + slotName(src) + ' · ' + Math.round(amt * 100) + '%' : 'DUCK · off');
  }

  /* the shared reverb: its decay as an envelope after the pre-delay gap (the highs dashed, dying sooner by the
     damping), on a seconds scale, with the reverb's own level now as a line across. In a channel (slot >= 0) the
     same tail in the channel's color, as bright as its send. */
  function drawSpaceView(cv, slot) {
    var g = fit2d(cv); if (!g) return;
    var chan = slot >= 0, ctx = g.ctx, w = g.w, h = g.h, hue = chan ? hueOfSlot(slot) : Knobs.hueOf(F.cr), sat = chan ? 92 : 80, i;
    pane(ctx, w, h);
    var t60 = 0.2 * Math.pow(100, gval('rev_decay')), pre = 0.25 * gval('rev_pre'), dampHz = 18000 * Math.pow(1200 / 18000, gval('rev_damp'));
    var size = 0.4 + 1.2 * gval('rev_size'), ret = chan ? sval(slot, 'rev_send') : gval('rev_return'), span = Math.min(24, Math.max(2, pre + t60 * 1.1));
    var x0 = 12, x1 = w - 12, top = 24, bot = h - 12, X = function (t) { return x0 + (x1 - x0) * t / span; }, Y = function (db) { return top + (bot - top) * Math.min(1, -db / 60); };
    var stepS = span > 8 ? 2 : span > 4 ? 1 : 0.5;
    for (var t = stepS; t < span; t += stepS) { ctx.fillStyle = 'rgba(255,255,255,0.05)'; ctx.fillRect(X(t) - 0.5, top, 1, bot - top); Knobs.text(ctx, t + ' s', X(t), bot + 6, '500 8px ' + Knobs.MONO, 'rgba(220,220,228,0.35)', 'center'); }
    var rise = 0.012 * size, hiT60 = t60 * Math.min(1, Math.pow(dampHz / 18000, 0.6)), p = new Path2D(), q = new Path2D();
    for (i = 0; i <= 160; i++) {
      var tt = span * i / 160, a = tt < pre ? -60 : tt < pre + rise ? -60 + 60 * (tt - pre) / rise : -60 * (tt - pre - rise) / t60, b = tt < pre + rise ? a : -60 * (tt - pre - rise) / hiT60;
      if (i) { p.lineTo(X(tt), Y(a)); q.lineTo(X(tt), Y(b)); } else { p.moveTo(X(tt), Y(a)); q.moveTo(X(tt), Y(b)); }
    }
    var fill = new Path2D(p); fill.lineTo(X(span), bot); fill.lineTo(X(0), bot); fill.closePath();
    var gr = ctx.createLinearGradient(0, top, 0, bot); gr.addColorStop(0, Knobs.hsla(hue, sat, 58, 0.18 + 0.2 * ret)); gr.addColorStop(1, Knobs.hsla(hue, sat, 50, 0.02));
    ctx.fillStyle = gr; ctx.fill(fill);
    ctx.save(); ctx.setLineDash([3, 4]); ctx.strokeStyle = 'rgba(239,230,216,0.4)'; ctx.lineWidth = 1; ctx.stroke(q); ctx.restore();
    Knobs.neon(ctx, p, hue, sat, Math.max(1.4, h / 70), 0.45 + 0.55 * ret);
    var t60s = (t60 < 10 ? t60.toFixed(1) : Math.round(t60)) + ' s';
    caption(ctx, chan ? (ret > 0 ? 'REVERB · send ' + Math.round(ret * 100) + '% · ' + t60s : 'REVERB · off') :
      'SPACE · ' + t60s + ' · pre ' + Math.round(pre * 1000) + ' ms · return ' + Math.round(ret * 100) + '%');
  }

  /* master: the output's level over the last four seconds (rms filled, peak as a line) on a dBFS scale with the
     ceiling dashed, and on the right the level now and the glue's and the limiter's gain reduction (hanging down) */
  function drawMasterView(cv) {
    var g = fit2d(cv); if (!g) return;
    var ctx = g.ctx, w = g.w, h = g.h, hue = Knobs.hueOf(F.cr), sat = 80, i, N = MH.rms.length;
    pane(ctx, w, h);
    var x0 = 12, x1 = w - 100, top = 24, bot = h - 14, H = bot - top;
    function Y(lin) { var db = 20 * Math.log(Math.max(lin, 1e-5)) / Math.LN10; return top + H * Math.min(1, Math.max(0, -db / 48)); }
    for (var d = 12; d < 48; d += 12) {
      ctx.fillStyle = 'rgba(255,255,255,0.05)'; ctx.fillRect(x0, top + H * d / 48, x1 - x0, 1);
      Knobs.text(ctx, '-' + d, x1 + 4, top + H * d / 48, '500 8px ' + Knobs.MONO, 'rgba(220,220,228,0.35)');
    }
    var cy = Y(Math.pow(10, gval('ceiling') / 20));
    ctx.save(); ctx.setLineDash([4, 4]); ctx.strokeStyle = 'rgba(239,230,216,0.45)'; ctx.beginPath(); ctx.moveTo(x0, cy); ctx.lineTo(x1, cy); ctx.stroke(); ctx.restore();
    var pr = new Path2D(), pp = new Path2D();
    pr.moveTo(x0, bot);
    for (i = 0; i < N; i++) {
      var k = (MH.pos + i) % N, x = x0 + (x1 - x0) * i / (N - 1);
      pr.lineTo(x, Y(MH.rms[k]));
      if (i) pp.lineTo(x, Y(MH.peak[k])); else pp.moveTo(x, Y(MH.peak[k]));
    }
    pr.lineTo(x1, bot); pr.closePath();
    var gr = ctx.createLinearGradient(0, top, 0, bot); gr.addColorStop(0, Knobs.hsla(hue, sat, 58, 0.4)); gr.addColorStop(1, Knobs.hsla(hue, sat, 50, 0.04));
    ctx.fillStyle = gr; ctx.fill(pr);
    Knobs.neon(ctx, pp, hue, sat, Math.max(1.2, h / 90), 0.6);
    function bar(x, y0, y1, col, lab) {
      ctx.fillStyle = 'rgba(255,255,255,0.05)'; ctx.fillRect(x, top, 12, H);
      ctx.fillStyle = col; ctx.fillRect(x, y0, 12, Math.max(0, y1 - y0));
      Knobs.text(ctx, lab, x + 6, bot + 7, '500 8px ' + Knobs.MONO, 'rgba(220,220,228,0.4)', 'center');
    }
    bar(w - 70, Y(F.mp), bot, Knobs.hsla(hue, sat, 62, 0.85), 'OUT');
    bar(w - 50, top, top + H * Math.min(1, F.gg / 12), 'rgba(255,190,110,0.8)', 'GLUE');
    bar(w - 30, top, top + H * Math.min(1, F.lg / 12), 'rgba(255,110,90,0.85)', 'LIM');
    caption(ctx, 'MASTER · peak ' + (F.mp > 1e-5 ? (20 * Math.log(F.mp) / Math.LN10).toFixed(1) : '-inf') + ' dB · glue -' + F.gg.toFixed(1) + ' · limit -' + F.lg.toFixed(1) + ' dB');
  }

  /* chord: the notes it holds now on a keyboard of two and a half octaves around them, each lit in its note's color */
  var BLACK = [0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0];
  function drawChord(cv, slot) {
    var g = fit2d(cv); if (!g) return;
    var ctx = g.ctx, w = g.w, h = g.h, c = CH[slot], i, n;
    pane(ctx, w, h);
    var notes = c.active ? c.vnotes : [], lo = 1e9, hi = -1e9;
    for (i = 0; i < notes.length; i++) { lo = Math.min(lo, notes[i]); hi = Math.max(hi, notes[i]); }
    if (lo > hi) { lo = 48; hi = 72; }
    var want = Math.round((lo + hi) / 2) - 15;
    // glide to a new register, but never leave a note off the keys
    c.kbLo = c.kbLo == null || lo < c.kbLo + 1 || hi > c.kbLo + 29 ? want : c.kbLo + (want - c.kbLo) * 0.1;
    var first = Math.round(c.kbLo), last = first + 30, whites = [], wIndex = {};
    for (n = first; n <= last; n++) if (!BLACK[((n % 12) + 12) % 12]) { wIndex[n] = whites.length; whites.push(n); }
    var x0 = 10, x1 = w - 10, top = 22, bot = h - 16, ww = (x1 - x0) / whites.length, lit = {}, level = 0.35 + 0.65 * Math.min(1, c.amp);
    for (i = 0; i < notes.length; i++) lit[Math.round(notes[i])] = true;
    function key(x, y, kw, kh, on, hue, off) {
      ctx.beginPath(); if (ctx.roundRect) ctx.roundRect(x, y, kw, kh, 3); else ctx.rect(x, y, kw, kh);
      ctx.fillStyle = on ? Knobs.hsla(hue, 85, 60, 0.88 * level) : off; ctx.fill();
      if (on) { ctx.save(); ctx.globalCompositeOperation = 'lighter'; ctx.shadowBlur = 12; ctx.shadowColor = Knobs.hsla(hue, 90, 60, 0.8 * level); ctx.fill(); ctx.restore(); }
    }
    for (i = 0; i < whites.length; i++) {
      var wn = whites[i], wpc = ((wn % 12) + 12) % 12;
      key(x0 + i * ww + 1, top, ww - 2, bot - top, lit[wn], Knobs.hueOf(wpc), 'rgba(255,255,255,0.10)');
      if (wpc === 0) Knobs.text(ctx, 'C' + Math.floor(wn / 12 - 1), x0 + i * ww + ww / 2, bot + 8, '500 9px ' + Knobs.MONO, 'rgba(220,220,228,0.4)', 'center');
    }
    for (n = first + 1; n <= last; n++) {
      var pc = ((n % 12) + 12) % 12; if (!BLACK[pc] || wIndex[n - 1] == null) continue;
      var bw = ww * 0.62, bx = x0 + (wIndex[n - 1] + 1) * ww - bw / 2; // between the white below and the one above
      key(bx, top, bw, (bot - top) * 0.6, lit[n], Knobs.hueOf(pc), '#0c0c0e');
    }
    caption(ctx, (chordArp(slot) ? 'CHORD · ARP' + (sval(slot, 'chord_ring') >= 0.5 ? ' · LET RING' : '') : 'CHORD') + (notes.length ? ' · ' + notes.map(noteName).join(' ') : ''));
  }

  /* pitch: the notes it played in the last four seconds on a roll, the chord's tones as lanes behind */
  var ROLL_S = 4;
  function drawRoll(cv, slot) {
    var g = fit2d(cv); if (!g) return;
    var ctx = g.ctx, w = g.w, h = g.h, c = CH[slot], now = performance.now() / 1000, i, n;
    pane(ctx, w, h);
    var lo = 1e9, hi = -1e9;
    for (i = 0; i < c.notes.length; i++) { lo = Math.min(lo, c.notes[i].n); hi = Math.max(hi, c.notes[i].n); }
    if (lo > hi) { lo = c.note - 6; hi = c.note + 6; }
    var span = Math.max(12, hi - lo + 4), cen = (lo + hi) / 2;
    c.rollLo = c.rollLo == null ? cen - span / 2 : c.rollLo + (cen - span / 2 - c.rollLo) * 0.08;
    c.rollSpan = c.rollSpan == null ? span : c.rollSpan + (span - c.rollSpan) * 0.08;
    var x0 = 10, x1 = w - 10, top = 22, bot = h - 8, Y = function (nn) { return bot - (bot - top) * (nn - c.rollLo) / c.rollSpan; };
    for (n = Math.ceil(c.rollLo); n <= c.rollLo + c.rollSpan; n++) {
      var pc = ((n % 12) + 12) % 12;
      if ((F.cm >> pc) & 1) { ctx.fillStyle = Knobs.hsla(Knobs.hueOf(pc), 70, 50, pc === F.cr ? 0.16 : 0.08); ctx.fillRect(x0, Y(n) - 2, x1 - x0, 4); }
    }
    for (i = 0; i < c.notes.length; i++) {
      var e = c.notes[i], age = now - e.t; if (age > ROLL_S || age < 0) continue;
      var x = x1 - (x1 - x0) * age / ROLL_S, y = Y(e.n), k = 1 - age / ROLL_S, hh = Knobs.hueOf(((e.n % 12) + 12) % 12);
      ctx.fillStyle = Knobs.hsla(hh, 85, 62, 0.25 + 0.6 * k); ctx.beginPath(); if (ctx.roundRect) ctx.roundRect(x - 1, y - 2.5, 12, 5, 2.5); else ctx.rect(x - 1, y - 2.5, 12, 5); ctx.fill();
      if (age < 0.25) Knobs.glowDot(ctx, x + 5, y, 4, hh, 92, 1 - age / 0.25);
    }
    caption(ctx, 'NOTES' + (c.active ? ' · ' + noteName(c.note) : ''));
  }

  /* visual: what the stage draws of the channel against what it hears. Left, the incoming wave (faint) and the
     wave the stage draws (bright, smoothed by Flow and Detail, swelling on the pulse); right, two meters: the
     channel's level now (thin) and the level the stage follows (wide) */
  function drawMotion(cv, slot) {
    var g = fit2d(cv); if (!g) return;
    var ctx = g.ctx, w = g.w, h = g.h, c = CH[slot], m = MO[slot], hue = hueOfSlot(slot), sat = 92, i;
    pane(ctx, w, h);
    var mx = w - 52, mid = h / 2 + 6, A = (h - 32) * 0.46, raw = new Path2D(), drawn = new Path2D(), N = 200;
    var ra = c.active ? Math.min(1, c.amp) : 0, da = Math.min(1, m.lvl * presenceOf(slot) * Math.max(0.2, 1 + 0.6 * m.swell));
    for (i = 0; i <= N; i++) {
      var u = i / N, idx = Math.floor(((u * 2) % 1) * 128) % 128, x = 10 + (mx - 20) * u;
      if (i) { raw.lineTo(x, mid - c.wave[idx] * ra * A); drawn.lineTo(x, mid - m.out[idx] * da * A); }
      else { raw.moveTo(x, mid - c.wave[idx] * ra * A); drawn.moveTo(x, mid - m.out[idx] * da * A); }
    }
    ctx.strokeStyle = 'rgba(220,220,228,0.18)'; ctx.lineWidth = 1; ctx.stroke(raw);
    Knobs.neon(ctx, drawn, hue, sat, Math.max(1.4, h / 70), (0.35 + 0.65 * Math.min(1, m.lvl)) * (0.2 + 0.8 * mv(slot, 'v_alpha'))); // as opaque as on the stage
    var top = 22, bot = h - 10, bh = bot - top;
    ctx.fillStyle = 'rgba(255,255,255,0.05)'; ctx.fillRect(mx + 4, top, 14, bh); ctx.fillRect(mx + 24, top, 6, bh);
    ctx.fillStyle = Knobs.hsla(hue, sat, 60, 0.85); ctx.fillRect(mx + 4, bot - bh * Math.min(1, m.lvl), 14, bh * Math.min(1, m.lvl));
    ctx.fillStyle = 'rgba(236,236,240,0.6)'; ctx.fillRect(mx + 24, bot - bh * ra, 6, bh * ra);
    caption(ctx, 'STAGE' + (m.swell > 0.05 ? ' · swell ' + Math.round(m.swell * 100) + '%' : ''));
  }

  /* particles: the wave the stage draws (as in the view above) made of points, as the knobs set them: as many as
     Density says, strayed by Scatter, shaking by Jitter (more as it plays louder), each its own color by Color
     jitter, as large and soft as Point size and Softness */
  function phash(n) { var x = Math.sin(n * 127.1 + 311.7) * 43758.5453; return x - Math.floor(x); }
  function drawPartView(cv, slot) {
    var g = fit2d(cv); if (!g) return;
    var ctx = g.ctx, w = g.w, h = g.h, m = MO[slot], hue = hueOfSlot(slot), i;
    pane(ctx, w, h);
    var mid = h / 2 + 6, A = (h - 32) * 0.4, n = Math.round(30 + 400 * mv(slot, 'v_pdens')), sc = mv(slot, 'v_pscat'), cj = mv(slot, 'v_pcol');
    var jit = mv(slot, 'v_pjit') * (0.25 + 0.75 * Math.min(1, m.lvl * 1.3)), pr = Math.max(0.6, mv(slot, 'v_psize') * 0.55), soft = mv(slot, 'v_psoft');
    var da = Math.max(0.15, Math.min(1, m.lvl * presenceOf(slot))), al = 0.25 + 0.75 * mv(slot, 'v_alpha');
    ctx.globalCompositeOperation = 'lighter';
    for (i = 0; i < n; i++) {
      var u = phash(i + 0.5), idx = Math.floor(((u * 2) % 1) * 128) % 128, f = 9 + 23 * phash(i + 7.1), ph = TAU * phash(i + 3.3);
      var x = 10 + (w - 20) * u + Math.sin(T * f + ph) * jit * 5, y = mid - m.out[idx] * da * A + (phash(i + 1.7) * 2 - 1) * sc * A * 0.5 + Math.sin(T * f * 1.13 + ph * 2.3) * jit * 5;
      var hh = hue + (phash(i + 9.9) - 0.5) * cj * 140, ll = 62 * (1 + (phash(i + 4.4) - 0.5) * cj * 0.9), a = al * (0.55 + 0.45 * phash(i + 2.2));
      if (soft > 0.05) { var gr = ctx.createRadialGradient(x, y, 0, x, y, pr * 1.6); gr.addColorStop(0, Knobs.hsla(hh, 92, ll, a)); gr.addColorStop(1 - soft * 0.6, Knobs.hsla(hh, 92, ll, a * (1 - soft) * 0.6)); gr.addColorStop(1, Knobs.hsla(hh, 92, ll, 0)); ctx.fillStyle = gr; }
      else ctx.fillStyle = Knobs.hsla(hh, 92, ll, a);
      ctx.beginPath(); ctx.arc(x, y, pr * 1.6, 0, TAU); ctx.fill();
    }
    ctx.globalCompositeOperation = 'source-over';
    caption(ctx, 'PARTICLES · ' + nameOf(vsid(slot, 'v_mode')).toLowerCase());
  }

  /* a channel row's view: two cycles of what it plays now, as tall as it is loud */
  function drawScope(cv, slot) {
    var g = fit2d(cv); if (!g) return;
    var ctx = g.ctx, w = g.w, h = g.h, c = CH[slot], hue = hueOfSlot(slot), sat = c.sat, muted = sval(slot, 'mute') >= 0.5, i;
    pane(ctx, w, h);
    var amp = c.active ? Math.min(1, c.amp) : 0, mid = h / 2, x0 = 10, x1 = w - 10, A = h * 0.42, q = new Path2D(), N = 240;
    ctx.fillStyle = 'rgba(255,255,255,0.05)'; ctx.fillRect(x0, mid - 0.5, x1 - x0, 1);
    for (i = 0; i <= N; i++) {
      var u = i / N, idx = Math.floor(((u * 2) % 1) * 128) % 128, y = mid - c.wave[idx] * amp * A;
      if (i) q.lineTo(x0 + (x1 - x0) * u, y); else q.moveTo(x0, y);
    }
    var qf = new Path2D(q); qf.lineTo(x1, mid); qf.lineTo(x0, mid); qf.closePath();
    var gr = ctx.createLinearGradient(0, mid - A, 0, mid + A);
    gr.addColorStop(0, Knobs.hsla(hue, sat, 60, 0.22 * amp * (muted ? 0.3 : 1))); gr.addColorStop(0.5, Knobs.hsla(hue, sat, 50, 0.02)); gr.addColorStop(1, Knobs.hsla(hue, sat, 60, 0.22 * amp * (muted ? 0.3 : 1)));
    ctx.fillStyle = gr; ctx.fill(qf);
    Knobs.neon(ctx, q, hue, sat, 1.8, muted ? 0.12 : 0.25 + 0.75 * amp);
  }

  /* a fader in a channel row: level (with the channel's meter under it) or pan (from the centre); or the master
     volume in the top bar (pearl, the output's peak under it); or a route's amount in the Modulation tab (its
     source's colour) */
  function drawFader(cv) {
    var g = fit2d(cv); if (!g) return;
    var id = +cv.getAttribute('data-id'), d = ORDER[id]; if (!d) return;
    var amtOf = isRoute(id) ? srcTone(Math.round(val(rid((id - ROUTE_BASE) >> 2, 'src')))) : null;
    var h = Math.min(g.h, 34); if (g.h > h) g.ctx.translate(0, (g.h - h) / 2); // a taller canvas (Mini's 54 px touch target): the bar 34 high across its middle
    var master = d.slot < 0 && !amtOf, ctx = g.ctx, w = g.w, spec = specOf(id),
        c = amtOf ? { hue: amtOf.h, sat: amtOf.s } : master ? { hue: Knobs.PEARL.h, sat: 30 } : CH[d.slot];
    var hue = c.hue == null ? 90 : c.hue, sat = c.sat;
    var x = (val(id) - d.min) / (d.max - d.min), from = d.min < 0 ? (0 - d.min) / (d.max - d.min) : 0, hot = drag && drag.cv === cv, near = hot || hover === cv;
    var muted = !master && !amtOf && sval(d.slot, 'mute') >= 0.5, r = 6, md = modOf(id), am = assignState(id);
    ctx.beginPath(); if (ctx.roundRect) ctx.roundRect(0.5, 0.5, w - 1, h - 1, r); else ctx.rect(0.5, 0.5, w - 1, h - 1);
    ctx.fillStyle = 'rgba(0,0,0,0.3)'; ctx.fill(); ctx.strokeStyle = near ? 'rgba(255,255,255,0.16)' : 'rgba(255,255,255,0.06)'; ctx.lineWidth = 1; ctx.stroke();
    ctx.save(); ctx.clip();
    var a = Math.min(from, x) * w, b = Math.max(from, x) * w;
    ctx.fillStyle = Knobs.hsla(hue, sat, 50, muted ? 0.12 : hot ? 0.42 : 0.3); ctx.fillRect(a, 0, Math.max(1, b - a), h);
    if (d.s === 'level' && c.active && !muted) { ctx.fillStyle = Knobs.hsla(hue, sat, 66, 0.85); ctx.fillRect(0, h - 3, w * x * Math.min(1, c.amp), 3); }
    if (master) { ctx.fillStyle = F.mp > 0.97 ? 'rgba(229,72,77,0.9)' : 'rgba(239,230,216,0.7)'; ctx.fillRect(0, h - 3, w * Math.min(1, F.mp), 3); }
    if (from > 0) { ctx.fillStyle = 'rgba(255,255,255,0.18)'; ctx.fillRect(from * w - 0.5, 4, 1, h - 8); }
    ctx.fillStyle = Knobs.hsla(hue, Math.min(sat, 80), 74, muted ? 0.4 : 1); ctx.fillRect(Math.max(0, Math.min(w - 2, x * w - 1)), 0, 2, h);
    if (md) { // as a knob's rings: a band per route along the top in its source's colour (three rows, past three the
              // rest share the third in pearl), a dot where that source has it, a white mark where the engine has it
      var own = md.rings.length > 3 ? 2 : md.rings.length, mine = md.rings.some(function (q) { return q.r === md.hot; });
      md.rings.forEach(function (q, i) {
        var y = 1 + Math.min(i, own) * 3.4, k = mine && q.r !== md.hot ? 0.28 : 1;
        ctx.fillStyle = i >= own && q.r !== md.hot ? Knobs.hsla(Knobs.PEARL.h, 30, 82, 0.42 * k) : Knobs.hsla(q.h, q.s, q.l, (q.r === md.hot ? 0.95 : 0.65) * k);
        ctx.fillRect(q.lo * w, y, Math.max(1, (q.hi - q.lo) * w), 2.6);
        if (q.at >= 0) { ctx.fillStyle = Knobs.hsla(q.h, q.s, Math.min(94, q.l + 18), k); ctx.fillRect(Math.max(0, Math.min(w - 4, q.at * w - 2)), y - 0.7, 4, 4); }
      });
      if (md.live >= 0) { ctx.fillStyle = 'rgba(255,255,255,0.92)'; ctx.fillRect(Math.max(0, Math.min(w - 2, md.live * w - 1)), 0, 2, h); }
    }
    ctx.restore();
    if (am) { var at = srcTone(assign.src); ctx.save(); ctx.beginPath(); if (ctx.roundRect) ctx.roundRect(1, 1, w - 2, h - 2, r); else ctx.rect(1, 1, w - 2, h - 2);
      ctx.setLineDash(am === 2 ? [] : [3, 3]); ctx.strokeStyle = Knobs.hsla(at.h, at.s, at.l, am === 2 ? 0.95 : 0.7); ctx.lineWidth = 1.5; ctx.stroke(); ctx.restore(); }
    // the text: level labelled on the left with its value on the right; pan's value on the side away from its marker
    ctx.save(); ctx.shadowColor = 'rgba(0,0,0,0.95)'; ctx.shadowBlur = 4;
    var col = near ? 'rgba(236,236,240,0.95)' : 'rgba(220,220,228,0.75)', fr = drag && drag.cv === cv && drag.assign ? routeOf(drag.assign.r) : null;
    if (fr) Knobs.text(ctx, routeLine(fr), w / 2, h / 2, '600 11px ' + Knobs.MONO, srcColor(fr.src), 'center'); // assigning: the new route's amount
    else if (d.s === 'pan') {
      var pv = val(id), vt = Math.abs(pv) < 0.005 ? 'C' : (pv < 0 ? 'L' : 'R') + Math.round(Math.abs(pv) * 100);
      if (pv < 0) Knobs.text(ctx, vt, w - 7, h / 2, '500 11px ' + Knobs.MONO, col, 'right'); else Knobs.text(ctx, vt, 7, h / 2, '500 11px ' + Knobs.MONO, col);
    } else {
      Knobs.text(ctx, cv.getAttribute('data-lab') || (master ? 'VOL' : 'LVL'), 8, h / 2, '600 10px ' + Knobs.SANS, 'rgba(220,220,228,0.5)');
      Knobs.text(ctx, Knobs.fmt(spec.fmt, val(id), spec), w - 8, h / 2, '500 11px ' + Knobs.MONO, col, 'right');
    }
    ctx.restore();
  }

  /* --------------------------------------------------------- the motion layer
     Between the telemetry and the stage, so the stage follows the music instead of copying it, frame by frame:
     - level: a follower that rises and falls at its own speeds (Flow sets them; a channel's Response scales its own)
     - shape: the drawn wave blends toward the incoming one, then Detail smooths it (low: a soft round form;
       high: every corner of the wave); the cycle count follows the note just as slowly
     - hits: each note kicks a damped spring, so the form rings and settles instead of jumping
     - pulse: a swell on the beat (every beat, 2 beats, bar, 2 bars), as deep as Pulse, only while playing
     - fades: a channel that stops fades out with its level instead of vanishing; mute and hide have a fade of their
       own (a quarter second, whatever Flow is), and the level follower keeps following underneath, so a channel
       shown again comes back at its level now; focus dims fade too
     - drift accumulates at Motion's rate, so turning it never jumps the form (the form's turn is the camera's) */
  var MO = {}, PULSE_BEATS = [1, 2, 4, 8], mo = { drift: 0, run: 0, pulse: 0 };
  for (var m0 = 0; m0 < SLOTS; m0++) MO[m0] = { lvl: 0, shape: new Float32Array(128), t1: new Float32Array(128), out: new Float32Array(128), bytes: new Uint8Array(128),
                                               note: -1, x: 0, v: 0, lastHit: 0, hit: 0, dim: 1, vis: 1, swell: 0 };
  function lerp(a, b, t) { return a + (b - a) * t; }
  function ease(dt, tau) { return 1 - Math.exp(-dt / Math.max(1e-3, tau)); }
  function presenceOf(s) { return 0.25 + 1.5 * mv(s, 'v_presence'); } // 0.25 .. 1 (middle) .. 1.75
  /* a circular moving average of radius r: the wave is one cycle, so it wraps */
  function blur(src, dst, r) {
    if (r < 1) { dst.set(src); return; }
    var sum = 0, n = 2 * r + 1, i;
    for (i = -r; i <= r; i++) sum += src[(i + 128) & 127];
    for (i = 0; i < 128; i++) { dst[i] = sum / n; sum += src[(i + r + 1) & 127] - src[(i - r + 128) & 127]; }
  }

  function motion(dt, now) {
    var flow = mg('v_flow'), mot = Math.pow(4, 2 * mg('v_motion') - 1), nowS = now / 1000;
    mo.drift += dt * mot;
    mo.run += ((F.playing ? 1 : 0) - mo.run) * ease(dt, 0.6);
    var ph = F.beat / PULSE_BEATS[Math.round(mg('v_pdiv'))]; ph -= Math.floor(ph);
    mo.pulse = Math.pow(0.5 + 0.5 * Math.cos(2 * Math.PI * ph), 3) * mg('v_pulse') * mo.run;
    var anyOpen = false;
    for (var o in ui.open) if (ui.open[o] && sval(+o, 'gen') > 0) anyOpen = true;
    var focusDim = ui.focus && anyOpen && !ui.rail;
    var w = 2 * Math.PI * lerp(5, 1.6, flow), z = 0.3, steps = Math.max(1, Math.ceil(dt / 0.004)), hs = dt / steps;
    for (var s = 0; s < SLOTS; s++) {
      var c = CH[s], m = MO[s], i;
      var resp = Math.pow(2, (mv(s, 'v_response') - 0.5) * 3.4); // 0.3x .. 1 .. 3.2x the Flow's times
      var rise = lerp(0.015, 0.35, Math.pow(flow, 1.2)) * resp, fall = lerp(0.12, 2.2, flow) * resp, shapeT = lerp(0.02, 0.9, flow) * resp;
      var on = c.active && sval(s, 'gen') > 0, seen = sval(s, 'show') >= 0.5 && sval(s, 'mute') < 0.5 && !c.muted; // muted or hidden: off the stage
      var target = on ? Math.min(1, c.amp) : 0;
      m.lvl += (target - m.lvl) * ease(dt, target > m.lvl ? rise : fall);
      m.vis += ((seen ? 1 : 0) - m.vis) * ease(dt, 0.08);
      m.dim += ((focusDim && !isOpen(s) ? 0.28 : 1) - m.dim) * ease(dt, 0.25);
      m.hit = 0;
      for (i = 0; i < c.notes.length; i++) { var e = c.notes[i]; if (e.t > m.lastHit && e.t <= nowS) { m.lastHit = e.t; m.hit = 1; } }
      if (m.lvl < 0.002 && !on) { m.x = m.v = 0; m.swell = 0; continue; }
      // the shape
      var a = ease(dt, shapeT), r = Math.round(Math.pow(1 - mv(s, 'v_detail'), 1.5) * 24), pk = 0;
      for (i = 0; i < 128; i++) m.shape[i] += (c.wave[i] - m.shape[i]) * a;
      blur(m.shape, m.t1, r); blur(m.t1, m.out, r);
      for (i = 0; i < 128; i++) pk = Math.max(pk, Math.abs(m.out[i]));
      var g = pk > 1e-3 ? 1 / pk : 0;
      for (i = 0; i < 128; i++) { m.out[i] *= g; m.bytes[i] = Math.max(0, Math.min(255, Math.round(m.out[i] * 127 + 128))); }
      m.note = m.note < 0 ? c.note : m.note + (c.note - m.note) * ease(dt, shapeT * 1.5);
      // the spring: kicked by each note, rings at a pitch Flow lowers, settles
      if (m.hit) m.v += w * 0.8;
      for (i = 0; i < steps; i++) { var acc = -w * w * m.x - 2 * z * w * m.v; m.v += acc * hs; m.x += m.v * hs; }
      m.x = Math.max(-1.5, Math.min(1.5, m.x));
      m.swell = mo.pulse * mv(s, 'v_cpulse') + m.x * 0.5 * (1 - 0.4 * flow);
    }
  }

  /* ---------------------------------------------------------- the camera
     How the stage is seen. Tilt (degrees seen from above, negative from below), Zoom, Perspective and Orbit (the
     form's turn of its own, once Motion's) are page parameters with knobs in the Visual tab; Yaw (how far it has
     turned, in degrees, kept when the state is saved) and Lie (the Side view: the form laid on its side) are page
     parameters without a knob. They are where the camera is going: what the stage draws eases toward them,
     quickly under the hand, slowly for a view chip or an opened file, so nothing jumps. On the stage: drag to
     orbit (the turn of its own waits while held), the wheel zooms, a double-click resets. The form is a body of
     revolution, so Front and a quarter turn round look alike: Side lays it down instead. */
  var DEG = Math.PI / 180, TAU = Math.PI * 2;
  var CAM_NAMES = [['front', 'Front'], ['above', 'Above'], ['side', 'Side'], ['orbit', 'Orbit']];
  var CAM_VIEWS = { front: { v_tilt: 0, v_lie: 0, v_orbit: 0 }, above: { v_tilt: 90, v_lie: 0, v_orbit: 0 }, side: { v_tilt: 0, v_lie: 1, v_orbit: 0 },
                    orbit: { v_tilt: 17, v_lie: 0 } }; // orbit: turning again at the speed it had
  var cam = { on: false, yaw: 0, yawT: 0, tilt: 17, zoom: 1, persp: 0.44, lie: 0, tau: 0.35, grab: null, lastOrbit: 0.4, vTilt: 17, vZoom: 1, vPersp: 0.44 };
  /** Aim the yaw at an angle the short way round, over tau seconds. */
  function camAim(rad, tau) {
    if (!cam || !cam.on) return;
    cam.yawT += ((rad - cam.yawT) % TAU + TAU * 1.5) % TAU - TAU / 2; cam.tau = tau;
  }
  /** The yaw into the page state (wrapped, in degrees): the camera keeps it live, the state only on a save. */
  function camKeep() {
    var id = vgid('v_yaw'); if (!cam || !cam.on || !ORDER[id]) return;
    var d = ((cam.yawT / DEG) % 360 + 540) % 360 - 180;
    P[id] = ui.vp[id] = Math.round(d * 10) / 10;
  }
  function camView(name) {
    var v = CAM_VIEWS[name]; if (!v || !DEFS.v_tilt) return;
    if (gval('v_orbit') > 0.004) cam.lastOrbit = gval('v_orbit');
    for (var k in v) setParam(gid(k), v[k]);
    if (name === 'orbit') setParam(gid('v_orbit'), cam.lastOrbit);
    else camAim(0, 0.45); // face the front
    cam.tau = 0.45;
  }
  /** The view chip the camera is at now, or '' (after a drag, a knob turned, a reset to between). */
  function camViewNow() {
    if (!DEFS.v_tilt) return '';
    if (gval('v_orbit') > 0.004) return gval('v_lie') < 0.01 ? 'orbit' : '';
    if (Math.abs(cam.yawT - Math.round(cam.yawT / TAU) * TAU) > 0.035) return '';
    for (var k in CAM_VIEWS) if (k !== 'orbit' && Math.abs(gval('v_tilt') - CAM_VIEWS[k].v_tilt) < 1 && Math.abs(gval('v_lie') - CAM_VIEWS[k].v_lie) < 0.01) return k;
    return '';
  }
  function camReset() {
    if (!DEFS.v_tilt) return;
    ['v_tilt', 'v_zoom', 'v_persp', 'v_orbit', 'v_lie'].forEach(function (k) { setParam(gid(k), DEFS[k].def); });
    camAim(0, 0.45);
  }
  function camera(dt) {
    if (!DEFS.v_tilt) return;
    var tilt = gval('v_tilt'), zoom = gval('v_zoom'), persp = gval('v_persp'), lie = gval('v_lie'); // the knobs (the routes on them: below)
    if (!cam.on) { cam.on = true; cam.yaw = cam.yawT = gval('v_yaw') * DEG; cam.tilt = tilt; cam.zoom = zoom; cam.persp = persp; cam.lie = lie; } // the first frame: there at once
    if (!cam.grab) { var turn = ORBIT_K * Math.pow(mg('v_orbit'), 2) * dt; cam.yaw += turn; cam.yawT += turn; }
    if (Math.abs(cam.yawT) > 1000) { var wrap = Math.round(cam.yawT / TAU) * TAU; cam.yaw -= wrap; cam.yawT -= wrap; }
    var a = ease(dt, cam.tau);
    cam.yaw += (cam.yawT - cam.yaw) * a; cam.tilt += (tilt - cam.tilt) * a; cam.zoom += (zoom - cam.zoom) * a;
    cam.persp += (persp - cam.persp) * a; cam.lie += (lie - cam.lie) * a;
    // what the stage is seen from: the camera eased to the knobs, the routes on them on top (not eased: an LFO keeps its shape)
    cam.vTilt = cam.tilt + mg('v_tilt') - tilt; cam.vZoom = cam.zoom * mg('v_zoom') / zoom; cam.vPersp = cam.persp + mg('v_persp') - persp;
  }
  /* on the stage (the panel above it keeps its own events; keys still go to the host); Mini has none */
  if (elStage) elStage.addEventListener('pointerdown', function (e) {
    if (e.button !== 0 || !DEFS.v_tilt || scopeOn()) return; // the scope has no camera
    e.preventDefault();
    cam.grab = { x: e.clientX, y: e.clientY }; cam.tau = 0.07; elStage.classList.add('grab');
    try { elStage.setPointerCapture(e.pointerId); } catch (err) { /* fine */ }
  });
  if (elStage) elStage.addEventListener('pointermove', function (e) {
    if (!cam.grab) return;
    var k = 1 / uiScale(), dx = (e.clientX - cam.grab.x) * k, dy = (e.clientY - cam.grab.y) * k, id = gid('v_tilt'), d = ORDER[id];
    cam.grab.x = e.clientX; cam.grab.y = e.clientY;
    cam.yawT += dx * 0.006; // the front follows the hand
    var v = Math.max(d.min, Math.min(d.max, val(id) + dy * 0.35)); // down: seen more from above
    if (v !== val(id)) P[id] = ui.vp[id] = v;
    dirty = true;
  });
  function endGrab() { if (!cam.grab) return; cam.grab = null; elStage.classList.remove('grab'); saveUi(); dirty = true; }
  if (elStage) elStage.addEventListener('pointerup', endGrab);
  if (elStage) elStage.addEventListener('pointercancel', endGrab);
  if (elStage) elStage.addEventListener('wheel', function (e) {
    if (!e.deltaY || !DEFS.v_zoom || scopeOn()) return;
    e.preventDefault(); cam.tau = 0.15;
    setParam(gid('v_zoom'), gval('v_zoom') * Math.exp(-e.deltaY * 0.0012));
  }, { passive: false });
  if (elStage) elStage.addEventListener('dblclick', function () { if (!scopeOn()) camReset(); });

  /* ------------------------------------------------------------ the stage
     What the motion layer made of each channel, centred in the room the panel leaves (sliding there when the
     panel moves), the whole form swelling a little on the pulse; each channel's particles; the stage FX. */
  var fxKick = 0;
  function drawStage(dt, LW, LH, freeW, cx, bh) {
    var chans = [], flow = mg('v_flow'), drums = false, hit = 0;
    for (var s = 0; s < SLOTS; s++) {
      var c = CH[s], m = MO[s];
      if (m.lvl < 0.002 || m.vis < 0.004) continue;
      chans.push({ slot: s, bytes: m.bytes, hue: c.hue == null ? 90 : c.hue, sat: c.sat, rgb: null, amp: m.lvl, pres: presenceOf(s), swell: m.swell, look: lookOf(s),
                   hit: m.hit, hitSeed: c.hitSeed, note: m.note, dim: m.dim, fade: m.vis, size: mv(s, 'v_size'), alpha: mv(s, 'v_alpha'),
                   mode: Math.round(mv(s, 'v_mode')), pd: mv(s, 'v_pdens'), ps: mv(s, 'v_pscat'), pj: mv(s, 'v_pjit'), pc: mv(s, 'v_pcol'), pz: mv(s, 'v_psize'), pf: mv(s, 'v_psoft'),
                   pfill: mv(s, 'v_pfill') });
      if (isDrum(s)) { drums = true; if (m.hit) hit = Math.max(hit, m.vis * m.dim); }
    }
    stageColors(chans, dt);
    // Feedback's Kick: a drum hit (with no drum playing, the beat's pulse) punches the zoom, fading in 0.15 s
    fxKick = Math.max(fxKick * Math.exp(-dt / 0.15), drums ? hit : mo.pulse);
    var wantX = cx / LW * 2 - 1;
    stageX = stageX == null ? wantX : stageX + (wantX - stageX) * (1 - Math.exp(-dt / 0.12));
    var room = Math.min(freeW, LH - bh) / LH; // the room beside the panel and above the pin bar (bh: its height, 0 hidden)
    try { Shells.draw(elStage, chans, { merge: ui.view === 'merged' ? 1 : 0, time: T, dt: dt, scale: Math.min(0.95, room * 0.92) * (1 + 0.03 * mo.pulse) * cam.vZoom, zoom: cam.vZoom, offX: stageX, offY: bh / LH,
                                       spin: cam.yaw, tilt: cam.vTilt * DEG, roll: cam.lie * Math.PI / 2, persp: cam.vPersp * 0.5, drift: mo.drift, ripple: { str: 0.12 * (1.15 - 0.7 * flow), spd: 2.2 * (1 - 0.55 * flow) },
                                       fx: stageFx() }); } catch (e) { glStatus = 'draw error ' + e.message; }
  }
  /** COLOR, Palette and Cycle: each shown channel's rgb from the map. Palette: the channels in slot order spread
      along it (Spread 0: all at its middle), each moved further along by its level (Level). Cycle: the map running
      by at 0.01 .. 0.5 turns a second (Speed), the channels Spread apart; a map that does not join end to end runs
      back and forth. */
  var colPhase = 0;
  function stageColors(chans, dt) {
    if (!DEFS.col_mode) return;
    var mode = Math.round(mg('col_mode')), map = Math.round(mg('col_map')), spread = mg('col_spread'), n = chans.length;
    if (!mode) return;
    colPhase = (colPhase + dt * 0.01 * Math.pow(50, mg('col_speed'))) % 1;
    for (var i = 0; i < n; i++) {
      var pos = n > 1 ? i / (n - 1) : 0.5, t;
      if (mode === 1) t = 0.5 + (pos - 0.5) * spread * 0.85 + mg('col_lvl') * 0.35 * Math.min(1, chans[i].amp * 1.3) - mg('col_lvl') * 0.12;
      else {
        var x = colPhase + spread * i / Math.max(1, n);
        t = COLMAPS[map] ? 1 - Math.abs(2 * (x % 1) - 1) : x % 1; // Rainbow comes round; the rest go back and forth
      }
      chans[i].rgb = colMap(map, t);
    }
  }
  /** The stage FX that are on, as Shells.draw takes them (rates per second). Amount maps as scAnimator's trail
      slider: a fade per 1/60 s of 0.85 .. 0.995 (sqrt curve). */
  function stageFx() {
    var fx = {};
    if (!DEFS.fx_mir) return fx;
    if (mg('fx_mir') >= 0.5) fx.mirror = { div: Math.round(mg('fx_mir_div')), op: mg('fx_mir_op'), top: mg('fx_mir_top') >= 0.5 };
    if (mg('fx_fb') >= 0.5) fx.feedback = { fade: 0.85 + 0.145 * Math.sqrt(Math.max(0.01, mg('fx_fb_amt'))),
      zoom: 1.2 * mg('fx_fb_zoom') + 4 * mg('fx_fb_kick') * fxKick, rot: 1.5 * mg('fx_fb_rot'), dx: 0.5 * mg('fx_fb_dx'), dy: 0.5 * mg('fx_fb_dy'),
      hue: Math.PI * mg('fx_fb_hue'), add: mg('fx_fb_blend') >= 0.5 };
    if (mg('fx_tr') >= 0.5) fx.tracer = { n: Math.round(mg('fx_tr_n')), gap: Math.round(mg('fx_tr_gap')), fade: mg('fx_tr_fade') };
    if (mg('fx_bl') >= 0.5) fx.bloom = { str: 2.5 * mg('fx_bl_str'), rad: mg('fx_bl_rad'), thr: 0.9 * mg('fx_bl_thr') };
    return fx;
  }
  /* ------------------------------------------------------------ the scope
     The stage's other mode (STAGE · MODE): an oscilloscope, one lane per channel shown and
     playing, stacked top to bottom in channel order in the room the panel leaves, sharing its height. Each lane is
     the channel's wave as the telemetry sends it (one period, phase-locked to the channel's first voice, scaled to
     its peak), drawn Cycles times across, in the channel's colour (COLOR's Palette and Cycle too), with a faint
     centre line and its name and note in a band along its top (HTML over the stage, so the FX leave them be).
     - Smoothing: each lane's wave (and its peak) eases toward the newest at a time constant of 0 .. 0.35 s
       (Smoothing squared), so the line does not jitter frame to frame; 0 draws every wave as it comes.
     - Level: Normalized, every wave as tall as Height makes it; True, by its peak `ws` against SC_TRUE (the
       peak is of the channel's output at its level, so the level is in it): loud channels big, quiet ones small,
       clipped at the lane's edges as a scope would.
     - A lane comes and goes over 0.12 s (eye, a channel added or stopped), so the others make room smoothly; a
       channel that falls silent flattens to its centre line (0.3 s); a muted one leaves as a hidden one does (eye or
       mute: the channel is off the stage in either view); Focus dims as on the form.
     - Legend (on by default): the names and notes, on each lane's top band (Stacked) or a key along the top
       (Overlaid); off, no band and no key, the waves take the room.
     - Drawn by Shells (shells.js, scopeScene) in the WebGL stage, so the stage FX (mirror, feedback, tracer,
       bloom) work on it as on the form: feedback is a phosphor's afterglow, bloom a glow of its own. */
  var SC = {}, SC_PAD = 28, SC_GAP = 8, SC_BAND = 18, SC_KEYW = 190, SC_TRUE = 0.35, scope = { x0: null, x1: null, ms: 0, lanes: 0, verts: 0, lab: null, labs: {} };
  for (var sc0 = 0; sc0 < SLOTS; sc0++) SC[sc0] = { wave: new Float32Array(128), ws: 0, w: 0, live: 0, dim: 1, fresh: true };
  function scopeOn() { return !!DEFS.v_stage && gval('v_stage') >= 0.5; }
  /** The stage, either mode: the form (drawStage) or the scope. */
  function paintStage(dt, LW, LH, freeW, cx, bh) {
    var on = scopeOn();
    bh = bh || 0;
    if (elStage.classList.contains('scopemode') !== on) elStage.classList.toggle('scopemode', on);
    if (on) drawLanes(dt, LW, LH, freeW, cx, bh);
    else { if (scope.lab) scope.lab.style.display = 'none'; drawStage(dt, LW, LH, freeW, cx, bh); }
  }
  function drawLanes(dt, LW, LH, freeW, cx, bh) {
    var t1 = performance.now(), sm = mg('sc_smooth'), a = sm < 0.005 ? 1 : ease(dt, 0.35 * sm * sm), cyc = Math.round(mg('sc_cyc'));
    var gain = mg('sc_gain'), tru = mg('sc_lvl') >= 0.5, lanes = [], tot = 0, s, i;
    // the room: the stage beside the panel, its edges sliding there when the panel moves
    var L = cx - freeW / 2 + SC_PAD, R = cx + freeW / 2 - SC_PAD, k = scope.x0 == null ? 1 : ease(dt, 0.12);
    scope.x0 = scope.x0 == null ? L : scope.x0 + (L - scope.x0) * k; scope.x1 = scope.x1 == null ? R : scope.x1 + (R - scope.x1) * k;
    for (s = 0; s < SLOTS; s++) {
      var c = CH[s], q = SC[s], on = sval(s, 'gen') > 0 && c.active && sval(s, 'show') >= 0.5 && sval(s, 'mute') < 0.5 && !c.muted; // muted or hidden: off
      q.w += ((on ? 1 : 0) - q.w) * ease(dt, 0.12);
      if (q.w < 0.002 && !on) { q.w = 0; q.fresh = true; continue; }
      var loud = c.amp > 0.003;
      q.live += ((loud ? 1 : 0) - q.live) * ease(dt, loud ? 0.03 : 0.3);
      q.dim += (MO[s].dim - q.dim) * ease(dt, 0.12);
      var b = q.fresh ? 1 : a; q.fresh = false; // a new lane starts at its wave, not from flat
      for (i = 0; i < 128; i++) q.wave[i] += (c.wave[i] - q.wave[i]) * b;
      q.ws += (c.ws - q.ws) * b;
      var amp = 0.85 * gain * q.live * (tru ? Math.min(1.5, q.ws / SC_TRUE) : 1);
      lanes.push({ slot: s, w: q.w, wave: q.wave, amp: amp, k: q.dim * Math.min(1, q.w * 1.5), hue: c.hue == null ? 90 : c.hue, sat: c.sat, lvl: MO[s].lvl });
      tot += q.w;
    }
    // COLOR's Palette and Cycle as on the form (by the lanes' levels)
    var cl = lanes.map(function (o) { return { amp: o.lvl, rgb: null }; });
    stageColors(cl, dt);
    var room = LH - bh - 2 * SC_PAD, y = SC_PAD, over = mg('sc_lay') >= 0.5, legend = mg('sc_key') >= 0.5; // above the pin bar
    if (over) {
      // Overlaid: the key along the top (rows of SC_KEYW), then one line for every wave, each faded in by its weight
      var per = Math.max(1, Math.floor((scope.x1 - scope.x0) / SC_KEYW)), key = lanes.length && legend ? Math.ceil(lanes.length / per) * 20 + 8 : 0;
      var otop = (SC_PAD + key) / LH, obot = (LH - bh - SC_PAD) / LH;
      for (i = 0; i < lanes.length; i++) {
        var ov = lanes[i];
        ov.top = otop; ov.bot = obot; ov.mid = (otop + obot) / 2; ov.half = (obot - otop) / 2; ov.hpx = room; ov.amp *= Math.min(1, ov.w);
        ov.lx = scope.x0 + 4 + (i % per) * SC_KEYW; ov.ltop = SC_PAD + Math.floor(i / per) * 20;
        ov.col = cl[i].rgb || Knobs.hslRgb(ov.hue, Math.min(100, ov.sat + 8), 60);
      }
    }
    for (i = 0; i < lanes.length && !over; i++) {
      // a band along the lane's top for its label, so no wave runs through the text (thin lanes go without both)
      var o = lanes[i], h = room * o.w / Math.max(1, tot), g = Math.min(SC_GAP, h * 0.15), band = h >= 30 && legend ? Math.min(SC_BAND, h * 0.2) : 0;
      o.ltop = y + g / 2; o.top = (y + g / 2 + band) / LH; o.bot = (y + h - g / 2) / LH; o.mid = (o.top + o.bot) / 2; o.half = (o.bot - o.top) / 2; o.hpx = h;
      o.col = cl[i].rgb || Knobs.hslRgb(o.hue, Math.min(100, o.sat + 8), 60); o.lx = scope.x0 + 4;
      y += h;
    }
    scopeLabels(legend ? lanes : [], LH);
    try {
      Shells.draw(elStage, null, { scope: { lanes: lanes, x0: scope.x0 / LW, x1: scope.x1 / LW, cycles: cyc, unit: 1 / LH, lw: mg('sc_lw'), glow: mg('sc_glow'), grid: mg('sc_grid') >= 0.5, overlay: over },
                                   dt: dt, offX: cx / LW * 2 - 1, offY: bh / LH, fx: stageFx() });
    } catch (e) { glStatus = 'scope error ' + e.message; }
    var si = Shells.scopeInfo();
    scope.lanes = lanes.length; scope.verts = si ? si.verts : 0;
    scope.ms = scope.ms * 0.95 + (performance.now() - t1) * 0.05;
  }
  /** Each lane's name and note (a drum's model) in its top left corner, in its colour; lanes too thin for them
      go without. */
  function scopeLabels(lanes, LH) {
    if (!scope.lab) { scope.lab = document.createElement('div'); scope.lab.id = 'scopelab'; elUI.insertBefore(scope.lab, elUI.firstChild); }
    scope.lab.style.display = '';
    var seen = {};
    for (var i = 0; i < lanes.length; i++) {
      var o = lanes[i], s = o.slot, el = scope.labs[s], c = CH[s];
      if (!el) { el = scope.labs[s] = document.createElement('div'); el.className = 'sclab'; el.innerHTML = '<b></b><i></i>'; scope.lab.appendChild(el); }
      seen[s] = 1;
      var nm = slotName(s), nt = isDrum(s) ? nameOf(sid(s, 'drum_model')) : c.active ? Knobs.NOTES[c.pc] + Math.floor(c.note / 12 - 1) : '';
      if (el.firstChild.textContent !== nm) el.firstChild.textContent = nm;
      if (el.lastChild.textContent !== nt) el.lastChild.textContent = nt;
      var col = 'rgb(' + o.col.map(function (x) { return Math.round(255 * x); }).join(', ') + ')';
      if (el.style.color !== col) el.style.color = col;
      el.style.left = o.lx + 'px'; el.style.top = (o.ltop + 1) + 'px';
      el.style.opacity = o.hpx < 30 ? 0 : (0.85 * o.k).toFixed(3);
    }
    for (var k in scope.labs) if (!seen[k]) { scope.lab.removeChild(scope.labs[k]); delete scope.labs[k]; }
  }
  function scopeStats() { return scopeOn() ? '\nscope ' + scope.lanes + ' lanes, ' + scope.verts + ' vertices, ' + scope.ms.toFixed(2) + ' ms a frame (the page: waves, layout, labels, draw calls)' : ''; }

  /* dev.html only (fake-engine.js sets AMMO_DEV_RUN from the URL): warm=N draws N frames of 1/60 s before the first
     real one (headless shots run few frames, so trails would not build up); bench=N then times N more, each
     timed on the GPU (in a real-time headless run: read the title), with the CPU time to build them, into the
     stats overlay. */
  var DEVRUN = window.AMMO_DEV_RUN || null;
  var stageArgs = null; // the layout the last frame drew the stage in
  if (DEVRUN) var devTimer = setInterval(function () { // a timer: headless runs rAF only now and then
    if (DEVRUN.done) clearInterval(devTimer); else if (stageArgs && performance.now() - t0 > 1500) devRun.apply(null, stageArgs);
  }, 100);
  function devRun(LW, LH, freeW, cx, bh, again) {
    if (!ORDER[VFXBASE]) return;
    DEVRUN.done = true; DEVRUN.size = window.innerWidth + 'x' + window.innerHeight; // the screenshot resizes the page: warm again then
    var k, h = 1 / 60;
    function step() { T += h; var now = performance.now(); sampleTelemetry(now, h); pageMod(); motion(h, now); camera(h); paintStage(h, LW, LH, freeW, cx, bh); }
    for (k = 0; k < (DEVRUN.warm || 0); k++) step();
    if (!DEVRUN.bench || again) return;
    // GPU time per frame, a timer query around each draw (they resolve only in a real-time run, not on virtual time)
    var qs = [], got = [], t1 = performance.now();
    for (k = 0; k < DEVRUN.bench; k++) { var q = Shells.gpuBegin(); step(); Shells.gpuEnd(); if (q) qs.push(q); }
    var cpu = (performance.now() - t1) / DEVRUN.bench; // the page's side: building and submitting a frame
    Shells.sync(); // and with the GPU done too: an upper bound per frame that needs no timer query (headless runs on virtual time)
    var wall = (performance.now() - t1) / DEVRUN.bench;
    document.title = 'stage wall ' + wall.toFixed(2) + ' ms/frame (GPU synced), cpu ' + cpu.toFixed(2) + ' ms';
    var fi = Shells.fxInfo() || {}, tail = ' (' + glStatus + ')  fx ' + (fi.kind || '-') + ' ' + (fi.mb || 0).toFixed(1) + ' MB, ' + (fi.passes || 0) + ' passes  point max ' +
      (fi.ptMax || 0) + '  canvas ' + elStage.width + 'x' + elStage.height;
    function done(s) { DEVRUN.result = s + tail; document.title = DEVRUN.result; elStats.style.display = 'block'; }
    if (!qs.length) { done('stage: no timer queries, wall ' + wall.toFixed(2) + ' ms/frame (GPU synced), cpu ' + cpu.toFixed(2) + ' ms'); return; }
    var poll = setInterval(function () {
      for (var i = qs.length - 1; i >= 0; i--) { var r = Shells.gpuResult(qs[i]); if (r !== -1) { if (r >= 0) got.push(r); qs.splice(i, 1); } }
      if (qs.length) { document.title = 'stage wall ' + wall.toFixed(2) + ' ms/frame (GPU synced), cpu ' + cpu.toFixed(2) + ' ms, ' + qs.length + ' timer queries pending'; return; }
      clearInterval(poll); got.sort(function (a, b) { return a - b; });
      done('stage GPU ' + (got.reduce(function (a, b) { return a + b; }, 0) / Math.max(1, got.length)).toFixed(2) + ' ms/frame mean, median ' + (got[got.length >> 1] || 0).toFixed(2) + ' over ' + got.length + ', cpu ' + cpu.toFixed(2) + ' ms');
    }, 50);
  }

  /* ------------------------------------------------------------ painting */
  var frames = [], last = 0, lastSize = '';
  function frame(now) {
    var dt = last ? Math.min(0.1, (now - last) / 1000) : 0.016; last = now; T = (now - t0) / 1000;
    frames.push({ t: now, dt: dt * 1000 }); if (frames.length > 1200) frames.shift();
    sampleTelemetry(now, dt);
    if (!MINI) { pageMod(); motion(dt, now); camera(dt); }
    // the layout: 1440 high (Mini 900), as wide as the window, scaled to it
    var S = uiScale(), LW = window.innerWidth / S, LH = window.innerHeight / S, size = LW.toFixed(1) + 'x' + LH.toFixed(1) + '@' + S.toFixed(4);
    if (size !== lastSize) {
      lastSize = size;
      elUI.style.transform = 'scale(' + S + ')'; elUI.style.width = LW + 'px'; elUI.style.height = LH + 'px';
    }
    if (structure && !drag) build(); // not under a drag (an assign adds a route): the control dragged stays in place
    else if (dirty || now - lastRefresh > 250) refresh();

    if (MINI) { // no stage: the drawer under the panel, the toast above it (centred by app.css)
      var dh = placeDrawer();
      if (elToast) {
        elToast.style.bottom = (32 + dh) + 'px';
        if (toastUntil && now > toastUntil) { elToast.classList.remove('show'); toastUntil = 0; }
      }
    } else {
      var taken = (ui.rail ? RAIL_W : PANEL_W) + 2 * M, freeW = LW - taken, cx = ui.dock === 'left' ? taken + freeW / 2 : freeW / 2;
      var bh = placeBar(LW, LH, taken, dt);
      stageArgs = [LW, LH, freeW, cx, bh];
      if (DEVRUN && DEVRUN.done && DEVRUN.warm && DEVRUN.size !== window.innerWidth + 'x' + window.innerHeight) devRun(LW, LH, freeW, cx, bh, true);
      paintStage(dt, LW, LH, freeW, cx, bh);
      if (elToast) {
        elToast.style.left = cx + 'px'; elToast.style.bottom = (32 + bh) + 'px';
        if (toastUntil && now > toastUntil) { elToast.classList.remove('show'); toastUntil = 0; }
      }
      elStats.style.left = ui.dock === 'left' ? (taken + M) * S + 'px' : 'auto';
      elStats.style.right = ui.dock === 'right' ? (taken + M) * S + 'px' : 'auto';
    }

    // knobs
    var knobs = document.querySelectorAll('canvas.knob'), aTone = assign ? srcTone(assign.src) : null;
    // a tag dragged (or the route an assigning click made, dragged from the knob): its target's value line reads it out
    var dragRt = drag && drag.tag ? routeOf(drag.tag.r) : drag && drag.assign ? routeOf(drag.assign.r) : null;
    for (var i = 0; i < knobs.length; i++) {
      var cv = knobs[i], id = +cv.getAttribute('data-id'), d = ORDER[id]; if (!d) continue;
      var spec = specOf(id), slot = d.slot, ch = slot >= 0 ? CH[slot] : null;
      var st = { v: val(id), hue: ch && ch.hue != null ? ch.hue : Knobs.hueOf(F.cr), sat: ch ? ch.sat : 70, amp: ch ? Math.min(1, ch.amp) : 0.5,
                 dragging: drag && drag.cv === cv, hover: hover === cv, time: T, beat: F.beat, step: ch ? ch.step : null, len: ch ? ch.len : null,
                 wave: (spec.kind === 'wave' && ch && ch.active) ? (d.s === 'v_detail' ? MO[slot].out : ch.wave) : null,
                 ch: slot >= 0 ? { cutoff: sval(slot, 'cutoff'), reso: sval(slot, 'reso'), type: sval(slot, 'filter_type') } : null, scale: S,
                 mod: modOf(id), assign: assignState(id), assignTone: aTone };
      if (dragRt && dragRt.target === id && (drag.tag || drag.cv === cv)) { st.text = routeLine(dragRt); st.textCol = srcColor(dragRt.src); }
      if (d.s === 'root') st.hue = Knobs.hueOf(Math.round(st.v));
      var pm = /^(?:lfo(\d+)_|macro(\d+)$)/.exec(d.s); // an LFO's or a macro's own knobs: in its colour
      if (pm) { var pt = srcTone(pm[1] ? SRC_LFO1 + +pm[1] - 1 : SRC_M1 + +pm[2] - 1); st.hue = pt.h; st.sat = pt.s; }
      Knobs.draw(cv, spec, st);
    }
    document.querySelectorAll('canvas.dot, canvas.bar').forEach(function (dv) {
      var sl = +dv.getAttribute('data-slot'), cc = CH[sl], mutedC = sval(sl, 'mute') >= 0.5 || cc.muted || !cc.active, hh = cc.hue == null ? 90 : cc.hue;
      if (dv.classList.contains('bar')) Knobs.chBar(dv, hh, cc.sat, Math.min(1, cc.amp), mutedC); else Knobs.chDot(dv, hh, cc.sat, Math.min(1, cc.amp), mutedC);
    });
    document.querySelectorAll('[data-note]').forEach(function (el) {
      var sl = +el.getAttribute('data-note'), cc = CH[sl], s2 = cc.active ? Knobs.NOTES[cc.pc] + Math.floor(cc.note / 12 - 1) : '';
      if (el.textContent !== s2) el.textContent = s2;
    });
    var VIEWS = { drumview: drawDrum, chordview: drawChord, scope: drawScope, stepview: drawSteps, rollview: drawRoll, waveview: drawWave, filtview: drawFilter, envview: drawEnv, motionview: drawMotion,
                 driveview: drawDriveView, chorusview: drawChorusView, delayview: drawDelayView, duckview: drawDuckView, revview: drawSpaceView, spaceview: drawSpaceView, masterview: drawMasterView,
                 lfoview: drawLfo };
    for (var vk in VIEWS) document.querySelectorAll('canvas.' + vk).forEach(function (vc) { VIEWS[vk](vc, +vc.getAttribute('data-slot')); });
    document.querySelectorAll('canvas.hfader').forEach(drawFader);
    var wheel = document.querySelector('canvas.wheel'); if (wheel) drawWheel(wheel);
    var pg = document.querySelector('canvas.prog'); if (pg) drawProg(pg);
    document.querySelectorAll('canvas.partview').forEach(function (pv) { drawPartView(pv, +pv.getAttribute('data-slot')); });
    if (elStats.style.display === 'block') stats_(now);
    requestAnimationFrame(frame);
  }
  function stats_(now) {
    var n = 0, slow = 0, mx = 0, i;
    for (i = frames.length - 1; i >= 0 && now - frames[i].t <= 10000; i--) { n++; if (frames[i].dt > 20) slow++; if (frames[i].dt > mx) mx = frames[i].dt; }
    var fps = n ? n / Math.min(10, (now - frames[Math.max(0, frames.length - n)].t + 1) / 1000) : 0, tk = 0;
    for (i = stats.tickT.length - 1; i >= 0 && now - stats.tickT[i] <= 10000; i--) tk++;
    elStats.textContent = (MINI ? 'Ammonite Mini #' : 'Ammonite Desktop #') + (INFO.inst || 0) + '  ' + (INFO.fmt || '') + '  ' + F.sr + ' Hz block ' + F.bl + '  window ' + window.innerWidth + 'x' + window.innerHeight + ' scale ' + uiScale().toFixed(2) + '\n' +
      'page ' + fps.toFixed(0) + ' fps (>20 ms: ' + slow + ', max ' + mx.toFixed(1) + ')  tick ' + stats.tickMs.toFixed(3) + ' ms\n' +
      'telemetry ' + (tk / 10).toFixed(0) + ' pushes/s, ' + (stats.frames / Math.max(1, stats.ticks)).toFixed(1) + ' frames each, smoothing ' + LAT.toFixed(0) + ' ms  host idle ' + F.idleMs.toFixed(1) + ' ms  eval ' + F.evalMs.toFixed(3) + ' ms\n' +
      (MINI ? '' : 'webgl ' + glStatus + scopeStats() + '\n') + 'beat ' + F.beat.toFixed(2) + '  ' + F.bpm.toFixed(1) + ' BPM ' + (F.playing ? 'playing' : 'stopped') + '  chord ' + chordName() + '  master rms ' + F.mr.toFixed(3) +
      fxStats();
  }
  function fxStats() {
    var fi = !MINI && Shells.fxInfo && Shells.fxInfo();
    return (fi ? '\nstage fx ' + fi.kind + ' targets, ' + fi.mb.toFixed(1) + ' MB held, ' + fi.passes + ' passes' : '') + (DEVRUN && DEVRUN.result ? '\n' + DEVRUN.result : '');
  }

  /** Mini: hover looks only where the main pointer hovers (a mouse), so a tap leaves nothing lit. Every top-level
      rule of app.css with :hover is put inside @media (hover: hover), in its place (the same order and specificity),
      rather than app.css keeping a second copy of each for Mini. Desktop's sheet is never touched. */
  function hoverOnlyWithMouse() {
    for (var i = 0; i < document.styleSheets.length; i++) {
      var sh = document.styleSheets[i], rules;
      try { rules = sh.cssRules; } catch (e) { continue; } // another origin's: not ours
      if (!rules || !/app\.css(\?|$)/.test(sh.href || '')) continue;
      for (var j = rules.length - 1; j >= 0; j--) {
        var r = rules[j];
        if (r.type !== 1 || r.selectorText.indexOf(':hover') < 0) continue;
        var css = r.cssText;
        sh.deleteRule(j); sh.insertRule('@media (hover: hover) { ' + css + ' }', j);
      }
    }
  }

  /* -------------------------------------------------------------- start */
  if (MINI) { glStatus = 'none (Mini)'; hoverOnlyWithMouse(); }
  else {
    glStatus = Shells.init(elStage);
    send('gl ' + glStatus);
    if (glStatus.indexOf('ok') !== 0) { var f = $('fail'); f.style.display = 'block'; f.textContent = 'WebGL unavailable: ' + glStatus; }
  }
  requestAnimationFrame(frame);
  send('ready');
})();
