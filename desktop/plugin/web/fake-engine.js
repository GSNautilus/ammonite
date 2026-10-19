/* Ammonite Desktop: a stand-in engine for working on the page in a plain
   browser (dev.html). Not bundled into the plugin.

   It plays the part of AmmoniteUI.cpp: it installs a fake WebView bridge
   before app.js loads, answers 'ready' with the parameter table (the same
   ids, symbols, ranges and names as engine/Params.hpp), and pushes telemetry
   frames shaped like the real ones: beat, chord, key and chord masks, per
   channel the note, envelope, hits, step and one cycle of the wave, and the
   modulation pool's LFOs and live routes.

   The music is a cartoon of the engine's: arps step through the chord on
   their division, length and density; envelopes rise and fall; the wave's
   shape follows Shape and Cutoff. Enough for layout and motion, not for
   tuning how anything sounds. Space starts and stops.

   Ammonite Mini's dev.html loads it too (window.AMMO_MINI set first): the same, its page state under 'ammo.mini.ui'. */
(function () {
  'use strict';
  if ((window.chrome && window.chrome.webview) || window.ammoHost) return; // the real host is here (desktop's, or Ammonite Mini's bridge)
  window.AMMO_DEV = true;

  /* ---------------------------------------------- the table (Params.hpp) */
  var SLOTS = 16, SLOT_BASE = 64, STRIDE = 128;
  var N = {
    sync: ['Host', 'Internal'], note: ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'],
    scale: ['Major', 'Minor', 'Dorian', 'Phrygian', 'Lydian', 'Mixolydian', 'Harmonic minor', 'Pentatonic', 'Pentatonic minor', 'Blues',
            'Maj', 'Min', 'Sus2', 'Sus4', 'Maj7', 'Min7', 'Dom7', 'Dim', 'Aug', 'Add9'],
    prog: ['Off', 'Pop I V vi IV', '50s I vi IV V', 'Sad vi IV I V', 'Epic i VI III VII', 'Jazz ii V I I', 'Vamp I IV', 'Canon', 'Fall i VII VI v', 'Custom'],
    change: ['1/2 bar', '1 bar', '2 bars', '4 bars'], roman: ['I', 'II', 'III', 'IV', 'V', 'VI', 'VII'], restart: ['Free', 'Bar', 'Chord'],
    gen: ['Off', 'Arp', 'Chord', 'Bass', 'Rhythm'], rpitch: ['Fixed', 'Root', 'Fifth'],
    drum: ['Kick', 'Snare', 'Hat', 'Open hat', 'Clap', 'Tom', 'Rim', 'Noise'], cnotes: ['Triad', '7th', '9th'], voicing: ['Close', 'Open', 'Drop 2', 'Spread'],
    lead: ['Fixed', 'Smooth'], crhythm: ['Hold', 'Steps', 'Arp'], chomode: ['Chorus', 'Ensemble', 'Vibrato'], bass: ['Root', 'Root-5th', 'Octaves', 'Walk'], inst: ['Synth', 'Drum'], mode: ['Off', 'Loop', 'Up', 'Down', 'Up-down', 'Random'], pool: ['Key', 'Triad', '7th', 'Fifths', 'Root'],
    div: ['4/1', '2/1', '1/1', '1/2.', '1/2', '1/4.', '1/4', '1/4T', '1/8.', '1/8', '1/8T', '1/16', '1/16T', '1/32'], offon: ['Off', 'On'],
    ddiv: ['1/32', '1/16', '1/16T', '1/8', '1/8T', '1/8.', '1/4', '1/4T', '1/4.', '1/2', '1/1'],
    duck: ['Off', '1', '2', '3', '4', '5', '6', '7', '8', '9', '10', '11', '12', '13', '14', '15', '16'],
    lshape: ['Sine', 'Triangle', 'Saw up', 'Saw down', 'Square', 'Random', 'Drift'], lsync: ['Free', 'Tempo'],
    ldiv: ['16/1', '8/1', '4/1', '2/1', '1/1', '1/2.', '1/2', '1/4.', '1/4', '1/4T', '1/8.', '1/8', '1/8T', '1/16', '1/16T', '1/32'],
    lrestart: ['Free'].concat([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16].map(function (n) { return 'Notes ' + n; })),
    pol: ['Unipolar', 'Bipolar']
  };
  // what a route can move (engine/Params.hpp IsModTarget): a channel's continuous parameters, and these globals
  var DEST_SYMS = ['level', 'pan', 'tune', 'shape', 'detune', 'sub', 'noise', 'drive', 'glide', 'cutoff', 'reso', 'filter_type', 'filt_amt',
                   'filt_a', 'filt_d', 'amp_a', 'amp_d', 'amp_s', 'amp_r', 'pitch_amt', 'pitch_d', 'arp_gate', 'arp_density', 'arp_vary', 'arp_ratchet',
                   'chord_strum', 'rhy_chance', 'drum_tune', 'drum_decay', 'drum_tone', 'drum_snap', 'fx_drive', 'fx_tone', 'cho_rate', 'cho_depth',
                   'cho_mix', 'dly_fb', 'dly_damp', 'dly_mix', 'rev_send', 'duck_amt'];
  var GLOBAL_TARGETS = ['swing', 'volume', 'rev_size', 'rev_decay', 'rev_damp', 'rev_pre', 'rev_width', 'rev_return', 'glue', 'ceiling'];
  // name, symbol, min, max, def, steps, names
  var GLOBAL = [
    ['Tempo', 'tempo', 20, 240, 110, 0], ['Sync', 'sync', 0, 1, 1, 2, N.sync], ['Swing', 'swing', 0, 1, 0, 0],
    ['Root', 'root', 0, 11, 9, 12, N.note], ['Scale', 'scale', 0, 19, 1, 20, N.scale], ['Progression', 'prog', 0, 9, 0, 10, N.prog],
    ['Chord change', 'change', 0, 3, 1, 4, N.change], ['Chord', 'chord', 0, 6, 0, 7, N.roman], ['Restart', 'restart', 0, 2, 0, 3, N.restart],
    ['Master octave', 'octave', -2, 2, 0, 5], ['Volume', 'volume', 0, 1, 0.8, 0],
    ['Macro 1', 'macro1', 0, 1, 0, 0], ['Macro 2', 'macro2', 0, 1, 0, 0], ['Macro 3', 'macro3', 0, 1, 0, 0], ['Macro 4', 'macro4', 0, 1, 0, 0],
    ['Reverb size', 'rev_size', 0, 1, 0.5, 0], ['Reverb decay', 'rev_decay', 0, 1, 0.45, 0], ['Reverb damping', 'rev_damp', 0, 1, 0.4, 0],
    ['Reverb pre-delay', 'rev_pre', 0, 1, 0.1, 0], ['Reverb width', 'rev_width', 0, 1, 1, 0], ['Reverb return', 'rev_return', 0, 1, 0.6, 0],
    ['Glue', 'glue', 0, 1, 0.3, 0], ['Ceiling', 'ceiling', -12, 0, -0.3, 0], ['Progression length', 'prog_len', 1, 8, 4, 8],
    ['Progression step 1', 'prog_s1', 0, 6, 0, 7, N.roman], ['Progression step 2', 'prog_s2', 0, 6, 3, 7, N.roman],
    ['Progression step 3', 'prog_s3', 0, 6, 4, 7, N.roman], ['Progression step 4', 'prog_s4', 0, 6, 0, 7, N.roman],
    ['Progression step 5', 'prog_s5', 0, 6, 0, 7, N.roman], ['Progression step 6', 'prog_s6', 0, 6, 0, 7, N.roman],
    ['Progression step 7', 'prog_s7', 0, 6, 0, 7, N.roman], ['Progression step 8', 'prog_s8', 0, 6, 0, 7, N.roman]
  ];
  var SLOTDEF = [
    ['Generator', 'gen', 0, 4, 0, 5, N.gen], ['Instrument', 'inst', 0, 1, 0, 2, N.inst], ['Level', 'level', 0, 1, 0.7, 0], ['Pan', 'pan', -1, 1, 0, 0],
    ['Mute', 'mute', 0, 1, 0, 2, N.offon], ['Show', 'show', 0, 1, 1, 2, N.offon], ['Degree', 'degree', 0, 7, 0, 8], ['Octave', 'oct', -4, 4, 0, 9],
    ['Arp mode', 'arp_mode', 0, 5, 2, 6, N.mode], ['Arp pool', 'arp_pool', 0, 4, 3, 5, N.pool], ['Arp range', 'arp_range', 1, 3, 1, 3],
    ['Arp division', 'arp_div', 0, 13, 6, 14, N.div], ['Arp length', 'arp_len', 1, 16, 8, 16], ['Arp gate', 'arp_gate', 0, 1, 0.5, 0],
    ['Arp density', 'arp_density', 0, 1, 1, 0], ['Arp vary', 'arp_vary', 0, 1, 0, 0], ['Arp ratchet', 'arp_ratchet', 0, 1, 0, 0],
    ['Shape', 'shape', 0, 1, 0.3, 0], ['Detune', 'detune', 0, 50, 6, 0], ['Sub', 'sub', 0, 1, 0, 0], ['Noise', 'noise', 0, 1, 0, 0],
    ['Cutoff', 'cutoff', 0, 1, 0.8, 0], ['Resonance', 'reso', 0, 1, 0.2, 0], ['Filter type', 'filter_type', 0, 1, 0, 0], ['Drive', 'drive', 0, 1, 0, 0],
    ['Amp attack', 'amp_a', 0, 1, 0.1, 0], ['Amp decay', 'amp_d', 0, 1, 0.45, 0], ['Amp sustain', 'amp_s', 0, 1, 0.3, 0], ['Amp release', 'amp_r', 0, 1, 0.45, 0],
    ['Filter env amount', 'filt_amt', -1, 1, 0, 0], ['Filter attack', 'filt_a', 0, 1, 0, 0], ['Filter decay', 'filt_d', 0, 1, 0.4, 0],
    ['Pitch env amount', 'pitch_amt', -1, 1, 0, 0], ['Pitch decay', 'pitch_d', 0, 1, 0.3, 0], ['Glide', 'glide', 0, 1, 0, 0],
    ['Chord notes', 'chord_notes', 0, 2, 0, 3, N.cnotes], ['Chord voicing', 'chord_voicing', 0, 3, 0, 4, N.voicing],
    ['Voice leading', 'chord_lead', 0, 1, 1, 2, N.lead], ['Chord rhythm', 'chord_rhythm', 0, 2, 0, 3, N.crhythm],
    ['Strum', 'chord_strum', 0, 1, 0, 0], ['Bass pattern', 'bass_pattern', 0, 3, 0, 4, N.bass],
    ['Rotate', 'rhy_rotate', 0, 15, 0, 16], ['Chance', 'rhy_chance', 0, 1, 1, 0], ['Rhythm pitch', 'rhy_pitch', 0, 2, 0, 3, N.rpitch],
    ['Drum', 'drum_model', 0, 7, 0, 8, N.drum], ['Drum tune', 'drum_tune', -12, 12, 0, 0], ['Drum decay', 'drum_decay', 0, 1, 0.5, 0],
    ['Drum tone', 'drum_tone', 0, 1, 0.5, 0], ['Drum snap', 'drum_snap', 0, 1, 0.5, 0],
    ['FX drive', 'fx_drive', 0, 1, 0, 0], ['FX drive tone', 'fx_tone', -1, 1, 0, 0], ['Chorus', 'cho_on', 0, 1, 0, 2, N.offon],
    ['Chorus rate', 'cho_rate', 0, 1, 0.4, 0], ['Chorus depth', 'cho_depth', 0, 1, 0.5, 0], ['Chorus mix', 'cho_mix', 0, 1, 0.5, 0],
    ['Delay division', 'dly_div', 0, 10, 3, 11, N.ddiv], ['Delay feedback', 'dly_fb', 0, 0.95, 0.4, 0], ['Ping-pong', 'dly_pp', 0, 1, 0, 2, N.offon],
    ['Delay damping', 'dly_damp', 0, 1, 0.3, 0], ['Delay mix', 'dly_mix', 0, 1, 0, 0],
    ['Reverb send', 'rev_send', 0, 1, 0, 0], ['Duck source', 'duck_src', 0, 16, 0, 17, N.duck], ['Duck amount', 'duck_amt', 0, 1, 0.5, 0],
    ['Chorus mode', 'cho_mode', 0, 2, 0, 3, N.chomode], ['Let ring', 'chord_ring', 0, 1, 0, 2, N.offon],
    ['Fine tune', 'tune', -100, 100, 0, 0]
  ];
  // the modulation pool (ids from 2112): per LFO in use and six settings (a spare id after), the macros' in-use flags,
  // macros 5..16, the routes (source 0 off / 1..32 an LFO / 33..48 a macro, target id, amount, polarity)
  var POOL_LFOS = 32, POOL_MACROS = 16, POOL_ROUTES = 128, LFO_BASE = 2112, MON_BASE = 2368, MVAL_BASE = 2384, ROUTE_BASE = 2400, SRC_M1 = 33;
  var SRCN = ['Off']; for (var sn = 1; sn <= 48; sn++) SRCN.push(sn < SRC_M1 ? 'LFO ' + sn : 'Macro ' + (sn - SRC_M1 + 1));
  var TABLE = [], DEF = {}, G = {}, SP = {};
  function add(id, d, slot) {
    var o = { id: id, n: d[0], s: d[1], slot: slot, min: d[2], max: d[3], def: d[4], steps: d[5], flags: 1 };
    if (d[6]) o.names = d[6];
    TABLE.push(o); DEF[id] = o;
  }
  GLOBAL.forEach(function (d, i) { add(i, d, -1); G[d[1]] = i; });
  // which notes a pattern plays (the engine's slot indices 90..92, past the retired 66..89)
  var NOTEDEF = [[90, ['Shift', 'note_shift', 0, 15, 0, 16]], [91, ['Count', 'note_count', 0, 2, 0, 3, ['Auto', 'Steps', 'Hits']]],
                 [92, ['Seed', 'seed', 0, 64, 0, 65, ['Free'].concat(Array.from({ length: 64 }, function (_, i) { return String(i + 1); }))]]];
  for (var s = 0; s < SLOTS; s++) {
    SLOTDEF.forEach(function (d, p) { add(SLOT_BASE + STRIDE * s + p, d, s); if (!s) SP[d[1]] = p; });
    NOTEDEF.forEach(function (e) { add(SLOT_BASE + STRIDE * s + e[0], e[1], s); if (!s) SP[e[1][1]] = e[0]; });
  }
  for (var l0 = 0; l0 < POOL_LFOS; l0++) {
    var n0 = 'LFO ' + (l0 + 1) + ' ', s0 = 'lfo' + (l0 + 1) + '_';
    [[n0 + 'in use', s0 + 'on', 0, 1, 0, 2, N.offon], [n0 + 'shape', s0 + 'shape', 0, 6, 0, 7, N.lshape], [n0 + 'sync', s0 + 'sync', 0, 1, 1, 2, N.lsync],
     [n0 + 'rate', s0 + 'rate', 0, 1, 0.5, 0], [n0 + 'division', s0 + 'div', 0, 15, 4, 16, N.ldiv], [n0 + 'phase', s0 + 'phase', 0, 1, 0, 0],
     [n0 + 'restart', s0 + 'restart', 0, 16, 0, 17, N.lrestart]].forEach(function (d, k) { add(LFO_BASE + 8 * l0 + k, d, -1); G[d[1]] = LFO_BASE + 8 * l0 + k; });
  }
  for (var m0 = 0; m0 < POOL_MACROS; m0++) {
    add(MON_BASE + m0, ['Macro ' + (m0 + 1) + ' in use', 'macro' + (m0 + 1) + '_on', 0, 1, 0, 2, N.offon], -1); G['macro' + (m0 + 1) + '_on'] = MON_BASE + m0;
    if (m0 >= 4) { add(MVAL_BASE + m0, ['Macro ' + (m0 + 1), 'macro' + (m0 + 1), 0, 1, 0, 0], -1); G['macro' + (m0 + 1)] = MVAL_BASE + m0; }
  }
  for (var r0 = 0; r0 < POOL_ROUTES; r0++) {
    var rn = 'Route ' + (r0 + 1) + ' ', rs = 'route' + (r0 + 1) + '_';
    [[rn + 'source', rs + 'src', 0, 48, 0, 49, SRCN], [rn + 'target', rs + 'target', -1, 5119, -1, 5121], [rn + 'amount', rs + 'amt', -1, 1, 0, 0],
     [rn + 'polarity', rs + 'pol', 0, 1, 1, 2, N.pol]].forEach(function (d, k) { add(ROUTE_BASE + 4 * r0 + k, d, -1); });
  }
  function sid(slot, sym) { return SLOT_BASE + STRIDE * slot + SP[sym]; }
  function lid(l, k) { return G['lfo' + (l + 1) + '_' + k]; }
  function rid(r, k) { return ROUTE_BASE + 4 * r + ['src', 'target', 'amt', 'pol'].indexOf(k); }
  function macroVal(m) { return V[G['macro' + (m + 1)]]; }

  /* ---------------------------------------------------------- presets */
  var V = {};
  function ch(slot, o) { var out = {}; out[sid(slot, 'gen')] = 1; for (var k in o) out[sid(slot, k)] = o[k]; return out; }
  function merge(list) { var out = {}; list.forEach(function (o) { for (var k in o) out[k] = o[k]; }); return out; }
  var FIRST = [
    ch(0, { arp_mode: 2, arp_pool: 3, arp_div: 6, arp_len: 8, level: 0.8, shape: 0.667, amp_s: 0.4, amp_d: 0.656, amp_r: 0.656 }),
    ch(1, { arp_mode: 4, arp_pool: 1, arp_range: 2, arp_div: 9, arp_len: 8, oct: 1, degree: 2, level: 0.7, shape: 1, amp_s: 0, amp_d: 0.585, pan: -0.4 }),
    ch(2, { arp_mode: 5, arp_pool: 0, arp_range: 2, arp_div: 11, arp_len: 6, arp_density: 0.6, oct: 2, degree: 4, level: 0.6, shape: 0.2, amp_s: 0, amp_d: 0.536, pan: 0.4 })
  ];
  // the dev presets' effects: something in every view
  // the pool, for the modulation views: LFO 1 (a half-note sine) on the first arp's cutoff, LFO 2 (a triangle four bars
  // long) on the second's pan and on the reverb's size, Bloom (Macro 1) on all three cutoffs and Space (Macro 3) on
  // their sends; the first arp also has LFO 2 on its cutoff (three rings) and pan, LFO 1 unipolar on its sustain.
  // A route [source, target, amount, polarity (0 unipolar; bipolar if left out)].
  function poolPreset(lfos, macros, routes) {
    var o = {};
    lfos.forEach(function (kv, l) { o[lid(l, 'on')] = 1; for (var k in kv) o[lid(l, k)] = kv[k]; });
    macros.forEach(function (m) { o[G['macro' + m + '_on']] = 1; });
    routes.forEach(function (rt, r) { o[rid(r, 'src')] = rt[0]; o[rid(r, 'target')] = rt[1]; o[rid(r, 'amt')] = rt[2]; if (rt[3] != null) o[rid(r, 'pol')] = rt[3]; });
    return o;
  }
  var MODS = [poolPreset([{ div: 6 }, { shape: 1, div: 2 }], [1, 3],
                   [[1, sid(0, 'cutoff'), 0.15], [2, sid(1, 'pan'), 0.3], [2, G.rev_size, 0.15], [33, sid(0, 'cutoff'), 0.3], [33, sid(1, 'cutoff'), 0.3],
                    [33, sid(2, 'cutoff'), 0.3], [35, sid(0, 'rev_send'), 0.45], [35, sid(1, 'rev_send'), 0.45], [35, sid(2, 'rev_send'), 0.45],
                    [2, sid(0, 'cutoff'), 0.2], [2, sid(0, 'pan'), 0.35], [1, sid(0, 'amp_s'), 0.4, 0]])];
  var FX = [
    ch(0, { fx_drive: 0.35, fx_tone: -0.3, dly_div: 5, dly_fb: 0.55, dly_damp: 0.5, dly_mix: 0.4, dly_pp: 1 }),
    ch(1, { cho_on: 1, cho_rate: 0.45, cho_depth: 0.6, cho_mix: 0.5, rev_send: 0.35 })
  ];
  function drum(slot, model, div, len, density, o) {
    var out = ch(slot, { gen: 4, inst: 1, drum_model: model, arp_div: div, arp_len: len, arp_density: density, oct: 0 });
    for (var k in o) out[sid(slot, k)] = o[k];
    return out;
  }
  var GROOVE = [
    drum(0, 0, 6, 4, 1, { drum_decay: 0.45, level: 0.9 }), drum(1, 4, 6, 4, 0.5, { rhy_rotate: 1, level: 0.7 }),
    drum(2, 2, 11, 16, 1, { rhy_chance: 0.85, arp_vary: 0.5, level: 0.5, pan: 0.25 }), drum(3, 3, 9, 8, 0.25, { rhy_rotate: 1, drum_decay: 0.35, level: 0.45 }),
    ch(4, { gen: 3, bass_pattern: 1, arp_div: 9, arp_len: 8, arp_density: 0.75, oct: 0, shape: 0.667, cutoff: 0.35, level: 0.75, duck_src: 1, duck_amt: 0.6 }),
    ch(5, { gen: 2, chord_notes: 1, chord_voicing: 2, chord_rhythm: 1, arp_div: 9, arp_len: 8, arp_density: 0.375, arp_gate: 0.2, level: 0.5, duck_src: 1, duck_amt: 0.7, rev_send: 0.4 })
  ];
  var BAND = [
    ch(0, { gen: 2, chord_notes: 1, chord_voicing: 1, chord_lead: 1, chord_rhythm: 0, chord_strum: 0.3, level: 0.6, shape: 0.5, cutoff: 0.55, amp_a: 0.6, amp_s: 0.8, amp_r: 0.8 }),
    ch(1, { gen: 3, bass_pattern: 3, arp_div: 6, arp_len: 4, level: 0.8, shape: 0.15, cutoff: 0.45, amp_s: 0.6, oct: 0 }),
    ch(2, { arp_mode: 4, arp_pool: 1, arp_range: 2, arp_div: 9, arp_len: 8, oct: 2, level: 0.55, shape: 0.2, amp_s: 0, amp_d: 0.536 })
  ];
  var BLOOM = [ // the engine's Slow bloom: an arpeggiated chord left ringing, Ensemble, a long echo
    ch(0, { gen: 2, chord_notes: 1, chord_voicing: 1, chord_lead: 1, chord_rhythm: 2, chord_ring: 1, arp_mode: 2, arp_div: 9, arp_len: 8, arp_density: 0.75,
            shape: 0.4, cutoff: 0.5, amp_a: 0.36, amp_s: 0.6, amp_d: 0.9, amp_r: 0.9, level: 0.7, cho_on: 1, cho_mode: 1, cho_rate: 0.3, cho_depth: 0.7,
            dly_div: 9, dly_fb: 0.55, dly_damp: 0.5, dly_pp: 1, dly_mix: 0.35, rev_send: 0.4 }),
    ch(1, { arp_mode: 1, arp_pool: 4, arp_div: 2, arp_len: 1, arp_gate: 1, oct: -1, shape: 0, amp_s: 1, amp_a: 0.83, level: 0.5, rev_send: 0.25 }),
    ch(2, { arp_mode: 5, arp_pool: 2, arp_range: 2, arp_div: 9, arp_len: 8, arp_density: 0.25, oct: 2, shape: 0, amp_s: 0, amp_d: 0.78, level: 0.45, pan: 0.3,
            dly_div: 8, dly_fb: 0.7, dly_mix: 0.5, dly_pp: 1, rev_send: 0.5, cho_on: 1, cho_mode: 2, cho_rate: 0.75, cho_depth: 0.25 }) // (a vibrato, for the view)
  ];
  function crowd(n) {
    var list = [];
    for (var i = 0; i < n; i++) list.push(ch(i, { arp_mode: 1 + (i % 5), arp_pool: i % 5, arp_div: [6, 9, 11, 9, 10, 11, 6, 12, 7, 11, 9, 13, 4, 11, 9, 2][i],
      arp_len: 3 + ((i * 5) % 13), arp_density: 0.45 + 0.55 * ((i * 7) % 10) / 10, oct: (i % 5) - 1, degree: (i * 3) % 8, shape: ((i * 37) % 100) / 100,
      cutoff: 0.45 + ((i * 13) % 50) / 100, level: 0.45 + ((i * 11) % 40) / 100, pan: (((i * 29) % 21) - 10) / 10, amp_s: (i % 3) * 0.2, amp_d: 0.35 + ((i * 17) % 50) / 100 }));
    return list;
  }
  var PRESETS = [
    { n: 'Init', b: 'Every slot off: a blank page.', v: {} },
    { n: 'First light', b: 'The three default arps in A minor: a saw pluck, a square an octave up, a soft sparkle.', v: merge(FIRST.concat(FX).concat(MODS)) },
    { n: 'Band (dev)', b: 'A chord pad, a walking bass and an arp over the Pop progression.', v: merge(BAND.concat([(function () { var o = {}; o[G.prog] = 1; return o; })()])) },
    { n: 'Groove (dev)', b: 'Drums, a bass and chord stabs, as the engine\'s Night groove.', v: merge(GROOVE.concat([(function () { var o = {}; o[G.prog] = 4; o[G.tempo] = 118; return o; })()])) },
    { n: 'Slow bloom (dev)', b: 'A chord arpeggiated and left ringing at 36 BPM, an ensemble chorus, long echoes.',
      v: merge(BLOOM.concat([(function () { var o = {}; o[G.prog] = 8; o[G.tempo] = 36; o[G.root] = 2; o[G.scale] = 1; return o; })()])) },
    { n: 'Full house (dev)', b: 'Twelve channels at once: the layout with a full list.', v: merge(crowd(12).concat([(function () { var o = {}; o[G.prog] = 1; return o; })()])) },
    { n: 'Every slot (dev)', b: 'All sixteen slots on: past the twelve that fit without scrolling.', v: merge(crowd(16).concat([(function () { var o = {}; o[G.prog] = 3; o[G.tempo] = 96; return o; })()])) }
  ];
  var preset = 1;
  function applyPreset(k) {
    preset = k;
    TABLE.forEach(function (d) { V[d.id] = d.def; });
    var pv = PRESETS[k].v; for (var id in pv) V[id] = pv[id];
  }
  // dev.html?preset=2&tab=channels&dock=right&rail=1&open=0,2&secs=pat,filt: a state to open in (screenshots);
  // open: the channels open, secs: the sections open in each of them, grps: their groups open (gen,inst,fx,vg; with
  // secs= and no grps=, all four), single=0: many open (One at a time otherwise); view=nested|merged;
  // cam=front|above|side|orbit: the camera there from the first frame; size=0.5,1.5 / alpha=1,0.2: per channel
  // from slot 0 (one value: every channel). Page parameters, ids as app.js's VGLOBAL / VCHAN.
  var Q = new URLSearchParams(location.search), UI_KEY = window.AMMO_MINI ? 'ammo.mini.ui' : 'ammo.ui'; // Mini mode keeps its page state apart
  applyPreset(Q.has('preset') ? +Q.get('preset') : 1);
  // set=prog:9,prog_len:5,prog_s2:5: global parameters over the preset, by symbol
  if (Q.has('set')) Q.get('set').split(',').forEach(function (kv) { var p = kv.split(':'); if (G[p[0]] != null) V[G[p[0]]] = +p[1]; });
  // sset=0:arp_pool:1,0:note_shift:1: a channel's parameters over the preset (slot from 0, symbol, value)
  if (Q.has('sset')) Q.get('sset').split(',').forEach(function (kv) { var p = kv.split(':'); if (SP[p[1]] != null) V[sid(+p[0], p[1])] = +p[2]; });
  // route=1:4101:0.5,33:4866:-0.3:0: routes over the preset's, from route 101 (source 1..32 an LFO / 33..48 a macro, put in
  // use; target id, a page parameter's too; amount; polarity 0 unipolar, bipolar if left out)
  if (Q.has('route')) Q.get('route').split(',').forEach(function (x, i) {
    var p = x.split(':').map(Number), r = 100 + i;
    V[rid(r, 'src')] = p[0]; V[rid(r, 'target')] = p[1]; V[rid(r, 'amt')] = p[2]; V[rid(r, 'pol')] = p.length > 3 ? p[3] : 1;
    V[p[0] >= SRC_M1 ? G['macro' + (p[0] - SRC_M1 + 1) + '_on'] : lid(p[0] - 1, 'on')] = 1;
  });
  try {
    var keep = JSON.parse(localStorage.getItem(UI_KEY) || '{}');
    ['tab', 'dock', 'view'].forEach(function (k) { if (Q.has(k)) keep[k] = Q.get(k); });
    var VB = 4096, CAMS = { front: [0, 0, 0], above: [90, 0, 0], side: [0, 0, 1], orbit: [17, 0.4, 0] }; // tilt, orbit, lie
    if (Q.has('cam') || Q.has('size') || Q.has('alpha')) keep.vp = keep.vp || {};
    if (CAMS[Q.get('cam')]) { var cv = CAMS[Q.get('cam')]; keep.vp[VB + 4] = cv[0]; keep.vp[VB + 7] = cv[1]; keep.vp[VB + 8] = 0; keep.vp[VB + 9] = cv[2]; }
    [['size', 4], ['alpha', 5]].forEach(function (o) {
      if (!Q.has(o[0])) return;
      var list = Q.get(o[0]).split(',').map(Number);
      for (var sl = 0; sl < SLOTS; sl++) keep.vp[VB + 16 + 8 * sl + o[1]] = list.length === 1 ? list[0] : list[sl] != null ? list[sl] : 1;
    });
    // stage FX and particles (ids as app.js's VFX from 4352, VPCHAN from 4608 in blocks of 8): with any URL option
    // they start at their defaults (off, Surface) unless given: fx=mirror,feedback,tracer switches them on;
    // mir=div,op,top / fb=amt,zoom,rot,dx,dy,hue,blend,kick / tr=n,gap,fade set their knobs (a list, any length);
    // mode=0|1|2 (Surface, Particles, Both), pdens= pscat= pjit= pcol= psize= psoft= pfill=: per channel as size= is;
    // col=mode,map,spread,level,speed (mode 0 Pitch, 1 Palette, 2 Cycle; map 0..6 as app.js's COLMAP_NAMES);
    // fx=...,bloom with bl=strength,radius,threshold.
    // scope=1,... the stage as a scope (below);
    // orbit=0..1 the camera's turn; look=shell,glow,ripple per channel; fxfmt=rgba8 the 8-bit fallback; warm=N draws N frames before the first shown (trails build up headless), bench=N times N more (the title).
    if (Q.toString()) {
      keep.vp = keep.vp || {};
      for (var vi = 4352; vi < 4352 + 32; vi++) delete keep.vp[vi];
      for (vi = 4608; vi < 4608 + 8 * SLOTS; vi++) delete keep.vp[vi];
      for (vi = 4864; vi < 4864 + 32; vi++) delete keep.vp[vi];
      // scope=mode,cycles,height,level,width,glow,smoothing,grid,layout,legend: the stage's scope (app.js's VSCOPE from
      // 4864; mode 1 the scope, level 1 True, layout 1 Overlaid, legend 0 off), any length
      if (Q.has('scope')) Q.get('scope').split(',').forEach(function (x, i) { if (x !== '') keep.vp[4864 + i] = +x; });
      var FXON = { mirror: 0, feedback: 4, tracer: 13, bloom: 22 }, FXK = { mir: 1, fb: 5, tr: 14, col: 17, bl: 23 };
      (Q.get('fx') || '').split(',').forEach(function (k) { if (FXON[k] != null) keep.vp[4352 + FXON[k]] = 1; });
      for (var fk in FXK) if (Q.has(fk)) Q.get(fk).split(',').forEach(function (x, i) { if (x !== '') keep.vp[4352 + FXK[fk] + i] = +x; });
      ['mode', 'pdens', 'pscat', 'pjit', 'pcol', 'psize', 'psoft', 'pfill'].forEach(function (k, i) {
        if (!Q.has(k)) return;
        var list = Q.get(k).split(',').map(Number);
        for (var sl = 0; sl < SLOTS; sl++) if (list.length === 1 || list[sl] != null) keep.vp[4608 + 8 * sl + i] = list.length === 1 ? list[0] : list[sl];
      });
      if (Q.has('orbit')) keep.vp[VB + 7] = +Q.get('orbit'); // the camera's turn of its own (after cam=), 0..1
      if (Q.has('zoom')) keep.vp[VB + 5] = +Q.get('zoom'); // the camera's zoom, 0.5 .. 16
      if (Q.has('look')) { keep.looks = {}; Q.get('look').split(',').forEach(function (k, sl) { if (k) keep.looks[sl] = k; }); } // per channel from slot 0
      if (Q.has('warm') || Q.has('bench') || Q.has('fxfmt')) window.AMMO_DEV_RUN = { warm: +Q.get('warm') || 0, bench: +Q.get('bench') || 0, rgba8: Q.get('fxfmt') === 'rgba8' };
    }
    if (Q.has('open')) {
      keep.open = {}; keep.secs = {};
      Q.get('open').split(',').filter(String).forEach(function (sl) {
        keep.open[sl] = true;
        if (Q.has('secs')) { keep.secs[sl] = {}; Q.get('secs').split(',').forEach(function (k) { keep.secs[sl][k] = true; }); }
        keep.grps = keep.grps || {}; keep.grps[sl] = {};
        (Q.has('grps') ? Q.get('grps').split(',') : Q.has('secs') ? ['gen', 'inst', 'fx', 'vg'] : []).forEach(function (k) { keep.grps[sl][k] = true; });
      });
    }
    if (Q.toString()) {
      keep.rail = Q.get('rail') === '1'; keep.single = Q.get('single') !== '0'; keep.uiv = 2;
      // pins=85,11,15: the pin bar's controls by id (bar=1 shows it; pins= alone too)
      if (Q.has('pins')) { keep.pins = Q.get('pins').split(',').filter(String).map(Number); keep.bar = true; }
      if (Q.has('bar')) keep.bar = Q.get('bar') === '1';
      // tgrp=stage,motion,camera,fx_mir,fx_fb,fx_tr,fx_bl,color,looks: the Visual tab's groups open (all: tgrp=all)
      keep.tgrp = {};
      (Q.get('tgrp') === 'all' ? ['stage', 'motion', 'camera', 'fx_mir', 'fx_fb', 'fx_tr', 'fx_bl', 'color', 'looks'] : (Q.get('tgrp') || '').split(',').filter(String))
        .forEach(function (k) { keep.tgrp[k] = true; });
    }
    localStorage.setItem(UI_KEY, JSON.stringify(keep));
  } catch (e) { /* no storage */ }

  /* ---------------------------------------------------------- harmony */
  var SCALE_IV = [[0, 2, 4, 5, 7, 9, 11], [0, 2, 3, 5, 7, 8, 10], [0, 2, 3, 5, 7, 9, 10], [0, 1, 3, 5, 7, 8, 10], [0, 2, 4, 6, 7, 9, 11],
                  [0, 2, 4, 5, 7, 9, 10], [0, 2, 3, 5, 7, 8, 11], [0, 2, 4, 7, 9], [0, 3, 5, 7, 10], [0, 3, 5, 6, 7, 10]];
  var PROGS = [[], [0, 4, 5, 3], [0, 5, 3, 4], [5, 3, 0, 4], [0, 5, 2, 6], [1, 4, 0, 0], [0, 3], [0, 4, 5, 2, 3, 0, 3, 4], [0, 6, 5, 4]];
  var DIV_BEATS = [16, 8, 4, 3, 2, 1.5, 1, 2 / 3, 0.75, 0.5, 1 / 3, 0.25, 1 / 6, 0.125];
  function harmony(beat) {
    var sc = Math.round(V[G.scale]), iv = SCALE_IV[sc < 10 ? sc : ([11, 15, 17].indexOf(sc) >= 0 ? 1 : 0)], root = Math.round(V[G.root]);
    var prog = Math.round(V[G.prog]) === 9 ? [0, 1, 2, 3, 4, 5, 6, 7].slice(0, Math.round(V[G.prog_len])).map(function (i) { return Math.round(V[G['prog_s' + (i + 1)]]); })
                                           : PROGS[Math.round(V[G.prog])] || [], changeBeats = [2, 4, 8, 16][Math.round(V[G.change])];
    var pp = prog.length ? Math.floor(Math.max(0, beat) / changeBeats) % prog.length : -1, deg = prog.length ? prog[pp] : Math.round(V[G.chord]);
    function pcOf(q) { return (root + iv[q % iv.length] + 12 * Math.floor(q / iv.length)); }
    var triad = [pcOf(deg), pcOf(deg + 2), pcOf(deg + 4)], sev = pcOf(deg + 6);
    var t3 = (triad[1] - triad[0] + 12) % 12, t5 = (triad[2] - triad[0] + 12) % 12;
    var cq = t3 === 4 && t5 === 7 ? 0 : t3 === 3 && t5 === 7 ? 1 : t3 === 3 && t5 === 6 ? 2 : t3 === 4 && t5 === 8 ? 3 : 4;
    var km = 0, cm = 0, i;
    for (i = 0; i < iv.length; i++) km |= 1 << ((root + iv[i]) % 12);
    for (i = 0; i < 3; i++) cm |= 1 << (triad[i] % 12);
    var scaleNotes = []; for (i = 0; i < iv.length; i++) scaleNotes.push(root + iv[i]);
    return { cr: triad[0] % 12, cd: deg, cq: cq, pp: pp, pl: prog.length, km: km, cm: cm,
             pools: [scaleNotes, triad, triad.concat([sev]), [triad[0], triad[0] + 7], [triad[0]]] };
  }

  /* -------------------------------------------------------- the voices */
  var beat = 0, playing = true, seq = 0, last = 0;
  var VO = []; for (s = 0; s < SLOTS; s++) VO.push({ step: -1, count: 0, ring: [], win: null, env: 0, gateUntil: -1, note: 57, hit: 0, seed: 0, dir: 1, idx: 0, wave: new Float32Array(128) });
  function sv(slot, sym) { return V[sid(slot, sym)]; }

  /* the engine's step grid and note choice, in small (engine/Steps.hpp, Arp.hpp): the Euclid turned by ROTATE; the
     arp's note from the step (Count Steps) or the hits before it (Hits), MODE, SHIFT; SEED a hash per step */
  function rotOf(slot, len) { return Math.round(sv(slot, 'rhy_rotate')) % len; }
  function plays(i, hits, len, rot) { return ((((i - rot) % len + len) % len) * hits) % len < hits; }
  function hitsBefore(i, hits, len, rot) { var c = 0; for (var q = 0; q < i; q++) if (plays(q, hits, len, rot)) c++; return c; }
  function seedRoll(seed, i) { var x = Math.sin(seed * 12.9898 + i * 78.233) * 43758.5453; return x - Math.floor(x); }
  function arpIndex(slot, i, len, hits, rot, n, played) {
    var mode = Math.round(sv(slot, 'arp_mode')), seed = Math.round(sv(slot, 'seed')), sh = Math.round(sv(slot, 'note_shift')), k;
    var c = Math.round(sv(slot, 'note_count')) === 2 ? hitsBefore(i, hits, len, rot) : i;
    if (mode === 5) { if (!seed && !played) return -2; k = Math.floor((seed ? seedRoll(seed, i) : Math.random()) * n); }
    else if (mode === 3) k = n - 1 - (c % n);
    else if (mode === 4) { var per = Math.max(1, 2 * n - 2); k = c % per; if (k >= n) k = per - k; }
    else if (mode === 1) k = 0;
    else k = c % n;
    return (k + sh) % n;
  }
  function arpNote(slot, h, k) {
    var pool = h.pools[Math.round(sv(slot, 'arp_pool'))], n = pool.length * Math.round(sv(slot, 'arp_range'));
    k = (k + Math.round(sv(slot, 'degree'))) % n; // degree: where in the pool the pattern starts
    return 48 + 12 * (Math.round(sv(slot, 'oct')) + Math.round(V[G.octave])) + pool[k % pool.length] + 12 * Math.floor(k / pool.length);
  }
  /** The telemetry's stepNote: each step's note (-1 a rest, -2 a Free random note not played yet). */
  function stepNotes(slot, h) {
    var gen = Math.round(sv(slot, 'gen')), rhythm = Math.round(sv(slot, 'chord_rhythm')), hold = gen === 2 && rhythm === 0;
    var len = hold ? 1 : Math.round(sv(slot, 'arp_len')), hits = hold ? 1 : Math.floor(sv(slot, 'arp_density') * len + 0.5), rot = hold ? 0 : rotOf(slot, len), out = [], vo = VO[slot];
    var pool = h.pools[Math.round(sv(slot, 'arp_pool'))], n = pool.length * Math.round(sv(slot, 'arp_range'));
    for (var i = 0; i < len; i++) {
      if (!plays(i, hits, len, rot)) { out.push(-1); continue; }
      if (gen === 1) { var k = arpIndex(slot, i, len, hits, rot, n, false); out.push(k < 0 ? (vo.played && vo.played[i] != null ? vo.played[i] : -2) : arpNote(slot, h, k)); }
      else if (gen === 4) out.push(60);
      else out.push((gen === 2 ? 48 : 36) + 12 * (Math.round(sv(slot, 'oct')) + Math.round(V[G.octave])) + h.pools[4][0]);
    }
    return out;
  }

  function stepVoice(slot, h, dt) {
    var vo = VO[slot], gen = Math.round(sv(slot, 'gen'));
    vo.hit = 0;
    if (!gen) { vo.env = 0; vo.vnotes = []; return false; }
    if (gen === 2) return stepChord(slot, vo, h, dt);
    if (gen === 3) return stepBass(slot, vo, h, dt);
    if (gen === 4) return stepRhythm(slot, vo, h, dt);
    vo.vnotes = [];
    var stepBeats = DIV_BEATS[Math.round(sv(slot, 'arp_div'))], len = Math.round(sv(slot, 'arp_len'));
    var st = Math.floor(beat / stepBeats);
    if (playing && st !== vo.step) {
      vo.step = st;
      var i = ((st % len) + len) % len, hits = Math.floor(sv(slot, 'arp_density') * len + 0.5), mode = Math.round(sv(slot, 'arp_mode')), rot = rotOf(slot, len);
      if (plays(i, hits, len, rot) && mode > 0) {
        var pool = h.pools[Math.round(sv(slot, 'arp_pool'))], range = Math.round(sv(slot, 'arp_range')), n = pool.length * range;
        vo.note = arpNote(slot, h, arpIndex(slot, i, len, hits, rot, n, true));
        (vo.played = vo.played || [])[i] = vo.note;
        vo.gateUntil = beat + stepBeats * Math.max(0.05, sv(slot, 'arp_gate'));
        vo.hit = 1; vo.seed = (Math.random() * 65535) | 0;
      }
    }
    var gate = playing && beat < vo.gateUntil, att = 0.004 + sv(slot, 'amp_a') * 0.4, dec = 0.03 + sv(slot, 'amp_d') * 1.2, rel = 0.03 + sv(slot, 'amp_r') * 1.4;
    if (vo.hit) vo.peakPhase = true;
    if (vo.peakPhase) { vo.env += (1.05 - vo.env) * (1 - Math.exp(-dt / att)); if (vo.env >= 1) { vo.env = 1; vo.peakPhase = false; } }
    else if (gate) vo.env += (sv(slot, 'amp_s') - vo.env) * (1 - Math.exp(-dt / dec));
    else vo.env *= Math.exp(-dt / rel);
    return true;
  }

  function envelope(slot, vo, dt) {
    var gate = playing && beat < vo.gateUntil, att = 0.004 + sv(slot, 'amp_a') * 0.4, dec = 0.03 + sv(slot, 'amp_d') * 1.2, rel = 0.03 + sv(slot, 'amp_r') * 1.4;
    if (vo.hit) vo.peakPhase = true;
    if (vo.peakPhase) { vo.env += (1.05 - vo.env) * (1 - Math.exp(-dt / att)); if (vo.env >= 1) { vo.env = 1; vo.peakPhase = false; } }
    else if (gate) vo.env += (sv(slot, 'amp_s') - vo.env) * (1 - Math.exp(-dt / dec));
    else vo.env *= Math.exp(-dt / rel);
    return true;
  }
  /* a chord: its tones (a cartoon of the engine's voicing), on each chord change (Hold) or step (Steps); Arp: one
     tone a step from the root (Up, Down, Up-down, Random), counted from the change; Let ring keeps them all to it */
  function stepChord(slot, vo, h, dt) {
    var rhythm = Math.round(sv(slot, 'chord_rhythm')), hold = rhythm === 0, arp = rhythm === 2;
    var win = Math.round(V[G.prog]) ? [2, 4, 8, 16][Math.round(V[G.change])] : 4, stepBeats = hold ? win : DIV_BEATS[Math.round(sv(slot, 'arp_div'))];
    var st = Math.floor(beat / stepBeats), len = hold ? 1 : Math.round(sv(slot, 'arp_len')), ring = arp && sv(slot, 'chord_ring') >= 0.5;
    if (playing && st !== vo.step) {
      vo.step = st;
      var i = ((st % len) + len) % len, hits = hold ? 1 : Math.floor(sv(slot, 'arp_density') * len + 0.5), w = Math.floor(beat / win);
      if (arp && w !== vo.win) { vo.win = w; vo.count = 0; vo.ring = []; }
      if (plays(i, hits, len, hold ? 0 : rotOf(slot, len))) {
        var nn = Math.round(sv(slot, 'chord_notes')), tones = h.pools[2].slice(0, 3 + Math.min(1, nn)), vc = Math.round(sv(slot, 'chord_voicing'));
        if (nn === 2) tones.push(tones[0] + 14);
        tones.sort(function (a, b) { return a - b; });
        if (vc === 1) for (var j = 1; j < tones.length; j += 2) tones[j] += 12;
        if (vc === 2) tones[tones.length - 2] -= 12;
        if (vc === 3) { tones[0] -= 12; tones[2] += 12; }
        var base = 48 + 12 * (Math.round(sv(slot, 'oct')) + Math.round(V[G.octave]));
        vo.vnotes = tones.map(function (t) { return base + t; }).sort(function (a, b) { return a - b; });
        vo.note = vo.vnotes[0];
        vo.gateUntil = beat + (hold ? stepBeats : stepBeats * Math.max(0.05, sv(slot, 'arp_gate')));
        if (arp) {
          var n = vo.vnotes.length, mode = Math.round(sv(slot, 'arp_mode')), root = 0, j, c = vo.count++;
          for (j = 0; j < n; j++) if (((vo.vnotes[j] - h.cr) % 12 + 12) % 12 === 0) { root = j; break; }
          if (mode === 3) j = (n - c % n) % n;
          else if (mode === 4) { var per = Math.max(1, 2 * n - 2); j = c % per; if (j >= n) j = per - j; }
          else if (mode === 5) j = Math.floor(Math.random() * n);
          else j = c % n;
          vo.note = vo.vnotes[(root + j) % n];
          if (vo.ring.indexOf(vo.note) < 0) vo.ring.push(vo.note);
          if (ring) vo.gateUntil = (Math.floor(beat / win) + 1) * win; else vo.ring = [vo.note];
        }
        vo.hit = 1; vo.seed = (Math.random() * 65535) | 0;
      }
    }
    if (arp) { if (!ring && beat >= vo.gateUntil + 0.1) vo.ring = []; vo.vnotes = vo.ring.slice(); } // the sounding notes
    return envelope(slot, vo, dt);
  }
  /* a bass line: the root on the steps, by pattern */
  function stepBass(slot, vo, h, dt) {
    var stepBeats = DIV_BEATS[Math.round(sv(slot, 'arp_div'))], len = Math.round(sv(slot, 'arp_len')), st = Math.floor(beat / stepBeats);
    vo.vnotes = [];
    if (playing && st !== vo.step) {
      vo.step = st;
      var i = ((st % len) + len) % len, hits = Math.floor(sv(slot, 'arp_density') * len + 0.5), rot = rotOf(slot, len);
      if (plays(i, hits, len, rot)) {
        var hit = hitsBefore(i, hits, len, rot), pat = Math.round(sv(slot, 'bass_pattern')), root = h.pools[4][0], semis = root;
        if (pat === 1) semis = hit & 1 ? root + 7 : root;
        if (pat === 2) semis = hit & 1 ? root + 12 : root;
        if (pat === 3) semis = h.pools[0][[0, 2, 4, 5][hit % 4] % h.pools[0].length] + (root - h.pools[0][0]);
        vo.note = 36 + 12 * (Math.round(sv(slot, 'oct')) + Math.round(V[G.octave])) + semis;
        vo.gateUntil = beat + stepBeats * Math.max(0.05, sv(slot, 'arp_gate'));
        vo.hit = 1; vo.seed = (Math.random() * 65535) | 0;
      }
    }
    return envelope(slot, vo, dt);
  }

  /* a rhythm: Euclid turned by ROTATE, each hit at CHANCE; a drum's envelope falls by its decay */
  function stepRhythm(slot, vo, h, dt) {
    var stepBeats = DIV_BEATS[Math.round(sv(slot, 'arp_div'))], len = Math.round(sv(slot, 'arp_len')), st = Math.floor(beat / stepBeats);
    vo.vnotes = [];
    if (playing && st !== vo.step) {
      vo.step = st;
      var i = ((st % len) + len) % len, hits = Math.floor(sv(slot, 'arp_density') * len + 0.5), rot = Math.round(sv(slot, 'rhy_rotate')) % len;
      var p = (i - rot + len) % len;
      if ((p * hits) % len < hits && Math.random() < sv(slot, 'rhy_chance')) {
        vo.note = 60; vo.hit = 1; vo.seed = (Math.random() * 65535) | 0; vo.env = 1;
        vo.gateUntil = beat + stepBeats * 0.1;
      }
    }
    if (Math.round(sv(slot, 'inst')) === 1) { var d = sv(slot, 'drum_decay'); vo.env *= Math.exp(-dt / (0.05 + d * d * 0.8)); return true; }
    return envelope(slot, vo, dt);
  }

  function waveOf(slot, out) {
    if (Math.round(sv(slot, 'inst')) === 1) { // a drum: a sine body or noise, by model
      var m = Math.round(sv(slot, 'drum_model')), tonal = m === 0 || m === 5 || m === 6, noisy = m !== 0 && m !== 5;
      for (var j = 0; j < 128; j++) out[j] = (tonal ? Math.sin(j / 128 * Math.PI * 2) : 0) + (noisy ? (Math.random() * 2 - 1) * (tonal ? 0.3 : 1) : 0);
      return;
    }
    var shp = sv(slot, 'shape'), cut = sv(slot, 'cutoff'), sub = sv(slot, 'sub'), noise = sv(slot, 'noise'), i, v;
    function osc(ph) {
      ph -= Math.floor(ph);
      var sine = Math.sin(ph * Math.PI * 2), tri = 1 - 4 * Math.abs(ph - 0.5), saw = 1 - 2 * ph, sq = ph < 0.5 ? 1 : -1;
      return shp < 1 / 3 ? sine + (tri - sine) * shp * 3 : shp < 2 / 3 ? tri + (saw - tri) * (shp - 1 / 3) * 3 : saw + (sq - saw) * (shp - 2 / 3) * 3;
    }
    // one period of the lowest voice, as the engine's is: the other voices of a chord ride on it at their own
    // pitch, drifting against it (they are not whole multiples of it), so a chord's wave keeps changing
    var vn = VO[slot].vnotes && VO[slot].vnotes.length > 1 ? VO[slot].vnotes : null, base = vn ? vn[0] : 0;
    for (i = 0; i < 128; i++) {
      var ph = i / 128, y = osc(ph);
      if (vn) { for (v = 1; v < vn.length; v++) y += osc(ph * Math.pow(2, (vn[v] - base) / 12) + beat * 0.11 * v); y /= Math.sqrt(vn.length); }
      out[i] = y + sub * 0.6 * (ph < 0.5 ? 1 : -1) * 0.5 + noise * (Math.random() * 2 - 1) * 0.5 + (Math.random() * 2 - 1) * 0.015; // a little jitter, as the real lock has
    }
    // a one-pole low pass around the cycle, twice, by the cutoff
    var a = 0.06 + 0.94 * cut * cut, z = out[127];
    for (var pass = 0; pass < 2; pass++) for (i = 0; i < 128; i++) { z += a * (out[i] - z); out[i] = z; }
    var dr = sv(slot, 'fx_drive'); // the insert drive squares it off
    if (dr > 0) { var g = 1 + 31 * dr * dr; for (i = 0; i < 128; i++) out[i] = Math.tanh(g * out[i] * 0.5) / Math.tanh(g * 0.5); }
  }

  /* the delay: echoes of the last hit at the division, each fb of the one before, raising the channel's level */
  var DLY_BEATS = [0.125, 0.25, 1 / 6, 0.5, 1 / 3, 0.75, 1, 2 / 3, 1.5, 2, 4];
  function echoes(slot, vo) {
    var mix = sv(slot, 'dly_mix'); if (mix <= 0 || vo.hitBeat == null) return 0;
    var div = DLY_BEATS[Math.round(sv(slot, 'dly_div'))], fb = sv(slot, 'dly_fb'), since = beat - vo.hitBeat, k = Math.floor(since / div);
    if (k < 1) return 0;
    var amp = mix * Math.pow(fb, k - 1), into = (since - k * div) * 60 / V[G.tempo];
    return amp < 0.01 ? 0 : amp * Math.exp(-into / 0.12);
  }

  function b64(bytes) {
    var str = ''; for (var i = 0; i < bytes.length; i++) str += String.fromCharCode(bytes[i]);
    return btoa(str);
  }

  /* the LFOs as the engine has them (engine/Mod.hpp): Tempo off the beat, Free on its own clock; no restart here */
  var LDIVB = [64, 32, 16, 8, 4, 3, 2, 1.5, 1, 2 / 3, 0.75, 0.5, 1 / 3, 0.25, 1 / 6, 0.125], lfoFree = {};
  function lfoShape(sh, pos) {
    var f = pos - Math.floor(pos), k = Math.floor(pos), rnd = function (n) { var x = Math.sin(n * 91.7 + 17.3) * 43758.5453; return (x - Math.floor(x)) * 2 - 1; };
    switch (sh) {
      case 1: return f < 0.25 ? 4 * f : f < 0.75 ? 2 - 4 * f : 4 * f - 4;
      case 2: return 2 * f - 1;
      case 3: return 1 - 2 * f;
      case 4: return f < 0.5 ? 1 : -1;
      case 5: return rnd(k);
      case 6: var a = rnd(k), b = rnd(k + 1), sm = f * f * (3 - 2 * f); return a + (b - a) * sm;
      default: return Math.sin(f * 2 * Math.PI);
    }
  }
  function lfoNow(key, val, dt) {
    var ph = lfoFree[key] || 0;
    lfoFree[key] = ph + 0.02 * Math.pow(2, 9.966 * val('rate')) * dt;
    var pos = Math.round(val('sync')) === 1 ? beat / LDIVB[Math.round(val('div'))] : ph;
    return lfoShape(Math.round(val('shape')), pos + val('phase'));
  }
  /** A route's target, if a route can move it (engine/Params.hpp IsModTarget). */
  function targetDef(t) {
    var d = DEF[t]; if (!d || d.steps) return null;
    return (d.slot >= 0 ? DEST_SYMS.indexOf(d.s) >= 0 : GLOBAL_TARGETS.indexOf(d.s) >= 0 && t < 64) ? d : null;
  }
  /** The pool this frame: the LFOs that run (in use, or a live route takes them) {l: value}, and each live route's
      target position after every route on it {r: 0..1} (bipolar an LFO as it is, unipolar (s + 1) / 2; a macro 0..1). */
  function pool(dt) {
    var live = [], run = {}, l, r;
    for (r = 0; r < POOL_ROUTES; r++) {
      var src = Math.round(V[rid(r, 'src')]), t = Math.round(V[rid(r, 'target')]), a = V[rid(r, 'amt')];
      if (!src || !a || !targetDef(t)) continue;
      live.push({ r: r, src: src, t: t, a: a, uni: Math.round(V[rid(r, 'pol')]) === 0 });
      if (src < SRC_M1) run[src - 1] = 1;
    }
    var lf = {}, rt = {}, add = {};
    for (l = 0; l < POOL_LFOS; l++) {
      if (!run[l] && V[lid(l, 'on')] < 0.5) continue;
      lf[l] = lfoNow('p' + l, function (k) { return V[lid(l, k)]; }, dt);
    }
    live.forEach(function (x) {
      var sv = x.src >= SRC_M1 ? macroVal(x.src - SRC_M1) : x.uni ? (lf[x.src - 1] + 1) / 2 : lf[x.src - 1];
      add[x.t] = (add[x.t] || 0) + x.a * sv;
    });
    live.forEach(function (x) { var d = DEF[x.t]; rt[x.r] = Math.max(0, Math.min(1, (V[x.t] - d.min) / (d.max - d.min) + add[x.t])); });
    return { lf: lf, rt: rt };
  }

  function frame() {
    var now = performance.now(), dt = last ? Math.min(0.1, (now - last) / 1000) : 0.016; last = now;
    var mod = pool(dt), bpm = V[G.tempo];
    if (playing) beat += dt * bpm / 60;
    var h = harmony(beat), chans = {}, mr = 0, bytes = new Uint8Array(128);
    for (var slot = 0; slot < SLOTS; slot++) {
      if (!stepVoice(slot, h, dt)) continue;
      var vo = VO[slot];
      if (vo.hit) vo.hitBeat = beat;
      vo.env = Math.max(vo.env, echoes(slot, vo));
      waveOf(slot, vo.wave);
      var peak = 0, i;
      for (i = 0; i < 128; i++) peak = Math.max(peak, Math.abs(vo.wave[i]));
      for (i = 0; i < 128; i++) { var q = peak > 0 ? Math.round(vo.wave[i] / peak * 127) : 0; bytes[i] = q < 0 ? q + 256 : q; }
      var e = vo.env * sv(slot, 'level'), muted = sv(slot, 'mute') >= 0.5;
      if (!muted) mr = Math.max(mr, e * 0.5);
      chans[slot] = { a: playing ? 1 : 0, m: muted ? 1 : 0, n: vo.note, pc: ((vo.note % 12) + 12) % 12, e: vo.env, r: e * 0.5, k: e * 0.8, h: vo.hit, sd: vo.seed,
                      st: Math.max(0, vo.step) % Math.max(1, Math.round(sv(slot, 'arp_len'))), ln: Math.round(sv(slot, 'arp_len')), sn: stepNotes(slot, h), ws: peak * vo.env * sv(slot, 'level') * sv(slot, 'level') * 0.6, w: b64(bytes), // the engine's: at the level (audio taper)
                      v: vo.vnotes && vo.vnotes.length ? vo.vnotes : [vo.note] };
      var dsrc = Math.round(sv(slot, 'duck_src')) - 1; // DUCK: the source's envelope, by the amount
      if (dsrc >= 0 && dsrc !== slot && sv(slot, 'duck_amt') > 0) chans[slot].dk = Math.min(1, VO[dsrc].env) * sv(slot, 'duck_amt');
    }
    seq++;
    // the CPU meter: a stand-in load, a little per channel playing and per route, jittered, a spike now and then
    // (dev URL cpu=0.8: that load, to see the meter warm)
    var nch = Object.keys(chans).length, cpu = Q.has('cpu') ? +Q.get('cpu') * (0.95 + 0.1 * Math.random()) :
      0.012 + 0.007 * nch + 0.004 * nch * Math.random() + (Math.random() < 0.01 ? 0.03 : 0);
    var f = { s: seq, b: beat, t: bpm, p: playing ? 1 : 0, h: 0, cr: h.cr, cd: h.cd, cq: h.cq, pp: h.pp, pl: h.pl, km: h.km, cm: h.cm,
              sr: 48000, bl: 128, mr: mr, mp: mr * 1.4, ch: chans, lf: mod.lf, rt: mod.rt, cpu: cpu };
    // the master bus: the glue takes half of what is over its threshold, the limiter what passes the ceiling
    var mdb = 20 * Math.log(Math.max(mr, 1e-5)) / Math.LN10;
    f.gg = Math.max(0, (mdb + 6 + 24 * V[G.glue]) * 0.5);
    f.lg = Math.max(0, 20 * Math.log(Math.max(f.mp, 1e-5)) / Math.LN10 - V[G.ceiling]);
    window.ammo.tick([f], { idleMs: 0, evalMs: 0 });
  }

  /* ------------------------------------------------------- the bridge */
  var recStart = 0, outCur = '';
  function fakeOuts() {
    var list = [{ id: 'fr', name: 'Focusrite USB (Focusrite USB Audio)' }, { id: 'vb', name: 'CABLE In 16ch (VB-Audio Virtual Cable)' },
                { id: 'hd', name: 'Speakers (Realtek(R) Audio)' }], cur = list.filter(function (d) { return d.id === outCur; })[0];
    return { cur: outCur, name: cur ? cur.name : list[0].name, rate: 48000, list: list };
  }
  function values() { return TABLE.map(function (d) { return [d.id, V[d.id]]; }); }
  function onMsg(m) {
    var a = String(m).split(' ');
    switch (a[0]) {
      case 'ready':
        window.ammo.params(TABLE);
        window.ammo.values(values());
        window.ammo.presets(PRESETS.map(function (p) { return { n: p.n, b: p.b }; }), preset);
        window.ammo.info({ fmt: 'browser', inst: 1 });
        if (window.ammo.outputs) window.ammo.outputs(fakeOuts());
        setInterval(frame, 16);
        break;
      // the standalone host's output devices, as it answers them (a stand-in list)
      case 'outputs': window.ammo.outputs(fakeOuts()); break;
      case 'output': outCur = a.slice(1).join(' '); window.ammo.outputs(fakeOuts()); break;
      case 'set': V[+a[1]] = +a[2]; break;
      case 'preset':
        applyPreset(+a[1]);
        window.ammo.values(values());
        window.ammo.presets(PRESETS.map(function (p) { return { n: p.n, b: p.b }; }), preset);
        break;
      case 'key':
        if (a[1] === '1' && a[2] === '32' && a[3] === '0') playing = !playing;
        break;
      case 'save': // no files in a browser: say what would have been written
        console.log('[fake] save', String(m).slice(5));
        window.ammo.toast('Saved (dev): the patch plus ' + (String(m).length - 5) + ' bytes of page state');
        break;
      case 'open':
        window.ammo.toast('Open (dev): no files in a browser');
        break;
      case 'record':
        if (a[1] === '1') { recStart = performance.now(); window.ammo.rec({ on: 1 }); }
        else window.ammo.rec({ on: 0, ok: 1, secs: (performance.now() - recStart) / 1000, dropped: 0, path: 'Documents\\Ammonite\\Recordings\\Ammonite (dev).wav' });
        break;
      case 'gl': case 'log': console.log('[page]', m); break;
    }
  }
  window.chrome = window.chrome || {};
  window.chrome.webview = { postMessage: function (m) { setTimeout(function () { onMsg(m); }, 0); } };
})();
