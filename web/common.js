// Connexion WebSocket avec reconnexion automatique
function connectWS(role, onMsg, onStatus) {
  let ws, timer;
  const api = { send(o) { if (ws && ws.readyState === 1) { ws.send(JSON.stringify(o)); return true } return false } };
  function open() {
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    ws = new WebSocket(`${proto}://${location.host}/ws?role=${role}`);
    ws.onopen = () => { onStatus && onStatus(true); api.onopen && api.onopen() };
    ws.onmessage = e => { try { onMsg(JSON.parse(e.data)) } catch (_) {} };
    ws.onclose = () => { onStatus && onStatus(false); clearTimeout(timer); timer = setTimeout(open, 1000) };
    ws.onerror = () => ws.close();
  }
  open();
  return api;
}
function esc(s) { return String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c])) }
function hhmm(ms) { return new Date(ms).toLocaleTimeString('fr-FR', { hour: '2-digit', minute: '2-digit', second: '2-digit' }) }

// ===== Grille de texte fixe, identique à la montre (firmware/src/main.cpp) =====
// 12 colonnes x 8 lignes = 96 caractères max. Coupure entre les mots ;
// un mot de plus de 12 lettres est coupé. Le bloc est centré (lignes et colonnes).
const GRID = { cols: 12, rows: 8, max: 96 };
function wrapGrid(text) {
  const C = GRID.cols, lines = [];
  for (const para of String(text).trim().split('\n')) {
    let line = [];
    for (let w of para.split(/[ \t]+/).filter(Boolean)) {
      w = [...w];
      if (line.length && line.length + 1 + w.length <= C) { line.push(' ', ...w); continue }
      if (line.length) { lines.push(line); line = [] }
      while (w.length > C) { lines.push(w.slice(0, C)); w = w.slice(C) }
      line = w;
    }
    lines.push(line);
  }
  while (lines.length && !lines[lines.length - 1].length) lines.pop();
  return { lines: lines.map(l => l.join('')), ok: lines.length <= GRID.rows };
}
// Rend la grille (8 x 12 cases) dans el. Les tailles viennent des variables CSS --cw / --ch / --fs.
function renderGrid(el, text) {
  const { lines } = wrapGrid(text || ''), shown = lines.slice(0, GRID.rows);
  const r0 = Math.floor((GRID.rows - shown.length) / 2);
  let h = '';
  for (let r = 0; r < GRID.rows; r++) {
    const l = [...(shown[r - r0] || '')], c0 = Math.floor((GRID.cols - l.length) / 2);
    for (let c = 0; c < GRID.cols; c++) h += `<span>${esc(l[c - c0] || '')}</span>`;
  }
  el.innerHTML = h;
}
(() => { const s = document.createElement('style'); s.textContent = `
.tgrid{display:grid;grid-template-columns:repeat(12,var(--cw));grid-template-rows:repeat(8,var(--ch));
  font-family:Menlo,Consolas,"DejaVu Sans Mono","Courier New",monospace;font-weight:700;font-size:var(--fs);line-height:1;color:inherit}
.tgrid span{display:flex;align-items:center;justify-content:center;overflow:visible}`;
  document.head.appendChild(s) })();
