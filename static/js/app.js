// QuickFolderSize フロントエンド。
// python/mainwindow.py + model.py + navmodel.py + delegate.py + utils.py + i18n.py の
// UIロジックをHTML/CSS/JS + WebView2 JSON WebMessageブリッジへ移植したもの。

const APP_VERSION = 'v1.0.0';

const I18N = {
  window_title: { ja: 'QuickFolderSize - フォルダ使用容量ビューワー', en: 'QuickFolderSize - Folder Size Viewer' },
  addr_placeholder: { ja: 'スキャンするフォルダのパスを入力...', en: 'Enter the folder path to scan...' },
  btn_scan: { ja: 'スキャン', en: 'Scan' },
  lbl_elapsed: { ja: 'スキャン時間', en: 'Scan Time' },

  menu_file: { ja: 'ファイル', en: 'File' },
  act_open: { ja: 'フォルダを開く...', en: 'Open Folder...' },
  act_rescan: { ja: '再スキャン', en: 'Rescan' },
  act_report: { ja: 'レポート作成...', en: 'Export Report...' },
  act_quit: { ja: '終了', en: 'Exit' },
  menu_help: { ja: 'ヘルプ', en: 'Help' },
  act_about: { ja: 'バージョン情報...', en: 'About...' },

  about_title: { ja: 'バージョン情報', en: 'About' },
  about_html: {
    ja: '<h3>QuickFolderSize</h3><p><b>Ver. {version}</b></p><hr>' +
        '<p><b>【開発環境】</b><br>・C++17 (MinGW-w64 / g++)<br>・WebView2 (Microsoft Edge WebView2 Runtime)<br>' +
        '・Win32 API (FindFirstFileW/FindNextFileW, IFileDialog)</p>' +
        '<p><b>【制作者】</b><br>GitHub: maktak-105</p>',
    en: '<h3>QuickFolderSize</h3><p><b>Ver. {version}</b></p><hr>' +
        '<p><b>[Development Environment]</b><br>- C++17 (MinGW-w64 / g++)<br>- WebView2 (Microsoft Edge WebView2 Runtime)<br>' +
        '- Win32 API (FindFirstFileW/FindNextFileW, IFileDialog)</p>' +
        '<p><b>[Author]</b><br>GitHub: maktak-105</p>',
  },

  scan_time_measuring: { ja: '計測中...', en: 'Measuring...' },

  err_folder_not_found: { ja: 'フォルダが見つかりません:\n{path}', en: 'Folder not found:\n{path}' },
  dlg_browse_title: { ja: 'フォルダを選択', en: 'Select Folder' },

  report_title: { ja: 'レポート作成', en: 'Export Report' },
  report_need_scan: { ja: '先にスキャンを実行してください。', en: 'Please run a scan first.' },
  report_done_text: { ja: 'レポートを保存しました:\n{path}', en: 'Report saved:\n{path}' },
  report_save_err_text: { ja: '保存に失敗しました:\n{path}', en: 'Failed to save:\n{path}' },

  col_name: { ja: '名前', en: 'Name' },
  col_size: { ja: 'サイズ', en: 'Size' },
  col_ratio: { ja: '割合', en: 'Ratio' },
  col_files: { ja: 'ファイル数', en: 'Files' },
  col_mtime: { ja: '更新日時', en: 'Modified' },

  report_h1: { ja: '# フォルダ使用容量レポート', en: '# Folder Size Report' },
  report_col_item: { ja: '項目', en: 'Item' },
  report_col_value: { ja: '値', en: 'Value' },
  report_path: { ja: 'パス', en: 'Path' },
  report_scan_datetime: { ja: 'スキャン日時', en: 'Scan Date' },
  report_total_size: { ja: '合計サイズ', en: 'Total Size' },
  report_subfolder_count: { ja: 'サブフォルダ数', en: 'Subfolders' },
  report_file_count_recursive: { ja: 'ファイル数（再帰）', en: 'Files (recursive)' },
  report_h2_structure: { ja: '## フォルダ構成', en: '## Folder Structure' },
  report_structure_desc: {
    ja: 'サイズ降順・階層表示。ファイルは各フォルダ内にインデントで記載。',
    en: 'Sorted by size (descending), shown hierarchically. Files are indented within each folder.',
  },
  report_file_suffix: { ja: 'ファイル: {count:,}個', en: 'Files: {count:,}' },
  report_generated_at: { ja: '生成日時', en: 'Generated at' },
};

let currentLang = 'ja';

function tr(key, params) {
  const entry = I18N[key];
  if (!entry) return key;
  const template = entry[currentLang];
  if (!params) return template;
  return template.replace(/\{(\w+)(:,)?\}/g, (m, name, comma) => {
    let val = params[name];
    if (val === undefined) return m;
    if (comma) val = Number(val).toLocaleString('en-US');
    return String(val);
  });
}

function formatSize(bytes) {
  if (bytes < 0) return '—';
  const units = [['GB', 1024 ** 3], ['MB', 1024 ** 2], ['KB', 1024]];
  for (const [unit, threshold] of units) {
    if (bytes >= threshold) return (bytes / threshold).toFixed(1) + ' ' + unit;
  }
  return Math.round(bytes) + ' B';
}

function pad2(n) { return String(n).padStart(2, '0'); }

function formatDateTime(ms, withSeconds) {
  const d = new Date(ms);
  let s = `${d.getFullYear()}/${pad2(d.getMonth() + 1)}/${pad2(d.getDate())} ${pad2(d.getHours())}:${pad2(d.getMinutes())}`;
  if (withSeconds) s += ':' + pad2(d.getSeconds());
  return s;
}

function baseName(path) {
  const p = path.replace(/[\\/]+$/, '');
  const idx = Math.max(p.lastIndexOf('\\'), p.lastIndexOf('/'));
  const name = idx >= 0 ? p.substring(idx + 1) : p;
  return name || path;
}

function normPath(p) {
  if (!p) return '';
  let s = p.toLowerCase();
  if (!s.endsWith('\\')) s += '\\';
  return s;
}

// ===== WebView2 ブリッジ =====

function getWebView() {
  return (window.chrome && window.chrome.webview) ? window.chrome.webview : null;
}

function sendCmd(obj) {
  const wv = getWebView();
  if (wv) wv.postMessage(obj);
}

function handleNativeMessage(event) {
  let msg;
  try {
    msg = (typeof event.data === 'string') ? JSON.parse(event.data) : event.data;
  } catch (e) { return; }
  if (!msg || !msg.type) return;
  switch (msg.type) {
    case 'drives': onDrives(msg.data); break;
    case 'browse_result': onBrowseResult(msg.path); break;
    case 'scan_placeholder': onScanPlaceholder(msg); break;
    case 'scan_progress': onScanProgress(msg); break;
    case 'scan_finished': onScanFinished(msg); break;
    case 'scan_error': onScanError(msg); break;
    case 'nav_children': onNavChildren(msg); break;
    case 'export_result': onExportResult(msg); break;
  }
}

// ===== 状態 =====

let drivesData = [];
let scanTree = null;        // 現在のスキャン結果ルート(完全な入れ子JSON)
let pathIndex = new Map();  // normPath(path) -> node (scanTree全体から構築)
let displayNode = null;     // 現在テーブルに表示中のノード(その children を表示)
let scanStartPath = '';
let scanStartTime = null;
let sortState = { col: 'size', dir: 'desc' };
let expandedPaths = new Set();
let navChildrenCache = new Map();
let pendingNavExpand = null;

// ===== ドライブ / アドレスバー =====

function onDrives(data) {
  drivesData = data || [];
  renderNavRoots();
}

function updateDriveLabel(drive) {
  const el = document.getElementById('drive-label');
  if (!drive) { el.textContent = '—'; return; }
  const labelPart = drive.label ? ` (${drive.label})` : '';
  el.textContent = `${drive.path}${labelPart}  ${formatSize(drive.used_gb * 1024 ** 3)} / ${formatSize(drive.total_gb * 1024 ** 3)}`;
}

function updateDriveLabelFromPath(path) {
  const letter = (path.length >= 2 && path[1] === ':') ? path.substring(0, 2).toUpperCase() + '\\' : null;
  const d = drivesData.find(x => x.path.toUpperCase() === letter);
  updateDriveLabel(d || null);
}

// ===== ナビペイン =====

function renderNavRoots() {
  const ul = document.getElementById('nav-tree');
  ul.innerHTML = '';
  for (const d of drivesData) {
    const label = d.label ? `${d.path} (${d.label})` : d.path;
    ul.appendChild(buildNavNode(d.path, label));
  }
}

function buildNavNode(path, label) {
  const li = document.createElement('li');
  li.className = 'nav-node';

  const row = document.createElement('div');
  row.className = 'nav-row';
  const twisty = document.createElement('span');
  twisty.className = 'nav-twisty';
  twisty.textContent = '▶';
  row.appendChild(twisty);
  const nameSpan = document.createElement('span');
  nameSpan.className = 'nav-name';
  nameSpan.textContent = label;
  row.appendChild(nameSpan);
  li.appendChild(row);

  const childUl = document.createElement('ul');
  childUl.className = 'nav-children hidden';
  li.appendChild(childUl);

  twisty.addEventListener('click', (e) => {
    e.stopPropagation();
    toggleNavNode(path, childUl, twisty);
  });
  nameSpan.addEventListener('click', () => onNavNodeClick(path));

  return li;
}

function toggleNavNode(path, childUl, twisty) {
  const isHidden = childUl.classList.contains('hidden');
  if (!isHidden) {
    childUl.classList.add('hidden');
    twisty.textContent = '▶';
    return;
  }
  childUl.classList.remove('hidden');
  twisty.textContent = '▼';
  const cached = navChildrenCache.get(normPath(path));
  if (cached) {
    renderNavChildren(childUl, cached);
    return;
  }
  childUl.innerHTML = '<li class="nav-loading">…</li>';
  pendingNavExpand = { path: normPath(path), childUl };
  sendCmd({ cmd: 'nav_expand', path });
}

function onNavChildren(msg) {
  navChildrenCache.set(normPath(msg.path), msg.data);
  if (pendingNavExpand && pendingNavExpand.path === normPath(msg.path)) {
    renderNavChildren(pendingNavExpand.childUl, msg.data);
    pendingNavExpand = null;
  }
}

function renderNavChildren(childUl, children) {
  childUl.innerHTML = '';
  for (const c of children) {
    childUl.appendChild(buildNavNode(c.path, c.name));
  }
}

function onNavNodeClick(path) {
  document.getElementById('addr-input').value = path;
  const node = pathIndex.get(normPath(path));
  if (node) {
    setDisplayNode(node, path, true);
    return;
  }
  startScan(path);
}

// ===== スキャン制御 =====

function startScan(path) {
  scanStartPath = path;
  scanStartTime = new Date();
  document.getElementById('addr-input').value = path;
  setScanTime('measuring');
  setReportEnabled(false);
  sendCmd({ cmd: 'scan', path });
}

function onScanError(msg) {
  setScanTime('idle');
  alert(tr('err_folder_not_found', { path: msg.path }));
}

function onScanPlaceholder(msg) {
  updateDriveLabel(msg.drive);
  const children = (msg.data || []).map(e => ({
    name: e.name, path: e.path, size: 0, file_count: 0,
    mtime_ms: 0, is_accessible: true, is_dir: e.is_dir, children: [],
  })).sort((a, b) => {
    if (a.is_dir !== b.is_dir) return a.is_dir ? -1 : 1;
    return a.name.toLowerCase().localeCompare(b.name.toLowerCase());
  });
  scanTree = {
    name: baseName(msg.path), path: msg.path, size: 0, file_count: 0,
    mtime_ms: 0, is_accessible: true, is_dir: true, children,
  };
  pathIndex = new Map();
  indexTree(scanTree);
  setDisplayNode(scanTree, msg.path, true);
}

function onScanProgress(msg) {
  if (!scanTree || normPath(msg.root) !== normPath(scanStartPath)) return;
  const node = msg.data;
  indexTree(node);
  const idx = scanTree.children.findIndex(c => normPath(c.path) === normPath(node.path));
  if (idx >= 0) scanTree.children[idx] = node; else scanTree.children.push(node);
  scanTree.size = scanTree.children.reduce((s, c) => s + c.size, 0);
  if (displayNode === scanTree) renderTreeTable();
}

function onScanFinished(msg) {
  // 表示は JS 側の経過時間で統一する。
  // native の elapsed_seconds は scan_directory 本体だけなので、JSON 化・
  // WebView2 転送の分だけ短く、完了時に 20s → 10s のように巻き戻って見えていた。
  const elapsed = scanStartTime
    ? (Date.now() - scanStartTime.getTime()) / 1000
    : (msg.elapsed_seconds || 0);
  setScanTime('done', elapsed);
  scanTree = msg.data;
  pathIndex = new Map();
  indexTree(scanTree);
  setDisplayNode(scanTree, msg.root, true);
  setReportEnabled(true);
}

function indexTree(node) {
  pathIndex.set(normPath(node.path), node);
  for (const c of node.children) indexTree(c);
}

function setDisplayNode(node, addrPath, resetSort) {
  displayNode = node;
  expandedPaths = new Set();
  if (addrPath !== undefined) document.getElementById('addr-input').value = addrPath;
  if (resetSort) sortState = { col: 'size', dir: 'desc' };
  updateDriveLabelFromPath(node.path);
  renderTreeTable();
  updateSortIndicators();
}

// ===== メインツリーテーブル =====

function sortNodes(nodes, parentSize) {
  const { col, dir } = sortState;
  const mul = dir === 'asc' ? 1 : -1;
  const arr = nodes.slice();
  arr.sort((a, b) => {
    let av, bv;
    switch (col) {
      case 'name':
        return mul * a.name.toLowerCase().localeCompare(b.name.toLowerCase());
      case 'files':
        av = a.is_dir ? a.file_count : -1; bv = b.is_dir ? b.file_count : -1; break;
      case 'mtime':
        av = a.mtime_ms; bv = b.mtime_ms; break;
      case 'bar':
      case 'size':
      default:
        av = a.size; bv = b.size; break;
    }
    return mul * (av - bv);
  });
  return arr;
}

function renderTreeTable() {
  const tbody = document.getElementById('tree-body');
  tbody.innerHTML = '';
  if (!displayNode) return;
  const rows = sortNodes(displayNode.children, displayNode.size);
  for (const child of rows) renderRow(tbody, child, 0, displayNode.size);
}

function renderRow(tbody, node, depth, parentSize) {
  const tr = document.createElement('tr');
  if (!node.is_accessible) tr.classList.add('inaccessible');
  tr.addEventListener('click', (e) => {
    if (e.target.classList.contains('row-twisty')) return;
    document.getElementById('addr-input').value = node.path;
  });

  const ratio = parentSize > 0 ? (node.size / parentSize * 100) : 0;
  const hasChildren = node.is_dir && node.children && node.children.length > 0;

  const tdName = document.createElement('td');
  tdName.className = 'col-name';
  tdName.style.paddingLeft = (depth * 18 + 4) + 'px';
  const twisty = document.createElement('span');
  twisty.className = 'row-twisty';
  twisty.textContent = hasChildren ? (expandedPaths.has(node.path) ? '▼' : '▶') : '';
  if (hasChildren) {
    twisty.addEventListener('click', (e) => {
      e.stopPropagation();
      if (expandedPaths.has(node.path)) expandedPaths.delete(node.path);
      else expandedPaths.add(node.path);
      renderTreeTable();
    });
  }
  tdName.appendChild(twisty);
  const icon = document.createElement('span');
  icon.className = 'row-icon';
  icon.textContent = node.is_dir ? '📁' : '📄';
  tdName.appendChild(icon);
  const nameSpan = document.createElement('span');
  nameSpan.textContent = node.name;
  tdName.appendChild(nameSpan);
  tr.appendChild(tdName);

  const tdSize = document.createElement('td');
  tdSize.className = 'col-size';
  tdSize.textContent = formatSize(node.size);
  tr.appendChild(tdSize);

  const tdBar = document.createElement('td');
  tdBar.className = 'col-bar';
  const barOuter = document.createElement('div');
  barOuter.className = 'size-bar-outer';
  const barInner = document.createElement('div');
  barInner.className = 'size-bar-inner';
  barInner.style.width = Math.max(0, Math.min(100, ratio)) + '%';
  barOuter.appendChild(barInner);
  const barLabel = document.createElement('span');
  barLabel.className = 'size-bar-label';
  barLabel.textContent = ratio.toFixed(1) + '%';
  tdBar.appendChild(barOuter);
  tdBar.appendChild(barLabel);
  tr.appendChild(tdBar);

  const tdFiles = document.createElement('td');
  tdFiles.className = 'col-files';
  tdFiles.textContent = node.is_dir ? node.file_count.toLocaleString('en-US') : '';
  tr.appendChild(tdFiles);

  const tdMtime = document.createElement('td');
  tdMtime.className = 'col-mtime';
  tdMtime.textContent = node.mtime_ms ? formatDateTime(node.mtime_ms, false) : '';
  tr.appendChild(tdMtime);

  tbody.appendChild(tr);

  if (hasChildren && expandedPaths.has(node.path)) {
    for (const c of sortNodes(node.children, node.size)) renderRow(tbody, c, depth + 1, node.size);
  }
}

function updateSortIndicators() {
  document.querySelectorAll('#tree-table thead th').forEach(th => {
    th.classList.remove('sort-asc', 'sort-desc');
    if (th.dataset.col === sortState.col) th.classList.add(sortState.dir === 'asc' ? 'sort-asc' : 'sort-desc');
  });
}

function setupSortHeaders() {
  document.querySelectorAll('#tree-table thead th').forEach(th => {
    th.addEventListener('click', () => {
      const col = th.dataset.col;
      if (sortState.col === col) sortState.dir = sortState.dir === 'asc' ? 'desc' : 'asc';
      else sortState = { col, dir: col === 'name' ? 'asc' : 'desc' };
      renderTreeTable();
      updateSortIndicators();
    });
  });
}

// ===== スキャン時間ボックス =====

let scanTimeKind = 'idle';
let scanTimeElapsed = 0;
let scanTimeTimer = null;

function startScanTimeTicker() {
  stopScanTimeTicker();
  const tick = () => {
    if (scanTimeKind !== 'measuring' || !scanStartTime) return;
    scanTimeElapsed = (Date.now() - scanStartTime.getTime()) / 1000;
    renderScanTime();
  };
  tick();
  scanTimeTimer = setInterval(tick, 200);
}

function stopScanTimeTicker() {
  if (scanTimeTimer !== null) {
    clearInterval(scanTimeTimer);
    scanTimeTimer = null;
  }
}

function setScanTime(kind, elapsed) {
  scanTimeKind = kind;
  if (kind === 'measuring') {
    startScanTimeTicker();
    return;
  }
  stopScanTimeTicker();
  scanTimeElapsed = elapsed || 0;
  renderScanTime();
}

function renderScanTime() {
  const el = document.getElementById('time-val');
  if (scanTimeKind === 'measuring' || scanTimeKind === 'done') {
    el.textContent = scanTimeElapsed.toFixed(2) + 's';
  } else {
    el.textContent = '—';
  }
}

// ===== レポート出力 =====

function generateMdReport(root, scanTime) {
  const dirCount = root.children.filter(c => c.is_dir).length;
  const lines = [];
  lines.push(tr('report_h1'), '');
  lines.push(`| ${tr('report_col_item')} | ${tr('report_col_value')} |`);
  lines.push('|:-----|:---|');
  lines.push(`| ${tr('report_path')} | \`${root.path}\` |`);
  lines.push(`| ${tr('report_scan_datetime')} | ${formatDateTime(scanTime.getTime(), false)} |`);
  lines.push(`| ${tr('report_total_size')} | ${formatSize(root.size)} |`);
  lines.push(`| ${tr('report_subfolder_count')} | ${dirCount.toLocaleString('en-US')} |`);
  lines.push(`| ${tr('report_file_count_recursive')} | ${root.file_count.toLocaleString('en-US')} |`);
  lines.push('', tr('report_h2_structure'), '', tr('report_structure_desc'), '');

  function render(node, depth, parentSize) {
    const indent = '  '.repeat(depth);
    const ratio = parentSize > 0 ? (node.size / parentSize * 100) : 0;
    if (node.is_dir) {
      lines.push(`${indent}- 📁 **${node.name}** — ${formatSize(node.size)} (${ratio.toFixed(1)}%)  ${tr('report_file_suffix', { count: node.file_count })}`);
      for (const c of node.children.slice().sort((a, b) => b.size - a.size)) render(c, depth + 1, node.size);
    } else {
      lines.push(`${indent}- 📄 ${node.name} — ${formatSize(node.size)} (${ratio.toFixed(1)}%)`);
    }
  }
  for (const c of root.children.slice().sort((a, b) => b.size - a.size)) render(c, 0, root.size);

  lines.push('', '---', `*${tr('report_generated_at')}: ${formatDateTime(Date.now(), true)}*`);
  return lines.join('\n');
}

function onExportReport() {
  if (!scanTree) { alert(tr('report_need_scan')); return; }
  const content = generateMdReport(scanTree, scanStartTime || new Date());
  const now = new Date();
  const defaultName = `report_${now.getFullYear()}${pad2(now.getMonth() + 1)}${pad2(now.getDate())}_${pad2(now.getHours())}${pad2(now.getMinutes())}${pad2(now.getSeconds())}.md`;
  sendCmd({ cmd: 'export_report', content, default_name: defaultName });
}

function onExportResult(msg) {
  if (msg.success) alert(tr('report_done_text', { path: msg.path }));
  else alert(tr('report_save_err_text', { path: msg.path }));
}

// ===== ブラウズ / リスキャン =====

function onBrowseClick() {
  sendCmd({ cmd: 'browse', initial: document.getElementById('addr-input').value || 'C:\\' });
}

function onBrowseResult(path) {
  if (path) startScan(path);
}

function onRescan() {
  const path = document.getElementById('addr-input').value.trim();
  if (path) startScan(path);
}

function setReportEnabled(enabled) {
  const el = document.getElementById('act-report');
  if (enabled) el.classList.remove('disabled'); else el.classList.add('disabled');
}

// ===== メニュー / About =====

function setupMenu() {
  document.querySelectorAll('.menu-item').forEach(item => {
    item.addEventListener('click', (e) => {
      e.stopPropagation();
      const dropdown = item.querySelector('.menu-dropdown');
      const isOpen = dropdown.classList.contains('open');
      closeAllMenus();
      if (!isOpen) dropdown.classList.add('open');
    });
  });
  document.addEventListener('click', closeAllMenus);

  document.querySelectorAll('.menu-action').forEach(action => {
    action.addEventListener('click', () => {
      closeAllMenus();
      handleMenuAction(action.dataset.action);
    });
  });

  document.getElementById('about-close').addEventListener('click', hideAbout);
  document.getElementById('about-overlay').addEventListener('click', (e) => {
    if (e.target.id === 'about-overlay') hideAbout();
  });
}

function closeAllMenus() {
  document.querySelectorAll('.menu-dropdown').forEach(d => d.classList.remove('open'));
}

function handleMenuAction(action) {
  switch (action) {
    case 'open': onBrowseClick(); break;
    case 'rescan': onRescan(); break;
    case 'report':
      if (!document.getElementById('act-report').classList.contains('disabled')) onExportReport();
      break;
    case 'quit': sendCmd({ cmd: 'quit' }); break;
    case 'about': showAbout(); break;
  }
}

function showAbout() {
  document.getElementById('about-content').innerHTML = tr('about_html', { version: APP_VERSION });
  document.getElementById('about-overlay').classList.remove('hidden');
}

function hideAbout() {
  document.getElementById('about-overlay').classList.add('hidden');
}

// ===== スプリッター =====

function setupSplitter() {
  const splitter = document.getElementById('splitter');
  const navCol = document.getElementById('nav-col');
  let dragging = false;
  splitter.addEventListener('mousedown', () => { dragging = true; document.body.style.cursor = 'col-resize'; });
  window.addEventListener('mousemove', (e) => {
    if (!dragging) return;
    const rect = document.getElementById('main').getBoundingClientRect();
    const w = Math.max(160, Math.min(340, e.clientX - rect.left));
    navCol.style.width = w + 'px';
  });
  window.addEventListener('mouseup', () => { dragging = false; document.body.style.cursor = ''; });
}

// ===== i18n 適用 =====

function applyI18n() {
  document.title = tr('window_title');
  document.querySelectorAll('[data-i18n]').forEach(el => { el.textContent = tr(el.dataset.i18n); });
  document.querySelectorAll('[data-i18n-placeholder]').forEach(el => { el.placeholder = tr(el.dataset.i18nPlaceholder); });
  document.getElementById('lang-text').textContent = currentLang === 'ja' ? '🌐 English' : '🌐 日本語';
  renderScanTime();
}

// ===== キーボードショートカット =====

function setupShortcuts() {
  document.addEventListener('keydown', (e) => {
    if (e.ctrlKey && !e.shiftKey && e.key.toLowerCase() === 'o') { e.preventDefault(); onBrowseClick(); }
    else if (e.key === 'F5') { e.preventDefault(); onRescan(); }
    else if (e.ctrlKey && e.shiftKey && e.key.toLowerCase() === 's') { e.preventDefault(); if (scanTree) onExportReport(); }
    else if (e.ctrlKey && e.key.toLowerCase() === 'q') { e.preventDefault(); sendCmd({ cmd: 'quit' }); }
    else if (e.key === 'Escape') closeAllMenus();
  });
}

// ===== 初期化 =====

window.addEventListener('DOMContentLoaded', () => {
  applyI18n();
  setupMenu();
  setupSortHeaders();
  setupSplitter();
  setupShortcuts();
  updateSortIndicators();

  document.getElementById('btn-scan').addEventListener('click', onRescan);
  document.getElementById('addr-input').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') onRescan();
  });
  document.getElementById('btn-lang').addEventListener('click', () => {
    currentLang = currentLang === 'ja' ? 'en' : 'ja';
    applyI18n();
  });

  const wv = getWebView();
  if (wv) wv.addEventListener('message', handleNativeMessage);
  sendCmd({ cmd: 'get_drives' });
});
