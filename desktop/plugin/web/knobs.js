/* Ammonite Desktop: the knob painter and the faces. The face
   is a screen showing what the knob makes, in the voice's color; the value
   ring around it; the value text under the knob, never over the face.

   Knobs.draw(canvas, spec, st): spec = {kind, fmt, min, max, steps, def}
   st = {v (real value), hue, sat, amp, dragging, hover, time, beat, wave
   (Float32Array or null), ch (the channel's values for filter faces),
   step, len, mod ({rings: [{r, lo, hi, at, h, s, l}], live, hot}: one ring
   per route on it in its source's colour, its reach lo..hi and where that
   source has it now (at, -1 if not known), 0..1 of the range; live where
   the engine has it with every route added (-1 if not known); hot the
   route whose tag is hovered or dragged), assign (1: the source being
   assigned can take it, 2: it already does), assignTone ({h, s, l}: that
   source's colour), text (in place of the value), textCol (its colour)}.
   Colors: Knobs.hsla, Knobs.hslRgb, Knobs.hueOf. */
var Knobs = (function () {
  'use strict';
  var TAU = Math.PI * 2;
  var SANS = '"Barlow Semi Condensed", "Segoe UI", sans-serif';
  var MONO = '"JetBrains Mono", Consolas, monospace';
  var PEARL = { h: 38, s: 30 };
  var NOTES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  // the step divisions, longest first (engine/Clock.hpp kArpDivBeats), and their length in beats
  var DIVN = ['4/1', '2/1', '1/1', '1/2.', '1/2', '1/4.', '1/4', '1/4T', '1/8.', '1/8', '1/8T', '1/16', '1/16T', '1/32'];
  var DIVB = [16, 8, 4, 3, 2, 1.5, 1, 2 / 3, 0.75, 0.5, 1 / 3, 0.25, 1 / 6, 0.125];

  function clamp(x, a, b) { return x < a ? a : x > b ? b : x; }
  function hsla(h, s, l, a) { return 'hsla(' + h.toFixed(1) + ',' + s + '%,' + l + '%,' + clamp(a, 0, 1).toFixed(3) + ')'; }
  function hslRgb(h, s, l) {
    h = ((h % 360) + 360) % 360 / 360; s /= 100; l /= 100;
    var q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q, out = [0, 0, 0];
    for (var i = 0; i < 3; i++) {
      var t = h + (1 - i) / 3; t -= Math.floor(t);
      out[i] = t < 1 / 6 ? p + (q - p) * 6 * t : t < 0.5 ? q : t < 2 / 3 ? p + (q - p) * (2 / 3 - t) * 6 : p;
    }
    return out;
  }
  function hueOf(pc) { return ((((pc % 12) + 12) % 12) * 7 % 12) * 30; } // circle of fifths

  function arc(ctx, x, y, r, a, b, col, lw) {
    if (b - a < 0.002 || r <= 0) return;
    ctx.beginPath(); ctx.arc(x, y, r, a, b); ctx.strokeStyle = col; ctx.lineWidth = lw; ctx.stroke();
  }
  function dot(ctx, x, y, r, col) { ctx.beginPath(); ctx.arc(x, y, Math.max(0.1, r), 0, TAU); ctx.fillStyle = col; ctx.fill(); }
  function text(ctx, s, x, y, font, col, align) {
    ctx.font = font; ctx.fillStyle = col; ctx.textAlign = align || 'left'; ctx.textBaseline = 'middle'; ctx.fillText(s, x, y);
  }
  function neon(ctx, path, h, s, w, k) {
    if (k <= 0.01) return;
    ctx.save();
    ctx.globalCompositeOperation = 'lighter'; ctx.lineJoin = 'round'; ctx.lineCap = 'round';
    ctx.strokeStyle = hsla(h, s, 55, 0.06 * k); ctx.lineWidth = w * 7; ctx.stroke(path);
    ctx.strokeStyle = hsla(h, s, 58, 0.32 * k); ctx.lineWidth = w * 2.4; ctx.stroke(path);
    ctx.strokeStyle = hsla(h, Math.min(s, 60), 88, 0.85 * k); ctx.lineWidth = w * 0.75; ctx.stroke(path);
    ctx.restore();
  }
  function glowDot(ctx, x, y, r, h, s, k) {
    ctx.save(); ctx.globalCompositeOperation = 'lighter';
    dot(ctx, x, y, r * 3, hsla(h, s, 55, 0.12 * k));
    dot(ctx, x, y, r * 1.6, hsla(h, s, 60, 0.4 * k));
    dot(ctx, x, y, r * 0.7, hsla(h, Math.min(s, 50), 92, 0.95 * k));
    ctx.restore();
  }

  /* ---- value text per format */
  function fmt(f, v, spec) {
    var x = spec ? (v - spec.min) / Math.max(1e-9, spec.max - spec.min) : v, s;
    switch (f) {
      case 'pct': return Math.round(x * 100) + '%';
      case 'bip': s = Math.round(v * 100); return (s > 0 ? '+' : '') + s + '%';
      case 'angle': return Math.round(v) + '°';
      case 'times': return v.toFixed(2) + 'x';
      case 'hz': s = 20 * Math.pow(1000, x); return s >= 1000 ? (s / 1000).toFixed(1) + ' kHz' : Math.round(s) + ' Hz';
      case 'att': s = 0.001 * Math.pow(2, 12 * x); return s >= 1 ? s.toFixed(2) + ' s' : Math.round(s * 1000) + ' ms';
      case 'dec': s = 0.005 * Math.pow(2, 9.64 * x); return s >= 1 ? s.toFixed(2) + ' s' : Math.round(s * 1000) + ' ms';
      case 'pdec': s = 0.005 * Math.pow(2, 7.64 * x); return s >= 1 ? s.toFixed(2) + ' s' : Math.round(s * 1000) + ' ms';
      case 'glide': s = x * x; return s < 0.005 ? 'off' : Math.round(s * 1000) + ' ms';
      case 'cents': return Math.round(v) + ' ct';
      case 'scents': s = Math.round(v); return (s > 0 ? '+' : '') + s + ' ct';
      // the LFOs (engine/Mod.hpp): Free RATE 0.02 .. 20 Hz, PHASE as an angle, a stepped value by its name
      case 'lhz': s = 0.02 * Math.pow(2, 9.966 * x); return (s < 0.1 ? s.toFixed(3) : s < 1 ? s.toFixed(2) : s.toFixed(1)) + ' Hz';
      case 'deg360': return Math.round(x * 360) + '°';
      case 'name': return spec && spec.names ? spec.names[clamp(Math.round(v) - spec.min, 0, spec.names.length - 1)] : String(Math.round(v));
      case 'bpm': return Math.round(v) + ' BPM';
      case 'semi': s = Math.round(v * 12); return (s > 0 ? '+' : '') + s + ' st';
      case 'oct4': s = (v * 4).toFixed(1); return (v > 0 ? '+' : '') + s + ' oct';
      case 'int': return String(Math.round(v));
      case 'sint': s = Math.round(v); return (s > 0 ? '+' : '') + s;
      case 'octs': s = Math.round(v); return (s > 0 ? '+' : '') + s + ' oct';
      case 'range': return Math.round(v) + ' oct';
      case 'steps': return Math.round(v) + ' steps';
      case 'deg': return '+' + Math.round(v);
      case 'note': return NOTES[((Math.round(v) % 12) + 12) % 12];
      case 'div': return DIVN[clamp(Math.round(v), 0, DIVN.length - 1)];
      case 'type': return x < 0.33 ? 'LP' : x < 0.66 ? 'BP' : 'HP';
      // the effects' curves (engine/Fx.hpp)
      case 'chz': s = 0.08 * Math.pow(40, x); return (s < 1 ? s.toFixed(2) : s.toFixed(1)) + ' Hz';   // Chorus, Ensemble
      case 'vhz': s = 0.05 * Math.pow(2, 7 * x); return (s < 1 ? s.toFixed(2) : s.toFixed(1)) + ' Hz'; // Vibrato
      case 'dhz': s = 12000 * Math.pow(500 / 12000, x); return s >= 1000 ? (s / 1000).toFixed(1) + ' kHz' : Math.round(s) + ' Hz';
      case 'rhz': s = 18000 * Math.pow(1200 / 18000, x); return (s / 1000).toFixed(1) + ' kHz';
      case 'sec': s = 0.2 * Math.pow(100, x); return (s < 10 ? s.toFixed(1) : Math.round(s)) + ' s';
      case 'ms': return Math.round(250 * x) + ' ms';
      case 'db': return v.toFixed(1) + ' dB';
      case 'px': return v.toFixed(1) + ' px';
      case 'frames': s = Math.round(v); return s === 1 ? 'every frame' : 'every ' + s;
      default: return Math.round(x * 100) + '';
    }
  }

  /* ---- the faces */
  function glyph(ctx, kind, cx, cy, r, x, h, s, st) {
    var lw = Math.max(1, r / 20), k = 0.55 + 0.45 * st.amp, p, i, t, T = st.time || 0;
    function line(fn, steps) {
      var q = new Path2D(); steps = steps || 48;
      for (var j = 0; j <= steps; j++) { var u = j / steps, y = fn(u); if (j) q.lineTo(cx - r + 2 * r * u, y); else q.moveTo(cx - r + 2 * r * u, y); }
      return q;
    }
    function fillUnder(q, col) {
      var fq = new Path2D(q); fq.lineTo(cx + r, cy + r); fq.lineTo(cx - r, cy + r); fq.closePath();
      var gr = ctx.createLinearGradient(0, cy - r, 0, cy + r);
      gr.addColorStop(0, col); gr.addColorStop(1, hsla(h, s, 50, 0));
      ctx.fillStyle = gr; ctx.fill(fq);
    }
    function curve(fc, q, mode) {
      return line(function (u) {
        var fq = 20 * Math.pow(1000, u), xx = fq / fc, re = 1 - xx * xx, im = xx / q, den = Math.sqrt(re * re + im * im);
        var m = mode < 0.5 ? (1 / den) * (1 - mode * 2) + (xx / q / den) * (mode * 2) : (xx / q / den) * (2 - mode * 2) + (xx * xx / den) * (mode * 2 - 1);
        var db = 20 * Math.log(Math.max(m, 1e-4)) / Math.LN10;
        return cy - r * 0.05 - clamp(db, -30, 15) / 30 * r * 0.8;
      });
    }
    function synthWave(ph, sh) {
      var pp = ph / TAU; pp -= Math.floor(pp);
      var tri = pp < 0.25 ? 4 * pp : pp < 0.75 ? 2 - 4 * pp : 4 * pp - 4, saw = pp < 0.5 ? 2 * pp : 2 * pp - 2, sq = Math.tanh(Math.sin(ph) * 5);
      var W = [Math.sin(ph), tri, saw, sq], xx = clamp(sh, 0, 1) * 3, ii = Math.min(2, Math.floor(xx)), f = xx - ii;
      return W[ii] * (1 - f) + W[ii + 1] * f;
    }
    var ch = st.ch || {};
    switch (kind) {
      case 'wave':
        if (st.wave) {
          p = line(function (u) { var ph = (u * 1.5 + T * 0.25) % 1, idx = Math.floor(ph * st.wave.length) % st.wave.length; return cy - st.wave[idx] * r * 0.55; }, 96);
        } else {
          p = line(function (u) { return cy - synthWave(u * TAU * 1.5 + T * 2, x) * r * 0.55; });
        }
        neon(ctx, p, h, s, lw, k); break;
      case 'filter': case 'reso': case 'type': {
        var cut = kind === 'filter' ? x : (ch.cutoff != null ? ch.cutoff : 0.6);
        var rs = kind === 'reso' ? x : (ch.reso != null ? ch.reso : 0.2);
        var mode = kind === 'type' ? x : (ch.type != null ? ch.type : 0);
        p = curve(20 * Math.pow(1000, kind === 'type' ? 0.55 : cut), 0.6 + rs * (kind === 'reso' ? 9 : 6), mode);
        fillUnder(p, hsla(h, s, 55, 0.22 * k)); neon(ctx, p, h, s, lw, k); break;
      }
      case 'detune': {
        var f2 = 1 + x * 0.22;
        p = line(function (u) { return cy - Math.sin(u * TAU * 2 + T * 3) * r * 0.45; });
        neon(ctx, p, h, s, lw * 0.9, k * 0.9);
        p = line(function (u) { return cy - Math.sin(u * TAU * 2 * f2 + T * 3 * f2) * r * 0.45; });
        neon(ctx, p, h + 40, s, lw * 0.9, k * 0.9); break;
      }
      case 'drive': {
        var gn = 1 + x * 8;
        p = line(function (u) { return cy - Math.tanh(Math.sin(u * TAU * 1.5 + T * 2) * gn) / Math.tanh(gn) * r * 0.55; });
        neon(ctx, p, h, s, lw, k); break;
      }
      case 'density': case 'chance': {
        var stp = Math.floor((st.beat || 0) * 4), cols = kind === 'chance' ? 1 : 4;
        for (i = 0; i < cols * cols; i++) {
          var gx = cx + ((i % cols) - (cols - 1) / 2) * r * 0.42, gy = cy + (Math.floor(i / cols) - (cols - 1) / 2) * r * 0.42;
          var hh = Math.sin((i * 7.13 + stp * 1.31) * 127.1 + 311.7) * 43758.5453; hh -= Math.floor(hh);
          var lit = hh < x;
          if (kind === 'chance') { gx = cx; gy = cy; }
          if (lit) glowDot(ctx, gx, gy, kind === 'chance' ? r * 0.18 : r * 0.07, h, s, k);
          else dot(ctx, gx, gy, kind === 'chance' ? r * 0.12 : r * 0.05, 'rgba(255,255,255,0.12)');
        }
        if (kind === 'chance') arc(ctx, cx, cy, r * 0.55, -Math.PI / 2, -Math.PI / 2 + TAU * x, hsla(h, s, 60, 0.5), 1.5);
        break;
      }
      case 'div': {
        var rate = 1 / DIVB[clamp(Math.round(st.v), 0, DIVB.length - 1)], cnt = 8;
        var b = st.beat || 0, cur = Math.floor(b * rate) % cnt, fr = b * rate - Math.floor(b * rate);
        for (i = 0; i < cnt; i++) {
          var a = -Math.PI / 2 + i * TAU / cnt, px = cx + Math.cos(a) * r * 0.62, py = cy + Math.sin(a) * r * 0.62;
          if (i === cur) glowDot(ctx, px, py, r * 0.09 * (1.4 - fr * 0.5), h, s, k * (1 - fr * 0.4));
          else dot(ctx, px, py, r * 0.05, 'rgba(255,255,255,0.14)');
        }
        break;
      }
      case 'len': {
        var nn = Math.max(1, Math.round(st.v)), sg = TAU / nn, curStep = st.step != null ? st.step % nn : -1;
        for (i = 0; i < nn; i++) {
          var a0 = -Math.PI / 2 + i * sg + 0.08, a1 = a0 + sg - 0.16;
          if (i === curStep) { p = new Path2D(); p.arc(cx, cy, r * 0.62, a0, Math.max(a0 + 0.01, a1)); neon(ctx, p, h, s, lw * 1.2, k); }
          else arc(ctx, cx, cy, r * 0.62, a0, Math.max(a0 + 0.01, a1), hsla(h, s, 58, 0.45), lw * 1.4);
        }
        break;
      }
      case 'range': {
        var ro = Math.max(1, Math.round(st.v));
        for (i = 0; i < 3; i++) {
          p = new Path2D(); p.arc(cx, cy, r * (0.25 + i * 0.25), 0, TAU);
          if (i < ro) neon(ctx, p, h + i * 25, s, lw * 0.8, k * (1 - i * 0.2)); else { ctx.strokeStyle = 'rgba(255,255,255,0.08)'; ctx.lineWidth = 1; ctx.stroke(p); }
        }
        break;
      }
      case 'pan': {
        p = new Path2D(); p.moveTo(cx - r * 0.8, cy); p.lineTo(cx + r * 0.8, cy);
        ctx.strokeStyle = 'rgba(255,255,255,0.12)'; ctx.lineWidth = 1; ctx.stroke(p);
        glowDot(ctx, cx + (x - 0.5) * 1.6 * r * 0.8, cy, r * 0.13, h, s, k);
        break;
      }
      case 'env': case 'attack': {
        var tau = 0.06 + x * 0.9;
        p = line(function (u) {
          return kind === 'env' ? cy + r * 0.55 - Math.exp(-u / tau) * r * 1.1 : cy + r * 0.55 - (1 - Math.exp(-u / (0.05 + x * 0.6))) * r * 1.1;
        });
        fillUnder(p, hsla(h, s, 55, 0.18 * k)); neon(ctx, p, h, s, lw, k);
        if (kind === 'env' && st.amp > 0.02) {
          var tp = -Math.log(Math.max(st.amp, 0.001)) * tau;
          if (tp <= 1) glowDot(ctx, cx - r + 2 * r * tp, cy + r * 0.55 - st.amp * r * 1.1, r * 0.08, h, s, 1);
        }
        break;
      }
      case 'bipenv': {
        // a bipolar amount: the envelope drawn up or down from the middle
        var amt = x * 2 - 1;
        p = line(function (u) { return cy - amt * Math.exp(-u / 0.35) * r * 0.75; });
        neon(ctx, p, h, s, lw, k * (0.3 + 0.7 * Math.abs(amt)));
        p = new Path2D(); p.moveTo(cx - r, cy); p.lineTo(cx + r, cy); ctx.strokeStyle = 'rgba(255,255,255,0.1)'; ctx.lineWidth = 1; ctx.stroke(p);
        break;
      }
      case 'lfo': {
        var spd = 0.3 + x * 4;
        p = line(function (u) { return cy - Math.sin(u * TAU + T * spd) * r * 0.5; });
        neon(ctx, p, h, s, lw, k); break;
      }
      case 'tilt': {
        // a ring on the ground seen from the knob's angle: a line at 0, round from above or below, the far half dim
        var el = (x * 2 - 1) * Math.PI / 2, ry = Math.max(0.02, Math.abs(Math.sin(el))) * r * 0.6;
        p = new Path2D(); p.ellipse(cx, cy, r * 0.6, ry, 0, el >= 0 ? Math.PI : 0, el >= 0 ? TAU : Math.PI);
        ctx.strokeStyle = hsla(h, s, 58, 0.35); ctx.lineWidth = lw; ctx.stroke(p);
        p = new Path2D(); p.ellipse(cx, cy, r * 0.6, ry, 0, el >= 0 ? 0 : Math.PI, el >= 0 ? Math.PI : TAU);
        neon(ctx, p, h, s, lw, k); break;
      }
      case 'size': {
        // a circle as large as the value (Size, Zoom), the middle marked
        p = new Path2D(); p.arc(cx, cy, r * 0.42, 0, TAU); ctx.strokeStyle = 'rgba(255,255,255,0.1)'; ctx.lineWidth = 1; ctx.stroke(p);
        p = new Path2D(); p.arc(cx, cy, r * (0.14 + 0.56 * x), 0, TAU); neon(ctx, p, h, s, lw, k); break;
      }
      case 'persp': {
        // a floor going away: its far edge shrinks as the perspective grows
        var far = 0.75 - 0.55 * x;
        p = new Path2D(); p.moveTo(cx - r * 0.75, cy + r * 0.4); p.lineTo(cx + r * 0.75, cy + r * 0.4); p.lineTo(cx + r * far, cy - r * 0.4); p.lineTo(cx - r * far, cy - r * 0.4); p.closePath();
        neon(ctx, p, h, s, lw, k);
        p = new Path2D(); p.moveTo(cx, cy + r * 0.4); p.lineTo(cx, cy - r * 0.4); ctx.strokeStyle = hsla(h, s, 58, 0.35); ctx.lineWidth = 1; ctx.stroke(p);
        break;
      }
      case 'note': {
        var nm = NOTES[((Math.round(st.v) % 12) + 12) % 12];
        ctx.save(); ctx.globalCompositeOperation = 'lighter';
        text(ctx, nm, cx, cy + 1, '600 ' + (r * 0.85).toFixed(1) + 'px ' + SANS, hsla(hueOf(Math.round(st.v)), 90, 66, 0.95), 'center');
        ctx.restore(); break;
      }
      case 'phase': {
        // where in its cycle the LFO starts: a dot on the circle, a quarter of the way round at 90°
        p = new Path2D(); p.arc(cx, cy, r * 0.6, 0, TAU); ctx.strokeStyle = 'rgba(255,255,255,0.1)'; ctx.lineWidth = 1; ctx.stroke(p);
        p = new Path2D(); p.arc(cx, cy, r * 0.6, -Math.PI / 2, -Math.PI / 2 + Math.max(0.01, x * TAU)); neon(ctx, p, h, s, lw * 0.9, k * 0.8);
        glowDot(ctx, cx + Math.cos(-Math.PI / 2 + x * TAU) * r * 0.6, cy + Math.sin(-Math.PI / 2 + x * TAU) * r * 0.6, r * 0.11, h, s, k);
        break;
      }
      case 'tempo': {
        var bb = st.beat || 0, phs = bb - Math.floor(bb);
        p = new Path2D(); p.arc(cx, cy, r * 0.6, 0, TAU); ctx.strokeStyle = 'rgba(255,255,255,0.1)'; ctx.lineWidth = 1; ctx.stroke(p);
        glowDot(ctx, cx + Math.cos(-Math.PI / 2 + phs * TAU) * r * 0.6, cy + Math.sin(-Math.PI / 2 + phs * TAU) * r * 0.6, r * 0.12 * (1.3 - phs * 0.5), h, s, k);
        break;
      }
      case 'swing': {
        var sw = x / 3;
        for (i = 0; i < 4; i++) {
          var xx0 = cx - r * 0.8 + (i % 2 === 0 ? (i / 2) * r * 0.8 : (Math.floor(i / 2) + 0.5 + sw * 1.5) * r * 0.8);
          glowDot(ctx, xx0, cy, r * (i % 2 ? 0.08 : 0.12), h, s, k * (i % 2 ? 0.7 : 1));
        }
        break;
      }
      default: {
        var top = cy + r - 2 * r * x * (0.85 + 0.15 * st.amp);
        p = new Path2D();
        for (i = 0; i <= 30; i++) { t = i / 30; var yy = top + Math.sin(t * TAU * 1.5 + T * 2.5) * r * 0.05; if (i) p.lineTo(cx - r + 2 * r * t, yy); else p.moveTo(cx - r, yy); }
        var fl = new Path2D(p); fl.lineTo(cx + r, cy + r); fl.lineTo(cx - r, cy + r); fl.closePath();
        var lg = ctx.createLinearGradient(0, top, 0, cy + r);
        lg.addColorStop(0, hsla(h, s, 60, 0.45 * k)); lg.addColorStop(1, hsla(h - 30, s, 40, 0.1));
        ctx.fillStyle = lg; ctx.fill(fl); neon(ctx, p, h, s, lw * 0.8, k);
      }
    }
  }

  /* ---- the knob */
  function draw(cv, spec, st) {
    var cssW = cv.clientWidth, cssH = cv.clientHeight;
    if (!cssW || !cssH) return;
    var dpr = Math.min(2, (window.devicePixelRatio || 1) * (st.scale || 1));
    var PW = Math.round(cssW * dpr), PH = Math.round(cssH * dpr);
    if (cv.width !== PW || cv.height !== PH) { cv.width = PW; cv.height = PH; }
    var ctx = cv.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, cssW, cssH);
    var W = Math.min(cssW, cssH - 18), cx = cssW / 2, cy = W / 2, R = W / 2 - 1.5;
    var x = clamp(spec.log ? Math.log(st.v / spec.min) / Math.log(spec.max / spec.min) : (st.v - spec.min) / Math.max(1e-9, spec.max - spec.min), 0, 1);
    var from = spec.fmt === 'bip' || spec.fmt === 'semi' || spec.fmt === 'oct4' || spec.fmt === 'sint' || spec.fmt === 'octs' || spec.fmt === 'angle' || spec.fmt === 'scents' ? clamp((0 - spec.min) / Math.max(1e-9, spec.max - spec.min), 0, 1) : 0;
    var h = st.hue, s = st.sat, A0 = Math.PI * 0.75, SW = Math.PI * 1.5;
    // a ring per route outside the value ring (up to 3: see modRings): the knob gives up room for the second and third
    var md = st.mod, rings = md && md.rings && md.rings.length ? md.rings : null, nr = rings ? Math.min(3, rings.length) : 0;
    var big = W >= 70, tw = Math.max(2, W * 0.036), gap = tw * 1.3, rv = R - (big ? 7 : 5.5) - Math.max(0, nr - 1) * gap, rf = rv - tw / 2 - (big ? 5 : 3.5);
    ctx.lineCap = 'round';
    arc(ctx, cx, cy, rv, A0, A0 + SW, 'rgba(255,255,255,0.07)', tw);
    var lo = Math.min(from, x), hi = Math.max(from, x);
    if (hi - lo > 0.003) {
      var hot = hsla(h, s, 66, 1), dim = hsla(h - 28, s, 42, 0.55), stroke = hot;
      if (ctx.createConicGradient) {
        var gr = ctx.createConicGradient(A0, cx, cy), oF = SW * from / TAU, oB = SW * x / TAU;
        if (oB >= oF) { gr.addColorStop(oF, dim); gr.addColorStop(oB, hot); } else { gr.addColorStop(oB, hot); gr.addColorStop(oF, dim); }
        stroke = gr;
      }
      ctx.save(); ctx.globalCompositeOperation = 'lighter';
      arc(ctx, cx, cy, rv, A0 + SW * lo, A0 + SW * hi, hsla(h, s, 58, 0.16 + (st.dragging ? 0.12 : 0)), tw * 3.2);
      ctx.restore();
      arc(ctx, cx, cy, rv, A0 + SW * lo, A0 + SW * hi, stroke, tw);
    }
    var ab = A0 + SW * x;
    glowDot(ctx, cx + Math.cos(ab) * rv, cy + Math.sin(ab) * rv, tw * 0.75, h, s, 1);
    // modulation: a ring per route in its source's colour, its reach and where that source has it now
    if (rings) modRings(ctx, cx, cy, rv + tw * 1.45, gap, tw * 0.62, rings, md.hot, A0, SW);
    if (st.assign) { // assigning: a dashed ring on what the source can take, a whole one on what it already moves, in its colour
      var at = st.assignTone || { h: PEARL.h, s: 40, l: 82 };
      ctx.save(); ctx.setLineDash(st.assign === 2 ? [] : [3, 4]);
      arc(ctx, cx, cy, R - 0.5, 0, TAU, hsla(at.h, at.s, at.l, st.assign === 2 ? 0.95 : 0.7), 1.4);
      ctx.restore();
    }
    var fg = ctx.createRadialGradient(cx - rf * 0.3, cy - rf * 0.4, rf * 0.1, cx, cy, rf);
    fg.addColorStop(0, '#222226'); fg.addColorStop(1, '#09090b');
    dot(ctx, cx, cy, rf, fg);
    var ig = ctx.createRadialGradient(cx, cy, 0, cx, cy, rf);
    ig.addColorStop(0, hsla(h, s, 50, 0.05 + 0.13 * st.amp)); ig.addColorStop(1, hsla(h, s, 50, 0));
    dot(ctx, cx, cy, rf, ig);
    arc(ctx, cx, cy, rf, Math.PI * 1.1, Math.PI * 1.9, 'rgba(255,255,255,0.09)', 1);
    ctx.save(); ctx.beginPath(); ctx.arc(cx, cy, rf * 0.94, 0, TAU); ctx.clip();
    try { glyph(ctx, spec.kind || 'level', cx, cy, rf * 0.8, x, h, s, st); } catch (e) { /* keep drawing */ }
    ctx.restore();
    ctx.beginPath();
    ctx.moveTo(cx + Math.cos(ab) * rf * 0.8, cy + Math.sin(ab) * rf * 0.8);
    ctx.lineTo(cx + Math.cos(ab) * rf * 0.98, cy + Math.sin(ab) * rf * 0.98);
    var moving = md && md.live >= 0;
    ctx.strokeStyle = moving ? 'rgba(255,255,255,0.35)' : 'rgba(255,255,255,0.85)'; ctx.lineWidth = 1.6; ctx.stroke();
    if (moving) { // the needle: where the knob really is with every route added (what is heard)
      var al = A0 + SW * md.live;
      ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(cx + Math.cos(al) * rf * 0.92, cy + Math.sin(al) * rf * 0.92);
      ctx.strokeStyle = 'rgba(255,255,255,0.92)'; ctx.lineWidth = 2; ctx.stroke();
      dot(ctx, cx, cy, 2.4, '#ffffff');
    }
    text(ctx, st.text || fmt(spec.fmt, st.v, spec), cx, W + 9, (st.dragging || st.text ? '600 ' : '500 ') + (big ? 13 : 11) + 'px ' + MONO,
      st.textCol || (st.dragging ? hsla(h, Math.min(s, 80), 74, 1) : st.hover ? 'rgba(236,236,240,0.92)' : 'rgba(220,220,228,0.55)'), 'center');
  }
  /** The routes' rings, inner to outer from r0: each its source's reach and a dot where that source has the knob now.
      Three at most: past three the first two keep theirs and the rest share the third, pearl, each still its own dot
      (and its reach in its colour while its tag is hovered). A hovered route's ring lights, the others dim. */
  function modRings(ctx, cx, cy, r0, gap, lw, rings, hot, A0, SW) {
    var own = rings.length > 3 ? 2 : rings.length, mine = false, i, g, rr, k;
    for (i = 0; i < rings.length; i++) if (rings[i].r === hot) mine = true;
    function reach(g, rr, col, w) { arc(ctx, cx, cy, rr, A0 + SW * g.lo, A0 + SW * Math.max(g.lo + 0.004, g.hi), col, w); }
    function mark(g, rr, big, k) { if (g.at >= 0) dot(ctx, cx + Math.cos(A0 + SW * g.at) * rr, cy + Math.sin(A0 + SW * g.at) * rr, lw * (big ? 1.35 : 1.05), hsla(g.h, g.s, Math.min(94, g.l + 18), k)); }
    for (i = 0; i < own; i++) {
      g = rings[i]; rr = r0 + i * gap; k = mine && g.r !== hot ? 0.28 : 1;
      reach(g, rr, hsla(g.h, g.s, g.l, (g.r === hot ? 0.95 : 0.62) * k), g.r === hot ? lw * 1.45 : lw);
      mark(g, rr, g.r === hot, k);
    }
    if (rings.length > own) {
      var rest = rings.slice(own), m = { lo: 1, hi: 0 }, hotG = null;
      rr = r0 + own * gap;
      rest.forEach(function (x) { m.lo = Math.min(m.lo, x.lo); m.hi = Math.max(m.hi, x.hi); if (x.r === hot) hotG = x; });
      reach(m, rr, hsla(PEARL.h, 30, 82, mine && !hotG ? 0.18 : 0.42), lw);
      if (hotG) reach(hotG, rr, hsla(hotG.h, hotG.s, hotG.l, 0.95), lw * 1.45);
      rest.forEach(function (x) { mark(x, rr, x === hotG, mine && x !== hotG ? 0.28 : 1); });
    }
  }

  /* a small glowing dot / bar in a channel's color (strips, rails, editor head) */
  function chDot(cv, hue, sat, amp, muted) {
    var w = cv.clientWidth, h = cv.clientHeight; if (!w || !h) return;
    var dpr = Math.min(2, window.devicePixelRatio || 1);
    if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) { cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr); }
    var ctx = cv.getContext('2d'); ctx.setTransform(dpr, 0, 0, dpr, 0, 0); ctx.clearRect(0, 0, w, h);
    var k = muted ? 0.15 : 0.35 + 0.65 * amp;
    glowDot(ctx, w / 2, h / 2, Math.min(w, h) * 0.17, hue, sat, k);
  }
  function chBar(cv, hue, sat, amp, muted) {
    var w = cv.clientWidth, h = cv.clientHeight; if (!w || !h) return;
    var dpr = Math.min(2, window.devicePixelRatio || 1);
    if (cv.width !== Math.round(w * dpr) || cv.height !== Math.round(h * dpr)) { cv.width = Math.round(w * dpr); cv.height = Math.round(h * dpr); }
    var ctx = cv.getContext('2d'); ctx.setTransform(dpr, 0, 0, dpr, 0, 0); ctx.clearRect(0, 0, w, h);
    var k = muted ? 0.12 : 0.3 + 0.7 * amp, gr = ctx.createLinearGradient(0, 0, 0, h);
    gr.addColorStop(0, hsla(hue + 20, sat, 70, k)); gr.addColorStop(1, hsla(hue - 20, sat, 45, k * 0.6));
    ctx.beginPath(); ctx.roundRect ? ctx.roundRect(0, 0, w, h, w / 2) : ctx.rect(0, 0, w, h); ctx.fillStyle = gr; ctx.fill();
  }

  return { draw: draw, fmt: fmt, hsla: hsla, hslRgb: hslRgb, hueOf: hueOf, neon: neon, glowDot: glowDot, chDot: chDot, chBar: chBar,
           text: text, NOTES: NOTES, PEARL: PEARL, SANS: SANS, MONO: MONO };
})();
