// Abas de conteúdo: galeria de imagens, navegador de textos e diálogos.
"use strict";

const contentEl = document.getElementById("content");
const inspectEl = document.getElementById("inspect");
const assetId = (a) => a.g + "/" + a.n;
const assetById = new Map(ASSETS.map((a) => [assetId(a), a]));
const groupLabel = (g) => (t("grp." + g) === "grp." + g ? g : t("grp." + g));

// Onde cada textura aparece: materiais das cenas e BTextureStatic das telas.
let usageIndex = null;
function textureUsage() {
  if (usageIndex) return usageIndex;
  usageIndex = new Map();
  const add = (tex, kind, id) => {
    if (!tex) return;
    if (!usageIndex.has(tex)) usageIndex.set(tex, new Map());
    usageIndex.get(tex).set(kind + ":" + id, { kind, id });
  };
  const matTex = new Map();
  for (const [key, m] of Object.entries(SD.materials)) {
    const list = [m.TDiffuseMap, m.TPatternMap].filter(Boolean);
    if (list.length) matTex.set(key, list);
  }
  for (const [name, root] of Object.entries(SD.scenes)) {
    const walk = (n) => {
      for (const [, mat] of n.draw || []) for (const tex of matTex.get(mat) || []) add(tex, "c", name);
      (n.c || []).forEach(walk);
    };
    walk(root);
  }
  for (const s of UD.screens) for (const it of s.items) for (const b of it.b) if (b.t === "BTextureStatic") add(b.a.texture, "s", s.id);
  return usageIndex;
}

// ------------------------------------------------------------ imagens

const gallery = { shown: 240, selected: null, bg: "checker" };
const imageGroups = () => {
  const counts = new Map();
  for (const a of ASSETS) counts.set(a.g, (counts.get(a.g) || 0) + 1);
  return [...counts.entries()].sort((a, b) => b[1] - a[1]);
};
function filteredAssets() {
  const q = state.search.trim().toLowerCase();
  return ASSETS.filter((a) => (state.filter === "all" || a.g === state.filter) && (!q || a.n.toLowerCase().includes(q)));
}
MODES.images = {
  stage: false, noList: true, hash: "i",
  filters: () => [["all", `${t("img.all")} (${ASSETS.length})`], ...imageGroups().map(([g, n]) => [g, `${groupLabel(g)} (${n})`])],
  list: () => [],
  current: () => gallery.selected,
  select(id) { gallery.selected = id; },
  onFilter() { gallery.shown = 240; this.show(); },
  onSearch() { gallery.shown = 240; this.show(); },
  show() {
    const list = filteredAssets();
    const tiles = list.slice(0, gallery.shown).map((a) => {
      const id = assetId(a);
      return `<button class="tile${id === gallery.selected ? " on" : ""}" data-id="${esc(id)}" title="${esc(a.n)}">
        <span class="pic bg-${gallery.bg}"><img src="${esc(a.t)}" loading="lazy" alt=""></span>
        <span class="name">${esc(a.n)}</span><span class="dim">${a.w}×${a.h}</span></button>`;
    });
    contentEl.innerHTML = `<div class="content-head"><b>${esc(t("img.count", { n: list.length }))}</b>
        <span class="muted">${esc(t("img.bg"))}</span>
        ${["checker", "dark", "light"].map((b) => `<button class="chip${gallery.bg === b ? " on" : ""}" data-bg="${b}">${b === "checker" ? "▦" : b === "dark" ? "■" : "□"}</button>`).join("")}
      </div>
      <div class="grid">${tiles.join("")}</div>
      ${list.length > gallery.shown ? `<div class="more"><button id="more">${esc(t("img.more"))} (${list.length - gallery.shown})</button></div>` : ""}`;
    contentEl.querySelectorAll(".tile").forEach((b) => b.addEventListener("click", () => select(b.dataset.id)));
    contentEl.querySelectorAll("[data-bg]").forEach((b) => b.addEventListener("click", () => { gallery.bg = b.dataset.bg; this.show(); }));
    const more = document.getElementById("more");
    if (more) more.addEventListener("click", () => { gallery.shown += 480; this.show(); });
    this.detail();
  },
  detail() {
    // Aceita "grupo/nome" ou só o nome (links vindos das telas).
    let a = assetById.get(gallery.selected) || ASSETS.find((x) => x.n === gallery.selected);
    if (!a) { inspectEl.innerHTML = `<p class="muted">${esc(t("img.pick"))}</p>`; return; }
    const uses = [...(textureUsage().get(a.n) || new Map()).values()];
    inspectEl.innerHTML = `<h2>${esc(a.n)}</h2>
      <a class="preview bg-${gallery.bg}" href="${esc(a.p)}" target="_blank" rel="noopener"><img src="${esc(a.p)}" alt=""></a>
      <div class="kv">
        <div>${t("img.size")}</div><div>${a.w} × ${a.h}</div>
        <div>${t("img.format")}</div><div class="mono">${esc(a.f)}</div>
        <div>${t("img.group")}</div><div>${esc(groupLabel(a.g))} <span class="muted mono">${esc(a.g)}</span></div>
      </div>
      <p><a href="${esc(a.p)}" target="_blank" rel="noopener">${esc(t("img.open"))} ↗</a></p>
      ${section("img.usedin", uses.length ? uses.slice(0, 60).map((u) => `<div><a class="mono" href="#${u.kind}=${encodeURIComponent(u.id)}">${esc(u.id)}</a></div>`).join("") : `<p class="muted">${esc(t("img.unused"))}</p>`)}`;
  },
};

// ------------------------------------------------------------ textos

const texts = { selected: null };
let keyUsage = null;
function textUsage() {
  if (keyUsage) return keyUsage;
  keyUsage = new Map();
  const add = (key, ref) => {
    if (!/^(lng_|db_)/.test(key)) return;
    for (const k of key.split(";")) {
      if (!keyUsage.has(k)) keyUsage.set(k, new Set());
      keyUsage.get(k).add(ref);
    }
  };
  for (const s of UD.screens) {
    for (const it of s.items) for (const b of it.b) for (const v of Object.values(b.a || {})) add(v, "s=" + s.id);
    for (const b of s.beh) for (const v of Object.values(b.a || {})) add(v, "s=" + s.id);
  }
  for (const d of UD.dialogs || []) for (const p of d.parts) for (const v of Object.values(p.a)) add(v, "d=" + d.a.id);
  return keyUsage;
}
function textRows() {
  const q = state.search.trim().toLowerCase();
  const usage = textUsage();
  const pt = STR.bra, en = STR.eng;
  const keys = state.filter === "used" ? [...usage.keys()] : Object.keys(en);
  const out = [];
  for (const k of keys) {
    if (state.filter === "missing" && pt[k] != null) continue;
    if (q.length >= 2) {
      const hay = (k + "\n" + (pt[k] || "") + "\n" + (en[k] || "")).toLowerCase();
      if (!hay.includes(q)) continue;
    } else if (state.filter === "all") continue;
    out.push(k);
    if (out.length >= 300) break;
  }
  return out;
}
MODES.texts = {
  stage: false, noList: true, hash: "t", defaultFilter: "used",
  filters: () => [["all", t("txt.f.all")], ["used", t("txt.f.used")], ["missing", t("txt.f.missing")]],
  list: () => [],
  current: () => texts.selected,
  select(id) { texts.selected = id; },
  onFilter() { this.show(); },
  onSearch() { this.show(); },
  show() {
    const rows = textRows();
    const usage = textUsage();
    const cell = (s) => s == null ? `<span class="bad" title="${esc(t("missing"))}">—</span>` : esc(cleanMarkup(s).slice(0, 220));
    const hint = state.filter === "all" && state.search.trim().length < 2 ? t("txt.hint") : "";
    contentEl.innerHTML = `<div class="content-head"><b>${esc(t("txt.count", { n: rows.length }))}</b> ${hint ? `<span class="muted">${esc(hint)}</span>` : ""}</div>
      <table class="texts"><thead><tr><th>${t("txt.key")}</th><th>Português</th><th>English</th><th>${t("txt.used")}</th></tr></thead>
      <tbody>${rows.map((k) => `<tr data-id="${esc(k)}" class="${k === texts.selected ? "on" : ""}"><td class="mono">${esc(k)}</td><td>${cell(STR.bra[k])}</td><td>${cell(STR.eng[k])}</td><td class="small">${(usage.get(k) ? usage.get(k).size : 0) || ""}</td></tr>`).join("")}</tbody></table>`;
    contentEl.querySelectorAll("tr[data-id]").forEach((r) => r.addEventListener("click", () => select(r.dataset.id)));
    this.detail();
  },
  detail() {
    const k = texts.selected;
    if (!k) { inspectEl.innerHTML = `<p class="muted">${esc(t("txt.hint"))}</p>`; return; }
    const refs = [...(textUsage().get(k) || [])];
    const block = (lang, s) => `<div class="lang"><div class="muted small">${lang}</div><div class="tx">${s == null ? `<span class="bad">—</span>` : esc(cleanMarkup(s))}</div>
      ${s && /\{[a-z]/.test(s) ? `<details><summary class="small">markup</summary><pre class="xml">${esc(s)}</pre></details>` : ""}</div>`;
    inspectEl.innerHTML = `<h2 class="mono">${esc(k)}</h2>${block("Português", STR.bra[k])}${block("English", STR.eng[k])}
      ${section("txt.used", refs.length ? refs.map((r) => `<div><a class="mono" href="#${esc(r)}">${esc(r.slice(2))}</a></div>`).join("") : "<p class=\"muted\">—</p>")}`;
  },
};

// ------------------------------------------------------------ diálogos

const dialogs = { selected: null };
const DIALOGS = UD.dialogs || [];
const dialogById = new Map(DIALOGS.map((d) => [d.a.id, d]));
const dialogPart = (d, tag) => d.parts.filter((p) => p.t === tag);
const dialogText = (p) => {
  if (!p) return "";
  const v = p.a.value;
  const shown = showStr(v);
  if (shown != null) return cleanMarkup(shown);
  if (v && /^(lng_|db_)/.test(v)) return `[${t("missing")}: ${v}]`;
  return v || "";
};
MODES.dialogs = {
  stage: false, hash: "d",
  filters() {
    const counts = new Map();
    for (const d of DIALOGS) counts.set(d.src, (counts.get(d.src) || 0) + 1);
    return [["all", `${t("filter.all")} (${DIALOGS.length})`], ...[...counts.entries()].map(([s, n]) => [s, `${s} (${n})`])];
  },
  list(q) {
    return DIALOGS.filter((d) => state.filter === "all" || d.src === state.filter).filter((d) => {
      if (!q) return true;
      const title = dialogText(dialogPart(d, "title")[0]), body = dialogText(dialogPart(d, "body")[0]);
      return (d.a.id + " " + title + " " + body).toLowerCase().includes(q);
    }).map((d) => `<div class="row${d.a.id === dialogs.selected ? " on" : ""}" data-id="${esc(d.a.id)}">
      <div class="id">${esc(d.a.id)}</div><div class="sub">${esc(dialogText(dialogPart(d, "title")[0]) || "—")}</div></div>`);
  },
  current: () => dialogs.selected,
  select(id) { dialogs.selected = id; },
  show() {
    const d = dialogById.get(dialogs.selected);
    if (!d) { contentEl.innerHTML = `<p class="muted pad">${esc(t("dlg.pick"))}</p>`; inspectEl.innerHTML = ""; return; }
    const title = dialogText(dialogPart(d, "title")[0]);
    const body = dialogText(dialogPart(d, "body")[0]);
    const opts = dialogPart(d, "option");
    contentEl.innerHTML = `<div class="dialog-stage"><div class="dialog">
      <div class="dlg-title">${esc(title || d.a.id)}</div>
      <div class="dlg-body">${esc(body).replace(/\n/g, "<br>")}</div>
      <div class="dlg-opts">${opts.map((o) => `<span class="dlg-opt${o.a.id === d.a.default ? " def" : ""}">${esc(dialogText(o))}<small>${esc(o.a.id || "")}</small></span>`).join("")}</div>
    </div></div>`;
    const states = Object.entries(UD.states).filter(([, st]) => Object.values(st.a).includes(d.a.id));
    inspectEl.innerHTML = `<h2 class="mono">${esc(d.a.id)}</h2>
      <div class="kv">
        <div>${t("dlg.file")}</div><div class="mono">message_dialogs/${esc(d.src)}.xml</div>
        <div>${t("dlg.type")}</div><div class="mono">${esc(d.a.type || "—")}</div>
        <div>${t("dlg.priority")}</div><div>${esc(d.a.priority || "—")}</div>
        <div>${t("dlg.default")}</div><div class="mono">${esc(d.a.default || "—")}</div>
        <div>${t("dlg.back")}</div><div class="mono">${esc(d.a.back || "—")}</div>
      </div>
      ${section("dlg.options", opts.map((o) => `<div class="beh"><div class="desc">${esc(dialogText(o))}</div><div class="raw">id=<span class="s">"${esc(o.a.id || "")}"</span> ${esc(o.a.value || "")}</div></div>`).join("") || "—")}
      ${section("dlg.states", states.length ? states.map(([sid]) => `<div class="link">${stateLabel(sid)}</div>`).join("") : `<p class="muted">${esc(t("dlg.nostate"))}</p>`)}
      ${section("sec.source", `<pre class="xml">${esc(JSON.stringify(d, null, 1))}</pre>`, false)}`;
  },
};

// ------------------------------------------------------------ modelos 3D

const MODEL_PACK = window.MODEL_DATA || { models: [], info: {} };
const MODEL_LIST = MODEL_PACK.models || [];
const modelById = new Map(MODEL_LIST.map((m) => [m.id, m]));
const modelView = {
  selected: null, loaded: null, variant: "tarmac", flip: false, forceTex: null,
  yaw: 0.7, pitch: 0.35, dist: 4, lookY: 0.4, pan: [0, 0, 0],
  gl: null, program: null, meshes: [], grid: null,
  drag: null, textures: new Map(),
};

function modelKind(k) { const label = t("kind." + k); return label === "kind." + k ? k : label; }
function meshVariant(name) {
  const n = name.toLowerCase();
  if (n.includes("snow")) return "snow";
  if (n.includes("gravel")) return "gravel";
  if (n.includes("tarmac")) return "tarmac";
  return "base";
}
function meshVisible(name) {
  // car_disc_blur é o cartão do disco girando. No repouso o material é preto
  // (EnvironmentColour 0,0,0,1) e fica por fora do rodão; opaco, cobre a roda.
  if (/disc_blur/i.test(name)) return false;
  const v = meshVariant(name);
  return v === "base" || modelView.variant === "all" || v === modelView.variant;
}
function fract1(v) { return v - Math.floor(v); }
function wheelGroups(pos) {
  const n = pos.length / 3;
  if (!n) return [];
  let minX = Infinity, maxX = -Infinity, minZ = Infinity, maxZ = -Infinity, sx = 0, sz = 0;
  for (let i = 0; i < n; i++) {
    const x = pos[i * 3], z = pos[i * 3 + 2];
    sx += x; sz += z;
    if (x < minX) minX = x; if (x > maxX) maxX = x;
    if (z < minZ) minZ = z; if (z > maxZ) maxZ = z;
  }
  const mx = sx / n, mz = sz / n;
  const splitX = maxX - minX > 0.8, splitZ = maxZ - minZ > 0.8;
  const buckets = new Map();
  for (let i = 0; i < n; i++) {
    const gx = splitX ? (pos[i * 3] < mx ? 0 : 1) : 0;
    const gz = splitZ ? (pos[i * 3 + 2] < mz ? 0 : 1) : 0;
    const key = gx + gz * 2;
    const list = buckets.get(key);
    if (list) list.push(i); else buckets.set(key, [i]);
  }
  const groups = [];
  for (const ids of buckets.values()) {
    if (ids.length < 8) continue;
    let x = 0, y = 0, z = 0;
    for (const i of ids) { x += pos[i * 3]; y += pos[i * 3 + 1]; z += pos[i * 3 + 2]; }
    x /= ids.length; y /= ids.length; z /= ids.length;
    let rmax = 0;
    for (const i of ids) {
      const r = Math.hypot(pos[i * 3 + 1] - y, pos[i * 3 + 2] - z);
      if (r > rmax) rmax = r;
    }
    groups.push({ x, y, z, rmax, ids });
  }
  return groups;
}
function assignWheels(pos) {
  const groups = wheelGroups(pos);
  const which = new Int16Array(pos.length / 3);
  which.fill(-1);
  groups.forEach((g, gi) => { for (const i of g.ids) which[i] = gi; });
  return { groups, which };
}
// O miolo da roda está no cartão de parafusos do atlas (canto inferior esquerdo).
// Esse cartão estica no centro e esconde o disco. O anel de aço fica.
function openWheelCenter(mesh) {
  const { groups, which } = assignWheels(mesh.pos);
  if (!groups.length) return;
  const ind = mesh.ind;
  const keep = [];
  const rad = (i, g) => Math.hypot(mesh.pos[i * 3 + 1] - g.y, mesh.pos[i * 3 + 2] - g.z) / g.rmax;
  for (let t = 0; t < ind.length; t += 3) {
    const a = ind[t], b = ind[t + 1], c = ind[t + 2];
    const u = (fract1(mesh.uv[a * 2]) + fract1(mesh.uv[b * 2]) + fract1(mesh.uv[c * 2])) / 3;
    const v = (fract1(mesh.uv[a * 2 + 1]) + fract1(mesh.uv[b * 2 + 1]) + fract1(mesh.uv[c * 2 + 1])) / 3;
    const gi = which[a];
    const g = gi >= 0 && which[b] === gi && which[c] === gi ? groups[gi] : null;
    const inner = g && g.rmax > 1e-4 && (rad(a, g) + rad(b, g) + rad(c, g)) / 3 < 0.36;
    if (inner && u < 0.32 && v > 0.75) continue;
    keep.push(a, b, c);
  }
  mesh.ind = Uint32Array.from(keep);
}
// A lateral de dentro usa o atlas inteiro (letra, código de barras, centro branco).
// Para dentro do carro fica a borracha escura; a face de fora conserva o UV gravado.
function rubberizeInnerWheel(mesh) {
  const { groups } = assignWheels(mesh.pos);
  if (!groups.length) return;
  const n = mesh.pos.length / 3;
  const which = new Int16Array(n);
  which.fill(-1);
  const depth = new Float32Array(n);
  groups.forEach((g, gi) => {
    const side = Math.sign(g.x) || 1;
    let face = g.x;
    for (const i of g.ids) {
      const x = mesh.pos[i * 3];
      face = side < 0 ? Math.min(face, x) : Math.max(face, x);
    }
    for (const i of g.ids) {
      which[i] = gi;
      depth[i] = (face - mesh.pos[i * 3]) * side;
    }
  });
  const ind = mesh.ind;
  const outer = new Uint8Array(n);
  for (let t = 0; t < ind.length; t += 3) {
    const a = ind[t], b = ind[t + 1], c = ind[t + 2];
    if ((depth[a] + depth[b] + depth[c]) / 3 < 0.08) outer[a] = outer[b] = outer[c] = 1;
  }
  const rubber = (g, i) => {
    const dy = mesh.pos[i * 3 + 1] - g.y;
    const dz = mesh.pos[i * 3 + 2] - g.z;
    const ang = Math.atan2(dz, dy);
    const r = Math.min(1, Math.hypot(dy, dz) / (g.rmax || 1));
    return [0.16 + ((ang / (Math.PI * 2) + 1) % 1) * 0.18, 0.855 + r * 0.04];
  };
  const pos = Array.from(mesh.pos);
  const uv = Array.from(mesh.uv);
  const keep = [];
  for (let t = 0; t < ind.length; t += 3) {
    const tri = [ind[t], ind[t + 1], ind[t + 2]];
    const d = (depth[tri[0]] + depth[tri[1]] + depth[tri[2]]) / 3;
    if (d > 0.1 && tri.every((i) => which[i] >= 0)) {
      for (let k = 0; k < 3; k++) {
        const i = tri[k];
        const rub = rubber(groups[which[i]], i);
        if (outer[i]) {
          tri[k] = pos.length / 3;
          pos.push(mesh.pos[i * 3], mesh.pos[i * 3 + 1], mesh.pos[i * 3 + 2]);
          uv.push(rub[0], rub[1]);
        } else {
          uv[i * 2] = rub[0];
          uv[i * 2 + 1] = rub[1];
        }
      }
    }
    keep.push(tri[0], tri[1], tri[2]);
  }
  mesh.pos = Float32Array.from(pos);
  mesh.uv = Float32Array.from(uv);
  mesh.ind = Uint32Array.from(keep);
}
// O UV gravado espalha o atlas inteiro na borda do disco. O rotor é o círculo
// no centro (037 e es2: raio ~0,40). A malha fica onde foi modelada.
function mapDiscUv(mesh) {
  const { groups, which } = assignWheels(mesh.pos);
  if (!groups.length) return;
  const uv = new Float32Array(mesh.uv);
  for (let i = 0; i < which.length; i++) {
    const g = groups[which[i]];
    if (!g || g.rmax < 1e-4) continue;
    const dy = mesh.pos[i * 3 + 1] - g.y;
    const dz = mesh.pos[i * 3 + 2] - g.z;
    const r = Math.min(1, Math.hypot(dy, dz) / g.rmax);
    const ang = Math.atan2(dz, dy);
    uv[i * 2] = 0.5 + Math.cos(ang) * r * 0.4;
    uv[i * 2 + 1] = 0.5 + Math.sin(ang) * r * 0.4;
  }
  mesh.uv = uv;
}
function colorOf(name) {
  let h = 0;
  for (let i = 0; i < name.length; i++) h = (h * 33 + name.charCodeAt(i)) >>> 0;
  const hue = h % 360;
  const s = 0.45, l = 0.55;
  const a = s * Math.min(l, 1 - l);
  const f = (n) => { const k = (n + hue / 30) % 12; return l - a * Math.max(Math.min(k - 3, 9 - k, 1), -1); };
  return [f(0), f(8), f(4)];
}
function surfaceToken(material) {
  const n = material.toLowerCase();
  if (n.includes("snow")) return "sn";
  if (n.includes("gravel")) return "gr";
  if (n.includes("wet")) return "wt";
  return "tm";
}
function isTread(material) { return /tread|tyre|tire/i.test(material); }
function wheelDiffuse(material, ids) {
  const pool = ids.map((id) => assetById.get(id)).filter(Boolean);
  const wheels = pool.filter((a) => /wheel_d/i.test(a.n));
  const token = surfaceToken(material);
  return wheels.find((a) => new RegExp("_" + token + "(\\.|_)", "i").test(a.n))
    || wheels.find((a) => !/_(gr|sn|wt|tm)(\.|_)/i.test(a.n))
    || wheels[0] || null;
}
function guessTexture(material, ids) {
  const n = material.toLowerCase();
  // A banda de rodagem não tem textura própria: o jogo amostra a faixa no atlas da roda.
  if (isTread(n) || n.includes("wheel")) {
    const wheel = wheelDiffuse(n, ids);
    if (wheel) return wheel;
  }
  const pool = ids.map((id) => assetById.get(id)).filter(Boolean);
  const diffs = pool.filter((a) => /(_d)(\.|_|$)/i.test(a.n) || /diffuse/i.test(a.n));
  const list = diffs.length ? diffs : pool;
  const table = [
    ["glass", ["glass"]],
    ["light", ["light", "lamp"]], ["cabin", ["cabin", "interior", "cockpit"]], ["carbon", ["carbon"]],
    ["body", ["body", "paint", "livery"]], ["caliper", ["caliper"]], ["disc", ["disc", "brake"]],
    ["grille", ["grill", "light"]], ["suspension", ["susp"]], ["aerial", ["aerial"]],
    ["helmet", ["helmet"]], ["head", ["head"]], ["hand", ["hand"]], ["eye", ["eye"]],
  ];
  for (const [key, words] of table) {
    if (!n.includes(key)) continue;
    const hit = list.find((a) => words.some((w) => a.n.toLowerCase().includes(w)));
    if (hit) return hit;
  }
  return list.find((a) => /body|paint|main/i.test(a.n)) || null;
}

function mat4Mul(a, b) {
  const o = new Float32Array(16);
  const a00 = a[0], a01 = a[1], a02 = a[2], a03 = a[3];
  const a10 = a[4], a11 = a[5], a12 = a[6], a13 = a[7];
  const a20 = a[8], a21 = a[9], a22 = a[10], a23 = a[11];
  const a30 = a[12], a31 = a[13], a32 = a[14], a33 = a[15];
  for (let i = 0; i < 4; i++) {
    const b0 = b[i * 4], b1 = b[i * 4 + 1], b2 = b[i * 4 + 2], b3 = b[i * 4 + 3];
    o[i * 4] = b0 * a00 + b1 * a10 + b2 * a20 + b3 * a30;
    o[i * 4 + 1] = b0 * a01 + b1 * a11 + b2 * a21 + b3 * a31;
    o[i * 4 + 2] = b0 * a02 + b1 * a12 + b2 * a22 + b3 * a32;
    o[i * 4 + 3] = b0 * a03 + b1 * a13 + b2 * a23 + b3 * a33;
  }
  return o;
}
function mat4Perspective(fovy, aspect, near, far) {
  const f = 1 / Math.tan(fovy / 2), nf = 1 / (near - far);
  const o = new Float32Array(16);
  o[0] = f / aspect; o[5] = f; o[10] = (far + near) * nf; o[11] = -1; o[14] = 2 * far * near * nf;
  return o;
}
function mat4Look(eye, at) {
  let zx = eye[0] - at[0], zy = eye[1] - at[1], zz = eye[2] - at[2];
  let len = Math.hypot(zx, zy, zz) || 1; zx /= len; zy /= len; zz /= len;
  let xx = zz, xy = 0, xz = -zx;
  len = Math.hypot(xx, xy, xz) || 1; xx /= len; xy /= len; xz /= len;
  const yx = zy * xz - zz * xy, yy = zz * xx - zx * xz, yz = zx * xy - zy * xx;
  const o = new Float32Array(16);
  o[0] = xx; o[1] = yx; o[2] = zx; o[4] = xy; o[5] = yy; o[6] = zy; o[8] = xz; o[9] = yz; o[10] = zz;
  o[12] = -(xx * eye[0] + xy * eye[1] + xz * eye[2]);
  o[13] = -(yx * eye[0] + yy * eye[1] + yz * eye[2]);
  o[14] = -(zx * eye[0] + zy * eye[1] + zz * eye[2]);
  o[15] = 1;
  return o;
}

function parseGeom(buf) {
  const view = new DataView(buf);
  const magic = String.fromCharCode(view.getUint8(0), view.getUint8(1), view.getUint8(2), view.getUint8(3));
  if (magic !== "DR2M") throw new Error("DR2M");
  const dec = new TextDecoder();
  let o = 8;
  const count = view.getUint32(4, true);
  const meshes = [];
  for (let m = 0; m < count; m++) {
    const nameLen = view.getUint16(o, true);
    const matLen = view.getUint16(o + 2, true);
    const verts = view.getUint32(o + 4, true);
    const indices = view.getUint32(o + 8, true);
    const flags = view.getUint32(o + 12, true);
    o += 16;
    const name = dec.decode(new Uint8Array(buf, o, nameLen)); o += nameLen;
    const material = dec.decode(new Uint8Array(buf, o, matLen)); o += matLen;
    o = (o + 3) & ~3;
    const pos = new Float32Array(verts * 3);
    const uv = new Float32Array(verts * 2);
    for (let i = 0; i < verts * 3; i++) pos[i] = view.getFloat32(o + i * 4, true);
    o += verts * 12;
    for (let i = 0; i < verts * 2; i++) uv[i] = view.getFloat32(o + i * 4, true);
    o += verts * 8;
    const wide = flags & 1;
    const ind = new Uint32Array(indices);
    const step = wide ? 4 : 2;
    for (let i = 0; i < indices; i++) ind[i] = wide ? view.getUint32(o + i * step, true) : view.getUint16(o + i * step, true);
    o += indices * step;
    o = (o + 3) & ~3;
    meshes.push({ name, material, pos, uv, ind });
  }
  return meshes;
}

function modelGL() {
  const canvas = document.getElementById("model-view");
  const gl = canvas.getContext("webgl", { antialias: true, alpha: false });
  if (!gl) return null;
  modelView.indexUint = gl.getExtension("OES_element_index_uint");
  const deriv = gl.getExtension("OES_standard_derivatives");
  const vs = `attribute vec3 aPos; attribute vec2 aUv; uniform mat4 uMvp; uniform mat4 uMv;
    varying vec2 vUv; varying vec3 vView;
    void main() { vUv = aUv; vec4 p = uMv * vec4(aPos, 1.0); vView = p.xyz; gl_Position = uMvp * vec4(aPos, 1.0); }`;
  const fs = `${deriv ? "#extension GL_OES_standard_derivatives : enable\n" : ""}
    precision mediump float; varying vec2 vUv; varying vec3 vView;
    uniform sampler2D uTex; uniform vec3 uColor; uniform int uHasTex; uniform int uFlip; uniform int uTread;
    void main() {
      ${deriv ? "vec3 n = normalize(cross(dFdx(vView), dFdy(vView))); float light = abs(dot(n, normalize(vec3(0.3, 0.85, 0.45)))) * 0.7 + 0.3;" : "float light = 1.0;"}
      vec2 uv = vUv;
      if (uTread == 1) {
        // Borracha escura do atlas, à direita dos parafusos e acima do rótulo
        // "TREAD" (037 e es2: x 128..384, y 868..932 de 1024). O retângulo de
        // baixo é o rótulo, não a banda.
        uv = vec2(0.125 + fract(vUv.x) * 0.25, 0.848 + fract(vUv.y) * 0.062);
      } else if (uFlip == 1) uv.y = 1.0 - uv.y;
      vec3 c = uHasTex == 1 ? texture2D(uTex, uv).rgb : uColor;
      gl_FragColor = vec4(c * light, 1.0);
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
  for (let i = -4; i <= 4; i++) {
    lines.push(i, 0, -4, i, 0, 4, -4, 0, i, 4, 0, i);
  }
  gl.bindBuffer(gl.ARRAY_BUFFER, grid);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(lines), gl.STATIC_DRAW);
  modelView.gl = gl;
  modelView.program = program;
  modelView.grid = { buf: grid, count: lines.length / 3 };
  const draw = () => {
    requestAnimationFrame(draw);
    if (state.mode !== "models" || !modelView.gl) return;
    const w = canvas.clientWidth || 800, h = canvas.clientHeight || 480;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    if (canvas.width !== Math.floor(w * dpr) || canvas.height !== Math.floor(h * dpr)) {
      canvas.width = Math.floor(w * dpr); canvas.height = Math.floor(h * dpr);
    }
    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.clearColor(0.05, 0.06, 0.08, 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    const aspect = canvas.width / Math.max(1, canvas.height);
    const cam = modelCamera();
    const view = mat4Look(cam.eye, cam.look);
    const proj = mat4Perspective(0.9, aspect, 0.05, 200);
    const mvp = mat4Mul(proj, view);
    gl.useProgram(program);
    const locPos = gl.getAttribLocation(program, "aPos");
    const locUv = gl.getAttribLocation(program, "aUv");
    gl.uniformMatrix4fv(gl.getUniformLocation(program, "uMvp"), false, mvp);
    gl.uniformMatrix4fv(gl.getUniformLocation(program, "uMv"), false, view);
    gl.uniform1i(gl.getUniformLocation(program, "uFlip"), modelView.flip ? 1 : 0);
    const locTread = gl.getUniformLocation(program, "uTread");
    gl.uniform1i(gl.getUniformLocation(program, "uHasTex"), 0);
    gl.uniform3f(gl.getUniformLocation(program, "uColor"), 0.25, 0.28, 0.32);
    gl.bindBuffer(gl.ARRAY_BUFFER, grid.buf);
    gl.enableVertexAttribArray(locPos);
    gl.vertexAttribPointer(locPos, 3, gl.FLOAT, false, 0, 0);
    gl.disableVertexAttribArray(locUv);
    gl.vertexAttrib2f(locUv, 0, 0);
    gl.drawArrays(gl.LINES, 0, modelView.grid.count);
    for (const mesh of modelView.meshes) {
      if (!meshVisible(mesh.material)) continue;
      gl.bindBuffer(gl.ARRAY_BUFFER, mesh.pos);
      gl.enableVertexAttribArray(locPos);
      gl.vertexAttribPointer(locPos, 3, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, mesh.uv);
      gl.enableVertexAttribArray(locUv);
      gl.vertexAttribPointer(locUv, 2, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, mesh.idx);
      gl.uniform1i(locTread, mesh.tread ? 1 : 0);
      const tex = modelView.forceTex ? modelView.textures.get(modelView.forceTex) : mesh.tex;
      if (tex) {
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, tex);
        gl.uniform1i(gl.getUniformLocation(program, "uHasTex"), 1);
        gl.uniform1i(gl.getUniformLocation(program, "uTex"), 0);
      } else {
        gl.uniform1i(gl.getUniformLocation(program, "uHasTex"), 0);
        const c = colorOf(mesh.material);
        gl.uniform3f(gl.getUniformLocation(program, "uColor"), c[0], c[1], c[2]);
      }
      gl.drawElements(gl.TRIANGLES, mesh.count, mesh.wide ? gl.UNSIGNED_INT : gl.UNSIGNED_SHORT, 0);
    }
  };
  canvas.addEventListener("contextmenu", (e) => e.preventDefault());
  canvas.addEventListener("pointerdown", (e) => {
    if (e.button > 2) return;
    const pan = e.button === 1 || e.button === 2 || e.shiftKey;
    const cam = modelCamera();
    modelView.drag = {
      x: e.clientX, y: e.clientY, yaw: modelView.yaw, pitch: modelView.pitch, pan,
      px: modelView.pan[0], py: modelView.pan[1], pz: modelView.pan[2],
      right: cam.right, up: cam.up,
    };
    canvas.setPointerCapture(e.pointerId);
    if (pan) e.preventDefault();
  });
  canvas.addEventListener("pointermove", (e) => {
    const drag = modelView.drag;
    if (!drag) return;
    const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
    if (drag.pan) {
      const scale = modelView.dist * 0.0022;
      modelView.pan[0] = drag.px + (drag.right[0] * dx - drag.up[0] * dy) * scale;
      modelView.pan[1] = drag.py + (drag.right[1] * dx - drag.up[1] * dy) * scale;
      modelView.pan[2] = drag.pz + (drag.right[2] * dx - drag.up[2] * dy) * scale;
      return;
    }
    // Agarrar o modelo: arrastar para a direita gira o modelo para a direita.
    modelView.yaw = drag.yaw - dx * 0.008;
    modelView.pitch = Math.max(-1.2, Math.min(1.2, drag.pitch + dy * 0.008));
  });
  canvas.addEventListener("pointerup", () => { modelView.drag = null; });
  canvas.addEventListener("pointercancel", () => { modelView.drag = null; });
  canvas.addEventListener("wheel", (e) => {
    modelView.dist = Math.max(0.4, Math.min(40, modelView.dist * (e.deltaY > 0 ? 1.08 : 0.92)));
    e.preventDefault();
  }, { passive: false });
  requestAnimationFrame(draw);
  return gl;
}

function modelCamera() {
  const cp = Math.cos(modelView.pitch), sp = Math.sin(modelView.pitch);
  const cy = Math.cos(modelView.yaw), sy = Math.sin(modelView.yaw);
  const pan = modelView.pan;
  const look = [pan[0], (modelView.lookY || 0.4) + pan[1], pan[2]];
  const dir = [sy * cp, sp, cy * cp];
  const dist = modelView.dist;
  const eye = [look[0] + dir[0] * dist, look[1] + dir[1] * dist, look[2] + dir[2] * dist];
  let rx = dir[2], ry = 0, rz = -dir[0];
  const rl = Math.hypot(rx, ry, rz) || 1;
  rx /= rl; ry /= rl; rz /= rl;
  const up = [dir[1] * rz - dir[2] * ry, dir[2] * rx - dir[0] * rz, dir[0] * ry - dir[1] * rx];
  return { eye, look, right: [rx, ry, rz], up };
}

function uploadModel(parsed, model) {
  const gl = modelView.gl;
  for (const old of modelView.meshes) {
    gl.deleteBuffer(old.pos); gl.deleteBuffer(old.uv); gl.deleteBuffer(old.idx);
  }
  let min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
  for (const mesh of parsed) for (let i = 0; i < mesh.pos.length; i += 3) {
    for (let k = 0; k < 3; k++) { min[k] = Math.min(min[k], mesh.pos[i + k]); max[k] = Math.max(max[k], mesh.pos[i + k]); }
  }
  const height = (max[1] - min[1]) || 1;
  const radius = 0.5 * Math.hypot(max[0] - min[0], height, max[2] - min[2]) || 1;
  modelView.lookY = height * 0.5;
  modelView.pan = [0, 0, 0];
  modelView.dist = (radius / Math.tan(0.45)) * 1.25;
  const cx = (min[0] + max[0]) / 2, cz = (min[2] + max[2]) / 2;
  const meshes = [];
  for (const mesh of parsed) {
    if (/disc/i.test(mesh.material) && !/disc_blur/i.test(mesh.material)) mapDiscUv(mesh);
    else if (/wheel/i.test(mesh.material) && !isTread(mesh.material)) {
      openWheelCenter(mesh);
      rubberizeInnerWheel(mesh);
    }
    const wide = mesh.ind.some((i) => i > 65535);
    if (wide && !modelView.indexUint) continue;
    const pos = new Float32Array(mesh.pos.length);
    for (let i = 0; i < mesh.pos.length; i += 3) {
      pos[i] = mesh.pos[i] - cx; pos[i + 1] = mesh.pos[i + 1] - min[1]; pos[i + 2] = mesh.pos[i + 2] - cz;
    }
    const pb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, pb); gl.bufferData(gl.ARRAY_BUFFER, pos, gl.STATIC_DRAW);
    const ub = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, ub); gl.bufferData(gl.ARRAY_BUFFER, mesh.uv, gl.STATIC_DRAW);
    const ib = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, wide ? mesh.ind : new Uint16Array(mesh.ind), gl.STATIC_DRAW);
    const guess = guessTexture(mesh.material, model.tex || []);
    meshes.push({
      material: mesh.material, tread: isTread(mesh.material), pos: pb, uv: ub, idx: ib, count: mesh.ind.length, wide,
      texId: guess && (guess.g + "/" + guess.n), tex: null,
    });
  }
  modelView.meshes = meshes;
  for (const mesh of meshes) if (mesh.texId) ensureModelTexture(mesh.texId, mesh);
}

function ensureModelTexture(id, mesh) {
  if (modelView.textures.has(id)) { mesh.tex = modelView.textures.get(id); return; }
  const asset = assetById.get(id);
  if (!asset) return;
  const image = new Image();
  image.onload = () => {
    const gl = modelView.gl;
    if (!gl) return;
    const tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, 0);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, image);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
    gl.generateMipmap(gl.TEXTURE_2D);
    modelView.textures.set(id, tex);
    if (mesh) mesh.tex = tex;
    for (const other of modelView.meshes) if (other.texId === id) other.tex = tex;
  };
  image.src = asset.p;
}

function loadModel(id) {
  const model = modelById.get(id);
  const note = document.getElementById("model-note");
  if (!model || !modelView.gl) return;
  modelView.loaded = id;
  modelView.forceTex = null;
  if (!model.geom) {
    modelView.meshes.forEach((m) => { modelView.gl.deleteBuffer(m.pos); modelView.gl.deleteBuffer(m.uv); modelView.gl.deleteBuffer(m.idx); });
    modelView.meshes = [];
    if (note) note.textContent = model.note || t("mdl.nogeom");
    return;
  }
  if (note) note.textContent = t("mdl.drag");
  fetch(model.geom).then((r) => {
    if (!r.ok) throw new Error(String(r.status));
    return r.arrayBuffer();
  }).then((buf) => {
    if (modelView.loaded !== id) return;
    uploadModel(parseGeom(buf), model);
  }).catch((err) => {
    if (note) note.textContent = String(err && err.message || err) + " — " + t("mdl.nogeom");
  });
}

function ensureModelStage() {
  if (document.getElementById("model-view")) return;
  const info = MODEL_PACK.info || {};
  contentEl.innerHTML = `<div class="model-stage">
    <div class="model-bar">
      <label>${esc(t("mdl.surface"))}
        <select id="model-surface">
          <option value="tarmac">${esc(t("mdl.tarmac"))}</option>
          <option value="gravel">${esc(t("mdl.gravel"))}</option>
          <option value="snow">${esc(t("mdl.snow"))}</option>
          <option value="all">${esc(t("mdl.all"))}</option>
        </select>
      </label>
      <label><input type="checkbox" id="model-flip"> ${esc(t("mdl.flip"))}</label>
      ${info.partial ? `<span class="warn">${esc(t("mdl.partial", { filter: info.filter || "—" }))}</span>` : ""}
    </div>
    <canvas class="gl" id="model-view"></canvas>
    <div class="model-foot" id="model-note"></div>
  </div>`;
  document.getElementById("model-surface").addEventListener("change", (e) => { modelView.variant = e.target.value; });
  document.getElementById("model-flip").addEventListener("change", (e) => { modelView.flip = e.target.checked; });
  try { modelGL(); }
  catch (err) { document.getElementById("model-note").textContent = String(err.message || err); }
}

MODES.models = {
  stage: false, hash: "m",
  filters() {
    const counts = new Map();
    for (const m of MODEL_LIST) counts.set(m.k, (counts.get(m.k) || 0) + 1);
    const withGeom = MODEL_LIST.filter((m) => m.geom).length;
    return [["all", `${t("filter.all")} (${MODEL_LIST.length})`], ["geom", `${t("mdl.geom")} (${withGeom})`],
      ...[...counts.entries()].sort((a, b) => b[1] - a[1]).map(([k, n]) => [k, `${modelKind(k)} (${n})`])];
  },
  list(q) {
    return MODEL_LIST.filter((m) => {
      if (state.filter === "geom") return !!m.geom;
      if (state.filter !== "all" && m.k !== state.filter) return false;
      return true;
    }).filter((m) => !q || (m.n + " " + m.src + " " + m.path + " " + (m.mats || []).join(" ")).toLowerCase().includes(q))
      .map((m) => `<div class="row${m.id === modelView.selected ? " on" : ""}" data-id="${esc(m.id)}">
        <div class="id">${esc(m.n)}${m.geom ? "" : ' <span class="badge b-orphan">índice</span>'}</div>
        <div class="sub"><span class="kind">${esc(modelKind(m.k))}</span> ${esc(m.src)}</div></div>`);
  },
  current: () => modelView.selected,
  select(id) { modelView.selected = id; },
  onFilter() {},
  show() {
    ensureModelStage();
    if (!modelView.selected) {
      const first = MODEL_LIST.find((m) => m.geom) || MODEL_LIST[0];
      if (first) modelView.selected = first.id;
    }
    if (modelView.selected && modelView.loaded !== modelView.selected) loadModel(modelView.selected);
    const m = modelById.get(modelView.selected);
    if (!m) { inspectEl.innerHTML = `<p class="muted">${esc(t("mdl.pick"))}</p>`; return; }
    const seenTex = new Set();
    const thumbs = (m.tex || []).filter((id) => (seenTex.has(id) ? false : seenTex.add(id))).map((id) => {
      const a = assetById.get(id);
      if (!a) return "";
      const on = modelView.forceTex === id ? " on" : "";
      return `<button class="tile${on}" data-tex="${esc(id)}" title="${esc(a.n)}"><span class="pic bg-checker"><img src="${esc(a.t)}" alt=""></span><span class="name">${esc(a.n)}</span></button>`;
    }).join("");
    inspectEl.innerHTML = `<h2>${esc(m.n)}</h2>
      <div class="kv">
        <div>${t("mdl.package")}</div><div class="mono">${esc(m.src)}</div>
        <div>${t("mdl.path")}</div><div class="mono">${esc(m.path)}</div>
        <div>${t("mdl.meshes")}</div><div>${m.meshes == null ? "—" : m.meshes}</div>
        <div>${t("mdl.verts")}</div><div>${m.verts == null ? "—" : m.verts.toLocaleString("pt-BR")}</div>
        <div>${t("mdl.tris")}</div><div>${m.tris == null ? "—" : m.tris.toLocaleString("pt-BR")}</div>
        <div>${t("img.size")}</div><div>${(m.bytes / (1024 * 1024)).toFixed(1)} MB</div>
      </div>
      ${m.note ? `<p class="warn">${esc(m.note)}</p>` : ""}
      ${section("mdl.mats", (m.mats || []).map((name) => `<div class="mono">${esc(name)}</div>`).join("") || "—")}
      ${section("mdl.textures", thumbs ? `<p class="muted small">${esc(t("mdl.auto"))}</p><div class="grid">${thumbs}</div>` : `<p class="muted">${esc(t("img.unused"))}</p>`)}
      <p class="muted small">${esc(t("mdl.limit"))}</p>`;
    inspectEl.querySelectorAll("[data-tex]").forEach((b) => b.addEventListener("click", () => {
      modelView.forceTex = modelView.forceTex === b.dataset.tex ? null : b.dataset.tex;
      if (modelView.forceTex) ensureModelTexture(modelView.forceTex, null);
      this.show();
    }));
  },
};

// Lista lateral escondida nas abas que usam a área central.
const _refresh = refresh;
refresh = function () {
  document.querySelector(".side .list").hidden = !!MODES[state.mode].noList;
  _refresh();
};
