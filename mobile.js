// ===== ImposOS Mobile Mockup =====

const screenEl = document.getElementById('screen');

// ===== ICONS =====
function svgIcon(inner) {
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 26 26" width="26" height="26">${inner}</svg>`;
}
const ICONS = {
  terminal: svgIcon(`<path d="M5 7l6 6-6 6" fill="none" stroke="white" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/><line x1="13" y1="19" x2="21" y2="19" stroke="white" stroke-width="2.2" stroke-linecap="round"/>`),
  files: svgIcon(`<path d="M3 7a2 2 0 012-2h5l2 2h9a2 2 0 012 2v10a2 2 0 01-2 2H5a2 2 0 01-2-2V7z" fill="none" stroke="white" stroke-width="1.8"/>`),
  browser: svgIcon(`<circle cx="13" cy="13" r="9" fill="none" stroke="white" stroke-width="1.8"/><ellipse cx="13" cy="13" rx="4.5" ry="9" fill="none" stroke="white" stroke-width="1.5"/><line x1="4" y1="13" x2="22" y2="13" stroke="white" stroke-width="1.5"/>`),
  music: svgIcon(`<ellipse cx="10" cy="18" rx="3.5" ry="2.8" fill="white"/><line x1="13.5" y1="18" x2="13.5" y2="5" stroke="white" stroke-width="2"/><path d="M13.5 5c3 0 5.5 2 5.5 4.5" fill="none" stroke="white" stroke-width="2" stroke-linecap="round"/>`),
  settings: svgIcon(`<path d="M9.5 6.9L11.2 3.2L14.8 3.2L16.5 6.9L20.6 6.5L22.4 9.7L20.0 13.0L22.4 16.3L20.6 19.5L16.5 19.1L14.8 22.8L11.2 22.8L9.5 19.1L5.4 19.5L3.6 16.3L6.0 13.0L3.6 9.7L5.4 6.5Z" fill="none" stroke="white" stroke-width="1.5" stroke-linejoin="round"/><circle cx="13" cy="13" r="3" fill="none" stroke="white" stroke-width="1.6"/>`),
  monitor: svgIcon(`<rect x="3" y="5" width="20" height="13" rx="2" fill="none" stroke="white" stroke-width="1.8"/><line x1="13" y1="18" x2="13" y2="22" stroke="white" stroke-width="1.8"/><line x1="8" y1="22" x2="18" y2="22" stroke="white" stroke-width="1.8" stroke-linecap="round"/>`),
  email: svgIcon(`<rect x="3" y="6" width="20" height="14" rx="2" fill="none" stroke="white" stroke-width="1.8"/><path d="M3 8l10 6 10-6" fill="none" stroke="white" stroke-width="1.8"/>`),
  chat: svgIcon(`<path d="M4 5h18a1 1 0 011 1v11a1 1 0 01-1 1h-4l-4 4v-4H4a1 1 0 01-1-1V6a1 1 0 011-1z" fill="none" stroke="white" stroke-width="1.8"/>`),
  video: svgIcon(`<rect x="3" y="6" width="14" height="14" rx="2" fill="none" stroke="white" stroke-width="1.8"/><path d="M17 10l6-3v12l-6-3z" fill="none" stroke="white" stroke-width="1.8"/>`),
  code: svgIcon(`<path d="M8 7l-5 6 5 6" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/><path d="M18 7l5 6-5 6" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/><line x1="15" y1="4" x2="11" y2="22" stroke="white" stroke-width="1.5"/>`),
  image: svgIcon(`<rect x="3" y="5" width="20" height="16" rx="2" fill="none" stroke="white" stroke-width="1.8"/><circle cx="9" cy="11" r="2" fill="white"/><path d="M3 18l5-5 4 4 3-3 8 6" fill="none" stroke="white" stroke-width="1.5"/>`),
  pdf: svgIcon(`<path d="M6 3h8l6 6v12a2 2 0 01-2 2H6a2 2 0 01-2-2V5a2 2 0 012-2z" fill="none" stroke="white" stroke-width="1.8"/><path d="M14 3v6h6" fill="none" stroke="white" stroke-width="1.8"/>`),
  gamepad: svgIcon(`<rect x="2" y="8" width="22" height="12" rx="4" fill="none" stroke="white" stroke-width="1.8"/><circle cx="17" cy="12" r="1.5" fill="white"/><circle cx="20" cy="14" r="1.5" fill="white"/><line x1="7" y1="11" x2="7" y2="17" stroke="white" stroke-width="1.8"/><line x1="4" y1="14" x2="10" y2="14" stroke="white" stroke-width="1.8"/>`),
  disk: svgIcon(`<ellipse cx="13" cy="13" rx="10" ry="10" fill="none" stroke="white" stroke-width="1.8"/><circle cx="13" cy="13" r="3.5" fill="none" stroke="white" stroke-width="1.5"/><circle cx="13" cy="13" r="1" fill="white"/>`),
  users: svgIcon(`<circle cx="10" cy="9" r="3.5" fill="none" stroke="white" stroke-width="1.8"/><path d="M3 21c0-4 3-7 7-7s7 3 7 7" fill="none" stroke="white" stroke-width="1.8"/><circle cx="19" cy="9" r="2.5" fill="none" stroke="white" stroke-width="1.5"/><path d="M19 14c3 0 5 2 5 5" fill="none" stroke="white" stroke-width="1.5"/>`),
  download: svgIcon(`<path d="M13 3v14" stroke="white" stroke-width="2" stroke-linecap="round"/><path d="M7 13l6 6 6-6" fill="none" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/><line x1="4" y1="22" x2="22" y2="22" stroke="white" stroke-width="2" stroke-linecap="round"/>`),
  table: svgIcon(`<rect x="3" y="5" width="20" height="16" rx="2" fill="none" stroke="white" stroke-width="1.8"/><line x1="3" y1="10" x2="23" y2="10" stroke="white" stroke-width="1.3"/><line x1="3" y1="15" x2="23" y2="15" stroke="white" stroke-width="1.3"/><line x1="10" y1="5" x2="10" y2="21" stroke="white" stroke-width="1.3"/>`),
  pen: svgIcon(`<path d="M17 3l6 6-14 14H3v-6L17 3z" fill="none" stroke="white" stroke-width="1.8" stroke-linejoin="round"/><line x1="14" y1="6" x2="20" y2="12" stroke="white" stroke-width="1.5"/>`),
  box: svgIcon(`<path d="M3 8l10-5 10 5v10l-10 5-10-5z" fill="none" stroke="white" stroke-width="1.8" stroke-linejoin="round"/><line x1="13" y1="13" x2="13" y2="23" stroke="white" stroke-width="1.5"/><line x1="3" y1="8" x2="13" y2="13" stroke="white" stroke-width="1.5"/><line x1="23" y1="8" x2="13" y2="13" stroke="white" stroke-width="1.5"/>`),
};

// ===== CATEGORIES =====
const CATEGORIES = [
  { id: 'system', label: 'System' },
  { id: 'internet', label: 'Internet' },
  { id: 'media', label: 'Media' },
  { id: 'graphics', label: 'Graphics' },
  { id: 'dev', label: 'Development' },
  { id: 'office', label: 'Office' },
  { id: 'games', label: 'Games' },
];

// ===== APP CATALOG =====
const APP_CATALOG = [
  { id: 'terminal', label: 'Terminal', color: '#3478F6', icon: 'terminal', category: 'system', dock: true },
  { id: 'files', label: 'Files', color: '#34C759', icon: 'files', category: 'system', home: true },
  { id: 'settings', label: 'Settings', color: '#FF9500', icon: 'settings', category: 'system', dock: true },
  { id: 'monitor', label: 'Monitor', color: '#00C7BE', icon: 'monitor', category: 'system', home: true },
  { id: 'disk_usage', label: 'Disk Usage', color: '#5856D6', icon: 'disk', category: 'system' },
  { id: 'sysinfo', label: 'System Info', color: '#3478F6', icon: null, category: 'system' },
  { id: 'packages', label: 'Packages', color: '#AF52DE', icon: 'box', category: 'system' },
  { id: 'users', label: 'Users', color: '#FF9500', icon: 'users', category: 'system' },
  { id: 'browser', label: 'Browser', color: '#5856D6', icon: 'browser', category: 'internet', dock: true },
  { id: 'email', label: 'Email', color: '#3478F6', icon: 'email', category: 'internet', home: true },
  { id: 'chat', label: 'Chat', color: '#34C759', icon: 'chat', category: 'internet', home: true },
  { id: 'torrent', label: 'Torrent', color: '#FF9500', icon: 'download', category: 'internet' },
  { id: 'music', label: 'Music', color: '#FF3B30', icon: 'music', category: 'media', dock: true },
  { id: 'video', label: 'Video', color: '#FF6600', icon: 'video', category: 'media', home: true },
  { id: 'podcast', label: 'Podcasts', color: '#AF52DE', icon: null, category: 'media' },
  { id: 'recorder', label: 'Recorder', color: '#FF3B30', icon: null, category: 'media' },
  { id: 'imageview', label: 'Gallery', color: '#34C759', icon: 'image', category: 'media', home: true },
  { id: 'photoeditor', label: 'Photo Edit', color: '#FF9500', icon: 'image', category: 'graphics', home: true },
  { id: 'vectordraw', label: 'Draw', color: '#34C759', icon: 'pen', category: 'graphics', home: true },
  { id: 'screenshot', label: 'Screenshot', color: '#3478F6', icon: null, category: 'graphics' },
  { id: 'codeeditor', label: 'Code', color: '#007ACC', icon: 'code', category: 'dev', home: true },
  { id: 'gitclient', label: 'Git', color: '#F05032', icon: null, category: 'dev' },
  { id: 'database', label: 'Database', color: '#336791', icon: 'table', category: 'dev' },
  { id: 'writer', label: 'Writer', color: '#185ABD', icon: null, category: 'office' },
  { id: 'spreadsheet', label: 'Sheets', color: '#107C41', icon: 'table', category: 'office' },
  { id: 'pdfreader', label: 'PDF', color: '#EC1C24', icon: 'pdf', category: 'office', home: true },
  { id: 'notes', label: 'Notes', color: '#FFD60A', icon: null, category: 'office', home: true },
  { id: 'solitaire', label: 'Solitaire', color: '#34C759', icon: null, category: 'games', home: true },
  { id: 'mines', label: 'Mines', color: '#8E8E93', icon: null, category: 'games' },
  { id: 'chess', label: 'Chess', color: '#1C1C1E', icon: null, category: 'games' },
  { id: 'tetris', label: 'Tetris', color: '#FF3B30', icon: null, category: 'games' },
  { id: 'snake', label: 'Snake', color: '#34C759', icon: null, category: 'games' },
];

// ===== STATE =====
let locked = true;
let currentApp = null;
let drawerOpen = false;
let ccOpen = false;
let openApps = new Set();

// ===== CLOCK =====
function fmt(d) { return d.toLocaleTimeString('en-US', { hour: 'numeric', minute: '2-digit', hour12: false }); }
function fmtDate(d) { return d.toLocaleDateString('en-US', { weekday: 'long', month: 'long', day: 'numeric' }); }

function updateClock() {
  const d = new Date(), t = fmt(d);
  document.getElementById('sb-time').textContent = t;
  const lt = document.getElementById('lock-time');
  const ld = document.getElementById('lock-date');
  if (lt) lt.textContent = t;
  if (ld) ld.textContent = fmtDate(d);
}
setInterval(updateClock, 1000);

// ===== INIT =====
function initMobile() {
  buildStatusBarIcons();
  buildLockScreen();
  buildHomeScreen();
  buildDock();
  buildControlCenter();
  initGestures();
  updateClock();
}

// ===== STATUS BAR ICONS =====
function buildStatusBarIcons() {
  document.getElementById('sb-icons').innerHTML = `
    <svg viewBox="0 0 16 16" fill="none"><path d="M1 12h2l1.5-4L7 11l2-6 2 8 1.5-5L14 12h1" fill="none" stroke="white" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"/></svg>
    <svg viewBox="0 0 16 16" fill="none"><path d="M8 1.5C4.8 1.5 2 3 0 5.5l2.2 2C3.7 6 5.7 4.8 8 4.8s4.3 1.2 5.8 2.7l2.2-2C14 3 11.2 1.5 8 1.5z" fill="white" opacity="0.5"/><path d="M8 5.5c-2 0-3.5.8-5 2l2.5 2.5c.8-.8 1.5-1.2 2.5-1.2s1.7.4 2.5 1.2L13 7.5c-1.5-1.2-3-2-5-2z" fill="white" opacity="0.7"/><circle cx="8" cy="12" r="1.5" fill="white"/></svg>
    <svg viewBox="0 0 20 12" fill="none"><rect x="0" y="1" width="16" height="10" rx="2" stroke="white" stroke-width="1.3" fill="none"/><rect x="16.5" y="3.5" width="2.5" height="5" rx="1" fill="white" opacity="0.4"/><rect x="2" y="3" width="9" height="6" rx="1" fill="white" opacity="0.6"/></svg>
  `;
}

// ===== LOCK SCREEN =====
function buildLockScreen() {
  const d = new Date();
  document.getElementById('lockscreen').innerHTML = `
    <div class="lock-overlay"></div>
    <div class="lock-content">
      <div class="lock-time" id="lock-time">${fmt(d)}</div>
      <div class="lock-date" id="lock-date">${fmtDate(d)}</div>
    </div>
    <div class="lock-notifications">
      <div class="lock-notif">
        <div class="lock-notif-icon" style="background:#3478F6">iO</div>
        <div>
          <div class="lock-notif-title">ImposOS</div>
          <div class="lock-notif-body">System ready. All services running.</div>
        </div>
      </div>
      <div class="lock-notif">
        <div class="lock-notif-icon" style="background:#34C759">
          <svg viewBox="0 0 26 26" width="16" height="16"><path d="M5 7l6 6-6 6" fill="none" stroke="white" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"/><line x1="13" y1="19" x2="21" y2="19" stroke="white" stroke-width="2.2" stroke-linecap="round"/></svg>
        </div>
        <div>
          <div class="lock-notif-title">Terminal</div>
          <div class="lock-notif-body">Build completed successfully.</div>
        </div>
      </div>
    </div>
    <div class="lock-bottom">
      <div class="lock-hint">Swipe up to unlock</div>
    </div>
  `;
}

function unlock() {
  if (!locked) return;
  locked = false;
  const ls = document.getElementById('lockscreen');
  ls.classList.add('unlocking');
  document.getElementById('homescreen').classList.add('visible');
  document.getElementById('dock').classList.add('visible');
  document.getElementById('statusbar').classList.add('visible');
  setTimeout(() => ls.classList.add('hidden'), 600);
}

function lock() {
  if (locked) return;
  locked = true;
  const ls = document.getElementById('lockscreen');
  ls.classList.remove('hidden', 'unlocking');
  document.getElementById('homescreen').classList.remove('visible');
  document.getElementById('dock').classList.remove('visible');
  document.getElementById('statusbar').classList.remove('visible');
  if (currentApp) closeApp();
  if (drawerOpen) closeDrawer();
  if (ccOpen) closeCC();
  updateClock();
}

// ===== HOME SCREEN (flat grid) =====
function buildHomeScreen() {
  const hs = document.getElementById('homescreen');
  hs.innerHTML = '';

  const grid = document.createElement('div');
  grid.className = 'home-grid';
  const homeApps = APP_CATALOG.filter(a => a.home);
  homeApps.forEach(app => grid.appendChild(createAppIcon(app)));
  hs.appendChild(grid);

  // Swipe-up handle hint
  const handle = document.createElement('div');
  handle.className = 'home-handle';
  handle.innerHTML = '<div class="home-handle-bar"></div>';
  hs.appendChild(handle);
}

function createAppIcon(app) {
  const wrap = document.createElement('div');
  wrap.className = 'home-icon';
  wrap.dataset.appId = app.id;

  const img = document.createElement('div');
  img.className = 'home-icon-img';
  img.style.backgroundColor = app.color;

  if (app.icon && ICONS[app.icon]) {
    img.innerHTML = ICONS[app.icon];
  } else {
    const letters = document.createElement('span');
    letters.className = 'icon-letters';
    letters.textContent = app.label.slice(0, 2);
    img.appendChild(letters);
  }

  const label = document.createElement('div');
  label.className = 'home-icon-label';
  label.textContent = app.label;

  wrap.appendChild(img);
  wrap.appendChild(label);
  wrap.addEventListener('click', e => { e.stopPropagation(); openApp(app.id); });
  return wrap;
}

// ===== DOCK (pinned apps) =====
function buildDock() {
  const dock = document.getElementById('dock');
  dock.innerHTML = '';
  const dockApps = APP_CATALOG.filter(a => a.dock);
  dockApps.forEach(app => {
    const icon = document.createElement('div');
    icon.className = 'dock-icon';
    icon.dataset.appId = app.id;

    const img = document.createElement('div');
    img.className = 'dock-icon-img';
    img.style.backgroundColor = app.color;

    if (app.icon && ICONS[app.icon]) {
      img.innerHTML = ICONS[app.icon];
    } else {
      const letters = document.createElement('span');
      letters.className = 'icon-letters';
      letters.textContent = app.label.slice(0, 2);
      img.appendChild(letters);
    }

    icon.appendChild(img);
    icon.addEventListener('click', e => { e.stopPropagation(); openApp(app.id); });
    dock.appendChild(icon);
  });
}

// ===== APP DRAWER (search + all apps) =====
function openDrawer(focusSearch) {
  if (drawerOpen) return;
  drawerOpen = true;

  const drawer = document.getElementById('app-drawer');
  drawer.innerHTML = '';

  const header = document.createElement('div');
  header.className = 'drawer-header';

  const searchWrap = document.createElement('div');
  searchWrap.className = 'drawer-search-wrap';
  searchWrap.innerHTML = `
    <svg viewBox="0 0 20 20" fill="none"><circle cx="8.5" cy="8.5" r="5.5" stroke="white" stroke-width="1.8"/><line x1="12.5" y1="12.5" x2="17" y2="17" stroke="white" stroke-width="2" stroke-linecap="round"/></svg>
  `;
  const input = document.createElement('input');
  input.className = 'drawer-search';
  input.type = 'text';
  input.placeholder = 'Search apps...';
  input.autocomplete = 'off';
  input.spellcheck = false;
  searchWrap.appendChild(input);

  const cancel = document.createElement('button');
  cancel.className = 'drawer-cancel';
  cancel.textContent = 'Close';
  cancel.addEventListener('click', closeDrawer);
  searchWrap.appendChild(cancel);

  header.appendChild(searchWrap);
  drawer.appendChild(header);

  const body = document.createElement('div');
  body.className = 'drawer-body';
  drawer.appendChild(body);

  renderDrawerContent(body, '');

  input.addEventListener('input', () => {
    renderDrawerContent(body, input.value);
  });

  drawer.classList.add('open');
  if (focusSearch) setTimeout(() => input.focus(), 100);
}

function closeDrawer() {
  if (!drawerOpen) return;
  drawerOpen = false;
  document.getElementById('app-drawer').classList.remove('open');
}

function renderDrawerContent(container, query) {
  container.innerHTML = '';
  const q = query.trim().toLowerCase();

  if (q.length > 0) {
    const results = APP_CATALOG.filter(app =>
      app.label.toLowerCase().includes(q) || app.category.toLowerCase().includes(q)
    ).sort((a, b) => {
      const aStart = a.label.toLowerCase().startsWith(q) ? 0 : 1;
      const bStart = b.label.toLowerCase().startsWith(q) ? 0 : 1;
      return aStart - bStart;
    });

    if (results.length === 0) {
      const empty = document.createElement('div');
      empty.className = 'drawer-empty';
      empty.textContent = 'No apps found';
      container.appendChild(empty);
      return;
    }

    const section = document.createElement('div');
    section.className = 'drawer-cat';
    section.innerHTML = `<div class="drawer-cat-label">${results.length} result${results.length !== 1 ? 's' : ''}</div>`;
    const tiles = document.createElement('div');
    tiles.className = 'drawer-tiles';
    results.forEach(app => tiles.appendChild(createAppIcon(app)));
    section.appendChild(tiles);
    container.appendChild(section);
  } else {
    CATEGORIES.forEach(cat => {
      const apps = APP_CATALOG.filter(a => a.category === cat.id);
      if (apps.length === 0) return;

      const section = document.createElement('div');
      section.className = 'drawer-cat';
      section.innerHTML = `<div class="drawer-cat-label">${cat.label}</div>`;
      const tiles = document.createElement('div');
      tiles.className = 'drawer-tiles';
      apps.forEach(app => tiles.appendChild(createAppIcon(app)));
      section.appendChild(tiles);
      container.appendChild(section);
    });
  }
}

// ===== APP SHEET (opaque) =====
function openApp(appId) {
  if (currentApp === appId) return;
  const app = APP_CATALOG.find(a => a.id === appId);
  if (!app) return;

  if (drawerOpen) closeDrawer();
  currentApp = appId;
  openApps.add(appId);

  const sheet = document.getElementById('app-sheet');
  sheet.innerHTML = '';

  const bg = document.createElement('div');
  bg.className = 'app-sheet-bg';
  sheet.appendChild(bg);

  const header = document.createElement('div');
  header.className = 'app-sheet-header';
  const backBtn = document.createElement('button');
  backBtn.className = 'app-back';
  backBtn.innerHTML = '<svg viewBox="0 0 24 24" fill="none"><path d="M15 18l-6-6 6-6" stroke="white" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>';
  backBtn.addEventListener('click', closeApp);
  const title = document.createElement('span');
  title.className = 'app-sheet-title';
  title.textContent = app.label;
  header.appendChild(backBtn);
  header.appendChild(title);
  sheet.appendChild(header);

  const body = document.createElement('div');
  body.className = 'app-sheet-body';
  const builder = APP_BUILDERS[appId];
  if (builder) builder(body, app);
  else buildPlaceholder(body, app);
  sheet.appendChild(body);

  sheet.classList.remove('closing');
  sheet.offsetHeight;
  sheet.classList.add('open');
  updateOpenDots();
}

function closeApp() {
  const sheet = document.getElementById('app-sheet');
  sheet.classList.remove('open');
  sheet.classList.add('closing');
  setTimeout(() => { sheet.classList.remove('closing'); currentApp = null; }, 350);
}

function updateOpenDots() {
  document.querySelectorAll('.home-icon, .dock-icon').forEach(icon => {
    const appId = icon.dataset.appId;
    const img = icon.querySelector('.home-icon-img, .dock-icon-img');
    if (!img) return;
    const existing = img.querySelector('.open-dot');
    if (openApps.has(appId) && !existing) {
      const dot = document.createElement('div');
      dot.className = 'open-dot';
      img.appendChild(dot);
    }
  });
}

// ===== CONTROL CENTER =====
function buildControlCenter() {
  const cc = document.getElementById('control-center');
  cc.innerHTML = `
    <div class="cc-grid">
      <div class="cc-toggle active" data-cc="wifi">
        <div class="cc-toggle-icon"><svg viewBox="0 0 18 18" fill="none"><path d="M9 1C5.5 1 2.5 2.5 0 5l2.5 2.5C4.5 5.5 6.5 4.5 9 4.5s4.5 1 6.5 3L18 5c-2.5-2.5-5.5-4-9-4z" fill="white" opacity="0.5"/><path d="M9 5.5c-2 0-3.5.8-5 2l2.5 2.5c.8-.8 1.5-1.2 2.5-1.2s1.7.4 2.5 1.2L14 7.5c-1.5-1.2-3-2-5-2z" fill="white" opacity="0.7"/><circle cx="9" cy="13" r="1.5" fill="white"/></svg></div>
        <div><div class="cc-toggle-label">Wi-Fi</div><div class="cc-toggle-sub">ImposNet</div></div>
      </div>
      <div class="cc-toggle active" data-cc="bt">
        <div class="cc-toggle-icon"><svg viewBox="0 0 18 18" fill="none"><path d="M9 1v16l5-5-5-5 5-5z" fill="none" stroke="white" stroke-width="1.5" stroke-linejoin="round"/><line x1="4" y1="5.5" x2="9" y2="9" stroke="white" stroke-width="1.5"/><line x1="4" y1="12.5" x2="9" y2="9" stroke="white" stroke-width="1.5"/></svg></div>
        <div><div class="cc-toggle-label">Bluetooth</div><div class="cc-toggle-sub">On</div></div>
      </div>
    </div>
    <div class="cc-row">
      <div class="cc-btn"><svg viewBox="0 0 20 20" fill="none"><path d="M7 1h6v6l-1 2v8a1 1 0 01-1 1H9a1 1 0 01-1-1v-8L7 7V1z" fill="none" stroke="white" stroke-width="1.5"/></svg><span class="cc-btn-label">Light</span></div>
      <div class="cc-btn"><svg viewBox="0 0 20 20" fill="none"><circle cx="10" cy="10" r="8" fill="none" stroke="white" stroke-width="1.5"/><path d="M10 2a8 8 0 000 16" fill="white" opacity="0.3"/></svg><span class="cc-btn-label">Focus</span></div>
      <div class="cc-btn"><svg viewBox="0 0 20 20" fill="none"><path d="M10 2l2 6h6l-1 2-5 1v5l2 2H8l2-2v-5L5 10 4 8h6z" fill="none" stroke="white" stroke-width="1.3" stroke-linejoin="round"/></svg><span class="cc-btn-label">Airplane</span></div>
      <div class="cc-btn active"><svg viewBox="0 0 20 20" fill="none"><path d="M14 3a7 7 0 11-8 0" fill="none" stroke="white" stroke-width="1.5" stroke-linecap="round"/><path d="M10 1v5h-4" fill="none" stroke="white" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg><span class="cc-btn-label">Rotation</span></div>
    </div>
    <div class="cc-slider-wrap"><div class="cc-slider-track"><div class="cc-slider-fill" style="width:70%"></div><div class="cc-slider-icon"><svg viewBox="0 0 16 16" fill="none"><circle cx="8" cy="8" r="4" fill="none" stroke="white" stroke-width="1.5"/><path d="M8 1v2M8 13v2M1 8h2M13 8h2" stroke="white" stroke-width="1" stroke-linecap="round"/></svg></div></div></div>
    <div class="cc-slider-wrap"><div class="cc-slider-track"><div class="cc-slider-fill" style="width:50%"></div><div class="cc-slider-icon"><svg viewBox="0 0 16 16" fill="none"><path d="M2 11V5M5 13V3M8 10V6M11 12V4" stroke="white" stroke-width="1.5" stroke-linecap="round"/></svg></div></div></div>
    <div class="cc-music">
      <div class="cc-music-art"></div>
      <div class="cc-music-info"><div class="cc-music-title">Kernel Panic</div><div class="cc-music-artist">The Interrupts</div></div>
      <div class="cc-music-controls">
        <button class="cc-music-btn"><svg viewBox="0 0 16 16" width="12" height="12"><path d="M3 3h2v10H3zM7 8l7-5v10z" fill="white"/></svg></button>
        <button class="cc-music-btn"><svg viewBox="0 0 16 16" width="14" height="14"><polygon points="4,2 14,8 4,14" fill="white"/></svg></button>
        <button class="cc-music-btn"><svg viewBox="0 0 16 16" width="12" height="12"><path d="M11 3h2v10h-2zM9 8L2 3v10z" fill="white"/></svg></button>
      </div>
    </div>
  `;
  cc.querySelectorAll('.cc-toggle, .cc-btn').forEach(el => el.addEventListener('click', () => el.classList.toggle('active')));
  document.getElementById('cc-overlay').addEventListener('click', closeCC);
}

function openCC() { if (ccOpen) return; ccOpen = true; document.getElementById('control-center').classList.add('open'); document.getElementById('cc-overlay').classList.add('open'); }
function closeCC() { if (!ccOpen) return; ccOpen = false; document.getElementById('control-center').classList.remove('open'); document.getElementById('cc-overlay').classList.remove('open'); }

// ===== GESTURES =====
function initGestures() {
  const scr = screenEl;
  let startX = 0, startY = 0, startTime = 0, gesture = null;

  scr.addEventListener('pointerdown', e => {
    startX = e.clientX; startY = e.clientY; startTime = Date.now(); gesture = null;
    const rect = scr.getBoundingClientRect();
    const relY = e.clientY - rect.top, screenH = rect.height;
    if (locked) gesture = 'unlock';
    else if (ccOpen) gesture = 'cc-close';
    else if (drawerOpen) gesture = null;
    else if (currentApp && relY > screenH - 50) gesture = 'close-app';
    else if (relY < 55) gesture = 'cc';
    else if (!currentApp) gesture = 'drawer';
  });

  scr.addEventListener('pointermove', e => {
    const dy = e.clientY - startY;
    if (gesture === 'unlock' && dy < -10) {
      const ls = document.getElementById('lockscreen');
      ls.style.transform = `translateY(${dy}px)`;
      ls.style.opacity = 1 - Math.min(1, Math.abs(dy) / 300) * 0.5;
      ls.style.transition = 'none';
    }
  });

  scr.addEventListener('pointerup', e => {
    const dy = e.clientY - startY, elapsed = Date.now() - startTime, velocity = Math.abs(dy) / elapsed;
    if (gesture === 'unlock') {
      const ls = document.getElementById('lockscreen');
      ls.style.transition = ''; ls.style.opacity = '';
      if (dy < -80 || (dy < -30 && velocity > 0.5)) { ls.style.transform = ''; unlock(); }
      else ls.style.transform = '';
    }
    if (gesture === 'cc' && dy > 60) openCC();
    if (gesture === 'cc-close' && (dy < -30 || elapsed < 200)) closeCC();
    if (gesture === 'close-app' && dy < -80) closeApp();
    if (gesture === 'drawer' && (dy < -80 || (dy < -30 && velocity > 0.5))) openDrawer(true);
    gesture = null;
  });

  scr.addEventListener('pointercancel', () => {
    if (gesture === 'unlock') {
      const ls = document.getElementById('lockscreen');
      ls.style.transition = ''; ls.style.transform = ''; ls.style.opacity = '';
    }
    gesture = null;
  });

  document.addEventListener('keydown', e => {
    if (e.key === 'Escape') {
      if (ccOpen) { closeCC(); return; }
      if (drawerOpen) { closeDrawer(); return; }
      if (currentApp) { closeApp(); return; }
      if (!locked) { lock(); return; }
    }
    if (e.key === ' ' && locked) { e.preventDefault(); unlock(); }
    if (e.key === 'Tab' && !locked) { e.preventDefault(); if (drawerOpen) closeDrawer(); else openDrawer(true); }
  });
}

// ===== APP BUILDERS =====
const APP_BUILDERS = {
  terminal: buildTerminal,
  music: buildMusic,
  settings: buildSettings,
  monitor: buildMonitor,
  files: buildFiles,
  sysinfo: buildAbout,
};

function buildTerminal(body) {
  const term = document.createElement('div');
  term.className = 'm-terminal';
  const hl = s => `<span class="hl">${s}</span>`;
  const lines = [
    { p: true, path: '~', cmd: 'neofetch' },
    { o: hl('  _____                            ____  _____ ') },
    { o: hl(' |_   _|_ __ ___  _ __   ___  ___|  _ \\/ ____|') },
    { o: hl("   | | | '_ ` _ \\| '_ \\ / _ \\/ __| | | \\___ \\ ") },
    { o: hl('   | | | | | | | | |_) | (_) \\__ \\ |_| |___) |') },
    { o: hl('   |_| |_| |_| |_| .__/ \\___/|___/____/|____/ ') },
    { o: hl('                  |_|') },
    { o: '' },
    { o: '  ' + hl('OS') + ':       ImposOS 0.1 i386' },
    { o: '  ' + hl('Kernel') + ':   imposkernel 0.1.0' },
    { o: '  ' + hl('Shell') + ':    impossh 1.0' },
    { o: '  ' + hl('WM') + ':       Liquid Glass' },
    { o: '  ' + hl('CPU') + ':      i686 @ 120Hz PIT' },
    { o: '  ' + hl('Memory') + ':   64MB / 256MB' },
    { o: '' },
    { p: true, path: '~', cmd: '' },
  ];
  lines.forEach(l => {
    const div = document.createElement('div');
    div.className = 'm-term-line';
    if (l.p) {
      div.innerHTML = `<span class="prompt">root</span>@<span class="path">${l.path}</span> $ <span>${l.cmd}</span>`;
      if (!l.cmd) div.innerHTML += '<span class="m-term-cursor"></span>';
    } else {
      div.innerHTML = `<span class="output">${l.o}</span>`;
    }
    term.appendChild(div);
  });
  body.appendChild(term);
}

function buildMusic(body) {
  const wrap = document.createElement('div');
  wrap.className = 'm-music';
  const artWrap = document.createElement('div');
  artWrap.className = 'm-music-art-wrap';
  const art = document.createElement('div');
  art.className = 'm-music-art';
  const canvas = document.createElement('canvas');
  canvas.width = 480; canvas.height = 480;
  const actx = canvas.getContext('2d');
  const g = actx.createLinearGradient(0, 0, 480, 480);
  g.addColorStop(0, '#1a0533'); g.addColorStop(0.4, '#3a1c71');
  g.addColorStop(0.7, '#d76d77'); g.addColorStop(1, '#ffaf7b');
  actx.fillStyle = g; actx.fillRect(0, 0, 480, 480);
  for (let i = 0; i < 5; i++) {
    actx.beginPath();
    actx.arc(90 + i * 70, 240 + Math.sin(i) * 70, 35 + i * 10, 0, Math.PI * 2);
    actx.fillStyle = `rgba(255,255,255,${0.03 + i * 0.015})`;
    actx.fill();
  }
  art.appendChild(canvas);
  artWrap.appendChild(art);
  wrap.appendChild(artWrap);
  wrap.insertAdjacentHTML('beforeend', `
    <div class="m-music-info"><div class="title">Kernel Panic</div><div class="artist">The Interrupts</div></div>
    <div class="m-music-progress"><div class="m-music-bar"><div class="m-music-fill"></div></div></div>
    <div class="m-music-time"><span>1:24</span><span>4:07</span></div>
    <div class="m-music-controls">
      <button class="m-music-btn"><svg viewBox="0 0 16 16" width="16" height="16"><path d="M3 3h2v10H3zM7 8l7-5v10z" fill="white"/></svg></button>
      <button class="m-music-btn play"><svg viewBox="0 0 16 16" width="20" height="20"><polygon points="4,2 14,8 4,14" fill="white"/></svg></button>
      <button class="m-music-btn"><svg viewBox="0 0 16 16" width="16" height="16"><path d="M11 3h2v10h-2zM9 8L2 3v10z" fill="white"/></svg></button>
    </div>
  `);
  body.appendChild(wrap);
}

function buildSettings(body) {
  const wrap = document.createElement('div');
  wrap.className = 'm-settings';
  const sections = [
    { title: 'General', rows: [
      { color: '#8E8E93', label: 'General', value: '' },
      { color: '#3478F6', label: 'Display', value: '1920\u00d71080' },
      { color: '#34C759', label: 'Sound', value: '' },
      { color: '#5856D6', label: 'Notifications', value: '' },
    ]},
    { title: 'Appearance', rows: [
      { color: '#FF9500', label: 'Wallpaper', value: 'Silk' },
      { color: '#AF52DE', label: 'Theme', value: 'Liquid Glass' },
    ]},
    { title: 'System', rows: [
      { color: '#00C7BE', label: 'Storage', value: '256 MB' },
      { color: '#3478F6', label: 'Network', value: 'Connected' },
      { color: '#8E8E93', label: 'About', value: 'ImposOS 0.1', action: 'sysinfo' },
    ]},
  ];
  sections.forEach(sec => {
    const section = document.createElement('div');
    section.className = 'm-settings-section';
    section.innerHTML = `<div class="m-settings-section-title">${sec.title}</div>`;
    const group = document.createElement('div');
    group.className = 'm-settings-group';
    sec.rows.forEach(row => {
      const el = document.createElement('div');
      el.className = 'm-settings-row';
      el.innerHTML = `
        <div class="m-settings-icon" style="background:${row.color}"><svg viewBox="0 0 16 16" fill="none"><circle cx="8" cy="8" r="3" fill="white" opacity="0.8"/></svg></div>
        <span class="m-settings-label">${row.label}</span>
        <span class="m-settings-value">${row.value}</span>
        <span class="m-settings-arrow">\u203A</span>
      `;
      if (row.action) el.addEventListener('click', () => { closeApp(); setTimeout(() => openApp(row.action), 400); });
      group.appendChild(el);
    });
    section.appendChild(group);
    wrap.appendChild(section);
  });
  body.appendChild(wrap);
}

function buildMonitor(body) {
  const wrap = document.createElement('div');
  wrap.className = 'm-monitor';
  const resources = [
    { label: 'CPU', pct: 34, color: '#3478F6' },
    { label: 'Memory', pct: 62, color: '#34C759' },
    { label: 'Disk', pct: 18, color: '#FF9500' },
    { label: 'Swap', pct: 5, color: '#AF52DE' },
  ];
  const resSection = document.createElement('div');
  resSection.className = 'm-monitor-section';
  resSection.innerHTML = '<div class="m-monitor-label">Resources</div>';
  resources.forEach(r => {
    const row = document.createElement('div');
    row.className = 'm-monitor-row';
    row.innerHTML = `<span class="m-monitor-name">${r.label}</span><div class="m-monitor-bar"><div class="m-monitor-fill" style="width:0%;background:${r.color}" data-target="${r.pct}"></div></div><span class="m-monitor-val">${r.pct}%</span>`;
    resSection.appendChild(row);
  });
  wrap.appendChild(resSection);
  const procSection = document.createElement('div');
  procSection.className = 'm-monitor-section';
  procSection.innerHTML = '<div class="m-monitor-label">Top Processes</div>';
  const procGroup = document.createElement('div');
  procGroup.className = 'm-settings-group';
  [{ n: 'wm', c: '12.1%', m: '6.8MB' }, { n: 'desktop', c: '8.4%', m: '4.2MB' }, { n: 'monitor', c: '3.5%', m: '2.1MB' }, { n: 'terminal', c: '1.2%', m: '1.4MB' }, { n: 'httpd', c: '0.1%', m: '0.6MB' }].forEach(p => {
    const row = document.createElement('div');
    row.className = 'm-settings-row';
    row.style.cursor = 'default';
    row.innerHTML = `<span class="m-settings-label" style="font-family:'IBM Plex Mono',monospace;font-size:12px">${p.n}</span><span class="m-settings-value">${p.c}</span><span class="m-settings-value">${p.m}</span>`;
    procGroup.appendChild(row);
  });
  procSection.appendChild(procGroup);
  wrap.appendChild(procSection);
  body.appendChild(wrap);
  setTimeout(() => wrap.querySelectorAll('.m-monitor-fill').forEach(el => { el.style.width = el.dataset.target + '%'; }), 100);
}

function buildFiles(body) {
  const wrap = document.createElement('div');
  wrap.className = 'm-files';
  wrap.innerHTML = `<div class="m-files-path">/ <span>home</span> / <span>root</span></div>`;
  const items = [
    { name: 'Desktop', folder: true }, { name: 'Documents', folder: true }, { name: 'Downloads', folder: true },
    { name: 'Music', folder: true }, { name: 'Pictures', folder: true },
    { name: '.bashrc', folder: false, size: '1.2 KB' }, { name: 'notes.txt', folder: false, size: '856 B' },
    { name: 'config.sh', folder: false, size: '2.4 KB' }, { name: 'kernel.elf', folder: false, size: '128 KB' },
  ];
  const folderSvg = `<svg viewBox="0 0 28 28" fill="none"><path d="M2 8a2 2 0 012-2h6l2 2h10a2 2 0 012 2v10a2 2 0 01-2 2H4a2 2 0 01-2-2V8z" fill="rgba(52,199,89,0.25)" stroke="rgba(52,199,89,0.6)" stroke-width="1.3"/></svg>`;
  const fileSvg = `<svg viewBox="0 0 28 28" fill="none"><path d="M6 3h10l6 6v14a2 2 0 01-2 2H6a2 2 0 01-2-2V5a2 2 0 012-2z" fill="rgba(255,255,255,0.04)" stroke="rgba(255,255,255,0.2)" stroke-width="1.2"/><path d="M16 3v6h6" fill="none" stroke="rgba(255,255,255,0.15)" stroke-width="1.2"/></svg>`;
  const list = document.createElement('div');
  list.className = 'm-files-list';
  items.forEach(item => {
    const el = document.createElement('div');
    el.className = 'm-file-item' + (item.folder ? ' folder' : '');
    el.innerHTML = `<div class="m-file-icon">${item.folder ? folderSvg : fileSvg}</div><span class="m-file-name">${item.name}</span><span class="m-file-meta">${item.folder ? '' : item.size}</span>`;
    list.appendChild(el);
  });
  wrap.appendChild(list);
  body.appendChild(wrap);
}

function buildAbout(body) {
  const wrap = document.createElement('div');
  wrap.className = 'm-about';
  wrap.innerHTML = `
    <div class="m-about-header">
      <div class="m-about-logo">iO</div>
      <div class="m-about-name">ImposOS</div>
      <div class="m-about-version">Version 0.1.0 \u2014 Liquid Glass Edition</div>
    </div>
    <div class="m-about-stats">
      <div class="m-about-stat"><div class="label">Architecture</div><div class="value">i386</div></div>
      <div class="m-about-stat"><div class="label">Kernel</div><div class="value">0.1.0</div></div>
      <div class="m-about-stat"><div class="label">Filesystem</div><div class="value">imposfs v4</div></div>
      <div class="m-about-stat"><div class="label">Memory</div><div class="value">256 MB</div></div>
      <div class="m-about-stat"><div class="label">Display</div><div class="value">1920\u00d71080</div></div>
      <div class="m-about-stat"><div class="label">Network</div><div class="value">TCP/IP</div></div>
    </div>
    <div class="m-about-credits">
      A bare-metal operating system built from scratch.<br>
      Multiboot kernel \u00b7 Custom filesystem \u00b7 Full networking stack<br>
      Liquid glass compositing \u00b7 60+ shell commands
    </div>
  `;
  body.appendChild(wrap);
}

function buildPlaceholder(body, app) {
  const wrap = document.createElement('div');
  wrap.className = 'm-placeholder';
  if (app.icon && ICONS[app.icon]) {
    const div = document.createElement('div');
    div.innerHTML = ICONS[app.icon];
    const svg = div.querySelector('svg');
    svg.style.cssText = 'width:52px;height:52px;opacity:0.08';
    wrap.appendChild(div);
  } else {
    wrap.innerHTML = `<svg viewBox="0 0 48 48" fill="none"><rect x="6" y="6" width="36" height="36" rx="8" stroke="white" stroke-width="2" opacity="0.08"/></svg>`;
  }
  wrap.insertAdjacentHTML('beforeend', `<div class="m-ph-label">${app.label} coming soon</div>`);
  body.appendChild(wrap);
}

// ===== BOOT =====
document.addEventListener('DOMContentLoaded', initMobile);
