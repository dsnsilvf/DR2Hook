"use strict";
// Car Model Explorer: árvore real do carro (LOD → nó → fatia), viewport 3D e inspector.
// Lê models/<id>.car.json (hierarquia e metadados) e models/<id>.car.bin (um buffer por
// RENDERDATASOURCE, enviado à GPU uma vez; cada fatia desenha só o seu intervalo).
// Os vértices são locais ao osso: cada nó entra com a matriz `world` do PSSG.
// Somente leitura. O que não foi interpretado aparece como veio em «extra» e «notas».

const CAR_LIST = MODEL_LIST.filter((m) => m.car);
const cv = {
  carId: null, loadedId: null, row: null, data: null,
  nodes: new Map(), slices: new Map(), lodRoots: new Map(),
  res: new Map(), textures: new Map(), matTex: new Map(), matBlend: new Map(),
  selected: null,                 // "n:<uid>" ou "s:<chave da fatia>"
  expanded: new Set(), hidden: new Set(), hiddenSlices: new Set(),
  lod: "LOD0", solo: false, wire: false, flat: false, flip: false, variant: "tarmac", glass: true,
  yaw: 0.7, pitch: 0.3, dist: 5, target: [0, 0.5, 0], drag: null,
  gl: null, program: null, grid: null, loc: null, draw: [], dirty: true, gen: 0, status: "",
};
const CAR_ACCENT = [1, 0.6, 0.15];

function carLog(...args) { console.info("[car]", ...args); }
function carKindLabel(m) { return modelKind(m.k); }

// ------------------------------------------------------------ dados

function carIndex(data) {
  cv.nodes.clear(); cv.slices.clear(); cv.lodRoots.clear();
  const lodByUid = new Map((data.lods || []).map((l) => [l.uid, l.name]));
  const walk = (n, parent, lod) => {
    n._parent = parent;
    n._lod = lodByUid.has(n.uid) ? lodByUid.get(n.uid) : lod;
    cv.nodes.set(n.uid, n);
    let tris = 0, verts = 0;
    n.slices.forEach((s, i) => {
      s.key = n.uid + "." + i;
      s._node = n;
      cv.slices.set(s.key, s);
      if (s.ok) { tris += s.tris; verts += s.vc; }
    });
    for (const c of n.children) { walk(c, n, n._lod); tris += c._tris; verts += c._verts; }
    n._tris = tris; n._verts = verts;
  };
  walk(data.tree, null, null);
}

function parseCarRes(buf) {
  const view = new DataView(buf);
  if (String.fromCharCode(view.getUint8(0), view.getUint8(1), view.getUint8(2), view.getUint8(3)) !== "DR2C") throw new Error("DR2C");
  const dec = new TextDecoder();
  const count = view.getUint32(4, true);
  let o = 8;
  const out = [];
  for (let k = 0; k < count; k++) {
    const nameLen = view.getUint16(o, true);
    const verts = view.getUint32(o + 4, true), indices = view.getUint32(o + 8, true), flags = view.getUint32(o + 12, true);
    o += 16;
    const id = dec.decode(new Uint8Array(buf, o, nameLen)); o += nameLen;
    o = (o + 3) & ~3;
    const pos = new Float32Array(buf, o, verts * 3); o += verts * 12;
    const uv = new Float32Array(buf, o, verts * 2); o += verts * 8;
    const wide = !!(flags & 1);
    const ind = wide ? new Uint32Array(buf, o, indices) : new Uint16Array(buf, o, indices);
    o += indices * (wide ? 4 : 2);
    o = (o + 3) & ~3;
    out.push({ id, verts, pos, uv, ind, wide });
  }
  return out;
}

function carFreeGpu() {
  const gl = cv.gl;
  if (gl) {
    for (const r of cv.res.values()) {
      gl.deleteBuffer(r.pb); gl.deleteBuffer(r.ub); gl.deleteBuffer(r.ib);
      if (r.lb) gl.deleteBuffer(r.lb);
    }
  }
  cv.res.clear();
}

function carUpload(list) {
  const gl = cv.gl;
  carFreeGpu();
  for (const r of list) {
    if (r.wide && !gl.getExtension("OES_element_index_uint")) { carLog("buffer 32-bit sem suporte, ignorado:", r.id); continue; }
    const pb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, pb); gl.bufferData(gl.ARRAY_BUFFER, r.pos, gl.STATIC_DRAW);
    const ub = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, ub); gl.bufferData(gl.ARRAY_BUFFER, r.uv, gl.STATIC_DRAW);
    const ib = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, r.ind, gl.STATIC_DRAW);
    // Arestas para o wireframe: criadas só quando o usuário liga a opção.
    cv.res.set(r.id, { id: r.id, verts: r.verts, tris: r.ind.length / 3, wide: r.wide, pb, ub, ib, lb: null, ind: r.ind });
  }
}

function carLines(r) {
  if (r.lb) return r.lb;
  const gl = cv.gl;
  const n = r.ind.length / 3;
  const lines = r.wide ? new Uint32Array(n * 6) : new Uint16Array(n * 6);
  for (let t = 0; t < n; t++) {
    const a = r.ind[t * 3], b = r.ind[t * 3 + 1], c = r.ind[t * 3 + 2];
    lines.set([a, b, b, c, c, a], t * 6);
  }
  r.lb = gl.createBuffer();
  gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, r.lb);
  gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, lines, gl.STATIC_DRAW);
  return r.lb;
}

function openCar(id) {
  const row = modelById.get(id);
  if (!row || !row.car) return;
  cv.carId = id;
  cv.loadedId = id;
  cv.row = row;
  cv.data = null;
  cv.selected = null; cv.hidden.clear(); cv.hiddenSlices.clear(); cv.expanded.clear(); cv.solo = false;
  cv.matTex.clear(); cv.matBlend.clear();
  cv.status = t("car.loading");
  carStatus(); renderList(); carInspect();
  fetch(row.car).then((r) => { if (!r.ok) throw new Error(String(r.status)); return r.json(); }).then((data) => {
    if (cv.carId !== id) return null;
    cv.data = data;
    carIndex(data);
    carLog(id, "árvore:", data.lods.map((l) => `${l.name}=${l.nodes} nós/${l.slices} fatias/${l.tris} tris`).join(", "),
      "| buffers:", Object.keys(data.resources).length, "| notas:", data.notes.length);
    for (const note of data.notes) console.warn("[car]", note);
    return fetch(data.bin).then((r) => { if (!r.ok) throw new Error(String(r.status)); return r.arrayBuffer(); });
  }).then((buf) => {
    if (!buf || cv.carId !== id) return;
    carUpload(parseCarRes(buf));
    const lods = cv.data.lods.map((l) => l.name);
    cv.lod = lods.includes("LOD0") ? "LOD0" : (lods[0] || "ALL");
    state.filter = cv.lod;
    const top = cv.data.tree;
    cv.expanded.add(top.uid);
    for (const c of top.children) if (!c._lod || c._lod === cv.lod) cv.expanded.add(c.uid);
    cv.dirty = true;
    cv.status = "";
    carFrame(null);
    renderFilters(); renderList(); carInspect(); carStatus(); carSyncBar();
  }).catch((err) => {
    cv.status = String((err && err.message) || err) + " — " + t("car.noload");
    carStatus();
  });
}

// ------------------------------------------------------------ geometria da seleção

function carCorners(bb, m) {
  const out = [];
  for (const x of [bb[0], bb[3]]) for (const y of [bb[1], bb[4]]) for (const z of [bb[2], bb[5]]) {
    out.push([x * m[0] + y * m[4] + z * m[8] + m[12], x * m[1] + y * m[5] + z * m[9] + m[13], x * m[2] + y * m[6] + z * m[10] + m[14]]);
  }
  return out;
}
function carAabb(n, box) {
  if (n._lod && cv.lod !== "ALL" && n._lod !== cv.lod) return;
  if (n.bbox && (n.slices.length || n.type === "MATRIXPALETTEJOINTNODE")) {
    for (const p of carCorners(n.bbox, n.world)) for (let k = 0; k < 3; k++) { box.min[k] = Math.min(box.min[k], p[k]); box.max[k] = Math.max(box.max[k], p[k]); }
  }
  for (const c of n.children) carAabb(c, box);
}
function carSelNode() {
  if (!cv.selected) return null;
  const [kind, key] = cv.selected.split(":");
  if (kind === "n") return cv.nodes.get(Number(key)) || null;
  if (kind === "s") { const s = cv.slices.get(key); return s ? s._node : null; }
  return null;
}
function carFrame(node) {
  const box = { min: [Infinity, Infinity, Infinity], max: [-Infinity, -Infinity, -Infinity] };
  if (node) carAabb(node, box); else if (cv.data) carAabb(cv.data.tree, box);
  if (!isFinite(box.min[0])) return;
  const c = [0, 1, 2].map((k) => (box.min[k] + box.max[k]) / 2);
  const radius = 0.5 * Math.hypot(box.max[0] - box.min[0], box.max[1] - box.min[1], box.max[2] - box.min[2]) || 1;
  cv.target = c;
  cv.dist = Math.max(0.3, (radius / Math.tan(0.45)) * 1.15);
}
function carResetCamera() { cv.yaw = 0.7; cv.pitch = 0.3; carFrame(null); }

// ------------------------------------------------------------ lista (árvore)

function carNodeMatches(n, q) {
  if (n.id.toLowerCase().includes(q)) return true;
  if (n.slices.some((s) => (s.material || "").toLowerCase().includes(q))) return true;
  return n.children.some((c) => carNodeMatches(c, q));
}
function carRows(q) {
  if (!cv.data) return [`<p class="muted" style="padding:12px">${esc(cv.status || t("car.pick"))}</p>`];
  const rows = [];
  const lodOk = (n) => !n._lod || cv.lod === "ALL" || n._lod === cv.lod;
  const eyeOn = (off) => (off ? "○" : "◉");
  const visit = (n, depth, hiddenUp) => {
    if (!lodOk(n)) return;
    if (q && !carNodeMatches(n, q)) return;
    const key = "n:" + n.uid;
    const open = q ? true : cv.expanded.has(n.uid);
    const has = n.children.length || n.slices.length;
    const off = hiddenUp || cv.hidden.has(n.uid);
    const label = n.id + (n.nickname && n.nickname !== n.id ? ` (${n.nickname})` : "");
    rows.push(`<div class="row tree-row${cv.selected === key ? " on" : ""}${off ? " off" : ""}" data-id="${esc(key)}" style="padding-left:${6 + depth * 14}px">
      <span class="tw caret" data-act="toggle" data-uid="${n.uid}">${has ? (open ? "▾" : "▸") : ""}</span>
      <span class="tw eye" data-act="eye" data-uid="${n.uid}" title="${esc(t("car.eye"))}">${eyeOn(cv.hidden.has(n.uid))}</span>
      <span class="nm">${esc(label)}</span><span class="cnt">${n._tris ? n._tris.toLocaleString("pt-BR") : ""}</span></div>`);
    if (!open) return;
    for (const s of n.slices) {
      if (q && !carNodeMatches({ id: "", slices: [s], children: [] }, q) && !n.id.toLowerCase().includes(q)) continue;
      const sk = "s:" + s.key;
      const soff = off || cv.hiddenSlices.has(s.key);
      rows.push(`<div class="row tree-row slice${cv.selected === sk ? " on" : ""}${soff ? " off" : ""}${s.ok ? "" : " bad"}" data-id="${esc(sk)}" style="padding-left:${6 + (depth + 1) * 14}px">
        <span class="tw"></span><span class="tw eye" data-act="seye" data-key="${s.key}" title="${esc(t("car.eye"))}">${eyeOn(cv.hiddenSlices.has(s.key))}</span>
        <span class="nm">${esc(s.material)}</span><span class="cnt">${s.tris.toLocaleString("pt-BR")}</span></div>`);
    }
    for (const c of n.children) visit(c, depth + 1, off);
  };
  visit(cv.data.tree, 0, false);
  return rows.length ? rows : [`<p class="muted" style="padding:12px">${esc(t("nothing"))}</p>`];
}

function carListClick(e) {
  const act = e.target.closest("[data-act]");
  const row = e.target.closest(".tree-row");
  if (!row) return;
  e.stopPropagation();
  if (act && act.dataset.act === "toggle") {
    const uid = Number(act.dataset.uid);
    if (cv.expanded.has(uid)) cv.expanded.delete(uid); else cv.expanded.add(uid);
    renderList();
    return;
  }
  if (act && act.dataset.act === "eye") { carToggleHidden(Number(act.dataset.uid)); return; }
  if (act && act.dataset.act === "seye") { carToggleSliceHidden(act.dataset.key); return; }
  carSelect(row.dataset.id);
}
function carSelect(id) {
  cv.selected = id;
  cv.dirty = true;
  renderList();
  carInspect();
  carSyncBar();
}
function carToggleHidden(uid) {
  if (cv.hidden.has(uid)) cv.hidden.delete(uid); else cv.hidden.add(uid);
  cv.dirty = true; renderList();
}
function carToggleSliceHidden(key) {
  if (cv.hiddenSlices.has(key)) cv.hiddenSlices.delete(key); else cv.hiddenSlices.add(key);
  cv.dirty = true; renderList();
}

// ------------------------------------------------------------ materiais e texturas

function carMaterial(name) { return (cv.data && cv.data.materials[name]) || { id: name, group: "", params: {}, textures: {}, extra: {} }; }
function carSliceVisible(material) {
  if (/disc_blur/i.test(material)) return false;
  const v = meshVariant(material);
  return v === "base" || cv.variant === "all" || v === cv.variant;
}
function carMatTexId(name) {
  if (!cv.matTex.has(name)) {
    const guess = guessTexture(name, (cv.row && cv.row.tex) || []);
    cv.matTex.set(name, guess ? guess.g + "/" + guess.n : null);
  }
  return cv.matTex.get(name);
}
function carIsBlend(name) {
  if (!cv.matBlend.has(name)) cv.matBlend.set(name, /glass/i.test(carMaterial(name).group) || /glass/i.test(name));
  return cv.matBlend.get(name);
}
function carTexture(id) {
  if (!id) return null;
  if (cv.textures.has(id)) return cv.textures.get(id);
  cv.textures.set(id, null);
  const asset = assetById.get(id);
  if (!asset || !cv.gl) return null;
  const image = new Image();
  const gen = cv.gen;
  image.onload = () => {
    const gl = cv.gl;
    if (!gl || gen !== cv.gen) return;
    const tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 0);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, image);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
    gl.generateMipmap(gl.TEXTURE_2D);
    cv.textures.set(id, tex);
  };
  image.src = asset.p;
  return null;
}

// ------------------------------------------------------------ desenho

function carCollect() {
  const out = [];
  const [skind, skey] = cv.selected ? cv.selected.split(":") : [null, null];
  const selUid = skind === "n" ? Number(skey) : null;
  const visit = (n, hiddenUp, selUp) => {
    if (n._lod && cv.lod !== "ALL" && n._lod !== cv.lod) return;
    const off = hiddenUp || cv.hidden.has(n.uid);
    const inSel = selUp || n.uid === selUid;
    if (!off) {
      for (const s of n.slices) {
        if (!s.ok || cv.hiddenSlices.has(s.key) || !carSliceVisible(s.material)) continue;
        const sliceSel = skind === "s" && skey === s.key;
        if (cv.solo && cv.selected && !(inSel || sliceSel)) continue;
        out.push({ s, n, hi: !cv.solo && (inSel || sliceSel) });
      }
    }
    for (const c of n.children) visit(c, off, inSel);
  };
  if (cv.data) visit(cv.data.tree, false, false);
  return out;
}

function carGL() {
  const canvas = document.getElementById("car-view");
  const gl = canvas.getContext("webgl", { antialias: true, alpha: false });
  if (!gl) return null;
  const deriv = gl.getExtension("OES_standard_derivatives");
  gl.getExtension("OES_element_index_uint");
  const vs = `attribute vec3 aPos; attribute vec2 aUv; uniform mat4 uVp; uniform mat4 uModel; uniform mat4 uView;
    varying vec2 vUv; varying vec3 vView;
    void main() { vec4 w = uModel * vec4(aPos, 1.0); vUv = aUv; vView = (uView * w).xyz; gl_Position = uVp * w; }`;
  const fs = `${deriv ? "#extension GL_OES_standard_derivatives : enable\n" : ""}
    precision mediump float; varying vec2 vUv; varying vec3 vView;
    uniform sampler2D uTex; uniform vec3 uColor; uniform int uHasTex; uniform int uFlip; uniform int uTread;
    uniform float uHi; uniform float uAlpha; uniform int uLine;
    void main() {
      if (uLine == 1) { gl_FragColor = vec4(uColor, 1.0); return; }
      ${deriv ? "vec3 n = normalize(cross(dFdx(vView), dFdy(vView))); float light = abs(dot(n, normalize(vec3(0.3, 0.85, 0.45)))) * 0.7 + 0.3;" : "float light = 1.0;"}
      vec2 uv = vUv;
      if (uTread == 1) uv = vec2(0.125 + fract(vUv.x) * 0.25, 0.848 + fract(vUv.y) * 0.062);
      else if (uFlip == 1) uv.y = 1.0 - uv.y;
      vec3 c = uHasTex == 1 ? texture2D(uTex, uv).rgb : uColor;
      gl_FragColor = vec4(mix(c * light, vec3(1.0, 0.6, 0.15), uHi * 0.45), uAlpha);
    }`;
  const compile = (type, src) => {
    const sh = gl.createShader(type);
    gl.shaderSource(sh, src); gl.compileShader(sh);
    if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(sh));
    return sh;
  };
  const program = gl.createProgram();
  gl.attachShader(program, compile(gl.VERTEX_SHADER, vs));
  gl.attachShader(program, compile(gl.FRAGMENT_SHADER, fs));
  gl.linkProgram(program);
  if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(program));
  gl.useProgram(program);
  gl.enable(gl.DEPTH_TEST);
  gl.disable(gl.CULL_FACE);
  const grid = gl.createBuffer();
  const lines = [];
  for (let i = -4; i <= 4; i++) lines.push(i, 0, -4, i, 0, 4, -4, 0, i, 4, 0, i);
  gl.bindBuffer(gl.ARRAY_BUFFER, grid);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(lines), gl.STATIC_DRAW);
  const loc = { pos: gl.getAttribLocation(program, "aPos"), uv: gl.getAttribLocation(program, "aUv") };
  for (const name of ["uVp", "uModel", "uView", "uTex", "uColor", "uHasTex", "uFlip", "uTread", "uHi", "uAlpha", "uLine"]) loc[name] = gl.getUniformLocation(program, name);
  Object.assign(cv, { gl, program, grid: { buf: grid, count: lines.length / 3 }, loc });

  const IDENT = new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);
  const gen = cv.gen;
  const frame = () => {
    if (gen !== cv.gen || !canvas.isConnected) return;
    requestAnimationFrame(frame);
    if (state.mode !== "cars") return;
    if (cv.dirty) { cv.draw = carCollect(); cv.dirty = false; carStatus(); }
    const w = canvas.clientWidth || 800, h = canvas.clientHeight || 480;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    if (canvas.width !== Math.floor(w * dpr) || canvas.height !== Math.floor(h * dpr)) {
      canvas.width = Math.floor(w * dpr); canvas.height = Math.floor(h * dpr);
    }
    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.clearColor(0.05, 0.06, 0.08, 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    const cam = carCamera();
    const view = mat4Look(cam.eye, cam.look);
    const vp = mat4Mul(mat4Perspective(0.9, canvas.width / Math.max(1, canvas.height), 0.05, 200), view);
    gl.useProgram(program);
    gl.uniformMatrix4fv(loc.uVp, false, vp);
    gl.uniformMatrix4fv(loc.uView, false, view);
    gl.uniform1i(loc.uFlip, cv.flip ? 1 : 0);
    gl.uniform1i(loc.uTex, 0);
    gl.uniform1f(loc.uHi, 0);
    gl.uniform1f(loc.uAlpha, 1);
    gl.uniform1i(loc.uTread, 0);
    gl.uniform1i(loc.uHasTex, 0);
    gl.uniform1i(loc.uLine, 1);
    gl.uniform3f(loc.uColor, 0.25, 0.28, 0.32);
    gl.uniformMatrix4fv(loc.uModel, false, IDENT);
    gl.bindBuffer(gl.ARRAY_BUFFER, grid);
    gl.enableVertexAttribArray(loc.pos);
    gl.vertexAttribPointer(loc.pos, 3, gl.FLOAT, false, 0, 0);
    gl.disableVertexAttribArray(loc.uv);
    gl.vertexAttrib2f(loc.uv, 0, 0);
    gl.drawArrays(gl.LINES, 0, cv.grid.count);
    gl.uniform1i(loc.uLine, 0);

    const drawOne = (d, blend) => {
      const r = cv.res.get(d.s.rds);
      if (!r) return;
      const s = d.s;
      gl.bindBuffer(gl.ARRAY_BUFFER, r.pb);
      gl.enableVertexAttribArray(loc.pos);
      gl.vertexAttribPointer(loc.pos, 3, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, r.ub);
      gl.enableVertexAttribArray(loc.uv);
      gl.vertexAttribPointer(loc.uv, 2, gl.FLOAT, false, 0, 0);
      if (!d.n._m) d.n._m = new Float32Array(d.n.world);
      gl.uniformMatrix4fv(loc.uModel, false, d.n._m);
      gl.uniform1f(loc.uHi, d.hi ? 1 : 0);
      gl.uniform1f(loc.uAlpha, blend ? 0.4 : 1);
      gl.uniform1i(loc.uTread, isTread(s.material) ? 1 : 0);
      const tex = cv.flat ? null : carTexture(carMatTexId(s.material));
      if (tex) {
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, tex);
        gl.uniform1i(loc.uHasTex, 1);
      } else {
        const c = colorOf(s.material);
        gl.uniform1i(loc.uHasTex, 0);
        gl.uniform3f(loc.uColor, c[0], c[1], c[2]);
      }
      const size = r.wide ? 4 : 2;
      const type = r.wide ? gl.UNSIGNED_INT : gl.UNSIGNED_SHORT;
      if (cv.wire) {
        gl.uniform1i(loc.uLine, 1);
        gl.uniform3f(loc.uColor, d.hi ? 1 : 0.7, d.hi ? 0.6 : 0.8, d.hi ? 0.15 : 0.9);
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, carLines(r));
        gl.drawElements(gl.LINES, s.ic * 2, type, s.io * 2 * size);
        gl.uniform1i(loc.uLine, 0);
      } else {
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, r.ib);
        gl.drawElements(gl.TRIANGLES, s.ic, type, s.io * size);
      }
    };
    const opaque = [], glass = [];
    for (const d of cv.draw) (cv.glass && carIsBlend(d.s.material) ? glass : opaque).push(d);
    for (const d of opaque) drawOne(d, false);
    if (glass.length) {
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
      gl.depthMask(false);
      for (const d of glass) drawOne(d, true);
      gl.depthMask(true);
      gl.disable(gl.BLEND);
    }
  };

  canvas.addEventListener("contextmenu", (e) => e.preventDefault());
  canvas.addEventListener("pointerdown", (e) => {
    if (e.button > 2) return;
    const pan = e.button === 1 || e.button === 2 || e.shiftKey;
    const cam = carCamera();
    cv.drag = { x: e.clientX, y: e.clientY, yaw: cv.yaw, pitch: cv.pitch, pan, target: cv.target.slice(), right: cam.right, up: cam.up };
    canvas.setPointerCapture(e.pointerId);
    if (pan) e.preventDefault();
  });
  canvas.addEventListener("pointermove", (e) => {
    const d = cv.drag;
    if (!d) return;
    const dx = e.clientX - d.x, dy = e.clientY - d.y;
    if (d.pan) {
      const k = cv.dist * 0.0022;
      for (let i = 0; i < 3; i++) cv.target[i] = d.target[i] + (d.right[i] * dx - d.up[i] * dy) * k;
      return;
    }
    cv.yaw = d.yaw - dx * 0.008;
    cv.pitch = Math.max(-1.4, Math.min(1.4, d.pitch + dy * 0.008));
  });
  canvas.addEventListener("pointerup", () => { cv.drag = null; });
  canvas.addEventListener("pointercancel", () => { cv.drag = null; });
  canvas.addEventListener("wheel", (e) => {
    cv.dist = Math.max(0.3, Math.min(60, cv.dist * (e.deltaY > 0 ? 1.08 : 0.92)));
    e.preventDefault();
  }, { passive: false });
  requestAnimationFrame(frame);
  return gl;
}

function carCamera() {
  const cp = Math.cos(cv.pitch), sp = Math.sin(cv.pitch), cy = Math.cos(cv.yaw), sy = Math.sin(cv.yaw);
  const dir = [sy * cp, sp, cy * cp];
  const look = cv.target;
  const eye = [look[0] + dir[0] * cv.dist, look[1] + dir[1] * cv.dist, look[2] + dir[2] * cv.dist];
  let rx = dir[2], rz = -dir[0];
  const rl = Math.hypot(rx, rz) || 1;
  rx /= rl; rz /= rl;
  const up = [dir[1] * rz, dir[2] * rx - dir[0] * rz, -dir[1] * rx];
  return { eye, look, right: [rx, 0, rz], up };
}

// ------------------------------------------------------------ barra, palco e inspector

function carStatus() {
  const el = document.getElementById("car-note");
  if (!el) return;
  if (cv.status) { el.textContent = cv.status; return; }
  const tris = cv.draw.reduce((a, d) => a + d.s.tris, 0);
  el.textContent = t("car.status", { slices: cv.draw.length, tris: tris.toLocaleString("pt-BR") }) + " · " + t("mdl.drag");
}
function carSyncBar() {
  const solo = document.getElementById("car-solo");
  if (solo) { solo.classList.toggle("on", cv.solo); solo.disabled = !cv.selected; }
  const focus = document.getElementById("car-focus");
  if (focus) focus.disabled = !cv.selected;
  const lod = document.getElementById("car-lod");
  if (lod && cv.data) {
    lod.innerHTML = [...cv.data.lods.map((l) => l.name), "ALL"].map((n) => `<option value="${esc(n)}"${n === cv.lod ? " selected" : ""}>${esc(n === "ALL" ? t("mdl.all") : n)}</option>`).join("");
  }
  const sel = document.getElementById("car-open");
  if (sel) sel.value = cv.carId || "";
}

function ensureCarStage() {
  if (document.getElementById("car-view")) return;
  cv.gen++;
  cv.gl = null; cv.res.clear(); cv.textures.clear(); cv.loadedId = null;
  const options = CAR_LIST.map((m) => `<option value="${esc(m.id)}">${esc(m.n)} · ${esc(carKindLabel(m))}</option>`).join("");
  contentEl.innerHTML = `<div class="model-stage">
    <div class="model-bar car-bar">
      <label>${esc(t("car.open"))} <select id="car-open">${options}</select></label>
      <label>${esc(t("car.lod"))} <select id="car-lod"></select></label>
      <label>${esc(t("mdl.surface"))}
        <select id="car-surface">
          <option value="tarmac">${esc(t("mdl.tarmac"))}</option><option value="gravel">${esc(t("mdl.gravel"))}</option>
          <option value="snow">${esc(t("mdl.snow"))}</option><option value="all">${esc(t("mdl.all"))}</option>
        </select></label>
      <label><input type="checkbox" id="car-wire"> ${esc(t("car.wire"))}</label>
      <label><input type="checkbox" id="car-mats" checked> ${esc(t("car.mats"))}</label>
      <label><input type="checkbox" id="car-glass" checked> ${esc(t("car.glass"))}</label>
      <label><input type="checkbox" id="car-flip"> ${esc(t("mdl.flip"))}</label>
      <button id="car-reset">${esc(t("car.reset"))}</button>
      <button id="car-focus" disabled>${esc(t("car.focus"))}</button>
      <button id="car-solo" disabled>${esc(t("car.solo"))}</button>
      <button id="car-show">${esc(t("car.showall"))}</button>
    </div>
    <canvas class="gl" id="car-view"></canvas>
    <div class="model-foot" id="car-note"></div>
  </div>`;
  const on = (id, ev, fn) => document.getElementById(id).addEventListener(ev, fn);
  on("car-open", "change", (e) => { openCar(e.target.value); history.replaceState(null, "", "#k=" + encodeURIComponent(e.target.value)); });
  on("car-lod", "change", (e) => {
    cv.lod = e.target.value; state.filter = cv.lod; cv.dirty = true;
    renderFilters(); renderList(); carFrame(null);
  });
  on("car-surface", "change", (e) => { cv.variant = e.target.value; cv.dirty = true; });
  on("car-wire", "change", (e) => { cv.wire = e.target.checked; });
  on("car-mats", "change", (e) => { cv.flat = !e.target.checked; });
  on("car-glass", "change", (e) => { cv.glass = e.target.checked; });
  on("car-flip", "change", (e) => { cv.flip = e.target.checked; });
  on("car-reset", "click", carResetCamera);
  on("car-focus", "click", () => carFrame(carSelNode()));
  on("car-solo", "click", () => { cv.solo = !cv.solo; cv.dirty = true; carSyncBar(); });
  on("car-show", "click", () => { cv.hidden.clear(); cv.hiddenSlices.clear(); cv.solo = false; cv.dirty = true; renderList(); carSyncBar(); });
  try { carGL(); } catch (err) { document.getElementById("car-note").textContent = String((err && err.message) || err); }
}

const carNum = (v, d = 4) => (typeof v === "number" ? String(Math.round(v * 10 ** d) / 10 ** d) : String(v));
function carMatrix(m) {
  const rows = [];
  for (let r = 0; r < 4; r++) rows.push(m.slice(r * 4, r * 4 + 4).map((v) => carNum(v, 4).padStart(9)).join(" "));
  return `<pre class="xml">${esc(rows.join("\n"))}</pre>`;
}
function carExtra(extra) {
  const keys = Object.keys(extra || {});
  return keys.length ? `<div class="kv">${keys.map((k) => `<div class="mono">${esc(k)}</div><div class="mono">${esc(JSON.stringify(extra[k]))}</div>`).join("")}</div>` : `<p class="muted small">—</p>`;
}
function carMaterialHtml(name) {
  const m = carMaterial(name);
  const params = Object.entries(m.params || {}).map(([k, v]) => `<div class="mono">${esc(k)}</div><div class="mono">${esc(JSON.stringify(v))}</div>`).join("");
  const tid = carMatTexId(name);
  const asset = tid && assetById.get(tid);
  const declared = Object.entries(m.textures || {}).map(([k, v]) => `<div class="mono">${esc(k)}</div><div class="mono">${esc(v)}</div>`).join("");
  return `<div class="kv">
      <div>${t("car.shader")}</div><div class="mono">${esc(m.group || "—")}</div>
      <div>${t("car.params")}</div><div>${m.paramCount == null ? "—" : `${m.savedCount}/${m.paramCount}`}</div>
      <div>${t("car.blend")}</div><div>${carIsBlend(name) ? t("car.yes") : t("car.no")}</div>
      <div>${t("mdl.textures")}</div><div class="mono">${asset ? esc(asset.n) : "—"} <span class="muted">${t("car.guessed")}</span></div>
    </div>
    ${asset ? `<div class="tile"><span class="pic bg-checker"><img src="${esc(asset.t)}" alt=""></span></div>` : ""}
    ${params ? `<h3>${esc(t("car.shaderparams"))}</h3><div class="kv">${params}</div>` : ""}
    ${declared ? `<h3>${esc(t("car.declared"))}</h3><div class="kv">${declared}</div>` : `<p class="muted small">${esc(t("car.nodeclared"))}</p>`}
    ${carExtra(m.extra)}`;
}
function carSliceHtml(s) {
  const r = cv.data.resources[s.rds];
  return `<div class="kv">
    <div>${t("car.slice")}</div><div class="mono">${esc(s.id)}</div>
    <div>${t("car.material")}</div><div class="mono">${esc(s.material)}</div>
    <div>${t("car.buffer")}</div><div class="mono">${esc(s.rds)}${r ? ` (${r.verts.toLocaleString("pt-BR")} v, ${r.tris.toLocaleString("pt-BR")} t)` : ""}</div>
    <div>${t("car.vrange")}</div><div class="mono">${s.vo} + ${s.vc}</div>
    <div>${t("car.irange")}</div><div class="mono">${s.io} + ${s.ic}</div>
    <div>${t("mdl.tris")}</div><div>${s.tris.toLocaleString("pt-BR")}</div>
    <div>jointID</div><div class="mono">${esc(s.joint)}</div>
    <div>${t("car.state")}</div><div>${s.ok ? "OK" : `<span class="warn">${esc(t("car.badslice"))}</span>`}</div>
  </div>
  ${carExtra(s.extra)}`;
}

function carInspect() {
  if (!cv.data) { inspectEl.innerHTML = `<p class="muted">${esc(cv.status || t("car.pick"))}</p>`; return; }
  const d = cv.data, src = d.source || {};
  const node = carSelNode();
  const head = `<h2 class="mono">${esc(cv.row ? cv.row.n : d.id)}</h2>
    <div class="kv">
      <div>${t("mdl.package")}</div><div class="mono">${esc(src.package || "—")}</div>
      <div>${t("mdl.path")}</div><div class="mono">${esc(src.path || "—")}</div>
      <div>${t("car.lods")}</div><div>${d.lods.map((l) => `${esc(l.name)}: ${l.nodes} / ${l.slices} / ${l.tris.toLocaleString("pt-BR")}`).join("<br>") || "—"}</div>
      <div>${t("car.buffers")}</div><div>${Object.keys(d.resources).length}</div>
      <div>${t("car.materials")}</div><div>${Object.keys(d.materials).length}</div>
    </div>`;
  const notes = d.notes.length ? section("car.notes", `<div class="mono small">${d.notes.map(esc).join("<br>")}</div>`, true) : "";
  if (!node) {
    const mats = Object.keys(d.materials).map((n) => `<div class="mono">${esc(n)} <span class="muted">${esc(d.materials[n].group)}</span></div>`).join("");
    inspectEl.innerHTML = head + notes + section("mdl.mats", mats || "—", true) + `<p class="muted small">${esc(t("car.hint"))}</p>`;
    return;
  }
  const [kind, key] = cv.selected.split(":");
  let body;
  if (kind === "s") {
    const s = cv.slices.get(key);
    body = `<h3>${esc(t("car.slice"))}</h3>${carSliceHtml(s)}
      ${section("car.material", carMaterialHtml(s.material), true)}
      <p class="muted small">${esc(t("car.owner"))}: <span class="mono">${esc(node.id)}</span></p>`;
  } else {
    const matNames = [...new Set(node.slices.map((s) => s.material))];
    const bb = node.bbox;
    body = `<h3>${esc(node.id)}</h3><div class="kv">
        <div>${t("car.pssgtype")}</div><div class="mono">${esc(node.type)}</div>
        <div>uid</div><div class="mono">${node.uid}</div>
        <div>${t("car.nickname")}</div><div class="mono">${esc(node.nickname || "—")}</div>
        <div>${t("car.parent")}</div><div class="mono">${esc(node._parent ? node._parent.id : "—")}</div>
        <div>${t("car.children")}</div><div>${node.children.length}</div>
        <div>${t("car.slices")}</div><div>${node.slices.length}</div>
        <div>${t("mdl.tris")}</div><div>${node._tris.toLocaleString("pt-BR")}</div>
        <div>${t("mdl.verts")}</div><div>${node._verts.toLocaleString("pt-BR")}</div>
        <div>${t("car.bbox")}</div><div class="mono">${bb ? esc(bb.map((v) => carNum(v, 3)).join(", ")) : "—"}</div>
      </div>
      ${section("car.local", carMatrix(node.local) + (node.identity ? `<p class="muted small">${esc(t("car.identity"))}</p>` : ""), true)}
      ${section("car.world", carMatrix(node.world), false)}
      ${node.slices.length ? section("car.slices", node.slices.map((s) => `<div class="beh"><div class="desc mono">${esc(s.id)} · ${esc(s.material)}</div><div class="raw">${esc(s.rds)} v ${s.vo}+${s.vc} i ${s.io}+${s.ic} · ${s.tris} tris</div></div>`).join(""), true) : ""}
      ${matNames.length ? section("mdl.mats", matNames.map((n) => `<details><summary class="mono">${esc(n)}</summary>${carMaterialHtml(n)}</details>`).join(""), false) : ""}
      ${section("car.extra", carExtra(node.extra), false)}`;
  }
  inspectEl.innerHTML = head + notes + body;
}

MODES.cars = {
  stage: false, hash: "k", defaultFilter: "LOD0",
  filters() {
    const lods = cv.data ? cv.data.lods.map((l) => l.name) : ["LOD0"];
    return [...lods.map((n) => [n, n]), ["ALL", t("mdl.all")]];
  },
  list: (q) => carRows(q),
  current: () => cv.carId,
  select(id) {
    if (modelById.has(id) && modelById.get(id).car) cv.carId = id;
  },
  onFilter() {
    if (!cv.data) return;
    cv.lod = state.filter; cv.dirty = true;
    renderList(); carFrame(null); carSyncBar();
  },
  show() {
    ensureCarStage();
    if (!cv.carId) { const first = CAR_LIST.find((m) => m.k === "carro") || CAR_LIST[0]; if (first) cv.carId = first.id; }
    if (cv.carId && cv.loadedId !== cv.carId) openCar(cv.carId);
    carSyncBar(); carInspect(); carStatus();
  },
};
document.getElementById("list").addEventListener("click", (e) => { if (state.mode === "cars") carListClick(e); }, true);
document.addEventListener("keydown", (e) => {
  if (state.mode !== "cars" || /INPUT|SELECT|TEXTAREA/.test((e.target || {}).tagName || "")) return;
  if (e.key === "f" || e.key === "F") carFrame(carSelNode());
  else if ((e.key === "h" || e.key === "H") && cv.selected) {
    const [kind, key] = cv.selected.split(":");
    if (kind === "n") carToggleHidden(Number(key)); else carToggleSliceHidden(key);
  }
});

// content.js e este arquivo registram as abas; só agora a página inicia.
start();
