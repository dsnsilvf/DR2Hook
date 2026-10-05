// Track Explorer: aba "Pistas". Terreno (tracksplit.pssg), objetos de route_N/objects.ens, portões e linha da IA.
// Dados vindos de tools/uiview/track.py: tracks/<id>/{track.json, terrain.bin, objects.bin}.
const TRACKS = typeof TRACK_DATA !== "undefined" ? TRACK_DATA : [];
const tv = {
  id: null, loadedId: null, data: null, gl: null, prog: null, loc: null, gen: 0,
  terrain: [], texs: new Map(), types: new Map(), typeList: [], inst: null, route: null, lines: [], ext: null, editRev: 0, scratch: new Float32Array(12 * 4096), drawDist: 700, vis: null, visKey: "",
  sel: -1, showTerrain: true, showObjects: true, showTrees: true, showDist: false, showGates: true, showAi: true, wire: false,
  yaw: 0.8, pitch: 0.6, dist: 400, target: [0, 1430, -430], drag: null, keys: new Set(),
  dirty: true, status: "", busy: false, tool: "orbit", cache: new Map(),
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
    let col = null;
    if (flags & 2) { col = new Uint8Array(buf, o, verts * 4); o += verts * 4; }
    const wide = !!(flags & 1);
    const ind = wide ? new Uint32Array(buf, o, indices) : new Uint16Array(buf, o, indices);
    o += indices * (wide ? 4 : 2);
    o = (o + 3) & ~3;
    out.push({ name, material, verts, pos, uv, col, ind, wide });
  }
  return out;
}

function tvHash(s) { let h = 2166136261; for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619); } return h >>> 0; }
function tvColor(name, base) {
  // terreno em lote (batched_track.fx): sem textura nem cor no arquivo; um tom de terra fixo
  if (/^g\|batchmaterial/.test(name)) return [0.36, 0.34, 0.28];
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
    const ub = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, ub); gl.bufferData(gl.ARRAY_BUFFER, m.uv, gl.STATIC_DRAW);
    let cb = null;
    if (m.col) { cb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, cb); gl.bufferData(gl.ARRAY_BUFFER, m.col, gl.STATIC_DRAW); }
    const ib = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, m.ind, gl.STATIC_DRAW);
    let lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
    for (let i = 0; i < m.pos.length; i += 3) for (let k = 0; k < 3; k++) { const v = m.pos[i + k]; if (v < lo[k]) lo[k] = v; if (v > hi[k]) hi[k] = v; }
    return { pb, ub, cb, ib, count: m.ind.length, wide: m.wide, color: tvColor(m.material, color), material: m.material, lo, hi };
  }).filter(Boolean);
}

// textura de cor de cada material: carregada sob demanda, o material fica na cor até a imagem chegar
function tvTexture(material) {
  const file = tv.data && tv.data.materials && tv.data.materials[material];
  if (!file) return null;
  let e = tv.texs.get(file);
  if (e) return e.ready ? e.tex : null;
  const gl = tv.gl;
  e = { tex: gl.createTexture(), ready: false };
  tv.texs.set(file, e);
  const img = new Image();
  img.onload = () => {
    if (tv.gl !== gl) return;
    gl.bindTexture(gl.TEXTURE_2D, e.tex);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, img);
    const pot = (n) => (n & (n - 1)) === 0;
    if (pot(img.width) && pot(img.height)) {
      gl.generateMipmap(gl.TEXTURE_2D);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
    } else {
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    }
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    e.ready = true;
  };
  img.src = `tracks/${encodeURIComponent(tv.id)}/${file}`;
  return null;
}

function tvFree() {
  const gl = tv.gl;
  if (gl) {
    for (const r of tv.terrain) { gl.deleteBuffer(r.pb); gl.deleteBuffer(r.ub); if (r.cb) gl.deleteBuffer(r.cb); gl.deleteBuffer(r.ib); }
    for (const t of tv.types.values()) { for (const r of t.meshes) { gl.deleteBuffer(r.pb); gl.deleteBuffer(r.ub); if (r.cb) gl.deleteBuffer(r.cb); gl.deleteBuffer(r.ib); } if (t.vb) gl.deleteBuffer(t.vb); }
    for (const l of tv.lines) gl.deleteBuffer(l.buf);
    for (const e of tv.texs.values()) gl.deleteTexture(e.tex);
  }
  tv.texs.clear();
  tv.terrain = []; tv.types = new Map(); tv.typeList = []; tv.inst = null; tv.lines = []; tv.vis = null; tv.terrainFile = null;
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

function tvParseInst(buf) {
  const view = new DataView(buf);
  if (String.fromCharCode(view.getUint8(0), view.getUint8(1), view.getUint8(2), view.getUint8(3)) !== "DR2I") throw new Error("DR2I");
  const n = view.getUint32(4, true);
  let o = 8;
  const type = new Uint16Array(buf, o, n); o += n * 2; o = (o + 3) & ~3;
  const idnum = new Uint32Array(buf, o, n); o += n * 4;
  const m = new Float32Array(buf, o, n * 12);
  return { n, type, idnum, m, m0: new Float32Array(m), hidden: new Uint8Array(n), hist: [], histPos: 0 };
}

async function tvOpen(id) {
  tv.busy = true; tv.status = t("trk.loading"); tvStatus();
  const gen = ++tv.gen;
  try {
    const base = `tracks/${encodeURIComponent(id)}/`;
    const [data, objs] = await Promise.all([tvFetch(base + "track.json", "json"), tvFetch(base + "objects.bin")]);
    if (gen !== tv.gen) return;
    tvFree();
    tv.data = data; tv.loadedId = id; tv.terrainFile = null; tv.cache.clear();
    await tvLoadTerrain(data.routes[0].name, gen);
    const lib = tvUpload(tvParse(objs), [0.8, 0.75, 0.7]);
    tv.typeList = data.type_order.map((name, n) => {
      const info = data.types[name];
      const meshes = lib.slice(info.first, info.first + info.count);
      const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
      for (const m of meshes) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], m.lo[k]); hi[k] = Math.max(hi[k], m.hi[k]); }
      const kind = name[0] === "t" ? (/_dist_/.test(name) ? "dist" : "tree") : "obj";
      const ty = { n, name, kind, meshes, lo, hi, empty: !meshes.length, vb: null, vis: 0 };
      tv.types.set(name, ty);
      return ty;
    });
    const gl = tv.gl;
    tv.lines = tvLinesFrom(data).map((l) => {
      const buf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, buf);
      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(l.pts.flat()), gl.STATIC_DRAW);
      return { ...l, buf, count: l.pts.length };
    });
    tv.route = data.routes[0].name;
    await tvLoadRoute(tv.route, gen);
    tvFrameRoute();
    tv.status = "";
  } catch (err) {
    tv.status = t("trk.noload") + " " + String((err && err.message) || err);
  } finally {
    tv.busy = false;
    tvRouteSelect(); tvStatus(); tvInspect(); tvEdited();
  }
}

// cada rota aponta para um terrain_<n>.bin; rotas com a mesma seleção dividem o arquivo
async function tvLoadTerrain(route, gen) {
  const entry = tv.data.routes.find((r) => r.name === route);
  const file = entry.terrain.file;
  if (file === tv.terrainFile) return;
  const buf = await tvFetch(`tracks/${encodeURIComponent(tv.id)}/${file}`);
  if (gen !== undefined && gen !== tv.gen) return;
  const gl = tv.gl;
  for (const r of tv.terrain) { gl.deleteBuffer(r.pb); gl.deleteBuffer(r.ub); if (r.cb) gl.deleteBuffer(r.cb); gl.deleteBuffer(r.ib); }
  tv.terrain = tvUpload(tvParse(buf), [0.42, 0.45, 0.34]);
  tv.terrainFile = file;
}

async function tvLoadRoute(name, gen) {
  await tvLoadTerrain(name, gen);
  let inst = tv.cache.get(name);
  if (!inst) {
    const buf = await tvFetch(`tracks/${encodeURIComponent(tv.id)}/inst_${name}.bin`);
    if (gen !== undefined && gen !== tv.gen) return;
    inst = tvParseInst(buf);
    tv.cache.set(name, inst);
  }
  tv.route = name;
  tv.inst = inst;
  tv.sel = -1;
  tv.visKey = "";
  tvGroups();
}

// instâncias de cada tipo, para cortar por distância sem varrer a rota inteira por tipo
function tvGroups() {
  const inst = tv.inst;
  const counts = new Uint32Array(tv.typeList.length);
  for (let i = 0; i < inst.n; i++) counts[inst.type[i]]++;
  const groups = tv.typeList.map((_, k) => new Uint32Array(counts[k]));
  const fill = new Uint32Array(tv.typeList.length);
  for (let i = 0; i < inst.n; i++) { const k = inst.type[i]; groups[k][fill[k]++] = i; }
  tv.groups = groups;
}

const tvName = (type) => type.slice(2);
const tvKind = (i) => tv.typeList[tv.inst.type[i]].kind;
const tvLayerOn = (kind) => (kind === "obj" ? tv.showObjects : kind === "tree" ? tv.showTrees : tv.showDist);

// recorta o que está perto da câmera, por tipo, e sobe para a GPU
function tvCull() {
  const cam = tvCam();
  const key = [tv.target.map((v) => Math.round(v / 8)), Math.round(tv.dist / 8), tv.drawDist, tv.showObjects, tv.showTrees, tv.showDist, tv.editRev].join();
  if (key === tv.visKey) return;
  tv.visKey = key;
  const gl = tv.gl, inst = tv.inst, R2 = tv.drawDist * tv.drawDist;
  const cx = tv.target[0], cz = tv.target[2];
  for (const ty of tv.typeList) {
    ty.vis = 0;
    if (ty.empty || !tvLayerOn(ty.kind)) continue;
    const grp = tv.groups[ty.n];
    let n = 0;
    const far = ty.kind === "dist";
    if (tv.scratch.length < grp.length * 12) tv.scratch = new Float32Array(grp.length * 12);
    const buf = tv.scratch;
    for (let g = 0; g < grp.length; g++) {
      const i = grp[g];
      if (inst.hidden[i]) continue;
      const o = i * 12;
      if (!far) { const dx = inst.m[o + 9] - cx, dz = inst.m[o + 11] - cz; if (dx * dx + dz * dz > R2) continue; }
      buf.set(inst.m.subarray(o, o + 12), n * 12);
      n++;
    }
    ty.vis = n;
    if (!n) continue;
    if (!ty.vb) ty.vb = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, ty.vb);
    gl.bufferData(gl.ARRAY_BUFFER, buf.subarray(0, n * 12), gl.DYNAMIC_DRAW);
  }
  tvStatus();
}

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
  const ty = tv.typeList[tv.inst.type[i]];
  const o = i * 12;
  tv.target = [tv.inst.m[o + 9], tv.inst.m[o + 10], tv.inst.m[o + 11]];
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
  const vs = `attribute vec2 aUv; varying vec2 vUv; attribute vec4 aCol; varying vec4 vCol; attribute vec3 aPos; attribute vec3 aX; attribute vec3 aY; attribute vec3 aZ; attribute vec3 aP;
    uniform mat4 uVp; varying vec3 vW; varying float vH;
    void main() { vUv = aUv; vCol = aCol; vec3 w = aX * aPos.x + aY * aPos.y + aZ * aPos.z + aP; vW = w; vH = w.y; gl_Position = uVp * vec4(w, 1.0); }`;
  const fs = `#extension GL_OES_standard_derivatives : enable
    precision mediump float; varying vec3 vW; varying float vH; varying vec2 vUv; varying vec4 vCol; uniform int uVCol; uniform vec3 uColor; uniform float uHi; uniform int uLine;
    uniform sampler2D uTex; uniform int uHasTex; uniform int uCut;
    void main() {
      if (uLine == 1) { gl_FragColor = vec4(uColor, 1.0); return; }
      vec3 n = normalize(cross(dFdx(vW), dFdy(vW)));
      float light = abs(dot(n, normalize(vec3(0.35, 0.85, 0.4)))) * 0.7 + 0.3;
      vec3 base = uVCol == 1 ? vCol.rgb : uColor;
      if (uHasTex == 1) { vec4 tx = texture2D(uTex, vUv); if (uCut == 1 && tx.a < 0.4) discard; base = tx.rgb; }
      gl_FragColor = vec4(mix(base * light, vec3(1.0, 0.6, 0.15), uHi * 0.55), 1.0);
    }`;
  const compile = (type, src) => { const sh = gl.createShader(type); gl.shaderSource(sh, src); gl.compileShader(sh); if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(sh)); return sh; };
  const prog = gl.createProgram();
  gl.attachShader(prog, compile(gl.VERTEX_SHADER, vs));
  gl.attachShader(prog, compile(gl.FRAGMENT_SHADER, fs));
  gl.linkProgram(prog);
  if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(prog));
  gl.useProgram(prog);
  gl.enable(gl.DEPTH_TEST);
  const loc = { pos: gl.getAttribLocation(prog, "aPos"), uv: gl.getAttribLocation(prog, "aUv"), col: gl.getAttribLocation(prog, "aCol"), x: gl.getAttribLocation(prog, "aX"), y: gl.getAttribLocation(prog, "aY"), z: gl.getAttribLocation(prog, "aZ"), p: gl.getAttribLocation(prog, "aP") };
  for (const n of ["uVp", "uColor", "uHi", "uLine", "uTex", "uHasTex", "uCut", "uVCol"]) loc[n] = gl.getUniformLocation(prog, n);
  const ext = gl.getExtension("ANGLE_instanced_arrays");
  Object.assign(tv, { gl, prog, loc, ext });
  const rows = [loc.x, loc.y, loc.z, loc.p];
  // sem instâncias: matriz identidade em atributos constantes
  const constant = (m) => {
    for (const a of rows) { gl.disableVertexAttribArray(a); if (ext) ext.vertexAttribDivisorANGLE(a, 0); }
    gl.vertexAttrib3f(loc.x, m[0], m[1], m[2]); gl.vertexAttrib3f(loc.y, m[3], m[4], m[5]);
    gl.vertexAttrib3f(loc.z, m[6], m[7], m[8]); gl.vertexAttrib3f(loc.p, m[9], m[10], m[11]);
  };
  const IDENT = [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0];
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
    if (tv.inst) tvCull();
    gl.uniform1i(loc.uTex, 0);
    gl.enableVertexAttribArray(loc.uv);
    const drawMesh = (r, hi, count) => {
      gl.uniform3fv(loc.uColor, r.color);
      gl.uniform1f(loc.uHi, hi);
      const tex = tvTexture(r.material);
      gl.uniform1i(loc.uHasTex, tex ? 1 : 0);
      gl.uniform1i(loc.uCut, r.material[0] === "g" ? 0 : 1);
      if (tex) { gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, tex); }
      gl.bindBuffer(gl.ARRAY_BUFFER, r.pb);
      gl.vertexAttribPointer(loc.pos, 3, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, r.ub);
      gl.vertexAttribPointer(loc.uv, 2, gl.FLOAT, false, 0, 0);
      if (r.cb && !tex) {
        gl.uniform1i(loc.uVCol, 1);
        gl.bindBuffer(gl.ARRAY_BUFFER, r.cb);
        gl.enableVertexAttribArray(loc.col);
        gl.vertexAttribPointer(loc.col, 4, gl.UNSIGNED_BYTE, true, 0, 0);
      } else { gl.uniform1i(loc.uVCol, 0); gl.disableVertexAttribArray(loc.col); gl.vertexAttrib4f(loc.col, 1, 1, 1, 1); }
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, r.ib);
      const type = r.wide ? gl.UNSIGNED_INT : gl.UNSIGNED_SHORT;
      if (count > 0) ext.drawElementsInstancedANGLE(gl.TRIANGLES, r.count, type, 0, count);
      else gl.drawElements(gl.TRIANGLES, r.count, type, 0);
    };
    gl.uniform1i(loc.uLine, 0);
    constant(IDENT);
    if (tv.showTerrain) for (const r of tv.terrain) drawMesh(r, 0, 0);
    if (tv.inst && ext) {
      for (const ty of tv.typeList) {
        if (!ty.vis) continue;
        gl.bindBuffer(gl.ARRAY_BUFFER, ty.vb);
        rows.forEach((a, k) => {
          gl.enableVertexAttribArray(a);
          gl.vertexAttribPointer(a, 3, gl.FLOAT, false, 48, k * 12);
          ext.vertexAttribDivisorANGLE(a, 1);
        });
        for (const r of ty.meshes) drawMesh(r, 0, ty.vis);
      }
      if (tv.sel >= 0) {
        const ty = tv.typeList[tv.inst.type[tv.sel]], o = tv.sel * 12;
        constant(tv.inst.m.subarray(o, o + 12));
        for (const r of ty.meshes) drawMesh(r, 1, 0);
        constant(IDENT);
      }
    }
    gl.uniform1i(loc.uLine, 1);
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
    const pan = e.button === 2 || e.button === 1 || e.shiftKey && tv.tool === "orbit";
    tv.drag = { x: e.clientX, y: e.clientY, pan, moved: 0, button: e.button, mode: "orbit" };
    if (e.button === 0 && !pan && tv.tool !== "orbit" && tv.inst) {
      const hit = tvPickIndex(e, canvas);
      if (hit >= 0) {
        tvSelect(hit);
        const o = hit * 12, m = tv.inst.m;
        tv.drag.mode = tv.tool;
        tv.drag.i = hit;
        tv.drag.before = tvSnap([hit]);
        tv.drag.base = m.slice(o, o + 12);
        tv.drag.start = tvGround(e, canvas, m[o + 10]);
        tv.drag.sy = e.clientY;
        tv.drag.ex = e.clientX;
      }
    }
  });
  el.addEventListener("pointermove", (e) => {
    const d = tv.drag;
    if (!d) return;
    const dx = e.clientX - d.x, dy = e.clientY - d.y;
    d.moved += Math.abs(dx) + Math.abs(dy);
    if (d.mode === "move" || d.mode === "rotate") {
      const o = d.i * 12, m = tv.inst.m;
      if (d.mode === "move") {
        if (e.shiftKey) {
          m[o + 10] = d.base[10] - (e.clientY - d.sy) * Math.max(0.02, tv.dist * 0.002);
        } else if (d.start) {
          const p = tvGround(e, canvas, d.base[10]);
          if (p) { m[o + 9] = d.base[9] + p[0] - d.start[0]; m[o + 11] = d.base[11] + p[2] - d.start[2]; m[o + 10] = d.base[10]; }
        }
      } else {
        const full = new Float32Array(tv.inst.m.length); full.set(d.base, o);
        tvSpin(m, full, o, (e.clientX - d.ex) * 0.01);
      }
      tv.visKey = "";
      tvStatus();
      return;
    }
    d.x = e.clientX; d.y = e.clientY;
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
    if (!d) return;
    if (d.mode === "move" || d.mode === "rotate") {
      tvCommit(d.mode === "move" ? t("trk.hist.move") : t("trk.hist.rotate"), d.before, tvSnap([d.i]));
      tvEdited();
      return;
    }
    if (d.moved < 5 && d.button === 0) tvPick(e, canvas);
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
// ponto do raio do mouse no plano horizontal y = h (null se o raio não desce até ele)
function tvGround(e, canvas, h) {
  const ray = tvRay(e, canvas);
  if (Math.abs(ray.d[1]) < 1e-6) return null;
  const t = (h - ray.o[1]) / ray.d[1];
  if (t < 0) return null;
  return [ray.o[0] + ray.d[0] * t, h, ray.o[2] + ray.d[2] * t];
}

function tvInvAffine(m) {
  const a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
  const det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g) || 1;
  const r = [(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det, (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det, (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det];
  // linhas de m = base; o inverso do bloco 3×3 (vetor-linha) é a transposta do inverso usual
  return r;
}
function tvPick(e, canvas) { tvSelect(tvPickIndex(e, canvas)); }
function tvPickIndex(e, canvas) {
  if (!tv.inst) return -1;
  const ray = tvRay(e, canvas), inst = tv.inst, R2 = tv.drawDist * tv.drawDist;
  let best = -1, bt = Infinity;
  for (const ty of tv.typeList) {
    if (ty.empty || !tvLayerOn(ty.kind)) continue;
    for (const i of tv.groups[ty.n]) {
      if (inst.hidden[i]) continue;
      const m = inst.m, b = i * 12;
      if (ty.kind !== "dist") { const dx = m[b + 9] - tv.target[0], dz = m[b + 11] - tv.target[2]; if (dx * dx + dz * dz > R2) continue; }
      // leva o raio ao espaço local: p_local = (p - t) * R^-1 (convenção de vetor-linha)
      const inv = tvInvAffine([m[b], m[b + 1], m[b + 2], m[b + 3], m[b + 4], m[b + 5], m[b + 6], m[b + 7], m[b + 8]]);
      const rel = [ray.o[0] - m[b + 9], ray.o[1] - m[b + 10], ray.o[2] - m[b + 11]];
      const loc = (v) => [v[0] * inv[0] + v[1] * inv[3] + v[2] * inv[6], v[0] * inv[1] + v[1] * inv[4] + v[2] * inv[7], v[0] * inv[2] + v[1] * inv[5] + v[2] * inv[8]];
      const o = loc(rel), d = loc(ray.d);
      let t0 = 0, t1 = Infinity, ok = true;
      for (let k = 0; k < 3 && ok; k++) {
        if (Math.abs(d[k]) < 1e-9) { if (o[k] < ty.lo[k] || o[k] > ty.hi[k]) ok = false; continue; }
        let a = (ty.lo[k] - o[k]) / d[k], c = (ty.hi[k] - o[k]) / d[k];
        if (a > c) [a, c] = [c, a];
        t0 = Math.max(t0, a); t1 = Math.min(t1, c);
        if (t0 > t1) ok = false;
      }
      if (ok && t0 < bt) { bt = t0; best = i; }
    }
  }
  return best;
}

// ------------------------------------------------------------ edição e histórico

const TV_HIST_MAX = 300;
const tvSnap = (list) => list.map((i) => ({ i, m: tv.inst.m.slice(i * 12, i * 12 + 12), h: tv.inst.hidden[i] }));
function tvApplySnap(snap) {
  for (const s of snap) { tv.inst.m.set(s.m, s.i * 12); tv.inst.hidden[s.i] = s.h; }
  tv.visKey = "";
}
function tvCommit(label, before, after) {
  const inst = tv.inst;
  if (JSON.stringify(before.map((b) => [...b.m, b.h])) === JSON.stringify(after.map((b) => [...b.m, b.h]))) return;
  inst.hist.length = inst.histPos;
  inst.hist.push({ label, before, after });
  if (inst.hist.length > TV_HIST_MAX) inst.hist.shift();
  inst.histPos = inst.hist.length;
  tvEdited();
}
function tvHistGo(pos) {
  const inst = tv.inst;
  pos = Math.max(0, Math.min(inst.hist.length, pos));
  while (inst.histPos > pos) tvApplySnap(inst.hist[--inst.histPos].before);
  while (inst.histPos < pos) tvApplySnap(inst.hist[inst.histPos++].after);
  tvEdited();
}
const tvUndo = () => { if (!tv.drag) tvHistGo(tv.inst.histPos - 1); };
const tvRedo = () => { if (!tv.drag) tvHistGo(tv.inst.histPos + 1); };

// instâncias que diferem do arquivo original (movidas, giradas ou apagadas), de todas as rotas abertas
function tvEditList() {
  const out = [];
  for (const [route, inst] of tv.cache) {
    for (let i = 0; i < inst.n; i++) {
      const o = i * 12;
      let moved = false;
      for (let k = 0; k < 12 && !moved; k++) if (inst.m[o + k] !== inst.m0[o + k]) moved = true;
      const name = tv.data.type_order[inst.type[i]];
      if (inst.idnum[i] >= TV_ADDED) {
        if (inst.hidden[i]) continue;
        out.push({ route, kind: name[0], type: name.slice(2), added: true, src: inst.idnum[i] - TV_ADDED, index: -1, deleted: false, m: [...inst.m.slice(o, o + 12)], m0: [...inst.m0.slice(o, o + 12)] });
        continue;
      }
      if (!moved && !inst.hidden[i]) continue;
      out.push({ route, kind: name[0], type: name.slice(2), index: inst.idnum[i], deleted: !!inst.hidden[i], m: [...inst.m.slice(o, o + 12)], m0: [...inst.m0.slice(o, o + 12)] });
    }
  }
  return out;
}
function tvEdited() {
  tvStatus(); tvInspect();
  const u = document.getElementById("trk-undo"), r = document.getElementById("trk-redo"), x = document.getElementById("trk-export");
  if (u) u.disabled = !tv.inst || tv.inst.histPos <= 0;
  if (r) r.disabled = !tv.inst || tv.inst.histPos >= tv.inst.hist.length;
  const sv = document.getElementById("trk-save");
  if (sv) sv.disabled = !tv.cache.size || [...tv.cache.values()].every((i) => !i.histPos && !i.hist.length);
  if (x) x.disabled = !tv.cache.size || [...tv.cache.values()].every((i) => !i.histPos && !i.hist.length);
}
const tvDoc = () => ({ format: "dr2-track-edits", version: 1, track: tv.id, src: (TRACKS.find((r) => r.id === tv.id) || {}).src, edits: tvEditList() });
// grava um .nefs novo em build/uiview/saves pelo servidor local; sem servidor, cai no download do JSON
async function tvSave() {
  const note = document.getElementById("trk-note");
  note.textContent = t("trk.saving");
  try {
    const r = await fetch("/api/save", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(tvDoc()) });
    const j = await r.json();
    note.textContent = r.ok ? t("trk.saved", { path: j.path }) : String(j.error || r.status);
  } catch (err) {
    tvExport();
    note.textContent = t("trk.nosave");
  }
}
function tvExport() {
  const blob = new Blob([JSON.stringify(tvDoc(), null, 1)], { type: "application/json" });
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = `${tv.id}.edits.json`;
  a.click();
  URL.revokeObjectURL(a.href);
}

// roda a matriz 3×3 em torno do eixo Y (vetor-linha: v' = v · R)
function tvSpin(m, base, o, th) {
  const c = Math.cos(th), s = Math.sin(th);
  for (let r = 0; r < 3; r++) {
    const x = base[o + r * 3], z = base[o + r * 3 + 2];
    m[o + r * 3] = x * c + z * s;
    m[o + r * 3 + 2] = -x * s + z * c;
  }
}

// duplicar: a cópia entra no fim das matrizes e vai para o arquivo como instância nova (só objects.ens)
const TV_ADDED = 0x80000000;
function tvDuplicateSel() {
  const inst = tv.inst, s0 = tv.sel;
  if (s0 < 0 || tv.typeList[inst.type[s0]].name[0] !== "e") return;
  const n = inst.n;
  const type = new Uint16Array(n + 1), idnum = new Uint32Array(n + 1), m = new Float32Array((n + 1) * 12), m0 = new Float32Array((n + 1) * 12), hidden = new Uint8Array(n + 1);
  type.set(inst.type); idnum.set(inst.idnum); m.set(inst.m); m0.set(inst.m0); hidden.set(inst.hidden);
  type[n] = inst.type[s0];
  idnum[n] = (inst.idnum[s0] >= TV_ADDED ? inst.idnum[s0] : TV_ADDED + inst.idnum[s0]);
  m.set(inst.m.subarray(s0 * 12, s0 * 12 + 12), n * 12);
  m[n * 12 + 9] += 2;
  m0.set(m.subarray(n * 12, n * 12 + 12), n * 12);
  hidden[n] = 1;
  Object.assign(inst, { n: n + 1, type, idnum, m, m0, hidden });
  tvGroups();
  const before = tvSnap([n]);
  inst.hidden[n] = 0; tv.visKey = "";
  tvCommit(t("trk.hist.duplicate"), before, tvSnap([n]));
  tvSelect(n);
}
function tvDeleteSel() {
  if (tv.sel < 0) return;
  const before = tvSnap([tv.sel]);
  tv.inst.hidden[tv.sel] = 1; tv.visKey = "";
  const after = tvSnap([tv.sel]);
  tvCommit(t("trk.hist.delete"), before, after);
  tv.sel = -1;
  tvEdited();
}
function tvSetPos(axis, value) {
  if (tv.sel < 0 || !Number.isFinite(value)) return;
  const before = tvSnap([tv.sel]);
  tv.inst.m[tv.sel * 12 + 9 + axis] = value; tv.visKey = "";
  tvCommit(t("trk.hist.move"), before, tvSnap([tv.sel]));
}
function tvTurnSel(deg) {
  if (tv.sel < 0 || !Number.isFinite(deg)) return;
  const before = tvSnap([tv.sel]);
  tvSpin(tv.inst.m, tv.inst.m.slice(), tv.sel * 12, deg * Math.PI / 180);
  tv.visKey = "";
  tvCommit(t("trk.hist.rotate"), before, tvSnap([tv.sel]));
}
function tvRestoreSel() {
  if (tv.sel < 0) return;
  const before = tvSnap([tv.sel]);
  tv.inst.m.set(tv.inst.m0.subarray(tv.sel * 12, tv.sel * 12 + 12), tv.sel * 12); tv.inst.hidden[tv.sel] = 0; tv.visKey = "";
  tvCommit(t("trk.hist.restore"), before, tvSnap([tv.sel]));
}

function tvSelect(n) {
  tv.sel = n;
  tvStatus(); tvInspect();
}

const tvRouteEntry = () => tv.data.routes.find((r) => r.name === tv.route) || tv.data.routes[0];

function tvInstInfo(i) {
  const ty = tv.typeList[tv.inst.type[i]], o = i * 12, m = tv.inst.m;
  const idnum = tv.inst.idnum[i];
  const route = tv.data.routes.find((r) => r.name === tv.route);
  const id = ty.name[0] === "e" && route && route.ens_ids ? route.ens_ids[idnum] : (ty.name[0] === "o" ? "orn" : "tree") + idnum;
  return { ty, id, pos: [m[o + 9], m[o + 10], m[o + 11]], rows: [...m.subarray(o, o + 9)] };
}

function tvStatus() {
  const note = document.getElementById("trk-note"), sel = document.getElementById("trk-sel");
  if (!note) return;
  if (tv.status) { note.textContent = tv.status; sel.textContent = ""; return; }
  const d = tv.data;
  let shown = 0;
  for (const ty of tv.typeList) shown += ty.vis || 0;
  note.textContent = d && tv.inst ? t("trk.status", { terrain: tvRouteEntry().terrain.meshes.toLocaleString("pt-BR"), inst: tv.inst.n.toLocaleString("pt-BR"), shown: shown.toLocaleString("pt-BR"), types: tv.typeList.length }) : "";
  if (tv.inst && tv.sel >= 0) { const f = tvInstInfo(tv.sel); sel.textContent = `${tvName(f.ty.name)} · ${f.pos.map((v) => v.toFixed(1)).join(" ")}`; } else sel.textContent = "";
}

function tvInspect() {
  const box = inspectEl;
  const row = TRACKS.find((r) => r.id === tv.id);
  if (!row) { box.innerHTML = `<p class="muted">${esc(t("trk.pick"))}</p>`; return; }
  const d = tv.data;
  const kv = (k, v) => `<div>${esc(k)}</div><div class="mono">${esc(String(v))}</div>`;
  const f = tv.inst && tv.sel >= 0 ? tvInstInfo(tv.sel) : null;
  box.innerHTML = `<h2>${esc(row.n)}</h2>
    <p class="muted small">${esc(row.c)} · ${esc(row.src)}</p>
    <div class="kv">
      ${kv(t("trk.terrain"), d ? `${tvRouteEntry().terrain.meshes.toLocaleString("pt-BR")} ${t("trk.blocks")} · ${tvRouteEntry().terrain.verts.toLocaleString("pt-BR")} ${t("mdl.verts")}` : "…")}
      ${kv(t("trk.routes"), (row.routes || []).join(", "))}
      ${kv(t("trk.objects"), d && tv.inst ? `${tv.inst.n.toLocaleString("pt-BR")} ${t("trk.instances")} (${tv.route}) · ${tv.typeList.length} ${t("trk.types")}` : "…")}
    </div>
    ${f ? `<h3>${esc(t("trk.selected"))}</h3><div class="kv">
      ${kv("id", f.id)}${kv(t("trk.type"), tvName(f.ty.name))}${kv(t("trk.kind"), t("trk.kind." + f.ty.kind))}
      ${kv(t("trk.meshes"), f.ty.empty ? t("trk.nomesh") : f.ty.meshes.length)}
    </div>
    <div class="trk-edit">
      ${["x", "y", "z"].map((a, k) => `<label>${a.toUpperCase()}<input type="number" step="0.1" data-trk-pos="${k}" value="${f.pos[k].toFixed(2)}"></label>`).join("")}
    </div>
    <div class="trk-edit">
      <button type="button" data-trk-turn="-15">-15°</button><button type="button" data-trk-turn="15">+15°</button>
      <button type="button" data-trk-turn="-90">-90°</button><button type="button" data-trk-turn="90">+90°</button>
      ${f.ty.name[0] === "e" ? `<button type="button" data-trk-act="duplicate">${esc(t("trk.duplicate"))}</button>` : ""}
      <button type="button" data-trk-act="delete">${esc(t("trk.delete"))}</button>
      <button type="button" data-trk-act="restore">${esc(t("trk.restore"))}</button>
    </div>
    ${tv.inst.hidden[tv.sel] ? "" : `<p class="muted small">${esc(t("trk.collision"))}</p>`}` : `<p class="muted">${esc(t("trk.hint"))}</p>`}
    ${tvDirtyHtml()}`;
}

function tvDirtyHtml() {
  if (!tv.cache.size) return "";
  const n = tvEditList().length;
  return n ? `<h3>${esc(t("trk.edits"))}</h3><p class="mono">${n} ${esc(t("trk.edited"))}</p>` : "";
}

const TV_ACTS = [
  ["showTerrain", "trk.show.terrain"], ["showObjects", "trk.show.objects"], ["showTrees", "trk.show.trees"], ["showDist", "trk.show.dist"], ["showGates", "trk.show.gates"], ["showAi", "trk.show.ai"],
];

function tvTool(k) {
  tv.tool = k;
  document.querySelectorAll("[data-trk-tool]").forEach((b) => b.classList.toggle("on", b.dataset.trkTool === k));
  const c = document.getElementById("trk-view");
  if (c) c.style.cursor = k === "orbit" ? "" : k === "move" ? "move" : "ew-resize";
}

function tvRouteSelect() {
  const el = document.getElementById("trk-route");
  if (!el || !tv.data) return;
  el.innerHTML = tv.data.routes.map((r) => `<option value="${esc(r.name)}">${esc(r.name)} · ${r.instances.toLocaleString("pt-BR")}</option>`).join("");
  el.value = tv.route;
}

function ensureTrackStage() {
  if (document.getElementById("trk-view")) return;
  tv.gen++;
  tv.gl = null; tv.loadedId = null; tv.terrain = []; tv.types = new Map(); tv.typeList = []; tv.inst = null; tv.lines = []; tv.visKey = "";
  const opts = TRACKS.map((r) => `<option value="${esc(r.id)}">${esc(r.c)} · ${esc(r.n)}</option>`).join("");
  contentEl.innerHTML = `<div class="trk-ws">
    <div class="car-tb" role="toolbar">
      <select id="trk-open" title="${esc(t("trk.open"))}">${opts}</select>
      <select id="trk-route" title="${esc(t("trk.route"))}"></select>
      <label class="trk-opt" title="${esc(t("trk.dist"))}">${esc(t("trk.dist"))} <input type="range" id="trk-dd" min="100" max="4000" step="50" value="${tv.drawDist}"> <span id="trk-ddv">${tv.drawDist} m</span></label>
      ${TV_ACTS.map(([k, key]) => `<label class="trk-opt"><input type="checkbox" data-trk="${k}" ${tv[k] ? "checked" : ""}> ${esc(t(key))}</label>`).join("")}
      <button type="button" class="tb-btn" id="trk-frame" title="${esc(t("trk.frame"))}">${esc(t("trk.frame"))}</button>
      <span class="tb-sep"></span>
      ${[["orbit", "trk.tool.orbit"], ["move", "trk.tool.move"], ["rotate", "trk.tool.rotate"]].map(([k, key]) => `<button type="button" class="tb-btn trk-tool${tv.tool === k ? " on" : ""}" data-trk-tool="${k}" title="${esc(t(key))}" aria-label="${esc(t(key))}">${carIco(k)}</button>`).join("")}
      <button type="button" class="tb-btn" id="trk-undo" title="${esc(t("car.undo"))}" aria-label="${esc(t("car.undo"))}" disabled>${carIco("undo")}</button>
      <button type="button" class="tb-btn" id="trk-redo" title="${esc(t("car.redo"))}" aria-label="${esc(t("car.redo"))}" disabled>${carIco("redo")}</button>
      <button type="button" class="tb-btn" id="trk-save" title="${esc(t("trk.save"))}" disabled>${esc(t("trk.save"))}</button>
      <button type="button" class="tb-btn" id="trk-export" title="${esc(t("trk.export"))}" disabled>${esc(t("trk.export"))}</button>
    </div>
    <div class="trk-vp"><canvas class="gl" id="trk-view"></canvas></div>
    <div class="car-sb"><span id="trk-note"></span><span id="trk-sel"></span></div>
  </div>`;
  document.getElementById("trk-open").addEventListener("change", (e) => { tv.id = e.target.value; history.replaceState(null, "", "#p=" + encodeURIComponent(tv.id)); tvOpen(tv.id); });
  document.getElementById("trk-route").addEventListener("change", async (e) => { tv.busy = true; await tvLoadRoute(e.target.value); tv.busy = false; tvFrameRoute(); tvStatus(); tvInspect(); tvEdited(); });
  document.getElementById("trk-dd").addEventListener("input", (e) => { tv.drawDist = Number(e.target.value); document.getElementById("trk-ddv").textContent = tv.drawDist + " m"; });
  contentEl.querySelectorAll("[data-trk-tool]").forEach((b) => b.addEventListener("click", () => tvTool(b.dataset.trkTool)));
  document.getElementById("trk-undo").addEventListener("click", tvUndo);
  document.getElementById("trk-redo").addEventListener("click", tvRedo);
  document.getElementById("trk-export").addEventListener("click", tvExport);
  document.getElementById("trk-save").addEventListener("click", tvSave);
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
  if ((e.ctrlKey || e.metaKey) && !e.altKey) {
    if (k === "z") { e.preventDefault(); if (e.shiftKey) tvRedo(); else tvUndo(); }
    else if (k === "y") { e.preventDefault(); tvRedo(); }
    else if (k === "d") { e.preventDefault(); tvDuplicateSel(); }
    return;
  }
  if (k === "1") tvTool("orbit");
  else if (k === "2") tvTool("move");
  else if (k === "3") tvTool("rotate");
  else if (k === "delete") tvDeleteSel();
  else if (TV_WASD[k] || k === "shift") tv.keys.add(k);
  else if (k === "f" && tv.sel >= 0) tvFrameInst(tv.sel);
  else if (k === "h" && tv.sel >= 0) { tvDeleteSel(); }
  else if (k === "escape") tvSelect(-1);
});
document.addEventListener("keyup", (e) => tv.keys.delete(e.key.toLowerCase()));
window.addEventListener("blur", () => tv.keys.clear());

// painel de edição do objeto selecionado
inspectEl.addEventListener("click", (e) => {
  if (state.mode !== "tracks") return;
  const turn = e.target.closest("[data-trk-turn]");
  if (turn) { tvTurnSel(Number(turn.dataset.trkTurn)); return; }
  const act = e.target.closest("[data-trk-act]");
  if (act) { if (act.dataset.trkAct === "delete") tvDeleteSel(); else if (act.dataset.trkAct === "duplicate") tvDuplicateSel(); else tvRestoreSel(); }
});
inspectEl.addEventListener("change", (e) => {
  if (state.mode !== "tracks") return;
  const pos = e.target.closest("[data-trk-pos]");
  if (pos) tvSetPos(Number(pos.dataset.trkPos), Number(e.target.value));
});
