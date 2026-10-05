// DR2 UI Viewer: compõe as telas de screens.bin sobre as cenas PSSG e desenha
// numa tela 1920x1080 (câmera ortográfica do jogo: 19,2 x 10,8 unidades).
"use strict";

const SD = window.SCENE_DATA, UD = window.UI_DATA, STR = window.STRING_DATA, ASSETS = window.ASSET_DATA || [];
const W = 1920, H = 1080, UNIT = 100;
const ID = [1, 0, 0, 1, 0, 0];

const state = {
  mode: "screens", lang: "bra", screen: null, component: null, focus: -1,
  filter: "all", search: "",
  opts: { boxes: false, reveal: false, names: false, data: true, cond: false, full: false, static: false, bg: true },
  // Animação: quadro por instância animada (caminho), quadro arrastado,
  // progresso das transições de posição (1 = tela aberta) e a reprodução.
  anim: { frames: new Map(), scrub: null, trans: 1, play: null, scope: "screen", speed: 1 },
  switchSel: new Map(),
};

// Abas do topo. Telas e Cenas são deste arquivo; content.js registra o resto.
const MODES = {};

// ------------------------------------------------------------ índices

const screenById = new Map(UD.screens.map((s) => [s.id, s]));
const scenesByLast = new Map();
for (const name of Object.keys(SD.scenes)) {
  const last = name.split("/").pop();
  if (!scenesByLast.has(last)) scenesByLast.set(last, []);
  scenesByLast.get(last).push(name);
}
const statesByScreen = new Map();
for (const [sid, st] of Object.entries(UD.states)) {
  for (const v of Object.values(st.a)) {
    if (screenById.has(v)) {
      if (!statesByScreen.has(v)) statesByScreen.set(v, []);
      statesByScreen.get(v).push(sid);
    }
  }
}
const flowByState = new Map();
const incoming = new Map();
for (const [nid, n] of Object.entries(UD.flow)) {
  if (!flowByState.has(n.s)) flowByState.set(n.s, []);
  flowByState.get(n.s).push(nid);
  for (const l of n.l) {
    if (!incoming.has(l.target)) incoming.set(l.target, []);
    incoming.get(l.target).push({ ev: l.id, from: nid, type: l.type });
  }
}
// Telas citadas por outras telas ou estados (abas, embutidas).
const referenced = new Set();
{
  const visit = (v, self) => {
    for (const tok of String(v).split(/[\s;,]+/)) if (tok !== self && screenById.has(tok)) referenced.add(tok);
  };
  for (const s of UD.screens) {
    for (const it of s.items) for (const b of it.b) { for (const v of Object.values(b.a || {})) visit(v, s.id); if (b.x) visit(b.x, s.id); }
    for (const b of s.beh) { for (const v of Object.values(b.a || {})) visit(v, s.id); if (b.x) visit(b.x, s.id); }
  }
}
const exeNames = new Set(UD.exe_names || []);
// state: algum estado do fluxo abre; ref: outra tela ou estado cita (aba,
// bloco embutido); code: só o executável tem o nome (o C++ pode abrir);
// orphan: nenhuma referência achada.
function screenClass(s) {
  const sts = statesByScreen.get(s.id) || [];
  if (sts.some((sid) => flowByState.has(sid))) return "state";
  if (referenced.has(s.id)) return "ref";
  if (exeNames.has(s.id)) return "code";
  return "orphan";
}
const assetByName = new Map();
for (const a of ASSETS) if (!assetByName.has(a.n)) assetByName.set(a.n, a);

function str(key) {
  if (key == null) return null;
  const own = STR[state.lang] && STR[state.lang][key];
  if (own != null) return own;
  const alt = (state.lang === "eng" ? STR.bra : STR.eng)[key];
  return alt == null ? null : alt;
}
// A língua escolhida não tem a chave, mas a outra tem.
function strFellBack(key) {
  if (key == null || !STR[state.lang] || STR[state.lang][key] != null) return false;
  const alt = (state.lang === "eng" ? STR.bra : STR.eng)[key];
  return alt != null;
}
function showStr(key) {
  const v = str(key);
  if (v == null) return null;
  return strFellBack(key) ? "† " + v : v;
}
function screenTitle(s) {
  const b = s.beh.find((x) => x.t === "SBScreenTitle");
  return b && b.a && b.a.string_id ? showStr(b.a.string_id) : null;
}
const esc = (s) => String(s ?? "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

// ------------------------------------------------------------ instâncias

let seq = 0;
// Nós de transição (null_trans_*, anim_main…) deslizam a tela na abertura;
// a posição gravada é o início do deslize. Guardamos o deslocamento e o
// aplicamos conforme o progresso das transições (state.anim.trans).
const TRANSITION_NAME = /^(null_trans|anim_(node|main|secondary|tertiary|tabs|transition))/;
const isTransition = (n) => TRANSITION_NAME.test(n.n) && (!n.m || Math.abs(n.m[5]) < 1e-4);
function makeInst(node, depth, parentPath) {
  let m = node.m || ID;
  let trans0 = null;
  if (isTransition(node) && (m[4] || m[5])) { trans0 = [m[4], m[5]]; m = [m[0], m[1], m[2], m[3], 0, 0]; }
  const path = (parentPath || "") + "/" + node.n;
  const o = { src: node, name: node.n, m, trans0, path, ui: node.ui || {}, draw: node.draw || [], kids: [], parent: null, id: seq++ };
  (node.c || []).forEach((c, i) => addKid(o, makeInst(c, depth, path + "#" + i)));
  const ref = o.ui.UINODEANIMATED || o.ui.UINODESCREEN || o.ui.UINODEXREF;
  if (ref && ref.xr) {
    const r = SD.scenes[ref.xr];
    if (r && depth < 16) { const ri = makeInst(r, depth + 1, path + "@"); ri.xref = ref.xr; addKid(o, ri); }
    else o.missing = ref.xr;
  }
  return o;
}
function addKid(p, k) { k.parent = p; p.kids.push(k); }
const isSwitch = (o) => !!o.ui.UINODESWITCH;
function switchKids(o) {
  if (!isSwitch(o)) return o.kids;
  if (o.sel != null) return o.kids.filter((k) => k.name === o.sel);
  const ir = o.ui.UINODESWITCH.ir;
  return Number.isInteger(ir) && o.kids[ir] ? [o.kids[ir]] : o.kids.slice(0, 1);
}
// Busca em largura pelo apelido, atravessando instâncias (xr) e respeitando
// a escolha dos switches.
function findDesc(base, name) {
  const q = [...switchKids(base)];
  while (q.length) {
    const o = q.shift();
    if (o.name === name) return o;
    q.push(...switchKids(o));
  }
  return null;
}
const isNum = (s) => /^\d+$/.test(s);
function numKids(o) { return o.kids.filter((k) => isNum(k.name)); }
// Posição além das declaradas na cena (ex.: smart_set.9 num molde de 0 a 7):
// copia o último filho numérico e extrapola o passo.
function synth(base, k) {
  const q = [base];
  while (q.length) {
    const o = q.shift();
    const nums = numKids(o);
    if (nums.length) {
      const sorted = nums.sort((a, b) => +a.name - +b.name);
      const last = sorted[sorted.length - 1];
      if (k <= +last.name) return null;
      const prev = sorted.length > 1 ? sorted[sorted.length - 2] : null;
      const dx = prev ? last.m[4] - prev.m[4] : 0, dy = prev ? last.m[5] - prev.m[5] : -0.6;
      const n = k - +last.name;
      const clone = makeInst(last.src, 0, o.path + "#s" + k);
      clone.name = String(k); clone.synth = true;
      clone.m = [...last.m.slice(0, 4), last.m[4] + dx * n, last.m[5] + dy * n];
      addKid(o, clone);
      return clone;
    }
    q.push(...switchKids(o));
  }
  return null;
}
function resolve(base, path) {
  if (!base) return null;
  if (!path) return base;
  let cur = base;
  for (const seg of path.split(".")) {
    let nx = findDesc(cur, seg);
    if (!nx && isNum(seg)) nx = synth(cur, +seg);
    if (!nx) return null;
    cur = nx;
  }
  return cur;
}
function markUsed(o) { for (let p = o; p; p = p.parent) p.used = true; }

// ------------------------------------------------------------ ligação dos itens

const TEXT_GLYPH_DEFAULT = { IBComboTextStatic: "text_value", IBComboTextData: "text_value" };

function bindScreen(root, screen, asParent) {
  const report = [];
  screen.items.forEach((it, i) => {
    const base = resolve(root, it.a.glyph);
    const r = { base, problems: [], warnings: [] };
    report.push(r);
    if (!base) { r.problems.push(t("err.itemglyph", { glyph: it.a.glyph || "—" })); return; }
    markUsed(base);
    base.item = asParent ? -2 : i;
    // Switches primeiro: definem qual componente responde aos glyphs seguintes.
    for (const b of it.b) if (b.t === "BSwitchStatic" || b.t === "IBSwitchChoice") {
      const a = b.a || {};
      let tg = a.glyph ? resolve(base, a.glyph) : base;
      if (tg && !isSwitch(tg)) tg = firstSwitch(tg);
      if (tg && a.object) { tg.sel = a.object; if (!tg.kids.some((k) => k.name === a.object)) r.problems.push(t("err.switch", { object: a.object })); }
      else if (a.object) r.problems.push(t("err.noswitch", { object: a.object }));
    }
    for (const b of it.b) applyBeh(base, b, r);
  });
  for (const b of screen.beh) applyScreenBeh(root, b);
  hideUnusedSlots(root);
  relayoutStackers(root);
  return report;
}
function firstSwitch(o) {
  const q = [o];
  while (q.length) { const x = q.shift(); if (isSwitch(x)) return x; q.push(...x.kids); }
  return null;
}
function applyBeh(base, b, r) {
  const a = b.a || {};
  const soft = /^(BVisibilityControl|BTextureData|BTextureStatic)/.test(b.t);
  const target = (g) => {
    const n = resolve(base, g);
    if (!n && g) (soft ? r.warnings : r.problems).push(t("err.glyph", { kind: b.t, glyph: g }));
    return n;
  };
  switch (b.t) {
    case "BTextStatic": { const n = target(a.glyph); if (n) n.text = { key: a.string, fmt: a.format_id }; break; }
    case "BTextData": case "BTextTokens": { const n = target(a.glyph); if (n) n.text = { data: a.data_path, fmt: a.format_id }; break; }
    case "BInputActionText": { const n = target(a.glyph); if (n) n.text = { data: "input:" + (a.action || "?") }; break; }
    case "IBComboTextStatic": {
      const n = target(a.glyph || TEXT_GLYPH_DEFAULT[b.t]);
      const first = (a.value_list || "").split(";")[0];
      if (n) n.text = { key: first, data: first ? null : a.data_path, combo: true };
      break;
    }
    case "IBComboTextData": { const n = target(a.glyph || TEXT_GLYPH_DEFAULT[b.t]); if (n) n.text = { data: a.list_data_path || a.data_path, combo: true }; break; }
    case "BTextureStatic": { const n = target(a.glyph); if (n) n.tex = a.texture; break; }
    case "BTextureData": { const n = target(a.glyph); if (n) n.texData = a.data_path; break; }
    case "BVisibilityControlStatic": { const n = target(a.glyph); if (n && a.visible === "false") { n.hidden = true; n.hiddenWhy = "beh"; } break; }
    case "BVisibilityControlData": { const n = target(a.glyph); if (n) n.cond = a.data_path; break; }
  }
}
function applyScreenBeh(root, b) {
  const a = b.a || {};
  if (b.t === "BVisibilityControlStatic" && a.visible === "false") { const n = resolve(root, a.glyph); if (n) { n.hidden = true; n.hiddenWhy = "beh"; } }
  if (b.t === "SBScreenTitle") {
    const n = ["screen_header_text", "text_screen_title", "title_text"].map((g) => findDesc(root, g)).find(Boolean);
    if (n && !n.text) n.text = a.override_data_path ? { data: a.override_data_path } : { key: a.string_id, fmt: a.override_format_id };
  }
}
// Posições numéricas que nenhum item usa ficam vazias no jogo.
function hideUnusedSlots(root) {
  const walk = (o) => {
    const nums = numKids(o);
    if (nums.length >= 2 && nums.some((k) => k.used)) for (const k of nums) if (!k.used && !k.hidden) { k.hidden = true; k.hiddenWhy = "slot"; }
    o.kids.forEach(walk);
  };
  walk(root);
}
// O stacker fecha os buracos dos itens escondidos.
function relayoutStackers(root) {
  const walk = (o) => {
    if (o.ui.UINODESTACKER) {
      const nums = numKids(o).sort((a, b) => +a.name - +b.name);
      if (nums.length >= 2) {
        const base = nums.filter((k) => !k.synth);
        const k0 = base[0], k1 = base[1] || base[0];
        const dx = k1.m[4] - k0.m[4], dy = k1.m[5] - k0.m[5];
        let i = 0;
        for (const k of nums) {
          if (k.hidden || (state.opts.cond && k.cond)) continue;
          k.m = [...k.m.slice(0, 4), k0.m[4] + dx * i, k0.m[5] + dy * i];
          i++;
        }
      }
    }
    o.kids.forEach(walk);
  };
  walk(root);
}

// ------------------------------------------------------------ texturas

const images = new Map();
function texImage(name) {
  if (!name) return null;
  if (images.has(name)) return images.get(name);
  const info = SD.textures[name];
  if (!info) { images.set(name, null); return null; }
  const img = new Image();
  img.onload = () => { img.ready = true; scheduleRender(); };
  img.onerror = () => { img.failed = true; };
  img.src = info[0];
  images.set(name, img);
  return img;
}
const tintCache = new Map();
function tinted(img, col) {
  if (col[0] >= 0.999 && col[1] >= 0.999 && col[2] >= 0.999) return img;
  const key = img.src + "|" + col.map((v) => v.toFixed(3)).join(",");
  if (tintCache.has(key)) return tintCache.get(key);
  const c = document.createElement("canvas");
  c.width = img.naturalWidth; c.height = img.naturalHeight;
  const g = c.getContext("2d");
  g.drawImage(img, 0, 0);
  g.globalCompositeOperation = "multiply";
  g.fillStyle = rgb(col, 1);
  g.fillRect(0, 0, c.width, c.height);
  g.globalCompositeOperation = "destination-in";
  g.drawImage(img, 0, 0);
  if (tintCache.size > 300) tintCache.clear();
  tintCache.set(key, c);
  return c;
}
const rgb = (c, a) => `rgba(${Math.round(Math.min(1, Math.max(0, c[0])) * 255)},${Math.round(Math.min(1, Math.max(0, c[1])) * 255)},${Math.round(Math.min(1, Math.max(0, c[2])) * 255)},${a})`;

// ------------------------------------------------------------ animação

// Curvas exportadas: [material, parâmetro, W, chaves, fim]; chave =
// [t, v0(W), v1(W), v2(W), v3(W)], cúbico em s normalizado no segmento.
function evalChannel(ch, f) {
  const [, , w, keys, end] = ch;
  const at = (k, s) => {
    const out = [];
    for (let j = 0; j < w; j++) out.push(k[1 + j] + k[1 + w + j] * s + k[1 + 2 * w + j] * s * s + k[1 + 3 * w + j] * s * s * s);
    return out;
  };
  if (!keys.length || f >= end[0]) return end.slice(1);
  if (f <= keys[0][0]) return at(keys[0], 0);
  for (let i = 0; i < keys.length; i++) {
    const t0 = keys[i][0], t1 = i + 1 < keys.length ? keys[i + 1][0] : end[0];
    if (f >= t0 && f < t1) return at(keys[i], t1 > t0 ? (f - t0) / (t1 - t0) : 0);
  }
  return end.slice(1);
}
function animSpan(anim) {
  let lo = Infinity, hi = 0;
  for (const ch of anim.ch) { lo = Math.min(lo, ch[3].length ? ch[3][0][0] : ch[4][0]); hi = Math.max(hi, ch[4][0]); }
  for (const v of Object.values(anim.ev || {})) hi = Math.max(hi, v);
  return [lo === Infinity ? 0 : lo, hi];
}
// Quadro de repouso: o fim do "open" (tela aberta) ou o quadro padrão; o item
// em foco usa o fim do "activate".
function restFrame(anim, focused) {
  const ev = anim.ev || {};
  const rest = ev.open ?? anim.df ?? 0;
  if (focused) return ev.activate ?? ev.highlight ?? ev.on ?? rest;
  return rest;
}
function frameOf(o) {
  const anim = o.src.anim;
  if (state.anim.scrub != null) return state.anim.scrub;
  if (state.anim.frames.has(o.path)) return state.anim.frames.get(o.path);
  return restFrame(anim, state.focus >= 0 && ownerItem(o) === state.focus);
}
function animOverrides(o, up) {
  const anim = o.src.anim;
  if (!anim || !anim.ch.length || state.opts.static) return up;
  const f = frameOf(o);
  const ov = Object.assign({}, up);
  for (const ch of anim.ch) {
    const v = evalChannel(ch, f);
    ov[ch[0]] = Object.assign({}, ov[ch[0]], { [ch[1]]: ch[2] === 1 ? v[0] : v });
  }
  return ov;
}
const param = (op, name) => {
  const o = op.ov && op.ov[op.matKey];
  if (o && o[name] != null) return o[name];
  return op.mat ? op.mat[name] : undefined;
};
// Instâncias animadas da composição atual (para a barra de animação).
function animInstances(root) {
  const out = [];
  const walk = (o) => {
    if (o.hidden) return;
    if (o.src.anim && (o.src.anim.ch.length || Object.keys(o.src.anim.ev || {}).length)) out.push(o);
    switchKids(o).forEach(walk);
  };
  if (root) walk(root);
  return out;
}
const EV_ORDER = ["open", "close", "open_back", "close_back", "activate", "deactivate", "active_hover", "disable", "active_disable", "esport_activate", "on", "off", "highlight", "unhighlight"];
function eventLabel(name) {
  const key = "ev." + name;
  return t(key) === key ? name : t(key);
}
function animTargets(scope, filter) {
  const list = (current && current.animInst) || [];
  return list.filter((o) => {
    if (filter && o.src.n !== filter) return false;
    if (scope === "item") return state.focus >= 0 && ownerItem(o) === state.focus;
    return true;
  });
}
function playEvent(name, scope, filter) {
  const targets = animTargets(scope, filter).filter((o) => o.src.anim.ev && o.src.anim.ev[name] != null);
  state.anim.scrub = null;
  const tracks = targets.map((o) => {
    const ev = o.src.anim.ev;
    let from = frameOf(o);
    if (name === "open" || name === "open_back") from = 0;
    return { path: o.path, from, to: ev[name] };
  });
  const span = Math.max(0.15, ...tracks.map((tr) => Math.abs(tr.to - tr.from)));
  let transFrom = state.anim.trans, transTo = transFrom;
  if (scope !== "item" && !filter) {
    if (name === "open" || name === "open_back") { transFrom = 0; transTo = 1; }
    if (name === "close" || name === "close_back") { transFrom = state.anim.trans; transTo = 0; }
  }
  state.anim.play = { tracks, t0: performance.now(), dur: (span / state.anim.speed) * 1000, transFrom, transTo, name };
  tick();
}
function tick() {
  const p = state.anim.play;
  if (!p) return;
  const k = Math.min(1, (performance.now() - p.t0) / p.dur);
  for (const tr of p.tracks) state.anim.frames.set(tr.path, tr.from + (tr.to - tr.from) * k);
  const e = 1 - Math.pow(1 - k, 3);
  state.anim.trans = p.transFrom + (p.transTo - p.transFrom) * e;
  render();
  updateAnimBar();
  if (k < 1) requestAnimationFrame(tick);
  else { state.anim.play = null; updateAnimBar(); inspectHidden(); }
}
function resetAnim() {
  state.anim.frames.clear();
  state.anim.scrub = null;
  state.anim.trans = 1;
  state.anim.play = null;
}

// ------------------------------------------------------------ desenho

function compose(L, P) {
  return [L[0] * P[0] + L[1] * P[2], L[0] * P[1] + L[1] * P[3], L[2] * P[0] + L[3] * P[2], L[2] * P[1] + L[3] * P[3],
    L[4] * P[0] + L[5] * P[2] + P[4], L[4] * P[1] + L[5] * P[3] + P[5]];
}
const toPx = (M, x, y) => [W / 2 + (x * M[0] + y * M[2] + M[4]) * UNIT, H / 2 - (x * M[1] + y * M[3] + M[5]) * UNIT];

function ownerItem(o) { for (let p = o; p; p = p.parent) if (p.item != null) return p.item; return -1; }

// ghost = motivo pelo qual o nó não aparece; esses nós só são desenhados com
// "Mostrar ocultos", mas sempre entram na lista do painel.
function collect(root, host) {
  const ops = [];
  const hidden = [];
  let order = 0;
  const walk = (o, P, layer, scroll, texUp, ovUp, ghost) => {
    let g = ghost;
    if (!g) {
      if (o.hidden) g = o.hiddenWhy || "beh";
      else if (o.synth && scroll && !state.opts.full) g = "scroll";
      else if (o.cond && state.opts.cond) g = "cond";
      if (g) hidden.push({ o, why: g });
    }
    let m = o.m;
    if (o.trans0 && state.anim.trans < 1) m = [m[0], m[1], m[2], m[3], o.trans0[0] * (1 - state.anim.trans), o.trans0[1] * (1 - state.anim.trans)];
    const M = compose(m, P);
    const la = o.ui.UIMODLAYER ? o.ui.UIMODLAYER.la : layer;
    const tex = o.tex || texUp;
    const ov = animOverrides(o, ovUp);
    const textUi = o.ui.UINODETEXT || o.ui.UINODETEXTDOC;
    if (textUi) ops.push({ kind: "text", o, M, la, order: order++, ui: textUi, mat: o.draw[0] && SD.materials[o.draw[0][1]], matKey: o.draw[0] && o.draw[0][1], ov, ghost: g });
    else for (const [mesh, mat] of o.draw) ops.push({ kind: "mesh", o, M, la, order: order++, mesh: SD.meshes[mesh], mat: SD.materials[mat] || { g: "?" }, matKey: mat, stretch: !!o.ui.UIMODSTRETCH, tex, ov, ghost: g });
    for (const k of switchKids(o)) walk(k, M, la, scroll || !!o.ui.UINODESCROLL, tex, ov, g);
    // A opção não escolhida do switch existe na cena e não é desenhada.
    if (isSwitch(o) && !g && o.kids.length >= 2) {
      const chosen = new Set(switchKids(o));
      for (const k of o.kids) if (!chosen.has(k)) hidden.push({ o: k, why: "switch" });
    }
  };
  walk(root, host || ID, 0, false, null, {}, null);
  ops.sort((a, b) => a.la - b.la || a.order - b.order);
  return { ops, hidden };
}

const canvas = document.getElementById("view");
const ctx = canvas.getContext("2d");
let current = null;

const GHOST = "rgba(255, 64, 214, 0.95)";
function meshPoints(op) {
  const { mesh, M } = op;
  let pts = [];
  for (let i = 0; i < mesh.v.length; i += 2) pts.push(toPx(M, mesh.v[i], mesh.v[i + 1]));
  if (op.stretch) {
    // UIMODSTRETCH: a malha cobre a tela inteira.
    const xs = pts.map((p) => p[0]), ys = pts.map((p) => p[1]);
    const x0 = Math.min(...xs), x1 = Math.max(...xs), y0 = Math.min(...ys), y1 = Math.max(...ys);
    pts = pts.map(([x, y]) => [x1 > x0 ? ((x - x0) / (x1 - x0)) * W : 0, y1 > y0 ? ((y - y0) / (y1 - y0)) * H : 0]);
  }
  return pts;
}
function drawGhostHull(pts) {
  ctx.save();
  ctx.setLineDash([7, 5]);
  ctx.lineWidth = 2;
  ctx.strokeStyle = GHOST;
  ctx.fillStyle = "rgba(255, 64, 214, 0.07)";
  const xs = pts.map((p) => p[0]), ys = pts.map((p) => p[1]);
  const x = Math.min(...xs), y = Math.min(...ys), w = Math.max(...xs) - x, h = Math.max(...ys) - y;
  ctx.fillRect(x, y, w, h);
  ctx.strokeRect(x, y, w, h);
  ctx.restore();
}

function drawMesh(op, frame) {
  const { mesh, mat } = op;
  if (!mesh) return;
  const pts = meshPoints(op);
  let alpha = param(op, "Alpha") ?? 1;
  let ghost = op.ghost;
  if (!ghost && alpha <= 0.001 && !state.opts.static) { ghost = "alpha"; frame.alpha.push(op.o); }
  if (ghost) {
    if (!state.opts.reveal) return;
    alpha = 0.45;
  }
  if (!ghost) extendBox(frame.boxes, op.o, pts);
  const dc = param(op, "DiffuseColour");
  const col = Array.isArray(dc) ? dc : [1, 1, 1];
  // Textura trocada por BTextureStatic vale para as malhas com imagem abaixo.
  const texName = (mat.TDiffuseMap && op.tex) || op.o.tex || mat.TDiffuseMap;
  if (texName && !ghost) frame.textures.add(texName);
  const img = mat.g === "ui_2d_pattern_stretch" ? null : texImage(texName);
  ctx.save();
  ctx.globalAlpha = Math.min(1, alpha);
  if (mat.g === "ui_2d_image_screenblend" && !ghost) ctx.globalCompositeOperation = "screen";
  if (mat.g === "ui_2d_gradient_uv_horizontal" && mat.GradientLeftColour) {
    const xs = pts.map((p) => p[0]);
    const gr = ctx.createLinearGradient(Math.min(...xs), 0, Math.max(...xs), 0);
    gr.addColorStop(0, rgb(mat.GradientLeftColour, 1));
    if (mat.GradientCentreColour) gr.addColorStop(0.5, rgb(mat.GradientCentreColour, 1));
    gr.addColorStop(1, rgb(mat.GradientRightColour || mat.GradientLeftColour, 1));
    fillTris(mesh, pts, gr);
  } else if (img && img.ready && mesh.uv) {
    drawTextured(mesh, pts, ghost ? img : tinted(img, col), img.naturalWidth, img.naturalHeight);
  } else if (texName && !SD.textures[texName]) {
    // Textura fora dos bundles (vem do jogo em runtime): placeholder.
    fillTris(mesh, pts, rgb(col, 0.18));
  } else if (!texName || mat.g === "ui_2d_pattern_stretch") {
    fillTris(mesh, pts, rgb(col, 1));
  }
  ctx.restore();
  if (ghost) drawGhostHull(pts);
}
function fillTris(mesh, pts, style) {
  ctx.fillStyle = style;
  ctx.beginPath();
  for (let i = 0; i + 2 < mesh.i.length; i += 3) {
    const a = pts[mesh.i[i]], b = pts[mesh.i[i + 1]], c = pts[mesh.i[i + 2]];
    if (!a || !b || !c) continue;
    ctx.moveTo(a[0], a[1]); ctx.lineTo(b[0], b[1]); ctx.lineTo(c[0], c[1]); ctx.closePath();
  }
  ctx.fill();
}
// Mapeamento afim da textura: quads coerentes saem num drawImage só.
function affineFor(src, dst) {
  const [[u0, v0], [u1, v1], [u2, v2]] = src, [[x0, y0], [x1, y1], [x2, y2]] = dst;
  const d = (u1 - u0) * (v2 - v0) - (u2 - u0) * (v1 - v0);
  if (Math.abs(d) < 1e-9) return null;
  const a = ((x1 - x0) * (v2 - v0) - (x2 - x0) * (v1 - v0)) / d;
  const b = ((y1 - y0) * (v2 - v0) - (y2 - y0) * (v1 - v0)) / d;
  const c = ((x2 - x0) * (u1 - u0) - (x1 - x0) * (u2 - u0)) / d;
  const e = ((y2 - y0) * (u1 - u0) - (y1 - y0) * (u2 - u0)) / d;
  return [a, b, c, e, x0 - a * u0 - c * v0, y0 - b * u0 - e * v0];
}
function drawTextured(mesh, pts, img, tw, th) {
  const uv = [];
  for (let i = 0; i < mesh.uv.length; i += 2) uv.push([mesh.uv[i] * tw, mesh.uv[i + 1] * th]);
  const base = ctx.getTransform();
  const draw = (tri, clipPts) => {
    const m = affineFor(tri.map((k) => uv[k]), tri.map((k) => pts[k]));
    if (!m) return;
    ctx.save();
    ctx.beginPath();
    clipPts.forEach((p, i) => (i ? ctx.lineTo(p[0], p[1]) : ctx.moveTo(p[0], p[1])));
    ctx.closePath();
    ctx.clip();
    ctx.setTransform(base.multiply(new DOMMatrix(m)));
    ctx.drawImage(img, 0, 0);
    ctx.restore();
  };
  if (pts.length === 4 && mesh.i.length === 6) {
    const m = affineFor([0, 1, 2].map((k) => uv[k]), [0, 1, 2].map((k) => pts[k]));
    if (m) {
      const p3 = [m[0] * uv[3][0] + m[2] * uv[3][1] + m[4], m[1] * uv[3][0] + m[3] * uv[3][1] + m[5]];
      if (Math.hypot(p3[0] - pts[3][0], p3[1] - pts[3][1]) < 1) { draw([0, 1, 2], hull4(pts)); return; }
    }
  }
  for (let i = 0; i + 2 < mesh.i.length; i += 3) {
    const tri = [mesh.i[i], mesh.i[i + 1], mesh.i[i + 2]];
    if (tri.some((k) => !pts[k] || !uv[k])) continue;
    draw(tri, tri.map((k) => pts[k]));
  }
}
function hull4(p) {
  const cx = p.reduce((s, q) => s + q[0], 0) / 4, cy = p.reduce((s, q) => s + q[1], 0) / 4;
  return [...p].sort((a, b) => Math.atan2(a[1] - cy, a[0] - cx) - Math.atan2(b[1] - cy, b[0] - cx));
}

// ------------------------------------------------------------ texto

const WRAP = { none: 0, wordwrap: 1, clampwidth: 2, clampuniform: 3, truncate: 4 };
function fontFor(style, scale) {
  const f = (style && style.font) || "din_cnd_bold";
  const px = Math.max(1, (+(style && style.height) || 30) * scale);
  if (f.startsWith("roboto")) return `${px}px "Roboto Condensed", "Arial Narrow", sans-serif`;
  return `${f.includes("ita") ? "italic " : ""}600 ${px}px "Barlow Condensed", "Arial Narrow", sans-serif`;
}
function cleanMarkup(s) {
  return String(s).replace(/\{t:([^}]+)\}/g, (_, k) => showStr(k) ?? k).replace(/\{v\}|\{p\}|\{s:[^}]*\}|\{\/?[a-z]\}/g, "");
}
function textOf(o) {
  const tx = o.text;
  if (!tx) return state.opts.names ? { s: o.name, kind: "name" } : null;
  if (tx.key) {
    let key = tx.key;
    if (tx.fmt === "localise_upper" && str(key + "_caps") != null) key += "_caps";
    let s = str(key);
    if (s == null) return { s: `[${t("missing")}: ${key}]`, kind: "missing" };
    if (tx.fmt === "localise_upper") s = s.toUpperCase();
    const fell = strFellBack(key);
    return { s: (fell ? "† " : "") + cleanMarkup(s), kind: fell ? "fallback" : "text" };
  }
  if (tx.data) return state.opts.data ? { s: "‹" + tx.data + "›", kind: "data" } : null;
  return null;
}
// "text_title_disabled" ao lado de "text_title" sem curva própria: a
// variante só aparece com o item desabilitado.
function hasBaseVariant(o) {
  const base = o.name.replace(/_?(disabled|locked|dis|inactive|off)$/, "");
  if (base === o.name || !o.parent) return false;
  const q = [o.parent];
  while (q.length) {
    const x = q.shift();
    if (x !== o && x.name === base) return true;
    if (q.length < 200) q.push(...x.kids);
  }
  return false;
}
function drawText(op, frame) {
  const o = op.o, ui = op.ui;
  const tx = textOf(o);
  if (!tx || !tx.s) return;
  let alpha = param(op, "Alpha") ?? 1;
  const animated = !!(op.ov && op.ov[op.matKey]);
  let ghost = op.ghost;
  if (!ghost && /disabled|locked|_dis$|inactive|_off$/.test(o.name) && !animated && hasBaseVariant(o)) { ghost = "variant"; frame.variant.push(o); }
  if (!ghost && alpha <= 0.001 && !state.opts.static) { ghost = "alpha"; frame.alpha.push(o); }
  if (ghost) { if (!state.opts.reveal) return; alpha = 0.9; }
  const style = UD.styles[ui.sn] || UD.styles.default;
  const M = op.M;
  const sx = Math.hypot(M[0], M[1]), sy = Math.hypot(M[2], M[3]);
  const [X, Y] = toPx(M, 0, 0);
  let wr = ui.wr;
  if (wr == null || wr < 0) wr = WRAP[(style && style.wrapping) || "clampuniform"] ?? 3;
  const maxW = (typeof ui.wi === "number" && ui.wi > 0 ? ui.wi : 0) * UNIT * sx;
  const ha = ui.ha === 1 ? "right" : ui.ha === 2 ? "center" : "left";
  const dc = param(op, "DiffuseColour");
  const col = ghost ? [1, 0.25, 0.84] : tx.kind === "data" ? [1, 0.7, 0.28] : tx.kind === "missing" || tx.kind === "fallback" ? [1, 0.72, 0.28] : tx.kind === "name" ? [0.55, 0.75, 1] : (Array.isArray(dc) ? dc : [1, 1, 1]);
  ctx.save();
  ctx.font = fontFor(style, sy);
  ctx.textAlign = ha;
  ctx.textBaseline = "alphabetic";
  ctx.fillStyle = rgb(col, Math.min(1, alpha));
  if (!ghost && (param(op, "ShadowAlpha") ?? 0) > 0 && tx.kind === "text") {
    ctx.shadowColor = rgb(param(op, "ShadowColour") || [0, 0, 0], Math.min(1, param(op, "ShadowAlpha")) * 0.8);
    ctx.shadowBlur = 4;
  }
  const lineH = (+(style && style.height) || 30) * sy * 1.05;
  let lines = tx.s.split(/\r?\n/);
  if (wr === WRAP.wordwrap && maxW > 0) lines = lines.flatMap((l) => wrapLine(l, maxW));
  let y = Y, left = Infinity, right = -Infinity;
  for (const line of lines) {
    const w = ctx.measureText(line).width;
    ctx.save();
    ctx.translate(X, y);
    let k = 1;
    if (maxW > 0 && w > maxW && (wr === WRAP.clampuniform || wr === WRAP.clampwidth)) k = maxW / w;
    if (wr === WRAP.clampuniform) ctx.scale(k, k);
    else if (wr === WRAP.clampwidth) ctx.scale(k, 1);
    if (wr === WRAP.truncate && maxW > 0) {
      ctx.beginPath();
      const x0 = ha === "left" ? 0 : ha === "center" ? -maxW / 2 : -maxW;
      ctx.rect(x0, -lineH * 1.2, maxW, lineH * 1.6);
      ctx.clip();
    }
    ctx.fillText(line, 0, 0);
    ctx.restore();
    const ww = Math.min(w * k, maxW || w * k);
    const x0 = ha === "left" ? X : ha === "center" ? X - ww / 2 : X - ww;
    left = Math.min(left, x0); right = Math.max(right, x0 + ww);
    y += lineH;
  }
  ctx.restore();
  const box = [[left, Y - lineH * 0.8], [right, y - lineH + lineH * 0.2]];
  if (ghost) drawGhostHull(box);
  else extendBox(frame.boxes, o, box);
}
function wrapLine(line, maxW) {
  const words = line.split(" "), out = [];
  let cur = "";
  for (const w of words) {
    const tt = cur ? cur + " " + w : w;
    if (cur && ctx.measureText(tt).width > maxW) { out.push(cur); cur = w; } else cur = tt;
  }
  out.push(cur);
  return out;
}

// ------------------------------------------------------------ render

function extendBox(boxes, o, pts) {
  const it = ownerItem(o);
  if (it < 0) return;
  const b = boxes[it] || (boxes[it] = [Infinity, Infinity, -Infinity, -Infinity]);
  for (const [x, y] of pts) { b[0] = Math.min(b[0], x); b[1] = Math.min(b[1], y); b[2] = Math.max(b[2], x); b[3] = Math.max(b[3], y); }
}
function drawBackground() {
  if (!state.opts.bg) { ctx.fillStyle = "#000"; ctx.fillRect(0, 0, W, H); return; }
  const g = ctx.createRadialGradient(W * 0.62, H * 0.35, 80, W * 0.5, H * 0.5, W * 0.75);
  g.addColorStop(0, "#4a5260"); g.addColorStop(0.55, "#262b33"); g.addColorStop(1, "#0d0f13");
  ctx.fillStyle = g; ctx.fillRect(0, 0, W, H);
}
function drawHotButtons(screen) {
  const hb = screen.beh.filter((b) => b.t === "SBHotButtonScreenEvent" && b.a && b.a.help_text);
  if (!hb.length) return;
  const NAMES = { Back: "B", Select: "A", Button3: "X", Button4: "Y", LeftShoulder: "LB", RightShoulder: "RB", Start: "≡", Options: "≡" };
  ctx.save();
  ctx.textBaseline = "middle";
  let x = 90;
  for (const b of hb) {
    const label = (showStr(b.a.help_text) || b.a.help_text).toUpperCase();
    const key = NAMES[b.a.action] || b.a.action;
    ctx.fillStyle = "rgba(255,255,255,.9)";
    ctx.beginPath(); ctx.arc(x, 1010, 15, 0, Math.PI * 2); ctx.fill();
    ctx.fillStyle = "#111"; ctx.textAlign = "center"; ctx.font = '700 18px "Barlow Condensed", sans-serif';
    ctx.fillText(key, x, 1011);
    ctx.textAlign = "left"; ctx.fillStyle = "#fff"; ctx.font = '600 26px "Barlow Condensed", sans-serif';
    ctx.fillText(label, x + 24, 1011);
    x += 24 + ctx.measureText(label).width + 44;
  }
  ctx.restore();
}

// Busca nos dados crus (sem instanciar), atravessando os xr.
function rawKids(n) {
  const out = n.c ? [...n.c] : [];
  const ui = n.ui || {};
  const ref = ui.UINODEANIMATED || ui.UINODESCREEN || ui.UINODEXREF;
  if (ref && ref.xr && SD.scenes[ref.xr]) out.push(SD.scenes[ref.xr]);
  return out;
}
function rawFind(base, name) {
  const q = [...rawKids(base)];
  let guard = 0;
  while (q.length && guard++ < 20000) {
    const n = q.shift();
    if (n.n === name) return n;
    q.push(...rawKids(n));
  }
  return null;
}
function rawResolve(root, path) {
  let cur = root;
  for (const seg of (path || "").split(".")) { if (!seg) continue; cur = rawFind(cur, seg); if (!cur) return null; }
  return cur;
}
// O objeto de uma tela é o filho com esse nome no switch apontado pelo glyph.
// Telas de topo: screen_directory.screen_choice (fe/screen_list).
const SCREEN_LIST = SD.scenes["fe/screen_list/screen_directory"];
function childScene(sw, object) {
  if (!sw) return null;
  const child = (sw.c || []).find((k) => k.n === object);
  if (!child) return null;
  const ui = child.ui || {};
  const ref = ui.UINODESCREEN || ui.UINODEANIMATED || ui.UINODEXREF;
  return ref && ref.xr && SD.scenes[ref.xr] ? ref.xr : null;
}
const pageParentCache = new Map();
// Página embutida: a tela-mãe cuja cena tem o glyph com um filho "object".
function pageParent(s) {
  if (pageParentCache.has(s.id)) return pageParentCache.get(s.id);
  let found = null;
  const glyph = s.a.glyph || "";
  if (s.a.object && !glyph.startsWith("screen_directory.")) {
    const cands = [];
    if (s.a.data_parent_override && screenById.has(s.a.data_parent_override)) cands.push(screenById.get(s.a.data_parent_override));
    for (const p of UD.screens) if (p !== s && (p.a.glyph || "").startsWith("screen_directory.")) cands.push(p);
    for (const p of cands) {
      const scene = topScene(p);
      if (!scene) continue;
      const sw = rawResolve(SD.scenes[scene], glyph);
      if (sw && (sw.c || []).some((k) => k.n === s.a.object)) { found = p; break; }
    }
  }
  pageParentCache.set(s.id, found);
  return found;
}
function topScene(s) {
  const pick = (name) => {
    const list = scenesByLast.get(name) || [];
    return list.find((n) => n.startsWith("fe/screens/")) || list[0] || null;
  };
  if (s.a.object && (s.a.glyph || "").startsWith("screen_directory.") && SCREEN_LIST) {
    const sw = rawResolve(SCREEN_LIST, (s.a.glyph || "").split(".").slice(1).join("."));
    const viaList = childScene(sw, s.a.object);
    if (viaList) return viaList;
  }
  if (s.a.object) return pick(s.a.object);
  const g = (s.a.glyph || "").split(".")[0];
  if (s.a.layer === "overlay" && SD.scenes["fe/" + g + "_scene"]) return "fe/" + g + "_scene";
  return pick(g) || (s.a.layer === "dialog" ? pick("popup") : null);
}
function sceneForScreen(s) {
  const parent = pageParent(s);
  return parent ? topScene(parent) : topScene(s);
}

// As telas entram em fe/main_scene > centre_safeframe > screen_directory
// (-9.6, 5.4: canto superior esquerdo) > screen_choice, que pode deslocar
// cada tela. Os componentes soltos ficam no centro.
const SCREEN_DIRECTORY = [1, 0, 0, 1, -9.6, 5.4];
const choiceOffset = new Map();
if (SCREEN_LIST) {
  const walk = (n) => {
    const ui = n.ui || {};
    const ref = ui.UINODESCREEN || ui.UINODEANIMATED;
    if (ref && ref.xr && n.m) choiceOffset.set(ref.xr, n.m);
    (n.c || []).forEach(walk);
  };
  walk(SCREEN_LIST);
}
function hostFor(sceneName) {
  const off = choiceOffset.get(sceneName);
  if (off) return compose(off, SCREEN_DIRECTORY);
  if (sceneName.startsWith("fe/screens/")) return SCREEN_DIRECTORY;
  return embedPlacement(sceneName, 0) || SCREEN_DIRECTORY;
}
// Cena que não é tela (aba, bloco): usa a posição do primeiro nó de uma tela
// que a inclui por xr, somando as transformações do caminho.
const embedCache = new Map();
function embedPlacement(sceneName, depth) {
  if (embedCache.has(sceneName)) return embedCache.get(sceneName);
  embedCache.set(sceneName, null);
  let found = null;
  const search = (n, P) => {
    if (found) return;
    let m = n.m || ID;
    if (isTransition(n)) m = [m[0], m[1], m[2], m[3], 0, 0];
    const M = compose(m, P);
    const ref = (n.ui || {}).UINODEANIMATED || (n.ui || {}).UINODESCREEN;
    if (ref && ref.xr === sceneName) { found = M; return; }
    (n.c || []).forEach((k) => search(k, M));
  };
  for (const name of Object.keys(SD.scenes).filter((k) => k.startsWith("fe/screens/"))) {
    if (found) break;
    search(SD.scenes[name], ID);
    if (found) found = compose(found, hostFor(name));
  }
  if (!found && depth < 4) {
    // Incluída por um componente que por sua vez está numa tela.
    for (const [name, n] of Object.entries(SD.scenes)) {
      if (found || name.startsWith("fe/screens/")) continue;
      let local = null;
      const s2 = (x, P) => {
        if (local) return;
        const M = compose(x.m || ID, P);
        const ref = (x.ui || {}).UINODEANIMATED;
        if (ref && ref.xr === sceneName) { local = M; return; }
        (x.c || []).forEach((k) => s2(k, M));
      };
      s2(n, ID);
      if (local) { const outer = embedPlacement(name, depth + 1); if (outer) found = compose(local, outer); }
    }
  }
  embedCache.set(sceneName, found);
  return found;
}

function switchesOf(root) {
  const out = [];
  const walk = (o) => {
    if (o.hidden) return;
    if (isSwitch(o) && o.kids.length >= 2) out.push(o);
    switchKids(o).forEach(walk);
  };
  if (root) walk(root);
  return out;
}
function applySwitchOverrides(root) {
  for (const o of switchesOf(root)) if (state.switchSel.has(o.path)) o.sel = state.switchSel.get(o.path);
}

function build() {
  seq = 0;
  let c = null;
  if (state.mode === "screens") {
    const s = screenById.get(state.screen);
    if (!s) return null;
    const parent = pageParent(s);
    const sceneName = sceneForScreen(s);
    const root = sceneName ? makeInst(SD.scenes[sceneName], 0, "") : null;
    let report = [], pageRoot = root;
    if (root && parent) {
      // A tela-mãe é desenhada com esta página encaixada no switch.
      bindScreen(root, parent, true);
      const sw = resolve(root, s.a.glyph);
      if (sw) { sw.sel = s.a.object; pageRoot = sw.kids.find((k) => k.name === s.a.object) || sw; }
    }
    if (root) report = bindScreen(pageRoot, s, false);
    c = { screen: s, parent, sceneName, root, report, host: root ? hostFor(sceneName) : null };
  } else if (state.mode === "components") {
    const name = state.component;
    if (!name || !SD.scenes[name]) return null;
    c = { sceneName: name, root: makeInst(SD.scenes[name], 0, ""), report: [],
      host: name.startsWith("fe/screens/") || name.startsWith("fe/component/") ? hostFor(name) : null };
  } else return null;
  if (c.root) applySwitchOverrides(c.root);
  c.animInst = animInstances(c.root);
  c.switches = switchesOf(c.root);
  return c;
}

let pending = false;
function scheduleRender() {
  if (pending) return;
  pending = true;
  requestAnimationFrame(() => { pending = false; render(); });
}
function render() {
  if (state.mode !== "screens" && state.mode !== "components") return;
  ctx.setTransform(1, 0, 0, 1, 0, 0);
  drawBackground();
  const c = current;
  if (!c || !c.root) {
    ctx.fillStyle = "#8b93a3"; ctx.font = '600 40px "Barlow Condensed", sans-serif'; ctx.textAlign = "center";
    ctx.fillText(c && c.screen ? t("status.noscene") : t("status.pick"), W / 2, H / 2);
    if (c && c.screen) drawHotButtons(c.screen);
    status();
    return;
  }
  const { ops, hidden } = collect(c.root, c.host);
  const frame = { boxes: [], alpha: [], variant: [], textures: new Set() };
  for (const op of ops) (op.kind === "mesh" ? drawMesh : drawText)(op, frame);
  c.boxes = frame.boxes;
  c.frame = frame;
  c.hidden = hidden;
  c.nops = ops.length;
  if (c.screen) drawHotButtons(c.screen);
  if (state.opts.boxes || state.focus >= 0) {
    frame.boxes.forEach((b, i) => {
      if (!b || !(state.opts.boxes || i === state.focus)) return;
      ctx.save();
      ctx.strokeStyle = i === state.focus ? "#ff3b5c" : "rgba(120,200,255,.7)";
      ctx.lineWidth = i === state.focus ? 3 : 1.5;
      ctx.strokeRect(b[0], b[1], b[2] - b[0], b[3] - b[1]);
      ctx.fillStyle = ctx.strokeStyle; ctx.font = "16px ui-monospace, monospace"; ctx.textAlign = "left";
      ctx.fillText(c.screen ? c.screen.items[i].a.id : "", b[0] + 2, b[1] - 4);
      ctx.restore();
    });
  }
  status();
}
function hiddenCount(c) {
  if (!c || !c.frame) return 0;
  return (c.hidden || []).length + new Set(c.frame.alpha).size + c.frame.variant.length;
}
function status() {
  const el = document.getElementById("status");
  const c = current;
  if (!c) { el.textContent = ""; return; }
  const bits = [];
  if (c.sceneName) bits.push(t("status.scene") + ": " + c.sceneName);
  if (c.nops != null) bits.push(c.nops + " " + t("status.elements"));
  const hc = hiddenCount(c);
  if (hc) bits.push(hc + " " + t("status.hidden"));
  const bad = (c.report || []).filter((r) => r.problems.length).length;
  const warn = (c.report || []).filter((r) => r.warnings && r.warnings.length).length;
  if (bad) bits.push(bad + " " + t("status.errors"));
  if (warn) bits.push(warn + " " + t("status.warnings"));
  el.textContent = bits.join("  ·  ");
}

// ------------------------------------------------------------ barra de animação

function updateAnimBar() {
  const bar = document.getElementById("animbar");
  const evEl = document.getElementById("events");
  const c = current;
  const list = (c && c.animInst) || [];
  const scope = state.anim.scope;
  const targets = animTargets(scope);
  const counts = new Map();
  for (const o of targets) for (const name of Object.keys(o.src.anim.ev || {})) counts.set(name, (counts.get(name) || 0) + 1);
  const names = [...counts.keys()].sort((a, b) => {
    const ia = EV_ORDER.indexOf(a), ib = EV_ORDER.indexOf(b);
    return (ia < 0 ? 99 : ia) - (ib < 0 ? 99 : ib) || a.localeCompare(b);
  });
  bar.classList.toggle("empty", !list.length);
  const playing = state.anim.play && state.anim.play.name;
  const sig = names.join(",") + "|" + uiLang() + "|" + playing + "|" + scope + "|" + state.focus;
  if (evEl.dataset.sig !== sig) {
    evEl.dataset.sig = sig;
    const empty = !list.length ? t("anim.none")
      : names.length ? ""
      : scope === "item" && state.focus < 0 ? t("anim.needfocus") : t("anim.itemnone");
    evEl.innerHTML = empty
      ? `<span class="muted">${esc(empty)}</span>`
      : names.map((n) => `<button data-ev="${esc(n)}" class="${n === playing ? "on" : ""}" title="${esc(n)}">${esc(eventLabel(n))}<small>${counts.get(n)}</small></button>`).join("");
    evEl.querySelectorAll("button").forEach((b) => b.addEventListener("click", () => playEvent(b.dataset.ev, state.anim.scope)));
  }
  let hi = 0;
  for (const o of list) hi = Math.max(hi, animSpan(o.src.anim)[1]);
  const scrub = document.getElementById("anim-scrub");
  scrub.max = Math.max(0.5, hi).toFixed(2);
  const out = document.getElementById("anim-frame");
  let shown = state.anim.scrub;
  if (shown == null && state.anim.play && state.anim.play.tracks.length) shown = state.anim.frames.get(state.anim.play.tracks[0].path);
  if (shown == null && list.length) shown = frameOf(list[0]);
  out.textContent = shown == null ? "—" : shown.toFixed(2);
  if (state.anim.scrub == null && shown != null && document.activeElement !== scrub) scrub.value = shown;
  document.getElementById("anim-release").hidden = state.anim.scrub == null;
}

// ------------------------------------------------------------ inspetor

function stateLabel(sid) {
  const st = UD.states[sid];
  if (!st) return esc(sid);
  const extra = Object.entries(st.a).filter(([k]) => k !== "screen_name").map(([k, v]) => `${k}=${v}`).join(" ");
  const scr = st.a.screen_name && screenById.has(st.a.screen_name)
    ? ` <a href="#s=${encodeURIComponent(st.a.screen_name)}">${esc(st.a.screen_name)}</a>` : (st.a.screen_name ? " " + esc(st.a.screen_name) : "");
  return `<span class="cls">${esc(st.c)}</span>${scr}${extra ? ` <span class="muted">[${esc(extra)}]</span>` : ""}`;
}
function nodeLabel(nid) { const n = UD.flow[nid]; return n ? stateLabel(n.s) : esc(nid); }
function describeBeh(b) {
  const tpl = (BEH_DESC[uiLang()] || BEH_DESC.pt)[b.t];
  if (!tpl) return null;
  const a = b.a || {};
  return tpl.replace(/\{(\w+)(!|\?)?\}/g, (_, k, mod) => {
    const v = a[k];
    if (mod === "?") return a.visible === "false" ? (uiLang() === "en" ? "hidden" : "oculto") : (uiLang() === "en" ? "visible" : "visível");
    if (v == null) return "";
    if (mod === "!") return v.split(";").map((x) => cleanMarkup(showStr(x) ?? x)).join(", ");
    return v;
  });
}
function behLine(b) {
  const desc = describeBeh(b);
  const attrs = Object.entries(b.a || {}).map(([k, v]) => {
    const tr = /^(lng_|db_)/.test(v) ? showStr(v) : null;
    return `${esc(k)}=<span class="s">"${esc(v)}"</span>${tr != null ? ` <span class="muted">(${esc(cleanMarkup(tr).slice(0, 60))})</span>` : ""}`;
  }).join(" ");
  const text = b.x ? ` <span class="muted">${esc(b.x.trim().replace(/\s+/g, " ").slice(0, 160))}</span>` : "";
  const thumb = b.t === "BTextureStatic" && assetByName.get(b.a.texture)
    ? `<img class="mini" src="${esc(assetByName.get(b.a.texture).t)}" alt="">` : "";
  return `<div class="beh">${thumb}${desc ? `<div class="desc">${esc(desc)}</div>` : ""}<div class="raw"><b>${esc(b.t)}</b> ${attrs}${text}</div></div>`;
}
function toXml(s) {
  const at = (a) => Object.entries(a || {}).map(([k, v]) => ` ${k}="${v}"`).join("");
  const node = (b, ind) => `${ind}<${b.t}${at(b.a)}${b.x ? `>${b.x.trim()}</${b.t}>` : "/>"}`;
  const out = [`<Screen${at(s.a)}>`, "  <items>"];
  for (const it of s.items) {
    out.push(`    <Item${at(it.a)}>`);
    for (const b of it.b) out.push(node(b, "      "));
    out.push("    </Item>");
  }
  out.push("  </items>", "  <behaviours>");
  for (const b of s.beh) out.push(node(b, "    "));
  out.push("  </behaviours>", "</Screen>");
  return out.join("\n");
}
const section = (key, body, open = true, extra = "") =>
  `<details class="sec" ${open ? "open" : ""} data-sec="${key}"><summary>${esc(t(key))}${extra}</summary><div class="sec-body">${body}</div></details>`;
const openSecs = new Map();
function rememberSections(el) {
  el.querySelectorAll("details.sec").forEach((d) => openSecs.set(d.dataset.sec, d.open));
}
function restoreSections(el) {
  el.querySelectorAll("details.sec").forEach((d) => { if (openSecs.has(d.dataset.sec)) d.open = openSecs.get(d.dataset.sec); });
}

function inspect() {
  const el = document.getElementById("inspect");
  rememberSections(el);
  const c = current;
  if (!c) { el.innerHTML = `<p class="muted">${esc(t("nothing.selected"))}</p>`; return; }
  const h = [];
  if (c.screen) {
    const s = c.screen, cls = screenClass(s);
    const title = screenTitle(s);
    h.push(`<h2>${esc(s.id)}</h2>`);
    if (title) h.push(`<div class="subtitle">${esc(cleanMarkup(title))}</div>`);
    h.push(section("sec.screen", `<div class="kv">
      <div>${t("kv.status")}</div><div><span class="badge b-${cls}">${esc(t("cls." + cls))}</span></div>
      <div>${t("kv.object")}</div><div class="mono">${esc(s.a.object || "—")}</div>
      ${c.parent ? `<div>${t("kv.inside")}</div><div class="mono"><a href="#s=${encodeURIComponent(c.parent.id)}">${esc(c.parent.id)}</a> (${esc(s.a.glyph)})</div>` : ""}
      <div>${t("kv.scene")}</div><div class="mono"><a href="#c=${encodeURIComponent(c.sceneName || "")}">${esc(c.sceneName || "—")}</a></div>
      <div>${t("kv.glyph")}</div><div class="mono">${esc(s.a.glyph || "—")}</div>
      <div>${t("kv.layer")}</div><div>${esc(s.a.layer || "screen")}</div>
      <div>${t("kv.data")}</div><div class="mono">ui.${esc(s.a.data_parent_override || s.id)}.*</div></div>`));
    h.push(section("sec.reach", reachHtml(s, cls)));
    h.push(section("sec.items", itemsHtml(c), true, ` <span class="count">${s.items.length}</span>`));
  } else {
    h.push(`<h2>${esc(c.sceneName)}</h2>`);
    h.push(section("sec.used", usedByHtml(c)));
  }
  h.push(section("sec.anim", animHtml(c), true, ` <span class="count">${c.animInst.length}</span>`));
  h.push(`<div id="hidden-sec"></div>`);
  h.push(section("sec.variants", variantsHtml(c), false, ` <span class="count">${c.switches.length}</span>`));
  h.push(`<div id="images-sec"></div>`);
  if (c.screen) {
    h.push(section("sec.beh", c.screen.beh.map(behLine).join(""), false, ` <span class="count">${c.screen.beh.length}</span>`));
    h.push(section("sec.source", `<pre class="xml">${esc(toXml(c.screen))}</pre>`, false));
  } else {
    h.push(section("sec.tree", treeHtml(c), false));
  }
  el.innerHTML = h.join("");
  restoreSections(el);
  wireInspector(el);
  inspectHidden();
}
function reachHtml(s, cls) {
  const h = [];
  const sts = statesByScreen.get(s.id) || [];
  if (!sts.length) h.push(`<p class="${cls === "orphan" ? "bad" : "muted"}">${esc(t("reach.none") + (cls !== "state" ? t("reach." + cls) : ""))}.</p>`);
  for (const sid of sts) {
    const nodes = flowByState.get(sid) || [];
    h.push(`<div class="link">${stateLabel(sid)}${nodes.length ? "" : ` <span class="bad">${t("reach.outside")}</span>`}</div>`);
    for (const nid of nodes.slice(0, 4)) {
      const inc = incoming.get(nid) || [];
      const out = UD.flow[nid].l.map((l) => l.id);
      h.push(`<div class="link muted small">${t("reach.node", { id: nid, parent: "" })}${nodeLabel(UD.flow[nid].p)}</div>`);
      for (const l of inc.slice(0, 6)) h.push(`<div class="link">← <span class="ev">${esc(l.ev)}</span> · ${nodeLabel(l.from)}</div>`);
      if (inc.length > 6) h.push(`<div class="link muted">${t("reach.more", { n: inc.length - 6 })}</div>`);
      if (out.length) h.push(`<div class="link">→ ${out.map((e) => `<span class="ev">${esc(e)}</span>`).join(", ")}</div>`);
    }
    if (nodes.length > 4) h.push(`<div class="link muted">${t("reach.more", { n: nodes.length - 4 })}</div>`);
  }
  return h.join("");
}
function itemsHtml(c) {
  const s = c.screen;
  return s.items.map((it, i) => {
    const r = c.report[i] || { problems: [], warnings: [] };
    const sw = it.b.find((b) => b.t === "BSwitchStatic");
    const txt = it.b.filter((b) => b.t === "BTextStatic").map((b) => showStr(b.a.string) ?? `[${t("missing")}: ${b.a.string}]`);
    const data = it.b.filter((b) => /^BText(Data|Tokens)$/.test(b.t)).map((b) => b.a.data_path);
    const ev = it.b.filter((b) => b.a && b.a.select_value).map((b) => b.a.select_value);
    const cond = it.b.filter((b) => b.t === "BVisibilityControlData").map((b) => b.a.data_path);
    const tex = it.b.filter((b) => b.t === "BTextureStatic" && assetByName.get(b.a.texture)).map((b) => assetByName.get(b.a.texture));
    return `<div class="item${i === state.focus ? " on" : ""}" data-item="${i}">
      <div class="t">${tex.map((a) => `<img class="mini" src="${esc(a.t)}" alt="">`).join("")}${esc(it.a.id)} ${sw ? `<span class="muted">· ${esc(sw.a.object)}</span>` : ""}</div>
      ${txt.length ? `<div class="txt"${txt.some((x) => String(x).startsWith("†")) ? ` title="${esc(t("txt.fallback"))}"` : ""}>${txt.map((x) => esc(cleanMarkup(x))).join(" · ")}</div>` : ""}
      ${data.length ? `<div class="mono data">${data.map(esc).join(", ")}</div>` : ""}
      ${ev.length ? `<div class="small">${t("item.event")}: <span class="mono ev">${ev.map(esc).join(", ")}</span></div>` : ""}
      ${cond.length ? `<div class="warn small">${t("item.visibleIf")} ${cond.map(esc).join(", ")}</div>` : ""}
      ${r.problems.map((p) => `<div class="bad small">${esc(p)}</div>`).join("")}
      ${(r.warnings || []).map((p) => `<div class="warn small" title="${esc(t("item.warn.tip"))}">⚠ ${esc(p)}</div>`).join("")}
      <details><summary>${t("item.beh", { n: it.b.length })}</summary><div class="g mono">${esc(it.a.glyph || "")}</div>${it.b.map(behLine).join("")}</details>
    </div>`;
  }).join("");
}
function usedByHtml(c) {
  const users = UD.screens.filter((s) => sceneForScreen(s) === c.sceneName).map((s) => s.id);
  const xrefBy = new Set();
  const walk = (n, root) => {
    const ui = n.ui || {};
    for (const k of ["UINODEANIMATED", "UINODESCREEN", "UINODEXREF"]) if (ui[k] && ui[k].xr === c.sceneName) xrefBy.add(root);
    (n.c || []).forEach((k) => walk(k, root));
  };
  for (const [name, n] of Object.entries(SD.scenes)) walk(n, name);
  return (users.length ? `<div>${t("comp.users")}: ${users.map((u) => `<a class="mono" href="#s=${encodeURIComponent(u)}">${esc(u)}</a>`).join(", ")}</div>` : `<p class="muted">${t("comp.nouse")}</p>`)
    + (xrefBy.size ? `<div>${t("comp.included")}: ${[...xrefBy].slice(0, 40).map((u) => `<a class="mono" href="#c=${encodeURIComponent(u)}">${esc(u)}</a>`).join(", ")}</div>` : "");
}
function treeHtml(c) {
  const tree = [];
  const dump = (o, d) => {
    if (d > 7 || tree.length > 400) return;
    const ui = Object.entries(o.ui).map(([k, v]) => `${k.replace(/^UI(NODE|MOD)/, "")}${Object.keys(v).length ? JSON.stringify(v) : ""}`).join(" ");
    tree.push(`<div class="beh" style="padding-left:${d * 12}px"><b>${esc(o.name)}</b> ${o.draw.length ? '<span class="s">▣</span>' : ""} <span class="muted">${esc(ui)}</span></div>`);
    if (!o.xref) o.kids.forEach((k) => dump(k, d + 1));
    else tree.push(`<div class="beh muted" style="padding-left:${(d + 1) * 12}px">→ <a href="#c=${encodeURIComponent(o.xref)}">${esc(o.xref)}</a></div>`);
  };
  dump(c.root, 0);
  return tree.join("");
}
// Curva de um canal como SVG: alfa em uma linha, cor em três (R, G, B).
function sparkline(ch, ev) {
  const [lo, hi] = [ch[3].length ? ch[3][0][0] : 0, ch[4][0]];
  const span = Math.max(0.01, hi - lo);
  const Wd = 220, Hd = 44, N = 48;
  const series = [];
  for (let i = 0; i <= N; i++) series.push(evalChannel(ch, lo + (span * i) / N));
  const comps = ch[2] === 1 ? 1 : 3;
  let vmin = 0, vmax = 1;
  for (const v of series) for (let j = 0; j < comps; j++) { vmin = Math.min(vmin, v[j]); vmax = Math.max(vmax, v[j]); }
  const y = (v) => Hd - 4 - ((v - vmin) / (vmax - vmin || 1)) * (Hd - 8);
  const colors = comps === 1 ? ["#f4f5f7"] : ["#ff6b6b", "#4fc38a", "#6aa8ff"];
  const lines = [];
  for (let j = 0; j < comps; j++) lines.push(`<polyline fill="none" stroke="${colors[j]}" stroke-width="1.6" points="${series.map((v, i) => `${(i / N) * Wd},${y(v[j]).toFixed(1)}`).join(" ")}"/>`);
  const ticks = Object.entries(ev || {}).filter(([, f]) => f >= lo && f <= hi).map(([name, f]) => {
    const x = ((f - lo) / span) * Wd;
    return `<line x1="${x}" x2="${x}" y1="0" y2="${Hd}" stroke="#ff3b5c" stroke-opacity=".45"/><title>${esc(name)} ${f}</title>`;
  });
  return `<svg class="spark" viewBox="0 0 ${Wd} ${Hd}" preserveAspectRatio="none">${ticks.join("")}${lines.join("")}</svg>`;
}
function animHtml(c) {
  if (!c.animInst.length) return `<p class="muted">${esc(t("anim.none"))}</p>`;
  const groups = new Map();
  for (const o of c.animInst) {
    const key = o.src.n;
    if (!groups.has(key)) groups.set(key, { o, n: 0 });
    groups.get(key).n++;
  }
  return [...groups.entries()].map(([name, g]) => {
    const anim = g.o.src.anim;
    const evs = Object.entries(anim.ev || {}).sort((a, b) => a[1] - b[1]);
    return `<div class="anim">
      <div class="t"><span class="mono">${esc(name.split("/").pop())}</span>${g.n > 1 ? ` <span class="muted">×${g.n}</span>` : ""}</div>
      <div class="evs">${evs.map(([e, f]) => `<button data-play="${esc(e)}" data-root="${esc(name)}" title="${esc(t("anim.play"))} · ${esc(e)}">▶ ${esc(eventLabel(e))} <small>${f}</small></button>`).join("")}</div>
      ${anim.ch.length ? `<details class="chs" data-anim="${esc(name)}"><summary>${tCount("anim.channels", anim.ch.length)}</summary></details>` : ""}
    </div>`;
  }).join("");
}
function variantsHtml(c) {
  if (!c.switches.length) return `<p class="muted">${esc(t("variants.none"))}</p>`;
  return `<p class="muted small">${esc(t("variants.tip"))}</p>` + c.switches.slice(0, 120).map((o) => {
    const it = ownerItem(o);
    const label = it >= 0 && c.screen ? c.screen.items[it].a.id : (o.parent ? o.parent.name : o.name);
    const cur = o.sel ?? (switchKids(o)[0] || {}).name;
    return `<div class="variant"><span class="mono">${esc(label)}</span>
      <select data-switch="${esc(o.path)}">${o.kids.map((k) => `<option ${k.name === cur ? "selected" : ""}>${esc(k.name)}</option>`).join("")}</select></div>`;
  }).join("");
}
// Seções que dependem do quadro desenhado: ocultos e imagens.
function inspectHidden() {
  const c = current;
  const hs = document.getElementById("hidden-sec");
  const is = document.getElementById("images-sec");
  if (!c || !hs || !c.frame) return;
  rememberSections(document.getElementById("inspect"));
  const groups = { alpha: [...new Set(c.frame.alpha)], variant: c.frame.variant, switch: [], beh: [], slot: [], cond: [], scroll: [] };
  for (const h of c.hidden || []) (groups[h.why] || (groups[h.why] = [])).push(h.o);
  const total = hiddenCount(c);
  const label = (o) => {
    const it = ownerItem(o);
    const owner = it >= 0 && c.screen ? c.screen.items[it].a.id + " › " : "";
    return owner + o.name;
  };
  const body = total ? Object.entries(groups).filter(([, l]) => l.length).map(([why, l]) =>
    `<div class="hgroup"><div class="hwhy">${esc(t("hidden." + why))} <span class="count">${l.length}</span></div>
     <div class="hlist">${l.slice(0, 80).map((o) => `<span class="mono">${esc(label(o))}</span>`).join("")}${l.length > 80 ? "…" : ""}</div></div>`).join("")
    : `<p class="muted">${esc(t("hidden.none"))}</p>`;
  hs.innerHTML = section("sec.hidden", body, true, ` <span class="count">${total}</span>`);
  const names = [...c.frame.textures];
  if (c.screen) for (const it of c.screen.items) for (const b of it.b) if (b.t === "BTextureStatic") names.push(b.a.texture);
  const uniq = [...new Set(names)].filter((n) => assetByName.has(n));
  is.innerHTML = section("sec.images", uniq.length ? `<div class="thumbs">${uniq.map((n) => {
    const a = assetByName.get(n);
    return `<a href="#i=${encodeURIComponent(n)}" title="${esc(n)} · ${a.w}×${a.h}"><img src="${esc(a.t)}" loading="lazy" alt=""><span>${esc(n)}</span></a>`;
  }).join("")}</div>` : `<p class="muted">—</p>`, false, ` <span class="count">${uniq.length}</span>`);
  restoreSections(document.getElementById("inspect"));
}
function wireInspector(el) {
  el.querySelectorAll(".item").forEach((d) => d.addEventListener("click", (e) => {
    if (e.target.closest("details") || e.target.closest("a")) return;
    setFocus(+d.dataset.item === state.focus ? -1 : +d.dataset.item);
  }));
  // As curvas só são desenhadas quando a lista abre.
  el.querySelectorAll("details.chs").forEach((d) => d.addEventListener("toggle", () => {
    if (!d.open || d.dataset.done) return;
    d.dataset.done = "1";
    const anim = SD.scenes[d.dataset.anim].anim;
    d.insertAdjacentHTML("beforeend", anim.ch.map((ch) => `<div class="ch"><span class="mono">${esc(ch[0].split(":").slice(1).join(":"))} · <b>${esc(ch[1])}</b></span>${sparkline(ch, anim.ev)}</div>`).join(""));
  }));
  el.querySelectorAll("[data-play]").forEach((b) => b.addEventListener("click", () => playEvent(b.dataset.play, "screen", b.dataset.root)));
  el.querySelectorAll("[data-switch]").forEach((s) => s.addEventListener("change", () => {
    state.switchSel.set(s.dataset.switch, s.value);
    refresh();
  }));
}

// ------------------------------------------------------------ abas Telas e Cenas

function screenSearchText(s) {
  const parts = [s.id, s.a.object || "", screenTitle(s) || ""];
  for (const it of s.items) for (const b of it.b) if (b.t === "BTextStatic") parts.push(str(b.a.string) || "");
  return parts.join(" ").toLowerCase();
}
const searchCache = new Map();
MODES.screens = {
  stage: true,
  filters() {
    const count = (c) => UD.screens.filter((s) => c === "all" || screenClass(s) === c).length;
    return ["all", "state", "ref", "code", "orphan"].map((k) => [k, `${t("filter." + k)} (${count(k)})`]);
  },
  list(q) {
    const rows = [];
    for (const s of UD.screens) {
      const cls = screenClass(s);
      if (state.filter !== "all" && cls !== state.filter) continue;
      const key = state.lang + s.id;
      if (!searchCache.has(key)) searchCache.set(key, screenSearchText(s));
      if (q && !searchCache.get(key).includes(q)) continue;
      const title = screenTitle(s);
      rows.push(`<div class="row${s.id === state.screen ? " on" : ""}" data-id="${esc(s.id)}">
        <div class="id">${esc(s.id)}<span class="badge b-${cls}">${esc(t("cls." + cls))}</span></div>
        <div class="sub">${esc(title ? cleanMarkup(title) : s.a.object || s.a.glyph || "")}</div></div>`);
    }
    return rows;
  },
  current: () => state.screen,
  select(id) { state.screen = id; },
  hash: "s",
};
MODES.components = {
  stage: true,
  filters() {
    const names = Object.keys(SD.scenes);
    const count = (p) => names.filter((n) => p === "all" || n.startsWith(p)).length;
    return [["all", t("filter.all")], ["fe/screens/", t("filter.screens")], ["fe/component/", t("filter.components")], ["osd/", t("filter.hud")], ["loading", t("filter.loading")]]
      .map(([k, l]) => [k, `${l} (${count(k)})`]);
  },
  list(q) {
    const rows = [];
    for (const name of Object.keys(SD.scenes).sort()) {
      if (state.filter !== "all" && !name.startsWith(state.filter)) continue;
      if (q && !name.toLowerCase().includes(q)) continue;
      const anim = SD.scenes[name].anim;
      rows.push(`<div class="row${name === state.component ? " on" : ""}" data-id="${esc(name)}"><div class="id">${esc(name)}${anim && anim.ch.length ? ' <span class="badge b-anim">anim</span>' : ""}</div></div>`);
    }
    return rows;
  },
  current: () => state.component,
  select(id) { state.component = id; },
  hash: "c",
};

// ------------------------------------------------------------ navegação

function renderList() {
  const list = document.getElementById("list");
  const mode = MODES[state.mode];
  const rows = mode.list(state.search.trim().toLowerCase());
  list.innerHTML = rows.join("") || `<p class="muted" style="padding:12px">${esc(t("nothing"))}</p>`;
  list.querySelectorAll(".row").forEach((r) => r.addEventListener("click", () => select(r.dataset.id)));
  const on = list.querySelector(".row.on");
  if (on) on.scrollIntoView({ block: "nearest" });
}
function renderFilters() {
  const el = document.getElementById("filters");
  const defs = MODES[state.mode].filters();
  el.innerHTML = defs.map(([k, l]) => `<button data-f="${esc(k)}" class="${state.filter === k ? "on" : ""}">${esc(l)}</button>`).join("");
  el.querySelectorAll("button").forEach((b) => b.addEventListener("click", () => { state.filter = b.dataset.f; renderFilters(); renderList(); if (MODES[state.mode].onFilter) MODES[state.mode].onFilter(); }));
}
function refresh() {
  tip.hidden = true;
  const mode = MODES[state.mode];
  document.getElementById("stage").hidden = !mode.stage;
  document.getElementById("content").hidden = !!mode.stage;
  if (mode.stage) {
    current = build();
    render();
    inspect();
    updateAnimBar();
  } else {
    current = null;
    mode.show();
  }
}
function select(id) {
  const mode = MODES[state.mode];
  if (mode.stage) { state.focus = -1; resetAnim(); state.switchSel.clear(); }
  mode.select(id);
  const want = mode.hash + "=" + encodeURIComponent(id);
  if (location.hash.slice(1) !== want) history.replaceState(null, "", "#" + want);
  renderList();
  refresh();
}
function setFocus(i) {
  state.focus = i;
  state.anim.frames.clear();
  render();
  inspect();
  updateAnimBar();
}
function writeHash() {
  const mode = MODES[state.mode];
  if (!mode) return;
  const id = mode.current();
  const hash = id ? mode.hash + "=" + encodeURIComponent(id) : mode.hash;
  if (location.hash.slice(1) !== hash) history.replaceState(null, "", "#" + hash);
}
function setMode(mode) {
  state.mode = mode;
  state.filter = (MODES[mode] && MODES[mode].defaultFilter) || "all";
  document.querySelectorAll(".modes button").forEach((b) => b.classList.toggle("on", b.dataset.mode === mode));
  const search = document.getElementById("search");
  search.placeholder = t("search." + mode);
  search.value = state.search = "";
  renderFilters();
  renderList();
  refresh();
  writeHash();
}
function applyLanguage() {
  applyI18n();
  document.getElementById("search").placeholder = t("search." + state.mode);
  searchCache.clear();
  renderFilters();
  renderList();
  refresh();
}

document.querySelectorAll(".modes button").forEach((b) => b.addEventListener("click", () => setMode(b.dataset.mode)));
document.getElementById("search").addEventListener("input", (e) => { state.search = e.target.value; renderList(); if (MODES[state.mode].onSearch) MODES[state.mode].onSearch(); });
document.getElementById("lang").addEventListener("change", (e) => { state.lang = e.target.value; applyLanguage(); });
for (const k of Object.keys(state.opts)) {
  const el = document.getElementById("o-" + k);
  if (!el) continue;
  el.checked = state.opts[k];
  el.addEventListener("change", () => { state.opts[k] = el.checked; refresh(); });
}
document.getElementById("anim-scope").addEventListener("change", (e) => { state.anim.scope = e.target.value; updateAnimBar(); });
document.getElementById("anim-speed").addEventListener("change", (e) => { state.anim.speed = +e.target.value; });
document.getElementById("anim-reset").addEventListener("click", () => { resetAnim(); render(); updateAnimBar(); inspectHidden(); });
document.getElementById("anim-scrub").addEventListener("input", (e) => {
  state.anim.play = null;
  state.anim.scrub = +e.target.value;
  render(); updateAnimBar(); inspectHidden();
});
document.getElementById("anim-release").addEventListener("click", () => { state.anim.scrub = null; render(); updateAnimBar(); inspectHidden(); });

function canvasPoint(e) {
  const r = canvas.getBoundingClientRect();
  return [(e.clientX - r.left) * (W / r.width), (e.clientY - r.top) * (H / r.height)];
}
function itemAt(x, y) {
  const boxes = (current && current.boxes) || [];
  let best = -1, area = Infinity;
  boxes.forEach((b, i) => {
    if (!b || x < b[0] || x > b[2] || y < b[1] || y > b[3]) return;
    const a = (b[2] - b[0]) * (b[3] - b[1]);
    if (a < area) { area = a; best = i; }
  });
  return best;
}
const tip = document.getElementById("tip");
canvas.addEventListener("mousemove", (e) => {
  const [x, y] = canvasPoint(e);
  const i = itemAt(x, y);
  if (i < 0 || !current || !current.screen) { tip.hidden = true; canvas.style.cursor = ""; return; }
  const it = current.screen.items[i];
  tip.hidden = false;
  tip.textContent = `${it.a.id}  ${it.a.glyph || ""}`;
  const wr = document.getElementById("wrap").getBoundingClientRect();
  tip.style.left = e.clientX - wr.left + 14 + "px";
  tip.style.top = e.clientY - wr.top + 14 + "px";
  canvas.style.cursor = "pointer";
});
canvas.addEventListener("mouseleave", () => { tip.hidden = true; });
canvas.addEventListener("click", (e) => {
  const [x, y] = canvasPoint(e);
  const i = itemAt(x, y);
  setFocus(i === state.focus ? -1 : i);
  const row = document.querySelector(`.item[data-item="${i}"]`);
  if (row) row.scrollIntoView({ block: "nearest" });
});
window.addEventListener("keydown", (e) => {
  if (/INPUT|SELECT|TEXTAREA/.test(e.target.tagName)) return;
  if (!MODES[state.mode].stage || !current || !current.screen || !current.screen.items.length) return;
  const n = current.screen.items.length;
  if (e.key === "ArrowDown" || e.key === "ArrowRight") { setFocus((state.focus + 1) % n); e.preventDefault(); }
  if (e.key === "ArrowUp" || e.key === "ArrowLeft") { setFocus((state.focus - 1 + n) % n); e.preventDefault(); }
  if (e.key === "Escape") setFocus(-1);
});

function fromHash() {
  const m = /^#([a-z])(?:=(.*))?$/.exec(location.hash);
  if (!m) return false;
  const id = m[2] ? decodeURIComponent(m[2]) : "";
  for (const [name, mode] of Object.entries(MODES)) {
    if (mode.hash === m[1]) { state.mode = name; if (id) mode.select(id); return true; }
  }
  return false;
}
window.addEventListener("hashchange", () => {
  const before = state.mode;
  if (!fromHash()) return;
  state.focus = -1; resetAnim(); state.switchSel.clear();
  if (before !== state.mode) setMode(state.mode);
  else { renderList(); refresh(); }
});

// content.js chama start() depois de registrar as outras abas.
function start() {
  if (!fromHash()) state.screen = "pause_menu";
  applyI18n();
  document.getElementById("lang").value = state.lang;
  const mode = state.mode;
  setMode(mode);
  if (document.fonts) document.fonts.ready.then(() => render());
}
