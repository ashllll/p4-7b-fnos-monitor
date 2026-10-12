#!/usr/bin/env node
/* 滚动条侵扰审计（回归看门狗）
 *
 * 背景：列表卡片右缘是"右对齐数值/状态"所在处，LVGL 的纵向滚动条贴在滚动区右缘
 * （列表自己 pad_right 之内），实测会与右对齐文字只留 0~1px，看起来就是"文字和竖条重叠"。
 * 本脚本在主机预览 PNG 上量化这个间隙。
 *
 * 判据：内容最右像素 → 滚动条左缘 >= 8px，且 滚动条右缘 → 卡片内缘 >= 4px。
 *
 * ── 判定依据（审计 H14 的整改点）────────────────────────────────────────
 * 旧版只用像素启发式找"中性灰竖条"：
 *   · 找不到就记"未判定"，但**分不清**"内容不溢出（LVGL 本就不画）"和"检测失灵"；
 *   · 未判定不阻塞总闸门，215 处未判定被当成噪声；
 *   · 更重要的是它会**认错**：卡片右缘那条 4px 灰边被当成滚动条，报出 18 处
 *     "条→内缘=3px"的假不合格（内容右缘离它 200 多像素，根本不挨着）。
 * 现在几何与"该不该有条"一律以**渲染它的那个二进制自己吐出来的**事实为准：
 * fnos_ui.c 的 fnos_ui_dump_card_map() 除了 [card] 行，还为每张列表卡打印一行 [bar]，
 * 内容取自 LVGL 绘制时用的同一个函数 lv_obj_get_scrollbar_area() 与真实滚动余量：
 *   over=1  内容确有溢出（scroll_top/bottom）
 *   drawn=1 这一帧条子真的会画出来，x/y/w/h 就是它的位置
 *   mode/dir/t/rs/ls/opa  复算"条子该在哪一列"所需的样式量（ACTIVE 模式静态帧不画条，
 *          但滑动时它占的列与这里复算的完全一致 —— lv_obj_scroll.c 的
 *          ver.x2 = coords.x2 - right_space; ver.x1 = x2 - thickness + 1）
 * 像素只用来确认两件事：条子说画了、画面上真的在；以及内容右缘量到哪。
 *
 * ── 分类与闸门 ───────────────────────────────────────────────────────
 *   audit     有条子（或滑动时会有）→ 量间隙，判据如上
 *   no-scroll 内容不溢出，无需滚动 → 判定结果，不算不合格也不算未判定
 *   no-vert   该列表不含纵向滚动
 *   bar-off   列表显式关掉了滚动条（模式 OFF），内容仍可拖动
 *   未判定    看得见"应该有"却拿不到列（缺 [bar] 行、或样式与模式自相矛盾）
 *             —— **未判定一律阻塞总闸门**（exit 3），不再当噪声放过
 * 退出码：0 全通过 / 1 有不合格 / 2 结构变了（一张列表卡都没有）/ 3 有未判定
 *
 * 用法：node tools/preview/scroll_gap.js [out目录]      （默认 tools/preview/out）
 *       node tools/preview/scroll_gap.js --self-test    （判定逻辑自测，不需要设备）
 * 自测会喂合成卡片表与合成 PNG，逐个断言上面每一类 —— 判定能力本身也有回归。
 */
'use strict';
const fs = require('fs'), path = require('path'), zlib = require('zlib');

/* 卡片几何不在这里手抄 —— 由被审计的那个二进制自己吐出来（PREVIEW_CARDMAP=1）。
   2026-10-06：手抄的 PAGES 表在 v9 版面重排后过期，一次报出 15 处假不合格
   （内容→条=2px），排查花了两小时。几何只有一个来源：fnos_ui.c 的
   fnos_ui_dump_card_map()。卡头的 CSS 坐标 → 卡片内区域一律按 KK_PAD/卡头高算。 */
const CARD_PAD = 16;              /* 卡内左右内边距 */
const GAP_MIN = 8;                /* 内容 → 条 */
const EDGE_MIN = 4;               /* 条 → 卡片内缘 */
const MODE = { OFF: 0, ON: 1, ACTIVE: 2, AUTO: 3 };

function loadCardMapText() {
  /* 自测/离线复算的入口：给定卡片表文本就直接用，不去跑二进制。 */
  if (process.env.SCROLL_GAP_CARDMAP) {
    return fs.readFileSync(process.env.SCROLL_GAP_CARDMAP, 'utf8');
  }
  const { execFileSync } = require('child_process');
  const bin = path.join(__dirname, 'build', 'preview');
  try {
    return execFileSync(bin, [path.join(require('os').tmpdir(), 'scrollgap-cm')],
                        { env: { ...Object.fromEntries(Object.entries(process.env).filter(([key]) => !key.startsWith('PREVIEW_'))), PREVIEW_CARDMAP: '1' }, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] });
  } catch (e) { throw new Error('preview 卡片几何生成失败：' + e.message); }
}

function parseCardMap(text) {
  const cards = [];
  for (const line of text.split('\n')) {
    let m = line.match(/^\[card\] p=(-?\d+) x=(-?\d+) y=(-?\d+) w=(\d+) h=(\d+) list=(\d)(?: nest=(\d+))?/);
    if (m) {
      /* nest= 是这张卡里"除直接子列表以外"的可滚动对象总数（分母）。真正溢出/画了条的
         嵌套滚动区随后各有一行 [bar] d>=1 —— 只看直接子对象会漏掉温度页那种
         卡片→容器→网格 的嵌套滚动区（审计 H14 的覆盖面）。 */
      cards.push({ p: +m[1], x: +m[2], y: +m[3], w: +m[4], h: +m[5], list: +m[6] === 1,
                   nest: m[7] === undefined ? 0 : +m[7], bar: null, nested: [], depth: 0 });
      continue;
    }
    m = line.match(/^\[bar\] p=(-?\d+) d=(-?\d+) lx=(-?\d+) ly=(-?\d+) lw=(\d+) lh=(\d+) drawn=(\d+) over=(\d+) mode=(\d+) dir=(\d+) t=(-?\d+) rs=(-?\d+) ls=(-?\d+) opa=(-?\d+) x=(-?\d+) y=(-?\d+) w=(\d+) h=(\d+)/);
    if (m && cards.length) {
      const rec = {
        p: +m[1], d: +m[2], lx: +m[3], ly: +m[4], lw: +m[5], lh: +m[6],
        drawn: +m[7] === 1, over: +m[8] === 1, mode: +m[9], dirRaw: +m[10],
        dir: (+m[10] & 12) !== 0,      /* LV_DIR_VER = TOP|BOTTOM = 12 */
        t: +m[11], rs: +m[12], ls: +m[13], opa: +m[14],
        x: +m[15], y: +m[16], w: +m[17], h: +m[18],
      };
      const clip = line.match(/ vis=(\d+) paint=(\d+) cx=(-?\d+) cy=(-?\d+) cw=(\d+) ch=(\d+)/);
      if (clip) {
        Object.assign(rec, { visible: +clip[1] === 1, paint: +clip[2] === 1,
                            cx: +clip[3], cy: +clip[4], cw: +clip[5], ch: +clip[6] });
      }
      if (rec.d === 0) cards[cards.length - 1].bar = rec;
      else cards[cards.length - 1].nested.push(rec);
    }
  }
  if (!cards.length) throw new Error('卡片几何图是空的：preview 没能吐出 [card] 行');
  cards.activePages = [...text.matchAll(/^\[page\] k=(\d+) .* hidden=0$/gm)].map(m => +m[1]);
  return cards;
}

/* 逐帧滚动条表：preview 在**每个快照**时把那一帧的 [card]/[bar] 行追加进一份文件
   （PREVIEW_BARMAP=<file>，见 tools/preview/preview.c 的 snapshot()）。
   审计为什么非要逐帧依据：卡片几何以前只在建树那一刻取一次（PREVIEW_CARDMAP 取完
   立刻返回），而渲染的帧里有 dense/expanded 这类列表真的会溢出的状态 —— 拿启动态的
   "有没有条、条在哪一列"去审别的时刻的 PNG，判出来的结论不是这一帧的（审计 H14）。
   找不到这份表就只有启动态几何可依据，那时审计一律记"未判定"（阻塞），
   不许拿错位时刻的依据冒充判定结果。 */
function loadFrameMap(dir) {
  const p = process.env.SCROLL_GAP_BARMAP || path.join(dir, 'barmap.txt');
  if (!fs.existsSync(p)) return { perFrame: null, src: null };
  const text = fs.readFileSync(p, 'utf8');
  const raw = new Map();
  let cur = null, buf = [];
  for (const line of text.split('\n')) {
    const m = line.match(/^\[frame\] (.+?)\s*$/);
    if (m) { if (cur) raw.set(cur, buf); cur = m[1]; buf = []; continue; }
    if (cur) buf.push(line);
  }
  if (cur) raw.set(cur, buf);
  const perFrame = new Map();
  for (const [name, lines] of raw) {
    const body = lines.join('\n');
    if (!/^\[card\]/m.test(body)) continue;          /* 整屏/诊断帧没有卡片表，不算覆盖 */
    try { perFrame.set(name, parseCardMap(body)); } catch (e) { /* 该帧没有 [card] 行 */ }
  }
  if (!perFrame.size) return { perFrame: null, src: p, empty: true };
  return { perFrame, src: p };
}

function readPNG(p) {
  const budget = 64 * 1024 * 1024;
  if (fs.statSync(p).size > budget) throw new Error('PNG 输入超预算');
  const d = fs.readFileSync(p);
  if (!d.subarray(0, 8).equals(Buffer.from([137,80,78,71,13,10,26,10]))) throw new Error('PNG 签名无效');
  let i = 8, idat = [], w = 0, h = 0, ct = 0, ch = 0, ended = false, dataEnded = false;
  while (i < d.length) {
    if (d.length - i < 12) throw new Error('PNG chunk 截断');
    const ln = d.readUInt32BE(i), end = i + 12 + ln;
    if (end > d.length) throw new Error('PNG chunk 越界');
    const t = d.toString('latin1', i + 4, i + 8), c = d.subarray(i + 8, end - 4);
    if (crc32(d.subarray(i + 4, end - 4)) !== d.readUInt32BE(end - 4)) throw new Error('PNG CRC 错误');
    if (i === 8 && t !== 'IHDR') throw new Error('PNG 缺首部');
    if (t === 'IHDR') {
      if (i !== 8 || ln !== 13) throw new Error('PNG IHDR 无效');
      w = c.readUInt32BE(0); h = c.readUInt32BE(4); ct = c[9];
      ch = {0:1, 2:3, 4:2, 6:4}[ct];
      if (!w || !h || c[8] !== 8 || !ch || c[10] || c[11] || c[12]) throw new Error('PNG 像素格式不支持');
      if (!Number.isSafeInteger(h * (w * ch + 1)) || h * (w * ch + 1) > budget || w * h * 3 > budget)
        throw new Error('PNG 像素超预算');
    } else if (t === 'IDAT') {
      if (dataEnded) throw new Error('PNG IDAT 不连续');
      idat.push(c);
    } else if (t === 'IEND') {
      if (ln || !idat.length || end !== d.length) throw new Error('PNG IEND 无效');
      ended = true; break;
    } else {
      if (idat.length) dataEnded = true;
      if (!(t.charCodeAt(0) & 32) && t !== 'PLTE') throw new Error('PNG 未知关键chunk');
    }
    i = end;
  }
  if (!ended) throw new Error('PNG 缺 IEND');
  const stride = w * ch, expected = h * (stride + 1), packed = Buffer.concat(idat);
  const inflated = zlib.inflateSync(packed, {maxOutputLength: expected + 1, info: true});
  const raw = inflated.buffer;
  if (raw.length !== expected || inflated.engine.bytesWritten !== packed.length) throw new Error('PNG 解压长度或尾流无效');
  const decoded = Buffer.alloc(w * h * ch);
  let prev = Buffer.alloc(stride), pos = 0;
  for (let y = 0; y < h; y++) {
    const f = raw[pos++];
    if (f > 4) throw new Error('PNG filter 无效');
    const line = Buffer.from(raw.subarray(pos, pos + stride)); pos += stride;
    if (f === 1) for (let x = ch; x < stride; x++) line[x] = (line[x] + line[x - ch]) & 255;
    else if (f === 2) for (let x = 0; x < stride; x++) line[x] = (line[x] + prev[x]) & 255;
    else if (f === 3) for (let x = 0; x < stride; x++) { const a = x >= ch ? line[x - ch] : 0; line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255; }
    else if (f === 4) for (let x = 0; x < stride; x++) {
      const a = x >= ch ? line[x - ch] : 0, b = prev[x], c2 = x >= ch ? prev[x - ch] : 0, pp = a + b - c2;
      const pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c2);
      line[x] = (line[x] + ((pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c2))) & 255;
    }
    line.copy(decoded, y * stride); prev = line;
  }
  // Pixel classification works on opaque RGB; grayscale is explicitly expanded.
  const out = Buffer.alloc(w * h * 3);
  for (let p = 0; p < w * h; p++) {
    const src = p * ch, dst = p * 3;
    if ((ct === 4 || ct === 6) && decoded[src + ch - 1] !== 255) throw new Error('PNG 透明像素无法判定');
    if (ct === 0 || ct === 4) out.fill(decoded[src], dst, dst + 3);
    else decoded.copy(out, dst, src, src + 3);
  }
  return {px: out, w, h, ch: 3};
}

/* 中性灰、纵向连续 >=24px 的像素列。只用来确认"说画了，画面上真的有"。 */
function findTrack(g, x0, x1, y0, y1) {
  const cols = [];
  for (let x = x0; x <= x1; x++) {
    let run = 0, best = 0;
    for (let y = y0; y < y1; y++) {
      const [r, gg, b] = g(x, y);
      const neutral = Math.abs(r - gg) <= 12 && Math.abs(gg - b) <= 26 && r >= 55 && r <= 160;
      run = neutral ? run + 1 : 0;
      if (run > best) best = run;
    }
    if (best >= 24) cols.push(x);
  }
  if (!cols.length) return null;
  return { x0: cols[0], x1: cols[cols.length - 1], len: cols.length };
}

/* 内容右缘：逐行排除滚动条与横向长连通背景带。 */
function contentRight(g, x0, x1, y0, y1, base, bars) {
  const isBar = x => bars.some(b => x >= b.x0 - 1 && x <= b.x1 + 1);
  let cr = -1;
  for (let y = y0; y < y1; y++) {
    const backgrounds = [];
    const distance = (a, b) => a.reduce((sum, v, i) => sum + Math.abs(v - b[i]), 0);
    let start = x0, color = g(x0, y);
    for (let x = x0 + 1; x <= x1 + 1; x++) {
      const next = x <= x1 ? g(x, y) : null;
      if (next && distance(next, color) <= 12) continue;
      if (x - start >= (x1 - x0 + 1) * 0.4) backgrounds.push(color);
      start = x; color = next;
    }
    if (!backgrounds.length) backgrounds.push(g(x0, y));
    if (base) backgrounds.push(base(y));
    for (let x = x0; x <= x1; x++) {
      if (isBar(x)) continue;
      const [r, gg, b] = g(x, y);
      if (backgrounds.every(bg => distance([r, gg, b], bg) > 44)) cr = Math.max(cr, x);
    }
  }
  return cr;
}

/* LVGL 自己的列公式（managed_components/lvgl__lvgl/src/core/lv_obj_scroll.c:
   ver_area->x2 = obj->coords.x2 - right_space; ver_area->x1 = x2 - thickness + 1）。
   模式 ACTIVE 时静态帧不画条，但滑动时占的就是这一列 —— 照样能判间隙。 */
function gutterColumn(bar) {
  const x2 = bar.lx + bar.lw - 1 - bar.rs;
  return { x0: x2 - bar.t + 1, x1: x2 };
}

/* 分类：一律基于 [bar] 行的硬事实，不猜像素。 */
function classify(bar) {
  if (!bar) return { kind: 'undetermined', why: '这张列表卡没有 [bar] 行（二进制没吐/版本不匹配）' };
  if (bar.visible === undefined) return { kind: 'undetermined', why: '缺少实际裁切和透明度依据，请重新渲染' };
  if (!bar.visible) return { kind: 'clipped', why: '被父容器裁切或隐藏，本帧屏幕不可见' };
  if (bar.t <= 0 && !bar.drawn) return { kind: 'bar-off', why: '滚动条宽度为零，LVGL 不绘制' };
  if (!bar.paint) return { kind: 'bar-off', why: '滚动条背景及边框均透明，LVGL 不绘制' };
  if (!bar.dir) return { kind: 'no-vert', why: '这个列表不含纵向滚动（scroll_dir 无 LV_DIR_VER）' };
  if (bar.mode === MODE.OFF && !bar.drawn) {
    if (!bar.over) return { kind: 'no-scroll', why: '内容不溢出（滚动余量 0），且滚动条被显式关掉' };
    return { kind: 'bar-off', why: '内容可滚动，但列表显式关掉了滚动条（模式 OFF）' };
  }
  if (bar.drawn) {
    return { kind: 'audit', pixelExpected: true, why: 'LVGL 这一帧会画条',
             track: { x0: bar.x, x1: bar.x + Math.max(bar.w, 1) - 1 } };
  }
  if (bar.mode === MODE.ACTIVE && bar.over) {
    return { kind: 'audit', pixelExpected: false, track: gutterColumn(bar),
             why: '滚动条只在滑动时出现（模式 ACTIVE），按 LVGL 列公式复算' };
  }
  if (!bar.over) return { kind: 'no-scroll', why: '内容不溢出（滚动余量 0），LVGL 本就不画条' };
  if (bar.mode === MODE.ON || bar.mode === MODE.AUTO) {
    return { kind: 'undetermined',
             why: `内容溢出、模式 ${bar.mode === MODE.ON ? 'ON' : 'AUTO'}，但 LVGL 说条子不画` +
                  `（样式 or 透明度 opa=${bar.opa}？）—— 需要人看一眼` };
  }
  return { kind: 'undetermined', why: `无法解释的状态：mode=${bar.mode} over=${bar.over ? 1 : 0}` };
}

/* 像素确认：LVGL 说这一帧画了条，画面里就得真有那一条。直接量它自己给的拇指矩形
   (b.x,b.y,b.w,b.h)，不再用"在窗口里找一条 ≥24px 的灰柱"的启发式 —— 短列表的拇指
   只有 8px 高，旧启发式必然找不到，于是"画了却说没有"（假不合格）。 */
function thumbVisible(g, b) {
  if (b.w <= 0 || b.h <= 0) return false;
  const yTop = Math.max(b.y, b.ly, b.cy), yBot = Math.min(b.y + b.h - 1, b.ly + b.lh - 1, b.cy + b.ch - 1);
  for (let y = yTop; y <= yBot; y++) {
    const bg = g(Math.max(b.lx, b.x - 6), y);          /* 拇指列左边的列表底色 */
    for (let x = Math.max(b.x, b.cx); x <= Math.min(b.x + b.w - 1, b.cx + b.cw - 1); x++) {
      const c = g(x, y);
      if (Math.abs(c[0] - bg[0]) + Math.abs(c[1] - bg[1]) + Math.abs(c[2] - bg[2]) > 24) return true;
    }
  }
  return false;
}

function auditPage(file, page, CARDS, opts) {
  opts = opts || {};
  const { px, w, ch } = readPNG(file);
  const g = (x, y) => { const o = (y * w + x) * ch; return [px[o], px[o + 1], px[o + 2]]; };
  /* 所有已知条子的列都要排除 —— 相邻卡片的条子不能被当成"内容"。 */
  const bars = [];
  for (const c of CARDS) {
    if (c.p !== page) continue;
    for (const b of [c.bar, ...c.nested]) {
      if (b && b.drawn) bars.push({ x0: b.x, x1: b.x + Math.max(b.w, 1) - 1 });
    }
  }
  const rows = [], noScroll = [], barOff = [], noVert = [], clipped = [], undetermined = [];
  /* 一条"待判定记录" = 一个真的可能有条的滚动区：卡片的直接子列表（d=0）+ 嵌套滚动区
     （d>=1，只在真的溢出或真的画了条时才由固件吐出来）。嵌套的那些以前根本不进审计，
     而温度页展开的通道网格、卡里的行池都在这一层 —— 漏掉它们等于"没看着也算过"（H14）。 */
  const recs = [];
  for (const c of CARDS) {
    if (c.p !== page) continue;
    const name = `卡(${c.x},${c.y} ${c.w}x${c.h})`;
    if (c.list) recs.push({ name, bar: c.bar, c, owner: c });
    for (const nb of c.nested)
      recs.push({ name: `${name} 嵌套(${nb.lx},${nb.ly} ${nb.lw}x${nb.lh}) d=${nb.d}`, bar: nb, c, owner: c, nested: true });
  }
  for (const rec of recs) {
    const { c, bar } = rec;
    const name = rec.name;
    if (opts.basisBlocked) { undetermined.push(`${name}：${opts.basisBlocked}`); continue; }
    const cls = classify(bar);
    if (cls.kind === 'clipped') { clipped.push(name); continue; }
    if (cls.kind === 'no-scroll') { noScroll.push(name); continue; }
    if (cls.kind === 'bar-off') { barOff.push(name); continue; }
    if (cls.kind === 'no-vert') { noVert.push(name); continue; }
    if (cls.kind === 'undetermined') { undetermined.push(`${name}：${cls.why}`); continue; }

    const b = bar;
    const track = cls.track;
    if (track.x1 < b.cx || track.x0 >= b.cx + b.cw) { clipped.push(name); continue; }
    const y0 = Math.max(b.ly + 4, b.cy, 0), y1 = Math.min(b.ly + b.lh - 4, b.cy + b.ch);
    let pixels = null;
    if (cls.pixelExpected) pixels = thumbVisible(g, b);
    /* 内容右缘量到条子左缘之前：条子底下压着的像素不算"内容已经让开"。 */
    const cr = contentRight(g, Math.max(b.lx + 4, b.cx), track.x0 - 1, y0, y1,
                            y => g(b.lx + b.lw - 2, y), bars);
    const gap = track.x0 - cr;
    /* "条→容器内缘"的容器是**拥有这条的那个滚动区**（列表/嵌套网格自己），不是外面那张卡：
       条子是贴着自己容器的右内缘画的，拿卡片的边去量会把 6px 的 gutter 算成别的数。 */
    const edge = (b.lx + b.lw - 1) - track.x1;
    const ok = cr >= 0 && gap >= GAP_MIN && edge >= EDGE_MIN && (!cls.pixelExpected || pixels === true);
    let note = '';
    if (cls.pixelExpected && pixels !== true) note = `  LVGL 说这一帧画条，像素上却没有 <<< 不合格`;
    else if (!ok && cr < 0) note = '  量不到内容右缘 <<< 不合格';
    rows.push({ name, track, cr, gap, edge, ok: ok && !note, note,
                trace: `${cls.why}${cls.pixelExpected ? '' : '（静态帧无条，按公式列）'}` });
  }
  return { rows, noScroll, barOff, noVert, clipped, undetermined };
}

function runAudit(dir, CARDS) {
  const files = fs.readdirSync(dir).filter(f => f.endsWith('.png')).sort();
  const fm = loadFrameMap(dir);
  const res = { audited: 0, pages: 0, noScroll: 0, barOff: 0, noVert: 0, undetermined: 0,
                bad: 0, clipped: 0, listCards: 0, basis: fm.src || null, basisFrames: 0, basisStale: 0,
                nestedTotal: 0, nestedScrolling: 0, lines: [] };
  res.lines.push(fm.perFrame
    ? `\n判定依据：逐帧滚动条表 ${fm.src}（${fm.perFrame.size} 帧）`
    : `\n判定依据：**没有逐帧滚动条表**（${fm.src ? fm.src + ' 里没有可用的帧' : '没找到 barmap.txt'}）` +
      ` —— 只有启动态几何，与所审的帧不是同一时刻，列表卡一律记未判定`);
  for (const f of files) {
    const m = f.match(/-p(\d)\.png$/);
    const frameCards = fm.perFrame && fm.perFrame.get(f.replace(/\.png$/, ''));
    const active = frameCards && frameCards.activePages;
    const page = active && active.length === 1 ? active[0] :
                 m ? +m[1] : (f.includes('system') ? 3 : f.includes('storage') ? 1 : null);
    if (page === null) { res.lines.push(`\n### ${f}：整屏/诊断态，跳过（非四页网格）`); continue; }
    /* 依据必须与这一帧同一时刻：逐帧表里有就用它；没有就说"没有依据"，
       退回启动态几何是审计 H14 的原始缺陷（18 处假不合格 + 215 处分不清原因）。 */
    const frame = f.replace(/\.png$/, '');
    let cards = CARDS, basisBlocked = null;
    if (fm.perFrame) {
      if (fm.perFrame.has(frame)) { cards = fm.perFrame.get(frame); res.basisFrames++; }
      /* 缺这一帧时仍用启动态那份，但只用来**点名**（哪几张列表卡没有依据）；
         判定结论一律是"未判定"，绝不借它去量净距。 */
      else { cards = CARDS; basisBlocked = '逐帧滚动条表里没有这一帧'; res.basisStale++; }
    } else {
      basisBlocked = '没有逐帧滚动条依据（只有启动态几何）—— 重渲染并带上 PREVIEW_BARMAP';
      res.basisStale++;
    }
    res.listCards += cards.filter(c => c.p === page && c.list).length;
    for (const c of cards) {
      if (c.p !== page) continue;
      res.nestedTotal += c.nest || 0;
      res.nestedScrolling += c.nested.length;
    }
    const a = auditPage(path.join(dir, f), page, cards, { basisBlocked });
    res.clipped += a.clipped.length;
    res.noScroll += a.noScroll.length;
    res.barOff += a.barOff.length;
    res.noVert += a.noVert.length;
    res.undetermined += a.undetermined.length;
    if (!a.rows.length && !a.undetermined.length && !a.noScroll.length && !a.barOff.length && !a.noVert.length) {
      res.lines.push(`\n### ${f}：本页没有列表卡`);
      continue;
    }
    if (!a.rows.length) {
      res.lines.push(`\n### ${f}：本页无可判定项（无需滚动 ${a.noScroll.length} / 关条 ${a.barOff.length} / ` +
                     `非纵向 ${a.noVert.length} / 未判定 ${a.undetermined.length}）`);
      a.undetermined.forEach(u => res.lines.push(`  未判定 ${u}`));
      continue;
    }
    res.pages++;
    res.lines.push(`\n### ${f}`);
    for (const r of a.rows) {
      res.audited++;
      if (!r.ok) res.bad++;
      res.lines.push(`  ${r.name}  滚动条 x=[${r.track.x0},${r.track.x1}]  内容右缘=${r.cr}  ` +
                     `内容→条=${r.gap}px  条→卡片内缘=${r.edge}px  ${r.ok ? 'OK' : '<<< 不合格'}${r.note}` +
                     `\n     依据：${r.trace}`);
    }
    a.undetermined.forEach(u => res.lines.push(`  未判定 ${u}`));
    if (a.undetermined.length) res.lines.push(`  （另有 ${a.undetermined.length} 处未判定，阻塞总闸门）`);
  }
  return res;
}

/* ── 判定逻辑自测：合成卡片表 + 合成 PNG，逐类断言 ─────────────────── */
function crc32(buf) {
  const t = [];
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; }
  let crc = 0xffffffff;
  for (const b of buf) crc = t[(crc ^ b) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const td = Buffer.concat([Buffer.from(type, 'latin1'), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
  return Buffer.concat([len, td, crc]);
}
function writePNG(p, w, h, rgb) {
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; ihdr[9] = 2;
  const stride = w * 3, raw = Buffer.alloc(h * (1 + stride));
  for (let y = 0; y < h; y++) { raw[y * (1 + stride)] = 0; rgb.copy(raw, y * (1 + stride) + 1, y * stride, (y + 1) * stride); }
  fs.writeFileSync(p, Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]));
}

function selfTest() {
  const tmp = fs.mkdtempSync(path.join(require('os').tmpdir(), 'scrollgap-self-'));
  const W = 220, H = 140, BG = [22, 30, 43], TXT = [240, 240, 240], BAR = [90, 98, 112];
  const CARD = { x: 8, y: 8, w: 200, h: 120 };
  const LIST = { lx: 24, ly: 44, lw: 168, lh: 68 };
  const BARCOL = 24 + 168 - 1 - 12;                 /* 179：LVGL 的 ver_area.x2 */
  let failures = 0;
  const check = (cond, what) => { console.log(`  ${cond ? '✓' : '✗'} ${what}`); if (!cond) failures++; };

  /* 场景：卡片底 + 一行"右对齐文字"（白色短划，模拟真实文本，不是一整条色带）+
     可选滚动条竖条。内容画成短划很关键：contentRight() 会把"占窗口 40% 以上的
     连通色带"当背景，一整条 130px 的白块会被当成背景而不是内容（第一版自测就
     栽在这儿：对照组量不到内容右缘）。 */
  function scene({ contentRight, drawBar, barX }) {
    const rgb = Buffer.alloc(W * H * 3);
    for (let i = 0; i < W * H; i++) { rgb[i * 3] = BG[0]; rgb[i * 3 + 1] = BG[1]; rgb[i * 3 + 2] = BG[2]; }
    if (contentRight > 0) {
      for (let y = LIST.ly + 10; y < LIST.ly + 40; y++) {
        let x = LIST.lx + 8;
        while (x <= contentRight) {
          const end = Math.min(x + 13, contentRight);           /* 14px 划 + 6px 空 */
          for (let xx = x; xx <= end; xx++) {
            const o = (y * W + xx) * 3; rgb[o] = TXT[0]; rgb[o + 1] = TXT[1]; rgb[o + 2] = TXT[2];
          }
          x += 20;
        }
      }
    }
    if (drawBar) {
      for (let y = LIST.ly + 6; y < LIST.ly + 60; y++)
        for (let x = barX; x <= barX + 3; x++) {
          const o = (y * W + x) * 3; rgb[o] = BAR[0]; rgb[o + 1] = BAR[1]; rgb[o + 2] = BAR[2];
        }
    }
    return rgb;
  }
  const barLine = (o = {}) => {
    const v = Object.assign({ drawn: 1, over: 1, mode: MODE.AUTO, dir: 12, t: 4, rs: 12, ls: 0, opa: 255,
                              x: BARCOL - 3, y: LIST.ly + 6, w: 4, h: 54 }, o);
    return `[bar] p=0 d=0 lx=${LIST.lx} ly=${LIST.ly} lw=${LIST.lw} lh=${LIST.lh} drawn=${v.drawn} ` +
           `over=${v.over} mode=${v.mode} dir=${v.dir} t=${v.t} rs=${v.rs} ls=${v.ls} opa=${v.opa} ` +
           `x=${v.x} y=${v.y} w=${v.w} h=${v.h} ` +
           `vis=${v.visible === false ? 0 : 1} paint=${v.paint === false ? 0 : 1} cx=${LIST.lx} cy=${LIST.ly} cw=${LIST.lw} ch=${LIST.lh}\n`;
  };
  const cardLine = `[card] p=0 x=${CARD.x} y=${CARD.y} w=${CARD.w} h=${CARD.h} list=1 nest=0\n`;
  /* 嵌套滚动区的记录：d>=1，坐标是它自己那块区域（与主列表同一套判据） */
  const nestedLine = (o = {}) => {
    const v = Object.assign({ lx: LIST.lx, ly: LIST.ly + 4, lw: LIST.lw - 40, lh: LIST.lh - 8,
                              drawn: 1, over: 1, mode: MODE.AUTO, dir: 12, t: 4, rs: 6, ls: 0, opa: 255,
                              x: LIST.lx + LIST.lw - 40 - 1 - 6, y: LIST.ly + 10, w: 4, h: 40 }, o);
    return `[bar] p=0 d=2 lx=${v.lx} ly=${v.ly} lw=${v.lw} lh=${v.lh} drawn=${v.drawn} over=${v.over} ` +
           `mode=${v.mode} dir=${v.dir} t=${v.t} rs=${v.rs} ls=${v.ls} opa=${v.opa} ` +
           `x=${v.x} y=${v.y} w=${v.w} h=${v.h} ` +
           `vis=${v.visible === false ? 0 : 1} paint=${v.paint === false ? 0 : 1} cx=${LIST.lx} cy=${LIST.ly} cw=${LIST.lw} ch=${LIST.lh}\n`;
  };
  /* 每次跑一个场景：写一张 PNG + 一份卡片表（用环境变量喂给同一套解析/审计代码） */
  function run(name, cardMap, png, opts) {
    opts = opts || {};
    const d = path.join(tmp, name); fs.mkdirSync(d);
    writePNG(path.join(d, 'self-p0.png'), W, H, png);
    const cm = path.join(d, 'cardmap.txt'); fs.writeFileSync(cm, cardMap);
    /* 逐帧表才是审计的依据来源：每个场景都写一份，走的就是真实路径。
       noBarmap / frameName 用来验证"依据不足时不许判"。 */
    if (!opts.noBarmap) {
      fs.writeFileSync(path.join(d, 'barmap.txt'), `[frame] ${opts.frameName || 'self-p0'}\n` + cardMap);
    }
    const cards = parseCardMap(cardMap);
    return runAudit(d, cards);
  }

  /* 1. 有条子、内容压到条子上 → 不合格 */
  let r = run('overlap', cardLine + barLine(), scene({ contentRight: BARCOL, drawBar: true, barX: BARCOL - 3 }));
  check(r.audited === 1 && r.bad === 1, '内容压着滚动条 → 判不合格（gap<8 或像素重叠）');

  /* 2. 有条子、内容让开 9px 以上 → 通过（证明判据不是"永远不合格"） */
  r = run('clear', cardLine + barLine(), scene({ contentRight: BARCOL - 3 - 9, drawBar: true, barX: BARCOL - 3 }));
  check(r.audited === 1 && r.bad === 0, '内容让开 >=8px → 判通过（对照组）');

  /* 3. 内容不溢出 → no-scroll（"本就不需要滚动"是判定结果，不是未判定） */
  r = run('noscroll', cardLine + barLine({ over: 0, drawn: 0, x: 0, y: 0, w: 0, h: 0 }),
          scene({ contentRight: BARCOL - 20, drawBar: false, barX: 0 }));
  check(r.audited === 0 && r.noScroll === 1 && r.undetermined === 0 && r.bad === 0,
        '滚动余量 0 → 记为"内容不需要滚动"（不阻塞）');

  /* 4. LVGL 说画条、画面上却没有 → 不合格（"应该有却找不到"） */
  r = run('phantom', cardLine + barLine(), scene({ contentRight: BARCOL - 20, drawBar: false, barX: 0 }));
  check(r.audited === 1 && r.bad === 1, 'LVGL 说画了但像素上没有 → 不合格（不是未判定）');

  /* 5. ACTIVE 模式静态帧无条 → 用 LVGL 列公式照样判（内容压上去 → 不合格） */
  r = run('active-overlap', cardLine + barLine({ mode: MODE.ACTIVE, drawn: 0, x: 0, y: 0, w: 0, h: 0 }),
          scene({ contentRight: BARCOL, drawBar: false, barX: 0 }));
  check(r.audited === 1 && r.bad === 1, 'ACTIVE 模式：按列公式复算，内容压条 → 不合格');
  r = run('active-clear', cardLine + barLine({ mode: MODE.ACTIVE, drawn: 0, x: 0, y: 0, w: 0, h: 0 }),
          scene({ contentRight: BARCOL - 3 - 9, drawBar: false, barX: 0 }));
  check(r.audited === 1 && r.bad === 0, 'ACTIVE 模式：内容让开 → 通过');

  /* 6. 缺 [bar] 行 → 未判定，并阻塞总闸门 */
  r = run('nobar', cardLine, scene({ contentRight: BARCOL - 20, drawBar: false, barX: 0 }));
  check(r.audited === 0 && r.undetermined === 1, '缺 [bar] 行 → 未判定');
  check(exitCode(r) === 3, '未判定把总闸门判成 exit 3（阻塞，不再静默放过）');

  /* 7. 显式关条 → bar-off（判定结果，不阻塞），非纵向列表 → no-vert */
  r = run('baroff', cardLine + barLine({ mode: MODE.OFF, drawn: 0, x: 0, y: 0, w: 0, h: 0 }),
          scene({ contentRight: BARCOL - 20, drawBar: false, barX: 0 }));
  check(r.barOff === 1 && r.audited === 0 && r.bad === 0 && exitCode(r) === 0,
        '显式关条（模式 OFF）→ 记为 bar-off，不阻塞');
  r = run('noyert', cardLine + barLine({ dir: 0, drawn: 0, x: 0, y: 0, w: 0, h: 0 }),
          scene({ contentRight: BARCOL - 20, drawBar: false, barX: 0 }));
  check(r.noVert === 1 && exitCode(r) === 0, '非纵向滚动的列表 → no-vert，不阻塞');

  /* 8. 内容溢出 + 模式 AUTO 却说不画条 → 未判定（样式自相矛盾要人看） */
  r = run('contradiction', cardLine + barLine({ over: 1, mode: MODE.AUTO, drawn: 0, x: 0, y: 0, w: 0, h: 0 }),
          scene({ contentRight: BARCOL - 20, drawBar: false, barX: 0 }));
  check(r.undetermined === 1 && exitCode(r) === 3, '溢出却说不画条 → 未判定（阻塞）');

  /* 9. 没有逐帧滚动条表 → 依据不足：不许拿启动态几何冒充判定结果 */
  r = run('nobarmap', cardLine + barLine(), scene({ contentRight: BARCOL - 20, drawBar: true, barX: BARCOL - 3 }),
          { noBarmap: true });
  check(r.undetermined === 1 && r.audited === 0 && exitCode(r) === 3,
        '没有逐帧滚动条表 → 依据不足记未判定，阻塞总闸门');
  /* 10. 逐帧表里没有这一帧 → 同样记未判定（依据对不上就不判） */
  r = run('wrongframe', cardLine + barLine(), scene({ contentRight: BARCOL - 20, drawBar: true, barX: BARCOL - 3 }),
          { frameName: 'other-frame' });
  check(r.undetermined === 1 && r.audited === 0 && exitCode(r) === 3,
        '逐帧表里没有这一帧 → 未判定（不退回启动态几何）');
  /* 11. 启动态几何说"不溢出"、这一帧的逐帧表说"溢出且内容压条" → 必须按逐帧判不合格
        （证明用的是这一帧的依据；退回启动态几何就会漏报） */
  { const d = path.join(tmp, 'framewins'); fs.mkdirSync(d);
    writePNG(path.join(d, 'self-p0.png'), W, H, scene({ contentRight: BARCOL, drawBar: true, barX: BARCOL - 3 }));
    fs.writeFileSync(path.join(d, 'barmap.txt'), '[frame] self-p0\n' + cardLine + barLine());
    r = runAudit(d, parseCardMap(cardLine + barLine({ over: 0, drawn: 0, x: 0, y: 0, w: 0, h: 0 })));
    check(r.audited === 1 && r.bad === 1,
          '启动态说"不溢出"、逐帧表说"压着条" → 按逐帧判不合格（依据与帧同刻）');
  }

  /* 12. 嵌套滚动区（卡片→容器→网格）也要逐条判定，且给出覆盖面分母 */
  r = run('nested-overlap', `[card] p=0 x=${CARD.x} y=${CARD.y} w=${CARD.w} h=${CARD.h} list=1 nest=3\n` +
          barLine({ over: 0, drawn: 0, x: 0, y: 0, w: 0, h: 0 }) +
          nestedLine({ x: LIST.lx + LIST.lw - 40 - 1 - 6 }),
          scene({ contentRight: LIST.lx + LIST.lw - 1, drawBar: true, barX: LIST.lx + LIST.lw - 40 - 1 - 6 }));
  check(r.nestedTotal === 3 && r.nestedScrolling === 1, '嵌套滚动区：分母 3、真的在滚的 1 个（其余计为无溢出）');
  check(r.audited === 1, '嵌套滚动区进入判定（不再漏审）');

  r = run('clipped', cardLine + barLine({ visible: false }),
          scene({ contentRight: BARCOL, drawBar: false, barX: 0 }));
  check(r.clipped === 1 && r.audited === 0 && r.undetermined === 0,
        '父容器裁切不可见 → 明确记裁切，不能误报幽灵滚动条');
  r = run('transparent', cardLine + barLine({ paint: false, drawn: 0, opa: 0 }),
          scene({ contentRight: BARCOL, drawBar: false, barX: 0 }));
  check(r.barOff === 1 && r.undetermined === 0,
        '背景和边框均透明 → LVGL 明确不绘制，不能误报未判定');
  r = run('zero-width', cardLine + barLine({ t: 0, drawn: 0 }),
          scene({ contentRight: BARCOL, drawBar: false, barX: 0 }));
  check(r.barOff === 1 && r.undetermined === 0,
        '宽度为零 → 明确关条，不误报未判定');
  r = run('no-clip-facts', cardLine + barLine().replace(/ vis=.*\n/, '\n'),
          scene({ contentRight: BARCOL - 20, drawBar: true, barX: BARCOL - 3 }));
  check(r.undetermined === 1 && exitCode(r) === 3,
        '缺少裁切/透明度依据 → 阻塞，不借旧元数据猜测');

  console.log(failures ? `\nFAIL: 判定逻辑自测有 ${failures} 项不通过` : '\nPASS: 判定逻辑自测全部通过（不合格/通过/无需滚动/缺依据/关条/非纵向/依据错位 都判得出来）');
  fs.rmSync(tmp, { recursive: true, force: true });
  return failures ? 1 : 0;
}

function exitCode(r) {
  if (r.bad) return 1;
  if (r.undetermined) return 3;
  if (!r.listCards) return 2;
  return 0;
}

function main() {
  if (process.argv.includes('--self-test')) return selfTest();
  const CARDS = parseCardMap(loadCardMapText());
  const dir = process.argv[2] || path.join(__dirname, 'out');
  const r = runAudit(dir, CARDS);
  r.lines.forEach(l => console.log(l));
  console.log(`\n合计 ${r.audited} 处滚动条可判定（${r.pages} 个页面），` +
              `内容不需要滚动 ${r.noScroll} 处，应用关掉条子 ${r.barOff} 处，非纵向 ${r.noVert} 处，` +
              `裁切不可见 ${r.clipped} 处，未判定 ${r.undetermined} 处，不合格 ${r.bad} 处。\n` +
              `覆盖面：${r.listCards} 张列表卡的直接子列表 + 嵌套滚动区 ${r.nestedTotal} 个` +
              `（其中 ${r.nestedScrolling} 个真的溢出/画了条，逐条判定；其余无溢出 → 不需要滚动）。\n` +
              `判据：内容→条 >= ${GAP_MIN}px 且 条→卡片内缘 >= ${EDGE_MIN}px；` +
              `判定依据来自二进制自己吐出的 [bar] 行（LVGL lv_obj_get_scrollbar_area + 真实滚动余量），` +
              (r.basis ? `并按帧取自 ${r.basis}（${r.basisFrames} 帧命中 / ${r.basisStale} 帧缺依据）。`
                       : `**没有逐帧表**（${r.basisStale} 帧缺依据）。`));
  if (r.undetermined) {
    console.error(`有 ${r.undetermined} 处未判定：拿不到"该不该有条/条在哪一列"的硬依据，` +
                  `不许当通过 —— 阻塞总闸门（exit 3）。`);
  }
  if (!r.listCards) {
    console.error('未执行滚动条审核：一张列表卡都没有（页面结构变了或缺卡片几何）');
  }
  return exitCode(r);
}

if (require.main === module) process.exit(main());
module.exports = { readPNG, parseCardMap, loadFrameMap, classify, exitCode, runAudit, gutterColumn };
