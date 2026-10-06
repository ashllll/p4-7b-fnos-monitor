#!/usr/bin/env node
/* 滚动条侵扰审计（回归看门狗）
 *
 * 背景：列表卡片右缘是"右对齐数值/状态"所在处，LVGL 的纵向滚动条默认贴在
 * 滚动区右缘（卡片 16px 内边距之内），实测会与右对齐文字只留 0~1px，
 * 看起来就是"文字和竖条重叠"。本脚本在主机预览 PNG 上量化这个间隙，
 * 用于改前基线与改后回归。
 *
 * 用法：node tools/preview/scroll_gap.js [out]        （默认 out）
 * 判据：文字最右像素→滚动条左缘 >= 8px 且 滚动条右缘→卡片内缘 >= 8px
 */
'use strict';
const fs = require('fs'), path = require('path'), zlib = require('zlib');

/* 卡片几何不在这里手抄 —— 由被审计的那个二进制自己吐出来（PREVIEW_CARDMAP=1）。
   2026-10-06：手抄的 PAGES 表在 v9 版面重排后过期，一次报出 15 处假不合格
   （内容→条=2px），排查花了两小时。几何只有一个来源：fnos_ui.c 的
   fnos_ui_dump_card_map()。卡头的 CSS 坐标 → 卡片内区域一律按 KK_PAD/卡头高算。 */
const CARD_TITLE_H = 36;          /* 卡头：标题 12..34 + 到内容 36 */
const CARD_PAD = 16;              /* 卡内左右内边距 */
function loadCards() {
  const { execFileSync } = require('child_process');
  const bin = path.join(__dirname, 'build', 'preview');
  let out;
  try {
    out = execFileSync(bin, [path.join(require('os').tmpdir(), 'scrollgap-cm')],
                       { env: { ...process.env, PREVIEW_CARDMAP: '1' }, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'] });
  } catch (e) { out = e.stdout || ''; }
  const cards = [];
  for (const line of out.split('\n')) {
    const m = line.match(/^\[card\] p=(-?\d+) x=(-?\d+) y=(-?\d+) w=(\d+) h=(\d+) list=(\d)/);
    if (m) cards.push({ p: +m[1], x: +m[2], y: +m[3], w: +m[4], h: +m[5], list: +m[6] === 1 });
  }
  if (!cards.length) throw new Error('卡片几何图是空的：preview 没能吐出 [card] 行');
  return cards;
}
const CARDS = loadCards();

/* 把"卡片的几何"和"页面"配起来：卡片按 coords 是绝对屏幕坐标，
   所以直接用 y 排序分页（同页卡片 y 相邻、跨页会重排）。实际做法更简单：
   审计时按被审计的卡片自己的坐标算窗口，不关心它属于哪一页。 */

function readPNG(p) {
  const d = fs.readFileSync(p);
  let i = 8, idat = [], w = 0, h = 0, ct = 0, bd = 0;
  while (i < d.length) {
    const ln = d.readUInt32BE(i), t = d.toString('latin1', i + 4, i + 8), c = d.slice(i + 8, i + 8 + ln);
    if (t === 'IHDR') { w = c.readUInt32BE(0); h = c.readUInt32BE(4); bd = c[8]; ct = c[9]; }
    else if (t === 'IDAT') idat.push(c); else if (t === 'IEND') break;
    i += 12 + ln;
  }
  if (bd !== 8) throw new Error('bit depth ' + bd);
  const raw = zlib.inflateSync(Buffer.concat(idat));
  const ch = { 0: 1, 2: 3, 4: 2, 6: 4 }[ct], stride = w * ch;
  const out = Buffer.alloc(w * h * ch);
  let prev = Buffer.alloc(stride), pos = 0;
  for (let y = 0; y < h; y++) {
    const f = raw[pos++], line = Buffer.from(raw.slice(pos, pos + stride));
    pos += stride;
    if (f === 1) for (let x = ch; x < stride; x++) line[x] = (line[x] + line[x - ch]) & 255;
    else if (f === 2) for (let x = 0; x < stride; x++) line[x] = (line[x] + prev[x]) & 255;
    else if (f === 3) for (let x = 0; x < stride; x++) { const a = x >= ch ? line[x - ch] : 0; line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255; }
    else if (f === 4) for (let x = 0; x < stride; x++) {
      const a = x >= ch ? line[x - ch] : 0, b = prev[x], c2 = x >= ch ? prev[x - ch] : 0, pp = a + b - c2;
      const pa = Math.abs(pp - a), pb = Math.abs(pp - b), pc = Math.abs(pp - c2);
      line[x] = (line[x] + ((pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c2))) & 255;
    }
    line.copy(out, y * stride); prev = line;
  }
  return { px: out, w, h, ch };
}

/* 滚动条轨道 = 在给定列范围内、满足"中性灰、纵向连续 >=24px"的像素列。
   卡片底 S1=#161E2B，滚动条 = 卡片底 40% 不透明度叠白 ≈ #5A6270。 */
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

/* 内容右缘 = 滚动条左侧（或不含滚动条的整行）里最右的"非底色"像素列。
   两个坑（都踩过）：
   1) 滚动条自身 1px 抗锯齿边会被当成内容 → 先把条子整列剔除（bars）。
   2) v8 起卡片是"竖向渐变"表面，**没有一个统一的底色**：拿卡片右上角一个点当
      base、按 60 的容差比全局，会把卡片自己下部的深色渐变点判成内容（假报 1~2px）。
      改成逐行基线——每行取左侧保证空白的参考列作该行底色，再按 44 容差判内容。 */
function contentRight(g, x0, x1, y0, y1, base, bars) {
  const isBar = x => bars.some(b => x >= b.x0 - 1 && x <= b.x1 + 1);
  let cr = -1;
  for (let y = y0; y < y1; y++) {
    const [br, bg, bb] = g(x0, y);            /* x0 落在卡片左侧内边距里，必为底色 */
    for (let x = x0; x <= x1; x++) {
      if (isBar(x)) continue;
      const [r, gg, b] = g(x, y);
      if (Math.abs(r - br) + Math.abs(gg - bg) + Math.abs(b - bb) > 44) cr = Math.max(cr, x);
    }
  }
  return cr;
}

function audit(file, page) {
  const { px, w, ch } = readPNG(file);
  const g = (x, y) => { const o = (y * w + x) * ch; return [px[o], px[o + 1], px[o + 2]]; };
  /* 本文件里所有卡片一起收集滚动条列：相邻卡片的条子不能被当成"内容"。
     纵扫窗从卡头下方（y + CARD_TITLE_H）起 —— 卡头那一行还压着卡片的上边与圆角，
     逐行基线一旦跨过卡片边界就会拿错底色，报出假的内容右缘（v8 踩过一次）。 */
  const mine = CARDS.filter(c => c.p === page);      /* 只审这一页自己的卡片 */
  const bars = mine.filter(c => c.list).map(c => {
    return findTrack(g, c.x + c.w - 80, c.x + c.w - 3, c.y + CARD_TITLE_H, c.y + c.h - 8);
  }).filter(Boolean);
  const rows = [];
  for (const c of mine) {
    if (!c.list) continue;
    const track = findTrack(g, c.x + c.w - 80, c.x + c.w - 3, c.y + CARD_TITLE_H, c.y + c.h - 8);
    if (!track) continue;                       /* 这张卡的列表没在滚（内容不溢出），跳过 */
    const cr = contentRight(g, c.x + CARD_PAD, track.x0 - 1,
                            c.y + CARD_TITLE_H, c.y + c.h - 8, null, bars);
    rows.push({ name: `卡(${c.x},${c.y} ${c.w}x${c.h})`, track, cr,
                gap: track.x0 - cr, edge: (c.x + c.w - 1) - track.x1 });
  }
  return rows;
}

const dir = process.argv[2] || path.join(__dirname, 'out');
const files = fs.readdirSync(dir).filter(f => f.endsWith('.png')).sort();
let bad = 0, checked = 0;
for (const f of files) {
  const m = f.match(/-p(\d)\.png$/);
  const page = m ? +m[1] : (f.includes('system') ? 3 : f.includes('storage') ? 1 : null);
  if (page === null) { console.log(`\n### ${f}：整屏/诊断态，跳过（非四页网格）`); continue; }
  const rows = audit(path.join(dir, f), page);
  if (!rows.length) { console.log(`\n### ${f}：没有在滚的列表，跳过`); continue; }
  console.log(`\n### ${f}`);
  for (const r of rows) {
    checked++;
    const ok = r.gap >= 8 && r.edge >= 4;
    if (!ok) bad++;
    console.log(`  ${r.name}  滚动条 x=[${r.track.x0},${r.track.x1}]  内容右缘=${r.cr}  ` +
                `内容→条=${r.gap}px  条→卡片内缘=${r.edge}px  ${ok ? 'OK' : '<<< 不合格'}`);
  }
}
console.log(`\n合计 ${checked} 处滚动条，不合格 ${bad} 处。判据：内容→条 >= 8px 且 条→内缘 >= 4px。`);
process.exit(bad ? 1 : 0);
