// Track Explorer: aba "Pistas". Terreno (tracksplit.pssg), objetos de route_N/objects.ens, portões e linha da IA.
// Dados vindos de tools/uiview/track.py: tracks/<id>/{track.json, terrain.bin, objects.bin}.
const TRACKS = typeof TRACK_DATA !== "undefined" ? TRACK_DATA : [];
const tv = {
  id: null, loadedId: null, data: null, gl: null, prog: null, loc: null, gen: 0,
  terrain: [], types: new Map(), inst: [], lines: [],
  sel: -1, showTerrain: true, showObjects: true, showTrees: true, showDist: false, showGates: true, showAi: true, wire: false,
  yaw: 0.8, pitch: 0.6, dist: 400, target: [0, 1430, -430], drag: null, keys: new Set(),
  dirty: true, status: "", busy: false,
};

function tvLog(...a) { console.info("[track]", ...a); }

function tvParse(buf) {
  const view = new DataView(buf);
  if (String.fromCharCode(view.getUint8(0), view.getUint8(1), view.getUint8(2), view.getUint8(3)) !== "DR2M") throw new Error("DR2M");
  const dec = new TextDecoder();
  const count = view.getUint32(4, true);
  let o = 8;
  const out = [];
  for (let m = 0; m < count; m++) {
    const nameLen = view.getUint16(o, true), matLen = view.getUint16(o + 2, true);
    const verts = view.getUint32(o + 4, true), indices = view.getUint32(o + 8, true), flags = view.getUint32(o + 12, true);
    o += 16;
    const name = dec.decode(new Uint8Array(buf, o, nameLen)); o += nameLen;
    const material = dec.decode(new Uint8Array(buf, o, matLen)); o += matLen;
    o = (o + 3) & ~3;
    const pos = new Float32Array(buf, o, verts * 3); o += verts * 12;
    const uv = new Float32Array(buf, o, verts * 2); o += verts * 8;
    const wide = !!(flags & 1);
    const ind = wide ? new Uint32Array(buf, o, indices) : new Uint16Array(buf, o, indices);
    o += indices * (wide ? 4 : 2);
    o = (o + 3) & ~3;
    out.push({ name, material, verts, pos, uv, ind, wide });
  }
  return out;
}

function tvHash(s) { let h = 2166136261; for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619); } return h >>> 0; }
function tvColor(name, base) {
  const h = tvHash(name);
  const j = (k) => ((h >> k) & 255) / 255;
  return [base[0] * (0.75 + 0.5 * j(0)), base[1] * (0.75 + 0.5 * j(8)), base[2] * (0.75 + 0.5 * j(16))];
}

function tvUpload(list, color) {
  const gl = tv.gl;
  const ext = gl.getExtension("OES_element_index_uint");
  return list.map((m) => {
    if (m.wide && !ext) return null;
    const pb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, pb); gl.bufferData(gl.ARRAY_BUFFER, m.pos, gl.STATIC_DRAW);
    const ib = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, m.ind, gl.STATIC_DRAW);
    let lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
    for (let i = 0; i < m.pos.length; i += 3) for (let k = 0; k < 3; k++) { const v = m.pos[i + k]; if (v < lo[k]) lo[k] = v; if (v > hi[k]) hi[k] = v; }
    return { pb, ib, count: m.ind.length, wide: m.wide, color: tvColor(m.material, color), material: m.material, lo, hi };
  }).filter(Boolean);
}

function tvFree() {
  const gl = tv.gl;
  if (gl) {
    for (const r of tv.terrain) { gl.deleteBuffer(r.pb); gl.deleteBuffer(r.ib); }
    for (const t of tv.types.values()) for (const r of t.meshes) { gl.deleteBuffer(r.pb); gl.deleteBuffer(r.ib); }
    for (const l of tv.lines) gl.deleteBuffer(l.buf);
  }
  tv.terrain = []; tv.types = new Map(); tv.inst = []; tv.lines = [];
}

async function tvFetch(url, kind) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(url + " " + r.status);
  return kind === "json" ? r.json() : r.arrayBuffer();
}

function tvLinesFrom(data) {
  const out = [];
  const add = (pts, color, mode, kind) => {
    if (pts.length < 2) return;
    out.push({ pts, color, mode, kind });
  };
  for (const route of data.routes) {
    const p = route.progress;
    if (p && p.gates.length) {
      // as rotas repetem a contagem de distância: um recuo quebra a linha
      let seg = [];
      const flush = () => {
        if (seg.length > 1) {
          add(seg.map((g) => g.l), [0.95, 0.4, 0.4], "strip", "gate");
          add(seg.map((g) => g.r), [0.4, 0.6, 1.0], "strip", "gate");
          const rungs = [];
          for (const g of seg) rungs.push(g.l, g.r);
          add(rungs, [0.5, 0.5, 0.55], "lines", "gate");
        }
        seg = [];
      };
      let last = -1;
      for (const g of p.gates) { if (g.d < last) flush(); seg.push(g); last = g.d; }
      flush();
    }
    for (const t of route.ai || []) add(t.pts, t.name === "default" ? [0.3, 0.95, 0.4] : [0.95, 0.8, 0.25], "strip", "ai");
  }
  return out;
}

function tvBox(lo, hi) {
  const c = [];
  for (let i = 0; i < 8; i++) c.push([i & 1 ? hi[0] : lo[0], i & 2 ? hi[1] : lo[1], i & 4 ? hi[2] : lo[2]]);
  return c;
}
function tvXf(m, p) {
  return [p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12], p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13], p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14]];
}

async function tvOpen(id) {
  tv.busy = true; tv.status = t("trk.loading"); tvStatus();
  const gen = ++tv.gen;
  try {
    const base = `tracks/${encodeURIComponent(id)}/`;
    const [data, terr, objs] = await Promise.all([tvFetch(base + "track.json", "json"), tvFetch(base + "terrain.bin"), tvFetch(base + "objects.bin")]);
    if (gen !== tv.gen) return;
    tvFree();
    tv.data = data; tv.loadedId = id;
    tv.terrain = tvUpload(tvParse(terr), [0.42, 0.45, 0.34]);
    const lib = tvUpload(tvParse(objs), [0.8, 0.75, 0.7]);
    for (const [name, info] of Object.entries(data.types)) {
      const meshes = lib.slice(info.first, info.first + info.count);
      const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
      for (const m of meshes) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], m.lo[k]); hi[k] = Math.max(hi[k], m.hi[k]); }
      tv.types.set(name, { name, meshes, lo, hi, empty: !meshes.length });
    }
    tv.inst = data.instances.map((i, n) => {
      const kind = i.type[0] === "t" ? (/_dist_/.test(i.type) ? "dist" : "tree") : "obj";
      return { n, id: i.id, type: i.type, kind, route: i.route, m: new Float32Array(i.m), hidden: false };
    });
    const gl = tv.gl;
    tv.lines = tvLinesFrom(data).map((l) => {
      const buf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, buf);
      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(l.pts.flat()), gl.STATIC_DRAW);
      return { ...l, buf, count: l.pts.length };
    });
    tv.sel = -1;
    tvFrameRoute();
    tv.status = "";
    tv.dirty = true;
  } catch (err) {
    tv.status = t("trk.noload") + " " + String((err && err.message) || err);
  } finally {
    tv.busy = false;
    tvStatus(); tvInspect();
  }
}

const tvLayer = (i) => (i.kind === "obj" ? tv.showObjects : i.kind === "tree" ? tv.showTrees : tv.showDist);
const tvName = (type) => type.slice(2);

function tvFrameRoute() {
  const pts = [];
  for (const r of tv.data.routes) { for (const t of r.ai || []) pts.push(...t.pts); if (r.progress) for (const g of r.progress.gates) pts.push(g.l, g.r); }
  if (!pts.length) return;
  const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
  for (const p of pts) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], p[k]); hi[k] = Math.max(hi[k], p[k]); }
  tv.target = [(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2];
  tv.dist = Math.max(60, Math.hypot(hi[0] - lo[0], hi[2] - lo[2]) * 0.9);
  tv.yaw = 0.8; tv.pitch = 0.7;
}

function tvFrameInst(i) {
  const ty = tv.types.get(i.type);
  tv.target = [i.m[12], i.m[13], i.m[14]];
  const size = ty && !ty.empty ? Math.hypot(ty.hi[0] - ty.lo[0], ty.hi[1] - ty.lo[1], ty.hi[2] - ty.lo[2]) : 4;
  tv.dist = Math.max(6, size * 3);
}

function tvCam() {
  const cp = Math.cos(tv.pitch), sp = Math.sin(tv.pitch);
  const eye = [tv.target[0] + tv.dist * cp * Math.sin(tv.yaw), tv.target[1] + tv.dist * sp, tv.target[2] + tv.dist * cp * Math.cos(tv.yaw)];
  return { eye, at: tv.target };
}
const TV_FOV = 0.9;
function tvVp(canvas) {
  const cam = tvCam();
  const near = Math.max(0.3, tv.dist / 200), far = Math.max(20000, tv.dist * 20);
  return mat4Mul(mat4Perspective(TV_FOV, canvas.width / Math.max(1, canvas.height), near, far), mat4Look(cam.eye, cam.at));
}

function tvGL() {
  const canvas = document.getElementById("trk-view");
  const gl = canvas.getContext("webgl", { antialias: true, alpha: false });
  if (!gl) return null;
  gl.getExtension("OES_standard_derivatives");
  gl.getExtension("OES_element_index_uint");
  const vs = `attribute vec3 aPos; uniform mat4 uVp; uniform mat4 uModel; varying vec3 vW; varying float vH;
    void main() { vec4 w = uModel * vec4(aPos, 1.0); vW = w.xyz; vH = w.y; gl_Position = uVp * w; }`;
  const fs = `#extension GL_OES_standard_derivatives : enable
    precision mediump float; varying vec3 vW; varying float vH; uniform vec3 uColor; uniform float uHi; uniform int uLine;
    void main() {
      if (uLine == 1) { gl_FragColor = vec4(uColor, 1.0); return; }
      vec3 n = normalize(cross(dFdx(vW), dFdy(vW)));
      float light = abs(dot(n, normalize(vec3(0.35, 0.85, 0.4)))) * 0.7 + 0.3;
      gl_FragColor = vec4(mix(uColor * light, vec3(1.0, 0.6, 0.15), uHi * 0.55), 1.0);
    }`;
  const compile = (type, src) => { const sh = gl.createShader(type); gl.shaderSource(sh, src); gl.compileShader(sh); if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(sh)); return sh; };
  const prog = gl.createProgram();
  gl.attachShader(prog, compile(gl.VERTEX_SHADER, vs));
  gl.attachShader(prog, compile(gl.FRAGMENT_SHADER, fs));
  gl.linkProgram(prog);
  if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(prog));
  gl.useProgram(prog);
  gl.enable(gl.DEPTH_TEST);
  const loc = { pos: gl.getAttribLocation(prog, "aPos") };
  for (const n of ["uVp", "uModel", "uColor", "uHi", "uLine"]) loc[n] = gl.getUniformLocation(prog, n);
  Object.assign(tv, { gl, prog, loc });
  const IDENT = new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);
  const gen = tv.gen;
  const frame = () => {
    if (!canvas.isConnected || tv.gl !== gl) return;
    requestAnimationFrame(frame);
    if (state.mode !== "tracks") return;
    tvKeys();
    const w = canvas.clientWidth || 800, h = canvas.clientHeight || 480;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    if (canvas.width !== Math.floor(w * dpr) || canvas.height !== Math.floor(h * dpr)) { canvas.width = Math.floor(w * dpr); canvas.height = Math.floor(h * dpr); }
    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.clearColor(0.55, 0.68, 0.82, 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.useProgram(prog);
    gl.uniformMatrix4fv(loc.uVp, false, tvVp(canvas));
    gl.enableVertexAttribArray(loc.pos);
    const drawMesh = (r, model, hi, color) => {
      gl.uniformMatrix4fv(loc.uModel, false, model);
      gl.uniform3fv(loc.uColor, color || r.color);
      gl.uniform1f(loc.uHi, hi);
      gl.bindBuffer(gl.ARRAY_BUFFER, r.pb);
      gl.vertexAttribPointer(loc.pos, 3, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, r.ib);
      gl.drawElements(gl.TRIANGLES, r.count, r.wide ? gl.UNSIGNED_INT : gl.UNSIGNED_SHORT, 0);
    };
    gl.uniform1i(loc.uLine, 0);
    if (tv.showTerrain) for (const r of tv.terrain) drawMesh(r, IDENT, 0);
    {
      for (const i of tv.inst) {
        if (i.hidden || !tvLayer(i)) continue;
        const ty = tv.types.get(i.type);
        if (!ty) continue;
        const hi = i.n === tv.sel ? 1 : 0;
        for (const r of ty.meshes) drawMesh(r, i.m, hi);
      }
    }
    gl.uniform1i(loc.uLine, 1);
    gl.uniformMatrix4fv(loc.uModel, false, IDENT);
    gl.disable(gl.DEPTH_TEST);
    for (const l of tv.lines) {
      if (l.kind === "gate" ? !tv.showGates : !tv.showAi) continue;
      gl.uniform3fv(loc.uColor, l.color);
      gl.bindBuffer(gl.ARRAY_BUFFER, l.buf);
      gl.vertexAttribPointer(loc.pos, 3, gl.FLOAT, false, 0, 0);
      gl.drawArrays(l.mode === "lines" ? gl.LINES : gl.LINE_STRIP, 0, l.count);
    }
    gl.enable(gl.DEPTH_TEST);
  };
  requestAnimationFrame(frame);
  const el = canvas;
  el.addEventListener("contextmenu", (e) => e.preventDefault());
  el.addEventListener("pointerdown", (e) => {
    el.setPointerCapture(e.pointerId);
    tv.drag = { x: e.clientX, y: e.clientY, pan: e.button === 2 || e.button === 1 || e.shiftKey, moved: 0, button: e.button };
  });
  el.addEventListener("pointermove", (e) => {
    const d = tv.drag;
    if (!d) return;
    const dx = e.clientX - d.x, dy = e.clientY - d.y;
    d.x = e.clientX; d.y = e.clientY; d.moved += Math.abs(dx) + Math.abs(dy);
    if (d.pan) {
      const k = tv.dist * 0.0016;
      const rx = Math.cos(tv.yaw), rz = -Math.sin(tv.yaw);
      tv.target[0] -= (rx * dx) * k; tv.target[2] -= (rz * dx) * k;
      const fx = -Math.sin(tv.yaw), fz = -Math.cos(tv.yaw);
      tv.target[0] += fx * dy * k; tv.target[2] += fz * dy * k;
    } else {
      tv.yaw -= dx * 0.005; tv.pitch = Math.max(-0.2, Math.min(1.5, tv.pitch + dy * 0.005));
    }
  });
  el.addEventListener("pointerup", (e) => {
    const d = tv.drag; tv.drag = null;
    if (d && d.moved < 5 && d.button === 0) tvPick(e, canvas);
  });
  el.addEventListener("wheel", (e) => { e.preventDefault(); tv.dist = Math.max(2, Math.min(15000, tv.dist * Math.exp(e.deltaY * 0.0012))); }, { passive: false });
  return gl;
}

const TV_WASD = { w: [0, -1], s: [0, 1], a: [-1, 0], d: [1, 0] };
function tvKeys() {
  if (!tv.keys.size) return;
  const step = tv.dist * 0.012 * (tv.keys.has("shift") ? 3 : 1);
  for (const k of tv.keys) {
    const v = TV_WASD[k];
    if (!v) continue;
    const fx = -Math.sin(tv.yaw), fz = -Math.cos(tv.yaw), rx = Math.cos(tv.yaw), rz = -Math.sin(tv.yaw);
    tv.target[0] += (fx * -v[1] + rx * v[0]) * step; tv.target[2] += (fz * -v[1] + rz * v[0]) * step;
  }
}

// picking: raio contra a caixa local de cada tipo de objeto
function tvRay(e, canvas) {
  const rect = canvas.getBoundingClientRect();
  const nx = ((e.clientX - rect.left) / rect.width) * 2 - 1, ny = 1 - ((e.clientY - rect.top) / rect.height) * 2;
  const cam = tvCam();
  const f = [cam.at[0] - cam.eye[0], cam.at[1] - cam.eye[1], cam.at[2] - cam.eye[2]];
  const fl = Math.hypot(...f); f.forEach((_, i) => (f[i] /= fl));
  let r = [-f[2], 0, f[0]]; const rl = Math.hypot(...r) || 1; r = r.map((v) => v / rl);
  const u = [r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]];
  const th = Math.tan(TV_FOV / 2), asp = rect.width / rect.height;
  const d = [f[0] + r[0] * nx * th * asp + u[0] * ny * th, f[1] + r[1] * nx * th * asp + u[1] * ny * th, f[2] + r[2] * nx * th * asp + u[2] * ny * th];
  const dl = Math.hypot(...d);
  return { o: cam.eye, d: d.map((v) => v / dl) };
}
function tvInvAffine(m) {
  const a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
  const det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g) || 1;
  const r = [(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det, (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det, (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det];
  // linhas de m = base; o inverso do bloco 3×3 (vetor-linha) é a transposta do inverso usual
  return r;
}
function tvPick(e, canvas) {
  const ray = tvRay(e, canvas);
  let best = -1, bt = Infinity;
  for (const i of tv.inst) {
    if (i.hidden || !tvLayer(i)) continue;
    const ty = tv.types.get(i.type);
    if (!ty || ty.empty) continue;
    // leva o raio ao espaço local: p_local = (p - t) * R^-1 (convenção de vetor-linha)
    const m = i.m, inv = tvInvAffine(m);
    const rel = [ray.o[0] - m[12], ray.o[1] - m[13], ray.o[2] - m[14]];
    const loc = (v) => [v[0] * inv[0] + v[1] * inv[3] + v[2] * inv[6], v[0] * inv[1] + v[1] * inv[4] + v[2] * inv[7], v[0] * inv[2] + v[1] * inv[5] + v[2] * inv[8]];
    const o = loc(rel), d = loc(ray.d);
    let t0 = 0, t1 = Infinity, ok = true;
    for (let k = 0; k < 3 && ok; k++) {
      if (Math.abs(d[k]) < 1e-9) { if (o[k] < ty.lo[k] || o[k] > ty.hi[k]) ok = false; continue; }
      let a = (ty.lo[k] - o[k]) / d[k], b = (ty.hi[k] - o[k]) / d[k];
      if (a > b) [a, b] = [b, a];
      t0 = Math.max(t0, a); t1 = Math.min(t1, b);
      if (t0 > t1) ok = false;
    }
    if (ok && t0 < bt) { bt = t0; best = i.n; }
  }
  tvSelect(best);
}

function tvSelect(n) {
  tv.sel = n;
  tvStatus(); tvInspect();
}

function tvStatus() {
  const note = document.getElementById("trk-note"), sel = document.getElementById("trk-sel");
  if (!note) return;
  if (tv.status) { note.textContent = tv.status; sel.textContent = ""; return; }
  const d = tv.data;
  note.textContent = d ? t("trk.status", { terrain: d.terrain.meshes.toLocaleString("pt-BR"), inst: tv.inst.length.toLocaleString("pt-BR"), types: tv.types.size }) : "";
  const i = tv.sel >= 0 ? tv.inst[tv.sel] : null;
  sel.textContent = i ? `${tvName(i.type)} · ${i.m[12].toFixed(1)} ${i.m[13].toFixed(1)} ${i.m[14].toFixed(1)}` : "";
}

function tvInspect() {
  const box = inspectEl;
  const row = TRACKS.find((r) => r.id === tv.id);
  if (!row) { box.innerHTML = `<p class="muted">${esc(t("trk.pick"))}</p>`; return; }
  const d = tv.data;
  const i = tv.sel >= 0 ? tv.inst[tv.sel] : null;
  const kv = (k, v) => `<div>${esc(k)}</div><div class="mono">${esc(String(v))}</div>`;
  const ty = i ? tv.types.get(i.type) : null;
  box.innerHTML = `<h2>${esc(row.n)}</h2>
    <p class="muted small">${esc(row.c)} · ${esc(row.src)}</p>
    <div class="kv">
      ${kv(t("trk.terrain"), d ? `${d.terrain.meshes.toLocaleString("pt-BR")} ${t("trk.blocks")} · ${d.terrain.verts.toLocaleString("pt-BR")} ${t("mdl.verts")}` : "…")}
      ${kv(t("trk.routes"), (row.routes || []).join(", "))}
      ${kv(t("trk.objects"), d ? `${tv.inst.length} ${t("trk.instances")} · ${tv.types.size} ${t("trk.types")}` : "…")}
    </div>
    ${i ? `<h3>${esc(t("trk.selected"))}</h3><div class="kv">
      ${kv("id", i.id)}${kv(t("trk.type"), tvName(i.type))}${kv(t("trk.kind"), t("trk.kind." + i.kind))}${kv(t("trk.route"), i.route)}
      ${kv(t("trk.pos"), `${i.m[12].toFixed(2)}, ${i.m[13].toFixed(2)}, ${i.m[14].toFixed(2)}`)}
      ${kv(t("trk.meshes"), ty ? (ty.empty ? t("trk.nomesh") : ty.meshes.length) : "—")}
    </div>` : `<p class="muted">${esc(t("trk.hint"))}</p>`}`;
}

const TV_ACTS = [
  ["showTerrain", "trk.show.terrain"], ["showObjects", "trk.show.objects"], ["showTrees", "trk.show.trees"], ["showDist", "trk.show.dist"], ["showGates", "trk.show.gates"], ["showAi", "trk.show.ai"],
];

function ensureTrackStage() {
  if (document.getElementById("trk-view")) return;
  tv.gen++;
  tv.gl = null; tv.loadedId = null; tv.terrain = []; tv.types = new Map(); tv.inst = []; tv.lines = [];
  const opts = TRACKS.map((r) => `<option value="${esc(r.id)}">${esc(r.c)} · ${esc(r.n)}</option>`).join("");
  contentEl.innerHTML = `<div class="trk-ws">
    <div class="car-tb" role="toolbar">
      <select id="trk-open" title="${esc(t("trk.open"))}">${opts}</select>
      ${TV_ACTS.map(([k, key]) => `<label class="trk-opt"><input type="checkbox" data-trk="${k}" ${tv[k] ? "checked" : ""}> ${esc(t(key))}</label>`).join("")}
      <button type="button" class="tb-btn" id="trk-frame" title="${esc(t("trk.frame"))}">${esc(t("trk.frame"))}</button>
    </div>
    <div class="trk-vp"><canvas class="gl" id="trk-view"></canvas></div>
    <div class="car-sb"><span id="trk-note"></span><span id="trk-sel"></span></div>
  </div>`;
  document.getElementById("trk-open").addEventListener("change", (e) => { tv.id = e.target.value; history.replaceState(null, "", "#p=" + encodeURIComponent(tv.id)); tvOpen(tv.id); });
  document.getElementById("trk-frame").addEventListener("click", () => { if (tv.data) tvFrameRoute(); });
  contentEl.querySelectorAll("[data-trk]").forEach((c) => c.addEventListener("change", () => { tv[c.dataset.trk] = c.checked; }));
  try { tvGL(); } catch (err) { document.getElementById("trk-note").textContent = String((err && err.message) || err); }
}

MODES.tracks = {
  stage: false, hash: "p",
  filters() { return []; },
  list(q) {
    return TRACKS.filter((r) => !q || (r.n + " " + r.c + " " + r.src).toLowerCase().includes(q))
      .map((r) => `<div class="row${r.id === tv.id ? " on" : ""}" data-id="${esc(r.id)}">
        <div class="id">${esc(r.n)}</div><div class="sub">${esc(r.c)} · ${r.inst} ${esc(t("trk.instances"))}</div></div>`);
  },
  current: () => tv.id,
  select(id) { tv.id = id; },
  onFilter() {},
  show() {
    ensureTrackStage();
    if (!tv.id && TRACKS.length) tv.id = TRACKS[0].id;
    const sel = document.getElementById("trk-open");
    if (sel && tv.id) sel.value = tv.id;
    if (tv.id && tv.loadedId !== tv.id && !tv.busy) tvOpen(tv.id);
    tvStatus(); tvInspect();
  },
};

document.addEventListener("keydown", (e) => {
  if (state.mode !== "tracks" || /INPUT|SELECT|TEXTAREA/.test((e.target || {}).tagName || "")) return;
  const k = e.key.toLowerCase();
  if (TV_WASD[k] || k === "shift") tv.keys.add(k);
  else if (k === "f" && tv.sel >= 0) tvFrameInst(tv.inst[tv.sel]);
  else if (k === "h" && tv.sel >= 0) { tv.inst[tv.sel].hidden = true; tvSelect(-1); }
  else if (k === "escape") tvSelect(-1);
});
document.addEventListener("keyup", (e) => tv.keys.delete(e.key.toLowerCase()));
window.addEventListener("blur", () => tv.keys.clear());
