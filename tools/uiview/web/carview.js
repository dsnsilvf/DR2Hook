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
  fixes: [], res: new Map(), textures: new Map(), matTex: new Map(), matBlend: new Map(),
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
    for (const f of cv.fixes) {
      gl.deleteBuffer(f.pb); gl.deleteBuffer(f.ub); gl.deleteBuffer(f.ib);
      if (f.lb) gl.deleteBuffer(f.lb);
    }
  }
  cv.res.clear();
  cv.fixes.length = 0;
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
    cv.res.set(r.id, { id: r.id, verts: r.verts, tris: r.ind.length / 3, wide: r.wide, pb, ub, ib, lb: null, ind: r.ind, pos: r.pos, uv: r.uv });
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

// ------------------------------------------------------------ correções de roda e disco
// O UV gravado no barril da roda e no disco não serve para o atlas: os mesmos remendos do
// Modelos (centro aberto, borracha por dentro, UV circular do rotor) rodam aqui, por LOD e
// material, em coordenadas do carro. Cada fatia recebe um buffer próprio já corrigido, e a
// malha original no arquivo continua intacta.

function carFixKind(material) {
  if (/disc_blur/i.test(material)) return null;
  if (/disc/i.test(material)) return "disc";
  if (/wheel/i.test(material) && !isTread(material)) return "wheel";
  return null;
}

function carWheelMesh(items) {
  const pos = [], uv = [], ind = [], owner = [];
  for (const { s, n } of items) {
    const r = cv.res.get(s.rds);
    if (!r || !r.pos) continue;
    const m = n.world;
    const remap = new Map();
    for (let k = 0; k < s.ic; k++) {
      const old = r.ind[s.io + k];
      let id = remap.get(old);
      if (id === undefined) {
        id = pos.length / 3;
        remap.set(old, id);
        const x = r.pos[old * 3], y = r.pos[old * 3 + 1], z = r.pos[old * 3 + 2];
        pos.push(m[0] * x + m[4] * y + m[8] * z + m[12], m[1] * x + m[5] * y + m[9] * z + m[13], m[2] * x + m[6] * y + m[10] * z + m[14]);
        uv.push(r.uv[old * 2], r.uv[old * 2 + 1]);
      }
      ind.push(id);
      if (k % 3 === 2) owner.push(s);
    }
  }
  return { pos: Float32Array.from(pos), uv: Float32Array.from(uv), ind: Uint32Array.from(ind), owner };
}

// Mesmos cortes de openWheelCenter e rubberizeInnerWheel (content.js), mantendo o dono de cada triângulo.
function carOpenCenter(mesh) {
  const { groups, which } = assignWheels(mesh.pos);
  if (!groups.length) return;
  const rad = (i, g) => Math.hypot(mesh.pos[i * 3 + 1] - g.y, mesh.pos[i * 3 + 2] - g.z) / g.rmax;
  const keep = [], owner = [];
  for (let t = 0; t < mesh.ind.length; t += 3) {
    const a = mesh.ind[t], b = mesh.ind[t + 1], c = mesh.ind[t + 2];
    const u = (fract1(mesh.uv[a * 2]) + fract1(mesh.uv[b * 2]) + fract1(mesh.uv[c * 2])) / 3;
    const v = (fract1(mesh.uv[a * 2 + 1]) + fract1(mesh.uv[b * 2 + 1]) + fract1(mesh.uv[c * 2 + 1])) / 3;
    const gi = which[a];
    const g = gi >= 0 && which[b] === gi && which[c] === gi ? groups[gi] : null;
    const inner = g && g.rmax > 1e-4 && (rad(a, g) + rad(b, g) + rad(c, g)) / 3 < 0.36;
    if (inner && u < 0.32 && v > 0.75) continue;
    keep.push(a, b, c); owner.push(mesh.owner[t / 3]);
  }
  mesh.ind = Uint32Array.from(keep);
  mesh.owner = owner;
}

function carRubberInner(mesh) {
  const { groups } = assignWheels(mesh.pos);
  if (!groups.length) return;
  const n = mesh.pos.length / 3;
  const which = new Int16Array(n).fill(-1);
  const depth = new Float32Array(n);
  groups.forEach((g, gi) => {
    const side = Math.sign(g.x) || 1;
    let face = g.x;
    for (const i of g.ids) {
      const x = mesh.pos[i * 3];
      face = side < 0 ? Math.min(face, x) : Math.max(face, x);
    }
    for (const i of g.ids) { which[i] = gi; depth[i] = (face - mesh.pos[i * 3]) * side; }
  });
  const ind = mesh.ind;
  const outer = new Uint8Array(n);
  for (let t = 0; t < ind.length; t += 3) {
    if (Math.max(depth[ind[t]], depth[ind[t + 1]], depth[ind[t + 2]]) < 0.1) outer[ind[t]] = outer[ind[t + 1]] = outer[ind[t + 2]] = 1;
  }
  const rubber = (g, i) => {
    const dy = mesh.pos[i * 3 + 1] - g.y, dz = mesh.pos[i * 3 + 2] - g.z;
    const ang = Math.atan2(dz, dy);
    const r = Math.min(1, Math.hypot(dy, dz) / (g.rmax || 1));
    return [0.16 + ((ang / (Math.PI * 2) + 1) % 1) * 0.18, 0.855 + r * 0.04];
  };
  const pos = Array.from(mesh.pos), uv = Array.from(mesh.uv), keep = [];
  for (let t = 0; t < ind.length; t += 3) {
    const tri = [ind[t], ind[t + 1], ind[t + 2]];
    // Qualquer vértice fundo põe o triângulo inteiro na borracha: os que ligam a borda ao fundo
    // do barril têm profundidade média baixa e, de outro modo, mostrariam o atlas inteiro.
    const d = Math.max(depth[tri[0]], depth[tri[1]], depth[tri[2]]);
    if (d >= 0.1 && tri.every((i) => which[i] >= 0)) {
      for (let k = 0; k < 3; k++) {
        const i = tri[k];
        const rub = rubber(groups[which[i]], i);
        if (outer[i]) {
          tri[k] = pos.length / 3;
          pos.push(mesh.pos[i * 3], mesh.pos[i * 3 + 1], mesh.pos[i * 3 + 2]);
          uv.push(rub[0], rub[1]);
        } else { uv[i * 2] = rub[0]; uv[i * 2 + 1] = rub[1]; }
      }
    }
    keep.push(tri[0], tri[1], tri[2]);
  }
  mesh.pos = Float32Array.from(pos); mesh.uv = Float32Array.from(uv); mesh.ind = Uint32Array.from(keep);
}

function carFixWheels() {
  const gl = cv.gl;
  if (!gl || !cv.data) return;
  const groups = new Map();
  for (const s of cv.slices.values()) {
    const kind = s.ok && carFixKind(s.material);
    if (!kind) continue;
    const key = kind + "|" + (s._node._lod || "") + "|" + s.material;
    if (!groups.has(key)) groups.set(key, { kind, items: [] });
    groups.get(key).items.push({ s, n: s._node });
  }
  for (const { kind, items } of groups.values()) {
    const mesh = carWheelMesh(items);
    if (!mesh.ind.length) continue;
    if (kind === "disc") mapDiscUv(mesh);
    else { carOpenCenter(mesh); carRubberInner(mesh); }
    const tris = new Map(items.map((it) => [it.s, []]));
    mesh.owner.forEach((s, t) => tris.get(s).push(t));
    for (const [s, list] of tris) {
      const remap = new Map(), pos = [], uv = [], ind = [];
      for (const t of list) for (let k = 0; k < 3; k++) {
        const old = mesh.ind[t * 3 + k];
        let id = remap.get(old);
        if (id === undefined) {
          id = pos.length / 3; remap.set(old, id);
          pos.push(mesh.pos[old * 3], mesh.pos[old * 3 + 1], mesh.pos[old * 3 + 2]);
          uv.push(mesh.uv[old * 2], mesh.uv[old * 2 + 1]);
        }
        ind.push(id);
      }
      const wide = pos.length / 3 > 65535;
      const arr = wide ? Uint32Array.from(ind) : Uint16Array.from(ind);
      const mk = (target, data) => { const b = gl.createBuffer(); gl.bindBuffer(target, b); gl.bufferData(target, data, gl.STATIC_DRAW); return b; };
      const fix = { wide, ind: arr, count: arr.length, lb: null,
        pb: mk(gl.ARRAY_BUFFER, Float32Array.from(pos)), ub: mk(gl.ARRAY_BUFFER, Float32Array.from(uv)), ib: mk(gl.ELEMENT_ARRAY_BUFFER, arr) };
      s._fix = fix;
      cv.fixes.push(fix);
    }
  }
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
    carFixWheels();
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
      <span class="nm" title="${esc(label)}${n._tris ? esc(` · ${n._verts.toLocaleString("pt-BR")} v · ${n._tris.toLocaleString("pt-BR")} △`) : ""}">${esc(label)}</span><span class="cnt">${n._tris ? n._tris.toLocaleString("pt-BR") : ""}</span></div>`);
    if (!open) return;
    for (const s of n.slices) {
      if (q && !carNodeMatches({ id: "", slices: [s], children: [] }, q) && !n.id.toLowerCase().includes(q)) continue;
      const sk = "s:" + s.key;
      const soff = off || cv.hiddenSlices.has(s.key);
      rows.push(`<div class="row tree-row slice${cv.selected === sk ? " on" : ""}${soff ? " off" : ""}${s.ok ? "" : " bad"}" data-id="${esc(sk)}" style="padding-left:${6 + (depth + 1) * 14}px">
        <span class="tw"></span><span class="tw eye" data-act="seye" data-key="${s.key}" title="${esc(t("car.eye"))}">${eyeOn(cv.hiddenSlices.has(s.key))}</span>
        <span class="nm" title="${esc(s.material)} · ${s.vc.toLocaleString("pt-BR")} v · ${s.tris.toLocaleString("pt-BR")} △">${esc(s.material)}</span><span class="cnt">${s.tris.toLocaleString("pt-BR")}</span></div>`);
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
  inspectEl.scrollTop = 0;
  carSyncBar(); carStatus();
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
      const s = d.s;
      const r = s._fix || cv.res.get(s.rds);
      if (!r) return;
      const io = s._fix ? 0 : s.io, ic = s._fix ? s._fix.count : s.ic;
      gl.bindBuffer(gl.ARRAY_BUFFER, r.pb);
      gl.enableVertexAttribArray(loc.pos);
      gl.vertexAttribPointer(loc.pos, 3, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, r.ub);
      gl.enableVertexAttribArray(loc.uv);
      gl.vertexAttribPointer(loc.uv, 2, gl.FLOAT, false, 0, 0);
      if (!d.n._m) d.n._m = new Float32Array(d.n.world);
      gl.uniformMatrix4fv(loc.uModel, false, s._fix ? IDENT : d.n._m);
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
        gl.drawElements(gl.LINES, ic * 2, type, io * 2 * size);
        gl.uniform1i(loc.uLine, 0);
      } else {
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, r.ib);
        gl.drawElements(gl.TRIANGLES, ic, type, io * size);
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

// ------------------------------------------------------------ painéis redimensionáveis

// Divisória arrastável reutilizável: controla uma variável CSS de largura de um painel vizinho.
// o: { root, panel, prop, edge: "left"|"right" (lado do painel), min, max, def, key, other(), reserve }
function makeResizer(handle, o) {
  const read = () => { try { return Number(localStorage.getItem(o.key)) || 0; } catch (e) { return 0; } };
  let last = 0, want = 0;
  const clamp = (w) => {
    const room = o.root.clientWidth - (o.other ? o.other() : 0) - o.reserve;
    return Math.round(Math.max(o.min, Math.min(o.max, room, w)));
  };
  const set = (w, save, keep) => {
    if (!keep) want = w;
    last = clamp(w);
    o.root.style.setProperty(o.prop, last + "px");
    handle.setAttribute("aria-valuenow", String(last));
    if (save) { try { localStorage.setItem(o.key, String(last)); } catch (e) { /* sem armazenamento */ } }
  };
  set(read() || o.def, false);
  handle.setAttribute("aria-valuemin", String(o.min));
  handle.setAttribute("aria-valuemax", String(o.max));
  handle.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    e.preventDefault();
    handle.setPointerCapture(e.pointerId);
    handle.classList.add("drag"); document.body.classList.add("rz-drag");
    const rect = o.root.getBoundingClientRect();
    const move = (ev) => set(o.edge === "left" ? ev.clientX - rect.left : rect.right - ev.clientX, false);
    const up = () => {
      handle.classList.remove("drag"); document.body.classList.remove("rz-drag");
      handle.removeEventListener("pointermove", move);
      handle.removeEventListener("pointerup", up);
      handle.removeEventListener("pointercancel", up);
      set(last, true);
    };
    handle.addEventListener("pointermove", move);
    handle.addEventListener("pointerup", up);
    handle.addEventListener("pointercancel", up);
  });
  handle.addEventListener("dblclick", () => { set(o.def, false); try { localStorage.removeItem(o.key); } catch (e) { /* ok */ } });
  handle.addEventListener("keydown", (e) => {
    const dir = e.key === "ArrowRight" ? 1 : e.key === "ArrowLeft" ? -1 : 0;
    if (!dir) return;
    e.preventDefault();
    set(last + 16 * dir * (o.edge === "left" ? 1 : -1), true);
  });
  return { refit: () => set(want || o.def, false, true) };
}

// ------------------------------------------------------------ barra, palco e inspector

function carSelLabel() {
  const n = carSelNode();
  if (!cv.selected || !n) return "";
  const [kind, key] = cv.selected.split(":");
  if (kind === "s") { const s = cv.slices.get(key); return `${n.id} › ${s.material}`; }
  return n.id;
}
function carStatus() {
  const el = document.getElementById("car-note");
  if (!el) return;
  const sel = document.getElementById("car-sel");
  if (sel) { sel.textContent = carSelLabel(); sel.title = sel.textContent; }
  if (cv.status) { el.textContent = cv.status; return; }
  const tris = cv.draw.reduce((a, d) => a + d.s.tris, 0);
  el.textContent = t("car.status", { slices: cv.draw.length, tris: tris.toLocaleString("pt-BR") }) + " · " + t("mdl.drag");
  el.title = el.textContent;
}

const CAR_ICONS = {
  wire: '<path d="M12 3l8 4.5v9L12 21l-8-4.5v-9z"/><path d="M12 12l8-4.5M12 12v9M12 12L4 7.5"/>',
  mats: '<circle cx="12" cy="12" r="8"/><path d="M12 4a8 8 0 0 1 0 16z" fill="currentColor"/>',
  glass: '<rect x="4" y="5" width="16" height="14" rx="2"/><path d="M8 15l6-6M12 17l5-5"/>',
  flip: '<path d="M8 4v16M8 4L4 8M8 4l4 4M16 20V4M16 20l-4-4M16 20l4-4"/>',
  reset: '<path d="M4 12a8 8 0 1 0 3-6.2"/><path d="M4 4v5h5"/>',
  focus: '<path d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5"/><circle cx="12" cy="12" r="2"/>',
  solo: '<circle cx="12" cy="12" r="3"/><circle cx="12" cy="12" r="8"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3"/>',
  show: '<path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
  insp: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M15 4v16"/>',
  tree: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M9 4v16"/>',
  expand: '<path d="M6 9l6 6 6-6"/>',
  collapse: '<path d="M6 15l6-6 6 6"/>',
};
const carIco = (k) => `<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${CAR_ICONS[k]}</svg>`;

// Ações da toolbar e do overlay do viewport (as mesmas, ligadas por data-car).
const CAR_ACTS = {
  wire: { key: "car.wire", toggle: () => { cv.wire = !cv.wire; }, on: () => cv.wire },
  mats: { key: "car.mats", toggle: () => { cv.flat = !cv.flat; }, on: () => !cv.flat },
  glass: { key: "car.glass", toggle: () => { cv.glass = !cv.glass; }, on: () => cv.glass },
  flip: { key: "mdl.flip", toggle: () => { cv.flip = !cv.flip; }, on: () => cv.flip },
  reset: { key: "car.reset", run: carResetCamera },
  focus: { key: "car.focus", run: () => carFrame(carSelNode()), need: true },
  solo: { key: "car.solo", run: () => { cv.solo = !cv.solo; cv.dirty = true; }, on: () => cv.solo, need: true },
  show: { key: "car.showall", run: () => { cv.hidden.clear(); cv.hiddenSlices.clear(); cv.solo = false; cv.dirty = true; renderList(); } },
};
const carBtn = (name) => `<button type="button" class="tb-btn" data-car="${name}" title="${esc(t(CAR_ACTS[name].key))}" aria-label="${esc(t(CAR_ACTS[name].key))}">${carIco(name)}</button>`;

function carSyncBar() {
  document.querySelectorAll("[data-car]").forEach((b) => {
    const a = CAR_ACTS[b.dataset.car];
    if (a.on) { b.classList.toggle("on", !!a.on()); b.setAttribute("aria-pressed", String(!!a.on())); }
    if (a.need) b.disabled = !cv.selected;
  });
  const lod = document.getElementById("car-lod");
  if (lod && cv.data) {
    lod.innerHTML = [...cv.data.lods.map((l) => l.name), "ALL"].map((n) => `<option value="${esc(n)}"${n === cv.lod ? " selected" : ""}>${esc(n === "ALL" ? t("mdl.all") : n)}</option>`).join("");
  }
  const sel = document.getElementById("car-open");
  if (sel) sel.value = cv.carId || "";
  const ws = cv.ws;
  if (ws) {
    const i = ws.querySelector('[data-pane="insp"]'), tr = ws.querySelector('[data-pane="tree"]');
    if (i) i.classList.toggle("on", !ws.classList.contains("insp-hidden"));
    if (tr) tr.classList.toggle("on", !ws.classList.contains("tree-hidden"));
  }
}

// Move a árvore (.side) e o inspector (#inspect) para dentro do workspace e de volta ao grid global.
function carDock(on) {
  const layout = document.querySelector(".layout");
  const side = document.querySelector(".side"), insp = document.getElementById("inspect");
  const ws = cv.ws;
  if (on && ws && !ws.contains(side)) {
    const body = ws.querySelector(".car-body");
    body.prepend(side); body.append(insp);
    if (!cv.treeTools) {
      cv.treeTools = document.createElement("div");
      cv.treeTools.className = "tree-tools";
      cv.treeTools.innerHTML = `<button type="button" class="tb-btn" data-tree="expand" title="${esc(t("car.tree.expand"))}">${carIco("expand")}</button>
        <button type="button" class="tb-btn" data-tree="collapse" title="${esc(t("car.tree.collapse"))}">${carIco("collapse")}</button><span class="grow"></span>`;
      cv.treeTools.addEventListener("click", carTreeTools);
    }
    side.insertBefore(cv.treeTools, document.getElementById("list"));
    layout.classList.add("docked");
    if (cv.rz) cv.rz.forEach((r) => r.refit());
  } else if (!on) {
    layout.classList.remove("docked");
    if (cv.treeTools) cv.treeTools.remove();
    if (!layout.contains(side) || ws && ws.contains(side)) { layout.insertBefore(side, layout.firstChild); layout.append(insp); }
  }
}
function carTreeTools(e) {
  const b = e.target.closest("[data-tree]");
  if (!b || !cv.data) return;
  if (b.dataset.tree === "expand") cv.nodes.forEach((n) => { if (n.children.length || n.slices.length) cv.expanded.add(n.uid); });
  else { cv.expanded.clear(); cv.expanded.add(cv.data.tree.uid); }
  renderList();
}

function ensureCarStage() {
  if (document.getElementById("car-view")) return;
  cv.gen++;
  cv.gl = null; cv.res.clear(); cv.textures.clear(); cv.loadedId = null;
  const options = CAR_LIST.map((m) => `<option value="${esc(m.id)}">${esc(m.n)} · ${esc(carKindLabel(m))}</option>`).join("");
  const paneBtn = (pane, icon, key) => `<button type="button" class="tb-btn" data-pane="${pane}" title="${esc(t(key))}" aria-label="${esc(t(key))}">${carIco(icon)}</button>`;
  const rz = (id) => `<div class="rz" id="${id}" role="separator" aria-orientation="vertical" tabindex="0" title="${esc(t("car.resize"))}"></div>`;
  contentEl.innerHTML = `<div class="car-ws" id="car-ws">
    <div class="car-tb" role="toolbar">
      ${paneBtn("tree", "tree", "car.tool.tree")}
      <select id="car-open" title="${esc(t("car.open"))}">${options}</select>
      <select id="car-lod" title="${esc(t("car.lod"))}"></select>
      <select id="car-surface" title="${esc(t("mdl.surface"))}">
        <option value="tarmac">${esc(t("mdl.tarmac"))}</option><option value="gravel">${esc(t("mdl.gravel"))}</option>
        <option value="snow">${esc(t("mdl.snow"))}</option><option value="all">${esc(t("mdl.all"))}</option>
      </select>
      <span class="tb-sep"></span>
      ${["wire", "mats", "glass", "flip"].map(carBtn).join("")}
      <span class="tb-sep"></span>
      ${["reset", "focus", "solo", "show"].map(carBtn).join("")}
      <span class="tb-grow"></span>
      ${paneBtn("insp", "insp", "car.tool.insp")}
    </div>
    <div class="car-body">
      ${rz("car-rz1")}
      <div class="car-vp">
        <canvas class="gl" id="car-view"></canvas>
        <div class="vp-tools" role="toolbar" aria-label="${esc(t("car.tool.view"))}">
          ${["reset", "focus", "solo", "show"].map(carBtn).join("")}<span class="tb-sep"></span>${["wire", "mats"].map(carBtn).join("")}
        </div>
      </div>
      ${rz("car-rz2")}
    </div>
    <div class="car-sb"><span id="car-note"></span><span id="car-sel"></span></div>
  </div>`;
  const ws = cv.ws = document.getElementById("car-ws");
  const on = (id, ev, fn) => document.getElementById(id).addEventListener(ev, fn);
  on("car-open", "change", (e) => { openCar(e.target.value); history.replaceState(null, "", "#k=" + encodeURIComponent(e.target.value)); });
  on("car-lod", "change", (e) => {
    cv.lod = e.target.value; state.filter = cv.lod; cv.dirty = true;
    renderFilters(); renderList(); carFrame(null);
  });
  on("car-surface", "change", (e) => { cv.variant = e.target.value; cv.dirty = true; });
  ws.addEventListener("click", (e) => {
    const pane = e.target.closest("[data-pane]");
    if (pane) { ws.classList.toggle(pane.dataset.pane === "tree" ? "tree-hidden" : "insp-hidden"); carSyncBar(); return; }
    const b = e.target.closest("[data-car]");
    if (!b || b.disabled) return;
    const a = CAR_ACTS[b.dataset.car];
    if (a.toggle) a.toggle(); else a.run();
    carSyncBar();
  });

  // larguras e breakpoints: viewport absorve; abaixo de 860px o inspector vira drawer, abaixo de 640px a árvore também
  const body = ws.querySelector(".car-body");
  const side = document.querySelector(".side"), insp = document.getElementById("inspect");
  const inFlow = (el) => (el.isConnected && el.offsetParent && getComputedStyle(el).position !== "absolute" ? el.offsetWidth : 0);
  cv.rz = [
    makeResizer(document.getElementById("car-rz1"), { root: body, panel: side, prop: "--tree-w", edge: "left", min: 200, max: 480, def: 300, key: "car.treeW", reserve: 240 + 12, other: () => inFlow(insp) }),
    makeResizer(document.getElementById("car-rz2"), { root: body, panel: insp, prop: "--insp-w", edge: "right", min: 300, max: 550, def: 400, key: "car.inspW", reserve: 240 + 12, other: () => inFlow(side) }),
  ];
  const mqI = window.matchMedia("(max-width: 860px)"), mqT = window.matchMedia("(max-width: 640px)");
  const adapt = () => {
    ws.classList.toggle("idrawer", mqI.matches);
    ws.classList.toggle("tdrawer", mqT.matches);
    ws.classList.toggle("insp-hidden", mqI.matches);
    ws.classList.toggle("tree-hidden", mqT.matches);
    cv.rz.forEach((r) => r.refit()); carSyncBar();
  };
  mqI.addEventListener("change", adapt); mqT.addEventListener("change", adapt);
  window.addEventListener("resize", () => cv.rz.forEach((r) => r.refit()));
  adapt();
  try { carGL(); } catch (err) { document.getElementById("car-note").textContent = String((err && err.message) || err); }
}

// ------------------------------------------------------------ inspector

const carNum = (v, d = 4) => (typeof v === "number" ? String(Math.round(v * 10 ** d) / 10 ** d) : String(v));
const carInt = (v) => (typeof v === "number" ? v.toLocaleString("pt-BR") : String(v));
// Linha rótulo/valor. mode: "mono", "mono ell" (uma linha, ellipsis + tooltip), "mono wrap" (até 3 linhas), "html".
function carKv(k, v, mode = "") {
  const text = v == null || v === "" ? "—" : String(v);
  if (mode === "html") return `<div class="k" title="${esc(k)}">${esc(k)}</div><div class="v">${text}</div>`;
  return `<div class="k" title="${esc(k)}">${esc(k)}</div><div class="v${mode ? " " + mode : ""}" title="${esc(text)}">${esc(text)}</div>`;
}
const carKvs = (rows) => `<div class="kv">${rows.join("")}</div>`;
function carMatrix(m) {
  const rows = [];
  for (let r = 0; r < 4; r++) rows.push(m.slice(r * 4, r * 4 + 4).map((v) => carNum(v, 4).padStart(9)).join(" "));
  return `<pre class="xml">${esc(rows.join("\n"))}</pre>`;
}
function carExtra(extra) {
  const keys = Object.keys(extra || {});
  return keys.length ? carKvs(keys.map((k) => carKv(k, JSON.stringify(extra[k]), "mono wrap"))) : `<p class="muted small">—</p>`;
}
function carNodeOff(n) {
  for (let p = n; p; p = p._parent) if (cv.hidden.has(p.uid)) return true;
  return false;
}

// Preview de textura: altura fixa, fit/zoom, fundo xadrez para alpha. Os botões usam delegação (carTexClick).
function carTexHtml(asset) {
  const btn = (act, label, key) => `<button type="button" data-tex="${act}" title="${esc(t(key))}" aria-label="${esc(t(key))}">${label}</button>`;
  return `<div class="tex fit" data-bg="0" data-zoom="1">
    <div class="tex-bar">${btn("fit", "⤢", "car.tex.fit")}${btn("actual", "1:1", "car.tex.actual")}${btn("out", "−", "car.tex.out")}${btn("in", "+", "car.tex.in")}${btn("bg", "▦", "car.tex.bg")}<span class="tex-info mono"></span></div>
    <div class="tex-box bg-checker"><img src="${esc(asset.p)}" alt="${esc(asset.n)}" loading="lazy"></div></div>`;
}
const CAR_TEX_BG = ["bg-checker", "bg-dark", "bg-light"];
function carTexUpdate(box) {
  const img = box.querySelector("img"), info = box.querySelector(".tex-info");
  const fit = box.classList.contains("fit");
  const zoom = Number(box.dataset.zoom) || 1;
  if (fit) { img.style.width = img.style.height = ""; }
  else if (img.naturalWidth) { img.style.width = img.naturalWidth * zoom + "px"; img.style.height = img.naturalHeight * zoom + "px"; }
  const dim = img.naturalWidth ? `${img.naturalWidth}×${img.naturalHeight}` : "";
  info.textContent = fit ? dim : `${dim} · ${Math.round(zoom * 100)}%`;
  box.querySelector('[data-tex="fit"]').classList.toggle("on", fit);
  const pane = box.querySelector(".tex-box");
  CAR_TEX_BG.forEach((c, i) => pane.classList.toggle(c, i === Number(box.dataset.bg)));
}
function carTexClick(e) {
  const b = e.target.closest("[data-tex]");
  if (!b) return;
  const box = b.closest(".tex");
  const act = b.dataset.tex;
  let zoom = Number(box.dataset.zoom) || 1;
  if (act === "fit") box.classList.add("fit");
  else if (act === "actual") { box.classList.remove("fit"); zoom = 1; }
  else if (act === "in" || act === "out") {
    const img = box.querySelector("img");
    if (box.classList.contains("fit") && img.naturalWidth) zoom = img.clientWidth / img.naturalWidth;
    box.classList.remove("fit");
    zoom = Math.max(0.1, Math.min(8, zoom * (act === "in" ? 1.5 : 1 / 1.5)));
  } else if (act === "bg") box.dataset.bg = String((Number(box.dataset.bg) + 1) % CAR_TEX_BG.length);
  box.dataset.zoom = String(zoom);
  carTexUpdate(box);
}

function carMaterialHtml(name) {
  const m = carMaterial(name);
  const params = Object.entries(m.params || {}).map(([k, v]) => carKv(k, JSON.stringify(v), "mono ell"));
  const parts = [carKvs([
    carKv(t("car.shader"), m.group, "mono ell"),
    carKv(t("car.params"), m.paramCount == null ? "—" : `${m.savedCount}/${m.paramCount}`),
    carKv(t("car.blend"), carIsBlend(name) ? t("car.yes") : t("car.no")),
  ])];
  if (params.length) parts.push(`<h4>${esc(t("car.shaderparams"))}</h4>${carKvs(params)}`);
  const extra = Object.keys(m.extra || {}).length ? carExtra(m.extra) : "";
  return parts.join("") + extra;
}
function carTexturesHtml(names) {
  const seen = new Set(), out = [];
  for (const name of names) {
    const tid = carMatTexId(name);
    const asset = tid && assetById.get(tid);
    const declared = Object.entries(carMaterial(name).textures || {});
    out.push(`<h4 title="${esc(name)}">${esc(name)}</h4>`);
    if (asset && !seen.has(tid)) {
      seen.add(tid);
      out.push(carKvs([carKv(t("mdl.textures"), asset.n, "mono ell"), carKv("", t("car.guessed"), "")]), carTexHtml(asset));
    } else if (asset) out.push(carKvs([carKv(t("mdl.textures"), asset.n, "mono ell")]));
    else out.push(`<p class="muted small">${esc(t("car.tex.none"))}</p>`);
    if (declared.length) out.push(carKvs(declared.map(([k, v]) => carKv(k, v, "mono ell"))));
    else out.push(`<p class="muted small">${esc(t("car.nodeclared"))}</p>`);
  }
  return out.join("");
}
function carLodHtml(node) {
  const lods = cv.data.lods;
  const cur = cv.lod === "ALL" ? { nodes: lods.reduce((a, l) => a + l.nodes, 0), slices: lods.reduce((a, l) => a + l.slices, 0), tris: lods.reduce((a, l) => a + l.tris, 0) } : lods.find((l) => l.name === cv.lod);
  const rows = [carKv(t("car.current"), cv.lod === "ALL" ? t("mdl.all") : cv.lod)];
  if (node && node._lod) rows.push(carKv(t("car.lod"), node._lod));
  if (cur) rows.push(carKv(t("car.meshes"), carInt(cur.nodes)), carKv(t("car.slices"), carInt(cur.slices)), carKv(t("mdl.tris"), carInt(cur.tris)));
  return carKvs(rows) + `<h4>${esc(t("car.lods"))}</h4>` + carKvs(lods.map((l) => carKv(l.name, `${carInt(l.nodes)} / ${carInt(l.slices)} / ${carInt(l.tris)}`, "mono")));
}
function carSourceRows() {
  const src = cv.data.source || {};
  return [carKv(t("mdl.package"), src.package, "mono ell"), carKv(t("mdl.path"), src.path, "mono wrap"),
    carKv(t("car.buffers"), Object.keys(cv.data.resources).length), carKv(t("car.materials"), Object.keys(cv.data.materials).length)];
}
function carBufferText(rds) {
  const r = cv.data.resources[rds];
  return r ? `${rds} (${carInt(r.verts)} v, ${carInt(r.tris)} t)` : rds;
}

function carInspect() {
  const el = inspectEl;
  rememberSections(el);
  const d = cv.data;
  if (!d) { el.innerHTML = `<p class="muted" style="padding-top:12px">${esc(cv.status || t("car.pick"))}</p>`; return; }
  const node = carSelNode();
  const sel = carSelLabel();
  const head = `<div class="insp-head"><h2 title="${esc(cv.row ? cv.row.n : d.id)}">${esc(cv.row ? cv.row.n : d.id)}</h2><div class="sub" title="${esc(sel)}">${esc(sel || t("car.hint"))}</div></div>`;
  const notes = d.notes.length ? section("car.notes", `<div class="mono small">${d.notes.map(esc).join("<br>")}</div>`, false) : "";
  let body;
  if (!node) {
    const mats = Object.keys(d.materials).map((n) => carKv(n, d.materials[n].group, "mono ell"));
    body = section("car.sec.car", carKvs(carSourceRows()), true) + section("car.sec.lod", carLodHtml(null), true)
      + section("car.sec.mats", carKvs(mats), false);
  } else {
    const [kind, key] = cv.selected.split(":");
    const off = carNodeOff(node);
    if (kind === "s") {
      const s = cv.slices.get(key);
      const hidden = off || cv.hiddenSlices.has(s.key);
      body = section("car.sec.object", carKvs([
        carKv(t("car.slice"), s.id, "mono ell"), carKv(t("car.owner"), node.id, "mono ell"),
        carKv(t("car.state"), (s.ok ? "OK" : `<span class="warn">${esc(t("car.badslice"))}</span>`) + ` · ${esc(hidden ? t("car.hidden") : t("car.visible"))}`, "html"),
      ]), true)
      + section("car.sec.geometry", carKvs([
        carKv(t("car.buffer"), carBufferText(s.rds), "mono ell"), carKv(t("car.vrange"), `${s.vo} + ${s.vc}`, "mono"),
        carKv(t("car.irange"), `${s.io} + ${s.ic}`, "mono"), carKv(t("mdl.tris"), carInt(s.tris)),
      ]), true)
      + section("car.sec.material", `<h4 title="${esc(s.material)}">${esc(s.material)}</h4>` + carMaterialHtml(s.material), true)
      + section("car.sec.textures", carTexturesHtml([s.material]), true)
      + section("car.sec.lod", carLodHtml(node), false)
      + section("car.sec.technical", carKvs([carKv("jointID", s.joint, "mono"), carKv("uid", node.uid, "mono"), ...carSourceRows()]) + carExtra(s.extra), false);
    } else {
      const matNames = [...new Set(node.slices.map((s) => s.material))];
      const buffers = [...new Set(node.slices.map((s) => s.rds))];
      const bb = node.bbox;
      body = section("car.sec.object", carKvs([
        carKv(t("car.mesh"), node.id, "mono ell"), carKv(t("car.pssgtype"), node.type, "mono ell"),
        carKv(t("car.nickname"), node.nickname, "mono ell"), carKv(t("car.parent"), node._parent ? node._parent.id : "", "mono ell"),
        carKv(t("car.children"), node.children.length), carKv(t("car.state"), off ? t("car.hidden") : t("car.visible")),
      ]), true)
      + section("car.sec.geometry", carKvs([
        carKv(t("car.slices"), carInt(node.slices.length)), carKv(t("mdl.verts"), carInt(node._verts)), carKv(t("mdl.tris"), carInt(node._tris)),
        carKv(t("car.bbox"), bb ? bb.map((v) => carNum(v, 3)).join(", ") : "", "mono wrap"),
        ...buffers.map((b) => carKv(t("car.buffer"), carBufferText(b), "mono ell")),
      ]) + (node.slices.length ? `<h4>${esc(t("car.slices"))}</h4>` + node.slices.map((s) => `<div class="beh"><div class="desc mono">${esc(s.id)} · ${esc(s.material)}</div><div class="raw">${esc(s.rds)} v ${s.vo}+${s.vc} i ${s.io}+${s.ic} · ${s.tris} ${esc(t("car.tris.short"))}</div></div>`).join("") : ""), true)
      + (matNames.length ? section("car.sec.material", matNames.map((n) => `<details class="mat"><summary class="mono" title="${esc(n)}">${esc(n)}</summary>${carMaterialHtml(n)}</details>`).join(""), true) : "")
      + (matNames.length ? section("car.sec.textures", carTexturesHtml(matNames), true) : "")
      + section("car.sec.lod", carLodHtml(node), false)
      + section("car.sec.technical", carKvs([carKv("uid", node.uid, "mono"), ...carSourceRows()])
        + `<h4>${esc(t("car.local"))}</h4>${carMatrix(node.local)}${node.identity ? `<p class="muted small">${esc(t("car.identity"))}</p>` : ""}`
        + `<h4>${esc(t("car.world"))}</h4>${carMatrix(node.world)}<h4>${esc(t("car.extra"))}</h4>${carExtra(node.extra)}`, false);
    }
  }
  el.innerHTML = head + body + notes;
  restoreSections(el);
  el.querySelectorAll(".tex").forEach((box) => {
    const img = box.querySelector("img");
    if (img.complete) carTexUpdate(box); else img.addEventListener("load", () => carTexUpdate(box), { once: true });
    carTexUpdate(box);
  });
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
    carDock(true);
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

// Fora da aba Carros, árvore e inspector voltam ao grid global antes de qualquer outra aba escrever em #content.
const _refreshCars = refresh;
refresh = function () {
  if (state.mode !== "cars") carDock(false);
  _refreshCars();
};
document.getElementById("inspect").addEventListener("click", carTexClick);

// content.js e este arquivo registram as abas; só agora a página inicia.
start();
