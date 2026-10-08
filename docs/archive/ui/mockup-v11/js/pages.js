/* ==========================================================================
   pages.js —— 7 屏渲染（数据夹具 → HTML）
   页面问题（每页一个）：
   总览=NAS 现在要处理什么？ 存储=哪个卷/阵列有风险？ 网络=收发多少、最近怎么变？
   系统=哪个服务/硬件要关注？ 温度=是谁在热？ 诊断=为什么连不上/指标异常？ 配对=下一步做什么？
   ========================================================================== */

const U = (n) => `${n}rem`;
const esc = (s) => String(s).replace(/[&<>]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" }[c]));
const thrUsage = (p) => (p >= 90 ? "crit" : p >= 80 ? "warn" : "");
const thrTemp = (c) => (c >= 75 ? "crit" : c >= 60 ? "warn" : "");

/* ---------------- 共享构件 ---------------- */

function micro(text, kind = "") {
  return `<span class="micro ${kind}"><span class="dot"></span>${esc(text)}</span>`;
}

function track(pct, kind, ticks = [80, 90]) {
  return `<div class="track">
    <div class="fill ${kind}" style="width:${pct}%"></div>
    ${ticks.map((t) => `<div class="tick" style="left:${t}%"></div>`).join("")}
  </div>`;
}

/* KPI 副读数：固定两行，每行一个完整语义单元（行内单行省略），
   避免“… · 峰”与“82%”被自动折行拆到两行（口径与数值必须同行） */
function subLines(lines) {
  return lines.filter(Boolean).map((l) => `<span class="sub-l">${l}</span>`).join("");
}

function kpi({ label, swatch, value, unit, sub, pct, kind = "", stale = false }) {
  return `<div class="kpi ${stale ? "stale" : ""}">
    <div class="lbl">${swatch ? `<span class="swatch" style="background:${swatch}"></span>` : ""}${esc(label)}</div>
    <div class="figure"><span class="v">${esc(value)}</span><span class="u">${esc(unit)}</span></div>
    <div class="sub">${sub}</div>
    ${pct != null ? track(pct, kind) : `<div class="track" style="visibility:hidden"></div>`}
  </div>`;
}

function bay({ led = "", l1, l2 = "", value, unit = "", vclass = "", pct = null, kind = "", stale = false, ledIdent = "", one = false }) {
  return `<div class="bay ${one ? "one" : ""} ${stale ? "stale" : ""}">
    <span class="led ${led} ${ledIdent}"></span>
    <div class="name">
      <div class="l1">${l1}</div>
      ${l2 ? `<div class="l2">${l2}</div>` : ""}
      ${pct != null ? track(pct, kind, [80, 90]) : ""}
    </div>
    <div class="val"><span class="v ${vclass}">${esc(value)}</span>${unit ? `<span class="u">${esc(unit)}</span>` : ""}</div>
  </div>`;
}

function ev({ lv, obj, why, age, stale = false }) {
  const lvText = { crit: "严重", warn: "注意", info: "信息" }[lv];
  return `<div class="ev ${lv} ${stale ? "stale" : ""}">
    <span class="lv">${lvText}</span>
    <div class="body"><span class="obj">${esc(obj)}</span><span class="why">${esc(why)}</span></div>
    <span class="age">${esc(age)}</span>
  </div>`;
}

/* 量程自适应：窗口峰值 ×1.25 向上取整到「好看」的刻度（1/1.5/2/3/5/7.5/10 × 10^n）。
   固定量程会让低流量曲线贴底（100 MB/s 量程下 12 MB/s 只占 12% 高度 = 图表九成留白）。 */
function niceCeil(v) {
  if (!(v > 0)) return 1;
  const p = Math.pow(10, Math.floor(Math.log10(v)));
  const n = v / p;
  const s = n <= 1 ? 1 : n <= 1.5 ? 1.5 : n <= 2 ? 2 : n <= 3 ? 3 : n <= 5 ? 5 : n <= 7.5 ? 7.5 : 10;
  return s * p;
}
function fmtRange(v) { return v >= 10 ? String(Math.round(v)) : String(Math.round(v * 10) / 10); }

/* 趋势：亮线 + 面积 + 稀疏参考线 + 虚线基线（状态纹理，不复刻桌面图表） */
function trendSvg(seriesList, ymax) {
  const W = 1000, H = 300;
  const px = (i, n) => (i / (n - 1)) * W;
  const py = (v) => H - (v / ymax) * (H - 8) - 4;
  const guides = [0.25, 0.5, 0.75].map((g) =>
    `<line x1="${W * g}" y1="0" x2="${W * g}" y2="${H}" stroke="rgba(255,255,255,0.06)" stroke-width="1" vector-effect="non-scaling-stroke"/>`).join("");
  const plots = seriesList.map(({ color, data }) => {
    const pts = data.map((v, i) => `${px(i, data.length)},${py(v)}`).join(" ");
    const area = `0,${H} ${pts} ${W},${H}`;
    return `<polygon points="${area}" fill="${color}" opacity="0.13"/>
      <polyline points="${pts}" fill="none" stroke="${color}" stroke-width="2" vector-effect="non-scaling-stroke" stroke-linejoin="round"/>`;
  }).join("");
  return `<svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="none">
    ${guides}
    <line x1="0" y1="${H - 1}" x2="${W}" y2="${H - 1}" stroke="rgba(255,255,255,0.16)" stroke-dasharray="4 4" vector-effect="non-scaling-stroke"/>
    ${plots}
  </svg>`;
}

function trendBlock({ seriesList, ymax, yTop, readouts, xFrom = "窗口开始", xTo = "现在" }) {
  return `<div class="trend">
    <div class="plot">
      ${trendSvg(seriesList, ymax)}
      <div style="position:absolute;left:0;top:0;font:400 var(--t-meta)/1 var(--font-ui);color:var(--silk-3)">${esc(yTop)}</div>
      <div style="position:absolute;left:0;bottom:0;font:400 var(--t-meta)/1 var(--font-ui);color:var(--silk-3)">0</div>
    </div>
    <div class="xaxis"><span>${esc(xFrom)}</span><span>${esc(xTo)}</span></div>
    <div class="readout">${readouts}</div>
  </div>`;
}

const readoutItem = (color, label, text) =>
  `<span class="item"><span class="swatch" style="background:${color}"></span>${esc(label)} <b>${esc(text)}</b></span>`;

/* ---------------- 骨架 ---------------- */

function renderHead(f) {
  const t = f.trust;
  const cap = {
    live: `<span class="capsule live">每秒更新</span>`,
    stale: `<span class="capsule stale">保留旧数据 · ${esc(t.age)}</span>`,
    off: `<span class="capsule off">采集端离线</span>`,
    warm: `<span class="capsule warm">等待数据</span>`,
  }[t.state];
  const lamp = { live: "dot-ok", stale: "dot-warn", off: "dot-crit", warm: "dot-idle" }[t.state];
  const word = { live: "在线", stale: "旧数据", off: "离线", warm: "等待" }[t.state];
  return `<header class="head">
    <div class="mark">FN</div>
    <div class="head-id">
      <div class="head-host">${esc(f.host)}</div>
      <div class="head-ep">${esc(f.endpoint)}</div>
    </div>
    <div class="head-spacer"></div>
    <div class="lightbar">
      <span class="lamp"><span class="dot ${lamp}"></span><span class="word">${word}</span></span>
      ${cap}
      <span class="lamp"><span class="wifi">${[0, 1, 2, 3].map((i) => `<i class="${i < f.wifi.bars ? "" : "off"}"></i>`).join("")}</span><span class="word">${esc(f.wifi.label)}</span></span>
      <span class="clock"><span class="hhmm">${esc(f.clock.hhmm)}</span><span class="date">${esc(f.clock.date)}</span></span>
    </div>
  </header>`;
}

const NAV = [
  ["overview", "总览"], ["storage", "存储"], ["network", "网络"], ["system", "系统"], ["temp", "温度"],
];
const NAV_ICON = {
  overview: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><rect x="3" y="3" width="8" height="8" rx="1.5"/><rect x="13" y="3" width="8" height="5" rx="1.5"/><rect x="13" y="10" width="8" height="11" rx="1.5"/><rect x="3" y="13" width="8" height="8" rx="1.5"/></svg>`,
  storage: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><rect x="3" y="4" width="18" height="6" rx="1.5"/><rect x="3" y="14" width="18" height="6" rx="1.5"/><circle cx="7" cy="7" r="1" fill="currentColor" stroke="none"/><circle cx="7" cy="17" r="1" fill="currentColor" stroke="none"/></svg>`,
  network: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><path d="M4 17l5-6 4 4 7-8"/><path d="M4 20h16"/></svg>`,
  system: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><rect x="5" y="5" width="14" height="14" rx="2"/><path d="M9 9h6v6H9z"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3"/></svg>`,
  temp: `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><path d="M10 4a2 2 0 1 1 4 0v9a4.5 4.5 0 1 1-4 0z"/><circle cx="12" cy="17.5" r="1.6" fill="currentColor" stroke="none"/></svg>`,
};

function renderRail(page, f) {
  const nav = NAV.map(([id, label]) => {
    const sel = id === page ? "sel" : "";
    const alarm = id === "storage" && f.alerts.some((a) => a.obj.includes("存储卷")) ? "alarm" : "";
    return `<div class="nav-item ${sel} ${alarm}">
      <span class="nav-led"></span>
      <span class="nav-ico">${NAV_ICON[id]}</span>
      <span class="nav-lbl">${label}</span>
    </div>`;
  }).join("");
  return `<nav class="app-rail">
    ${nav}
    <div class="rail-foot">
      <div class="util ${page === "diag" ? "sel" : ""}">
        <span class="nav-ico"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><circle cx="12" cy="12" r="3"/><path d="M12 3v3M12 18v3M3 12h3M18 12h3M5.6 5.6l2.1 2.1M16.3 16.3l2.1 2.1M18.4 5.6l-2.1 2.1M7.7 16.3l-2.1 2.1"/></svg></span>
        <span class="nav-lbl">诊断</span>
      </div>
      <div class="util pair-pending ${page === "pair" ? "sel" : ""}">
        <span class="nav-led"></span>
        <span class="nav-ico"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><path d="M10 14a4 4 0 0 1 0-5.7l5.7-5.7 3 3-2 2 1.5 1.5-2 2L14 8.5l-2 2"/></svg></span>
        <span class="nav-lbl">配对</span>
      </div>
    </div>
  </nav>`;
}

function renderFoot(f) {
  const t = f.trust;
  const alert = f.alerts[0];
  const band = alert
    ? `<div class="alert-band ${alert.lv}">
        <span class="dot ${alert.lv === "crit" ? "dot-crit" : "dot-warn"}"></span>
        <span class="txt"><b>${esc(alert.obj)}</b> · ${esc(alert.why)}</span>
        <span class="jump">查看 ›</span>
      </div>`
    : `<div class="alert-band">
        <span class="dot dot-ok"></span>
        <span class="txt">${t.state === "stale" ? "告警状态未知 · 保留旧数据" : "无告警事件 · 24 路传感器均在阈值内"}</span>
      </div>`;
  return `<footer class="foot">
    <span class="poll">轮询 ${esc(t.poll.interval)} · ok ${t.poll.ok} · fail ${t.poll.fail}</span>
    ${band}
  </footer>`;
}

/* ---------------- P0 总览 ---------------- */
function renderOverview(f) {
  const stale = f.trust.state === "stale";
  const h = f.health;
  const top = f.alerts[0];
  const eventCard = top
    ? `<div class="top-event">
        <span class="lv" style="color:${top.lv === "crit" ? "var(--led-crit)" : "var(--led-warn)"}">最高优先级事件 · ${top.lv === "crit" ? "严重" : "注意"}</span>
        <span class="obj">${esc(top.obj)}</span>
        <span class="why">${esc(top.why)}</span>
        <div class="foot-row"><span class="age">${esc(top.age)}</span><span class="jump" style="margin-left:auto;font:500 var(--t-meta)/1 var(--font-ui);color:var(--focus)">查看 ›</span></div>
      </div>`
    : `<div class="top-event" style="border-color:var(--etch)">
        <span class="lv" style="color:var(--silk-3)">事件区</span>
        <span class="obj" style="color:var(--silk-2)">暂无需要处理的事项</span>
        <span class="why">${esc(h.meta)}</span>
        <div class="foot-row">${micro(stale ? `旧值 · ${f.trust.age}` : "全部检查通过", stale ? "old" : "")}</div>
      </div>`;

  const kpis = `<div class="kpi-row">
    ${kpi({
      label: "CPU 使用率", swatch: "var(--id-cpu)", value: stale ? "47" : String(f.cpu.pct), unit: "%",
      sub: subLines([`${f.cpu.cores} 核 · 负载 ${f.cpu.load1.toFixed(2)} · 队列 ${f.cpu.runq}`, `峰 ${f.cpu.peak}%`]),
      pct: f.cpu.pct, kind: thrUsage(f.cpu.pct), stale,
    })}
    ${kpi({
      label: "内存", swatch: "var(--id-mem)", value: String(f.mem.pct), unit: "%",
      sub: subLines([`${f.mem.used_gb} / ${f.mem.total_gb} GB · 换 ${f.mem.swap_used_gb} / ${f.mem.swap_total_gb} G`, `峰 ${f.mem.peak}%`]),
      pct: f.mem.pct, kind: thrUsage(f.mem.pct), stale,
    })}
    ${kpi({
      label: "最高温度", swatch: "var(--id-temp)", value: f.temp.max.toFixed(1), unit: "°C",
      sub: subLines([
        `${f.temp.crit_n ? `<b style="color:var(--led-crit)">危险 ${f.temp.crit_n} 路</b> · ` : ""}${f.temp.warn_n ? `<b style="color:var(--led-warn)">注意 ${f.temp.warn_n} 路</b> · ` : ""}${esc(f.temp.max_ch)}`,
        esc(f.temp.max_dev),
      ]),
      pct: Math.min(100, Math.round(f.temp.max)), kind: thrTemp(f.temp.max), stale,
    })}
    ${kpi({
      label: "网络下行", swatch: "var(--id-down)", value: f.net.down.toFixed(1), unit: "MB/s",
      sub: subLines([`上行 ${f.net.up.toFixed(1)} MB/s · 累计收 ${f.net.rx_total_tb} TB`, `发送 ${f.net.tx_total_gb} GB`]),
      pct: Math.min(100, Math.round((f.net.down / f.net.peak_down) * 100)), kind: "", stale,
    })}
  </div>`;

  return `<div class="page" style="grid-template-rows:120rem 140rem 1fr">
    <div class="health-band">
      <div class="health-main">
        <span class="health-lamp ${h.level}"></span>
        <span class="health-word ${h.level}">${h.level === "crit" ? "严重" : h.level === "warn" ? "注意" : "正常"}</span>
        <div class="health-why">
          <span class="why">${esc(h.why)}</span>
          <span class="meta">${esc(h.meta)}</span>
        </div>
        <span class="btn" style="margin-left:auto">查看原因 ›</span>
      </div>
      ${eventCard}
    </div>
    ${kpis}
    <div class="row" style="grid-template-columns:2fr 1fr">
      <div class="card">
        <div class="card-head"><span class="card-title">资源趋势 · 最近 180 次采集</span><span class="card-note">${stale ? "曲线在断流处停止推进" : "较早 — 现在"}</span></div>
        ${trendBlock({
          seriesList: [
            { color: "var(--id-cpu)", data: f.cpu_series },
            { color: "var(--id-mem)", data: f.mem_series },
          ],
          ymax: 100, yTop: "100%",
          readouts:
            readoutItem("var(--id-cpu)", "CPU", `${f.cpu.pct}% · 峰 ${f.cpu.peak}%`) +
            readoutItem("var(--id-mem)", "内存", `${f.mem.pct}% · 峰 ${f.mem.peak}%`),
        })}
      </div>
      <div class="card">
        <div class="card-head"><span class="card-title">存储风险</span>
          <span class="card-note">显示 <span data-fitnote>${Math.min(4, f.vols.length)}</span> / ${f.vols.length} · 见「存储」页</span></div>
        <div class="pool poolrail" data-pool data-mincol="200" data-fit>
          ${f.vols.slice().sort((a, b) => b.pct - a.pct).map((v) => bay({
            led: v.pct >= 90 ? "crit" : v.pct >= 80 ? "warn" : "",
            l1: esc(v.mnt), l2: `已用 ${(v.used_gb / 1000).toFixed(2)} TB / ${(v.total_gb / 1000).toFixed(1)} TB · ${esc(v.fs)}`,
            value: String(v.pct), unit: "%", vclass: thrUsage(v.pct), stale,
          })).join("")}
        </div>
        <div style="padding-top:var(--s1)" class="summary-line">
          <span>最满 <b>${Math.max(...f.vols.map((v) => v.pct))}%</b></span>
          <span>阵列 <b>${f.raid.filter((r) => r.ok).length}/${f.raid.length}</b> 正常</span>
        </div>
      </div>
    </div>
  </div>`;
}

/* ---------------- P1 存储 ---------------- */
function renderStorage(f) {
  const stale = f.trust.state === "stale";
  const riskVols = f.vols.filter((v) => v.pct >= 80);
  const segColors = ["#2E6E8E", "#2F7E77", "#3A6FA8", "#4B6B93", "#2C5F72", "#3E7C6B"];
  const stacked = f.vols.map((v, i) => {
    const w = (v.total_gb / f.vols.reduce((s, x) => s + x.total_gb, 0)) * 100;
    return `<div style="width:${w}%;height:100%;border-right:2rem solid var(--chassis);display:flex">
      <div style="width:${v.pct}%;background:${segColors[i % segColors.length]}"></div>
      <div style="flex:1;background:var(--slot)"></div>
    </div>`;
  }).join("");

  return `<div class="page" style="grid-template-rows:112rem 1fr">
    <div class="row" style="grid-template-columns:3fr 2fr">
      <div class="card">
        <div class="card-head"><span class="card-title">容量总览（背景信息）</span>
          <span class="card-note">${stale ? micro(`旧值 · ${f.trust.age}`, "old") : micro("每秒更新")}</span></div>
        <div class="card-body" style="gap:var(--s2)">
          <div style="display:flex;align-items:baseline;gap:var(--s3)">
            <span class="num" style="font:600 var(--t-hero)/1 var(--font-num);color:var(--silk-1)">${f.vol_sum.used_tb}</span>
            <span style="font:500 var(--t-sub)/1 var(--font-ui);color:var(--silk-3)">TB 已用</span>
            <span class="summary-line" style="margin-left:auto">
              <span>总计 <b>${f.vol_sum.total_tb} TB</b></span><span class="sep">·</span>
              <span>可用 <b>${f.vol_sum.free_tb} TB</b></span><span class="sep">·</span>
              <span>按卷分段</span>
            </span>
          </div>
          <div style="display:flex;height:10rem;border-radius:2rem;overflow:hidden;border:1rem solid var(--etch)">${stacked}</div>
        </div>
      </div>
      <div class="card" style="border-color:${riskVols.length || f.raid.some((r) => !r.ok) ? "rgba(255,176,32,.35)" : "var(--etch)"}">
        <div class="card-head"><span class="card-title">风险摘要（优先看这里）</span></div>
        <div class="card-body" style="gap:var(--s1)">
          ${riskVols.map((v) => `<div class="summary-line"><span class="led ${v.pct >= 90 ? "crit" : "warn"}" style="width:8rem;height:8rem;border-radius:50%;background:${v.pct >= 90 ? "var(--led-crit)" : "var(--led-warn)"}"></span>
            <span>最满卷 <b>${esc(v.mnt)}</b> 已用 <b>${v.pct}%</b> · 剩 ${v.free_gb ? (v.free_gb / 1000).toFixed(2) : (v.total_gb / 1000 * (1 - v.pct / 100)).toFixed(2)} TB</span></div>`).join("") || `<div class="summary-line"><span>全部卷低于 80% 阈值</span></div>`}
          ${f.raid.filter((r) => !r.ok).map((r) => `<div class="summary-line"><span style="width:8rem;height:8rem;border-radius:50%;background:var(--led-crit)"></span>
            <span>阵列 <b>${esc(r.dev)}</b> 降级 ${r.have}/${r.want} · ${esc(r.what)} 待执行</span></div>`).join("")}
          ${f.raid.filter((r) => r.sync_pct >= 0).map((r) => `<div class="summary-line"><span style="width:8rem;height:8rem;border-radius:50%;background:var(--led-warn)"></span>
            <span>阵列 <b>${esc(r.dev)}</b> ${esc(r.what)} <b>${r.sync_pct}%</b></span></div>`).join("")}
        </div>
      </div>
    </div>
    <div class="row" style="grid-template-columns:3fr 2fr">
      <div class="card">
        <div class="card-head"><span class="card-title">全部存储卷 · ${f.vols.length}</span>
          <span class="card-note">条上刻度 = 80 / 90 阈值</span></div>
      <div class="pool poolrail" data-pool data-mincol="240">
          ${f.vols.map((v) => bay({
            led: thrUsage(v.pct) || "ok",
            l1: esc(v.mnt),
            l2: `已用 ${(v.used_gb / 1000).toFixed(2)} TB / ${(v.total_gb / 1000).toFixed(2)} TB · ${esc(v.fs)}`,
            value: String(v.pct), unit: "%", vclass: thrUsage(v.pct),
            pct: v.pct, kind: thrUsage(v.pct), stale,
          })).join("")}
        </div>
      </div>
      <div class="row" style="grid-template-rows:auto 1fr">
        <div class="card">
          <div class="card-head"><span class="card-title">阵列与同步</span></div>
          <div class="rail-list" style="padding-left:var(--s5)">
            ${f.raid.map((r) => bay({
              led: r.ok ? "ok" : "crit",
              l1: `${esc(r.dev)} · ${esc(r.lvl)}`,
              l2: `${r.have}/${r.want} 在线 · ${r.sync_pct >= 0 ? esc(r.what) + " " + r.sync_pct + "%" : r.ok ? "无同步任务" : "recovery 待执行"}`,
              value: r.ok ? "正常" : "降级", vclass: r.ok ? "" : "crit", stale, one: true,
            })).join("")}
          </div>
        </div>
        <div class="card">
          <div class="card-head"><span class="card-title">磁盘活动</span><span class="card-note">按繁忙度排序</span></div>
          <div class="rail-list" style="padding-left:var(--s5)">
            ${f.disks.map((d) => bay({
              led: d.rd + d.wr > 10 ? "ident" : "", ledIdent: "color:var(--id-store)",
              l1: esc(d.dev), l2: `读 ${d.rd.toFixed(1)} · 写 ${d.wr.toFixed(1)} MB/s`,
              value: (d.rd + d.wr).toFixed(1), unit: "MB/s", stale, one: true,
            })).join("")}
          </div>
        </div>
      </div>
    </div>
  </div>`;
}

/* ---------------- P2 网络 ---------------- */
function renderNetwork(f) {
  const stale = f.trust.state === "stale";
  /* 吞吐量程按「窗口内」峰值自适应（下行/上行共用同一量程，便于两条线互相比较）。
     不并入历史峰值 peak_down（86.2 MB/s）：那是文字读数，把它算进量程会让曲线重新贴底。 */
  const netYmax = niceCeil(Math.max(...f.net.down_series, ...f.net.up_series, 0.1) * 1.25);
  return `<div class="page" style="grid-template-rows:132rem 1fr 92rem">
    <div class="dual-hero">
      <div class="hero-val">
        <span class="lbl" style="color:var(--id-down)"><span class="swatch" style="background:var(--id-down)"></span><span style="color:var(--silk-3)">下行 DOWN</span></span>
        <span class="figure"><span class="v" style="${stale ? "color:var(--silk-3)" : ""}">${f.net.down.toFixed(1)}</span><span class="u">MB/s</span></span>
        <span class="sub">峰值 ${f.net.peak_down} MB/s · 单位档位带迟滞</span>
      </div>
      <div class="hero-val">
        <span class="lbl" style="color:var(--id-up)"><span class="swatch" style="background:var(--id-up)"></span><span style="color:var(--silk-3)">上行 UP</span></span>
        <span class="figure"><span class="v" style="${stale ? "color:var(--silk-3)" : ""}">${f.net.up.toFixed(1)}</span><span class="u">MB/s</span></span>
        <span class="sub">峰值 ${f.net.peak_up} MB/s · byte 不与 bit 混用</span>
      </div>
      <div class="hero-val" style="min-width:200rem">
        <span class="lbl"><span style="color:var(--silk-3)">双向合计</span></span>
        <span class="figure"><span class="v" style="font-size:var(--t-metric)">${f.net.both.toFixed(1)}</span><span class="u">MB/s</span></span>
        <span class="sub">${stale ? micro(`旧值 · ${f.trust.age}`, "old") : micro("LIVE · 1s 采样")}</span>
      </div>
    </div>
    <div class="card">
      <div class="card-head"><span class="card-title">吞吐趋势 · 最近 180 次采集</span>
        <span class="card-note">量程自适应 · 突增后带迟滞回缩</span></div>
      ${trendBlock({
        seriesList: [
          { color: "var(--id-down)", data: f.net.down_series },
          { color: "var(--id-up)", data: f.net.up_series },
        ],
        ymax: netYmax, yTop: `量程 ${fmtRange(netYmax)} MB/s`,
        readouts:
          readoutItem("var(--id-down)", "DOWN", `${f.net.down.toFixed(1)} MB/s · 峰 ${f.net.peak_down}`) +
          readoutItem("var(--id-up)", "UP", `${f.net.up.toFixed(1)} MB/s · 峰 ${f.net.peak_up}`),
      })}
    </div>
    <div class="row" style="grid-template-columns:1fr 1fr">
      <div class="card">
        <div class="card-head"><span class="card-title">链路与累计</span></div>
        <div class="card-body" style="justify-content:center;gap:var(--s2)">
          <div class="summary-line"><b style="font-size:var(--t-sub)">${esc(f.net.ifname)}</b>
            <span class="sep">·</span><span>${esc(f.net.link)}</span>
            <span style="margin-left:auto" class="summary-line"><span>累计收 <b>${f.net.rx_total_tb} TB</b></span><span class="sep">·</span><span>发 <b>${f.net.tx_total_gb} GB</b></span></span>
          </div>
          <div class="summary-line"><span>数值来源 = NAS 网卡计数器，不是板端 HTTP 速率</span></div>
        </div>
      </div>
      <div class="card">
        <div class="card-head"><span class="card-title">采集链路质量（次级）</span></div>
        <div class="card-body" style="justify-content:center;gap:var(--s2)">
          <div class="summary-line">
            <span>HTTP <b>${f.net.http_ms} ms</b></span><span class="sep">·</span>
            <span>成功率 <b>${((f.net.ok_n / (f.net.ok_n + f.net.fail_n)) * 100).toFixed(0)}%</b>（${f.net.ok_n}/${f.net.ok_n + f.net.fail_n}）</span><span class="sep">·</span>
            <span>曲线 <b>${f.net.samples}</b> 点</span>
          </div>
          <div class="summary-line"><span>这是板端→NAS 的取数耗时，与上方网卡吞吐分开表述</span></div>
        </div>
      </div>
    </div>
  </div>`;
}

/* ---------------- 自适应助手（数据量 × 视口） ---------------- */
const MOD_TXT = { ok: "正常", stale: "旧值", missing: "未上报", denied: "权限不足", error: "错误", disabled: "已关闭", na: "无此项" };
const MOD_CLS = { ok: "", stale: "warn", missing: "warn", denied: "crit", error: "crit", disabled: "", na: "" };
const EV_ROWH = 55;                       // ev 行实测高（rem，含 padding 与两行文案）
const EV_GAP = 8;                          // .events gap（--s2）
/* 采集段状态摘要：由数据算出，不再用固定文案（"全部正常" 与 zfs=无此项 曾互相矛盾） */
function modNote(f) {
  const odd = f.modules.filter((m) => m.state !== "ok");
  return odd.length
    ? `${f.modules.length - odd.length} 正常 · ${odd.map((m) => `${m.name} ${MOD_TXT[m.state] || m.state}`).join("、")}`
    : "全部正常";
}
/* 同屏条数口径：容器高 40vh ÷ 行高，与 .events 的 max-height 保持一致 */
function evNote(n) {
  if (!n) return "按严重度排序，严重不被截断";
  const U = parseFloat(getComputedStyle(document.documentElement).fontSize) || 1;
  const vis = Math.min(n, Math.max(1, Math.floor((window.innerHeight * 0.4 + EV_GAP) / ((EV_ROWH + EV_GAP) * U))));
  return `${vis < n ? `显示前 ${vis} / ${n}` : `显示 ${n} / ${n}`} · 按严重度排序，严重不被截断`;
}

/* ---------------- P3 系统 ---------------- */
function renderSystem(f) {
  const stale = f.trust.state === "stale";
  const down = f.containers.filter((c) => !c.up);
  return `<div class="page" style="grid-template-rows:auto 1fr">
    <div class="card" style="border-color:${f.alerts.length ? "rgba(255,176,32,.3)" : "var(--etch)"}">
      <div class="card-head"><span class="card-title">异常与事件 · ${f.alerts.length}</span>
        <span class="card-note">${evNote(f.alerts.length)}</span></div>
      <div class="events">
        ${f.alerts.length ? f.alerts.map((a) => ev({ ...a, stale })).join("") :
          `<div class="empty">${stale ? "告警状态未知 · 保留旧数据" : "暂无告警事件"}</div>`}
      </div>
    </div>
    <div class="row" style="grid-template-columns:1fr 1fr">
      <div class="card">
        <div class="card-head"><span class="card-title">容器服务 · ${f.containers.length ? `${f.containers.filter((c) => c.up).length}/${f.containers.length} 运行` : "未上报"}</span>
          <span class="card-note">停止的排前 · 名称+同行状态</span></div>
        ${f.containers.length ? `<div class="pool poolrail" data-pool data-mincol="200">
          ${[...f.containers].sort((a, b) => Number(b.up) - Number(a.up)).map((c) => bay({
            led: c.up ? "ok" : "warn",
            l1: esc(c.n), l2: c.up ? "" : `意外退出 · ${esc(c.s)}`,
            value: c.up ? "运行中" : "已停止", vclass: c.up ? "" : "warn", stale,
          })).join("")}
        </div>` : `<div class="rail-list"><div class="empty">未采集到容器 · 采集段 docker ${MOD_TXT[(f.modules.find((m) => m.name === "docker") || {}).state] || "未上报"}</div></div>`}
      </div>
      <div class="card">
        <div class="card-head"><span class="card-title">硬件温度 · 最热 4 路</span>
          <span class="card-note">危险 <b style="color:var(--led-crit)">${f.temp.crit_n}</b> 路 · 注意 <b style="color:var(--led-warn)">${f.temp.warn_n}</b> 路 · 共 ${f.temp.channels.length} 路</span></div>
        <div class="pool poolrail" data-pool data-mincol="240">
          ${f.temp.channels.slice().sort((a, b) => b.c - a.c).slice(0, 4).map((t) => bay({
            led: thrTemp(t.c) || "ident", ledIdent: "color:var(--id-temp)",
            l1: `${esc(t.ch)} · ${esc(t.dn || t.dev)}`,
            l2: `${esc(t.dev)} · 阈值 60 / 75°C`,
            value: t.c.toFixed(1), unit: "°C", vclass: stale ? "" : thrTemp(t.c), stale,
          })).join("")}
        </div>
      </div>
    </div>
  </div>`;
}

/* ---------------- P4 温度（数据自适应：行形态由 ch 有无决定） ---------------- */
function renderTemp(f) {
  const stale = f.trust.state === "stale";
  const chans = f.temp.channels.slice().sort((a, b) => {
    const ka = (a.dev || a.n || "") + "\u0000" + (a.ch || "");
    const kb = (b.dev || b.n || "") + "\u0000" + (b.ch || "");
    return ka < kb ? -1 : ka > kb ? 1 : 0;
  });
  const hasCh = chans.some((t) => t.ch);
  return `<div class="page" style="grid-template-rows:68rem 1fr">
    <div class="card" style="padding-top:var(--s2);padding-bottom:var(--s2);justify-content:center;gap:4rem">
      <div class="summary-line">
        <b>${chans.length}</b> 路传感器 · 最热 <b>${esc(f.temp.max_dev)}${f.temp.max_ch ? " · " + esc(f.temp.max_ch) : ""}</b>
        <b style="color:${thrTemp(f.temp.max) === "crit" ? "var(--led-crit)" : "var(--silk-1)"}">${f.temp.max.toFixed(1)}°C</b>
        ${stale ? micro(`旧值 · ${f.trust.age}`, "old") : micro("每秒更新")}
      </div>
      <div class="summary-line" style="color:var(--silk-3)">
        <span>危险 ≥75°C：<b style="color:var(--led-crit)">${f.temp.crit_n}</b> 路</span><span class="sep">·</span>
        <span>注意 ≥60°C：<b style="color:var(--led-warn)">${f.temp.warn_n}</b> 路</span><span class="sep">·</span>
        <span>行形态由数据决定（${hasCh ? "有通道名 · 两行" : "老采集端 · 一行"}），设备名不缩写</span>
      </div>
    </div>
    <div class="card">
      <div class="card-head"><span class="card-title">温度传感器 · 全部通道</span>
        <span class="card-note">顺序 = 设备名 → 通道名，不随温度跳动</span></div>
      <div class="card-body">
        <div class="pool poolrail" data-pool data-mincol="260">
          ${chans.map((t) => hasCh && t.ch ? bay({
            led: thrTemp(t.c) || "ident", ledIdent: "color:var(--id-temp)",
            l1: esc(t.dn || t.dev), l2: esc(t.ch),
            value: t.c.toFixed(1), unit: "°C", vclass: stale ? "" : thrTemp(t.c), stale,
          }) : bay({
            led: thrTemp(t.c) || "ident", ledIdent: "color:var(--id-temp)",
            l1: esc(t.dn || t.n || t.dev),
            value: t.c.toFixed(1), unit: "°C", vclass: stale ? "" : thrTemp(t.c), stale,
          })).join("")}
        </div>
      </div>
    </div>
  </div>`;
}

/* ---------------- 诊断 ---------------- */
function renderDiag(f) {
  const stale = f.trust.state === "stale";
  return `<div class="page" style="grid-template-rows:96rem 88rem 1fr 60rem">
    <div class="card">
      <div class="card-head"><span class="card-title">活动目标与最近成功</span>
        <span class="card-note">端点取自 NVS 实际生效配置，不是编译模板</span></div>
      <div class="card-body" style="justify-content:center;gap:var(--s2)">
        <div class="summary-line">
          <span>目标 <b style="font-size:var(--t-sub)">${esc(f.endpoint)}</b></span><span class="sep">·</span>
          <span>最近成功 <b>21:45:31</b></span><span class="sep">·</span>
          <span>数据年龄 <b style="color:${stale ? "var(--led-warn)" : "var(--silk-1)"}">${esc(f.trust.age)}</b></span>
          ${stale ? micro("已断连", "bad") : micro("在线")}
        </div>
      </div>
    </div>
    <div class="card" style="border-color:${stale ? "rgba(255,176,32,.35)" : "var(--etch)"}">
      <div class="card-head"><span class="card-title">原因与建议</span></div>
      <div class="card-body" style="justify-content:center;gap:var(--s1)">
        ${stale
          ? `<div class="summary-line"><b style="color:var(--led-warn)">TLS 握手失败</b><span>证书多半换了，需要重新配对</span></div>
             <div class="summary-line" style="color:var(--silk-3)"><span>建议：先确认 NAS 时间与板端一致；仍失败则在配对页重新核对指纹</span></div>`
          : `<div class="summary-line"><b style="color:var(--led-ok)">链路正常</b><span>1s 轮询 · 连续 ${f.trust.poll.ok} 次成功</span></div>
             <div class="summary-line" style="color:var(--silk-3)"><span>无需处理；异常时这里显示可操作的中文原因</span></div>`}
      </div>
    </div>
    <div class="card">
      <div class="card-head"><span class="card-title">全部采集段 · ${f.modules.length}</span>
        <span class="card-note">${esc(modNote(f))}</span></div>
      <div class="pool poolrail" data-pool data-mincol="200">
        ${f.modules.map((m) => `<div class="kv"><span class="k">${esc(m.name)}</span><span class="v ${MOD_CLS[m.state] || ""}">${MOD_TXT[m.state] || m.state}</span></div>`).join("")}
      </div>
    </div>
    <div class="card" style="padding:var(--s2) var(--s4)">
      <div class="summary-line" style="color:var(--silk-3)">
        <span>技术详情</span><span class="sep">·</span>
        <span>协议 v2</span><span class="sep">·</span>
        <span>板端内存 211 KB 空闲</span><span class="sep">·</span>
        <span>HTTP ${f.net.http_ms} ms</span><span class="sep">·</span>
        <span>ZFS ARC 未采集</span><span class="sep">·</span>
        <span>展开 ›</span>
      </div>
    </div>
  </div>`;
}

/* ---------------- 配对（每阶段一个主操作） ---------------- */
function renderPair(f) {
  const poll = (f.trust && f.trust.poll) || {};
  return `<div class="page" style="grid-template-rows:48rem 1fr">
    <div class="card" style="padding:0 var(--s4);flex-direction:row;align-items:center;gap:var(--s5)">
      <span class="summary-line"><b style="font-size:var(--t-sub)">连接 NAS · 第 2/4 步</b></span>
      <div class="steps" style="flex-direction:row;gap:var(--s5);align-items:center">
        <span class="step done"><span class="no">✓</span><span class="txt"><span class="t1">网络已连接</span></span></span>
        <span style="color:var(--silk-4)">—</span>
        <span class="step now"><span class="no">2</span><span class="txt"><span class="t1">输入配对码</span></span></span>
        <span style="color:var(--silk-4)">—</span>
        <span class="step"><span class="no">3</span><span class="txt"><span class="t1">核对指纹</span></span></span>
        <span style="color:var(--silk-4)">—</span>
        <span class="step"><span class="no">4</span><span class="txt"><span class="t1">完成</span></span></span>
      </div>
    </div>
    <div class="row" style="grid-template-columns:2fr 3fr">
      <div class="card">
        <div class="card-head"><span class="card-title">现在要做的事</span></div>
        <div class="card-body" style="gap:var(--s3)">
          <div class="summary-line"><span>目标 NAS</span><b style="margin-left:auto;font-size:var(--t-sub)">192.0.2.10:8799</b></div>
          <div class="summary-line"><span>伴侣服务</span><b style="margin-left:auto;color:var(--led-ok)">已发现 · 8798</b></div>
          <div class="summary-line"><span>采集链路</span><b style="margin-left:auto">轮询 ${esc(poll.interval || "1s")} · 成功 ${poll.ok ?? 0} / 失败 ${poll.fail ?? 0}</b></div>
          <div style="height:1rem;background:var(--etch)"></div>
          <div class="summary-line" style="color:var(--silk-2)"><span>在 NAS 的「屏幕伴侣」页面上读取 6 位配对码，输入下面的槽位。配对码 300 秒内有效。</span></div>
          <div class="summary-line"><b style="color:var(--silk-2)">读不到码？</b></div>
          <div class="summary-line" style="color:var(--silk-3)"><span>确认 NAS 上「屏幕伴侣」页已打开（码只在页面可见时生成），且本机与 NAS 在同一网段；网段不同时改用 IP 直连。</span></div>
          <div style="margin-top:auto">${micro("等待输入 · 300s", "na")}</div>
        </div>
      </div>
      <div class="card">
        <div class="card-head"><span class="card-title">配对码</span>
          <span class="card-note">错误提示紧贴输入区，不与步骤文案混排</span></div>
        <div class="card-body" style="display:grid;grid-template-rows:auto 1fr;gap:var(--s4);align-items:stretch">
          <div class="code-slots" style="justify-content:center">
            <div class="code-slot filled">4</div><div class="code-slot filled">1</div><div class="code-slot filled">7</div>
            <div class="code-slot empty"></div><div class="code-slot empty"></div><div class="code-slot empty"></div>
          </div>
          <div style="display:flex;flex-direction:row;gap:var(--s4);align-items:stretch;justify-content:center;min-height:0">
            <div class="numpad">
              ${[1, 2, 3, 4, 5, 6, 7, 8, 9].map((n) => `<div class="key">${n}</div>`).join("")}
              <div class="key util">清除</div><div class="key">0</div><div class="key util">退格</div>
            </div>
            <div style="display:flex;flex-direction:column;gap:var(--s2);justify-content:center;width:160rem">
              <span class="btn primary" style="height:44rem;justify-content:center">下一步</span>
              <span class="btn ghost" style="height:44rem;justify-content:center">返回</span>
              <span style="font:400 var(--t-meta)/1.3 var(--font-ui);color:var(--led-warn);text-align:center">输入的码已过期 · 请重新读取</span>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>`;
}

const PAGES = {
  overview: renderOverview, storage: renderStorage, network: renderNetwork,
  system: renderSystem, temp: renderTemp, diag: renderDiag, pair: renderPair,
};

function renderApp(page, fixture) {
  const f = FIXTURES[fixture] || FIXTURES.live;
  const showRail = page !== "pair";
  document.getElementById("root").innerHTML = `
    ${renderHead(f)}
    ${showRail ? renderRail(page, f) : `<nav class="app-rail" style="visibility:hidden"></nav>`}
    <main class="app-body">${PAGES[page](f)}</main>
    ${renderFoot(f)}`;
  document.title = `v11 mockup · ${page} · ${fixture}`;
}
