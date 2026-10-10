/* ==========================================================================
   fixtures.js —— 数据夹具（按 nas/fnos-agent.py 契约造数）
   夹具值不得伪装实时：stale/oldagent/sparse 显式表达可信度与缺失语义。
   注意：各夹具先独立构造成常量再组装 FIXTURES，避免初始化顺序 TDZ。
   ========================================================================== */

// 确定性伪随机（渲染可复现）
function seeded(seed) {
  let s = seed;
  return () => { s = (s * 1103515245 + 12345) & 0x7fffffff; return s / 0x7fffffff; };
}
function series(seed, n, base, amp) {
  const r = seeded(seed); const out = []; let v = base;
  for (let i = 0; i < n; i++) {
    v = Math.max(0, v + (r() - 0.5) * amp);
    out.push(Math.round(v * 10) / 10);
  }
  return out;
}

// 24 路温度：CPU 7 + 网卡 2 + 核显 1 + NVMe 14（dev/ch/dn 全自适应枚举形状）
const TEMPS_24 = [
  { dev: "CPU",      ch: "Package",  dn: "Intel Core i7-8700",        c: 52.0 },
  { dev: "CPU",      ch: "Core 0",   dn: "Intel Core i7-8700",        c: 48.0 },
  { dev: "CPU",      ch: "Core 1",   dn: "Intel Core i7-8700",        c: 47.0 },
  { dev: "CPU",      ch: "Core 2",   dn: "Intel Core i7-8700",        c: 49.0 },
  { dev: "CPU",      ch: "Core 3",   dn: "Intel Core i7-8700",        c: 46.0 },
  { dev: "CPU",      ch: "Core 4",   dn: "Intel Core i7-8700",        c: 50.0 },
  { dev: "CPU",      ch: "Core 5",   dn: "Intel Core i7-8700",        c: 45.0 },
  { dev: "enp1s0",   ch: "PHY",      dn: "Marvell AQC113 10GbE",      c: 58.4 },
  { dev: "enp1s0",   ch: "MAC",      dn: "Marvell AQC113 10GbE",      c: 55.1 },
  { dev: "i915",     ch: "Composite",dn: "Intel UHD Graphics 630",    c: 41.0 },
  { dev: "nvme0n1",  ch: "Composite",dn: "ZHITAI TiPlus7100 1TB",     c: 44.0 },
  { dev: "nvme0n1",  ch: "Sensor 1", dn: "ZHITAI TiPlus7100 1TB",     c: 44.0 },
  { dev: "nvme0n1",  ch: "Sensor 2", dn: "ZHITAI TiPlus7100 1TB",     c: 42.0 },
  { dev: "nvme0n1",  ch: "Sensor 3", dn: "ZHITAI TiPlus7100 1TB",     c: 41.0 },
  { dev: "nvme1n1",  ch: "Composite",dn: "ZHITAI TiPlus7100 1TB",     c: 46.0 },
  { dev: "nvme1n1",  ch: "Sensor 1", dn: "ZHITAI TiPlus7100 1TB",     c: 46.0 },
  { dev: "nvme1n1",  ch: "Sensor 2", dn: "ZHITAI TiPlus7100 1TB",     c: 43.0 },
  { dev: "nvme1n1",  ch: "Sensor 3", dn: "ZHITAI TiPlus7100 1TB",     c: 42.0 },
  { dev: "nvme2n1",  ch: "Composite",dn: "PCIe-8 SSD 512GB",          c: 39.0 },
  { dev: "nvme2n1",  ch: "Sensor 1", dn: "PCIe-8 SSD 512GB",          c: 39.0 },
  { dev: "nvme2n1",  ch: "Sensor 2", dn: "PCIe-8 SSD 512GB",          c: 37.0 },
  { dev: "nvme3n1",  ch: "Composite",dn: "PCIe-8 SSD 512GB",          c: 38.0 },
  { dev: "nvme3n1",  ch: "Sensor 1", dn: "PCIe-8 SSD 512GB",          c: 38.0 },
  { dev: "nvme3n1",  ch: "Sensor 2", dn: "PCIe-8 SSD 512GB",          c: 36.0 },
];

// 老采集端 n-only 形状（无 ch/dn，行收成一行）
const TEMPS_OLD = [
  { n: "coretemp Package", c: 52.0 }, { n: "coretemp Core 0", c: 48.0 },
  { n: "coretemp Core 1", c: 47.0 },  { n: "enp1s0 PHY", c: 58.4 },
  { n: "enp1s0 MAC", c: 55.1 },       { n: "nvme0 Composite", c: 44.0 },
  { n: "nvme1 Composite", c: 46.0 },
];

const VOLS_6 = [
  { mnt: "/",      fs: "ext4", used_gb: 18.4,  total_gb: 58.0,  pct: 32 },
  { mnt: "/vol1",  fs: "btrfs",used_gb: 1640,  total_gb: 3600,  pct: 46 },
  { mnt: "/vol2",  fs: "btrfs",used_gb: 2210,  total_gb: 3600,  pct: 61 },
  { mnt: "/vol3",  fs: "ext4", used_gb: 940,   total_gb: 2000,  pct: 47 },
  { mnt: "/vol4",  fs: "ext4", used_gb: 456,   total_gb: 480,   pct: 95 },
  { mnt: "/vol5",  fs: "btrfs",used_gb: 890,   total_gb: 1400,  pct: 64 },
];

const CONTAINERS_8 = [
  { n: "fnos-agent",    up: true,  s: "running" },
  { n: "netdata",       up: true,  s: "running" },
  { n: "immich-server", up: true,  s: "running" },
  { n: "immich-worker", up: false, s: "exited (1)" },
  { n: "postgres",      up: true,  s: "running" },
  { n: "jellyfin",      up: true,  s: "running" },
  { n: "adguardhome",   up: true,  s: "running" },
  { n: "frpc",          up: true,  s: "running" },
];

function base() {
  return {
    host: "fnos 存储塔", endpoint: "<NAS_IP>:8799", ip: "<NAS_IP>",
    clock: { hhmm: "21:46", date: "10-06", night: false },
    wifi: { bars: 4, label: "Wi-Fi 6" },
    trust: { state: "live", age: "刚刚", poll: { ok: 1284, fail: 0, interval: "1s" } },
    health: { level: "ok", checks: 24, why: "全部检查通过 · 无采集告警", meta: "6 个卷 · 2 个阵列 · 24 路传感器 · 8 个容器" },
    cpu: { pct: 37, cores: 12, load1: 1.42, load5: 1.18, load15: 0.96, procs: 412, runq: 2, peak: 51 },
    mem: { pct: 63, used_gb: 10.1, total_gb: 16.0, swap_used_gb: 0.1, swap_total_gb: 2.0, peak: 67 },
    temp: { max: 58.4, max_dev: "Marvell AQC113 10GbE", max_ch: "PHY", crit_n: 0, warn_n: 0, channels: TEMPS_24 },
    uptime: "2d 07h",
    net: {
      down: 12.4, up: 1.2, both: 13.6,
      rx_total_tb: 1.28, tx_total_gb: 86.4,
      ifname: "enp1s0", link: "10 GbE 全双工",
      http_ms: 42, ok_n: 1284, fail_n: 0, samples: 180,
      down_series: series(7, 60, 12, 6), up_series: series(13, 60, 1.4, 1.2),
      peak_down: 86.2, peak_up: 12.8,
    },
    cpu_series: series(3, 60, 38, 14), mem_series: series(5, 60, 62, 6),
    vols: VOLS_6,
    vol_sum: { used_tb: 6.18, total_tb: 11.1, free_tb: 4.95 },
    raid: [
      { dev: "md0", lvl: "raid6", have: 6, want: 6, ok: true,  state: "active", sync_pct: -1, what: "" },
      { dev: "md1", lvl: "raid1", have: 1, want: 2, ok: false, state: "active", sync_pct: -1, what: "recovery" },
    ],
    disks: [
      { dev: "nvme0n1", rd: 24.2, wr: 12.1 }, { dev: "nvme1n1", rd: 8.4, wr: 42.6 },
      { dev: "nvme2n1", rd: 2.1, wr: 0.4 },   { dev: "nvme3n1", rd: 0.6, wr: 1.2 },
    ],
    containers: CONTAINERS_8,
    alerts: [],
    modules: [
      { name: "cpu",     state: "ok" },   { name: "mem",    state: "ok" },
      { name: "net",     state: "ok" },   { name: "vols",   state: "ok" },
      { name: "raid",    state: "ok" },   { name: "temps",  state: "ok" },
      { name: "docker",  state: "ok" },   { name: "zfs",    state: "na" },
    ],
    mod_note: "8 个采集段 · 全部正常",
  };
}

/* 事件态：满卷 + 降级阵列 + 高温通道（展示告警编码与跳转） */
const FIX_ALERT = (() => {
  const f = base();
  f.health = { level: "crit", checks: 24, why: "存在严重告警 · 1 个卷超过 90% 阈值", meta: "6 个卷 · 2 个阵列 · 24 路传感器 · 8 个容器" };
  f.temp.channels = TEMPS_24.map((t) =>
    t.dev === "enp1s0" && t.ch === "PHY" ? { ...t, c: 74.2 } :
    t.dev === "nvme0n1" && t.ch === "Composite" ? { ...t, c: 76.2 } : t);
  f.temp.max = 76.2; f.temp.max_dev = "ZHITAI TiPlus7100 1TB"; f.temp.max_ch = "Composite";
  f.temp.crit_n = 1; f.temp.warn_n = 1;
  f.vols = VOLS_6.map((v) => (v.mnt === "/vol2" ? { ...v, pct: 84, used_gb: 3020 } : v));
  f.cpu.pct = 47; f.cpu.peak = 82; f.mem.pct = 68;
  f.alerts = [
    { lv: "crit", obj: "存储卷 /vol4", why: "已用 95% · 超过 90% 危险阈值", age: "12 分钟" },
    { lv: "crit", obj: "阵列 md1", why: "降级 1/2 · 盘 sdb1 离线，recovery 待执行", age: "1 小时" },
    { lv: "warn", obj: "温度 ZHITAI TiPlus7100 1TB · Composite", why: "76.2°C · 超过 75°C 危险阈值", age: "3 分钟" },
  ];
  return f;
})();

/* 离线保留旧数据：遮住顶栏仍能辨认局部可信度 */
const FIX_STALE = (() => {
  const g = JSON.parse(JSON.stringify(FIX_ALERT));
  g.trust = { state: "stale", age: "43 秒前", poll: { ok: 1284, fail: 3, interval: "1s" } };
  g.health = { level: "warn", checks: 24, why: "采集端离线 · 保留旧数据，结论不更新", meta: "旧值 · 43 秒前 · 上次成功 21:45:31" };
  return g;
})();

/* 老采集端：温度只有 n（一行形态）、无 dn */
const FIX_OLD = (() => {
  const f = base();
  f.temp = { max: 58.4, max_dev: "enp1s0 PHY", max_ch: "", crit_n: 0, warn_n: 0, channels: TEMPS_OLD };
  f.modules[6] = { name: "temps", state: "stale" };
  f.mod_note = "8 个采集段 · temps 旧值（老版本采集端无设备名）";
  return f;
})();

/* 稀疏：3 卷 / 无容器上报 / 5 路温度（数据自适应） */
const FIX_SPARSE = (() => {
  const f = base();
  f.vols = VOLS_6.slice(0, 3);
  f.vol_sum = { used_tb: 3.86, total_tb: 7.25, free_tb: 3.39 };
  f.containers = [];
  f.temp.channels = TEMPS_24.slice(0, 5);
  f.temp.max = 52.0; f.temp.max_dev = "Intel Core i7-8700"; f.temp.max_ch = "Package";
  f.health = { level: "warn", checks: 14, why: "数据不完整 · 容器采集未上报", meta: "3 个卷 · 1 个阵列 · 5 路传感器 · 容器未上报" };
  f.modules[6] = { name: "docker", state: "missing" };
  f.mod_note = "8 个采集段 · docker 未上报（已关闭或权限不足）";
  f.alerts = [];
  return f;
})();

const FIXTURES = {
  live: base(),
  alert: FIX_ALERT,
  stale: FIX_STALE,
  oldagent: FIX_OLD,
  sparse: FIX_SPARSE,
};
