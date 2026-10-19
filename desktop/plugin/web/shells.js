/* Ammonite Desktop: the stage. Each shown channel is a shell: its
   waveform turned around a vertical axis, so the outline is the wave. Nested
   (one shell each, inner to outer) or merged (one surface, all waves added),
   with a morph between. Looks: shell, ripple (hits strike the surface),
   breath (the whole form swells), glow (light inside the glass).

   The waves come from the engine's telemetry as 128 signed bytes per channel
   and live in a 128 x 16 texture the vertex shader reads (one row per slot).

   Shells.init(canvas) -> status string ('ok ...' or 'fail ...')
   Shells.draw(canvas, chans, opts): chans = [{slot, bytes (Uint8Array 128),
   hue, sat, rgb, amp, look, hit, hitSeed, note, dim, pres, swell, fade, size, alpha}], opts = {merge
   (0..1), time, dt, scale, zoom, offX, offY, spin, drift, tilt, roll, persp, ripple: {str, spd}}.
   amp, bytes and note come smoothed from the page's motion layer; pres
   scales a channel's depth, swell (the pulse and the ring after a hit,
   about -1..1) swells it, fade (0..1: shown and unmuted) takes it all out; spin and drift are accumulated angles (time,
   when absent). size (0.5..1.5) is a channel's radius (nested: its shell; merged: its weight in the surface; glow:
   the light's radius; ripple: the crest's width; breath: how far it swells); alpha (0..1) its opacity (its
   shell's intensity, its glow, its ripples, its breath). The camera: spin is the yaw (around the vertical), tilt
   the angle seen from above (radians, negative from below), roll lays the form on its side (pi/2: its axis left
   to right), persp how much nearer parts grow (0.22 before the camera); scale already carries the zoom.

   Particles (2026-10-06): a channel's mode (0 Surface, 1 Particles, 2 Both) and its points: pd density (0..1 of
   PTS_RANGE points), ps scatter (a fixed offset off the surface), pj jitter (each point's vibration, by its level
   and its hits), pc color jitter (hue and brightness per point), pz size (px at a 1440-high stage), pf softness.
   The points sit on the very surface the shell's shader computes (the same pos(), waves, ripples, breath, camera,
   Size and Alpha), drawn as soft additive gl.POINTS. Nested: each shell's own points. Merged: one surface; each
   channel in Particles or Both puts its own points (its own color) on it, and the surface itself is as opaque as
   the channels in Surface or Both make it. Glow in Particles: motes filling the glass instead of the light (Both:
   both). Ripple and breath have no surface of their own: the pearl drawn for them when no shell is up follows
   their mode (points with the first such channel's settings). pf fill (0..1): the points from the surface into
   the form's volume (scaled toward its centre: the form is a radius in every direction, so that fills it exactly),
   0 all on the surface, 1 spread evenly through it; the inner ones without the outline's brightening.

   Color (2026-10-06): a channel's rgb (0..1), when given, stands for its hue / sat everywhere (the page's Palette
   and Cycle color modes); without it, hue and sat as always (Pitch).

   Stage FX (2026-10-06), opts.fx = {mirror: {div, op, top}, feedback: {fade, zoom, rot, dx, dy, hue, add},
   tracer: {n, gap, fade}, bloom: {str, rad, thr}} (any of them absent = off; rates per second, the fade per 1/60 s): the stage is drawn
   as always, copied to a texture, then Mirror -> Feedback -> Tracer -> the screen (scAnimator's order). All off:
   no copy, no pass, no texture (freed). The passes work on the light above the background (the background
   taken off, put back on the screen), so mirrors and trails fade to the background, not to black.
   Tracer up to TRACER_MAX held frames: up to 24 slots at half resolution, each the brightest of B frames in a row
   (B = 1 up to 24 frames, 8 at 180), max-blended as they come (EXT_blend_minmax) and onto the screen K slots a
   pass; without the extension one frame a slot, at most K.
   Bloom (2026-10-06), fx.bloom = {str, rad, thr}, last: the light above thr (a soft knee) at half size, blurred
   down a chain of halvings (dual Kawase) and added back up it, each smaller level by rad, normalized, then str of
   it over the picture. Half-float levels when the feedback has them.

   The scope (2026-10-13), opts.scope = {lanes: [{wave (Float32Array 128, -1..1), amp (of the lane's half height),
   top, bot, mid, half (fractions of the canvas height), col [r, g, b], k (0..1 brightness)}], x0, x1 (fractions of
   its width), cycles, unit (a layout pixel, as a fraction of the height), lw (line width, layout px), glow (0..1),
   grid, overlay (the lanes share one place: one grey centre line and one pair of edges)}: the stage as an oscilloscope instead of the form (chans unused). Each lane's wave, cycles times across,
   is a strip of triangles round the line, built here in pixels (mitred at the corners); the fragment's distance
   from the line's middle gives a crisp core, antialiased over a pixel, and a Gaussian halo past it (glow 0: none).
   Max-blended (EXT_blend_minmax), so a steep edge folding the strip over itself does not brighten; additive
   without it. A faint centre line per lane in its colour; grid: lines at each quarter period and each lane's
   edges. The stage FX run on it as on the form. */
var Shells = (function () {
  'use strict';
  var TAU = Math.PI * 2;
  var VS_LIB = [
    'uniform float uRad, uAspect, uScale, uFlat, uPersp;',
    'uniform vec2 uOff, uBreath;',
    'uniform mat3 uRot;',
    'uniform int uNS, uNR;',
    'uniform vec4 uSA[6];',   // row, cycles, depth, phase
    'uniform vec4 uSC[6];',   // color, its weight in the surface's color (alpha)
    'uniform vec4 uRD[8];',   // ripple: direction xyz, travel angle
    'uniform vec2 uRP[8];',   // ripple: strength, width
    'uniform vec3 uRC[8];',   // ripple: color
    'uniform sampler2D uWave;',
    'const float PI = 3.14159265;',
    'float wv(float row, float f) { return texture2D(uWave, vec2(fract(f), (row + 0.5) / 16.0)).r * 2.0 - 1.0; }',
    'vec3 pos(float u, float v, out vec3 col, out vec3 em, out float wn) {',
    '  float phi = -PI * 0.5 + PI * u, lam = 2.0 * PI * v, c = cos(phi);',
    '  vec3 n0 = vec3(c * sin(lam), -sin(phi), c * cos(lam));',
    '  float d = 0.0, ws = 0.0, dsum = 0.0; col = vec3(0.0); em = vec3(0.0);',
    '  if (uFlat < 0.5) {',
    '    for (int i = 0; i < 6; i++) {',
    '      if (i >= uNS) break;',
    '      vec4 a = uSA[i];',
    '      float s = wv(a.x, u * a.y + a.w) * a.z;',
    '      d += s; dsum += a.z;',
    '      float aw = (s * s + 0.00002) * uSC[i].w;',
    '      col += aw * uSC[i].rgb; ws += aw;',
    '    }',
    '    for (int j = 0; j < 8; j++) {',
    '      if (j >= uNR) break;',
    '      float ang = acos(clamp(dot(n0, uRD[j].xyz), -1.0, 1.0));',
    '      float x = (ang - uRD[j].w) / uRP[j].y;',
    '      float cr = uRP[j].x * exp(-x * x) * (1.0 - 0.6 * x * x);',
    '      d += cr; em += max(cr, 0.0) * uRC[j] * 4.0;',
    '    }',
    '  }',
    '  col = ws > 0.0 ? col / ws : uSC[0].rgb;',
    '  wn = dsum > 0.0 ? clamp(d / dsum, -1.0, 1.0) : 0.0;',
    '  vec3 p = n0 * uRad * (1.0 + d);',
    '  p.y *= 1.0 + uBreath.y; p.xz *= 1.0 + uBreath.x - 0.35 * uBreath.y;',
    '  return p; }'
  ];
  var VS = ['attribute vec2 aUV;'].concat(VS_LIB, [
    'varying vec3 vN; varying vec3 vCol; varying vec3 vEm; varying float vW;',
    'void main() {',
    '  float u = aUV.x, v = aUV.y, e = 0.003, w0, wd; vec3 c0, e0, cd, ed;',
    '  vec3 p = pos(u, v, c0, e0, w0);',
    '  vec3 pu = pos(clamp(u + e, 0.0, 1.0), v, cd, ed, wd) - pos(clamp(u - e, 0.0, 1.0), v, cd, ed, wd);',
    '  vec3 pv = pos(u, v + e, cd, ed, wd) - pos(u, v - e, cd, ed, wd);',
    '  vec3 n = cross(pu, pv); n = length(n) > 1e-7 ? normalize(n) : normalize(p + 1e-6);',
    '  p = uRot * p; vN = uRot * n; vCol = c0; vEm = e0; vW = w0;',
    '  float w = max(0.05, 1.0 - p.z * uPersp);',
    '  gl_Position = vec4(p.x * uScale / uAspect + uOff.x * w, p.y * uScale + uOff.y * w, 0.0, w); }'
  ]).join('\n');
  var FS = [
    'precision mediump float;',
    'uniform float uInt, uGlowF, uSat;',
    'varying vec3 vN; varying vec3 vCol; varying vec3 vEm; varying float vW;',
    'void main() {',
    '  vec3 n = normalize(vN); float fres = pow(1.0 - abs(n.z), 2.2);',
    '  vec3 c = vCol; float g = dot(c, vec3(0.333)); c = clamp(mix(vec3(g), c, uSat), 0.0, 1.0);',
    '  if (uGlowF > 0.5) { float core = pow(abs(n.z), 1.6); gl_FragColor = vec4(c * (0.55 + 0.6 * core), uInt * 0.14 * core); return; }',
    '  float crest = 0.6 + 0.5 * vW, front = gl_FrontFacing ? 1.0 : 0.55;',
    '  vec3 col = c * (0.08 + 1.35 * fres) * crest + vec3(1.0) * pow(fres, 7.0) * 0.4 + vEm * 1.4;',
    '  gl_FragColor = vec4(col, uInt * (0.08 + 0.8 * fres) * front + min(length(vEm), 1.0) * 0.45 * uInt); }'
  ].join('\n');

  /* particles: the same pos() at a random place on the surface (u by area, so the poles are not crowded), pushed
     off it by a fixed random offset (scatter) and a vibration of its own (jitter); a: u, v, two seeds */
  var PVS = ['attribute vec4 aP;'].concat(VS_LIB, [
    'uniform float uTime, uScat, uJit, uCJit, uPt, uPtMax, uPInt, uMote, uFill;',
    'uniform vec3 uPCol;',
    'varying vec3 vC; varying float vA;',
    'float h1(float n) { return fract(sin(n * 12.9898) * 43758.5453); }',
    'vec3 hueRot(vec3 c, float a) { vec3 k = vec3(0.57735); float ca = cos(a); return c * ca + cross(k, c) * sin(a) + k * dot(k, c) * (1.0 - ca); }',
    'void main() {',
    '  float ra = aP.z, rb = aP.w, wn = 0.0, fres = 1.0; vec3 col, em = vec3(0.0), p;',
    '  float phi = -PI * 0.5 + PI * aP.x, lam = 2.0 * PI * aP.y, c0 = cos(phi);',
    '  vec3 n0 = vec3(c0 * sin(lam), -sin(phi), c0 * cos(lam));',
    '  if (uMote > 0.5) p = n0 * uRad * pow(h1(ra * 3.7 + rb), 0.45);',   // glow: motes inside the glass, a little denser in the middle
    '  else {',   // on the surface; brighter toward its outline, as the glass is (the sphere's normal stands in for the surface's)
    '    p = pos(aP.x, aP.y, col, em, wn);',
    '    fres = 0.3 + 1.3 * pow(1.0 - abs((uRot * n0).z), 1.5);',
    '    float fr = 1.0 - uFill * (1.0 - pow(h1(ra * 5.3 + rb * 2.9), 0.3333));',   // fill: toward the centre, even in volume at 1
    '    p *= fr; fres = mix(0.6, fres, fr * fr * fr * fr);',
    '  }',
    '  vec3 rnd = vec3(h1(ra * 91.7 + rb), h1(rb * 53.1 + ra * 7.0), h1(ra * 17.3 + rb * 29.0)) * 2.0 - 1.0;',
    '  p += rnd * uScat * uRad * 0.35 * h1(ra + rb * 3.1);',
    '  float f = 9.0 + 23.0 * rb, ph = 6.2831853 * ra;',
    '  p += vec3(sin(uTime * f + ph), sin(uTime * f * 1.13 + ph * 2.3), sin(uTime * f * 0.87 + ph * 3.7)) * uJit * uRad * 0.06;',
    '  p = uRot * p;',
    '  float w = max(0.05, 1.0 - p.z * uPersp);',
    '  gl_Position = vec4(p.x * uScale / uAspect + uOff.x * w, p.y * uScale + uOff.y * w, 0.0, w);',
    '  vec3 c = hueRot(uPCol, (h1(ra * 7.7 + rb * 3.3) - 0.5) * uCJit * 2.4) * (1.0 + (h1(rb * 5.1 + ra) - 0.5) * uCJit * 1.4);',
    '  vC = max(c, 0.0) * (0.7 + 0.5 * wn) + em * 1.4;',
    '  float sz = uPt / w, back = 0.55 + 0.45 * clamp(p.z / max(uRad, 0.05) * 0.5 + 0.5, 0.0, 1.0);',
    '  vA = uPInt * fres * (0.6 + 0.4 * h1(ra * 3.3 + rb * 1.7)) * back * min(1.0, sz * sz);',   // under a pixel: dimmer, not bigger
    '  gl_PointSize = clamp(sz, 1.0, uPtMax); }'
  ]).join('\n');
  var PFS = [
    'precision mediump float;',
    'uniform float uSoft;',
    'varying vec3 vC; varying float vA;',
    'void main() {',
    '  vec2 q = gl_PointCoord * 2.0 - 1.0; float d = dot(q, q);',
    '  if (d > 1.0) discard;',
    '  gl_FragColor = vec4(vC, vA * pow(1.0 - d, 0.15 + 2.5 * uSoft)); }'
  ].join('\n');
  var PTS_RANGE = 32768, PTS_RANGES = 6, PTS_TOTAL = PTS_RANGE * PTS_RANGES; // ~33k a channel, 197k on the stage

  /* the stage FX: full-screen passes. uSub takes the background off a texture straight from the stage (zero for
     the passes' own textures), uAdd puts it back on the pass that draws the screen */
  var QVS = 'attribute vec2 aQ; varying vec2 vUv; void main() { vUv = aQ * 0.5 + 0.5; gl_Position = vec4(aQ, 0.0, 1.0); }';
  var QHEAD = ['#ifdef GL_FRAGMENT_PRECISION_HIGH', 'precision highp float;', '#else', 'precision mediump float;', '#endif',
               'varying vec2 vUv;', 'uniform vec3 uSub, uAdd;', 'uniform sampler2D uSrc;',
               'vec3 L(vec2 uv) { return max(texture2D(uSrc, uv).rgb - uSub, 0.0); }'];
  /* Mirror: scAnimator's fractal reflection, around the form: a square as high as the stage, centred on the form,
     tiled div x div with mirroring, then each tile again; behind the original at an opacity. The copies are
     1/div^2 the size, so four taps a quarter pixel apart keep their thin lines from sparkling. */
  var MIR_FS = QHEAD.concat([
    'uniform vec2 uCtr, uSide, uPx; uniform float uDiv, uOp, uTop;',
    'vec2 fold(vec2 q) { q *= uDiv; q = abs(2.0 * fract(q * 0.5) - 1.0); q *= uDiv; return abs(2.0 * fract(q * 0.5) - 1.0); }',
    'vec3 M(vec2 q) { return L(uCtr + (fold(q) - 0.5) * uSide); }',
    'void main() {',
    '  vec3 o = L(vUv); vec2 q = (vUv - uCtr) / uSide + 0.5, d = 0.25 * uPx / uSide;',
    '  vec3 m = 0.25 * (M(q + vec2(-d.x, -d.y)) + M(q + vec2(d.x, -d.y)) + M(q + vec2(-d.x, d.y)) + M(q + vec2(d.x, d.y))) * uOp;',
    '  vec3 c = uTop > 0.5 ? mix(m, o, step(0.01, max(o.r, max(o.g, o.b)))) : m;',
    '  gl_FragColor = vec4(c + uAdd, 1.0); }']).join('\n');
  /* Feedback: the last frame back, zoomed / turned / drifted about the form's centre and hue-turned, faded by
     fade^(dt*60), then max(old, new) (scAnimator's trails exactly, at no zoom or turn) or a screen blend (Add,
     glowing but never past white). uFloor: an 8-bit target would stick at a low value without it. */
  var FB_FS = QHEAD.concat([
    'uniform sampler2D uOld; uniform vec2 uCtr, uShift; uniform float uAsp, uFade, uFloor, uMode; uniform mat2 uM; uniform mat3 uHue;',
    'void main() {',
    '  vec3 n = L(vUv);',
    '  vec2 d = uM * ((vUv - uCtr) * vec2(uAsp, 1.0) - uShift), uv = uCtr + d / vec2(uAsp, 1.0);',
    '  float inside = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);',
    '  vec3 o = min(max(uHue * texture2D(uOld, uv).rgb, 0.0), 1.0) * inside;',
    '  o = max(o * uFade - uFloor, 0.0);',
    '  gl_FragColor = vec4(uMode > 0.5 ? n + o * (1.0 - min(n, 1.0)) : max(o, n), 1.0); }']).join('\n');
  var COPY_FS = QHEAD.concat(['void main() { gl_FragColor = vec4(L(vUv) + uAdd, 1.0); }']).join('\n');
  /* Tracer: the held frames (a ring at half resolution), the newest brightest, max-composited over the frame */
  /* Bloom: the bright pass (a 4-tap box at half size, a soft-kneed threshold), the dual Kawase halving and
     doubling (the doubling adds the level it lands on, the smaller one weighted by uW), the composite */
  var BRIGHT_FS = QHEAD.concat([
    'uniform vec2 uPx; uniform float uThr;',
    'void main() {',
    '  vec3 c = 0.25 * (L(vUv - uPx) + L(vUv + uPx) + L(vUv + vec2(uPx.x, -uPx.y)) + L(vUv + vec2(-uPx.x, uPx.y)));',
    '  float br = max(c.r, max(c.g, c.b)), k = max(uThr * 0.5, 1e-3), s = clamp(br - uThr + k, 0.0, 2.0 * k);',
    '  s = s * s / (4.0 * k);',
    '  gl_FragColor = vec4(c * max(s, br - uThr) / max(br, 1e-4), 1.0); }']).join('\n');
  var DOWN_FS = QHEAD.concat([
    'uniform vec2 uPx;',
    'void main() {',
    '  vec3 c = L(vUv) * 4.0 + L(vUv - uPx) + L(vUv + uPx) + L(vUv + vec2(uPx.x, -uPx.y)) + L(vUv + vec2(-uPx.x, uPx.y));',
    '  gl_FragColor = vec4(c / 8.0, 1.0); }']).join('\n');
  var UP_FS = QHEAD.concat([
    'uniform vec2 uPx; uniform sampler2D uCur; uniform float uW;',
    'void main() {',
    '  vec2 h = uPx * 0.5;',
    '  vec3 c = L(vUv + vec2(-2.0 * h.x, 0.0)) + L(vUv + vec2(2.0 * h.x, 0.0)) + L(vUv + vec2(0.0, 2.0 * h.y)) + L(vUv + vec2(0.0, -2.0 * h.y))',
    '         + 2.0 * (L(vUv + h) + L(vUv - h) + L(vUv + vec2(h.x, -h.y)) + L(vUv + vec2(-h.x, h.y)));',
    '  gl_FragColor = vec4(texture2D(uCur, vUv).rgb + uW * c / 12.0, 1.0); }']).join('\n');
  var BLOOM_FS = QHEAD.concat([
    'uniform sampler2D uBloom; uniform float uStr;',
    'void main() { gl_FragColor = vec4(L(vUv) + uStr * texture2D(uBloom, vUv).rgb + uAdd, 1.0); }']).join('\n');
  var BLOOM_LEVELS = 6;
  function tracerFs(k) {
    var out = QHEAD.concat(['uniform sampler2D uH[' + k + ']; uniform float uW[' + k + '];', 'void main() {', '  vec3 c = L(vUv);']);
    for (var i = 0; i < k; i++) out.push('  c = max(c, texture2D(uH[' + i + '], vUv).rgb * uW[' + i + ']);');
    return out.concat(['  gl_FragColor = vec4(c + uAdd, 1.0); }']).join('\n');
  }

  function meshFor(gl) {
    var NL = 220, NO = 72, uv = [], idx = [], i, j;
    for (i = 0; i <= NL; i++) for (j = 0; j <= NO; j++) uv.push(i / NL, j / NO);
    for (i = 0; i < NL; i++) for (j = 0; j < NO; j++) { var a = i * (NO + 1) + j, b = a + NO + 1; idx.push(a, b, a + 1, a + 1, b, b + 1); }
    var vb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, vb); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(uv), gl.STATIC_DRAW);
    var ib = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array(idx), gl.STATIC_DRAW);
    return { vb: vb, ib: ib, n: idx.length };
  }
  /* Rx(ax) Ry(ay) Rz(az): the roll turns the form in its own space (laid on its side), then the yaw turns it
     around the vertical and the tilt tips it toward the viewer */
  function rotation(ax, ay, az) {
    var cx = Math.cos(ax), sx = Math.sin(ax), cy = Math.cos(ay), sy = Math.sin(ay), cz = Math.cos(az || 0), sz = Math.sin(az || 0);
    var a = [[cy, 0, sy], [sx * sy, cx, -sx * cy], [-cx * sy, sx, cx * cy]], z = [[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]], out = new Float32Array(9);
    for (var c = 0; c < 3; c++) for (var r = 0; r < 3; r++) out[c * 3 + r] = a[r][0] * z[0][c] + a[r][1] * z[1][c] + a[r][2] * z[2][c];
    return out;
  }
  function hash(n) { var x = Math.sin(n * 127.1 + 311.7) * 43758.5453; return x - Math.floor(x); }
  /** A channel's color at lightness l (and saturation + more): its rgb when the page gives one (Palette, Cycle),
      lighter or darker about l 58 as the hue's would be; else its hue and sat. */
  function colOf(c, more, l) {
    if (!c.rgb) return Knobs.hslRgb(c.hue, Math.min(100, c.sat + more), l);
    var k = 1 + (l - 58) * 0.035;
    return [Math.min(1, c.rgb[0] * k), Math.min(1, c.rgb[1] * k), Math.min(1, c.rgb[2] * k)];
  }

  /* the scope: a strip vertex is its place in pixels (y down) and its signed distance from the line's middle */
  var SVS = 'attribute vec3 aS; uniform vec2 uRes; varying float vD; void main() { vD = aS.z; gl_Position = vec4(aS.x / uRes.x * 2.0 - 1.0, 1.0 - aS.y / uRes.y * 2.0, 0.0, 1.0); }';
  var SFS = ['precision mediump float;', 'varying float vD; uniform vec3 uCol; uniform float uHW, uGW, uGlow;',
             'void main() {',
             '  float d = abs(vD), core = 1.0 - smoothstep(uHW - 0.5, uHW + 0.5, d), g = uGW > 0.0 ? uGlow * exp(-d * d / (uGW * uGW)) : 0.0;',
             '  gl_FragColor = vec4(uCol * max(core, g), 1.0); }'].join('\n');

  var S = null, rips = [], mergeNow = 0, texData = new Uint8Array(128 * 16), TRACER_MAX = 180;

  function init(cv) {
    var gl = cv.getContext('webgl', { antialias: true, alpha: false, preserveDrawingBuffer: false });
    if (!gl) return 'fail no webgl context';
    if (gl.getParameter(gl.MAX_VERTEX_TEXTURE_IMAGE_UNITS) < 1) return 'fail no vertex texture units';
    function sh(type, src) {
      var s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
      return s;
    }
    var pr;
    try {
      pr = gl.createProgram();
      gl.attachShader(pr, sh(gl.VERTEX_SHADER, VS)); gl.attachShader(pr, sh(gl.FRAGMENT_SHADER, FS)); gl.bindAttribLocation(pr, 0, 'aUV');
      gl.linkProgram(pr);
      if (!gl.getProgramParameter(pr, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(pr));
    } catch (e) { return 'fail shader: ' + e.message; }
    var m = meshFor(gl), U = {};
    ['uRad', 'uAspect', 'uScale', 'uFlat', 'uPersp', 'uOff', 'uBreath', 'uRot', 'uNS', 'uNR', 'uSA', 'uSC', 'uRD', 'uRP', 'uRC', 'uWave', 'uInt', 'uGlowF', 'uSat']
      .forEach(function (n) { U[n] = gl.getUniformLocation(pr, n); });
    var tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    for (var i = 0; i < texData.length; i++) texData[i] = 128;
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.LUMINANCE, 128, 16, 0, gl.LUMINANCE, gl.UNSIGNED_BYTE, texData);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    var dbg = gl.getExtension('WEBGL_debug_renderer_info');
    var renderer = dbg ? gl.getParameter(dbg.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER);
    S = { gl: gl, pr: pr, vb: m.vb, ib: m.ib, n: m.n, aUV: gl.getAttribLocation(pr, 'aUV'), U: U, tex: tex, renderer: renderer };
    initExtras(gl, sh);
    return 'ok ' + renderer + (S.P ? '' : ' (no particles)') + (S.X ? ' fx ' + S.X.kind : ' (no stage fx)') + (S.SC ? '' : ' (no scope)');
  }

  function link(gl, sh, vs, fs, attr, names) {
    var pr = gl.createProgram(), U = {};
    gl.attachShader(pr, sh(gl.VERTEX_SHADER, vs)); gl.attachShader(pr, sh(gl.FRAGMENT_SHADER, fs));
    gl.bindAttribLocation(pr, 0, attr); gl.linkProgram(pr);
    if (!gl.getProgramParameter(pr, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(pr));
    names.forEach(function (n) { U[n] = gl.getUniformLocation(pr, n); });
    return { pr: pr, U: U };
  }
  /* The particle program and its points, the stage FX's programs and what their targets can be. Either failing
     leaves the stage as it was (surfaces, no FX); init still says 'ok'. */
  function initExtras(gl, sh) {
    var COMMON = ['uRad', 'uAspect', 'uScale', 'uFlat', 'uPersp', 'uOff', 'uBreath', 'uRot', 'uNS', 'uNR', 'uSA', 'uSC', 'uRD', 'uRP', 'uRC', 'uWave'];
    try {
      var P = link(gl, sh, PVS, PFS, 'aP', COMMON.concat(['uTime', 'uScat', 'uJit', 'uCJit', 'uPt', 'uPtMax', 'uPInt', 'uMote', 'uFill', 'uPCol', 'uSoft']));
      var seed = 0x2545f491, rnd = function () { seed = (seed + 0x6d2b79f5) | 0; var t = Math.imul(seed ^ (seed >>> 15), 1 | seed); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
      var a = new Float32Array(PTS_TOTAL * 4);
      for (var i = 0; i < PTS_TOTAL; i++) { a[i * 4] = Math.asin(2 * rnd() - 1) / Math.PI + 0.5; a[i * 4 + 1] = rnd(); a[i * 4 + 2] = rnd(); a[i * 4 + 3] = rnd(); }
      P.vb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, P.vb); gl.bufferData(gl.ARRAY_BUFFER, a, gl.STATIC_DRAW);
      P.ptMax = gl.getParameter(gl.ALIASED_POINT_SIZE_RANGE)[1];
      S.P = P;
    } catch (e) { S.P = null; S.pErr = e.message; }
    try {
      var units = gl.getParameter(gl.MAX_TEXTURE_IMAGE_UNITS), K = Math.max(1, Math.min(12, units - 2)), Q = ['uSub', 'uAdd', 'uSrc'];
      var X = { K: K, mir: link(gl, sh, QVS, MIR_FS, 'aQ', Q.concat(['uCtr', 'uSide', 'uPx', 'uDiv', 'uOp', 'uTop'])),
                fb: link(gl, sh, QVS, FB_FS, 'aQ', Q.concat(['uOld', 'uCtr', 'uShift', 'uAsp', 'uFade', 'uFloor', 'uMode', 'uM', 'uHue'])),
                copy: link(gl, sh, QVS, COPY_FS, 'aQ', Q), tr: link(gl, sh, QVS, tracerFs(K), 'aQ', Q.concat(['uH', 'uW'])),
                bb: link(gl, sh, QVS, BRIGHT_FS, 'aQ', Q.concat(['uPx', 'uThr'])), bdn: link(gl, sh, QVS, DOWN_FS, 'aQ', Q.concat(['uPx'])),
                bup: link(gl, sh, QVS, UP_FS, 'aQ', Q.concat(['uPx', 'uCur', 'uW'])), bc: link(gl, sh, QVS, BLOOM_FS, 'aQ', Q.concat(['uBloom', 'uStr'])) };
      X.qb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, X.qb); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), gl.STATIC_DRAW);
      /* the feedback's own targets: half float (linear filtering), else float, else 8-bit with a floor */
      var hf = gl.getExtension('OES_texture_half_float'), hfl = gl.getExtension('OES_texture_half_float_linear');
      gl.getExtension('EXT_color_buffer_half_float');
      var ff = gl.getExtension('OES_texture_float'), ffl = gl.getExtension('OES_texture_float_linear');
      gl.getExtension('WEBGL_color_buffer_float');
      var tries = [];
      if (hf && hfl) tries.push(['half float', hf.HALF_FLOAT_OES]);
      if (ff && ffl) tries.push(['float', gl.FLOAT]);
      X.kind = 'rgba8'; X.type = gl.UNSIGNED_BYTE;
      if (window.AMMO_DEV_RUN && window.AMMO_DEV_RUN.rgba8) tries = []; // dev.html?fxfmt=rgba8: the fallback, to see it fade out
      for (var t = 0; t < tries.length; t++) {
        var tt = target(gl, 4, 4, gl.RGBA, tries[t][1]), ok = tt && gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE;
        gl.bindFramebuffer(gl.FRAMEBUFFER, null); freeTarget(gl, tt);
        if (ok) { X.kind = tries[t][0]; X.type = tries[t][1]; break; }
      }
      X.bytes = X.type === gl.UNSIGNED_BYTE ? 4 : X.type === gl.FLOAT ? 16 : 8;
      X.mm = gl.getExtension('EXT_blend_minmax'); X.R = X.mm ? 24 : K; // tracer slots
      X.w = X.h = 0; X.frame = 0;
      S.X = X;
    } catch (e) { S.X = null; S.xErr = e.message; }
    try {
      var SC = link(gl, sh, SVS, SFS, 'aS', ['uRes', 'uCol', 'uHW', 'uGW', 'uGlow']);
      SC.vb = gl.createBuffer(); SC.data = new Float32Array(0); SC.verts = 0;
      SC.mm = gl.getExtension('EXT_blend_minmax');
      S.SC = SC;
    } catch (e) { S.SC = null; S.scErr = e.message; }
  }

  /* ---- stage FX targets: a texture and its framebuffer; freed with the effect, recreated on a resize */
  function target(gl, w, h, fmt, type) {
    var tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.texImage2D(gl.TEXTURE_2D, 0, fmt, w, h, 0, fmt, type, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    var fb = gl.createFramebuffer(); gl.bindFramebuffer(gl.FRAMEBUFFER, fb);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
    return { tex: tex, fb: fb, w: w, h: h, bytes: w * h * (type === gl.UNSIGNED_BYTE ? (fmt === gl.RGB ? 3 : 4) : type === gl.FLOAT ? 16 : 8) };
  }
  function freeTarget(gl, t) { if (!t) return; if (t.fb) gl.deleteFramebuffer(t.fb); gl.deleteTexture(t.tex); }
  /** Free what an effect no longer needs: which = 'scene', 'mir', 'acc', 'ring', 'pre', 'bloom', or all of them. */
  function fxFree(which) {
    var X = S && S.X; if (!X) return;
    var gl = S.gl, all = !which;
    if (all || which === 'scene') { freeTarget(gl, X.scene); X.scene = null; }
    if (all || which === 'mir') { freeTarget(gl, X.mirT); X.mirT = null; }
    if (all || which === 'acc') { if (X.acc) { freeTarget(gl, X.acc[0]); freeTarget(gl, X.acc[1]); } X.acc = null; }
    if (all || which === 'ring') { if (X.ring) X.ring.forEach(function (r) { freeTarget(gl, r); }); X.ring = null; }
    if (all || which === 'pre') { freeTarget(gl, X.pre); X.pre = null; }
    if (all || which === 'bloom') { (X.bd || []).concat(X.bu || []).forEach(function (r) { freeTarget(gl, r); }); X.bd = X.bu = null; }
  }
  function fxBytes() {
    var X = S && S.X, b = 0; if (!X) return 0;
    [X.scene, X.mirT, X.pre].concat(X.acc || [], X.ring || [], X.bd || [], X.bu || []).forEach(function (t) { if (t) b += t.bytes; });
    return b;
  }
  function clearTarget(gl, t) { gl.bindFramebuffer(gl.FRAMEBUFFER, t.fb); gl.viewport(0, 0, t.w, t.h); gl.clearColor(0, 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT); }
  /* a 3x3 turning colors around the grey axis by a radians (the feedback's hue shift) */
  function hueMat(a) {
    var c = Math.cos(a), s = Math.sin(a), k = 1 / 3, q = Math.sqrt(k), m = new Float32Array(9);
    // R = c I + s [k]x + (1 - c) k k^T with k = (1,1,1)/sqrt(3); column-major
    var K = [[0, -q, q], [q, 0, -q], [-q, q, 0]];
    for (var col = 0; col < 3; col++) for (var row = 0; row < 3; row++) m[col * 3 + row] = (row === col ? c : 0) + s * K[row][col] + (1 - c) * k;
    return m;
  }
  /** The stage as drawn (on the screen) through the effects that are on, back onto the screen:
      copy -> [Mirror] -> [Feedback] -> [Tracer] -> screen, the last one drawing the screen itself. */
  function post(W, H, fx, opts) {
    var gl = S.gl, X = S.X, BG = [14 / 255, 14 / 255, 16 / 255], ZERO = [0, 0, 0], dt = opts.dt || 0.016, p, t;
    if (X.w !== W || X.h !== H) { fxFree(); X.w = W; X.h = H; }
    if (!fx.mirror || !fx.feedback && !fx.tracer && !fx.bloom) fxFree('mir');
    if (!fx.bloom || !fx.tracer) fxFree('pre'); // the tracer's picture for the bloom to read
    if (!fx.bloom) fxFree('bloom');
    if (!fx.feedback) fxFree('acc');
    // the tracer's layout: B frames a slot (the brightest of them), as many slots as cover n
    var trN = fx.tracer ? Math.max(1, Math.min(TRACER_MAX, fx.tracer.n, X.mm ? TRACER_MAX : X.K)) : 0, trB = Math.ceil(trN / X.R), trS = Math.ceil(trN / Math.max(1, trB));
    if (!fx.tracer || (X.ring && (X.ring.length !== trS || X.B !== trB))) fxFree('ring');
    if (!X.scene) X.scene = target(gl, W, H, gl.RGB, gl.UNSIGNED_BYTE);
    // the stage, antialiased as it is on the screen, into a texture
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, X.scene.tex);
    gl.copyTexSubImage2D(gl.TEXTURE_2D, 0, 0, 0, 0, 0, W, H);
    gl.disable(gl.BLEND);
    gl.bindBuffer(gl.ARRAY_BUFFER, X.qb); gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
    var last = fx.tracer ? 'tr' : fx.feedback ? 'fb' : 'mir', cur = X.scene.tex, sub = BG, passes = 0;
    var ctr = [((opts.offX || 0) + 1) / 2, ((opts.offY || 0) + 1) / 2], asp = W / H;
    function use(prog, to, w, h) {
      p = prog; gl.useProgram(p.pr);
      gl.bindFramebuffer(gl.FRAMEBUFFER, to ? to.fb : null); gl.viewport(0, 0, w || W, h || H);
      gl.uniform3fv(p.U.uSub, sub); gl.uniform3fv(p.U.uAdd, to ? ZERO : BG);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, cur); gl.uniform1i(p.U.uSrc, 0);
    }
    function quad() { gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4); passes++; }
    if (fx.mirror) {
      var m = fx.mirror, mirTo = last === 'mir' && !fx.bloom ? null : (X.mirT = X.mirT || target(gl, W, H, gl.RGBA, gl.UNSIGNED_BYTE));
      use(X.mir, mirTo);
      gl.uniform2f(p.U.uCtr, ctr[0], ctr[1]); gl.uniform2f(p.U.uSide, H / W, 1); gl.uniform2f(p.U.uPx, 1 / W, 1 / H);
      gl.uniform1f(p.U.uDiv, m.div); gl.uniform1f(p.U.uOp, m.op); gl.uniform1f(p.U.uTop, m.top ? 1 : 0);
      quad();
      if (mirTo) { cur = mirTo.tex; sub = ZERO; }
    }
    if (fx.feedback) {
      var f = fx.feedback;
      if (!X.acc) { X.acc = [target(gl, W, H, gl.RGBA, X.type), target(gl, W, H, gl.RGBA, X.type)]; clearTarget(gl, X.acc[0]); clearTarget(gl, X.acc[1]); }
      var s = Math.exp(f.zoom * dt), a = f.rot * dt, c = Math.cos(-a) / s, sn = Math.sin(-a) / s;
      use(X.fb, X.acc[1]);
      gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, X.acc[0].tex); gl.uniform1i(p.U.uOld, 1);
      gl.uniform2f(p.U.uCtr, ctr[0], ctr[1]); gl.uniform2f(p.U.uShift, f.dx * dt, f.dy * dt); gl.uniform1f(p.U.uAsp, asp);
      gl.uniform1f(p.U.uFade, Math.pow(f.fade, dt * 60)); gl.uniform1f(p.U.uFloor, X.type === gl.UNSIGNED_BYTE ? 1.5 / 255 : 0);
      gl.uniform1f(p.U.uMode, f.add ? 1 : 0);
      gl.uniformMatrix2fv(p.U.uM, false, new Float32Array([c, sn, -sn, c])); // column-major: R(-a) / s
      gl.uniformMatrix3fv(p.U.uHue, false, hueMat(f.hue * dt));
      quad();
      X.acc.reverse(); cur = X.acc[0].tex; sub = ZERO;
      if (last === 'fb' && !fx.bloom) { use(X.copy, null); quad(); } // with the bloom it reads the feedback's own target
    }
    if (fx.tracer) {
      var tr = fx.tracer, ns = trS, tw = Math.max(1, Math.round(W / 2)), th = Math.max(1, Math.round(H / 2));
      var trTo = fx.bloom ? (X.pre = X.pre || target(gl, W, H, gl.RGBA, gl.UNSIGNED_BYTE)) : null;
      if (!X.ring) { X.ring = []; for (t = 0; t < ns; t++) { X.ring.push(target(gl, tw, th, gl.RGBA, gl.UNSIGNED_BYTE)); X.ring[t].at = 0; X.ring[t].cnt = 0; } X.B = trB; X.head = 0; X.held = 0; X.inB = 0; }
      // hold one frame in every `gap`, at half resolution: the first of a block replaces the slot (its oldest), the
      // rest max-blend into it
      if (X.frame % Math.max(1, tr.gap) === 0) {
        var slot = X.ring[X.head];
        if (X.inB > 0) { gl.enable(gl.BLEND); gl.blendEquation(X.mm.MAX_EXT); }
        use(X.copy, slot, tw, th); quad(); gl.disable(gl.BLEND); gl.blendEquation(gl.FUNC_ADD);
        slot.at = ++X.held; slot.cnt = X.inB + 1;
        if (++X.inB >= X.B) { X.inB = 0; X.head = (X.head + 1) % ns; }
      }
      // the frame, then the slots K a pass, the newest brightest; the oldest slot fades as it leaves the n frames
      var units = [], wts = new Float32Array(X.K);
      for (t = 0; t < X.K; t++) units.push(t + 1);
      for (var c0 = 0; c0 < ns; c0 += X.K) {
        use(X.tr, trTo);
        if (c0 > 0) { gl.enable(gl.BLEND); gl.blendEquation(X.mm.MAX_EXT); }
        for (t = 0; t < X.K; t++) {
          var r = X.ring[Math.min(ns - 1, c0 + t)], age = X.held - r.at;
          gl.activeTexture(gl.TEXTURE1 + t); gl.bindTexture(gl.TEXTURE_2D, r.tex);
          wts[t] = c0 + t < ns && r.at > 0 ? Math.max(0, 1 - tr.fade * age / trN) * Math.min(1, Math.max(0, (trN - age) / r.cnt)) : 0;
        }
        gl.uniform1iv(p.U.uH, units); gl.uniform1fv(p.U.uW, wts);
        quad();
      }
      gl.disable(gl.BLEND); gl.blendEquation(gl.FUNC_ADD);
      if (trTo) { cur = trTo.tex; sub = ZERO; }
    }
    if (fx.bloom) {
      var bl = fx.bloom, lightT = cur, lightS = sub, i;
      if (!X.bd) {
        X.bd = []; X.bu = [];
        for (i = 0; i < BLOOM_LEVELS; i++) {
          var lw = Math.max(1, Math.round(W / Math.pow(2, i + 1))), lh = Math.max(1, Math.round(H / Math.pow(2, i + 1)));
          X.bd.push(target(gl, lw, lh, gl.RGBA, X.type)); if (i < BLOOM_LEVELS - 1) X.bu.push(target(gl, lw, lh, gl.RGBA, X.type));
        }
      }
      // the light above the threshold, at half size
      use(X.bb, X.bd[0], X.bd[0].w, X.bd[0].h); gl.uniform2f(p.U.uPx, 1 / W, 1 / H); gl.uniform1f(p.U.uThr, bl.thr); quad();
      // down the halvings
      sub = ZERO;
      for (i = 1; i < BLOOM_LEVELS; i++) {
        cur = X.bd[i - 1].tex; use(X.bdn, X.bd[i], X.bd[i].w, X.bd[i].h);
        gl.uniform2f(p.U.uPx, 1 / X.bd[i - 1].w, 1 / X.bd[i - 1].h); quad();
      }
      // and back up, each level plus the next smaller one by rad: sum w^k level_k, normalized
      var w = 0.35 + 0.85 * bl.rad, norm = 0, wk = 1;
      for (i = 0; i < BLOOM_LEVELS; i++) { norm += wk; wk *= w; }
      for (i = BLOOM_LEVELS - 2; i >= 0; i--) {
        var from = i === BLOOM_LEVELS - 2 ? X.bd[i + 1] : X.bu[i + 1];
        cur = from.tex; use(X.bup, X.bu[i], X.bu[i].w, X.bu[i].h);
        gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, X.bd[i].tex); gl.uniform1i(p.U.uCur, 1);
        gl.uniform2f(p.U.uPx, 1 / from.w, 1 / from.h); gl.uniform1f(p.U.uW, w); quad();
      }
      // over the picture
      cur = lightT; sub = lightS; use(X.bc, null);
      gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, X.bu[0].tex); gl.uniform1i(p.U.uBloom, 1);
      gl.uniform1f(p.U.uStr, bl.str / norm); quad();
    }
    X.frame++; X.passes = passes;
    gl.activeTexture(gl.TEXTURE0); gl.bindFramebuffer(gl.FRAMEBUFFER, null); gl.viewport(0, 0, W, H);
  }


  var hitEnv = new Float32Array(16); // per slot: a hit's kick to its particles' jitter, fading in 0.2 s

  function draw(cv, chans, opts) {
    if (!S) return;
    var w = cv.clientWidth, h = cv.clientHeight; if (!w || !h) return;
    var dpr = Math.min(2, window.devicePixelRatio || 1), W = Math.round(w * dpr), H = Math.round(h * dpr);
    if (cv.width !== W || cv.height !== H) { cv.width = W; cv.height = H; }
    if (opts.scope) scopeScene(W, H, opts.scope); else scene(W, H, w, h, chans, opts);
    var fx = opts.fx;
    if (S.X && fx && (fx.mirror || fx.feedback || fx.tracer || fx.bloom)) post(W, H, fx, opts);
    else if (S.X) { if (S.X.scene) fxFree(); S.X.passes = 0; } // all off: nothing kept, nothing drawn twice
  }

  function scene(W, H, w, h, chans, opts) {
    var gl = S.gl, U = S.U, i, j, T = opts.time, dt = opts.dt || 0.016;
    mergeNow += (opts.merge - mergeNow) * (1 - Math.exp(-dt / 0.3));
    var mg = mergeNow;

    // the waves into the texture, one row per slot
    gl.bindTexture(gl.TEXTURE_2D, S.tex);
    for (i = 0; i < chans.length; i++) if (chans[i].bytes) gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, chans[i].slot, 128, 1, gl.LUMINANCE, gl.UNSIGNED_BYTE, chans[i].bytes);

    var shells = [], glows = [], breath = [0, 0], drift = opts.drift != null ? opts.drift : T, rp = opts.ripple || { str: 0.12, spd: 2.2 };
    var pearlSurf = false, pearlPts = null, pearlFrom = 0, pdraw = [], hasP = !!S.P;
    for (i = 0; i < chans.length; i++) {
      var c = chans[i], amp = c.amp, dim = c.dim, fd = c.fade != null ? c.fade : 1; // fade: show / mute, every look goes with it
      var sw = c.swell || 0, pres = c.pres || 1, sz = c.size != null ? c.size : 1, al = c.alpha != null ? c.alpha : 1;
      // particles: the mode (Surface without the particle program), and the settings the points take
      var pm = hasP ? (c.mode || 0) : 0, he = hitEnv[c.slot & 15] = Math.max(hitEnv[c.slot & 15] * Math.exp(-dt / 0.2), c.hit ? 1 : 0);
      var pt = pm ? { n: Math.round(PTS_RANGE * Math.max(0, Math.min(1, c.pd != null ? c.pd : 0.5))), scat: c.ps || 0, cj: c.pc || 0, z: c.pz || 2.5, soft: c.pf != null ? c.pf : 0.6,
                      fill: c.pfill || 0, jit: (c.pj || 0) * (0.25 + 0.75 * Math.min(1, amp * 1.3) + 1.6 * he), col: colOf(c, 8, 62) } : null;
      if (c.look === 'shell') {
        shells.push({ row: c.slot, cyc: Math.min(10, 2 + Math.max(0, c.note - 30) / 7),
          depth: (0.05 + 0.17 * Math.min(1, amp * 1.3)) * (0.35 + 0.65 * dim) * pres * Math.max(0.2, 1 + 0.6 * sw) * fd,
          phase: -drift * (0.25 + 0.06 * i), col: colOf(c, 8, 58), k: dim * fd * (0.25 + 0.75 * Math.min(1, amp * 1.4)), amp: amp, swell: sw,
          size: sz, alpha: al, pm: pm, pt: pt });
      } else {
        if (c.look === 'ripple' || c.look === 'breath') { if (pm !== 1) pearlSurf = true; if (pm && !pearlPts) { pearlPts = pt; pearlFrom = c.slot; } }
        if (c.look === 'ripple') {
          if (c.hit && al > 0.001) {
            var az = c.hue / 180 * Math.PI, dir = [Math.sin(az) * 0.95, 0.3, Math.cos(az) * 0.95];
            var dl = Math.sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            rips.push({ dir: [dir[0] / dl, dir[1] / dl, dir[2] / dl], t0: T, str: rp.str * (0.4 + 0.6 * dim) * pres * fd * al, wid: 0.16 * sz, spd: rp.spd, life: Math.PI / rp.spd, col: colOf(c, 0, 62) });
            if (rips.length > 8) rips.shift();
          }
        } else if (c.look === 'breath') { var bk = amp * dim * fd * pres * Math.max(0.2, 1 + 0.6 * sw) * sz * al; breath[0] += 0.05 * bk; breath[1] += 0.07 * bk; }
        else if (c.look === 'glow') glows.push({ col: colOf(c, 0, 55), k: Math.min(1, amp * pres * (1 + 0.5 * sw)) * dim * fd * al, amp: amp, size: sz, pm: pm, pt: pt, slot: c.slot });
      }
    }
    if (!pearlPts) pearlSurf = true; // no ripple or breath channel in Particles: the pearl as it always was
    for (j = rips.length - 1; j >= 0; j--) if (T - rips[j].t0 > rips[j].life || T < rips[j].t0) rips.splice(j, 1);
    var RD = new Float32Array(32), RP = new Float32Array(16), RC = new Float32Array(24);
    for (j = 0; j < rips.length; j++) {
      var r = rips[j], age = T - r.t0, fade = Math.pow(1 - age / r.life, 1.5);
      RD[j * 4] = r.dir[0]; RD[j * 4 + 1] = r.dir[1]; RD[j * 4 + 2] = r.dir[2]; RD[j * 4 + 3] = age * r.spd;
      RP[j * 2] = r.str * fade; RP[j * 2 + 1] = r.wid;
      RC[j * 3] = r.col[0] * fade; RC[j * 3 + 1] = r.col[1] * fade; RC[j * 3 + 2] = r.col[2] * fade;
    }
    var ROT = rotation(-(opts.tilt != null ? opts.tilt : 0.3), opts.spin != null ? opts.spin : T * 0.1, opts.roll || 0);

    gl.viewport(0, 0, W, H);
    gl.clearColor(14 / 255, 14 / 255, 16 / 255, 1); gl.clear(gl.COLOR_BUFFER_BIT);
    gl.disable(gl.DEPTH_TEST); gl.disable(gl.CULL_FACE);
    gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE);
    gl.useProgram(S.pr);
    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, S.tex); gl.uniform1i(U.uWave, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, S.vb); gl.enableVertexAttribArray(S.aUV); gl.vertexAttribPointer(S.aUV, 2, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, S.ib);
    /* what the surface and the points share: the frame, the camera, the breath, the ripples */
    function common(Uo) {
      gl.uniform1i(Uo.uWave, 0);
      gl.uniform1f(Uo.uAspect, w / h); gl.uniform1f(Uo.uScale, opts.scale || 0.78); gl.uniform2f(Uo.uOff, opts.offX || 0, opts.offY || 0);
      gl.uniform1f(Uo.uPersp, opts.persp != null ? opts.persp : 0.22);
      gl.uniform2f(Uo.uBreath, breath[0], breath[1]);
      gl.uniform4fv(Uo.uRD, RD); gl.uniform2fv(Uo.uRP, RP); gl.uniform3fv(Uo.uRC, RC); gl.uniform1i(Uo.uNR, rips.length);
      gl.uniformMatrix3fv(Uo.uRot, false, ROT);
    }
    common(U);
    /* list: [{row, cyc, depth, phase, col, alpha}]; depth weighted by `wt` (merged: each channel's size) */
    function load(list, wt, Uo) {
      var SA = new Float32Array(24), SC = new Float32Array(24), q;
      Uo = Uo || U;
      for (q = 0; q < list.length && q < 6; q++) {
        var o = list[q];
        SA[q * 4] = o.row; SA[q * 4 + 1] = o.cyc; SA[q * 4 + 2] = o.depth * (wt ? o.size : 1); SA[q * 4 + 3] = o.phase;
        SC[q * 4] = o.col[0]; SC[q * 4 + 1] = o.col[1]; SC[q * 4 + 2] = o.col[2]; SC[q * 4 + 3] = o.alpha != null ? o.alpha : 1;
      }
      gl.uniform4fv(Uo.uSA, SA); gl.uniform4fv(Uo.uSC, SC); gl.uniform1i(Uo.uNS, Math.min(6, list.length));
    }
    /* glow: soft light inside the glass (in Particles: motes filling it instead; Both: both) */
    gl.uniform1f(U.uFlat, 1); gl.uniform1f(U.uGlowF, 1); gl.uniform1f(U.uSat, 1.2);
    for (j = 0; j < glows.length; j++) {
      var gr = 0.8 * (0.92 + 0.12 * glows[j].amp) * glows[j].size, flat = [{ row: 0, cyc: 1, depth: 0, phase: 0, col: glows[j].col }];
      if (glows[j].pm) pdraw.push({ list: flat, rad: gr, int: glows[j].k * 0.8, pt: glows[j].pt, mote: 1, range: glows[j].slot % PTS_RANGES });
      if (glows[j].pm === 1) continue;
      load(flat);
      gl.uniform1f(U.uRad, gr); gl.uniform1f(U.uInt, glows[j].k);
      gl.drawElements(gl.TRIANGLES, S.n, gl.UNSIGNED_SHORT, 0);
    }
    gl.uniform1f(U.uFlat, 0); gl.uniform1f(U.uGlowF, 0);
    if (!shells.length) {
      if (rips.length || breath[0] > 0) {
        var pearl = [{ row: 0, cyc: 1, depth: 0, phase: 0, col: Knobs.hslRgb(Knobs.PEARL.h, 20, 70) }];
        if (pearlSurf) {
          load(pearl);
          gl.uniform1f(U.uRad, 0.55); gl.uniform1f(U.uInt, 0.35); gl.uniform1f(U.uSat, 1);
          gl.drawElements(gl.TRIANGLES, S.n, gl.UNSIGNED_SHORT, 0);
        }
        if (pearlPts) { pearlPts.col = pearl[0].col; pdraw.push({ list: pearl, rad: 0.55, int: 0.6, pt: pearlPts, range: pearlFrom % PTS_RANGES }); }
      }
      points(pdraw, mg, common, load, T, H, opts);
      return;
    }
    /* nested: one shell each, inner to outer (each scaled by its size); they converge as the morph runs */
    if (mg < 0.999) {
      gl.uniform1f(U.uSat, 1.15);
      for (j = 0; j < shells.length; j++) {
        var o = shells[j], n = shells.length, rad = (n === 1 ? 0.52 : 0.34 + 0.3 * j / (n - 1)) * (0.88 + 0.2 * Math.min(1, o.amp)) * (1 + 0.04 * o.swell) * o.size;
        if (o.alpha <= 0.001) continue;
        var radNow = rad * (1 - mg) + 0.58 * mg;
        if (o.pm) pdraw.push({ list: [o], rad: radNow, int: o.k * o.alpha * (1 - mg), pt: o.pt, range: j % PTS_RANGES });
        if (o.pm === 1) continue;
        load([o]);
        gl.uniform1f(U.uRad, radNow); gl.uniform1f(U.uInt, o.k * o.alpha * (1 - mg));
        gl.drawElements(gl.TRIANGLES, S.n, gl.UNSIGNED_SHORT, 0);
      }
    }
    /* merged: one surface, every shell's wave added (weighted by its size), colored by whichever is strongest
       (and most opaque); the surface as opaque as its channels are, the louder ones counting more (a channel in
       Particles adds its wave but not its opacity: its points stand on the surface instead, in its own color) */
    if (mg > 0.001) {
      var am = 0, aw = 0, as = 0;
      for (j = 0; j < shells.length; j++) { var sj = shells[j], wj = 0.02 + Math.min(1, sj.amp); am = Math.max(am, sj.amp); aw += wj; as += wj * sj.alpha * (sj.pm === 1 ? 0 : 1); }
      var mrad = 0.58 * (0.92 + 0.12 * Math.min(1, am));
      for (j = 0; j < shells.length; j++) if (shells[j].pm && shells[j].alpha > 0.001) pdraw.push({ list: shells, wt: true, rad: mrad, int: mg * shells[j].k * shells[j].alpha, pt: shells[j].pt, range: j % PTS_RANGES });
      if (as > 0) {
        load(shells, true);
        gl.uniform1f(U.uSat, 1.5); gl.uniform1f(U.uRad, mrad); gl.uniform1f(U.uInt, mg * as / aw);
        gl.drawElements(gl.TRIANGLES, S.n, gl.UNSIGNED_SHORT, 0);
      }
    }
    points(pdraw, mg, common, load, T, H, opts);
  }

  /** The particles after the surfaces (additive, so the order does not matter): one draw per channel, a range of
      the point buffer each (so channels on one merged surface do not land on the same places), all of them
      thinned together past PTS_TOTAL. */
  function points(pdraw, mg, common, load, T, H, opts) {
    if (!pdraw.length) return;
    var gl = S.gl, P = S.P, PU = P.U, want = 0, j;
    for (j = 0; j < pdraw.length; j++) want += pdraw[j].pt.n;
    // points grow with the stage, but zoomed in only by the square root of the zoom: close up they part into dust
    var zm = opts.zoom || 1, thin = want > PTS_TOTAL ? PTS_TOTAL / want : 1, zk = H / 1440 * (opts.scale || 0.78) / 0.78 / zm * (zm > 1 ? Math.sqrt(zm) : zm);
    gl.useProgram(P.pr);
    gl.bindBuffer(gl.ARRAY_BUFFER, P.vb); gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 4, gl.FLOAT, false, 0, 0);
    common(PU);
    gl.uniform1f(PU.uFlat, 0); gl.uniform1f(PU.uTime, T % 600); gl.uniform1f(PU.uPtMax, P.ptMax);
    for (j = 0; j < pdraw.length; j++) {
      var d = pdraw[j], pt = d.pt, cnt = Math.floor(pt.n * thin);
      if (cnt < 1 || d.int <= 0.0005) continue;
      load(d.list, d.wt, PU);
      gl.uniform1f(PU.uRad, d.rad); gl.uniform1f(PU.uPInt, Math.min(1.5, d.int * 1.5)); gl.uniform1f(PU.uMote, d.mote ? 1 : 0);
      gl.uniform1f(PU.uScat, pt.scat); gl.uniform1f(PU.uJit, pt.jit); gl.uniform1f(PU.uCJit, pt.cj); gl.uniform1f(PU.uFill, pt.fill);
      gl.uniform1f(PU.uPt, pt.z * zk); gl.uniform1f(PU.uSoft, pt.soft); gl.uniform3fv(PU.uPCol, pt.col);
      gl.drawArrays(gl.POINTS, d.range * PTS_RANGE, cnt);
    }
  }

  /** The scope's frame (see the head of the file): the lanes' strips into one buffer, then a draw call per line. */
  var scXs = new Float32Array(4 * 128 + 1), scYs = new Float32Array(4 * 128 + 1);
  function scopeScene(W, H, o) {
    var gl = S.gl, SC = S.SC, lanes = o.lanes || [], cyc = Math.max(1, Math.min(4, o.cycles || 1)), N = cyc * 128, px = H * (o.unit || 1 / 1440);
    gl.viewport(0, 0, W, H);
    gl.clearColor(14 / 255, 14 / 255, 16 / 255, 1); gl.clear(gl.COLOR_BUFFER_BIT);
    if (!SC) return;
    var x0 = o.x0 * W, x1 = o.x1 * W, hw = Math.max(0.5, 0.5 * (o.lw || 2) * px), glow = Math.max(0, Math.min(1, o.glow || 0));
    var gw = glow > 0.002 ? hw + (3 + 15 * glow) * px : 0, ext = hw + (gw ? 2.2 * gw : 0) + 1, gk = 0.55 * Math.sqrt(glow);
    var nGrid = o.grid ? 4 * cyc + 1 + lanes.length * 2 : 0, need = (lanes.length * (N + 1) * 2 + (lanes.length + nGrid) * 4) * 3;
    if (SC.data.length < need) SC.data = new Float32Array(need + 4096);
    var D = SC.data, at = 0, draws = [], i, j;
    /* a strip round the polyline xs, ys (n points), half as wide as r: each point pushed out both ways along the
       mitre of its two segments (at most 2.9 times r, at a hairpin) */
    function strip(n, r, col, h, g, gl0) {
      var first = at / 3;
      for (var q = 0; q < n; q++) {
        var a0 = q > 0 ? q - 1 : 0, a1 = q > 0 ? q : 1, b0 = q < n - 1 ? q : n - 2, b1 = q < n - 1 ? q + 1 : n - 1;
        var ax = scXs[a1] - scXs[a0], ay = scYs[a1] - scYs[a0], bx = scXs[b1] - scXs[b0], by = scYs[b1] - scYs[b0];
        var la = Math.sqrt(ax * ax + ay * ay) || 1, lb = Math.sqrt(bx * bx + by * by) || 1;
        var n1x = -ay / la, n1y = ax / la, mx = n1x - by / lb, my = n1y + bx / lb, ml = Math.sqrt(mx * mx + my * my);
        if (ml < 1e-3) { mx = n1x; my = n1y; } else { mx /= ml; my /= ml; }
        var kk = r / Math.max(0.35, mx * n1x + my * n1y);
        D[at++] = scXs[q] + mx * kk; D[at++] = scYs[q] + my * kk; D[at++] = r;
        D[at++] = scXs[q] - mx * kk; D[at++] = scYs[q] - my * kk; D[at++] = -r;
      }
      draws.push({ first: first, n: n * 2, col: col, hw: h, gw: g, gk: gl0 });
    }
    function line(xa, ya, xb, yb, col) { scXs[0] = xa; scYs[0] = ya; scXs[1] = xb; scYs[1] = yb; strip(2, 1.5, col, 0.5, 0, 0); }
    // the grid: a line at each quarter period (brighter at each period) over the lanes, and the lanes' edges
    if (o.grid && lanes.length) {
      var gt = lanes[0].top * H, gb = lanes[lanes.length - 1].bot * H;
      for (j = 0; j <= 4 * cyc; j++) { var gv = j % 4 ? 0.1 : 0.2, gx = x0 + (x1 - x0) * j / (4 * cyc); line(gx, gt, gx, gb, [gv, gv, gv * 1.08]); }
      for (i = 0; i < (o.overlay ? 1 : lanes.length); i++) { line(x0, lanes[i].top * H, x1, lanes[i].top * H, [0.13, 0.13, 0.14]); line(x0, lanes[i].bot * H, x1, lanes[i].bot * H, [0.13, 0.13, 0.14]); }
    }
    if (o.overlay && lanes.length) line(x0, lanes[0].mid * H, x1, lanes[0].mid * H, [0.16, 0.16, 0.17]); // overlaid: one centre line, grey
    for (i = 0; i < lanes.length; i++) {
      var L = lanes[i], c = L.col, k = L.k, mid = L.mid * H, half = L.half * H, top = L.top * H, bot = L.bot * H, A = L.amp * half;
      if (!o.overlay) line(x0, mid, x1, mid, [c[0] * 0.2 * k, c[1] * 0.2 * k, c[2] * 0.2 * k]); // the centre line
      for (j = 0; j <= N; j++) {
        scXs[j] = x0 + (x1 - x0) * j / N;
        scYs[j] = Math.max(top, Math.min(bot, mid - L.wave[j & 127] * A));
      }
      strip(N + 1, ext, [c[0] * k, c[1] * k, c[2] * k], hw, gw, gk);
    }
    SC.verts = at / 3;
    gl.useProgram(SC.pr);
    gl.bindBuffer(gl.ARRAY_BUFFER, SC.vb); gl.bufferData(gl.ARRAY_BUFFER, D.subarray(0, at), gl.STREAM_DRAW);
    gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);
    gl.uniform2f(SC.U.uRes, W, H);
    gl.disable(gl.DEPTH_TEST); gl.disable(gl.CULL_FACE); gl.enable(gl.BLEND);
    if (SC.mm) gl.blendEquation(SC.mm.MAX_EXT); else { gl.blendEquation(gl.FUNC_ADD); gl.blendFunc(gl.ONE, gl.ONE); }
    for (i = 0; i < draws.length; i++) {
      var d = draws[i];
      gl.uniform3fv(SC.U.uCol, d.col); gl.uniform1f(SC.U.uHW, d.hw); gl.uniform1f(SC.U.uGW, d.gw); gl.uniform1f(SC.U.uGlow, d.gk);
      gl.drawArrays(gl.TRIANGLE_STRIP, d.first, d.n);
    }
    gl.blendEquation(gl.FUNC_ADD); gl.blendFunc(gl.SRC_ALPHA, gl.ONE);
  }
  /** What the scope drew last: {verts} (the stats overlay), or null before it ever drew. */
  function scopeInfo() { return S && S.SC ? { verts: S.SC.verts } : null; }

  /** What the stage FX hold and draw: {kind, mb, passes, maxTracer} (dev overlay, the docs). */
  function fxInfo() {
    var X = S && S.X; if (!X) return null;
    return { kind: X.kind, mb: fxBytes() / 1048576, passes: X.passes || 0, maxTracer: X.K, ptMax: S.P ? S.P.ptMax : 0 };
  }
  /** Wait for the GPU (a one-pixel read): for timing in a dev run only. */
  function sync() { if (S) { var px = new Uint8Array(4); S.gl.readPixels(0, 0, 1, 1, S.gl.RGBA, S.gl.UNSIGNED_BYTE, px); } }
  /** GPU time of what is drawn between begin and end (EXT_disjoint_timer_query), for a dev run: begin() -> a query or
      null; result(q) -> ms, or -1 while not ready (it is ready only after the page returns to the browser). */
  var TQ;
  function gpuBegin() {
    if (!S) return null;
    if (TQ === undefined) TQ = S.gl.getExtension('EXT_disjoint_timer_query') || null;
    if (!TQ) return null;
    var q = TQ.createQueryEXT(); TQ.beginQueryEXT(TQ.TIME_ELAPSED_EXT, q); return q;
  }
  function gpuEnd() { if (TQ) TQ.endQueryEXT(TQ.TIME_ELAPSED_EXT); }
  function gpuResult(q) {
    if (!TQ.getQueryObjectEXT(q, TQ.QUERY_RESULT_AVAILABLE_EXT)) return -1;
    var ns = TQ.getQueryObjectEXT(q, TQ.QUERY_RESULT_EXT); TQ.deleteQueryEXT(q);
    return S.gl.getParameter(TQ.GPU_DISJOINT_EXT) ? -2 : ns / 1e6;
  }

  return { init: init, draw: draw, fxInfo: fxInfo, scopeInfo: scopeInfo, sync: sync, gpuBegin: gpuBegin, gpuEnd: gpuEnd, gpuResult: gpuResult, TRACER_MAX: TRACER_MAX, status: function () { return S ? S.renderer : null; } };
})();
