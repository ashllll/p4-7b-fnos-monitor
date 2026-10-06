#!/usr/bin/env node
// 管理页 JS 的运行时体检：在 Node 里挂一套最小 DOM 垫片，把 index.html 里那段
// 脚本原样跑一遍，并把每个按钮的 onclick 都点一次。
//
// 为什么需要它：管理页是用户看到的主界面，但那 89 项断言全都是用 curl 打接口，
// **页面的 JavaScript 一次都没被执行过**（只有 `node --check` 过了语法）。一个
// 拼错的属性名、一次对 undefined 取属性，语法检查全都发现不了，而用户打开应用
// 就是一片空白或者点了没反应。这里用真实的接口响应（由 test_lifecycle.sh 先从
// 跑着的实例上抓下来）喂给它，任何运行时异常都会带着栈报出来。
//
// 用法：node page_check.js <index.html> <responses.json>
//   responses.json 形如 {"GET /api/state": {...}, "POST /api/config": {...}}
// 退出码非 0 表示页面脚本抛了异常或某个点击处理器挂了。

const fs = require('fs');
const vm = require('vm');
const path = require('path');

const [htmlPath, respPath] = process.argv.slice(2);
if (!htmlPath || !respPath) {
  console.error('用法：node page_check.js <index.html> <responses.json>');
  process.exit(2);
}

const html = fs.readFileSync(htmlPath, 'utf8');
const canned = JSON.parse(fs.readFileSync(respPath, 'utf8'));

// ── 从 HTML 里收集 id 与 nav button，垫片只需要认识这些 ────────────────
const ids = new Set();
for (const m of html.matchAll(/\bid="([^"]+)"/g)) ids.add(m[1]);

// 导航按钮与 section 都要按真实标记建：页面在导航处理器里会读 b.dataset.t、
// 并按 s.id === 's-' + b.dataset.t 去 toggle section。垫片里少给一个属性，
// 报出来的就会是"页面的错"，而其实是垫片的错 —— 这种误报最浪费人。
function attrs(tag) {
  const out = {};
  for (const m of tag.matchAll(/([a-zA-Z-]+)="([^"]*)"/g)) out[m[1]] = m[2];
  return out;
}
const navButtons = [];
for (const m of html.matchAll(/<nav\b[^>]*>([\s\S]*?)<\/nav>/g)) {
  for (const b of m[1].matchAll(/<button\b[^>]*>/g)) navButtons.push(attrs(b[0]));
}
const sections = [];
for (const m of html.matchAll(/<section\b[^>]*>/g)) sections.push(attrs(m[0]));

class El {
  constructor(tag = 'div', id = '') {
    this.tagName = tag.toUpperCase();
    this.id = id;
    this.textContent = '';
    this.innerHTML = '';
    this.value = '';
    this.checked = false;
    this.disabled = false;
    this.style = {};
    this.onclick = null;
    this.dataset = {};
    this._classes = new Set();
    const self = this;
    this.classList = {
      add: (c) => self._classes.add(c),
      remove: (c) => self._classes.delete(c),
      toggle: (c, on) => (on === undefined ? (self._classes.has(c) ? self._classes.delete(c) : self._classes.add(c))
                                           : (on ? self._classes.add(c) : self._classes.delete(c))),
      contains: (c) => self._classes.has(c),
    };
  }
  get className() { return [...this._classes].join(' '); }
  set className(v) { this._classes = new Set(String(v).split(/\s+/).filter(Boolean)); }
  addEventListener() {}
  removeEventListener() {}
  appendChild() {}
  removeChild() {}
  setAttribute() {}
  getAttribute() { return null; }
  querySelector() { return null; }
  querySelectorAll() { return []; }
  focus() {}
  select() {}
  remove() {}
  closest() { return null; }
  insertAdjacentHTML(_pos, html) { this.innerHTML += String(html); }
  insertBefore() {}
  replaceChildren() {}
  // 页面用 matches(':focus') 判断"这个输入框用户正在打字，别覆盖它"。
  // 垫片一律答 false＝没聚焦，正好是页面刚加载时的真实状态。
  matches() { return false; }
  getBoundingClientRect() { return { top: 0, left: 0, width: 0, height: 0 }; }
}

const byId = new Map();
function el(id) {
  if (!byId.has(id)) byId.set(id, new El('div', id));
  return byId.get(id);
}
for (const id of ids) el(id);

// nav 按钮与 section 是**两个**独立的对象图：页面用 querySelectorAll 拿它们，
// 用 getElementById 拿别的。两边共用同一个实例会更真实，但页面没这么用，
// 垫片按页面的用法给即可（section 的 id 也登记进 ids 表，便于后面检查）。
const navEls = navButtons.map((a) => {
  const e = new El('button', a.id || '');
  e.dataset = { ...a };
  if (a.class) e.className = a.class;
  return e;
});
const sectionEls = sections.map((a) => {
  const e = new El('section', a.id || '');
  e.dataset = { ...a };
  if (a.class) e.className = a.class;
  return e;
});

// 选择器解析：页面用 `document.querySelector('#s-caps .note')` 这种"某个 section
// 里的某个 class"。垫片按 HTML 原文核对一下它存不存在——不存在就是页面真写错了
// （浏览器里会拿到 null 然后炸）。核对结果记进 unresolved，最后一起报。
const unresolved = [];
function selectorExists(sel) {
  const m = sel.match(/^#([\w-]+)\s+\.([\w-]+)$/);
  if (!m) return null;                       // 认不出的写法，交给 unresolved 报
  const sec = new RegExp('<section[^>]*id="' + m[1] + '"[\\s\\S]*?</section>').exec(html);
  if (!sec) return false;
  return new RegExp('class="[^"]*\\b' + m[2] + '\\b').test(sec[0]);
}
const selCache = new Map();

const dom = {
  getElementById: (id) => (ids.has(id) ? el(id) : null),
  querySelector: (sel) => {
    if (!selCache.has(sel)) {
      const found = selectorExists(sel);
      if (found === false) unresolved.push(sel);
      selCache.set(sel, new El('div', sel.replace(/[^\w]/g, '_')));
    }
    return selCache.get(sel);
  },
  querySelectorAll: (sel) => {
    if (/nav\s+button/i.test(sel)) return navEls;
    if (/^section$/i.test(sel.trim())) return sectionEls;
    return [];
  },
  createElement: (t) => new El(t),
  addEventListener: () => {},
  body: new El('body'),
  documentElement: new El('html'),
};

// ── fetch：按 "METHOD path" 查预制响应 ────────────────────────────────
const seen = [];
function fetchStub(url, opts = {}) {
  const method = (opts.method || 'GET').toUpperCase();
  // 页面里的 url 是 BASE + path；BASE 由 location.pathname 推出来，这里直接取路径尾
  let p = String(url).replace(/^https?:\/\/[^/]+/, '');
  for (const base of ['/app/nasscreencompanion', '']) {
    if (base && p.startsWith(base)) { p = p.slice(base.length) || '/'; break; }
  }
  const key = method + ' ' + p;
  seen.push(key);
  if (!(key in canned)) {
    return Promise.resolve({
      ok: false, status: 404,
      json: async () => ({ error: 'page_check 没有为 ' + key + ' 准备响应' }),
      text: async () => '',
    });
  }
  return Promise.resolve({
    ok: true, status: 200,
    json: async () => canned[key],
    text: async () => JSON.stringify(canned[key]),
  });
}

// ── 组装沙箱并跑 ─────────────────────────────────────────────────────
const scriptMatch = html.match(/<script\b[^>]*>([\s\S]*?)<\/script>/);
if (!scriptMatch) { console.error('index.html 里没有 <script>'); process.exit(2); }

const sandbox = {
  document: dom,
  location: { pathname: '/app/nasscreencompanion/', hostname: '127.0.0.1', protocol: 'https:' },
  fetch: fetchStub,
  setInterval: () => 0,
  clearInterval: () => {},
  setTimeout: (fn) => { Promise.resolve().then(fn); return 0; },
  console,
  confirm: () => true,
  alert: () => {},
  navigator: { clipboard: { writeText: async () => {} } },
  JSON, Math, Date, Number, String, Boolean, Array, Object, Promise, Error, RegExp, isNaN, parseInt, parseFloat,
};
sandbox.window = sandbox;
sandbox.globalThis = sandbox;

let failures = 0;
// 页面里有 async 处理器（refresh/savecfg…），它们抛出来是 unhandled rejection。
// 不接住的话 Node 直接退出，一次只能看到第一个错 —— 接住并记账，一轮跑完。
process.on('unhandledRejection', (e) => fail('异步路径抛出未捕获异常', e));
function fail(what, e) {
  failures++;
  console.log(`  \x1b[31m✗\x1b[0m ${what}：${e && e.message ? e.message : e}`);
  if (e && e.stack) {
    const line = e.stack.split('\n').find((l) => l.includes('index.html') || l.includes('evalmachine'));
    if (line) console.log('      ' + line.trim());
  }
}
function ok(what) { console.log(`  \x1b[32m✓\x1b[0m ${what}`); }

const context = vm.createContext(sandbox);
try {
  new vm.Script(scriptMatch[1], { filename: 'index.html' }).runInContext(context);
  ok('页面脚本加载并执行（末尾的 refresh() 也跑了）');
} catch (e) {
  fail('页面脚本加载就抛异常', e);
  process.exit(1);
}

(async () => {
  const settle = () => new Promise((r) => setTimeout(r, 0));
  for (let i = 0; i < 20; i++) await settle();

  // 逐个点击处理器：一条条走，哪条挂了能直接点名
  const clicks = ['gencode', 'cancelcode', 'savecfg', 'rundiag', 'reloadlogs'];
  for (const id of clicks) {
    const node = byId.get(id);
    if (!node || typeof node.onclick !== 'function') {
      fail(`按钮 #${id} 没有挂上 onclick`, new Error('未绑定'));
      continue;
    }
    try {
      await node.onclick();
      for (let i = 0; i < 10; i++) await settle();
      ok(`#${id} 点击处理器跑通`);
    } catch (e) {
      fail(`#${id} 点击处理器抛异常`, e);
    }
  }

  // 导航按钮（切页）
  const navs = navEls;
  let navOk = 0;
  for (const b of navs) {
    if (typeof b.onclick !== 'function') continue;
    try { await b.onclick(); await settle(); navOk++; } catch (e) { fail('导航按钮切换抛异常', e); }
  }
  if (navs.length) ok(`${navOk}/${navs.length} 个导航按钮切换正常`);

  // 内容断言：光"没抛异常"不够。JS 读一个不存在的属性只会得到 undefined，
  // 不会报错 —— 页面会把错的东西安静地画出来（和固件读不到字段就显示 0 一个道理）。
  // 所以这里用**预制响应里的真实值**去核对渲染结果：字段名一旦对不上，这里就红。
  const st = canned['GET /api/state'] || {};
  const pr = canned['GET /api/probe'] || {};
  const lg = canned['GET /api/logs'] || {};
  function contentOf(node) {
    const v = node.value;
    return String((v !== undefined && v !== null && v !== '') ? v
                  : (node.textContent || node.innerHTML || ''));
  }
  const assertedIds = new Set();
  function expect(id, needle, what) {
    assertedIds.add(id);
    const node = byId.get(id);
    if (!node) return fail(`HTML 里没有 #${id}`, new Error('缺元素'));
    if (needle === undefined || needle === null || needle === '') {
      return fail(`无法核对 #${id}（预制响应里没有可用的 ${what}）`, new Error('样本缺失'));
    }
    const text = contentOf(node);
    if (!text.includes(String(needle))) {
      fail(`#${id} 没有渲染出${what}（期望含 ${JSON.stringify(String(needle))}，实际 ${JSON.stringify(text.slice(0, 80))}）`,
           new Error('内容不符'));
    } else {
      ok(`#${id} 渲染了${what}`);
    }
  }
  if (st.config) {
    expect('c-port', st.config.port, '配置里的端口');
    expect('c-interval', st.config.interval, '采集间隔');
    expect('c-auth', st.config.auth_mode, '鉴权模式');
  }
  expect('hver', st.ver, '应用版本');
  if (st.tls && st.tls.fingerprint) {
    // 指纹在 #tlsfp；#tlsline 放的是 subject/到期那些，别把两者搞混
    expect('tlsfp', st.tls.fingerprint.slice(0, 11), '证书指纹前几段');
    expect('tlsline', st.tls.enabled === false ? '明文' : '已启用', '传输加密状态');
    if (st.tls.subject) expect('tlsline', st.tls.subject, '证书 subject');
  }
  expect('capsrc', (pr.sources && pr.sources[0] && pr.sources[0].path) || undefined, '来源路径');
  expect('capmods', (pr.modules && Object.keys(pr.modules)[0]) || undefined, '采集段名');
  // 包用户 uid 落在 `#s-caps .note`（页面用 querySelector 取的那个），不在 $() 的 id 里
  const note = dom.querySelector('#s-caps .note');
  if (note && String(note.innerHTML).includes(String(pr.uid))) {
    ok(`#s-caps .note 渲染了包用户 uid（P0 权限矩阵的关键信息）`);
  } else {
    fail(`#s-caps .note 没渲染出包用户 uid（期望含 ${pr.uid}）`,
         new Error('实际 ' + JSON.stringify(String(note && note.innerHTML).slice(0, 80))));
  }
  // ── 概览页（用户打开看到的第一屏）**原来一条内容断言都没有**
  // 上面那些 cover 了服务设置 / 设备配对 / 能力矩阵 / 诊断四个标签页，
  // 唯独最显眼的概览卡片和采集段表格没人核 —— 它们要是渲染空了，用户第一眼
  // 看到的就是一屏空白，而所有检查照样全绿。
  // **概览卡片的数据来自 `/api/status-now`，不是 `/api/state`**：
  // `refresh()` 里是 `const [st, pr, dg] = await Promise.all([get('/api/status-now'), …])`，
  // 再 `s.snapshot = st; renderCards(s)`。第一版我按 `st.snapshot` 取，5 条断言全报
  // "样本缺失"——**样本没错，是我找错了地方**。
  const snap = canned['GET /api/status-now'] || {};
  const cpu = snap.cpu || {}, mem = snap.mem || {}, net = snap.net || {};
  assertedIds.add('cards');
  const cardsEl = byId.get('cards');
  if (!cardsEl) {
    fail('HTML 里没有 #cards（概览卡片容器）', new Error('缺元素'));
  } else {
    const cards = String(cardsEl.innerHTML || '');
    const want = [
      ['CPU', cpu.pct != null ? Number(cpu.pct).toFixed(1) : undefined, 'CPU 占用'],
      ['内存', mem.total_mb != null ? String(Math.round(mem.total_mb)) : undefined, '内存总量'],
      ['网络', net.if || undefined, '网卡名'],
      ['运行时长', snap.host || undefined, '主机名'],
    ];
    for (const [what, needle, label] of want) {
      if (needle === undefined) { fail(`无法核对概览卡片的${label}（样本里没有）`, new Error('样本缺失')); continue; }
      if (!cards.includes(String(needle))) {
        fail(`概览卡片没有渲染出${label}（期望含 ${JSON.stringify(String(needle))}）`,
             new Error('实际 ' + JSON.stringify(cards.slice(0, 100))));
      } else {
        ok(`概览卡片渲染了${label}`);
      }
    }
    // 六张卡一张都不能少：少了就是布局/渲染路径断了
    const n = (cards.match(/class="card"/g) || []).length;
    if (n < 6) fail(`概览只有 ${n} 张卡片，应当是 6 张`, new Error('卡片缺失'));
    else ok(`概览 6 张卡片都在（${n} 张）`);
  }
  // 采集段表格：至少要有模块名和它的状态
  const mods = snap.modules || {};
  const modName = Object.keys(mods)[0];
  if (modName) {
    expect('modrows', modName, '采集段名');
    expect('modrows', mods[modName].status, '采集段状态');
  } else {
    fail('无法核对 #modrows（样本里没有 modules）', new Error('样本缺失'));
  }

  // ── 配对码显示：**用户唯一要把这串数字抄到板子上的地方**
  // 这一页的其它东西（指纹、加密状态、地址）都核过了，唯独"码本身有没有显示出来"
  // 没人核 —— 它要是没渲染，用户根本配不上，而检查全绿。
  const newCode = canned['POST /api/pairing/new'] || {};
  assertedIds.add('codeval'); assertedIds.add('codewrap');
  const codeval = byId.get('codeval');
  if (!codeval) {
    fail('HTML 里没有 #codeval（配对码显示区）', new Error('缺元素'));
  } else if (newCode.code !== undefined && newCode.code !== null) {
    if (String(codeval.textContent || '').includes(String(newCode.code))) {
      ok(`#codeval 显示了生成出来的配对码`);
    } else {
      fail(`#codeval 没有显示出配对码（期望含 ${JSON.stringify(String(newCode.code))}，` +
           `实际 ${JSON.stringify(String(codeval.textContent).slice(0, 40))}）`, new Error('内容不符'));
    }
  } else {
    fail('无法核对 #codeval（预制响应里没有 POST /api/pairing/new 的 code）', new Error('样本缺失'));
  }
  // 点过「生成配对码」之后，码区必须从 display:none 变成可见，
  // 否则码是渲染了、但用户看不见
  const codewrap = byId.get('codewrap');
  if (codewrap && /none/.test(String(codewrap.style && codewrap.style.display))) {
    fail('点过「生成配对码」之后 #codewrap 仍然是 display:none —— 码用户看不见', new Error('未展开'));
  } else if (codewrap) {
    ok('#codewrap 在生成配对码后已展开');
  }

  // ── 页面地址与已配对设备表
  expect('paddr', (st.listen && st.listen.tcp) || undefined, '开发板要连的地址');
  // 空表也要核：全新装的实例本来就没有已配对设备，页面该给出**空状态文案**。
  // （第一版写成"没有设备就 fail"，结果在套件里必红——那不是页面坏了，
  //   是我把"样本里没有"当成了"页面没渲染"。两件事要分开。）
  const devs = (st.devices || []);
  if (devs.length) {
    expect('devrows', devs[0].name || devs[0].last4, '已配对设备');
    expect('devrows', devs[0].last4, '设备的令牌尾号');
  } else {
    expect('devrows', '还没有设备配对', '空设备表的提示');
  }

  // ── 剩下几个"会被 JS 填、却没人核"的地方。用一次机械对账找出来的：
  //   把 index.html 里所有 `$('x').textContent|innerHTML|value =` 的元素列出来，
  //   减去 page_check 断言过的，剩下的就是这一批。**这类漏网是靠"问一句还有谁"
  //   找出来的，不是靠碰巧想起来。**
  const col = st.collector || {};
  const ready = col.ready && col.age_s != null && col.age_s < 30;
  // 顶栏那句一眼可见的状态：装好了没配对 / 采集是不是正常，用户最先看这里
  if (ready) {
    expect('hhint', st.uid, '顶栏里的包用户 uid');
  } else {
    expect('hhint', '采集异常', '采集异常时的顶栏提示');
  }
  expect('hseq', 'seq ' + (col.seq || 0), '顶栏的采样序号');
  // 诊断页那张表：出问题时用户就是照它逐项看的
  const dg2 = canned['GET /api/diagnostics'] || {};
  const firstCheck = (dg2.checks || [])[0];
  if (firstCheck) {
    expect('diagrows', firstCheck.name, '诊断项名称');
    if (firstCheck.detail) expect('diagrows', String(firstCheck.detail).slice(0, 8), '诊断项详情');
  } else {
    fail('无法核对 #diagrows（预制响应里没有 diagnostics.checks）', new Error('样本缺失'));
  }
  // 设置页剩下四个控件也要落地（前三个核过了）
  if (st.config) {
    expect('c-bind', st.config.bind, '绑定地址');
    expect('c-hist', st.config.hist_len, '历史长度');
    expect('c-docker', st.config.docker_enabled ? '1' : '0', '容器采集开关');
    expect('c-tls', st.config.tls_enabled === false ? '0' : '1', '传输加密开关');
  }
  // 路径表：用户在 NAS 上找日志、找证书全靠它
  const paths = st.paths || {};
  const firstPath = Object.keys(paths)[0];
  if (firstPath) {
    expect('paths', String(paths[firstPath]).slice(0, 10), '安装/配置/数据目录');
  } else {
    fail('无法核对 #paths（预制响应里没有 paths）', new Error('样本缺失'));
  }
  // 配对页那段可以直接复制粘贴的 curl：抄错了用户会以为是自己环境的问题
  if (st.config && String(byId.get('paircurl') && byId.get('paircurl').textContent || '').length) {
    expect('paircurl', st.config.port, 'curl 示例里的端口');
  } else {
    fail('无法核对 #paircurl（它是空的或者元素不存在）', new Error('未渲染'));
  }

  // 状态圆点：颜色本身就是结论（绿=好、红=坏），别让它悄悄一直是灰的
  const expectClass = (id, want, what) => {
    assertedIds.add(id);
    const el = byId.get(id);
    if (!el) return fail(`HTML 里没有 #${id}`, new Error('缺元素'));
    const cls = String((el.className && el.className.baseVal) || el.className || '');
    if (cls.split(/\s+/).includes(want)) ok(`#${id} 的状态点是「${want}」（${what}）`);
    else fail(`#${id} 的状态点不是「${want}」（实际 class=${JSON.stringify(cls)}，${what}）`,
              new Error('状态点不对'));
  };
  expectClass('hdot', ready ? 'ok' : 'err', '采集是否正常');
  expectClass('tlsdot', st.tls && st.tls.enabled !== false ? 'ok' : 'err', '传输加密是否生效');

  // ── 错误路径：管理接口挂掉的时候，页面到底给用户看什么
  // 上面所有断言走的都是"一切正常"。而 `refresh()` 的第一段就是
  //   `if(s.error){ $('hdot').className='dot err'; $('hhint').textContent = s.error; return; }`
  // —— **这条分支从来没被渲染过**。它要是坏了，用户看到的是一屏陈旧数据配一个绿点，
  // 完全不知道管理接口已经不通了。
  const savedState = canned['GET /api/state'];
  const errText = '管理接口不可用（page_check 注入）';
  canned['GET /api/state'] = { error: errText };
  try {
    await context.refresh();
    for (let i = 0; i < 10; i++) await settle();
    expectClass('hdot', 'err', '管理接口出错时的状态点');
    expect('hhint', errText, '管理接口出错时的顶栏提示');
  } catch (e) {
    fail('注入管理接口错误后 refresh() 抛异常', e);
  } finally {
    canned['GET /api/state'] = savedState;
  }

  // ── 「传输加密」卡片的另外两个分支：**用户在出事时最需要看的就是这两屏**
  // 上面核的是"一切正常"那一支（`enabled=true`）。另外两支——
  //   ① TLS 该启用却没启用（证书坏了/生成失败）；
  //   ② 配置里主动关掉了（明文排障）——
  // 从来没被渲染过。这两屏写错的话，用户看到的是"传输加密：已启用"配一个红点，
  // 或者一片空白。
  const savedTls = (savedState && savedState.tls) || {};
  try {
    // ① 启用失败
    canned['GET /api/state'] = Object.assign({}, savedState, {
      tls: { configured: true, enabled: false, error: '证书文件读不出来（page_check 注入）' },
    });
    await context.refresh();
    for (let i = 0; i < 10; i++) await settle();
    expect('tlsline', '未能启用', 'TLS 启用失败时的提示');
    expect('tlsline', '证书文件读不出来（page_check 注入）', 'TLS 失败的具体原因');
    expect('tlsline', '明文 HTTP', 'TLS 失败时的明文警告');
    expectClass('tlsdot', 'err', 'TLS 启用失败时的状态点');
    // ② 配置里主动关掉
    canned['GET /api/state'] = Object.assign({}, savedState, {
      tls: { configured: false, enabled: false },
    });
    await context.refresh();
    for (let i = 0; i < 10; i++) await settle();
    expect('tlsline', '已关闭', '配置里关掉 TLS 时的提示');
    expect('tlsline', '明文 HTTP', '关掉 TLS 时的明文警告');
  } catch (e) {
    fail('注入 TLS 异常状态后 refresh() 抛异常', e);
  } finally {
    canned['GET /api/state'] = Object.assign({}, savedState, { tls: savedTls });
  }

  const firstLog = (lg.lines || [])[0];
  expect('logs', firstLog ? firstLog.slice(0, 8) : '（无日志）', '日志尾巴');

  // ── 机械对账：**凡是页面会用 JS 填内容的元素，都必须有人核**
  // 这一批（顶栏状态、诊断表、四个设置控件、路径表、curl 示例）就是靠这条问出来的：
  // 把 `$('x').textContent|innerHTML|value =` 的目标列出来，减去断言过的，剩下的
  // 全是从没被检查过的渲染路径。手动问一次有用，但要它**一直有用**就得自动化。
  // 允许清单故意留空：新加一个会填内容的元素、却没写断言，这里就会红。
  const NOT_ASSERTED_OK = new Set([
    // 例：'some-id',   // 为什么它可以不核
  ]);
  const filled = new Set();
  const fillRe = /\$\('([a-z0-9-]+)'\)\.(?:textContent|innerHTML|value|className)\s*=/g;
  let fm;
  while ((fm = fillRe.exec(html)) !== null) filled.add(fm[1]);
  const silent = [...filled].filter((id) => !assertedIds.has(id) && !NOT_ASSERTED_OK.has(id));
  if (silent.length) {
    fail('这些元素页面会用 JS 填内容，但没有任何断言核过它们渲染成什么：' + silent.join(', ') +
         '（要么补断言，要么加进 NOT_ASSERTED_OK 并写清理由）', new Error('未覆盖'));
  } else {
    ok(`页面里 ${filled.size} 个会被 JS 填内容的元素，全部有断言核过`);
  }

  if (unresolved.length) {
    for (const sel of unresolved) fail(`页面按选择器 ${sel} 取元素，但 HTML 里找不到`, new Error('选择器落空'));
  }

  const missing = [...new Set(seen)].filter((k) => !(k in canned));
  if (missing.length) {
    // **这条以前只是黄色警告**，而它的后果一点都不黄：没有预制响应时垫片返回一个
    // 带 error 的对象，页面里 `if (!st.error) { … renderCards(s); renderModules(s); }`
    // 直接整段跳过 —— **概览页渲染成空白，而检查照样全绿**。刚刚就是这么发现的：
    // 我漏了 `/api/status-now`，6 条概览断言报"样本缺失"，而在这之前没有任何断言
    // 关心过概览页。缺响应 = 有一条渲染路径根本没被跑到，必须是失败。
    fail('页面请求了没有预制响应的接口：' + missing.join(', ') +
         '（那些渲染路径等于没被跑到）', new Error('样本缺失'));
  }

  console.log(`\n结果：${failures === 0 ? '管理页脚本全部路径跑通 ✓' : failures + ' 处异常'}`);
  process.exit(failures ? 1 : 0);
})();
